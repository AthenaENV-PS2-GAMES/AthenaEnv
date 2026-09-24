#include <limits.h>
#include <math.h>
#include <stdint.h>

#include <ath_env.h>
#include <athena/graphics.h>

#include "ath_draw.h"

static int draw_argc(JSContext *ctx, int argc, int expected, const char *name)
{
    if (argc != expected) {
        JS_ThrowTypeError(ctx, "%s expects exactly %d arguments", name,
            expected);
        return 0;
    }
    return 1;
}

static int draw_number(JSContext *ctx, JSValueConst value, float *result,
    const char *name)
{
    if (JS_ToFloat32(ctx, result, value))
        return 0;
    if (!isfinite(*result)) {
        JS_ThrowRangeError(ctx, "%s must be a finite number", name);
        return 0;
    }
    return 1;
}

static int draw_color(JSContext *ctx, JSValueConst value, Color *result)
{
    uint32_t color;
    if (JS_ToUint32(ctx, &color, value))
        return 0;
    *result = color;
    return 1;
}

static int draw_ready(JSContext *ctx)
{
    if (!getGSGLOBAL()) {
        JS_ThrowInternalError(ctx, "Graphics service is not initialized");
        return 0;
    }
    return 1;
}

static JSValue draw_point_js(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    float x, y;
    Color color;
    if (!draw_argc(ctx, argc, 3, "Draw.point") || !draw_ready(ctx) ||
        !draw_number(ctx, argv[0], &x, "Draw.point x") ||
        !draw_number(ctx, argv[1], &y, "Draw.point y") ||
        !draw_color(ctx, argv[2], &color))
        return JS_EXCEPTION;
    draw_point(x, y, color);
    return JS_UNDEFINED;
}

static JSValue draw_line_js(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    float x1, y1, x2, y2;
    Color color;
    if (!draw_argc(ctx, argc, 5, "Draw.line") || !draw_ready(ctx) ||
        !draw_number(ctx, argv[0], &x1, "Draw.line x1") ||
        !draw_number(ctx, argv[1], &y1, "Draw.line y1") ||
        !draw_number(ctx, argv[2], &x2, "Draw.line x2") ||
        !draw_number(ctx, argv[3], &y2, "Draw.line y2") ||
        !draw_color(ctx, argv[4], &color))
        return JS_EXCEPTION;
    draw_line(x1, y1, x2, y2, color);
    return JS_UNDEFINED;
}

static int draw_triangle_coordinates(JSContext *ctx, JSValueConst *argv,
    float *x1, float *y1, float *x2, float *y2, float *x3, float *y3,
    const char *name)
{
    return draw_number(ctx, argv[0], x1, name) &&
        draw_number(ctx, argv[1], y1, name) &&
        draw_number(ctx, argv[2], x2, name) &&
        draw_number(ctx, argv[3], y2, name) &&
        draw_number(ctx, argv[4], x3, name) &&
        draw_number(ctx, argv[5], y3, name);
}

static JSValue draw_triangle_js(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    float x1, y1, x2, y2, x3, y3;
    Color color;
    if (!draw_argc(ctx, argc, 7, "Draw.triangle") || !draw_ready(ctx) ||
        !draw_triangle_coordinates(ctx, argv, &x1, &y1, &x2, &y2, &x3, &y3,
            "Draw.triangle coordinate") ||
        !draw_color(ctx, argv[6], &color))
        return JS_EXCEPTION;
    draw_triangle(x1, y1, x2, y2, x3, y3, color);
    return JS_UNDEFINED;
}

static JSValue draw_triangle_gouraud_js(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv)
{
    float x1, y1, x2, y2, x3, y3;
    Color color1, color2, color3;
    if (!draw_argc(ctx, argc, 9, "Draw.triangleGouraud") ||
        !draw_ready(ctx) ||
        !draw_number(ctx, argv[0], &x1, "Draw.triangleGouraud x1") ||
        !draw_number(ctx, argv[1], &y1, "Draw.triangleGouraud y1") ||
        !draw_color(ctx, argv[2], &color1) ||
        !draw_number(ctx, argv[3], &x2, "Draw.triangleGouraud x2") ||
        !draw_number(ctx, argv[4], &y2, "Draw.triangleGouraud y2") ||
        !draw_color(ctx, argv[5], &color2) ||
        !draw_number(ctx, argv[6], &x3, "Draw.triangleGouraud x3") ||
        !draw_number(ctx, argv[7], &y3, "Draw.triangleGouraud y3") ||
        !draw_color(ctx, argv[8], &color3))
        return JS_EXCEPTION;
    draw_triangle_gouraud(x1, y1, x2, y2, x3, y3, color1, color2, color3);
    return JS_UNDEFINED;
}

static JSValue draw_quad_js(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    float x1, y1, x2, y2, x3, y3, x4, y4;
    Color color;
    if (!draw_argc(ctx, argc, 9, "Draw.quad") || !draw_ready(ctx) ||
        !draw_number(ctx, argv[0], &x1, "Draw.quad x1") ||
        !draw_number(ctx, argv[1], &y1, "Draw.quad y1") ||
        !draw_number(ctx, argv[2], &x2, "Draw.quad x2") ||
        !draw_number(ctx, argv[3], &y2, "Draw.quad y2") ||
        !draw_number(ctx, argv[4], &x3, "Draw.quad x3") ||
        !draw_number(ctx, argv[5], &y3, "Draw.quad y3") ||
        !draw_number(ctx, argv[6], &x4, "Draw.quad x4") ||
        !draw_number(ctx, argv[7], &y4, "Draw.quad y4") ||
        !draw_color(ctx, argv[8], &color))
        return JS_EXCEPTION;
    draw_quad(x1, y1, x2, y2, x3, y3, x4, y4, color);
    return JS_UNDEFINED;
}

static JSValue draw_quad_gouraud_js(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    float x1, y1, x2, y2, x3, y3, x4, y4;
    Color color1, color2, color3, color4;
    if (!draw_argc(ctx, argc, 12, "Draw.quadGouraud") ||
        !draw_ready(ctx) ||
        !draw_number(ctx, argv[0], &x1, "Draw.quadGouraud x1") ||
        !draw_number(ctx, argv[1], &y1, "Draw.quadGouraud y1") ||
        !draw_color(ctx, argv[2], &color1) ||
        !draw_number(ctx, argv[3], &x2, "Draw.quadGouraud x2") ||
        !draw_number(ctx, argv[4], &y2, "Draw.quadGouraud y2") ||
        !draw_color(ctx, argv[5], &color2) ||
        !draw_number(ctx, argv[6], &x3, "Draw.quadGouraud x3") ||
        !draw_number(ctx, argv[7], &y3, "Draw.quadGouraud y3") ||
        !draw_color(ctx, argv[8], &color3) ||
        !draw_number(ctx, argv[9], &x4, "Draw.quadGouraud x4") ||
        !draw_number(ctx, argv[10], &y4, "Draw.quadGouraud y4") ||
        !draw_color(ctx, argv[11], &color4))
        return JS_EXCEPTION;
    draw_quad_gouraud(x1, y1, x2, y2, x3, y3, x4, y4,
        color1, color2, color3, color4);
    return JS_UNDEFINED;
}

static JSValue draw_rect(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    float x, y, width, height;
    Color color;
    if (!draw_argc(ctx, argc, 5, "Draw.rect") || !draw_ready(ctx) ||
        !draw_number(ctx, argv[0], &x, "Draw.rect x") ||
        !draw_number(ctx, argv[1], &y, "Draw.rect y") ||
        !draw_number(ctx, argv[2], &width, "Draw.rect width") ||
        !draw_number(ctx, argv[3], &height, "Draw.rect height"))
        return JS_EXCEPTION;
    if (width < 1.0f || height < 1.0f)
        return JS_ThrowRangeError(ctx,
            "Draw.rect width and height must be at least 1");
    if (!draw_color(ctx, argv[4], &color))
        return JS_EXCEPTION;
    if (width > INT_MAX || height > INT_MAX)
        return JS_ThrowRangeError(ctx, "Draw.rect dimensions are too large");
    draw_sprite(x, y, (int)width, (int)height, color);
    return JS_UNDEFINED;
}

static JSValue draw_circle_js(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    float x, y, radius;
    Color color;
    int filled = 1;
    if (argc != 4 && argc != 5) {
        JS_ThrowTypeError(ctx, "Draw.circle expects between 4 and 5 arguments");
        return JS_EXCEPTION;
    }
    if (!draw_ready(ctx) ||
        !draw_number(ctx, argv[0], &x, "Draw.circle x") ||
        !draw_number(ctx, argv[1], &y, "Draw.circle y") ||
        !draw_number(ctx, argv[2], &radius, "Draw.circle radius") ||
        !draw_color(ctx, argv[3], &color))
        return JS_EXCEPTION;
    if (radius <= 0.0f)
        return JS_ThrowRangeError(ctx, "Draw.circle radius must be positive");
    if (argc == 5) {
        if (!JS_IsBool(argv[4]))
            return JS_ThrowTypeError(ctx, "Draw.circle filled must be boolean");
        filled = JS_ToBool(ctx, argv[4]);
    }
    draw_circle(x, y, radius, color, (u8)filled);
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry draw_funcs[] = {
    JS_CFUNC_DEF("point", 3, draw_point_js),
    JS_CFUNC_DEF("line", 5, draw_line_js),
    JS_CFUNC_DEF("triangle", 7, draw_triangle_js),
    JS_CFUNC_DEF("triangleGouraud", 9, draw_triangle_gouraud_js),
    JS_CFUNC_DEF("quad", 9, draw_quad_js),
    JS_CFUNC_DEF("quadGouraud", 12, draw_quad_gouraud_js),
    JS_CFUNC_DEF("rect", 5, draw_rect),
    JS_CFUNC_DEF("circle", 5, draw_circle_js),
};

static int draw_module_init(JSContext *ctx, JSModuleDef *module)
{
    return JS_SetModuleExportList(ctx, module, draw_funcs,
        countof(draw_funcs));
}

JSModuleDef *athena_draw_init(JSContext *ctx)
{
    return athena_push_module(ctx, draw_module_init, draw_funcs,
        countof(draw_funcs), "Draw");
}
