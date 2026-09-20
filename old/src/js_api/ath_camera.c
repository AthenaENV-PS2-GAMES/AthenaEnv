#include <ath_env.h>
#include <athena/camera.h>

static JSValue js_camera_init(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    int mode = 0;
    if (argc == 1)
        JS_ToInt32(ctx, &mode, argv[0]);
    athena_camera_startup(mode);
    return JS_UNDEFINED;
}

static JSValue js_camera_count(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    return JS_NewInt32(ctx, athena_camera_device_count());
}

static JSValue js_camera_open(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    int index = 0;
    if (argc == 1)
        JS_ToInt32(ctx, &index, argv[0]);
    return JS_NewInt32(ctx, athena_camera_open(index));
}

static JSValue js_camera_close(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    int devid;
    JS_ToInt32(ctx, &devid, argv[0]);
    return JS_NewInt32(ctx, athena_camera_close(devid));
}

static JSValue js_camera_status(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    int devid;
    JS_ToInt32(ctx, &devid, argv[0]);
    return JS_NewInt32(ctx, athena_camera_status(devid));
}

static JSValue js_camera_bandwidth(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    int devid, bandwidth;
    JS_ToInt32(ctx, &devid, argv[0]);
    JS_ToInt32(ctx, &bandwidth, argv[1]);
    return JS_NewInt32(ctx, athena_camera_set_bandwidth(devid, bandwidth));
}

static JSValue js_camera_packet(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    int devid;
    JS_ToInt32(ctx, &devid, argv[0]);
    return JS_NewInt32(ctx, athena_camera_read_packet(devid));
}

static JSValue js_camera_led(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    int devid, mode;
    JS_ToInt32(ctx, &devid, argv[0]);
    JS_ToInt32(ctx, &mode, argv[1]);
    return JS_NewInt32(ctx, athena_camera_set_led(devid, mode));
}

static JSValue js_camera_setconfig(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    int devid;
    PS2CAM_DEVICE_CONFIG cfg;

    cfg.ssize = sizeof(cfg);
    cfg.mask = CAM_CONFIG_MASK_DIMENSION | CAM_CONFIG_MASK_DIVIDER |
               CAM_CONFIG_MASK_OFFSET | CAM_CONFIG_MASK_FRAMERATE;
    cfg.width = 640;
    cfg.height = 480;
    cfg.x_offset = 0x00;
    cfg.y_offset = 0x00;
    cfg.h_divider = 0;
    cfg.v_divider = 0;
    cfg.framerate = 30;

    JS_ToInt32(ctx, &devid, argv[0]);

    if (argc == 3) {
        JS_ToInt32(ctx, &cfg.width, argv[1]);
        JS_ToInt32(ctx, &cfg.height, argv[2]);
    }

    if (argc == 5) {
        JS_ToInt32(ctx, &cfg.x_offset, argv[3]);
        JS_ToInt32(ctx, &cfg.y_offset, argv[4]);
    }

    if (argc == 6)
        JS_ToInt32(ctx, &cfg.framerate, argv[5]);

    if (argc == 8) {
        JS_ToInt32(ctx, &cfg.h_divider, argv[6]);
        JS_ToInt32(ctx, &cfg.v_divider, argv[7]);
    }

    athena_camera_set_config(devid, &cfg);
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry module_funcs[] = {
    JS_CFUNC_DEF("init", 1, js_camera_init),
    JS_CFUNC_DEF("count", 0, js_camera_count),
    JS_CFUNC_DEF("open", 1, js_camera_open),
    JS_CFUNC_DEF("close", 1, js_camera_close),
    JS_CFUNC_DEF("getStatus", 1, js_camera_status),
    JS_CFUNC_DEF("setBandwidth", 2, js_camera_bandwidth),
    JS_CFUNC_DEF("readPacket", 1, js_camera_packet),
    JS_CFUNC_DEF("setLED", 2, js_camera_led),
    JS_CFUNC_DEF("setConfig", 8, js_camera_setconfig),
};

static int camera_init(JSContext *ctx, JSModuleDef *m)
{
    return JS_SetModuleExportList(ctx, m, module_funcs, countof(module_funcs));
}

JSModuleDef *athena_camera_init(JSContext *ctx)
{
    return athena_push_module(ctx, camera_init, module_funcs, countof(module_funcs), "Camera");
}
