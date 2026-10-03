#include <ath_env.h>
#include <athena/pad.h>

static JSClassID js_pads_class_id;

void js_pads_update(AthenaPad *pad)
{
    athena_pad_update(pad);
}

static JSValue js_pad_gettype(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    if (argc != 0 && argc != 1)
        return JS_ThrowSyntaxError(ctx, "wrong number of arguments");
    int port = 0;
    if (argc == 1) {
        JS_ToInt32(ctx, &port, argv[0]);
        if (port > 1)
            return JS_ThrowSyntaxError(ctx, "wrong port number.");
    }
    return JS_NewInt32(ctx, athena_pad_type(port));
}

static JSValue js_pad_getactivetype(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    if (argc != 0 && argc != 1)
        return JS_ThrowSyntaxError(ctx, "wrong number of arguments");
    int32_t port = 0;
    if (argc == 1) {
        JS_ToInt32(ctx, &port, argv[0]);
        if (port < 0 || port > 1)
            return JS_ThrowSyntaxError(ctx, "wrong port number.");
    }
    return JS_NewInt32(ctx, athena_pad_active_type(port));
}

static JSValue js_pad_setmode(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    if (argc != 2 && argc != 3)
        return JS_ThrowSyntaxError(ctx, "wrong number of arguments.");
    int32_t port = 0, mode = 0;
    int lock;
    if (argc == 3) {
        JS_ToInt32(ctx, &port, argv[0]);
        JS_ToInt32(ctx, &mode, argv[1]);
        lock = JS_ToBool(ctx, argv[2]);
    } else {
        JS_ToInt32(ctx, &mode, argv[0]);
        lock = JS_ToBool(ctx, argv[1]);
    }

    if (port < 0 || port > 1)
        return JS_ThrowSyntaxError(ctx, "wrong port number.");

    return JS_NewInt32(ctx, athena_pad_set_mode(port, mode, lock));
}

static JSValue js_pad_new_event(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    int buttons, flavour;
    JS_ToInt32(ctx, &buttons, argv[0]);
    JS_ToInt32(ctx, &flavour, argv[1]);
    int id = js_new_input_event(buttons, JS_DupValue(ctx, argv[2]), flavour);
    return JS_NewInt32(ctx, id);
}

static JSValue js_pad_delete_event(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    int id;
    JS_ToInt32(ctx, &id, argv[0]);
    js_delete_input_event(id);
    return JS_UNDEFINED;
}

static JSValue js_pad_getstate(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    if (argc != 0 && argc != 1)
        return JS_ThrowSyntaxError(ctx, "wrong number of arguments");
    int port = 0;
    if (argc == 1) {
        JS_ToInt32(ctx, &port, argv[0]);
        if (port > 1)
            return JS_ThrowSyntaxError(ctx, "wrong port number.");
    }
    return JS_NewInt32(ctx, athena_pad_state(port));
}

static JSValue js_pad_isactive(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    if (argc != 1)
        return JS_ThrowSyntaxError(ctx, "wrong number of arguments");
    int port;
    JS_ToInt32(ctx, &port, argv[0]);
    if (port > 1)
        return JS_ThrowSyntaxError(ctx, "wrong port number.");

    return JS_NewBool(ctx, athena_pad_is_active(port));
}

static JSValue js_pad_get_connected(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    JSValue arr = JS_NewArray(ctx);
    int count = 0;

    for (int port = 0; port < 2; port++) {
        if (athena_pad_is_active(port)) {
            JS_SetPropertyUint32(ctx, arr, count, JS_NewInt32(ctx, port));
            count++;
        }
    }

    return arr;
}

static JSValue js_pad_getconnectedcount(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    return JS_NewInt32(ctx, athena_pad_connected_count());
}

static JSValue js_pad_getpad(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    int port = 0;
    AthenaPad *pad;
    JSValue obj = JS_UNDEFINED;

    if (argc == 1) {
        JS_ToInt32(ctx, &port, argv[0]);
        if (port > 1)
            return JS_ThrowSyntaxError(ctx, "wrong port number.");
    }

    pad = athena_pad_open(port);
    if (!pad)
        return JS_EXCEPTION;

    obj = JS_NewObjectClass(ctx, js_pads_class_id);
    if (JS_IsException(obj)) {
        athena_pad_close(pad);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, pad);
    return obj;
}

static JSValue js_pad_update(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    AthenaPad *pad = JS_GetOpaque2(ctx, this_val, js_pads_class_id);
    athena_pad_update(pad);
    return JS_UNDEFINED;
}

static JSValue js_pad_getpressure(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    if (argc != 1 && argc != 2)
        return JS_ThrowSyntaxError(ctx, "wrong number of arguments.");
    int port = 0;
    u32 button;
    if (argc == 2) {
        JS_ToInt32(ctx, &port, argv[0]);
        JS_ToUint32(ctx, &button, argv[1]);
    } else {
        JS_ToUint32(ctx, &button, argv[0]);
    }

    if (port < 0 || port > 1)
        return JS_ThrowSyntaxError(ctx, "wrong port number.");

    return JS_NewInt32(ctx, athena_pad_pressure(port, button));
}

static JSValue js_pad_rumble(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    if (argc != 2 && argc != 3)
        return JS_ThrowSyntaxError(ctx, "wrong number of arguments.");
    int32_t port = 0;
    int32_t act_align0 = 0, act_align1 = 0;
    if (argc == 3) {
        JS_ToInt32(ctx, &port, argv[0]);
        JS_ToInt32(ctx, &act_align0, argv[1]);
        JS_ToInt32(ctx, &act_align1, argv[2]);
    } else {
        JS_ToInt32(ctx, &act_align0, argv[0]);
        JS_ToInt32(ctx, &act_align1, argv[1]);
    }

    if (port < 0 || port > 1)
        return JS_ThrowSyntaxError(ctx, "wrong port number.");

    athena_pad_rumble(port, (char)act_align0, (char)act_align1);
    return JS_UNDEFINED;
}

static JSValue js_pad_check(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    int button;
    AthenaPad *pad = JS_GetOpaque2(ctx, this_val, js_pads_class_id);

    JS_ToInt32(ctx, &button, argv[0]);
    return JS_NewBool(ctx, athena_pad_pressed(pad, button));
}

static JSValue js_pad_justpressed(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    int button;
    AthenaPad *pad = JS_GetOpaque2(ctx, this_val, js_pads_class_id);

    JS_ToInt32(ctx, &button, argv[0]);
    return JS_NewBool(ctx, athena_pad_just_pressed(pad, button));
}

static JSValue js_pad_seteventhandler(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    AthenaPad *pad = JS_GetOpaque2(ctx, this_val, js_pads_class_id);
    js_set_input_event_handler(pad);
    return JS_UNDEFINED;
}

static JSValue js_pad_set_led(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    if (argc != 3 && argc != 4)
        return JS_ThrowSyntaxError(ctx, "wrong number of arguments.");
    int32_t port = 0;
    int32_t led0 = 0, led1 = 0, led2 = 0;
    if (argc == 4) {
        JS_ToInt32(ctx, &port, argv[0]);
        JS_ToInt32(ctx, &led0, argv[1]);
        JS_ToInt32(ctx, &led1, argv[2]);
        JS_ToInt32(ctx, &led2, argv[3]);
    } else {
        JS_ToInt32(ctx, &led0, argv[0]);
        JS_ToInt32(ctx, &led1, argv[1]);
        JS_ToInt32(ctx, &led2, argv[2]);
    }

    if (port < 0 || port > 1)
        return JS_ThrowSyntaxError(ctx, "wrong port number.");

    athena_pad_set_led(port, (u8)led0, (u8)led1, (u8)led2);
    return JS_UNDEFINED;
}

static JSValue js_pad_get_prop(JSContext *ctx, JSValueConst this_val, int magic)
{
    JSValue val = JS_UNDEFINED;
    AthenaPad *pad = JS_GetOpaque2(ctx, this_val, js_pads_class_id);

    if (!pad)
        return JS_EXCEPTION;

    switch (magic) {
    case 0:
        val = JS_NewUint32(ctx, pad->btns);
        break;
    case 1:
        val = JS_NewInt32(ctx, pad->lx);
        break;
    case 2:
        val = JS_NewInt32(ctx, pad->ly);
        break;
    case 3:
        val = JS_NewInt32(ctx, pad->rx);
        break;
    case 4:
        val = JS_NewInt32(ctx, pad->ry);
        break;
    case 5:
        val = JS_NewUint32(ctx, pad->old_btns);
        break;
    case 6:
        val = JS_NewInt32(ctx, pad->old_lx);
        break;
    case 7:
        val = JS_NewInt32(ctx, pad->old_ly);
        break;
    case 8:
        val = JS_NewInt32(ctx, pad->old_rx);
        break;
    case 9:
        val = JS_NewInt32(ctx, pad->old_ry);
        break;
    }

    return val;
}

static JSValue js_pad_set_prop(JSContext *ctx, JSValueConst this_val, JSValue val, int magic)
{
    AthenaPad *pad = JS_GetOpaque2(ctx, this_val, js_pads_class_id);
    u32 v;

    if (!pad || JS_ToUint32(ctx, &v, val))
        return JS_EXCEPTION;

    switch (magic) {
    case 0:
        pad->btns = v;
        break;
    case 1:
        pad->lx = v;
        break;
    case 2:
        pad->ly = v;
        break;
    case 3:
        pad->rx = v;
        break;
    case 4:
        pad->ry = v;
        break;
    case 5:
        pad->old_btns = v;
        break;
    case 6:
        pad->old_lx = v;
        break;
    case 7:
        pad->old_ly = v;
        break;
    case 8:
        pad->old_rx = v;
        break;
    case 9:
        pad->old_ry = v;
        break;
    }

    return JS_UNDEFINED;
}

static const JSCFunctionListEntry module_funcs[] = {
    JS_CFUNC_DEF("get", 1, js_pad_getpad),
    JS_CFUNC_DEF("getType", 1, js_pad_gettype),
    JS_CFUNC_DEF("getActiveType", 1, js_pad_getactivetype),
    JS_CFUNC_DEF("setMode", 3, js_pad_setmode),
    JS_CFUNC_DEF("getState", 1, js_pad_getstate),
    JS_CFUNC_DEF("getPressure", 2, js_pad_getpressure),
    JS_CFUNC_DEF("rumble", 3, js_pad_rumble),
    JS_CFUNC_DEF("setLED", 4, js_pad_set_led),
    JS_CFUNC_DEF("isActive", 1, js_pad_isactive),
    JS_CFUNC_DEF("getConnected", 0, js_pad_get_connected),
    JS_CFUNC_DEF("getConnectedCount", 0, js_pad_getconnectedcount),
    JS_CFUNC_DEF("newEvent", 3, js_pad_new_event),
    JS_CFUNC_DEF("deleteEvent", 1, js_pad_delete_event),
    JS_PROP_INT32_DEF("PRESSED", PRESSED_EVENT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("JUST_PRESSED", JUSTPRESSED_EVENT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("NONPRESSED", NONPRESSED_EVENT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SELECT", PAD_SELECT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("START", PAD_START, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("UP", PAD_UP, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("RIGHT", PAD_RIGHT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DOWN", PAD_DOWN, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("LEFT", PAD_LEFT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("TRIANGLE", PAD_TRIANGLE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("CIRCLE", PAD_CIRCLE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("CROSS", PAD_CROSS, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SQUARE", PAD_SQUARE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("L1", PAD_L1, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("R1", PAD_R1, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("L2", PAD_L2, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("R2", PAD_R2, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("L3", PAD_L3, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("R3", PAD_R3, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("TYPE_NEJICON", PAD_TYPE_NEJICON, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("TYPE_KONAMIGUN", PAD_TYPE_KONAMIGUN, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("TYPE_DIGITAL", PAD_TYPE_DIGITAL, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("TYPE_ANALOG", PAD_TYPE_ANALOG, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("TYPE_NAMCOGUN", PAD_TYPE_NAMCOGUN, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("TYPE_DUALSHOCK", PAD_TYPE_DUALSHOCK, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("TYPE_JOGCON", PAD_TYPE_JOGCON, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("TYPE_EX_TSURICON", PAD_TYPE_EX_TSURICON, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("TYPE_EX_JOGCON", PAD_TYPE_EX_JOGCON, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("STATE_DISCONN", PAD_STATE_DISCONN, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("STATE_FINDPAD", PAD_STATE_FINDPAD, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("STATE_FINDCTP1", PAD_STATE_FINDCTP1, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("STATE_EXECCMD", PAD_STATE_EXECCMD, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("STATE_STABLE", PAD_STATE_STABLE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("STATE_ERROR", PAD_STATE_ERROR, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MMODE_DIGITAL", PAD_MMODE_DIGITAL, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MMODE_DUALSHOCK", PAD_MMODE_DUALSHOCK, JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_pad_proto_funcs[] = {
    JS_CFUNC_DEF("update", 0, js_pad_update),
    JS_CFUNC_DEF("pressed", 1, js_pad_check),
    JS_CFUNC_DEF("justPressed", 1, js_pad_justpressed),
    JS_CFUNC_DEF("setEventHandler", 0, js_pad_seteventhandler),
    JS_CGETSET_MAGIC_DEF("btns", js_pad_get_prop, js_pad_set_prop, 0),
    JS_CGETSET_MAGIC_DEF("lx", js_pad_get_prop, js_pad_set_prop, 1),
    JS_CGETSET_MAGIC_DEF("ly", js_pad_get_prop, js_pad_set_prop, 2),
    JS_CGETSET_MAGIC_DEF("rx", js_pad_get_prop, js_pad_set_prop, 3),
    JS_CGETSET_MAGIC_DEF("ry", js_pad_get_prop, js_pad_set_prop, 4),
    JS_CGETSET_MAGIC_DEF("old_btns", js_pad_get_prop, js_pad_set_prop, 5),
    JS_CGETSET_MAGIC_DEF("old_lx", js_pad_get_prop, js_pad_set_prop, 6),
    JS_CGETSET_MAGIC_DEF("old_ly", js_pad_get_prop, js_pad_set_prop, 7),
    JS_CGETSET_MAGIC_DEF("old_rx", js_pad_get_prop, js_pad_set_prop, 8),
    JS_CGETSET_MAGIC_DEF("old_ry", js_pad_get_prop, js_pad_set_prop, 9),
};

static void js_pad_finalizer(JSRuntime *rt, JSValue val)
{
    AthenaPad *pad = JS_GetOpaque(val, js_pads_class_id);
    if (pad)
        athena_pad_close(pad);
}

static JSClassDef js_pads_class = {
    "Pad",
    .finalizer = js_pad_finalizer,
};

static int js_pads_init(JSContext *ctx, JSModuleDef *m)
{
    JSValue proto;

    JS_NewClassID(&js_pads_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_pads_class_id, &js_pads_class);
    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, js_pad_proto_funcs, countof(js_pad_proto_funcs));
    JS_SetClassProto(ctx, js_pads_class_id, proto);

    return JS_SetModuleExportList(ctx, m, module_funcs, countof(module_funcs));
}

JSModuleDef *athena_pads_init(JSContext *ctx)
{
    return athena_push_module(ctx, js_pads_init, module_funcs, countof(module_funcs), "Pads");
}
