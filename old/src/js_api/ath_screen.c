#include <ath_env.h>
#include <athena/screen.h>
#include <athena/image.h>
#include <ath_bindings.h>

static JSValue js_screen_set_clear_color(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	Color clear_color = GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x80, 0x00);
  	JS_ToInt32(ctx, &clear_color, argv[0]);
  	athena_screen_set_clear_color(clear_color);
  	return JS_UNDEFINED;
}

static JSValue js_screen_display(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	js_set_render_loop_func(ctx, JS_DupValue(ctx, argv[0]));
	return JS_UNDEFINED;
}

static JSValue js_screen_flip(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
  athena_screen_flip();
  return JS_UNDEFINED;
}

static JSValue js_screen_clear(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	Color color = GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x80, 0x00);
  	if (argc == 1) JS_ToInt32(ctx, &color, argv[0]);
	athena_screen_clear(color);
	return JS_UNDEFINED;
}

static JSValue js_screen_vblank(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	athena_screen_wait_vblank();
	return JS_UNDEFINED;
}

static JSValue js_screen_vsync(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	athena_screen_set_vsync(JS_ToBool(ctx, argv[0]));
	return JS_UNDEFINED;
}

static JSValue js_screen_fcount(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	athena_screen_set_frame_counter(JS_ToBool(ctx, argv[0]));
	return JS_UNDEFINED;
}

static JSValue js_screen_get_free_vram(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	uint32_t mode = VRAM_USED_TOTAL;
	if (argc)
		JS_ToUint32(ctx, &mode, argv[0]);
	return JS_NewUint32(ctx, (uint32_t)athena_screen_get_free_vram(mode));
}

static JSValue js_screen_get_fps(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	int time_intvl;
	JS_ToInt32(ctx, &time_intvl, argv[0]);
	return JS_NewFloat32(ctx, athena_screen_get_fps(time_intvl));
}

static JSValue js_screen_get_mode(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	AthenaVideoMode mode;
	JSValue obj = JS_NewObject(ctx);
	athena_screen_get_mode(&mode);
	JS_DefinePropertyValueStr(ctx, obj, "mode", JS_NewInt32(ctx, mode.mode), JS_PROP_C_W_E);
	JS_DefinePropertyValueStr(ctx, obj, "width", JS_NewInt32(ctx, mode.width), JS_PROP_C_W_E);
	JS_DefinePropertyValueStr(ctx, obj, "height", JS_NewInt32(ctx, mode.height), JS_PROP_C_W_E);
	JS_DefinePropertyValueStr(ctx, obj, "psm", JS_NewInt32(ctx, mode.psm), JS_PROP_C_W_E);
	JS_DefinePropertyValueStr(ctx, obj, "interlace", JS_NewInt32(ctx, mode.interlace), JS_PROP_C_W_E);
	JS_DefinePropertyValueStr(ctx, obj, "field", JS_NewInt32(ctx, mode.field), JS_PROP_C_W_E);
	JS_DefinePropertyValueStr(ctx, obj, "psmz", JS_NewInt32(ctx, mode.psmz), JS_PROP_C_W_E);
	JS_DefinePropertyValueStr(ctx, obj, "zbuffering", JS_NewBool(ctx, mode.zbuffering), JS_PROP_C_W_E);
	JS_DefinePropertyValueStr(ctx, obj, "double_buffering", JS_NewBool(ctx, mode.double_buffering), JS_PROP_C_W_E);
	return obj;
}

static JSValue js_screen_set_mode(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	AthenaVideoMode mode = {0};
	JSValue val;

	val = JS_GetPropertyStr(ctx, argv[0], "mode");
	JS_ToInt32(ctx, &mode.mode, val);
	JS_FreeValue(ctx, val);
	val = JS_GetPropertyStr(ctx, argv[0], "width");
	JS_ToInt32(ctx, &mode.width, val);
	JS_FreeValue(ctx, val);
	val = JS_GetPropertyStr(ctx, argv[0], "height");
	JS_ToInt32(ctx, &mode.height, val);
	JS_FreeValue(ctx, val);
	val = JS_GetPropertyStr(ctx, argv[0], "psm");
	JS_ToInt32(ctx, &mode.psm, val);
	JS_FreeValue(ctx, val);
	val = JS_GetPropertyStr(ctx, argv[0], "interlace");
	JS_ToInt32(ctx, &mode.interlace, val);
	JS_FreeValue(ctx, val);
	val = JS_GetPropertyStr(ctx, argv[0], "field");
	JS_ToInt32(ctx, &mode.field, val);
	JS_FreeValue(ctx, val);
	val = JS_GetPropertyStr(ctx, argv[0], "psmz");
	JS_ToInt32(ctx, &mode.psmz, val);
	JS_FreeValue(ctx, val);
	val = JS_GetPropertyStr(ctx, argv[0], "zbuffering");
	mode.zbuffering = JS_ToBool(ctx, val);
	JS_FreeValue(ctx, val);
	val = JS_GetPropertyStr(ctx, argv[0], "double_buffering");
	mode.double_buffering = JS_ToBool(ctx, val);
	JS_FreeValue(ctx, val);
	val = JS_GetPropertyStr(ctx, argv[0], "pass_count");
	JS_ToUint32(ctx, &mode.pass_count, val);
	JS_FreeValue(ctx, val);

	athena_screen_set_mode(&mode);
	return JS_UNDEFINED;
}

static JSValue js_screen_alpha_equation(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	int a, b, c, d, fix;
	JS_ToInt32(ctx, &a, argv[0]);
	JS_ToInt32(ctx, &b, argv[1]);
	JS_ToInt32(ctx, &c, argv[2]);
	JS_ToInt32(ctx, &d, argv[3]);
	JS_ToInt32(ctx, &fix, argv[4]);
	return JS_NewInt64(ctx, athena_screen_alpha_equation(a, b, c, d, fix));
}

static JSValue js_screen_set_param(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	int param;
	uint64_t value;
	JS_ToInt32(ctx, &param, argv[0]);

	switch (param) {
		case ALPHA_TEST_ENABLE:
		case DST_ALPHA_TEST_ENABLE:
		case DEPTH_TEST_ENABLE:
		case PIXEL_ALPHA_BLEND_ENABLE:
			value = (uint64_t)JS_ToBool(ctx, argv[1]);
			break;
		case ALPHA_TEST_METHOD:
		case ALPHA_TEST_REF:
		case ALPHA_TEST_FAIL:
		case DST_ALPHA_TEST_METHOD:
		case DEPTH_TEST_METHOD:
		case COLOR_CLAMP_MODE:
			JS_ToInt64(ctx, &value, argv[1]);
			break;
		case ALPHA_BLEND_EQUATION:
			{
				int a, b, c, d, fix;
				JS_ToInt32(ctx, &a, JS_GetPropertyStr(ctx, argv[1], "a"));
				JS_ToInt32(ctx, &b, JS_GetPropertyStr(ctx, argv[1], "b"));
				JS_ToInt32(ctx, &c, JS_GetPropertyStr(ctx, argv[1], "c"));
				JS_ToInt32(ctx, &d, JS_GetPropertyStr(ctx, argv[1], "d"));
				JS_ToInt32(ctx, &fix, JS_GetPropertyStr(ctx, argv[1], "fix"));
				value = athena_screen_alpha_equation(a, b, c, d, fix);
			}
			break;
		case SCISSOR_BOUNDS:
			if (JS_IsNumber(argv[1])) {
				JS_ToInt64(ctx, &value, argv[1]);
			} else {
				int x0, x1, y0, y1;
				JS_ToInt32(ctx, &x0, JS_GetPropertyStr(ctx, argv[1], "x1"));
				JS_ToInt32(ctx, &y0, JS_GetPropertyStr(ctx, argv[1], "y1"));
				JS_ToInt32(ctx, &x1, JS_GetPropertyStr(ctx, argv[1], "x2"));
				JS_ToInt32(ctx, &y1, JS_GetPropertyStr(ctx, argv[1], "y2"));
				value = GS_SETREG_SCISSOR_1(x0, x1, y0, y1);
			}
			break;
		default:
			return JS_UNDEFINED;
	}

	athena_screen_set_param(param, value);
	return JS_UNDEFINED;
}

static JSValue js_screen_get_param(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	int param;
	JS_ToInt32(ctx, &param, argv[0]);
	uint64_t value = athena_screen_get_param(param);

	switch (param) {
		case ALPHA_TEST_ENABLE:
		case ALPHA_TEST_METHOD:
		case ALPHA_TEST_REF:
		case ALPHA_TEST_FAIL:
		case DST_ALPHA_TEST_ENABLE:
		case DST_ALPHA_TEST_METHOD:
		case DEPTH_TEST_ENABLE:
		case DEPTH_TEST_METHOD:
		case PIXEL_ALPHA_BLEND_ENABLE:
		case COLOR_CLAMP_MODE:
			return JS_NewInt32(ctx, value);
		case ALPHA_BLEND_EQUATION:
			{
				AthenaAlphaBlend blend;
				JSValue obj = JS_NewObject(ctx);
				if (!athena_screen_get_alpha_blend(param, &blend))
					return JS_UNDEFINED;
				JS_DefinePropertyValueStr(ctx, obj, "a", JS_NewInt32(ctx, blend.a), JS_PROP_C_W_E);
				JS_DefinePropertyValueStr(ctx, obj, "b", JS_NewInt32(ctx, blend.b), JS_PROP_C_W_E);
				JS_DefinePropertyValueStr(ctx, obj, "c", JS_NewInt32(ctx, blend.c), JS_PROP_C_W_E);
				JS_DefinePropertyValueStr(ctx, obj, "d", JS_NewInt32(ctx, blend.d), JS_PROP_C_W_E);
				JS_DefinePropertyValueStr(ctx, obj, "fix", JS_NewInt32(ctx, blend.fix), JS_PROP_C_W_E);
				return obj;
			}
		case SCISSOR_BOUNDS:
			{
				AthenaScissorBounds bounds;
				JSValue obj = JS_NewObject(ctx);
				if (!athena_screen_get_scissor_bounds(param, &bounds))
					return JS_UNDEFINED;
				JS_DefinePropertyValueStr(ctx, obj, "x1", JS_NewInt32(ctx, bounds.x1), JS_PROP_C_W_E);
				JS_DefinePropertyValueStr(ctx, obj, "y1", JS_NewInt32(ctx, bounds.y1), JS_PROP_C_W_E);
				JS_DefinePropertyValueStr(ctx, obj, "x2", JS_NewInt32(ctx, bounds.x2), JS_PROP_C_W_E);
				JS_DefinePropertyValueStr(ctx, obj, "y2", JS_NewInt32(ctx, bounds.y2), JS_PROP_C_W_E);
				return obj;
			}
	}
	return JS_UNDEFINED;
}

JSValue js_screen_buffers[3] = { JS_UNDEFINED, JS_UNDEFINED, JS_UNDEFINED };
JSValue js_current_screen_buffers[3] = { JS_UNDEFINED, JS_UNDEFINED, JS_UNDEFINED };

static JSValue js_screen_init_buffers(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	if (athena_screen_buffers_ready())
		return JS_ThrowInternalError(ctx, "Screen.initBuffers() shouldn't be called more than once!");

	if (athena_screen_init_buffers() < 0)
		return JS_EXCEPTION;

	for (int i = 0; i < 3; i++) {
		AthenaImage *image = athena_screen_get_buffer(i);
		js_screen_buffers[i] = JS_NewObjectClass(ctx, get_img_class_id());
		if (JS_IsException(js_screen_buffers[i]))
			return JS_EXCEPTION;
		JS_SetOpaque(js_screen_buffers[i], image);
		js_current_screen_buffers[i] = js_screen_buffers[i];
	}
	return JS_UNDEFINED;
}

static JSValue js_screen_get_buffer(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	if (!athena_screen_buffers_ready())
		return JS_ThrowInternalError(ctx, "Call Screen.initBuffers() before using this function!");
	int buffer_id;
	JS_ToInt32(ctx, &buffer_id, argv[0]);
	return JS_DupValue(ctx, js_current_screen_buffers[buffer_id]);
}

static JSValue js_screen_set_buffer(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	int buffer_id;
	uint32_t mask = 0;

	if (!athena_screen_buffers_ready())
		return JS_ThrowInternalError(ctx, "Call Screen.initBuffers() before using this function!");

	JS_ToInt32(ctx, &buffer_id, argv[0]);
	AthenaImage *image = JS_GetOpaque2(ctx, argv[1], get_img_class_id());
	if (argc > 2)
		JS_ToUint32(ctx, &mask, argv[2]);

	athena_screen_set_buffer(buffer_id, image, mask);

	if (!getGSGLOBAL()->PrimContext)
		js_current_screen_buffers[buffer_id] = argv[1];

	return JS_UNDEFINED;
}

static JSValue js_screen_reset_buffers(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	if (!athena_screen_buffers_ready())
		return JS_ThrowInternalError(ctx, "Call Screen.initBuffers() before using this function!");
	athena_screen_reset_buffers();
	for (int i = 0; i < 3; i++)
		js_current_screen_buffers[i] = js_screen_buffers[i];
	return JS_UNDEFINED;
}

static JSValue js_screen_switch_context(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	return JS_NewInt32(ctx, athena_screen_switch_context());
}

static JSValue js_screen_flush(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	athena_screen_flush();
	return JS_UNDEFINED;
}

static const JSCFunctionListEntry module_funcs[] = {
    JS_CFUNC_DEF("flip", 0, js_screen_flip),
    JS_CFUNC_DEF("clear", 1, js_screen_clear),
	JS_CFUNC_DEF("getMemoryStats", 1, js_screen_get_free_vram),
	JS_PROP_INT32_DEF("VRAM_SIZE", VRAM_SIZE, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("VRAM_USED_TOTAL", VRAM_USED_TOTAL, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("VRAM_USED_STATIC", VRAM_USED_STATIC, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("VRAM_USED_DYNAMIC", VRAM_USED_DYNAMIC, JS_PROP_CONFIGURABLE),
	JS_CFUNC_DEF("getFPS", 1, js_screen_get_fps),
    JS_CFUNC_DEF("waitVblankStart", 0, js_screen_vblank),
	JS_CFUNC_DEF("setVSync", 1, js_screen_vsync),
	JS_CFUNC_DEF("setFrameCounter", 1, js_screen_fcount),
	JS_CFUNC_DEF("getMode", 0, js_screen_get_mode),
    JS_CFUNC_DEF("setMode", 1, js_screen_set_mode),
	JS_CFUNC_DEF("clearColor", 1, js_screen_set_clear_color),
	JS_CFUNC_DEF("display", 1, js_screen_display),
	JS_CFUNC_DEF("getParam", 1, js_screen_get_param),
	JS_CFUNC_DEF("setParam", 2, js_screen_set_param),
	JS_CFUNC_DEF("alphaEquation", 1, js_screen_alpha_equation),
	JS_PROP_INT32_DEF("ALPHA_TEST_ENABLE", ALPHA_TEST_ENABLE, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("ALPHA_TEST_METHOD", ALPHA_TEST_METHOD, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("ALPHA_TEST_REF", ALPHA_TEST_REF, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("ALPHA_TEST_FAIL", ALPHA_TEST_FAIL, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("ALPHA_NEVER", ALPHA_NEVER, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("ALPHA_ALWAYS", ALPHA_ALWAYS, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("ALPHA_LESS", ALPHA_LESS, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("ALPHA_LEQUAL", ALPHA_LEQUAL, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("ALPHA_EQUAL", ALPHA_EQUAL, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("ALPHA_GEQUAL", ALPHA_GEQUAL, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("ALPHA_GREATER", ALPHA_GREATER, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("ALPHA_NEQUAL", ALPHA_NEQUAL, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("ALPHA_FAIL_NO_UPDATE", ALPHA_FAIL_NO_UPDATE, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("ALPHA_FAIL_FB_ONLY", ALPHA_FAIL_FB_ONLY, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("ALPHA_FAIL_ZB_ONLY", ALPHA_FAIL_ZB_ONLY, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("ALPHA_FAIL_RGB_ONLY", ALPHA_FAIL_RGB_ONLY, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("DST_ALPHA_TEST_ENABLE", DST_ALPHA_TEST_ENABLE, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("DST_ALPHA_TEST_METHOD", DST_ALPHA_TEST_METHOD, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("DST_ALPHA_ZERO", DEST_ALPHA_ZERO, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("DST_ALPHA_ONE", DEST_ALPHA_ONE, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("DEPTH_TEST_ENABLE", DEPTH_TEST_ENABLE, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("DEPTH_TEST_METHOD", DEPTH_TEST_METHOD, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("DEPTH_NEVER", DEPTH_NEVER, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("DEPTH_ALWAYS", DEPTH_ALWAYS, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("DEPTH_GEQUAL", DEPTH_GEQUAL, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("DEPTH_GREATER", DEPTH_GREATER, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("ALPHA_BLEND_EQUATION", ALPHA_BLEND_EQUATION, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("SRC_RGB", SRC_RGB, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("DST_RGB", DST_RGB, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("ZERO_RGB", ZERO_RGB, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("SRC_ALPHA", SRC_ALPHA, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("DST_ALPHA", DST_ALPHA, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("ALPHA_FIX", ALPHA_FIX, JS_PROP_CONFIGURABLE),
	JS_PROP_INT64_DEF("BLEND_DEFAULT", GS_ALPHA_BLEND_NORMAL, JS_PROP_CONFIGURABLE),
	JS_PROP_INT64_DEF("BLEND_ADD_NOALPHA", GS_ALPHA_BLEND_ADD_NOALPHA, JS_PROP_CONFIGURABLE),
	JS_PROP_INT64_DEF("BLEND_ADD", GS_ALPHA_BLEND_ADD, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("SCISSOR_BOUNDS", SCISSOR_BOUNDS, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("PIXEL_ALPHA_BLEND_ENABLE", PIXEL_ALPHA_BLEND_ENABLE, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("COLOR_CLAMP_MODE", COLOR_CLAMP_MODE, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("NTSC", GS_MODE_NTSC, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("DTV_480p", GS_MODE_DTV_480P, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("PAL", GS_MODE_PAL, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("DTV_576p", GS_MODE_DTV_576P, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("DTV_720p", GS_MODE_DTV_720P, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("DTV_1080i", GS_MODE_DTV_1080I, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("INTERLACED", GS_INTERLACED, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("PROGRESSIVE", GS_NONINTERLACED, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("FIELD", GS_FIELD, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("FRAME", GS_FRAME, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("CT32", GS_PSM_CT32, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("CT24", GS_PSM_CT24, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("CT16", GS_PSM_CT16, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("CT16S", GS_PSM_CT16S, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("Z32", GS_ZBUF_32, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("Z24", GS_ZBUF_24, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("Z16", GS_ZBUF_16, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("Z16S", GS_ZBUF_16S, JS_PROP_CONFIGURABLE),
	JS_CFUNC_DEF("initBuffers", 0, js_screen_init_buffers),
	JS_CFUNC_DEF("resetBuffers", 0, js_screen_reset_buffers),
	JS_CFUNC_DEF("getBuffer", 1, js_screen_get_buffer),
	JS_CFUNC_DEF("setBuffer", 3, js_screen_set_buffer),
	JS_PROP_INT32_DEF("DRAW_BUFFER", DRAW_BUFFER, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("DISPLAY_BUFFER", DISPLAY_BUFFER, JS_PROP_CONFIGURABLE),
	JS_PROP_INT32_DEF("DEPTH_BUFFER", DEPTH_BUFFER, JS_PROP_CONFIGURABLE),
	JS_CFUNC_DEF("switchContext", 0, js_screen_switch_context),
	JS_CFUNC_DEF("flush", 0, js_screen_flush),
};

static int js_screen_module_init(JSContext *ctx, JSModuleDef *m) {
    return JS_SetModuleExportList(ctx, m, module_funcs, countof(module_funcs));
}

JSModuleDef *athena_screen_init(JSContext* ctx){
	return athena_push_module(ctx, js_screen_module_init, module_funcs, countof(module_funcs), "Screen");
}
