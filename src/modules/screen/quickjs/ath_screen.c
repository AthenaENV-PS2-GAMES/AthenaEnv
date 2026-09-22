#include <stdint.h>

#include <ath_env.h>
#include <graphics.h>
#include <owl_packet.h>

#include "ath_screen.h"

static int screen_argc(JSContext *ctx, int argc, int minimum, int maximum,
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

static int screen_color(JSContext *ctx, JSValueConst value, Color *result) {
    uint32_t color;
    if (JS_ToUint32(ctx, &color, value))
        return 0;
    *result = color;
    return 1;
}

static JSValue screen_flip(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    if (!screen_argc(ctx, argc, 0, 0, "Screen.flip"))
        return JS_EXCEPTION;
    if (!flipScreen)
        return JS_ThrowInternalError(ctx, "Graphics service is not initialized");
    dbgprintf("[Screen] flip begin\n");
    flipScreen();
    dbgprintf("[Screen] flip complete\n");
    return JS_UNDEFINED;
}

static JSValue screen_clear(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    Color color = GS_SETREG_RGBAQ(0, 0, 0, 0x80, 0);
    if (!screen_argc(ctx, argc, 0, 1, "Screen.clear"))
        return JS_EXCEPTION;
    if (argc == 1 && !screen_color(ctx, argv[0], &color))
        return JS_EXCEPTION;
    clearScreen(color);
    return JS_UNDEFINED;
}

static JSValue screen_wait_vblank(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    if (!screen_argc(ctx, argc, 0, 0, "Screen.waitVblankStart"))
        return JS_EXCEPTION;
    graphicWaitVblankStart();
    return JS_UNDEFINED;
}

static JSValue screen_set_vsync(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    if (!screen_argc(ctx, argc, 1, 1, "Screen.setVSync"))
        return JS_EXCEPTION;
    setVSync(JS_ToBool(ctx, argv[0]));
    return JS_UNDEFINED;
}

static JSValue screen_set_frame_counter(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv) {
    if (!screen_argc(ctx, argc, 1, 1, "Screen.setFrameCounter"))
        return JS_EXCEPTION;
    toggleFrameCounter(JS_ToBool(ctx, argv[0]));
    return JS_UNDEFINED;
}

static JSValue screen_memory_stats(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    uint32_t mode = VRAM_USED_TOTAL;
    if (!screen_argc(ctx, argc, 0, 1, "Screen.getMemoryStats"))
        return JS_EXCEPTION;
    if (argc == 1 && JS_ToUint32(ctx, &mode, argv[0]))
        return JS_EXCEPTION;
    return JS_NewUint32(ctx, (uint32_t)getFreeVRAM(mode));
}

static JSValue screen_fps(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    int32_t interval;
    if (!screen_argc(ctx, argc, 1, 1, "Screen.getFPS"))
        return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &interval, argv[0]))
        return JS_EXCEPTION;
    if (interval <= 0)
        return JS_ThrowRangeError(ctx, "Screen.getFPS interval must be positive");
    return JS_NewFloat64(ctx, FPSCounter(interval));
}

static JSValue screen_get_mode(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    GSCONTEXT *global;
    JSValue result;
    if (!screen_argc(ctx, argc, 0, 0, "Screen.getMode"))
        return JS_EXCEPTION;
    global = getGSGLOBAL();
    if (!global)
        return JS_ThrowInternalError(ctx, "Graphics service is not initialized");
    result = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, result, "mode", JS_NewInt32(ctx, global->Mode));
    JS_SetPropertyStr(ctx, result, "width", JS_NewInt32(ctx, global->Width));
    JS_SetPropertyStr(ctx, result, "height", JS_NewInt32(ctx, global->Height));
    JS_SetPropertyStr(ctx, result, "psm", JS_NewInt32(ctx, global->PSM));
    JS_SetPropertyStr(ctx, result, "interlace", JS_NewInt32(ctx, global->Interlace));
    JS_SetPropertyStr(ctx, result, "field", JS_NewInt32(ctx, global->Field));
    JS_SetPropertyStr(ctx, result, "psmz", JS_NewInt32(ctx, global->PSMZ));
    JS_SetPropertyStr(ctx, result, "zbuffering", JS_NewBool(ctx, global->ZBuffering));
    JS_SetPropertyStr(ctx, result, "double_buffering",
        JS_NewBool(ctx, global->DoubleBuffering));
    return result;
}

static int screen_property_int(JSContext *ctx, JSValueConst object,
    const char *name, int32_t *result) {
    JSValue value = JS_GetPropertyStr(ctx, object, name);
    int failed = JS_IsException(value) || JS_ToInt32(ctx, result, value);
    JS_FreeValue(ctx, value);
    return !failed;
}

static int screen_property_bool(JSContext *ctx, JSValueConst object,
    const char *name, bool *result) {
    JSValue value = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsException(value)) {
        JS_FreeValue(ctx, value);
        return 0;
    }
    *result = JS_ToBool(ctx, value);
    JS_FreeValue(ctx, value);
    return 1;
}

static JSValue screen_set_mode(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    int32_t mode, width, height, psm, interlace, field;
    int32_t psmz = GS_PSMZ_16S;
    bool zbuffering = false;
    bool double_buffering = true;
    int32_t pass_count = 0;
    if (!screen_argc(ctx, argc, 1, 1, "Screen.setMode"))
        return JS_EXCEPTION;
    if (!JS_IsObject(argv[0]) ||
        !screen_property_int(ctx, argv[0], "mode", &mode) ||
        !screen_property_int(ctx, argv[0], "width", &width) ||
        !screen_property_int(ctx, argv[0], "height", &height) ||
        !screen_property_int(ctx, argv[0], "psm", &psm) ||
        !screen_property_int(ctx, argv[0], "interlace", &interlace) ||
        !screen_property_int(ctx, argv[0], "field", &field)) {
        return JS_ThrowTypeError(ctx,
            "Screen.setMode requires mode, width, height, psm, interlace, and field");
    }
    {
        JSValue value = JS_GetPropertyStr(ctx, argv[0], "psmz");
        if (!JS_IsUndefined(value) && JS_ToInt32(ctx, &psmz, value)) {
            JS_FreeValue(ctx, value);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, value);
    }
    if (!screen_property_bool(ctx, argv[0], "zbuffering", &zbuffering) ||
        !screen_property_bool(ctx, argv[0], "double_buffering", &double_buffering))
        return JS_ThrowTypeError(ctx,
            "Screen.setMode requires zbuffering and double_buffering");
    {
        JSValue value = JS_GetPropertyStr(ctx, argv[0], "pass_count");
        if (!JS_IsUndefined(value) && JS_ToInt32(ctx, &pass_count, value)) {
            JS_FreeValue(ctx, value);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, value);
    }
    if (width <= 0 || height <= 0 || pass_count < 0)
        return JS_ThrowRangeError(ctx, "Screen.setMode dimensions are invalid");
    setVideoMode((s16)mode, width, height, psm, (s16)interlace, (s16)field,
        zbuffering, psmz, double_buffering, (uint8_t)pass_count);
    return JS_UNDEFINED;
}

static JSValue screen_alpha_equation(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    int32_t a, b, c, d, fix;
    if (!screen_argc(ctx, argc, 5, 5, "Screen.alphaEquation"))
        return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &a, argv[0]) || JS_ToInt32(ctx, &b, argv[1]) ||
        JS_ToInt32(ctx, &c, argv[2]) || JS_ToInt32(ctx, &d, argv[3]) ||
        JS_ToInt32(ctx, &fix, argv[4]))
        return JS_EXCEPTION;
    return JS_NewInt64(ctx, ALPHA_EQUATION(a, b, c, d, fix));
}

static int screen_param_value(JSContext *ctx, int param, JSValueConst value,
    uint64_t *result) {
    switch (param) {
    case ALPHA_TEST_ENABLE:
    case DST_ALPHA_TEST_ENABLE:
    case DEPTH_TEST_ENABLE:
    case PIXEL_ALPHA_BLEND_ENABLE:
        *result = JS_ToBool(ctx, value);
        return 1;
    case ALPHA_TEST_METHOD:
    case ALPHA_TEST_REF:
    case ALPHA_TEST_FAIL:
    case DST_ALPHA_TEST_METHOD:
    case DEPTH_TEST_METHOD:
    case COLOR_CLAMP_MODE:
    {
        int64_t integer;
        if (JS_ToInt64(ctx, &integer, value))
            return 0;
        *result = (uint64_t)integer;
        return 1;
    }
    case ALPHA_BLEND_EQUATION: {
        int32_t a, b, c, d, fix;
        if (!JS_IsObject(value) ||
            !screen_property_int(ctx, value, "a", &a) ||
            !screen_property_int(ctx, value, "b", &b) ||
            !screen_property_int(ctx, value, "c", &c) ||
            !screen_property_int(ctx, value, "d", &d) ||
            !screen_property_int(ctx, value, "fix", &fix))
            return 0;
        *result = ALPHA_EQUATION(a, b, c, d, fix);
        return 1;
    }
    case SCISSOR_BOUNDS: {
        int32_t x0, y0, x1, y1;
        if (!JS_IsObject(value) ||
            !screen_property_int(ctx, value, "x0", &x0) ||
            !screen_property_int(ctx, value, "y0", &y0) ||
            !screen_property_int(ctx, value, "x1", &x1) ||
            !screen_property_int(ctx, value, "y1", &y1))
            return 0;
        *result = GS_SETREG_SCISSOR_1(x0, x1, y0, y1);
        return 1;
    }
    default:
        return 0;
    }
}

static JSValue screen_set_param(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    int32_t param;
    uint64_t value;
    if (!screen_argc(ctx, argc, 2, 2, "Screen.setParam"))
        return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &param, argv[0]) ||
        !screen_param_value(ctx, param, argv[1], &value))
        return JS_ThrowTypeError(ctx, "Screen.setParam received an invalid value");
    set_screen_param((uint8_t)param, value);
    return JS_UNDEFINED;
}

static JSValue screen_get_param(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    int32_t param;
    uint64_t value;
    if (!screen_argc(ctx, argc, 1, 1, "Screen.getParam"))
        return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &param, argv[0]))
        return JS_EXCEPTION;
    value = get_screen_param((uint8_t)param);
    switch (param) {
    case ALPHA_BLEND_EQUATION: {
        alpha_reg alpha = { .data = value };
        JSValue result = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, result, "a", JS_NewInt32(ctx, alpha.fields.a));
        JS_SetPropertyStr(ctx, result, "b", JS_NewInt32(ctx, alpha.fields.b));
        JS_SetPropertyStr(ctx, result, "c", JS_NewInt32(ctx, alpha.fields.c));
        JS_SetPropertyStr(ctx, result, "d", JS_NewInt32(ctx, alpha.fields.d));
        JS_SetPropertyStr(ctx, result, "fix", JS_NewInt32(ctx, alpha.fields.fix));
        return result;
    }
    case SCISSOR_BOUNDS: {
        scissor_reg scissor = { .data = value };
        JSValue result = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, result, "x0", JS_NewInt32(ctx, scissor.fields.x0));
        JS_SetPropertyStr(ctx, result, "y0", JS_NewInt32(ctx, scissor.fields.y0));
        JS_SetPropertyStr(ctx, result, "x1", JS_NewInt32(ctx, scissor.fields.x1));
        JS_SetPropertyStr(ctx, result, "y1", JS_NewInt32(ctx, scissor.fields.y1));
        return result;
    }
    case ALPHA_TEST_ENABLE:
    case DST_ALPHA_TEST_ENABLE:
    case DEPTH_TEST_ENABLE:
    case PIXEL_ALPHA_BLEND_ENABLE:
        return JS_NewBool(ctx, value != 0);
    default:
        return JS_NewInt64(ctx, (int64_t)value);
    }
}

static JSValue js_screen_switch_context(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    if (!screen_argc(ctx, argc, 0, 0, "Screen.switchContext"))
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, screen_switch_context());
}

static JSValue screen_flush(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    if (!screen_argc(ctx, argc, 0, 0, "Screen.flush"))
        return JS_EXCEPTION;
    owl_flush_packet();
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry screen_funcs[] = {
    JS_CFUNC_DEF("flip", 0, screen_flip),
    JS_CFUNC_DEF("clear", 1, screen_clear),
    JS_CFUNC_DEF("waitVblankStart", 0, screen_wait_vblank),
    JS_CFUNC_DEF("setVSync", 1, screen_set_vsync),
    JS_CFUNC_DEF("setFrameCounter", 1, screen_set_frame_counter),
    JS_CFUNC_DEF("getMemoryStats", 1, screen_memory_stats),
    JS_CFUNC_DEF("getFPS", 1, screen_fps),
    JS_CFUNC_DEF("getMode", 0, screen_get_mode),
    JS_CFUNC_DEF("setMode", 1, screen_set_mode),
    JS_CFUNC_DEF("alphaEquation", 5, screen_alpha_equation),
    JS_CFUNC_DEF("getParam", 1, screen_get_param),
    JS_CFUNC_DEF("setParam", 2, screen_set_param),
    JS_CFUNC_DEF("switchContext", 0, js_screen_switch_context),
    JS_CFUNC_DEF("flush", 0, screen_flush),
    JS_PROP_INT32_DEF("VRAM_SIZE", VRAM_SIZE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("VRAM_USED_TOTAL", VRAM_USED_TOTAL, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("VRAM_USED_STATIC", VRAM_USED_STATIC, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("VRAM_USED_DYNAMIC", VRAM_USED_DYNAMIC, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ALPHA_TEST_ENABLE", ALPHA_TEST_ENABLE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ALPHA_TEST_METHOD", ALPHA_TEST_METHOD, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ALPHA_TEST_REF", ALPHA_TEST_REF, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ALPHA_TEST_FAIL", ALPHA_TEST_FAIL, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DST_ALPHA_TEST_ENABLE", DST_ALPHA_TEST_ENABLE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DST_ALPHA_TEST_METHOD", DST_ALPHA_TEST_METHOD, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DEPTH_TEST_ENABLE", DEPTH_TEST_ENABLE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DEPTH_TEST_METHOD", DEPTH_TEST_METHOD, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ALPHA_BLEND_EQUATION", ALPHA_BLEND_EQUATION, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SCISSOR_BOUNDS", SCISSOR_BOUNDS, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PIXEL_ALPHA_BLEND_ENABLE", PIXEL_ALPHA_BLEND_ENABLE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("COLOR_CLAMP_MODE", COLOR_CLAMP_MODE, JS_PROP_CONFIGURABLE),
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
    JS_PROP_INT32_DEF("DST_ALPHA_ZERO", DEST_ALPHA_ZERO, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DST_ALPHA_ONE", DEST_ALPHA_ONE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DEPTH_NEVER", DEPTH_NEVER, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DEPTH_ALWAYS", DEPTH_ALWAYS, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DEPTH_GEQUAL", DEPTH_GEQUAL, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DEPTH_GREATER", DEPTH_GREATER, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SRC_RGB", SRC_RGB, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DST_RGB", DST_RGB, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ZERO_RGB", ZERO_RGB, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SRC_ALPHA", SRC_ALPHA, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DST_ALPHA", DST_ALPHA, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ALPHA_FIX", ALPHA_FIX, JS_PROP_CONFIGURABLE),
    JS_PROP_INT64_DEF("BLEND_DEFAULT", GS_ALPHA_BLEND_NORMAL, JS_PROP_CONFIGURABLE),
    JS_PROP_INT64_DEF("BLEND_ADD_NOALPHA", GS_ALPHA_BLEND_ADD_NOALPHA, JS_PROP_CONFIGURABLE),
    JS_PROP_INT64_DEF("BLEND_ADD", GS_ALPHA_BLEND_ADD, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("NTSC", GS_MODE_NTSC, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PAL", GS_MODE_PAL, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DTV_480p", GS_MODE_DTV_480P, JS_PROP_CONFIGURABLE),
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
    JS_PROP_INT32_DEF("DRAW_BUFFER", DRAW_BUFFER, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DISPLAY_BUFFER", DISPLAY_BUFFER, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DEPTH_BUFFER", DEPTH_BUFFER, JS_PROP_CONFIGURABLE)
};

static int screen_module_init(JSContext *ctx, JSModuleDef *module) {
    graphics_service_init();
    return JS_SetModuleExportList(ctx, module, screen_funcs,
        countof(screen_funcs));
}

JSModuleDef *athena_screen_init(JSContext *ctx) {
    return athena_push_module(ctx, screen_module_init, screen_funcs,
        countof(screen_funcs), "Screen");
}
