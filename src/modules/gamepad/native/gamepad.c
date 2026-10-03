#include <math.h>
#include <stdio.h>
#include <string.h>
#include <timer.h>

#include <tamtypes.h>
#include <libpad.h>

#include <athena/debug.h>

#include <athena/gamepad.h>
#include "gamepad_iop.h"

/* Updates a configuration command may stay busy before it is abandoned. */
#define GAMEPAD_COMMAND_TIMEOUT 120
/* Updates to wait for padman to report the controller mode. */
#define GAMEPAD_DETECT_TIMEOUT 60
/* Updates between checks for multitaps and for DualShock 3/4 appearing. */
#define GAMEPAD_SCAN_INTERVAL 30
/*
 * Updates after start-up before controllers get players. Every scan runs at
 * least once in this window, so controllers present at boot are bound in
 * device-table order rather than in the order they happened to answer.
 */
#define GAMEPAD_STARTUP_UPDATES (GAMEPAD_SCAN_INTERVAL + 1)

#define GAMEPAD_AXIS_NEUTRAL 128
#define GAMEPAD_DEFAULT_DEADZONE 0.15f
#define GAMEPAD_MAX_DEADZONE 0.95f

#define GAMEPAD_PORTS 2
#define GAMEPAD_SLOTS 4
#define GAMEPAD_PORT_DEVICES (GAMEPAD_PORTS * GAMEPAD_SLOTS)
#define GAMEPAD_DEVICES (GAMEPAD_PORT_DEVICES + 2 * GAMEPAD_DS34_PADS)

enum { AXIS_RX, AXIS_RY, AXIS_LX, AXIS_LY };

/*
 * PS2 controllers are configured one step per update so a pad being plugged
 * in, or switching modes, never stalls the frame. `phase` is the next step to
 * run; `pending` is the command issued by the previous step, if any.
 */
typedef enum {
    GAMEPAD_PHASE_DETECT,
    GAMEPAD_PHASE_PRESSURE,
    GAMEPAD_PHASE_ACTUATORS,
    GAMEPAD_PHASE_READY,
} GamepadPhase;

typedef enum {
    GAMEPAD_PENDING_NONE,
    GAMEPAD_PENDING_MODE,
    GAMEPAD_PENDING_PRESSURE,
    GAMEPAD_PENDING_ACTUATORS,
} GamepadPending;

typedef struct {
    AthenaGamepadConnection connection;
    /* Port devices: libpad port/slot. DualShock 3/4: driver pad index. */
    int port;
    int slot;
    bool open;
    bool connected;
    int player;

    int type;
    bool type_known;
    int mode_id;

    uint16_t buttons;
    /* rx, ry, lx, ly: the order of padButtonStatus and of the ds34 drivers. */
    uint8_t axes[4];
    uint8_t pressure[12];
    bool pressure_valid;
    bool pressure_enabled;
    int actuators;

    /* PS2 controller configuration. */
    GamepadPhase phase;
    GamepadPending pending;
    int wait_updates;
    int detect_updates;
    int pending_actuators;
    bool requested_analog;
    bool requested_lock;

    /* Motor output requested by the player and last written to the device. */
    uint8_t want_strong;
    uint8_t want_weak;
    uint8_t applied_strong;
    uint8_t applied_weak;
} GamepadDevice;

typedef struct {
    int device;
    bool was_connected;
    uint16_t buttons;
    uint16_t previous;
    /* Time (ms) each button became held, for repeatPressed(). */
    uint64_t pressed_at[16];

    uint8_t rumble_strong;
    uint8_t rumble_weak;
    /* Time (ms) the motors stop; 0 when they run until changed. */
    uint64_t rumble_deadline;

    /* Preferences: stay with the player when controllers are swapped. */
    float deadzone;
    bool requested_analog;
    bool requested_lock;
} GamepadPlayer;

static char gamepad_buffers[GAMEPAD_PORT_DEVICES][256] __attribute__((aligned(64)));
static GamepadDevice gamepad_devices[GAMEPAD_DEVICES];
static GamepadPlayer gamepad_players[ATHENA_GAMEPAD_MAX_PLAYERS];
static bool gamepad_defaults_set;
static bool gamepad_initialized;
static unsigned int gamepad_updates;
/* Time (ms) of the last two updates, for repeatPressed(). */
static uint64_t gamepad_update_ms;
static uint64_t gamepad_previous_update_ms;

/*
 * Milliseconds from the EE's 64-bit bus-clock counter. clock() is a 32-bit
 * microsecond count on the EE and wraps every ~71 minutes.
 */
static uint64_t gamepad_now_ms(void) {
    return GetTimerSystemTime() / (kBUSCLK / 1000);
}

/* Optional drivers cost IOP memory: none is loaded until the program asks for it. */
static AthenaGamepadDrivers gamepad_enabled = { false, false, false };
/* Drivers that failed to start are not retried until re-enabled or an IOP reset. */
static bool gamepad_driver_failed[GAMEPAD_DRIVER_COUNT];
static bool gamepad_multitap[GAMEPAD_PORTS];
/* Set when the multitap setting changes, so slots follow it on the next update. */
static bool gamepad_force_scan;

static const uint16_t gamepad_pressure_buttons[12] = {
    PAD_RIGHT, PAD_LEFT, PAD_UP, PAD_DOWN,
    PAD_TRIANGLE, PAD_CIRCLE, PAD_CROSS, PAD_SQUARE,
    PAD_L1, PAD_R1, PAD_L2, PAD_R2,
};

/*
 * Device table: port 0 slots 0-3, port 1 slots 0-3, then the USB and the
 * Bluetooth DualShock 3/4. It is also the order in which new controllers get
 * players, so without multitaps port 1 comes before port 2.
 */
static int gamepad_port_device(int port, int slot) {
    return port * GAMEPAD_SLOTS + slot;
}

static GamepadDs34Bus gamepad_device_bus(const GamepadDevice *d) {
    return d->connection == ATHENA_GAMEPAD_CONNECTION_USB ?
        GAMEPAD_DS34_USB : GAMEPAD_DS34_BLUETOOTH;
}

static void gamepad_restart_configuration(GamepadDevice *d, GamepadPhase phase) {
    d->phase = phase;
    d->detect_updates = 0;
    d->pressure_enabled = false;
    d->pressure_valid = false;
    d->actuators = 0;
    d->applied_strong = 0;
    d->applied_weak = 0;
}

/* Clears everything learned from the controller. */
static void gamepad_forget_device(GamepadDevice *d) {
    gamepad_restart_configuration(d, GAMEPAD_PHASE_DETECT);
    d->connected = false;
    d->type = 0;
    d->type_known = false;
    d->mode_id = 0;
    d->pending = GAMEPAD_PENDING_NONE;
    d->wait_updates = 0;
    d->pending_actuators = 0;
    d->buttons = 0;
    memset(d->axes, GAMEPAD_AXIS_NEUTRAL, sizeof(d->axes));
    memset(d->pressure, 0, sizeof(d->pressure));
    d->want_strong = 0;
    d->want_weak = 0;
}

static void gamepad_set_defaults(void) {
    if (gamepad_defaults_set)
        return;

    for (int i = 0; i < GAMEPAD_DEVICES; i++) {
        GamepadDevice *d = &gamepad_devices[i];
        memset(d, 0, sizeof(*d));
        d->player = -1;
        if (i < GAMEPAD_PORT_DEVICES) {
            d->connection = ATHENA_GAMEPAD_CONNECTION_PORT;
            d->port = i / GAMEPAD_SLOTS;
            d->slot = i % GAMEPAD_SLOTS;
        } else {
            int ds34 = i - GAMEPAD_PORT_DEVICES;
            d->connection = ds34 < GAMEPAD_DS34_PADS ?
                ATHENA_GAMEPAD_CONNECTION_USB : ATHENA_GAMEPAD_CONNECTION_BLUETOOTH;
            d->port = ds34 % GAMEPAD_DS34_PADS;
        }
        d->requested_analog = true;
        d->requested_lock = true;
        gamepad_forget_device(d);
    }

    for (int i = 0; i < ATHENA_GAMEPAD_MAX_PLAYERS; i++) {
        GamepadPlayer *p = &gamepad_players[i];
        memset(p, 0, sizeof(*p));
        p->device = -1;
        p->deadzone = GAMEPAD_DEFAULT_DEADZONE;
        p->requested_analog = true;
        p->requested_lock = true;
    }
    gamepad_defaults_set = true;
}

/* ---- Motors ------------------------------------------------------------ */

/* Byte 0 drives the small (on/off) motor and byte 1 the big one, per act_align. */
static int gamepad_write_pad_motors(const GamepadDevice *d, uint8_t strong, uint8_t weak) {
    char act_direct[6] = { weak ? 1 : 0, (char)strong, 0, 0, 0, 0 };
    return padSetActDirect(d->port, d->slot, act_direct);
}

static void gamepad_apply_motors(GamepadDevice *d) {
    bool ok;

    if (d->want_strong == d->applied_strong && d->want_weak == d->applied_weak)
        return;

    if (d->connection == ATHENA_GAMEPAD_CONNECTION_PORT) {
        if (d->actuators <= 0 || d->phase != GAMEPAD_PHASE_READY ||
            d->pending != GAMEPAD_PENDING_NONE)
            return;
        ok = gamepad_write_pad_motors(d, d->want_strong, d->want_weak) == 1;
    } else {
        /* The DualShock 3 small motor is on/off; the DualShock 4 has levels. */
        uint8_t weak = d->type == ATHENA_GAMEPAD_TYPE_DUALSHOCK4 ?
            d->want_weak : (d->want_weak ? 1 : 0);
        ok = gamepad_ds34_rumble(gamepad_device_bus(d), d->port, d->want_strong, weak);
    }

    if (ok) {
        d->applied_strong = d->want_strong;
        d->applied_weak = d->want_weak;
    }
}

/* ---- PS2 controllers ------------------------------------------------------ */

static void gamepad_issue(GamepadDevice *d, int result, GamepadPending pending) {
    if (result == 1) {
        d->pending = pending;
        d->wait_updates = 0;
    }
}

static void gamepad_finish_pending(GamepadDevice *d, bool ok) {
    if (ok && d->pending == GAMEPAD_PENDING_PRESSURE)
        d->pressure_enabled = true;
    if (ok && d->pending == GAMEPAD_PENDING_ACTUATORS)
        d->actuators = d->pending_actuators;
    d->pending = GAMEPAD_PENDING_NONE;
}

static bool gamepad_supports_dualshock(const GamepadDevice *d) {
    int modes = padInfoMode(d->port, d->slot, PAD_MODETABLE, -1);

    for (int i = 0; i < modes; i++) {
        if (padInfoMode(d->port, d->slot, PAD_MODETABLE, i) == PAD_TYPE_DUALSHOCK)
            return true;
    }
    return false;
}

/*
 * padman reports mode info as 0 until it has (re)read the controller
 * configuration, which also happens for a few updates after a mode change.
 * Returns false while the step should wait; gives up after a timeout.
 */
static bool gamepad_info_ready(GamepadDevice *d) {
    if (padInfoMode(d->port, d->slot, PAD_MODECURID, 0) == 0 &&
        ++d->detect_updates < GAMEPAD_DETECT_TIMEOUT)
        return false;
    d->detect_updates = 0;
    return true;
}

/* Runs at most one configuration step. Only called while the pad is idle. */
static void gamepad_configure(GamepadDevice *d) {
    int port = d->port, slot = d->slot;

    if (d->pending != GAMEPAD_PENDING_NONE) {
        int req = padGetReqState(port, slot);
        if (req == PAD_RSTAT_BUSY && ++d->wait_updates < GAMEPAD_COMMAND_TIMEOUT)
            return;
        if (req != PAD_RSTAT_COMPLETE)
            dbgprintf("[Gamepad] pad %d.%d: command %d did not complete (%d)\n",
                port, slot, d->pending, req);
        gamepad_finish_pending(d, req == PAD_RSTAT_COMPLETE);
        return;
    }

    switch (d->phase) {
    case GAMEPAD_PHASE_DETECT: {
        bool dualshock;

        if (!gamepad_info_ready(d))
            return;

        dualshock = gamepad_supports_dualshock(d);
        d->type = dualshock ? PAD_TYPE_DUALSHOCK : padInfoMode(port, slot, PAD_MODECURID, 0);
        d->type_known = true;
        d->phase = GAMEPAD_PHASE_PRESSURE;

        if (padInfoMode(port, slot, PAD_MODETABLE, -1) <= 0) {
            /* Digital-only controllers have no mode table nor actuators. */
            d->phase = GAMEPAD_PHASE_READY;
            return;
        }
        if (d->requested_analog && !dualshock)
            return;

        gamepad_issue(d, padSetMainMode(port, slot,
            d->requested_analog ? PAD_MMODE_DUALSHOCK : PAD_MMODE_DIGITAL,
            d->requested_lock ? PAD_MMODE_LOCK : PAD_MMODE_UNLOCK),
            GAMEPAD_PENDING_MODE);
        return;
    }

    case GAMEPAD_PHASE_PRESSURE:
        if (!gamepad_info_ready(d))
            return;
        d->phase = GAMEPAD_PHASE_ACTUATORS;
        if (padInfoMode(port, slot, PAD_MODECURID, 0) == PAD_TYPE_DUALSHOCK &&
            padInfoPressMode(port, slot) == 1)
            gamepad_issue(d, padEnterPressMode(port, slot), GAMEPAD_PENDING_PRESSURE);
        return;

    case GAMEPAD_PHASE_ACTUATORS: {
        static const char act_align[6] = { 0, 1, 0xff, 0xff, 0xff, 0xff };
        int actuators;

        if (!gamepad_info_ready(d))
            return;
        actuators = padInfoAct(port, slot, -1, 0);
        d->phase = GAMEPAD_PHASE_READY;
        if (actuators > 0) {
            d->pending_actuators = actuators;
            gamepad_issue(d, padSetActAlign(port, slot, act_align),
                GAMEPAD_PENDING_ACTUATORS);
        }
        return;
    }

    case GAMEPAD_PHASE_READY:
        gamepad_apply_motors(d);
        return;
    }
}

static void gamepad_read_pad(GamepadDevice *d) {
    struct padButtonStatus status;
    unsigned char length = padRead(d->port, d->slot, &status);
    int kind;

    if (length == 0)
        return;

    d->buttons = 0xffff ^ status.btns;

    kind = status.mode >> 4;
    if (length >= 8 && (kind == PAD_TYPE_ANALOG || kind == PAD_TYPE_DUALSHOCK))
        memcpy(d->axes, &status.rjoy_h, sizeof(d->axes));
    else
        memset(d->axes, GAMEPAD_AXIS_NEUTRAL, sizeof(d->axes));

    d->pressure_valid = d->pressure_enabled && length >= 20;
    if (d->pressure_valid)
        memcpy(d->pressure, &status.right_p, sizeof(d->pressure));
}

static void gamepad_track_mode(GamepadDevice *d, int mode_id) {
    if (!mode_id || mode_id == d->mode_id)
        return;

    /*
     * The player switched to analog with the ANALOG button (unlocked mode):
     * the pad left pressure mode and needs its motors remapped.
     */
    if (d->mode_id && mode_id == PAD_TYPE_DUALSHOCK &&
        d->phase == GAMEPAD_PHASE_READY && d->pending == GAMEPAD_PENDING_NONE)
        gamepad_restart_configuration(d, GAMEPAD_PHASE_PRESSURE);

    d->mode_id = mode_id;
    if (!d->type_known)
        d->type = mode_id;
}

static void gamepad_poll_pad(GamepadDevice *d) {
    int state = padGetState(d->port, d->slot);

    if (state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1) {
        /*
         * Configuration starts on the next update, once the pad is bound to a
         * player and has received that player's mode preference.
         */
        if (d->connected)
            gamepad_configure(d);
        d->connected = true;
        /* Mode info is unavailable while a command is in flight. */
        gamepad_track_mode(d, padInfoMode(d->port, d->slot, PAD_MODECURID, 0));
        gamepad_read_pad(d);
    } else if (state == PAD_STATE_EXECCMD && d->connected) {
        /* A configuration command is running; hold the last known input. */
    } else if (d->connected || d->phase != GAMEPAD_PHASE_DETECT) {
        gamepad_forget_device(d);
    }
}

/* ---- Multitap ------------------------------------------------------------ */

/*
 * Multitap slots are opened the first time a multitap shows up and then stay
 * open: libpad's padPortClose() waits for padman's vblank-driven thread (one
 * or two frames per slot), and reopening an open slot closes it the same way.
 * While the multitap is absent or disabled the slots are simply not polled.
 */
static void gamepad_release_multitap_slots(int port) {
    for (int slot = 1; slot < GAMEPAD_SLOTS; slot++)
        gamepad_forget_device(&gamepad_devices[gamepad_port_device(port, slot)]);
}

static void gamepad_scan_multitaps(void) {
    bool ready = gamepad_iop_ready(GAMEPAD_DRIVER_MTAPMAN) && gamepad_enabled.multitap;

    for (int port = 0; port < GAMEPAD_PORTS; port++) {
        bool present = ready && gamepad_mtap_connected(port);

        if (present == gamepad_multitap[port])
            continue;
        gamepad_multitap[port] = present;

        if (!present) {
            gamepad_release_multitap_slots(port);
            continue;
        }
        for (int slot = 1; slot < GAMEPAD_SLOTS; slot++) {
            int device = gamepad_port_device(port, slot);
            GamepadDevice *d = &gamepad_devices[device];
            if (d->open)
                continue;
            d->open = padPortOpen(port, slot, gamepad_buffers[device]) == 1;
            if (!d->open)
                dbgprintf("[Gamepad] padPortOpen(%d, %d) failed\n", port, slot);
        }
        dbgprintf("[Gamepad] multitap connected on port %d\n", port);
    }
}

/* ---- DualShock 3/4 ------------------------------------------------------- */

static void gamepad_ds34_apply(GamepadDevice *d, const uint8_t data[18], int status) {
    d->connected = true;
    d->type = status & GAMEPAD_DS34_DS4 ?
        ATHENA_GAMEPAD_TYPE_DUALSHOCK4 : ATHENA_GAMEPAD_TYPE_DUALSHOCK3;
    d->type_known = true;
    d->buttons = 0xffff ^ (data[0] | (data[1] << 8));
    memcpy(d->axes, &data[2], sizeof(d->axes));
    memcpy(d->pressure, &data[6], sizeof(d->pressure));
    /* Pressure bytes are real on the DualShock 3; the DualShock 4 only has analog L2/R2. */
    d->pressure_valid = true;
    d->pressure_enabled = d->type == ATHENA_GAMEPAD_TYPE_DUALSHOCK3;
    d->actuators = 2;
}

static void gamepad_poll_ds34(GamepadDevice *d, int index) {
    GamepadDs34Bus bus = gamepad_device_bus(d);
    uint8_t data[18];
    int status;

    if (!gamepad_iop_ready(bus == GAMEPAD_DS34_USB ?
            GAMEPAD_DRIVER_DS34USB : GAMEPAD_DRIVER_DS34BT) ||
        !(bus == GAMEPAD_DS34_USB ? gamepad_enabled.usb : gamepad_enabled.bluetooth)) {
        if (d->connected)
            gamepad_forget_device(d);
        return;
    }

    /* Idle slots are only checked now and then; each check is an RPC. */
    if (!d->connected &&
        (gamepad_updates + index) % GAMEPAD_SCAN_INTERVAL != 0)
        return;
    if (!d->connected && !(gamepad_ds34_status(bus, d->port) & GAMEPAD_DS34_RUNNING))
        return;

    if (!gamepad_ds34_read(bus, d->port, data, &status) ||
        !(status & GAMEPAD_DS34_RUNNING)) {
        if (d->connected)
            gamepad_forget_device(d);
        return;
    }
    gamepad_ds34_apply(d, data, status);
    gamepad_apply_motors(d);
}

/* ---- Service lifecycle --------------------------------------------------- */

static int gamepad_on_iop_reset(void *module) {
    (void)module;
    if (!gamepad_initialized)
        return 0;

    /*
     * Ports are not closed and padEnd() is not called: both wait for padman's
     * thread and the reset discards padman's state anyway. libpad forgets its
     * own state when padInit() sees the new IOP reboot count.
     */
    for (int i = 0; i < GAMEPAD_DEVICES; i++) {
        GamepadDevice *d = &gamepad_devices[i];
        if (d->connected && (d->applied_strong || d->applied_weak)) {
            d->want_strong = 0;
            d->want_weak = 0;
            gamepad_apply_motors(d);
        }
        d->open = false;
        /* Players notice the loss on the next update. */
        gamepad_forget_device(d);
    }
    memset(gamepad_driver_failed, 0, sizeof(gamepad_driver_failed));
    memset(gamepad_multitap, 0, sizeof(gamepad_multitap));
    gamepad_initialized = false;
    dbgprintf("[Gamepad] IOP reset; service released\n");
    return 0;
}

AthenaGamepadResult athena_gamepad_core_init(void) {
    int opened = 0;

    gamepad_set_defaults();
    if (gamepad_initialized)
        return ATHENA_GAMEPAD_OK;

    if (!gamepad_iop_start(GAMEPAD_DRIVER_PADMAN, gamepad_on_iop_reset))
        return ATHENA_GAMEPAD_ERR_IOP;

    /* 0 means libpad was already bound in this IOP session; < 0 is an RPC failure. */
    if (padInit(0) < 0)
        return ATHENA_GAMEPAD_ERR_RPC;

    for (int port = 0; port < GAMEPAD_PORTS; port++) {
        GamepadDevice *d = &gamepad_devices[gamepad_port_device(port, 0)];
        d->open = padPortOpen(port, 0, gamepad_buffers[gamepad_port_device(port, 0)]) == 1;
        if (d->open)
            opened++;
        else
            dbgprintf("[Gamepad] padPortOpen(%d, 0) failed\n", port);
    }
    if (!opened) {
        padEnd();
        return ATHENA_GAMEPAD_ERR_RPC;
    }

    gamepad_initialized = true;
    gamepad_updates = 0;
    dbgprintf("[Gamepad] ready\n");
    return ATHENA_GAMEPAD_OK;
}

uint16_t athena_gamepad_core_peek(int port) {
    struct padButtonStatus status;
    int state;

    if (!gamepad_initialized || port < 0 || port >= GAMEPAD_PORTS ||
        !gamepad_devices[gamepad_port_device(port, 0)].open)
        return 0;
    state = padGetState(port, 0);
    if (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1)
        return 0;
    if (padRead(port, 0, &status) == 0)
        return 0;
    return 0xffff ^ status.btns;
}

static void gamepad_start_driver(GamepadDriver driver, bool enabled) {
    if (!enabled || gamepad_iop_ready(driver) || gamepad_driver_failed[driver])
        return;
    if (!gamepad_iop_start(driver, NULL)) {
        gamepad_driver_failed[driver] = true;
        return;
    }
    if (driver == GAMEPAD_DRIVER_MTAPMAN) {
        for (int port = 0; port < GAMEPAD_PORTS; port++)
            gamepad_mtap_open(port);
    }
}

/* ---- Players ------------------------------------------------------------- */

/* Gives a PS2 controller the mode preference of the player it now belongs to. */
static void gamepad_apply_player_mode(GamepadDevice *d, const GamepadPlayer *p) {
    if (d->connection != ATHENA_GAMEPAD_CONNECTION_PORT ||
        (d->requested_analog == p->requested_analog &&
         d->requested_lock == p->requested_lock))
        return;
    d->requested_analog = p->requested_analog;
    d->requested_lock = p->requested_lock;
    gamepad_restart_configuration(d, GAMEPAD_PHASE_DETECT);
}

static void gamepad_bind(int device, int player) {
    GamepadDevice *d = &gamepad_devices[device];
    GamepadPlayer *p = &gamepad_players[player];

    d->player = player;
    p->device = device;
    p->rumble_strong = 0;
    p->rumble_weak = 0;
    p->rumble_deadline = 0;
    gamepad_apply_player_mode(d, p);
}

void athena_gamepad_core_swap_players(int a, int b) {
    GamepadPlayer *pa = &gamepad_players[a];
    GamepadPlayer *pb = &gamepad_players[b];
    GamepadPlayer swapped;

    gamepad_set_defaults();
    if (a == b)
        return;

    /* Controller state (binding, buttons, edges, rumble) moves; preferences stay. */
    swapped = *pa;
    *pa = *pb;
    *pb = swapped;
    pb->deadzone = pa->deadzone;
    pb->requested_analog = pa->requested_analog;
    pb->requested_lock = pa->requested_lock;
    pa->deadzone = swapped.deadzone;
    pa->requested_analog = swapped.requested_analog;
    pa->requested_lock = swapped.requested_lock;

    if (pa->device >= 0) {
        gamepad_devices[pa->device].player = a;
        gamepad_apply_player_mode(&gamepad_devices[pa->device], pa);
    }
    if (pb->device >= 0) {
        gamepad_devices[pb->device].player = b;
        gamepad_apply_player_mode(&gamepad_devices[pb->device], pb);
    }
}

static void gamepad_update_players(void) {
    /* Release players whose controller went away. */
    for (int i = 0; i < ATHENA_GAMEPAD_MAX_PLAYERS; i++) {
        GamepadPlayer *p = &gamepad_players[i];
        if (p->device >= 0 && !gamepad_devices[p->device].connected) {
            gamepad_devices[p->device].player = -1;
            p->device = -1;
        }
    }

    /* New controllers take the lowest free player, in device table order. */
    for (int device = 0; gamepad_updates >= GAMEPAD_STARTUP_UPDATES &&
            device < GAMEPAD_DEVICES; device++) {
        if (!gamepad_devices[device].connected || gamepad_devices[device].player >= 0)
            continue;
        for (int i = 0; i < ATHENA_GAMEPAD_MAX_PLAYERS; i++) {
            if (gamepad_players[i].device < 0) {
                gamepad_bind(device, i);
                break;
            }
        }
    }

    gamepad_previous_update_ms = gamepad_update_ms;
    gamepad_update_ms = gamepad_now_ms();

    for (int i = 0; i < ATHENA_GAMEPAD_MAX_PLAYERS; i++) {
        GamepadPlayer *p = &gamepad_players[i];
        GamepadDevice *d = p->device >= 0 ? &gamepad_devices[p->device] : NULL;
        uint16_t pressed;

        p->previous = p->buttons;
        p->buttons = d ? d->buttons : 0;
        pressed = p->buttons & ~p->previous;
        for (int bit = 0; pressed; bit++, pressed >>= 1) {
            if (pressed & 1)
                p->pressed_at[bit] = gamepad_update_ms;
        }
        if (!d)
            continue;

        if (p->rumble_deadline && gamepad_update_ms >= p->rumble_deadline) {
            p->rumble_strong = 0;
            p->rumble_weak = 0;
            p->rumble_deadline = 0;
        }
        d->want_strong = p->rumble_strong;
        d->want_weak = p->rumble_weak;
        gamepad_apply_motors(d);
    }
}

AthenaGamepadResult athena_gamepad_core_update(void) {
    AthenaGamepadResult result = athena_gamepad_core_init();

    for (int i = 0; i < ATHENA_GAMEPAD_MAX_PLAYERS; i++)
        gamepad_players[i].was_connected = gamepad_players[i].device >= 0;

    if (result != ATHENA_GAMEPAD_OK)
        return result;

    gamepad_start_driver(GAMEPAD_DRIVER_MTAPMAN, gamepad_enabled.multitap);
    gamepad_start_driver(GAMEPAD_DRIVER_DS34USB, gamepad_enabled.usb);
    gamepad_start_driver(GAMEPAD_DRIVER_DS34BT, gamepad_enabled.bluetooth);

    if (gamepad_force_scan || gamepad_updates % GAMEPAD_SCAN_INTERVAL == 0) {
        gamepad_force_scan = false;
        gamepad_scan_multitaps();
    }

    for (int i = 0; i < GAMEPAD_DEVICES; i++) {
        GamepadDevice *d = &gamepad_devices[i];
        if (d->connection == ATHENA_GAMEPAD_CONNECTION_PORT) {
            /* Multitap slots stay open without a multitap; skip them then. */
            if (d->open && (d->slot == 0 || gamepad_multitap[d->port]))
                gamepad_poll_pad(d);
        } else {
            gamepad_poll_ds34(d, i);
        }
    }

    gamepad_update_players();
    gamepad_updates++;
    return ATHENA_GAMEPAD_OK;
}

/* ---- Drivers ------------------------------------------------------------- */

void athena_gamepad_core_set_drivers(AthenaGamepadDrivers enabled) {
    if (enabled.multitap && !gamepad_enabled.multitap)
        gamepad_driver_failed[GAMEPAD_DRIVER_MTAPMAN] = false;
    if (enabled.usb && !gamepad_enabled.usb)
        gamepad_driver_failed[GAMEPAD_DRIVER_DS34USB] = false;
    if (enabled.bluetooth && !gamepad_enabled.bluetooth)
        gamepad_driver_failed[GAMEPAD_DRIVER_DS34BT] = false;
    if (enabled.multitap != gamepad_enabled.multitap)
        gamepad_force_scan = true;
    gamepad_enabled = enabled;
}

AthenaGamepadDrivers athena_gamepad_core_drivers_enabled(void) {
    return gamepad_enabled;
}

AthenaGamepadDrivers athena_gamepad_core_drivers_ready(void) {
    AthenaGamepadDrivers ready = {
        gamepad_enabled.multitap && gamepad_iop_ready(GAMEPAD_DRIVER_MTAPMAN),
        gamepad_enabled.usb && gamepad_iop_ready(GAMEPAD_DRIVER_DS34USB),
        gamepad_enabled.bluetooth && gamepad_iop_ready(GAMEPAD_DRIVER_DS34BT),
    };
    return ready;
}

bool athena_gamepad_core_multitap(int port) {
    return gamepad_multitap[port];
}

bool athena_gamepad_core_bluetooth_adapter(void) {
    uint8_t address[6];

    return gamepad_enabled.bluetooth && gamepad_ds34_adapter_address(address);
}

/* ---- Player queries ------------------------------------------------------ */

static const GamepadDevice *gamepad_player_device(int player) {
    gamepad_set_defaults();
    int device = gamepad_players[player].device;
    return device >= 0 ? &gamepad_devices[device] : NULL;
}

bool athena_gamepad_core_connected(int player) {
    return gamepad_player_device(player) != NULL;
}

bool athena_gamepad_core_just_connected(int player) {
    return gamepad_player_device(player) && !gamepad_players[player].was_connected;
}

bool athena_gamepad_core_just_disconnected(int player) {
    return !gamepad_player_device(player) && gamepad_players[player].was_connected;
}

AthenaGamepadConnection athena_gamepad_core_connection(int player) {
    const GamepadDevice *d = gamepad_player_device(player);
    return d ? d->connection : ATHENA_GAMEPAD_CONNECTION_NONE;
}

int athena_gamepad_core_port(int player) {
    const GamepadDevice *d = gamepad_player_device(player);
    return d && d->connection == ATHENA_GAMEPAD_CONNECTION_PORT ? d->port : -1;
}

int athena_gamepad_core_slot(int player) {
    const GamepadDevice *d = gamepad_player_device(player);
    return d && d->connection == ATHENA_GAMEPAD_CONNECTION_PORT ? d->slot : -1;
}

int athena_gamepad_core_type(int player) {
    const GamepadDevice *d = gamepad_player_device(player);
    return d ? d->type : 0;
}

bool athena_gamepad_core_analog(int player) {
    const GamepadDevice *d = gamepad_player_device(player);

    if (!d)
        return false;
    if (d->connection != ATHENA_GAMEPAD_CONNECTION_PORT)
        return true;
    return d->mode_id == PAD_TYPE_ANALOG || d->mode_id == PAD_TYPE_DUALSHOCK;
}

uint16_t athena_gamepad_core_buttons(int player) {
    gamepad_set_defaults();
    return gamepad_players[player].buttons;
}

uint16_t athena_gamepad_core_previous_buttons(int player) {
    gamepad_set_defaults();
    return gamepad_players[player].previous;
}

bool athena_gamepad_core_pressed(int player, uint16_t mask) {
    return (athena_gamepad_core_buttons(player) & mask) == mask;
}

bool athena_gamepad_core_just_pressed(int player, uint16_t mask) {
    const GamepadPlayer *p = &gamepad_players[player];
    gamepad_set_defaults();
    return (p->buttons & mask) == mask && (p->previous & mask) != mask;
}

bool athena_gamepad_core_just_released(int player, uint16_t mask) {
    const GamepadPlayer *p = &gamepad_players[player];
    gamepad_set_defaults();
    return (p->buttons & mask) == 0 && (p->previous & mask) != 0;
}

bool athena_gamepad_core_any_pressed(int player, uint16_t mask) {
    return (athena_gamepad_core_buttons(player) & mask) != 0;
}

bool athena_gamepad_core_any_just_pressed(int player, uint16_t mask) {
    const GamepadPlayer *p = &gamepad_players[player];
    gamepad_set_defaults();
    return (p->buttons & ~p->previous & mask) != 0;
}

bool athena_gamepad_core_repeat_pressed(int player, uint16_t mask,
    uint32_t delay_ms, uint32_t interval_ms) {
    const GamepadPlayer *p = &gamepad_players[player];
    uint16_t held;

    gamepad_set_defaults();
    if (athena_gamepad_core_any_just_pressed(player, mask))
        return true;

    /*
     * A button held since `t` repeats at t + delay + k * interval. The update
     * fires when one of those instants falls in (previous update, this one].
     */
    held = p->buttons & p->previous & mask;
    for (int bit = 0; held; bit++, held >>= 1) {
        uint64_t first;

        if (!(held & 1))
            continue;
        first = p->pressed_at[bit] + delay_ms;
        if (gamepad_update_ms < first)
            continue;
        if (gamepad_previous_update_ms < first)
            return true;
        if ((gamepad_update_ms - first) / interval_ms !=
            (gamepad_previous_update_ms - first) / interval_ms)
            return true;
    }
    return false;
}

AthenaGamepadDpad athena_gamepad_core_dpad(int player) {
    uint16_t buttons = athena_gamepad_core_buttons(player);
    AthenaGamepadDpad dpad = {
        ((buttons & PAD_RIGHT) != 0) - ((buttons & PAD_LEFT) != 0),
        ((buttons & PAD_DOWN) != 0) - ((buttons & PAD_UP) != 0),
    };
    return dpad;
}

static float gamepad_normalize_axis(uint8_t raw) {
    int value = (int)raw - GAMEPAD_AXIS_NEUTRAL;
    return value < 0 ? value / 128.0f : value / 127.0f;
}

AthenaGamepadStick athena_gamepad_core_stick(int player, bool right) {
    const GamepadDevice *d = gamepad_player_device(player);
    float deadzone = gamepad_players[player].deadzone;
    AthenaGamepadStick stick = { 0.0f, 0.0f };
    float magnitude, scale;

    if (!d)
        return stick;

    stick.x = gamepad_normalize_axis(d->axes[right ? AXIS_RX : AXIS_LX]);
    stick.y = gamepad_normalize_axis(d->axes[right ? AXIS_RY : AXIS_LY]);
    magnitude = sqrtf(stick.x * stick.x + stick.y * stick.y);

    if (magnitude <= deadzone || magnitude == 0.0f) {
        stick.x = 0.0f;
        stick.y = 0.0f;
        return stick;
    }

    /* Rescale so the output starts at 0 at the dead-zone edge. */
    scale = (magnitude - deadzone) / (1.0f - deadzone);
    if (scale > 1.0f)
        scale = 1.0f;
    stick.x = stick.x / magnitude * scale;
    stick.y = stick.y / magnitude * scale;
    return stick;
}

float athena_gamepad_core_deadzone(int player) {
    gamepad_set_defaults();
    return gamepad_players[player].deadzone;
}

void athena_gamepad_core_set_deadzone(int player, float deadzone) {
    gamepad_set_defaults();
    if (deadzone < 0.0f)
        deadzone = 0.0f;
    if (deadzone > GAMEPAD_MAX_DEADZONE)
        deadzone = GAMEPAD_MAX_DEADZONE;
    gamepad_players[player].deadzone = deadzone;
}

uint8_t athena_gamepad_core_pressure(int player, uint16_t button) {
    const GamepadDevice *d = gamepad_player_device(player);
    bool held = (gamepad_players[player].buttons & button) != 0;

    if (d && d->pressure_valid) {
        for (int i = 0; i < 12; i++) {
            if (gamepad_pressure_buttons[i] == button)
                return held ? d->pressure[i] : 0;
        }
    }
    return held ? 255 : 0;
}

bool athena_gamepad_core_has_pressure(int player) {
    const GamepadDevice *d = gamepad_player_device(player);
    return d && d->pressure_enabled;
}

bool athena_gamepad_core_has_rumble(int player) {
    const GamepadDevice *d = gamepad_player_device(player);
    return d && d->actuators > 0;
}

void athena_gamepad_core_rumble(int player, uint8_t strong, uint8_t weak,
    uint32_t duration_ms) {
    GamepadPlayer *p = &gamepad_players[player];
    GamepadDevice *d;

    gamepad_set_defaults();
    if (p->device < 0)
        return;
    d = &gamepad_devices[p->device];

    p->rumble_strong = strong;
    p->rumble_weak = weak;
    p->rumble_deadline = 0;
    /* duration_ms > 0, so the deadline is never 0 ("no deadline"). */
    if (duration_ms && (strong || weak))
        p->rumble_deadline = gamepad_now_ms() + duration_ms;

    /* Apply now when possible so the motors react within the same frame. */
    d->want_strong = strong;
    d->want_weak = weak;
    if (gamepad_initialized)
        gamepad_apply_motors(d);
}

void athena_gamepad_core_set_analog(int player, bool analog, bool lock) {
    GamepadPlayer *p = &gamepad_players[player];

    gamepad_set_defaults();
    p->requested_analog = analog;
    p->requested_lock = lock;
    if (p->device < 0)
        return;

    GamepadDevice *d = &gamepad_devices[p->device];
    if (d->connection != ATHENA_GAMEPAD_CONNECTION_PORT)
        return;
    d->requested_analog = analog;
    d->requested_lock = lock;
    gamepad_restart_configuration(d, GAMEPAD_PHASE_DETECT);
}

AthenaGamepadPairResult athena_gamepad_core_pair_bluetooth(int player) {
    const GamepadDevice *d = gamepad_player_device(player);
    uint8_t address[6];

    if (!d || d->connection != ATHENA_GAMEPAD_CONNECTION_USB)
        return ATHENA_GAMEPAD_PAIR_NOT_USB;
    if (!gamepad_ds34_adapter_address(address))
        return ATHENA_GAMEPAD_PAIR_NO_ADAPTER;
    return gamepad_ds34_pair(d->port, address) ?
        ATHENA_GAMEPAD_PAIR_OK : ATHENA_GAMEPAD_PAIR_FAILED;
}
