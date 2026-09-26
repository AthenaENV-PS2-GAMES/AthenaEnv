#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <ath_env.h>
#include "../native/fntsys.h"

#include <athena/font.h>
#include <athena/job.h>
#include <athena/js/job.h>
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

/* Opaque of a Font after free(): using it throws instead of reading freed memory. */
static AthenaFont font_freed;

static AthenaFont *font_this(JSContext *ctx, JSValueConst value)
{
    AthenaFont *font = JS_GetOpaque2(ctx, value, font_class_id);
    if (font == &font_freed) {
        JS_ThrowTypeError(ctx, "Font was freed");
        return NULL;
    }
    return font;
}

static void font_finalizer(JSRuntime *rt, JSValue value)
{
    AthenaFont *font = JS_GetOpaque(value, font_class_id);
    if (font && font != &font_freed)
        athena_font_destroy(font);
    JS_SetOpaque(value, NULL);
}

/* Reads the optional { size } of the constructor; 0 when absent. */
static int font_options(JSContext *ctx, JSValueConst options, int *size)
{
    JSValue value;
    double number;

    *size = 0;
    if (JS_IsUndefined(options))
        return 1;
    if (!JS_IsObject(options) || JS_IsArray(ctx, options)) {
        JS_ThrowTypeError(ctx, "Font options must be an object");
        return 0;
    }
    value = JS_GetPropertyStr(ctx, options, "size");
    if (JS_IsException(value))
        return 0;
    if (!JS_IsUndefined(value)) {
        if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &number, value) ||
            number != floor(number) || number < FNTSYS_MIN_SIZE || number > FNTSYS_MAX_SIZE) {
            JS_FreeValue(ctx, value);
            JS_ThrowRangeError(ctx, "Font size must be an integer from %d to %d pixels",
                FNTSYS_MIN_SIZE, FNTSYS_MAX_SIZE);
            return 0;
        }
        *size = (int)number;
    }
    JS_FreeValue(ctx, value);
    return 1;
}

static JSValue font_load_error(JSContext *ctx, const char *path, int error)
{
    if (error == ATHENA_FONT_ERR_SLOTS)
        return JS_ThrowInternalError(ctx, "Unable to load font '%s': %d different "
            "fonts are already loaded; free() the ones no longer used",
            path ? path : "default", FNT_MAX_COUNT);
    if (error == ATHENA_FONT_ERR_MEMORY)
        return JS_ThrowOutOfMemory(ctx);
    return JS_ThrowInternalError(ctx, "Unable to load font '%s'", path ? path : "default");
}

/* Wraps a loaded font in a new Font object; destroys it on failure. */
static JSValue font_wrap(JSContext *ctx, JSValueConst new_target, AthenaFont *font)
{
    JSValue proto, object;

    proto = JS_IsUndefined(new_target) ? JS_GetClassProto(ctx, font_class_id) :
        JS_GetPropertyStr(ctx, new_target, "prototype");
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

/* new Font(path?, { size }?) or new Font({ size }): an undefined or null path is the embedded font. */
static JSValue font_ctor(JSContext *ctx, JSValueConst new_target, int argc,
    JSValueConst *argv)
{
    const char *path = NULL;
    JSValueConst options = JS_UNDEFINED;
    AthenaFont *font;
    JSValue result;
    int size, error;

    if (!font_argc(ctx, argc, 0, 2, "Font"))
        return JS_EXCEPTION;
    if (argc >= 1 && JS_IsObject(argv[0])) {
        if (argc == 2)
            return JS_ThrowTypeError(ctx, "Font accepts a path and options, or options alone");
        options = argv[0];
    } else {
        if (argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0]) && !JS_IsString(argv[0]))
            return JS_ThrowTypeError(ctx, "Font path must be a string");
        if (argc == 2)
            options = argv[1];
    }
    if (!font_options(ctx, options, &size))
        return JS_EXCEPTION;
    if (argc >= 1 && JS_IsString(argv[0])) {
        path = JS_ToCString(ctx, argv[0]);
        if (!path)
            return JS_EXCEPTION;
    }

    font = athena_font_load_ex(path, size, &error);
    if (!font && (error == ATHENA_FONT_ERR_SLOTS || error == ATHENA_FONT_ERR_MEMORY)) {
        /* Fonts no longer referenced may still wait for the collector. */
        JS_RunGC(JS_GetRuntime(ctx));
        font = athena_font_load_ex(path, size, &error);
    }
    result = font ? font_wrap(ctx, new_target, font) : font_load_error(ctx, path, error);
    if (path)
        JS_FreeCString(ctx, path);
    return result;
}

/* Releases the font now instead of when the collector finds the object. */
static JSValue font_free(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    AthenaFont *font = JS_GetOpaque2(ctx, this_val, font_class_id);

    if (!font)
        return JS_EXCEPTION;   /* not a Font: JS_GetOpaque2 threw */
    if (font != &font_freed) {
        athena_font_destroy(font);
        JS_SetOpaque(this_val, &font_freed);
    }
    return JS_UNDEFINED;
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
    /* The Font may have been freed since render(). */
    data->render->font = font_this(ctx, data->font_ref);
    if (!data->render->font)
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
    if (magic == 3) return JS_NewInt32(ctx, font->size);
    if (magic == 4) return JS_NewInt32(ctx, athena_font_get_line_height(font));
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

/* ---- Font.loadAsync: the file is read on the job pool ---- */

/* Errors of the read job. */
#define FONT_READ_FAILED    (-1)
#define FONT_READ_CANCELLED (-2)

/* Data of the read job; the pool frees it. */
typedef struct {
    char *path;         /* NULL: the embedded font, nothing to read */
    void *data;
    int length;
} FontRead;

/* Worker: only reads bytes. FreeType is not thread-safe, so the face is created on the script thread. */
static int font_read_run(AthenaJob *job, void *arg)
{
    FontRead *read = arg;
    void *data;
    int length = 0;

    if (!read->path)
        return 0;
    data = fntReadFile(read->path, &length);
    if (athena_job_should_stop(job)) {
        free(data);
        return FONT_READ_CANCELLED;
    }
    if (!data)
        return FONT_READ_FAILED;
    athena_job_lock(job);
    read->data = data;
    read->length = length;
    athena_job_unlock(job);
    return 0;
}

static void font_read_free(void *arg)
{
    FontRead *read = arg;

    free(read->data);
    free(read->path);
    free(read);
}

static const AthenaJobType font_read_type = {
    "Font", font_read_run, font_read_free, FONT_READ_CANCELLED, ATHENA_JOB_PRIORITY_IO,
};

/* What the Job object keeps: the options, for the script-thread half. */
typedef struct {
    char *path;
    int size;
} FontJobInfo;

/* Creates the Font from the bytes read, on the script thread. */
static int font_job_settle(JSContext *ctx, AthenaJob *job, AthenaJobState state, int result,
    void *user, JSValue *outcome, bool *failed)
{
    FontJobInfo *info = user;
    FontRead *read = athena_job_data(job);
    const char *name = info->path ? info->path : "default";
    AthenaFont *font;
    int error;

    *failed = true;
    if (state == ATHENA_JOB_CANCELLED) {
        JS_ThrowInternalError(ctx, "Font.loadAsync: cancelled: %s", name);
        *outcome = JS_GetException(ctx);
        return 0;
    }
    if (state != ATHENA_JOB_DONE) {
        JS_ThrowInternalError(ctx, "Unable to load font '%s'", name);
        *outcome = JS_GetException(ctx);
        return 0;
    }

    if (read->data) {
        /* The font takes the bytes on success; on failure they stay ours. */
        font = athena_font_from_memory(info->path, read->data, read->length, info->size, &error);
        if (!font && (error == ATHENA_FONT_ERR_SLOTS || error == ATHENA_FONT_ERR_MEMORY)) {
            JS_RunGC(JS_GetRuntime(ctx));
            font = athena_font_from_memory(info->path, read->data, read->length, info->size, &error);
        }
        if (font)
            read->data = NULL;
    } else {
        font = athena_font_load_ex(NULL, info->size, &error);
    }
    if (!font) {
        font_load_error(ctx, info->path, error);
        *outcome = JS_GetException(ctx);
        return 0;
    }
    *outcome = font_wrap(ctx, JS_UNDEFINED, font);
    if (JS_IsException(*outcome)) {
        *outcome = JS_UNDEFINED;
        return -1;
    }
    *failed = false;
    return 0;
}

static void font_job_free_info(JSRuntime *rt, void *user)
{
    FontJobInfo *info = user;

    free(info->path);
    free(info);
}

static const AthenaJsJobKind font_job_kind = {
    "Font", font_job_settle, NULL, font_job_free_info,
};

/* Font.loadAsync(path?, { size }): a Job that resolves with the Font. TrueType only. */
static JSValue font_load_async(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    const char *name = "Font.loadAsync";
    JSValueConst options = JS_UNDEFINED;
    const char *path = NULL;
    FontJobInfo *info;
    FontRead *read;
    int size;

    if (!font_argc(ctx, argc, 0, 2, name))
        return JS_EXCEPTION;
    if (argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0]) && !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "%s path must be a string", name);
    if (argc == 2)
        options = argv[1];
    if (!font_options(ctx, options, &size))
        return JS_EXCEPTION;
    if (argc >= 1 && JS_IsString(argv[0])) {
        const char *extension;
        path = JS_ToCString(ctx, argv[0]);
        if (!path)
            return JS_EXCEPTION;
        extension = strrchr(path, '.');
        if (extension && (!strcasecmp(extension, ".png") || !strcasecmp(extension, ".bmp") ||
            !strcasecmp(extension, ".jpg") || !strcasecmp(extension, ".jpeg"))) {
            JS_FreeCString(ctx, path);
            return JS_ThrowTypeError(ctx, "%s loads TrueType fonts; load bitmap fonts with new Font()", name);
        }
        if (!strcmp(path, "default")) {
            JS_FreeCString(ctx, path);
            path = NULL;
        }
    }

    info = calloc(1, sizeof(*info));
    read = calloc(1, sizeof(*read));
    if (!info || !read || (path && (!(info->path = strdup(path)) || !(read->path = strdup(path))))) {
        if (path)
            JS_FreeCString(ctx, path);
        if (info)
            free(info->path);
        if (read)
            free(read->path);
        free(info);
        free(read);
        return JS_ThrowOutOfMemory(ctx);
    }
    if (path)
        JS_FreeCString(ctx, path);
    info->size = size;
    return athena_js_job_new(ctx, &font_job_kind, athena_job_submit(&font_read_type, read), info, name);
}

static JSValue font_job_poll(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (!font_argc(ctx, argc, 1, 1, "Font.poll"))
        return JS_EXCEPTION;
    return athena_js_job_poll(ctx, argv[0], &font_job_kind, "Font.poll");
}

static JSValue font_job_wait(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (!font_argc(ctx, argc, 1, 2, "Font.wait"))
        return JS_EXCEPTION;
    return athena_js_job_wait(ctx, argv[0], argc - 1, argv + 1, &font_job_kind, "Font.wait");
}

static JSValue font_job_cancel(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    if (!font_argc(ctx, argc, 1, 1, "Font.cancel"))
        return JS_EXCEPTION;
    return athena_js_job_cancel(ctx, argv[0], &font_job_kind, "Font.cancel");
}

static const JSCFunctionListEntry font_proto[] = {
    JS_CGETSET_MAGIC_DEF("scale", font_get_number, font_set_number, 0),
    JS_CGETSET_MAGIC_DEF("outline", font_get_number, font_set_number, 1),
    JS_CGETSET_MAGIC_DEF("dropshadow", font_get_number, font_set_number, 2),
    JS_CGETSET_MAGIC_DEF("color", font_get_color, font_set_color, 0),
    JS_CGETSET_MAGIC_DEF("outlineColor", font_get_color, font_set_color, 1),
    JS_CGETSET_MAGIC_DEF("dropshadowColor", font_get_color, font_set_color, 2),
    /* Original names, kept for existing scripts. */
    JS_CGETSET_MAGIC_DEF("outline_color", font_get_color, font_set_color, 1),
    JS_CGETSET_MAGIC_DEF("dropshadow_color", font_get_color, font_set_color, 2),
    JS_CGETSET_MAGIC_DEF("align", font_get_color, font_set_color, 3),
    JS_CGETSET_MAGIC_DEF("size", font_get_number, NULL, 3),
    JS_CGETSET_MAGIC_DEF("lineHeight", font_get_number, NULL, 4),
    JS_CFUNC_DEF("print", 3, font_print),
    JS_CFUNC_DEF("free", 0, font_free),
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
    JS_CFUNC_DEF("loadAsync", 2, font_load_async),
    JS_CFUNC_DEF("poll", 1, font_job_poll),
    JS_CFUNC_DEF("wait", 2, font_job_wait),
    JS_CFUNC_DEF("cancel", 1, font_job_cancel),
};

static int font_module_init(JSContext *ctx, JSModuleDef *module)
{
    JSValue font_proto_value, font_constructor, render_proto_value;

    if (!font_system_initialized) {
        /* Glyph proportions follow the video mode, so the GS is set up first. */
        graphics_service_init();
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
