#include <ath_env.h>
#include <athena/color.h>

static JSValue js_color_ctor(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	uint32_t r, g, b, a = 0x80;
	JS_ToUint32(ctx, &r, argv[0]);
	JS_ToUint32(ctx, &g, argv[1]);
	JS_ToUint32(ctx, &b, argv[2]);
	if (argc == 4) JS_ToUint32(ctx, &a, argv[3]);
	return JS_NewUint32(ctx, athena_color_rgba(r, g, b, a));
}

static JSValue js_color_get_r(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	uint32_t color;
	JS_ToUint32(ctx, &color, argv[0]);
	return JS_NewUint32(ctx, athena_color_get_r(color));
}

static JSValue js_color_get_g(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	uint32_t color;
	JS_ToUint32(ctx, &color, argv[0]);
	return JS_NewUint32(ctx, athena_color_get_g(color));
}

static JSValue js_color_get_b(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	uint32_t color;
	JS_ToUint32(ctx, &color, argv[0]);
	return JS_NewUint32(ctx, athena_color_get_b(color));
}

static JSValue js_color_get_a(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	uint32_t color;
	JS_ToUint32(ctx, &color, argv[0]);
	return JS_NewUint32(ctx, athena_color_get_a(color));
}

static JSValue js_color_set_r(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	uint32_t color, r;
	JS_ToUint32(ctx, &color, argv[0]);
	JS_ToUint32(ctx, &r, argv[1]);
	return JS_NewUint32(ctx, athena_color_set_r(color, r));
}

static JSValue js_color_set_g(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	uint32_t color, g;
	JS_ToUint32(ctx, &color, argv[0]);
	JS_ToUint32(ctx, &g, argv[1]);
	return JS_NewUint32(ctx, athena_color_set_g(color, g));
}

static JSValue js_color_set_b(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	uint32_t color, b;
	JS_ToUint32(ctx, &color, argv[0]);
	JS_ToUint32(ctx, &b, argv[1]);
	return JS_NewUint32(ctx, athena_color_set_b(color, b));
}

static JSValue js_color_set_a(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	uint32_t color, a;
	JS_ToUint32(ctx, &color, argv[0]);
	JS_ToUint32(ctx, &a, argv[1]);
	return JS_NewUint32(ctx, athena_color_set_a(color, a));
}

static const JSCFunctionListEntry color_funcs[] = {
	JS_CFUNC_DEF("new",  4, js_color_ctor),
	JS_CFUNC_DEF("getR", 1, js_color_get_r),
	JS_CFUNC_DEF("getG", 1, js_color_get_g),
	JS_CFUNC_DEF("getB", 1, js_color_get_b),
	JS_CFUNC_DEF("getA", 1, js_color_get_a),
	JS_CFUNC_DEF("setR", 2, js_color_set_r),
	JS_CFUNC_DEF("setG", 2, js_color_set_g),
	JS_CFUNC_DEF("setB", 2, js_color_set_b),
	JS_CFUNC_DEF("setA", 2, js_color_set_a),
};

static int js_color_init(JSContext *ctx, JSModuleDef *m) {
    return JS_SetModuleExportList(ctx, m, color_funcs, countof(color_funcs));
}

JSModuleDef *athena_color_init(JSContext* ctx){
    return athena_push_module(ctx, js_color_init, color_funcs, countof(color_funcs), "Color");
}
