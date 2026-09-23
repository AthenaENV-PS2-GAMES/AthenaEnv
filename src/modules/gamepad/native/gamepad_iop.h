#ifndef ATH_NATIVE_GAMEPAD_IOP_H
#define ATH_NATIVE_GAMEPAD_IOP_H

/*
 * IOP side of the Gamepad module: registers and loads the embedded drivers
 * and talks to them over SIF RPC. The multitap and DualShock 3/4 clients are
 * implemented here instead of using libmtap/libds34*: they bind with a
 * timeout instead of spinning forever, and can be re-bound after an IOP reset.
 */

#include <stdbool.h>
#include <stdint.h>

#include <iop_manager.h>

typedef enum {
    GAMEPAD_DRIVER_PADMAN,
    GAMEPAD_DRIVER_MTAPMAN,
    GAMEPAD_DRIVER_DS34USB,
    GAMEPAD_DRIVER_DS34BT,
    GAMEPAD_DRIVER_COUNT,
} GamepadDriver;

/* DualShock 3/4 transports; each driver handles up to two pads. */
typedef enum {
    GAMEPAD_DS34_USB,
    GAMEPAD_DS34_BLUETOOTH,
} GamepadDs34Bus;

#define GAMEPAD_DS34_PADS 2
/* Status bits shared by ds34usb and ds34bt. */
#define GAMEPAD_DS34_RUNNING 0x08
#define GAMEPAD_DS34_DS4     0x10

/*
 * Registers the driver in the IOP manager on first use, loads it and binds
 * its RPC client. Returns true when the driver is ready. `end_hook` is
 * installed on padman so the caller learns about IOP resets.
 */
bool gamepad_iop_start(GamepadDriver driver, iopman_func end_hook);
bool gamepad_iop_ready(GamepadDriver driver);
/* Drops every RPC binding; call when the IOP is about to be reset. */
void gamepad_iop_forget(void);

/* Multitap (mtapman). Return 1 on success/connected, 0 otherwise. */
int gamepad_mtap_open(int port);
int gamepad_mtap_connected(int port);

/* DualShock 3/4. `status` receives the driver status bits. */
int gamepad_ds34_status(GamepadDs34Bus bus, int pad);
bool gamepad_ds34_read(GamepadDs34Bus bus, int pad, uint8_t data[18], int *status);
bool gamepad_ds34_rumble(GamepadDs34Bus bus, int pad, uint8_t strong, uint8_t weak);
/* Address of the Bluetooth adapter handled by ds34bt. */
bool gamepad_ds34_adapter_address(uint8_t address[6]);
/* Makes a USB-connected pad connect to `address` when used wirelessly. */
bool gamepad_ds34_pair(int usb_pad, const uint8_t address[6]);

#endif /* ATH_NATIVE_GAMEPAD_IOP_H */
