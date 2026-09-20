#include <stdint.h>

#include <ath_env.h>
#include <graphics.h>

#include "ath_color.h"

static int color_argc(JSContext *ctx, int argc, int minimum, int maximum,
    const char *name) {
    if (argc < minimum || argc > maximum) {
        if (minimum == maximum) {
            JS_ThrowTypeError(ctx, "%s expects %d argument%s", name, minimum,
                minimum == 1 ? "" : "s");
        } else {
            JS_ThrowTypeError(ctx, "%s expects between %d and %d arguments",
                name, minimum, maximum);
        }
        return 0;
    }
    return 1;
}

static int color_value(JSContext *ctx, JSValueConst value, uint32_t *result) {
    return JS_ToUint32(ctx, result, value) == 0;
}

static JSValue color_new(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    uint32_t r, g, b, a = 0x80;
    if (!color_argc(ctx, argc, 3, 4, "Color.new"))
        return JS_EXCEPTION;
    if (!color_value(ctx, argv[0], &r) || !color_value(ctx, argv[1], &g) ||
        !color_value(ctx, argv[2], &b))
        return JS_EXCEPTION;
    if (argc == 4 && !color_value(ctx, argv[3], &a))
        return JS_EXCEPTION;
    return JS_NewUint32(ctx, (r & 0xff) | ((g & 0xff) << 8) |
        ((b & 0xff) << 16) | ((a & 0xff) << 24));
}

static JSValue color_get_component(JSContext *ctx, JSValueConst value,
    int component) {
    uint32_t color;
    if (!color_value(ctx, value, &color))
        return JS_EXCEPTION;
    switch (component) {
    case 0: return JS_NewUint32(ctx, R(color));
    case 1: return JS_NewUint32(ctx, G(color));
    case 2: return JS_NewUint32(ctx, B(color));
    default: return JS_NewUint32(ctx, A(color));
    }
}

static JSValue color_get_r(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    if (!color_argc(ctx, argc, 1, 1, "Color.getR"))
        return JS_EXCEPTION;
    return color_get_component(ctx, argv[0], 0);
}

static JSValue color_get_g(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    if (!color_argc(ctx, argc, 1, 1, "Color.getG"))
        return JS_EXCEPTION;
    return color_get_component(ctx, argv[0], 1);
}

static JSValue color_get_b(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    if (!color_argc(ctx, argc, 1, 1, "Color.getB"))
        return JS_EXCEPTION;
    return color_get_component(ctx, argv[0], 2);
}

static JSValue color_get_a(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    if (!color_argc(ctx, argc, 1, 1, "Color.getA"))
        return JS_EXCEPTION;
    return color_get_component(ctx, argv[0], 3);
}

static JSValue color_set_component(JSContext *ctx, JSValueConst *argv,
    int component) {
    uint32_t color, value;
    if (!color_value(ctx, argv[0], &color) ||
        !color_value(ctx, argv[1], &value))
        return JS_EXCEPTION;
    value &= 0xff;
    switch (component) {
    case 0: color = (color & 0xffffff00U) | value; break;
    case 1: color = (color & 0xffff00ffU) | (value << 8); break;
    case 2: color = (color & 0xff00ffffU) | (value << 16); break;
    default: color = (color & 0x00ffffffU) | (value << 24); break;
    }
    return JS_NewUint32(ctx, color);
}

static JSValue color_set_r(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    if (!color_argc(ctx, argc, 2, 2, "Color.setR"))
        return JS_EXCEPTION;
    return color_set_component(ctx, argv, 0);
}

static JSValue color_set_g(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    if (!color_argc(ctx, argc, 2, 2, "Color.setG"))
        return JS_EXCEPTION;
    return color_set_component(ctx, argv, 1);
}

static JSValue color_set_b(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    if (!color_argc(ctx, argc, 2, 2, "Color.setB"))
        return JS_EXCEPTION;
    return color_set_component(ctx, argv, 2);
}

static JSValue color_set_a(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    if (!color_argc(ctx, argc, 2, 2, "Color.setA"))
        return JS_EXCEPTION;
    return color_set_component(ctx, argv, 3);
}

static const JSCFunctionListEntry color_funcs[] = {
    JS_CFUNC_DEF("new", 4, color_new),
    JS_CFUNC_DEF("getR", 1, color_get_r),
    JS_CFUNC_DEF("getG", 1, color_get_g),
    JS_CFUNC_DEF("getB", 1, color_get_b),
    JS_CFUNC_DEF("getA", 1, color_get_a),
    JS_CFUNC_DEF("setR", 2, color_set_r),
    JS_CFUNC_DEF("setG", 2, color_set_g),
    JS_CFUNC_DEF("setB", 2, color_set_b),
    JS_CFUNC_DEF("setA", 2, color_set_a),
};

static int color_module_init(JSContext *ctx, JSModuleDef *module) {
    graphics_service_init();
    return JS_SetModuleExportList(ctx, module, color_funcs,
        countof(color_funcs));
}

JSModuleDef *athena_color_init(JSContext *ctx) {
    return athena_push_module(ctx, color_module_init, color_funcs,
        countof(color_funcs), "Color");
}
