#include <ath_env.h>
#include <athena/box2d.h>
#include <athena/box2ddraw.h>
#include <athena/js/box2d.h>

#include "ath_box2ddraw.h"

#define MAX_SCALE 1e6f
#define MAX_OFFSET 1e6f

/* options[key] as a float in [min, max]: 1 read, 0 absent, -1 error. */
static int option_float(JSContext *ctx, JSValueConst options, const char *key, float min, float max, float *out) {
    JSValue value = JS_GetPropertyStr(ctx, options, key);
    float number;
    int ret = 1;

    if (JS_IsException(value))
        return -1;
    if (JS_IsUndefined(value))
        return 0;
    if (!JS_IsNumber(value)) {
        JS_ThrowTypeError(ctx, "Box2DDraw.draw: options.%s must be a number", key);
        ret = -1;
    } else if (JS_ToFloat32(ctx, &number, value) < 0) {
        ret = -1;
    /* Bit test: Infinity and NaN narrow to finite-looking floats on the EE. */
    } else if (!athena_box2d_valid_float(number) || number < min || number > max) {
        JS_ThrowRangeError(ctx, "Box2DDraw.draw: options.%s must be between %g and %g", key, (double)min, (double)max);
        ret = -1;
    } else {
        *out = number;
    }
    JS_FreeValue(ctx, value);
    return ret;
}

static int option_bool(JSContext *ctx, JSValueConst options, const char *key, bool *out) {
    JSValue value = JS_GetPropertyStr(ctx, options, key);
    int ret = 1;

    if (JS_IsException(value))
        return -1;
    if (JS_IsUndefined(value))
        return 0;
    if (!JS_IsBool(value) && !JS_IsNumber(value)) {
        JS_ThrowTypeError(ctx, "Box2DDraw.draw: options.%s must be a boolean", key);
        ret = -1;
    } else {
        *out = JS_ToBool(ctx, value);
    }
    JS_FreeValue(ctx, value);
    return ret;
}

/* Box2DDraw.draw(world, options?) */
static JSValue box2ddraw_draw(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaBox2DDrawOptions options;
    b2WorldId world;

    if (argc < 1 || argc > 2)
        return JS_ThrowTypeError(ctx, "Box2DDraw.draw expects between 1 and 2 arguments");
    world = athena_box2d_js_world(ctx, argv[0], "Box2DDraw.draw");
    if (B2_IS_NULL(world))
        return JS_EXCEPTION;

    athena_box2d_draw_defaults(&options);
    if (argc > 1 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
        JSValueConst o = argv[1];
        if (!JS_IsObject(o))
            return JS_ThrowTypeError(ctx, "Box2DDraw.draw: options must be an object");
        if (option_float(ctx, o, "scale", 1e-3f, MAX_SCALE, &options.scale) < 0 ||
            option_float(ctx, o, "offsetX", -MAX_OFFSET, MAX_OFFSET, &options.offsetX) < 0 ||
            option_float(ctx, o, "offsetY", -MAX_OFFSET, MAX_OFFSET, &options.offsetY) < 0 ||
            option_bool(ctx, o, "flipY", &options.flipY) < 0 ||
            option_bool(ctx, o, "fill", &options.fill) < 0 ||
            option_bool(ctx, o, "shapes", &options.shapes) < 0 ||
            option_bool(ctx, o, "joints", &options.joints) < 0 ||
            option_bool(ctx, o, "jointExtras", &options.jointExtras) < 0 ||
            option_bool(ctx, o, "bounds", &options.bounds) < 0 ||
            option_bool(ctx, o, "contacts", &options.contacts) < 0 ||
            option_bool(ctx, o, "mass", &options.mass) < 0)
            return JS_EXCEPTION;
    }
    if (!athena_box2d_draw(world, &options))
        return JS_ThrowInternalError(ctx, "Box2DDraw.draw: the graphics service is not initialized");
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry box2ddraw_module_funcs[] = {
    JS_CFUNC_DEF("draw", 2, box2ddraw_draw),
};

static int box2ddraw_module_init(JSContext *ctx, JSModuleDef *m) {
    return JS_SetModuleExportList(ctx, m, box2ddraw_module_funcs, countof(box2ddraw_module_funcs));
}

JSModuleDef *athena_box2ddraw_init(JSContext *ctx) {
    return athena_push_module(ctx, box2ddraw_module_init, box2ddraw_module_funcs,
        countof(box2ddraw_module_funcs), "Box2DDraw");
}
