#include <math.h>

#include <tamtypes.h>
#include <libpad.h>

#include <ath_env.h>
#include "../native/gamepad.h"

/*
 * Every player object is created once, at module initialization, and points
 * at its player index. The controller state itself lives in the native
 * singleton, so players never own resources and need no finalizer.
 */
static JSClassID gamepad_player_class_id;
static bool gamepad_player_class_registered;
static int gamepad_player_indexes[ATHENA_GAMEPAD_MAX_PLAYERS] = { 0, 1, 2, 3, 4, 5, 6, 7 };

static JSClassDef gamepad_player_class = {
    "GamepadPlayer",
};

static int gamepad_require_argc(JSContext *ctx, int argc, int min, int max,
    const char *name) {
    if (argc < min || argc > max) {
        if (min == max)
            JS_ThrowTypeError(ctx, "%s expects %d argument%s",
                name, min, min == 1 ? "" : "s");
        else
            JS_ThrowTypeError(ctx, "%s expects %d to %d arguments",
                name, min, max);
        return 0;
    }
    return 1;
}

static int gamepad_to_number(JSContext *ctx, JSValueConst value, double min,
    double max, double *out, const char *name) {
    if (!JS_IsNumber(value)) {
        JS_ThrowTypeError(ctx, "%s must be a number", name);
        return 0;
    }
    if (JS_ToFloat64(ctx, out, value))
        return 0;
    if (!(*out >= min && *out <= max)) {
        JS_ThrowRangeError(ctx, "%s must be between %g and %g", name, min, max);
        return 0;
    }
    return 1;
}

static int gamepad_to_integer(JSContext *ctx, JSValueConst value, double min,
    double max, double *out, const char *name) {
    /* Range first: converting NaN or out-of-range values to an integer is undefined. */
    if (!gamepad_to_number(ctx, value, min, max, out, name))
        return 0;
    if (*out != floor(*out)) {
        JS_ThrowRangeError(ctx, "%s must be an integer", name);
        return 0;
    }
    return 1;
}

static int gamepad_to_mask(JSContext *ctx, JSValueConst value, uint16_t *mask) {
    double number;

    if (!gamepad_to_integer(ctx, value, 1, 0xFFFF, &number, "buttons"))
        return 0;
    *mask = (uint16_t)number;
    return 1;
}

static int gamepad_to_button(JSContext *ctx, JSValueConst value, uint16_t *button) {
    if (!gamepad_to_mask(ctx, value, button))
        return 0;
    if (*button & (*button - 1)) {
        JS_ThrowRangeError(ctx, "button must be a single Gamepad button constant");
        return 0;
    }
    return 1;
}

static int gamepad_to_bool(JSContext *ctx, JSValueConst value, bool *out,
    const char *name) {
    if (!JS_IsBool(value)) {
        JS_ThrowTypeError(ctx, "%s must be a boolean", name);
        return 0;
    }
    *out = JS_ToBool(ctx, value);
    return 1;
}

static uint8_t gamepad_to_level(double value) {
    return (uint8_t)lround(value * 255.0);
}

static JSValue gamepad_throw_result(JSContext *ctx, AthenaGamepadResult result) {
    switch (result) {
    case ATHENA_GAMEPAD_ERR_UNAVAILABLE:
        return JS_ThrowInternalError(ctx, "Gamepad: padman is not registered in the IOP manager");
    case ATHENA_GAMEPAD_ERR_IOP:
        return JS_ThrowInternalError(ctx, "Gamepad: failed to load padman on the IOP");
    case ATHENA_GAMEPAD_ERR_RPC:
        return JS_ThrowInternalError(ctx, "Gamepad: unable to bind or open the pad ports");
    default:
        return JS_ThrowInternalError(ctx, "Gamepad: unknown error %d", result);
    }
}

static int gamepad_player_index(JSContext *ctx, JSValueConst this_val) {
    int *index = JS_GetOpaque2(ctx, this_val, gamepad_player_class_id);
    return index ? *index : -1;
}

#define GAMEPAD_THIS_PLAYER(player) \
    int player = gamepad_player_index(ctx, this_val); \
    if (player < 0) return JS_EXCEPTION

/* Player getters */

static JSValue gamepad_player_get_index(JSContext *ctx, JSValueConst this_val) {
    GAMEPAD_THIS_PLAYER(player);
    return JS_NewInt32(ctx, player);
}

static JSValue gamepad_player_get_connected(JSContext *ctx, JSValueConst this_val) {
    GAMEPAD_THIS_PLAYER(player);
    return JS_NewBool(ctx, athena_gamepad_core_connected(player));
}

static JSValue gamepad_player_get_just_connected(JSContext *ctx, JSValueConst this_val) {
    GAMEPAD_THIS_PLAYER(player);
    return JS_NewBool(ctx, athena_gamepad_core_just_connected(player));
}

static JSValue gamepad_player_get_just_disconnected(JSContext *ctx, JSValueConst this_val) {
    GAMEPAD_THIS_PLAYER(player);
    return JS_NewBool(ctx, athena_gamepad_core_just_disconnected(player));
}

static JSValue gamepad_player_get_connection(JSContext *ctx, JSValueConst this_val) {
    GAMEPAD_THIS_PLAYER(player);
    switch (athena_gamepad_core_connection(player)) {
    case ATHENA_GAMEPAD_CONNECTION_PORT:
        return JS_NewString(ctx, "port");
    case ATHENA_GAMEPAD_CONNECTION_USB:
        return JS_NewString(ctx, "usb");
    case ATHENA_GAMEPAD_CONNECTION_BLUETOOTH:
        return JS_NewString(ctx, "bluetooth");
    default:
        return JS_NULL;
    }
}

static JSValue gamepad_player_get_port(JSContext *ctx, JSValueConst this_val) {
    GAMEPAD_THIS_PLAYER(player);
    return JS_NewInt32(ctx, athena_gamepad_core_port(player));
}

static JSValue gamepad_player_get_slot(JSContext *ctx, JSValueConst this_val) {
    GAMEPAD_THIS_PLAYER(player);
    return JS_NewInt32(ctx, athena_gamepad_core_slot(player));
}

static JSValue gamepad_player_get_type(JSContext *ctx, JSValueConst this_val) {
    GAMEPAD_THIS_PLAYER(player);
    return JS_NewInt32(ctx, athena_gamepad_core_type(player));
}

static JSValue gamepad_player_get_analog(JSContext *ctx, JSValueConst this_val) {
    GAMEPAD_THIS_PLAYER(player);
    return JS_NewBool(ctx, athena_gamepad_core_analog(player));
}

static JSValue gamepad_player_get_buttons(JSContext *ctx, JSValueConst this_val) {
    GAMEPAD_THIS_PLAYER(player);
    return JS_NewInt32(ctx, athena_gamepad_core_buttons(player));
}

static JSValue gamepad_player_get_previous_buttons(JSContext *ctx, JSValueConst this_val) {
    GAMEPAD_THIS_PLAYER(player);
    return JS_NewInt32(ctx, athena_gamepad_core_previous_buttons(player));
}

static JSValue gamepad_player_get_has_pressure(JSContext *ctx, JSValueConst this_val) {
    GAMEPAD_THIS_PLAYER(player);
    return JS_NewBool(ctx, athena_gamepad_core_has_pressure(player));
}

static JSValue gamepad_player_get_has_rumble(JSContext *ctx, JSValueConst this_val) {
    GAMEPAD_THIS_PLAYER(player);
    return JS_NewBool(ctx, athena_gamepad_core_has_rumble(player));
}

static JSValue gamepad_player_get_deadzone(JSContext *ctx, JSValueConst this_val) {
    GAMEPAD_THIS_PLAYER(player);
    return JS_NewFloat64(ctx, athena_gamepad_core_deadzone(player));
}

static JSValue gamepad_player_set_deadzone(JSContext *ctx, JSValueConst this_val,
    JSValueConst value) {
    double deadzone;
    GAMEPAD_THIS_PLAYER(player);

    if (!gamepad_to_number(ctx, value, 0.0, 0.95, &deadzone, "deadzone"))
        return JS_EXCEPTION;
    athena_gamepad_core_set_deadzone(player, (float)deadzone);
    return JS_UNDEFINED;
}

/* Player methods */

static JSValue gamepad_player_pressed(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    uint16_t mask;
    GAMEPAD_THIS_PLAYER(player);

    if (!gamepad_require_argc(ctx, argc, 1, 1, "player.pressed") ||
        !gamepad_to_mask(ctx, argv[0], &mask))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, athena_gamepad_core_pressed(player, mask));
}

static JSValue gamepad_player_just_pressed(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    uint16_t mask;
    GAMEPAD_THIS_PLAYER(player);

    if (!gamepad_require_argc(ctx, argc, 1, 1, "player.justPressed") ||
        !gamepad_to_mask(ctx, argv[0], &mask))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, athena_gamepad_core_just_pressed(player, mask));
}

static JSValue gamepad_player_just_released(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    uint16_t mask;
    GAMEPAD_THIS_PLAYER(player);

    if (!gamepad_require_argc(ctx, argc, 1, 1, "player.justReleased") ||
        !gamepad_to_mask(ctx, argv[0], &mask))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, athena_gamepad_core_just_released(player, mask));
}

static JSValue gamepad_stick_value(JSContext *ctx, AthenaGamepadStick stick) {
    JSValue object = JS_NewObject(ctx);

    if (JS_IsException(object))
        return object;
    JS_SetPropertyStr(ctx, object, "x", JS_NewFloat64(ctx, stick.x));
    JS_SetPropertyStr(ctx, object, "y", JS_NewFloat64(ctx, stick.y));
    return object;
}

static JSValue gamepad_player_left_stick(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    GAMEPAD_THIS_PLAYER(player);

    if (!gamepad_require_argc(ctx, argc, 0, 0, "player.leftStick"))
        return JS_EXCEPTION;
    return gamepad_stick_value(ctx, athena_gamepad_core_stick(player, false));
}

static JSValue gamepad_player_right_stick(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    GAMEPAD_THIS_PLAYER(player);

    if (!gamepad_require_argc(ctx, argc, 0, 0, "player.rightStick"))
        return JS_EXCEPTION;
    return gamepad_stick_value(ctx, athena_gamepad_core_stick(player, true));
}

static JSValue gamepad_player_pressure(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    uint16_t button;
    GAMEPAD_THIS_PLAYER(player);

    if (!gamepad_require_argc(ctx, argc, 1, 1, "player.pressure") ||
        !gamepad_to_button(ctx, argv[0], &button))
        return JS_EXCEPTION;
    return JS_NewFloat64(ctx, athena_gamepad_core_pressure(player, button) / 255.0);
}

static JSValue gamepad_player_rumble(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    double strong, weak = 0.0, duration = 0.0;
    GAMEPAD_THIS_PLAYER(player);

    if (!gamepad_require_argc(ctx, argc, 1, 3, "player.rumble") ||
        !gamepad_to_number(ctx, argv[0], 0.0, 1.0, &strong, "strong"))
        return JS_EXCEPTION;
    if (argc >= 2 && !gamepad_to_number(ctx, argv[1], 0.0, 1.0, &weak, "weak"))
        return JS_EXCEPTION;
    if (argc >= 3 && !gamepad_to_integer(ctx, argv[2], 0.0, 600000.0, &duration,
            "durationMs"))
        return JS_EXCEPTION;

    /* Any non-zero weak level must still turn on/off-only motors on. */
    athena_gamepad_core_rumble(player, gamepad_to_level(strong),
        weak > 0.0 && gamepad_to_level(weak) == 0 ? 1 : gamepad_to_level(weak),
        (uint32_t)duration);
    return JS_UNDEFINED;
}

static JSValue gamepad_player_stop_rumble(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    GAMEPAD_THIS_PLAYER(player);

    if (!gamepad_require_argc(ctx, argc, 0, 0, "player.stopRumble"))
        return JS_EXCEPTION;
    athena_gamepad_core_rumble(player, 0, 0, 0);
    return JS_UNDEFINED;
}

static JSValue gamepad_player_set_analog(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    bool analog, lock = true;
    GAMEPAD_THIS_PLAYER(player);

    if (!gamepad_require_argc(ctx, argc, 1, 2, "player.setAnalog") ||
        !gamepad_to_bool(ctx, argv[0], &analog, "enabled"))
        return JS_EXCEPTION;
    if (argc == 2 && !gamepad_to_bool(ctx, argv[1], &lock, "lock"))
        return JS_EXCEPTION;
    athena_gamepad_core_set_analog(player, analog, lock);
    return JS_UNDEFINED;
}

static JSValue gamepad_player_pair_bluetooth(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    GAMEPAD_THIS_PLAYER(player);

    if (!gamepad_require_argc(ctx, argc, 0, 0, "player.pairBluetooth"))
        return JS_EXCEPTION;
    switch (athena_gamepad_core_pair_bluetooth(player)) {
    case ATHENA_GAMEPAD_PAIR_OK:
        return JS_TRUE;
    case ATHENA_GAMEPAD_PAIR_NO_ADAPTER:
        return JS_FALSE;
    case ATHENA_GAMEPAD_PAIR_NOT_USB:
        return JS_ThrowTypeError(ctx,
            "player.pairBluetooth requires a DualShock 3/4 connected over USB");
    default:
        return JS_ThrowInternalError(ctx, "Gamepad: failed to write the Bluetooth address");
    }
}

static const JSCFunctionListEntry gamepad_player_proto[] = {
    JS_CGETSET_DEF("index", gamepad_player_get_index, NULL),
    JS_CGETSET_DEF("connected", gamepad_player_get_connected, NULL),
    JS_CGETSET_DEF("justConnected", gamepad_player_get_just_connected, NULL),
    JS_CGETSET_DEF("justDisconnected", gamepad_player_get_just_disconnected, NULL),
    JS_CGETSET_DEF("connection", gamepad_player_get_connection, NULL),
    JS_CGETSET_DEF("port", gamepad_player_get_port, NULL),
    JS_CGETSET_DEF("slot", gamepad_player_get_slot, NULL),
    JS_CGETSET_DEF("type", gamepad_player_get_type, NULL),
    JS_CGETSET_DEF("analog", gamepad_player_get_analog, NULL),
    JS_CGETSET_DEF("buttons", gamepad_player_get_buttons, NULL),
    JS_CGETSET_DEF("previousButtons", gamepad_player_get_previous_buttons, NULL),
    JS_CGETSET_DEF("hasPressure", gamepad_player_get_has_pressure, NULL),
    JS_CGETSET_DEF("hasRumble", gamepad_player_get_has_rumble, NULL),
    JS_CGETSET_DEF("deadzone", gamepad_player_get_deadzone, gamepad_player_set_deadzone),
    JS_CFUNC_DEF("pressed", 1, gamepad_player_pressed),
    JS_CFUNC_DEF("justPressed", 1, gamepad_player_just_pressed),
    JS_CFUNC_DEF("justReleased", 1, gamepad_player_just_released),
    JS_CFUNC_DEF("leftStick", 0, gamepad_player_left_stick),
    JS_CFUNC_DEF("rightStick", 0, gamepad_player_right_stick),
    JS_CFUNC_DEF("pressure", 1, gamepad_player_pressure),
    JS_CFUNC_DEF("rumble", 1, gamepad_player_rumble),
    JS_CFUNC_DEF("stopRumble", 0, gamepad_player_stop_rumble),
    JS_CFUNC_DEF("setAnalog", 1, gamepad_player_set_analog),
    JS_CFUNC_DEF("pairBluetooth", 0, gamepad_player_pair_bluetooth),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "GamepadPlayer", JS_PROP_CONFIGURABLE),
};

/* Module functions. The ones returning players receive the players array as data. */

static JSValue gamepad_update(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    AthenaGamepadResult result;

    if (!gamepad_require_argc(ctx, argc, 0, 0, "Gamepad.update"))
        return JS_EXCEPTION;
    result = athena_gamepad_core_update();
    if (result != ATHENA_GAMEPAD_OK)
        return gamepad_throw_result(ctx, result);
    return JS_UNDEFINED;
}

/* Reads an optional boolean option; leaves `out` unchanged when absent. */
static int gamepad_option(JSContext *ctx, JSValueConst options, const char *name,
    bool *out) {
    JSValue value = JS_GetPropertyStr(ctx, options, name);
    int ok = 1;

    if (JS_IsException(value))
        return 0;
    if (!JS_IsUndefined(value))
        ok = gamepad_to_bool(ctx, value, out, name);
    JS_FreeValue(ctx, value);
    return ok;
}

static JSValue gamepad_configure(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    AthenaGamepadDrivers drivers = athena_gamepad_core_drivers_enabled();

    if (!gamepad_require_argc(ctx, argc, 1, 1, "Gamepad.configure"))
        return JS_EXCEPTION;
    if (!JS_IsObject(argv[0]))
        return JS_ThrowTypeError(ctx, "Gamepad.configure expects an options object");
    if (!gamepad_option(ctx, argv[0], "multitap", &drivers.multitap) ||
        !gamepad_option(ctx, argv[0], "usb", &drivers.usb) ||
        !gamepad_option(ctx, argv[0], "bluetooth", &drivers.bluetooth))
        return JS_EXCEPTION;
    athena_gamepad_core_set_drivers(drivers);
    return JS_UNDEFINED;
}

static JSValue gamepad_driver_value(JSContext *ctx, bool enabled, bool ready) {
    JSValue object = JS_NewObject(ctx);

    if (JS_IsException(object))
        return object;
    JS_SetPropertyStr(ctx, object, "enabled", JS_NewBool(ctx, enabled));
    JS_SetPropertyStr(ctx, object, "ready", JS_NewBool(ctx, ready));
    return object;
}

static JSValue gamepad_drivers(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    AthenaGamepadDrivers enabled = athena_gamepad_core_drivers_enabled();
    AthenaGamepadDrivers ready = athena_gamepad_core_drivers_ready();
    JSValue object;

    if (!gamepad_require_argc(ctx, argc, 0, 0, "Gamepad.drivers"))
        return JS_EXCEPTION;
    object = JS_NewObject(ctx);
    if (JS_IsException(object))
        return object;
    JS_SetPropertyStr(ctx, object, "multitap",
        gamepad_driver_value(ctx, enabled.multitap, ready.multitap));
    JS_SetPropertyStr(ctx, object, "usb",
        gamepad_driver_value(ctx, enabled.usb, ready.usb));
    JS_SetPropertyStr(ctx, object, "bluetooth",
        gamepad_driver_value(ctx, enabled.bluetooth, ready.bluetooth));
    return object;
}

static JSValue gamepad_has_multitap(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    double port;

    if (!gamepad_require_argc(ctx, argc, 1, 1, "Gamepad.hasMultitap") ||
        !gamepad_to_integer(ctx, argv[0], 0, 1, &port, "port"))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, athena_gamepad_core_multitap((int)port));
}

static JSValue gamepad_player(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic, JSValue *players) {
    double index;

    if (!gamepad_require_argc(ctx, argc, 1, 1, "Gamepad.player") ||
        !gamepad_to_integer(ctx, argv[0], 0, ATHENA_GAMEPAD_MAX_PLAYERS - 1, &index,
            "index"))
        return JS_EXCEPTION;
    return JS_GetPropertyUint32(ctx, players[0], (uint32_t)index);
}

static JSValue gamepad_connected_players(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic, JSValue *players) {
    JSValue connected;
    uint32_t count = 0;

    if (!gamepad_require_argc(ctx, argc, 0, 0, "Gamepad.connectedPlayers"))
        return JS_EXCEPTION;
    connected = JS_NewArray(ctx);
    if (JS_IsException(connected))
        return connected;
    for (int i = 0; i < ATHENA_GAMEPAD_MAX_PLAYERS; i++) {
        if (athena_gamepad_core_connected(i))
            JS_SetPropertyUint32(ctx, connected, count++,
                JS_GetPropertyUint32(ctx, players[0], (uint32_t)i));
    }
    return connected;
}

static JSValue gamepad_find_just_pressed(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic, JSValue *players) {
    uint16_t mask;

    if (!gamepad_require_argc(ctx, argc, 1, 1, "Gamepad.findJustPressed") ||
        !gamepad_to_mask(ctx, argv[0], &mask))
        return JS_EXCEPTION;
    for (int i = 0; i < ATHENA_GAMEPAD_MAX_PLAYERS; i++) {
        if (athena_gamepad_core_just_pressed(i, mask))
            return JS_GetPropertyUint32(ctx, players[0], (uint32_t)i);
    }
    return JS_NULL;
}

typedef struct {
    const char *name;
    int length;
    JSCFunctionData *func;
} GamepadPlayerFunction;

static const GamepadPlayerFunction gamepad_player_functions[] = {
    { "player", 1, gamepad_player },
    { "connectedPlayers", 0, gamepad_connected_players },
    { "findJustPressed", 1, gamepad_find_just_pressed },
};

static const JSCFunctionListEntry gamepad_module_funcs[] = {
    JS_CFUNC_DEF("update", 0, gamepad_update),
    JS_CFUNC_DEF("configure", 1, gamepad_configure),
    JS_CFUNC_DEF("drivers", 0, gamepad_drivers),
    JS_CFUNC_DEF("hasMultitap", 1, gamepad_has_multitap),

    JS_PROP_INT32_DEF("MAX_PLAYERS", ATHENA_GAMEPAD_MAX_PLAYERS, JS_PROP_ENUMERABLE),

    JS_PROP_INT32_DEF("SELECT", PAD_SELECT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("L3", PAD_L3, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("R3", PAD_R3, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("START", PAD_START, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("UP", PAD_UP, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("RIGHT", PAD_RIGHT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("DOWN", PAD_DOWN, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("LEFT", PAD_LEFT, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("L2", PAD_L2, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("R2", PAD_R2, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("L1", PAD_L1, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("R1", PAD_R1, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TRIANGLE", PAD_TRIANGLE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("CIRCLE", PAD_CIRCLE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("CROSS", PAD_CROSS, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SQUARE", PAD_SQUARE, JS_PROP_ENUMERABLE),

    JS_PROP_INT32_DEF("TYPE_NONE", 0, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_NEJICON", PAD_TYPE_NEJICON, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_KONAMIGUN", PAD_TYPE_KONAMIGUN, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_DIGITAL", PAD_TYPE_DIGITAL, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_ANALOG", PAD_TYPE_ANALOG, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_NAMCOGUN", PAD_TYPE_NAMCOGUN, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_DUALSHOCK", PAD_TYPE_DUALSHOCK, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_JOGCON", PAD_TYPE_JOGCON, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_DUALSHOCK3", ATHENA_GAMEPAD_TYPE_DUALSHOCK3, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("TYPE_DUALSHOCK4", ATHENA_GAMEPAD_TYPE_DUALSHOCK4, JS_PROP_ENUMERABLE),
};

static JSValue gamepad_new_players(JSContext *ctx) {
    JSValue players = JS_NewArray(ctx);

    if (JS_IsException(players))
        return players;

    for (int i = 0; i < ATHENA_GAMEPAD_MAX_PLAYERS; i++) {
        JSValue player = JS_NewObjectClass(ctx, gamepad_player_class_id);
        if (JS_IsException(player)) {
            JS_FreeValue(ctx, players);
            return player;
        }
        JS_SetOpaque(player, &gamepad_player_indexes[i]);
        /* Read-only, non-configurable slots: players cannot be replaced. */
        if (JS_DefinePropertyValueUint32(ctx, players, (uint32_t)i, player,
                JS_PROP_ENUMERABLE) < 0) {
            JS_FreeValue(ctx, players);
            return JS_EXCEPTION;
        }
    }

    if (JS_PreventExtensions(ctx, players) < 0) {
        JS_FreeValue(ctx, players);
        return JS_EXCEPTION;
    }
    return players;
}

static int gamepad_module_init(JSContext *ctx, JSModuleDef *m) {
    JSValue proto, players;

    if (!gamepad_player_class_registered) {
        JS_NewClassID(&gamepad_player_class_id);
        if (JS_NewClass(JS_GetRuntime(ctx), gamepad_player_class_id,
                &gamepad_player_class) < 0)
            return -1;
        gamepad_player_class_registered = true;
    }

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto))
        return -1;
    JS_SetPropertyFunctionList(ctx, proto, gamepad_player_proto,
        countof(gamepad_player_proto));
    JS_SetClassProto(ctx, gamepad_player_class_id, proto);

    players = gamepad_new_players(ctx);
    if (JS_IsException(players))
        return -1;

    for (size_t i = 0; i < countof(gamepad_player_functions); i++) {
        const GamepadPlayerFunction *fn = &gamepad_player_functions[i];
        JSValue value = JS_NewCFunctionData(ctx, fn->func, fn->length, 0, 1, &players);
        if (JS_IsException(value)) {
            JS_FreeValue(ctx, players);
            return -1;
        }
        JS_SetModuleExport(ctx, m, fn->name, value);
    }

    JS_SetModuleExport(ctx, m, "players", players);
    return JS_SetModuleExportList(ctx, m, gamepad_module_funcs,
        countof(gamepad_module_funcs));
}

JSModuleDef *athena_gamepad_init(JSContext *ctx) {
    JSModuleDef *m = athena_push_module(ctx, gamepad_module_init,
        gamepad_module_funcs, countof(gamepad_module_funcs), "Gamepad");

    if (!m)
        return NULL;
    JS_AddModuleExport(ctx, m, "players");
    for (size_t i = 0; i < countof(gamepad_player_functions); i++)
        JS_AddModuleExport(ctx, m, gamepad_player_functions[i].name);
    return m;
}
