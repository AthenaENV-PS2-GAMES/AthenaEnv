#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <ath_env.h>
#include <fntsys.h>

#include "../native/font.h"
#include "ath_font.h"

static JSClassID font_class_id;
static JSClassID render_class_id;
static int font_system_initialized;
static void font_finalizer(JSRuntime *rt, JSValue value);
static void render_finalizer(JSRuntime *rt, JSValue value);
static JSClassDef font_class = {
    "Font",
    .finalizer = font_finalizer,
};
static JSClassDef render_class = {
    "FontRender",
    .finalizer = render_finalizer,
};

typedef struct {
    AthenaFontRender *render;
    JSValue font_ref;
} FontRenderData;

static int font_argc(JSContext *ctx, int argc, int minimum, int maximum,
    const char *name)
{
    if (argc < minimum || argc > maximum) {
        if (minimum == maximum)
            JS_ThrowTypeError(ctx, "%s expects exactly %d arguments", name,
                minimum);
        else
            JS_ThrowTypeError(ctx, "%s expects between %d and %d arguments",
                name, minimum, maximum);
        return 0;
    }
    return 1;
}

static int font_number(JSContext *ctx, JSValueConst value, float *result,
    const char *name, int nonnegative)
{
    if (JS_ToFloat32(ctx, result, value))
        return 0;
    if (!isfinite(*result) || (nonnegative && *result < 0.0f))
        return JS_ThrowRangeError(ctx, "%s must be finite%s", name,
            nonnegative ? " and non-negative" : "") == JS_EXCEPTION ? 0 : 1;
    return 1;
}

static int font_color(JSContext *ctx, JSValueConst value, Color *result)
{
    uint32_t color;
    if (JS_ToUint32(ctx, &color, value))
        return 0;
    *result = color;
    return 1;
}

static AthenaFont *font_this(JSContext *ctx, JSValueConst value)
{
    return JS_GetOpaque2(ctx, value, font_class_id);
}

static void font_finalizer(JSRuntime *rt, JSValue value)
{
    AthenaFont *font = JS_GetOpaque(value, font_class_id);
    if (font) {
        athena_font_destroy(font);
        JS_SetOpaque(value, NULL);
    }
}

static void render_finalizer(JSRuntime *rt, JSValue value)
{
    FontRenderData *data = JS_GetOpaque(value, render_class_id);
    if (data) {
        athena_font_render_destroy(data->render);
        JS_FreeValueRT(rt, data->font_ref);
        free(data);
        JS_SetOpaque(value, NULL);
    }
}

static JSValue font_ctor(JSContext *ctx, JSValueConst new_target, int argc,
    JSValueConst *argv)
{
    const char *path;
    AthenaFont *font;
    JSValue proto, object;

    if (!font_argc(ctx, argc, 0, 1, "Font"))
        return JS_EXCEPTION;
    if (argc == 1 && !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "Font path must be a string");
    path = argc == 1 ? JS_ToCString(ctx, argv[0]) : NULL;
    if (argc == 1 && !path)
        return JS_EXCEPTION;
    font = athena_font_load(path);
    if (path)
        JS_FreeCString(ctx, path);
    if (!font)
        return JS_ThrowInternalError(ctx, "Unable to load font");

    proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    if (JS_IsException(proto)) {
        athena_font_destroy(font);
        return proto;
    }
    object = JS_NewObjectProtoClass(ctx, proto, font_class_id);
    JS_FreeValue(ctx, proto);
    if (JS_IsException(object)) {
        athena_font_destroy(font);
        return object;
    }
    JS_SetOpaque(object, font);
    return object;
}

static JSValue font_print(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    AthenaFont *font = font_this(ctx, this_val);
    float x, y;
    const char *text;
    if (!font || !font_argc(ctx, argc, 3, 3, "Font.print") ||
        !font_number(ctx, argv[0], &x, "Font.print x", 0) ||
        !font_number(ctx, argv[1], &y, "Font.print y", 0))
        return JS_EXCEPTION;
    text = JS_ToCString(ctx, argv[2]);
    if (!text)
        return JS_EXCEPTION;
    athena_font_print(font, x, y, text);
    JS_FreeCString(ctx, text);
    return JS_UNDEFINED;
}

static JSValue font_size(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    AthenaFont *font = font_this(ctx, this_val);
    const char *text;
    Coords size;
    JSValue object;
    if (!font || !font_argc(ctx, argc, 1, 1, "Font.getTextSize"))
        return JS_EXCEPTION;
    text = JS_ToCString(ctx, argv[0]);
    if (!text)
        return JS_EXCEPTION;
    size = athena_font_get_text_size(font, text);
    JS_FreeCString(ctx, text);
    object = JS_NewObject(ctx);
    if (JS_IsException(object))
        return object;
    JS_DefinePropertyValueStr(ctx, object, "width", JS_NewInt32(ctx, size.width),
        JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, object, "height", JS_NewInt32(ctx, size.height),
        JS_PROP_C_W_E);
    return object;
}

static JSValue font_render(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    AthenaFont *font = font_this(ctx, this_val);
    const char *text;
    AthenaFontRender *render;
    FontRenderData *data;
    JSValue object;
    if (!font || !font_argc(ctx, argc, 1, 1, "Font.render"))
        return JS_EXCEPTION;
    text = JS_ToCString(ctx, argv[0]);
    if (!text)
        return JS_EXCEPTION;
    render = athena_font_render_create(font, text);
    JS_FreeCString(ctx, text);
    if (!render)
        return JS_ThrowOutOfMemory(ctx);
    data = calloc(1, sizeof(*data));
    if (!data) {
        athena_font_render_destroy(render);
        return JS_ThrowOutOfMemory(ctx);
    }
    object = JS_NewObjectClass(ctx, render_class_id);
    if (JS_IsException(object)) {
        athena_font_render_destroy(render);
        free(data);
        return object;
    }
    data->render = render;
    data->font_ref = JS_DupValue(ctx, this_val);
    JS_SetOpaque(object, data);
    return object;
}

static JSValue render_print(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    FontRenderData *data = JS_GetOpaque2(ctx, this_val, render_class_id);
    float x, y;
    if (!data || !font_argc(ctx, argc, 2, 2, "FontRender.print") ||
        !font_number(ctx, argv[0], &x, "FontRender.print x", 0) ||
        !font_number(ctx, argv[1], &y, "FontRender.print y", 0))
        return JS_EXCEPTION;
    athena_font_render_print(data->render, x, y);
    return JS_UNDEFINED;
}

static JSValue font_get_number(JSContext *ctx, JSValueConst this_val,
    int magic)
{
    AthenaFont *font = font_this(ctx, this_val);
    if (!font)
        return JS_EXCEPTION;
    if (magic == 0) return JS_NewFloat32(ctx, font->scale);
    if (magic == 1) return JS_NewFloat32(ctx, font->outline);
    return JS_NewFloat32(ctx, font->dropshadow);
}

static JSValue font_set_number(JSContext *ctx, JSValueConst this_val,
    JSValue value, int magic)
{
    AthenaFont *font = font_this(ctx, this_val);
    float number;
    if (!font || !font_number(ctx, value, &number, "Font property", 1))
        return JS_EXCEPTION;
    if (magic == 0) font->scale = number;
    else if (magic == 1) font->outline = number;
    else font->dropshadow = number;
    return JS_UNDEFINED;
}

static JSValue font_get_color(JSContext *ctx, JSValueConst this_val, int magic)
{
    AthenaFont *font = font_this(ctx, this_val);
    if (!font)
        return JS_EXCEPTION;
    if (magic == 0) return JS_NewUint32(ctx, font->color);
    if (magic == 1) return JS_NewUint32(ctx, font->outline_color);
    if (magic == 2) return JS_NewUint32(ctx, font->dropshadow_color);
    return JS_NewInt32(ctx, font->align);
}

static JSValue font_set_color(JSContext *ctx, JSValueConst this_val,
    JSValue value, int magic)
{
    AthenaFont *font = font_this(ctx, this_val);
    if (!font)
        return JS_EXCEPTION;
    if (magic == 3) {
        int32_t align;
        if (JS_ToInt32(ctx, &align, value))
            return JS_EXCEPTION;
        if (align != ALIGN_NONE && align != ALIGN_TOP &&
            align != ALIGN_BOTTOM && align != ALIGN_VCENTER &&
            align != ALIGN_LEFT && align != ALIGN_RIGHT &&
            align != ALIGN_HCENTER && align != ALIGN_CENTER)
            return JS_ThrowRangeError(ctx, "Font.align is invalid");
        font->align = align;
    } else {
        Color color;
        if (!font_color(ctx, value, &color))
            return JS_EXCEPTION;
        if (magic == 0) font->color = color;
        else if (magic == 1) font->outline_color = color;
        else font->dropshadow_color = color;
    }
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry font_proto[] = {
    JS_CGETSET_MAGIC_DEF("scale", font_get_number, font_set_number, 0),
    JS_CGETSET_MAGIC_DEF("outline", font_get_number, font_set_number, 1),
    JS_CGETSET_MAGIC_DEF("dropshadow", font_get_number, font_set_number, 2),
    JS_CGETSET_MAGIC_DEF("color", font_get_color, font_set_color, 0),
    JS_CGETSET_MAGIC_DEF("outline_color", font_get_color, font_set_color, 1),
    JS_CGETSET_MAGIC_DEF("dropshadow_color", font_get_color, font_set_color, 2),
    JS_CGETSET_MAGIC_DEF("align", font_get_color, font_set_color, 3),
    JS_CFUNC_DEF("print", 3, font_print),
    JS_CFUNC_DEF("getTextSize", 1, font_size),
    JS_CFUNC_DEF("render", 1, font_render),
};

static const JSCFunctionListEntry render_proto[] = {
    JS_CFUNC_DEF("print", 2, render_print),
};

static const JSCFunctionListEntry font_exports[] = {
    JS_PROP_INT32_DEF("ALIGN_TOP", ALIGN_TOP, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ALIGN_BOTTOM", ALIGN_BOTTOM, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ALIGN_VCENTER", ALIGN_VCENTER, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ALIGN_LEFT", ALIGN_LEFT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ALIGN_RIGHT", ALIGN_RIGHT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ALIGN_HCENTER", ALIGN_HCENTER, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ALIGN_NONE", ALIGN_NONE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("ALIGN_CENTER", ALIGN_CENTER, JS_PROP_CONFIGURABLE),
};

static int font_module_init(JSContext *ctx, JSModuleDef *module)
{
    JSValue font_proto_value, font_constructor, render_proto_value;

    if (!font_system_initialized) {
        fntInit();
        font_system_initialized = 1;
    }
    JS_NewClassID(&font_class_id);
    /*
     * `font_class` must be the static JSClassDef. A local JSValue with the
     * same name used to shadow it, so the class was registered from stack
     * garbage: a bogus `exotic` pointer made every Font property lookup read
     * near address 0, and the finalizer and call hooks were garbage too.
     */
    JS_NewClass(JS_GetRuntime(ctx), font_class_id, &font_class);
    font_proto_value = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, font_proto_value, font_proto,
        countof(font_proto));
    JS_SetClassProto(ctx, font_class_id, font_proto_value);
    font_constructor = JS_NewCFunction2(ctx, font_ctor, "Font", 1,
        JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, font_constructor, font_proto_value);
    JS_SetPropertyFunctionList(ctx, font_constructor, font_exports,
        countof(font_exports));

    JS_NewClassID(&render_class_id);
    JS_NewClass(JS_GetRuntime(ctx), render_class_id, &render_class);
    render_proto_value = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, render_proto_value, render_proto,
        countof(render_proto));
    JS_SetClassProto(ctx, render_class_id, render_proto_value);

    JS_SetModuleExport(ctx, module, "Font", font_constructor);
    return 0;
}

JSModuleDef *athena_font_init(JSContext *ctx)
{
    JSModuleDef *module = athena_push_module(ctx, font_module_init, NULL, 0,
        "Font");
    if (!module)
        return NULL;
    JS_AddModuleExport(ctx, module, "Font");
    return module;
}
