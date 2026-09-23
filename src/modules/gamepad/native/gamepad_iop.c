#include <stdio.h>
#include <string.h>

#include <kernel.h>
#include <sifrpc.h>
#include <delaythread.h>

#include <dbgprintf.h>
#include <iop_manager.h>

#include "gamepad_iop.h"

/* How long to wait for a freshly loaded driver to register its RPC server. */
#define GAMEPAD_BIND_ATTEMPTS 500
#define GAMEPAD_BIND_DELAY_US 1000

/* libmtap protocol: one RPC server per operation. */
#define MTAP_SERVER_OPEN       0x80000901
#define MTAP_SERVER_GET_CONN   0x80000903

/* ds34usb / ds34bt protocol (see iop_modules/ds34*). */
#define DS34USB_SERVER 0x18E3878E
#define DS34BT_SERVER  0x18E3878F
#define DS34USB_GET_STATUS 2
#define DS34USB_SET_BDADDR 4
#define DS34USB_SET_RUMBLE 5
#define DS34USB_GET_DATA   7
#define DS34BT_GET_STATUS  3
#define DS34BT_GET_BDADDR  4
#define DS34BT_SET_RUMBLE  5
#define DS34BT_GET_DATA    7

iopman_define_module(mtapman);
iopman_define_module(ds34usb);
iopman_define_module(ds34bt);

typedef struct {
    const char *name;
    const char *dependency;
    void *irx;
    unsigned int *size;
} GamepadDriverInfo;

static const GamepadDriverInfo gamepad_drivers[GAMEPAD_DRIVER_COUNT] = {
    [GAMEPAD_DRIVER_PADMAN] = { "padman", NULL, NULL, NULL },
    [GAMEPAD_DRIVER_MTAPMAN] = { "mtapman", "sio2man", mtapman_irx, &size_mtapman_irx },
    [GAMEPAD_DRIVER_DS34USB] = { "ds34usb", "usbd", ds34usb_irx, &size_ds34usb_irx },
    [GAMEPAD_DRIVER_DS34BT] = { "ds34bt", "usbd", ds34bt_irx, &size_ds34bt_irx },
};

static bool gamepad_driver_ready[GAMEPAD_DRIVER_COUNT];
static iopman_func gamepad_end_hook;
static iopman_func gamepad_previous_padman_end;

/*
 * Installed as padman's end callback. iopman_reset() calls it before the IOP
 * is rebooted, while padman can still answer, so the caller's hook may close
 * ports; the RPC bindings are dropped afterwards.
 */
static int gamepad_padman_end(void *module) {
    if (gamepad_end_hook)
        gamepad_end_hook(module);
    gamepad_iop_forget();
    return gamepad_previous_padman_end ? gamepad_previous_padman_end(module) : 0;
}

static SifRpcClientData_t mtap_open_client __attribute__((aligned(64)));
static SifRpcClientData_t mtap_conn_client __attribute__((aligned(64)));
static SifRpcClientData_t ds34_clients[2] __attribute__((aligned(64)));
static uint32_t mtap_buffer[16] __attribute__((aligned(64)));
static uint8_t ds34_buffer[64] __attribute__((aligned(64)));

static bool gamepad_bind(SifRpcClientData_t *client, int server) {
    memset(client, 0, sizeof(*client));
    for (int attempt = 0; attempt < GAMEPAD_BIND_ATTEMPTS; attempt++) {
        if (SifBindRpc(client, server, 0) < 0)
            return false;
        if (client->server)
            return true;
        DelayThread(GAMEPAD_BIND_DELAY_US);
    }
    return false;
}

static module_entry *gamepad_register(const GamepadDriverInfo *info) {
    module_entry *entry = iopman_search_module(info->name);
    uint8_t dependencies[4] = { EMPTY_ENTRY, EMPTY_ENTRY, EMPTY_ENTRY, EMPTY_ENTRY };

    if (entry || !info->irx)
        return entry;

    if (info->dependency)
        dependencies[0] = iopman_dependency(iopman_search_module(info->dependency));
    return iopman_register_module((char *)info->name, info->irx, *info->size,
        dependencies, NULL, NULL);
}

static bool gamepad_bind_driver(GamepadDriver driver) {
    switch (driver) {
    case GAMEPAD_DRIVER_MTAPMAN:
        return gamepad_bind(&mtap_open_client, MTAP_SERVER_OPEN) &&
            gamepad_bind(&mtap_conn_client, MTAP_SERVER_GET_CONN);
    case GAMEPAD_DRIVER_DS34USB:
        return gamepad_bind(&ds34_clients[GAMEPAD_DS34_USB], DS34USB_SERVER);
    case GAMEPAD_DRIVER_DS34BT:
        return gamepad_bind(&ds34_clients[GAMEPAD_DS34_BLUETOOTH], DS34BT_SERVER);
    default:
        /* padman is bound by libpad's padInit(). */
        return true;
    }
}

bool gamepad_iop_start(GamepadDriver driver, iopman_func end_hook) {
    const GamepadDriverInfo *info = &gamepad_drivers[driver];
    module_entry *entry;

    if (gamepad_driver_ready[driver])
        return true;

    entry = gamepad_register(info);
    if (!entry) {
        dbgprintf("[Gamepad] IOP module %s is not available\n", info->name);
        return false;
    }
    if (iopman_load_module(entry, 0, NULL) != MODULE_STATUS_LOADED) {
        dbgprintf("[Gamepad] failed to load IOP module %s\n", info->name);
        return false;
    }
    if (driver == GAMEPAD_DRIVER_PADMAN && entry->end != gamepad_padman_end) {
        gamepad_end_hook = end_hook;
        gamepad_previous_padman_end = entry->end;
        entry->end = gamepad_padman_end;
    }

    if (!gamepad_bind_driver(driver)) {
        dbgprintf("[Gamepad] %s RPC server did not answer\n", info->name);
        return false;
    }
    gamepad_driver_ready[driver] = true;
    return true;
}

bool gamepad_iop_ready(GamepadDriver driver) {
    return gamepad_driver_ready[driver];
}

void gamepad_iop_forget(void) {
    memset(gamepad_driver_ready, 0, sizeof(gamepad_driver_ready));
}

static int gamepad_mtap_call(SifRpcClientData_t *client, int port) {
    mtap_buffer[0] = port;
    if (SifCallRpc(client, 1, 0, mtap_buffer, 4, mtap_buffer, 8, NULL, NULL) < 0)
        return 0;
    return (int)mtap_buffer[1];
}

int gamepad_mtap_open(int port) {
    if (!gamepad_driver_ready[GAMEPAD_DRIVER_MTAPMAN])
        return 0;
    return gamepad_mtap_call(&mtap_open_client, port) == 1;
}

int gamepad_mtap_connected(int port) {
    if (!gamepad_driver_ready[GAMEPAD_DRIVER_MTAPMAN])
        return 0;
    return gamepad_mtap_call(&mtap_conn_client, port) == 1;
}

static bool gamepad_ds34_bus_ready(GamepadDs34Bus bus) {
    return gamepad_driver_ready[bus == GAMEPAD_DS34_USB ?
        GAMEPAD_DRIVER_DS34USB : GAMEPAD_DRIVER_DS34BT];
}

int gamepad_ds34_status(GamepadDs34Bus bus, int pad) {
    if (!gamepad_ds34_bus_ready(bus))
        return 0;
    ds34_buffer[0] = pad;
    if (SifCallRpc(&ds34_clients[bus],
            bus == GAMEPAD_DS34_USB ? DS34USB_GET_STATUS : DS34BT_GET_STATUS,
            0, ds34_buffer, 1, ds34_buffer, 1, NULL, NULL) < 0)
        return 0;
    return ds34_buffer[0];
}

bool gamepad_ds34_read(GamepadDs34Bus bus, int pad, uint8_t data[18], int *status) {
    *status = 0;
    if (!gamepad_ds34_bus_ready(bus))
        return false;
    ds34_buffer[0] = pad;
    /* The drivers append the status byte after the 18 data bytes. */
    if (SifCallRpc(&ds34_clients[bus],
            bus == GAMEPAD_DS34_USB ? DS34USB_GET_DATA : DS34BT_GET_DATA,
            0, ds34_buffer, 1, ds34_buffer, 19, NULL, NULL) < 0)
        return false;
    memcpy(data, ds34_buffer, 18);
    *status = ds34_buffer[18];
    return true;
}

bool gamepad_ds34_rumble(GamepadDs34Bus bus, int pad, uint8_t strong, uint8_t weak) {
    if (!gamepad_ds34_bus_ready(bus))
        return false;
    ds34_buffer[0] = pad;
    ds34_buffer[1] = strong;
    ds34_buffer[2] = weak;
    return SifCallRpc(&ds34_clients[bus],
        bus == GAMEPAD_DS34_USB ? DS34USB_SET_RUMBLE : DS34BT_SET_RUMBLE,
        0, ds34_buffer, 3, NULL, 0, NULL, NULL) >= 0;
}

bool gamepad_ds34_adapter_address(uint8_t address[6]) {
    if (!gamepad_driver_ready[GAMEPAD_DRIVER_DS34BT])
        return false;
    if (SifCallRpc(&ds34_clients[GAMEPAD_DS34_BLUETOOTH], DS34BT_GET_BDADDR,
            0, NULL, 0, ds34_buffer, 7, NULL, NULL) < 0)
        return false;
    /* Byte 6 is the driver's result: 0 while no adapter is configured. */
    if (!ds34_buffer[6])
        return false;
    memcpy(address, ds34_buffer, 6);
    return true;
}

bool gamepad_ds34_pair(int usb_pad, const uint8_t address[6]) {
    if (!gamepad_driver_ready[GAMEPAD_DRIVER_DS34USB])
        return false;
    ds34_buffer[0] = usb_pad;
    memcpy(&ds34_buffer[1], address, 6);
    return SifCallRpc(&ds34_clients[GAMEPAD_DS34_USB], DS34USB_SET_BDADDR,
        0, ds34_buffer, 7, NULL, 0, NULL, NULL) >= 0;
}
