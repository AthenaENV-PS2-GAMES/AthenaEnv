#include <stdlib.h>
#include <malloc.h>
#include <string.h>

#include <graphics.h>
#include <ath_env.h>
#include <athena/draw.h>
#include <ath_bindings.h>
#include <macros.h>

static JSValue js_draw_point(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    float x, y;
    Color color;

    JS_ToFloat32(ctx, &x, argv[0]);
    JS_ToFloat32(ctx, &y, argv[1]);
	JS_ToUint32(ctx, &color, argv[2]);
	athena_draw_point(x, y, color);
	return JS_UNDEFINED;
}

static JSValue js_draw_line(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    float x1, y1, x2, y2;
    Color color;

    JS_ToFloat32(ctx, &x1, argv[0]);
    JS_ToFloat32(ctx, &y1, argv[1]);
    JS_ToFloat32(ctx, &x2, argv[2]);
    JS_ToFloat32(ctx, &y2, argv[3]);
	JS_ToUint32(ctx, &color, argv[4]);
	athena_draw_line(x1, y1, x2, y2, color);
	return JS_UNDEFINED;
}

static JSValue js_draw_triangle(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    float x1, y1, x2, y2, x3, y3;
    Color color1, color2, color3;

    JS_ToFloat32(ctx, &x1, argv[0]);
    JS_ToFloat32(ctx, &y1, argv[1]);
    JS_ToFloat32(ctx, &x2, argv[2]);
    JS_ToFloat32(ctx, &y2, argv[3]);
    JS_ToFloat32(ctx, &x3, argv[4]);
    JS_ToFloat32(ctx, &y3, argv[5]);
	JS_ToUint32(ctx, &color1, argv[6]);

    if (argc == 7) {
        athena_draw_triangle(x1, y1, x2, y2, x3, y3, color1);
    } else {
        JS_ToUint32(ctx, &color2, argv[7]);
        JS_ToUint32(ctx, &color3, argv[8]);
        athena_draw_triangle_gouraud(x1, y1, x2, y2, x3, y3, color1, color2, color3);
    }
	return JS_UNDEFINED;
}

static JSValue js_draw_quad(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    float x1, y1, x2, y2, x3, y3, x4, y4;
    Color color1, color2, color3, color4;

    JS_ToFloat32(ctx, &x1, argv[0]);
    JS_ToFloat32(ctx, &y1, argv[1]);
    JS_ToFloat32(ctx, &x2, argv[2]);
    JS_ToFloat32(ctx, &y2, argv[3]);
    JS_ToFloat32(ctx, &x3, argv[4]);
    JS_ToFloat32(ctx, &y3, argv[5]);
    JS_ToFloat32(ctx, &x4, argv[6]);
    JS_ToFloat32(ctx, &y4, argv[7]);
	JS_ToUint32(ctx, &color1, argv[8]);

    if (argc == 9) {
        athena_draw_quad(x1, y1, x2, y2, x3, y3, x4, y4, color1);
    } else {
        JS_ToUint32(ctx, &color2, argv[9]);
	    JS_ToUint32(ctx, &color3, argv[10]);
	    JS_ToUint32(ctx, &color4, argv[11]);
        athena_draw_quad_gouraud(x1, y1, x2, y2, x3, y3, x4, y4, color1, color2, color3, color4);
    }
	return JS_UNDEFINED;
}

static JSValue js_draw_rect(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    float x, y, w, h;
    Color color;

    JS_ToFloat32(ctx, &x, argv[0]);
    JS_ToFloat32(ctx, &y, argv[1]);
    JS_ToFloat32(ctx, &w, argv[2]);
    JS_ToFloat32(ctx, &h, argv[3]);
	JS_ToUint32(ctx, &color, argv[4]);
	athena_draw_rect(x, y, w, h, color);
	return JS_UNDEFINED;
}

static JSValue js_draw_circle(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    bool filled = true;
    float x, y, r;
    Color color;

    JS_ToFloat32(ctx, &x, argv[0]);
    JS_ToFloat32(ctx, &y, argv[1]);
    JS_ToFloat32(ctx, &r, argv[2]);
    JS_ToUint32(ctx, &color, argv[3]);
    if (argc == 5) filled = JS_ToBool(ctx, argv[4]);
	athena_draw_circle(x, y, r, color, filled);
	return JS_UNDEFINED;
}

static const JSCFunctionListEntry module_funcs[] = {
	JS_CFUNC_DEF("point", 3, js_draw_point),
	JS_CFUNC_DEF("line", 5, js_draw_line),
	JS_CFUNC_DEF("triangle", 9, js_draw_triangle),
	JS_CFUNC_DEF("quad", 12, js_draw_quad),
	JS_CFUNC_DEF("rect", 5, js_draw_rect),
	JS_CFUNC_DEF("circle", 5, js_draw_circle),
};

static int js_draw_module_init(JSContext *ctx, JSModuleDef *m) {
    return JS_SetModuleExportList(ctx, m, module_funcs, countof(module_funcs));
}

JSModuleDef *athena_shape_init(JSContext* ctx){
	return athena_push_module(ctx, js_draw_module_init, module_funcs, countof(module_funcs), "Draw");
}
