#ifndef ATH_NATIVE_GAMEPAD_H
#define ATH_NATIVE_GAMEPAD_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Controllers are physical devices (two ports with up to four multitap slots
 * each, two DualShock 3/4 over USB and two over Bluetooth). Players are
 * logical: a device that connects takes the lowest free player and keeps it
 * until it disconnects. All player functions take a player index.
 */
#define ATHENA_GAMEPAD_MAX_PLAYERS 8

/* Device types beyond libpad's PAD_TYPE_* values. */
#define ATHENA_GAMEPAD_TYPE_DUALSHOCK3 0x1003
#define ATHENA_GAMEPAD_TYPE_DUALSHOCK4 0x1004

typedef enum {
    ATHENA_GAMEPAD_OK = 0,
    /* padman is not registered in the IOP module registry. */
    ATHENA_GAMEPAD_ERR_UNAVAILABLE = -1,
    /* padman (or one of its dependencies) failed to load on the IOP. */
    ATHENA_GAMEPAD_ERR_IOP = -2,
    /* libpad could not bind to the padman RPC server. */
    ATHENA_GAMEPAD_ERR_RPC = -3,
} AthenaGamepadResult;

typedef enum {
    ATHENA_GAMEPAD_CONNECTION_NONE,
    ATHENA_GAMEPAD_CONNECTION_PORT,
    ATHENA_GAMEPAD_CONNECTION_USB,
    ATHENA_GAMEPAD_CONNECTION_BLUETOOTH,
} AthenaGamepadConnection;

typedef enum {
    ATHENA_GAMEPAD_PAIR_OK,
    ATHENA_GAMEPAD_PAIR_NOT_USB,
    ATHENA_GAMEPAD_PAIR_NO_ADAPTER,
    ATHENA_GAMEPAD_PAIR_FAILED,
} AthenaGamepadPairResult;

/* Optional drivers, all disabled by default. padman is always used. */
typedef struct {
    bool multitap;
    bool usb;
    bool bluetooth;
} AthenaGamepadDrivers;

typedef struct {
    float x;
    float y;
} AthenaGamepadStick;

/*
 * Loads padman when needed, binds libpad and opens both ports. Safe to call
 * every frame. After an IOP reset the next call starts everything again.
 */
AthenaGamepadResult athena_gamepad_core_init(void);

/*
 * Polls every device, binds new controllers to players and captures this
 * frame's snapshot. Must be called once per frame. Enabled optional drivers
 * are loaded here the first time.
 */
AthenaGamepadResult athena_gamepad_core_update(void);

/* Selects the optional drivers; takes effect on the next update. */
void athena_gamepad_core_set_drivers(AthenaGamepadDrivers enabled);
AthenaGamepadDrivers athena_gamepad_core_drivers_enabled(void);
/* Drivers that are loaded and answering. */
AthenaGamepadDrivers athena_gamepad_core_drivers_ready(void);
/* A multitap is plugged into `port` (0 or 1). */
bool athena_gamepad_core_multitap(int port);

bool athena_gamepad_core_connected(int player);
/* The controller was bound to / released from the player at the last update. */
bool athena_gamepad_core_just_connected(int player);
bool athena_gamepad_core_just_disconnected(int player);

AthenaGamepadConnection athena_gamepad_core_connection(int player);
/* Controller port (0-1) and multitap slot (0-3), or -1 when not on a port. */
int athena_gamepad_core_port(int player);
int athena_gamepad_core_slot(int player);

/*
 * Kind of device (a PAD_TYPE_* value or ATHENA_GAMEPAD_TYPE_*, 0 when none).
 * A DualShock 2 keeps reporting PAD_TYPE_DUALSHOCK while in digital mode.
 */
int athena_gamepad_core_type(int player);
/* The controller is in an analog mode, so its sticks are live. */
bool athena_gamepad_core_analog(int player);

uint16_t athena_gamepad_core_buttons(int player);
uint16_t athena_gamepad_core_previous_buttons(int player);

/* All bits of `mask` are held this frame. */
bool athena_gamepad_core_pressed(int player, uint16_t mask);
/* All bits of `mask` are held this frame and at least one was not held last frame. */
bool athena_gamepad_core_just_pressed(int player, uint16_t mask);
/* At least one bit of `mask` was released this frame and none is held. */
bool athena_gamepad_core_just_released(int player, uint16_t mask);

/* Normalized stick in [-1, 1] with the player's radial dead zone applied. */
AthenaGamepadStick athena_gamepad_core_stick(int player, bool right);

float athena_gamepad_core_deadzone(int player);
void athena_gamepad_core_set_deadzone(int player, float deadzone);

/*
 * Pressure of a single button in [0, 255]. Buttons without a pressure sensor
 * report 255 while held and 0 otherwise.
 */
uint8_t athena_gamepad_core_pressure(int player, uint16_t button);
/* Face, shoulder and d-pad buttons report real pressure. */
bool athena_gamepad_core_has_pressure(int player);

bool athena_gamepad_core_has_rumble(int player);
/*
 * Drives the big motor at `strong` and the small one at `weak` (0-255). The
 * small motor of DualShock 2 and 3 only turns on or off. Stops after
 * `duration_ms` (0 = until changed); cleared when the controller disconnects.
 * Ignored when no controller is bound to the player.
 */
void athena_gamepad_core_rumble(int player, uint8_t strong, uint8_t weak,
    uint32_t duration_ms);

/*
 * Requests analog or digital mode for PS2 controllers bound to the player.
 * With `lock`, the ANALOG button cannot change it. Applied over the next
 * updates and whenever a controller binds to the player. DualShock 3/4 are
 * always analog.
 */
void athena_gamepad_core_set_analog(int player, bool analog, bool lock);

/*
 * Makes the USB-connected DualShock 3/4 bound to the player connect to the
 * Bluetooth adapter when unplugged. Blocks for the USB transfer.
 */
AthenaGamepadPairResult athena_gamepad_core_pair_bluetooth(int player);

#endif /* ATH_NATIVE_GAMEPAD_H */
