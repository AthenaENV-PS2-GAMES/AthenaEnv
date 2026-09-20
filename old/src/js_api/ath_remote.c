#include <ath_env.h>
#include <athena/remote.h>

static JSClassID js_remote_class_id;

static JSValue js_remote_init(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    return JS_NewInt32(ctx, athena_remote_open());
}

static JSValue js_remote_deinit(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    return JS_NewInt32(ctx, athena_remote_close());
}

static JSValue js_remote_isactive(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    int32_t port = 1;
    if (argc == 1)
        JS_ToInt32(ctx, &port, argv[0]);

    if (port < 0 || port > 1)
        return JS_ThrowSyntaxError(ctx, "wrong port number.");

    return JS_NewBool(ctx, athena_remote_is_active(port));
}

static JSValue js_remote_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    int32_t port = 1;
    AthenaRemote *remote;
    JSValue obj;

    if (argc == 1) {
        JS_ToInt32(ctx, &port, argv[0]);
        if (port < 0 || port > 1)
            return JS_ThrowSyntaxError(ctx, "wrong port number.");
    }

    remote = athena_remote_get(port);
    if (!remote)
        return JS_EXCEPTION;

    obj = JS_NewObjectClass(ctx, js_remote_class_id);
    if (JS_IsException(obj)) {
        athena_remote_free(remote);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, remote);
    return obj;
}

static JSValue js_remote_update(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    AthenaRemote *remote = JS_GetOpaque2(ctx, this_val, js_remote_class_id);
    if (!remote)
        return JS_EXCEPTION;

    athena_remote_update(remote);
    return JS_UNDEFINED;
}

static JSValue js_remote_pressed(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    uint32_t button;
    AthenaRemote *remote = JS_GetOpaque2(ctx, this_val, js_remote_class_id);
    if (!remote)
        return JS_EXCEPTION;

    JS_ToUint32(ctx, &button, argv[0]);
    return JS_NewBool(ctx, athena_remote_pressed(remote, button));
}

static JSValue js_remote_justpressed(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    uint32_t button;
    AthenaRemote *remote = JS_GetOpaque2(ctx, this_val, js_remote_class_id);
    if (!remote)
        return JS_EXCEPTION;

    JS_ToUint32(ctx, &button, argv[0]);
    return JS_NewBool(ctx, athena_remote_just_pressed(remote, button));
}

static JSValue js_remote_released(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    AthenaRemote *remote = JS_GetOpaque2(ctx, this_val, js_remote_class_id);
    if (!remote)
        return JS_EXCEPTION;

    return JS_NewBool(ctx, athena_remote_released(remote));
}

static JSValue js_remote_get_prop(JSContext *ctx, JSValueConst this_val, int magic)
{
    AthenaRemote *remote = JS_GetOpaque2(ctx, this_val, js_remote_class_id);
    if (!remote)
        return JS_EXCEPTION;

    switch (magic) {
    case 0:
        return JS_NewUint32(ctx, remote->button);
    case 1:
        return JS_NewUint32(ctx, remote->old_button);
    case 2:
        return JS_NewUint32(ctx, remote->status);
    }

    return JS_UNDEFINED;
}

static const JSCFunctionListEntry module_funcs[] = {
    JS_CFUNC_DEF("init", 0, js_remote_init),
    JS_CFUNC_DEF("deinit", 0, js_remote_deinit),
    JS_CFUNC_DEF("get", 1, js_remote_get),
    JS_CFUNC_DEF("isActive", 1, js_remote_isactive),

    /* Status values returned by remote.status */
    JS_PROP_INT32_DEF("INIT", RM_INIT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("READY", RM_READY, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("KEYPRESSED", RM_KEYPRESSED, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("NOREMOTE", RM_NOREMOTE, JS_PROP_CONFIGURABLE),

    /* Button values that are not an actual button code */
    JS_PROP_INT32_DEF("RELEASED", (int32_t)RM_RELEASED, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("IDLE", (int32_t)RM_IDLE, JS_PROP_CONFIGURABLE),

    /* DVD remote buttons */
    JS_PROP_INT32_DEF("DVD_ZERO", (int32_t)RM_DVD_ZERO, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_ONE", (int32_t)RM_DVD_ONE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_TWO", (int32_t)RM_DVD_TWO, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_THREE", (int32_t)RM_DVD_THREE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_FOUR", (int32_t)RM_DVD_FOUR, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_FIVE", (int32_t)RM_DVD_FIVE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_SIX", (int32_t)RM_DVD_SIX, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_SEVEN", (int32_t)RM_DVD_SEVEN, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_EIGHT", (int32_t)RM_DVD_EIGHT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_NINE", (int32_t)RM_DVD_NINE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_ENTER", (int32_t)RM_DVD_ENTER, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_BROWSE", (int32_t)RM_DVD_BROWSE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_SET", (int32_t)RM_DVD_SET, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_RETURN", (int32_t)RM_DVD_RETURN, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_CLEAR", (int32_t)RM_DVD_CLEAR, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_SOURCE", (int32_t)RM_DVD_SOURCE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_CHUP", (int32_t)RM_DVD_CHUP, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_CHDOWN", (int32_t)RM_DVD_CHDOWN, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_REC", (int32_t)RM_DVD_REC, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_TITLE", (int32_t)RM_DVD_TITLE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_MENU", (int32_t)RM_DVD_MENU, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_PROGRAM", (int32_t)RM_DVD_PROGRAM, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_TIME", (int32_t)RM_DVD_TIME, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_ATOB", (int32_t)RM_DVD_ATOB, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_REPEAT", (int32_t)RM_DVD_REPEAT, JS_PROP_CONFIGURABLE),
    /* Playback transport controls */
    JS_PROP_INT32_DEF("DVD_PREV", (int32_t)RM_DVD_PREV, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_NEXT", (int32_t)RM_DVD_NEXT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_PLAY", (int32_t)RM_DVD_PLAY, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_STOP", (int32_t)RM_DVD_STOP, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_PAUSE", (int32_t)RM_DVD_PAUSE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_SCAN_BACK", (int32_t)RM_DVD_SCAN_BACK, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_SCAN_FORW", (int32_t)RM_DVD_SCAN_FORW, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_SHUFFLE", (int32_t)RM_DVD_SHUFFLE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_STEP_BACK", (int32_t)RM_DVD_STEP_BACK, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_STEP_FORWARD", (int32_t)RM_DVD_STEP_FORWARD, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_SLOW_BACK", (int32_t)RM_DVD_SLOW_BACK, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_SLOW_FORW", (int32_t)RM_DVD_SLOW_FORW, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_DISPLAY", (int32_t)RM_DVD_DISPLAY, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_SUBTITLE", (int32_t)RM_DVD_SUBTITLE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_SUBTITLE_ON_OFF", (int32_t)RM_DVD_SUBTITLE_ON_OFF, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_AUDIO", (int32_t)RM_DVD_AUDIO, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_ANGLE", (int32_t)RM_DVD_ANGLE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_UP", (int32_t)RM_DVD_UP, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_DOWN", (int32_t)RM_DVD_DOWN, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_LEFT", (int32_t)RM_DVD_LEFT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_RIGHT", (int32_t)RM_DVD_RIGHT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_OPEN_CLOSE", (int32_t)RM_DVD_OPEN_CLOSE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_POWER", (int32_t)RM_DVD_POWER, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_SEARCH_MODE", (int32_t)RM_DVD_SEARCH_MODE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DVD_SET_UP", (int32_t)RM_DVD_SET_UP, JS_PROP_CONFIGURABLE),

    /* Controller-equivalent buttons (already mirrored into the normal Pads
       stream by the IOP, exposed here too for completeness) */
    JS_PROP_INT32_DEF("PS2_SELECT", (int32_t)RM_PS2_SELECT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PS2_L3", (int32_t)RM_PS2_L3, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PS2_R3", (int32_t)RM_PS2_R3, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PS2_START", (int32_t)RM_PS2_START, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PS2_UP", (int32_t)RM_PS2_UP, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PS2_RIGHT", (int32_t)RM_PS2_RIGHT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PS2_DOWN", (int32_t)RM_PS2_DOWN, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PS2_LEFT", (int32_t)RM_PS2_LEFT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PS2_L2", (int32_t)RM_PS2_L2, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PS2_R2", (int32_t)RM_PS2_R2, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PS2_L1", (int32_t)RM_PS2_L1, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PS2_R1", (int32_t)RM_PS2_R1, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PS2_TRIANGLE", (int32_t)RM_PS2_TRIANGLE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PS2_CIRCLE", (int32_t)RM_PS2_CIRCLE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PS2_CROSS", (int32_t)RM_PS2_CROSS, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PS2_SQUARE", (int32_t)RM_PS2_SQUARE, JS_PROP_CONFIGURABLE),

    /* Dragon (slim) only commands */
    JS_PROP_INT32_DEF("PS2_POWER", (int32_t)RM_PS2_POWER, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PS2_EJECT", (int32_t)RM_PS2_EJECT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PS2_RESET", (int32_t)RM_PS2_RESET, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PS2_POWERON", (int32_t)RM_PS2_POWERON, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PS2_POWEROFF", (int32_t)RM_PS2_POWEROFF, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PS2_NOLIGHT", (int32_t)RM_PS2_NOLIGHT, JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_remote_proto_funcs[] = {
    JS_CFUNC_DEF("update", 0, js_remote_update),
    JS_CFUNC_DEF("pressed", 1, js_remote_pressed),
    JS_CFUNC_DEF("justPressed", 1, js_remote_justpressed),
    JS_CFUNC_DEF("released", 0, js_remote_released),
    JS_CGETSET_MAGIC_DEF("button", js_remote_get_prop, NULL, 0),
    JS_CGETSET_MAGIC_DEF("old_button", js_remote_get_prop, NULL, 1),
    JS_CGETSET_MAGIC_DEF("status", js_remote_get_prop, NULL, 2),
};

static void js_remote_finalizer(JSRuntime *rt, JSValue val)
{
    AthenaRemote *remote = JS_GetOpaque(val, js_remote_class_id);
    if (remote)
        athena_remote_free(remote);
}

static JSClassDef js_remote_class = {
    "Remote",
    .finalizer = js_remote_finalizer,
};

static int js_remote_init_module(JSContext *ctx, JSModuleDef *m)
{
    JSValue proto;

    JS_NewClassID(&js_remote_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_remote_class_id, &js_remote_class);
    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, js_remote_proto_funcs, countof(js_remote_proto_funcs));
    JS_SetClassProto(ctx, js_remote_class_id, proto);

    return JS_SetModuleExportList(ctx, m, module_funcs, countof(module_funcs));
}

JSModuleDef *athena_remote_init(JSContext *ctx)
{
    return athena_push_module(ctx, js_remote_init_module, module_funcs, countof(module_funcs), "Remote");
}
