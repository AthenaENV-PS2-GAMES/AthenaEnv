
#include <stdlib.h>
#include <malloc.h>
#include <string.h>

#include <athena/font.h>
#include <ath_env.h>
#include <ath_bindings.h>

static JSClassID js_font_class_id;
static JSClassID js_fontrender_class_id;

static void js_font_dtor(JSRuntime *rt, JSValue val) {
    AthenaFont *font = JS_GetOpaque(val, js_font_class_id);
    if (font) {
        athena_font_destroy(font);
        JS_SetOpaque(val, NULL);
    }
}

static JSValue js_font_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    AthenaFont *font = NULL;
    JSValue obj = JS_UNDEFINED;
    JSValue proto;

    if (argc >= 1) {
        const char *path = JS_ToCString(ctx, argv[0]);
        font = athena_font_load(path);
        JS_FreeCString(ctx, path);
    } else {
        font = athena_font_load(NULL);
    }

    if (!font)
        return JS_EXCEPTION;

    proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    if (JS_IsException(proto))
        goto fail;
    obj = JS_NewObjectProtoClass(ctx, proto, js_font_class_id);
    JS_FreeValue(ctx, proto);
    if (JS_IsException(obj))
        goto fail;
    JS_SetOpaque(obj, font);
    return obj;

 fail:
    athena_font_destroy(font);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue js_font_print(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    float x, y;
    AthenaFont *font = JS_GetOpaque2(ctx, this_val, js_font_class_id);
    JS_ToFloat32(ctx, &x, argv[0]);
    JS_ToFloat32(ctx, &y, argv[1]);
    const char *text = JS_ToCString(ctx, argv[2]);
    athena_font_print(font, x, y, text);
    JS_FreeCString(ctx, text);
    return JS_UNDEFINED;
}

static JSValue js_font_gettextsize(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    AthenaFont *font = JS_GetOpaque2(ctx, this_val, js_font_class_id);
    const char *str = JS_ToCString(ctx, argv[0]);
    Coords size = athena_font_get_text_size(font, str);
    JS_FreeCString(ctx, str);

    JSValue obj = JS_NewObject(ctx);
    JS_DefinePropertyValueStr(ctx, obj, "width", JS_NewUint32(ctx, size.width), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "height", JS_NewUint32(ctx, size.height), JS_PROP_C_W_E);
    return obj;
}

static JSValue js_font_get_scale(JSContext *ctx, JSValueConst this_val, int magic) {
    AthenaFont *font = JS_GetOpaque2(ctx, this_val, js_font_class_id);
    if (!font) return JS_EXCEPTION;
    switch (magic) {
        case 0: return JS_NewFloat32(ctx, font->scale);
        case 1: return JS_NewFloat32(ctx, font->outline);
        case 2: return JS_NewFloat32(ctx, font->dropshadow);
    }
    return JS_UNDEFINED;
}

static JSValue js_font_set_scale(JSContext *ctx, JSValueConst this_val, JSValue val, int magic) {
    AthenaFont *font = JS_GetOpaque2(ctx, this_val, js_font_class_id);
    float v;
    if (!font || JS_ToFloat32(ctx, &v, val)) return JS_EXCEPTION;
    switch (magic) {
        case 0: font->scale = v; break;
        case 1: font->outline = v; break;
        case 2: font->dropshadow = v; break;
    }
    return JS_UNDEFINED;
}

static JSValue js_font_get_color(JSContext *ctx, JSValueConst this_val, int magic) {
    AthenaFont *font = JS_GetOpaque2(ctx, this_val, js_font_class_id);
    if (!font) return JS_EXCEPTION;
    switch (magic) {
        case 0: return JS_NewUint32(ctx, font->color);
        case 1: return JS_NewUint32(ctx, font->outline_color);
        case 2: return JS_NewUint32(ctx, font->dropshadow_color);
        case 3: return JS_NewUint32(ctx, (uint32_t)font->align);
    }
    return JS_UNDEFINED;
}

static JSValue js_font_set_color(JSContext *ctx, JSValueConst this_val, JSValue val, int magic) {
    AthenaFont *font = JS_GetOpaque2(ctx, this_val, js_font_class_id);
    uint32_t v;
    if (!font || JS_ToUint32(ctx, &v, val)) return JS_EXCEPTION;
    switch (magic) {
        case 0: font->color = v; break;
        case 1: font->outline_color = v; break;
        case 2: font->dropshadow_color = v; break;
        case 3: font->align = (int)v; break;
    }
    return JS_UNDEFINED;
}

static JSValue js_font_render(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaFont *font = JS_GetOpaque2(ctx, this_val, js_font_class_id);
    const char *text = JS_ToCString(ctx, argv[0]);
    AthenaFontRender *render = athena_font_render_create(font, text);
    JS_FreeCString(ctx, text);
    if (!render) return JS_EXCEPTION;

    JSValue obj = JS_NewObjectClass(ctx, js_fontrender_class_id);
    if (JS_IsException(obj)) {
        athena_font_render_destroy(render);
        return obj;
    }
    JS_SetOpaque(obj, render);
    return obj;
}

static JSValue js_fontrender_print(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    float x, y;
    AthenaFontRender *render = JS_GetOpaque2(ctx, this_val, js_fontrender_class_id);
    JS_ToFloat32(ctx, &x, argv[0]);
    JS_ToFloat32(ctx, &y, argv[1]);
    athena_font_render_print(render, x, y);
    return JS_UNDEFINED;
}

static JSClassDef js_fontrender_class = { "FontRender" };

static const JSCFunctionListEntry js_fontrender_proto_funcs[] = {
    JS_CFUNC_DEF("print", 2, js_fontrender_print),
};

static JSClassDef js_font_class = {
    "Font",
    .finalizer = js_font_dtor,
};

static const JSCFunctionListEntry js_font_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("scale", js_font_get_scale, js_font_set_scale, 0),
    JS_CGETSET_MAGIC_DEF("outline", js_font_get_scale, js_font_set_scale, 1),
    JS_CGETSET_MAGIC_DEF("dropshadow", js_font_get_scale, js_font_set_scale, 2),
    JS_CGETSET_MAGIC_DEF("color", js_font_get_color, js_font_set_color, 0),
    JS_CGETSET_MAGIC_DEF("outline_color", js_font_get_color, js_font_set_color, 1),
    JS_CGETSET_MAGIC_DEF("dropshadow_color", js_font_get_color, js_font_set_color, 2),
    JS_CGETSET_MAGIC_DEF("align", js_font_get_color, js_font_set_color, 3),
    JS_CFUNC_DEF("print", 3, js_font_print),
    JS_CFUNC_DEF("render", 1, js_font_render),
    JS_CFUNC_DEF("getTextSize", 1, js_font_gettextsize),
};

static const JSCFunctionListEntry js_font_funcs[] = {
    JS_PROP_INT32_DEF("ALIGN_TOP", ALIGN_TOP, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ALIGN_BOTTOM", ALIGN_BOTTOM, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ALIGN_VCENTER", ALIGN_VCENTER, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ALIGN_LEFT", ALIGN_LEFT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ALIGN_RIGHT", ALIGN_RIGHT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ALIGN_HCENTER", ALIGN_HCENTER, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ALIGN_NONE", ALIGN_NONE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ALIGN_CENTER", ALIGN_CENTER, JS_PROP_CONFIGURABLE),
};

static void js_renderfont_init(JSContext *ctx) {
    JSValue proto;
    JS_NewClassID(&js_fontrender_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_fontrender_class_id, &js_fontrender_class);
    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, js_fontrender_proto_funcs, countof(js_fontrender_proto_funcs));
    JS_SetClassProto(ctx, js_fontrender_class_id, proto);
}

static int font_init(JSContext *ctx, JSModuleDef *m) {
    JSValue font_proto, font_class;

    JS_NewClassID(&js_font_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_font_class_id, &js_font_class);

    font_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, font_proto, js_font_proto_funcs, countof(js_font_proto_funcs));

    font_class = JS_NewCFunction2(ctx, js_font_ctor, "Font", 1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, font_class, font_proto);
    JS_SetClassProto(ctx, js_font_class_id, font_proto);
    JS_SetPropertyFunctionList(ctx, font_class, js_font_funcs, countof(js_font_funcs));

    JS_SetModuleExport(ctx, m, "Font", font_class);
    js_renderfont_init(ctx);
    return 0;
}

JSModuleDef *athena_font_init(JSContext *ctx) {
    JSModuleDef *m = JS_NewCModule(ctx, "Font", font_init);
    if (!m) return NULL;
    JS_AddModuleExport(ctx, m, "Font");
    return m;
}
