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
static void render_mark(JSRuntime *rt, JSValueConst value, JS_MarkFunc *mark_func);
static JSClassDef font_class = {
    "Font",
    .finalizer = font_finalizer,
};
/*
 * A FontRender holds its Font (font_ref) from C: gc_mark must report that
 * edge. Without it the cycle collector counts those references as external,
 * so the Font looks alive and keeps alive everything it reaches (its
 * prototype's methods hold the context, which holds the globals and modules
 * that hold the FontRender): a render kept in a global or a module until the
 * script ends leaked, and JS_FreeRuntime aborted on the leak.
 */
static JSClassDef render_class = {
    "FontRender",
    .finalizer = render_finalizer,
    .gc_mark = render_mark,
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

static void render_mark(JSRuntime *rt, JSValueConst value, JS_MarkFunc *mark_func)
{
    FontRenderData *data = JS_GetOpaque(value, render_class_id);
    if (data)
        JS_MarkValue(rt, data->font_ref, mark_func);
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

/* ---- Glyph preloading: rasterized a slice per frame ---- */

/* Printable ASCII, the default set of font.preload() and the loadAsync `preload` option. */
#define FONT_ASCII " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"

/* Rasterization time per frame when `budgetMs` is not given. */
#define FONT_PRELOAD_BUDGET_MS 2.0

/*
 * Reads an optional `budgetMs` into `budget`, left untouched when absent;
 * 0 on error, with an exception pending.
 */
static int font_budget_option(JSContext *ctx, JSValueConst options, double *budget,
    const char *name)
{
    JSValue value = JS_GetPropertyStr(ctx, options, "budgetMs");
    int ok = 1;

    if (JS_IsException(value))
        return 0;
    if (!JS_IsUndefined(value)) {
        if (!JS_IsNumber(value) || JS_ToFloat64(ctx, budget, value) ||
            !isfinite(*budget) || *budget < 0) {
            JS_ThrowRangeError(ctx, "%s budgetMs must be a non-negative number of milliseconds", name);
            ok = 0;
        }
    }
    JS_FreeValue(ctx, value);
    return ok;
}

/* Calls `func(arg)` and drops the result; -1 when it threw. */
static int font_call1(JSContext *ctx, JSValueConst func, JSValueConst arg)
{
    JSValue ret = JS_Call(ctx, func, JS_UNDEFINED, 1, &arg);

    if (JS_IsException(ret))
        return -1;
    JS_FreeValue(ctx, ret);
    return 0;
}

/*
 * Runs `func` in `delay_ms` through the event loop's timers. With the Loop
 * running, expired timers run once per frame, between the flip and the next
 * frame; a delay of 1 ms keeps a timer that schedules itself from running
 * twice in the same frame.
 */
static int font_set_timeout(JSContext *ctx, JSValue func, int delay_ms)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue set_timeout = JS_GetPropertyStr(ctx, global, "setTimeout");
    JSValue args[2], ret;

    JS_FreeValue(ctx, global);
    if (JS_IsException(func) || !JS_IsFunction(ctx, set_timeout)) {
        JS_FreeValue(ctx, func);
        JS_FreeValue(ctx, set_timeout);
        if (!JS_IsException(func))
            JS_ThrowInternalError(ctx, "Font.preload needs setTimeout");
        return -1;
    }
    args[0] = func;
    args[1] = JS_NewInt32(ctx, delay_ms);
    ret = JS_Call(ctx, set_timeout, JS_UNDEFINED, 2, args);
    JS_FreeValue(ctx, func);
    JS_FreeValue(ctx, set_timeout);
    if (JS_IsException(ret))
        return -1;
    JS_FreeValue(ctx, ret);
    return 0;
}

static JSValue font_preload_tick(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int magic, JSValue *data);

/* The preload in progress, kept in the timer function's data. */
enum {
    PRELOAD_FONT,       /* the Font object */
    PRELOAD_TEXT,       /* the characters, a string */
    PRELOAD_OFFSET,     /* UTF-8 byte of the next character */
    PRELOAD_BUDGET,     /* milliseconds per slice */
    PRELOAD_RESOLVE,
    PRELOAD_REJECT,
    PRELOAD_COUNT
};

/*
 * One slice: rasterizes glyphs until the budget is spent, then resolves the
 * promise with the font or schedules the next slice. 0, or -1 with an
 * exception pending.
 */
static int font_preload_slice(JSContext *ctx, JSValueConst *data)
{
    AthenaFont *font = JS_GetOpaque(data[PRELOAD_FONT], font_class_id);
    const char *text;
    int32_t offset;
    double budget;
    JSValue next[PRELOAD_COUNT];
    int done, i, position;

    if (!font || font == &font_freed) {
        JSValue error;
        JS_ThrowTypeError(ctx, "Font.preload: the Font was freed");
        error = JS_GetException(ctx);
        i = font_call1(ctx, data[PRELOAD_REJECT], error);
        JS_FreeValue(ctx, error);
        return i;
    }
    if (JS_ToInt32(ctx, &offset, data[PRELOAD_OFFSET]) ||
        JS_ToFloat64(ctx, &budget, data[PRELOAD_BUDGET]))
        return -1;
    text = JS_ToCString(ctx, data[PRELOAD_TEXT]);
    if (!text)
        return -1;
    position = offset;
    done = athena_font_preload(font, text, &position, (float)budget);
    JS_FreeCString(ctx, text);
    if (done)
        return font_call1(ctx, data[PRELOAD_RESOLVE], data[PRELOAD_FONT]);

    for (i = 0; i < PRELOAD_COUNT; i++)
        next[i] = data[i];
    next[PRELOAD_OFFSET] = JS_NewInt32(ctx, position);
    return font_set_timeout(ctx, JS_NewCFunctionData(ctx,
        font_preload_tick, 0, 0, PRELOAD_COUNT, next), 1);
}

static JSValue font_preload_tick(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int magic, JSValue *data)
{
    return font_preload_slice(ctx, (JSValueConst *)data) < 0 ? JS_EXCEPTION : JS_UNDEFINED;
}

/*
 * font.preload(chars = Font.ASCII, { budgetMs = 2 }): rasterizes the glyphs
 * ahead of the first print, `budgetMs` per frame (0: all now). Resolves with
 * the font.
 */
static JSValue font_preload(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    AthenaFont *font = font_this(ctx, this_val);
    const char *name = "Font.preload";
    double budget = FONT_PRELOAD_BUDGET_MS;
    JSValue data[PRELOAD_COUNT], funcs[2], promise;
    int i, failed;

    if (!font || !font_argc(ctx, argc, 0, 2, name))
        return JS_EXCEPTION;
    if (argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "%s characters must be a string", name);
    if (argc == 2 && !JS_IsUndefined(argv[1])) {
        if (!JS_IsObject(argv[1]) || JS_IsArray(ctx, argv[1]))
            return JS_ThrowTypeError(ctx, "%s options must be an object", name);
        if (!font_budget_option(ctx, argv[1], &budget, name))
            return JS_EXCEPTION;
    }

    promise = JS_NewPromiseCapability(ctx, funcs);
    if (JS_IsException(promise))
        return promise;
    data[PRELOAD_FONT] = JS_DupValue(ctx, this_val);
    data[PRELOAD_TEXT] = argc >= 1 && JS_IsString(argv[0]) ?
        JS_DupValue(ctx, argv[0]) : JS_NewString(ctx, FONT_ASCII);
    data[PRELOAD_OFFSET] = JS_NewInt32(ctx, 0);
    data[PRELOAD_BUDGET] = JS_NewFloat64(ctx, budget);
    data[PRELOAD_RESOLVE] = funcs[0];
    data[PRELOAD_REJECT] = funcs[1];

    /* The first slice runs now: a few characters are done without waiting a frame. */
    failed = JS_IsException(data[PRELOAD_TEXT]) || font_preload_slice(ctx, data) < 0;
    for (i = 0; i < PRELOAD_COUNT; i++)
        JS_FreeValue(ctx, data[i]);
    if (failed) {
        JS_FreeValue(ctx, promise);
        return JS_EXCEPTION;
    }
    return promise;
}

/* ---- Font.loadAsync: the file is read (TrueType) or decoded (bitmap) on the job pool ---- */

/* Errors of the read job. */
#define FONT_READ_FAILED    (-1)
#define FONT_READ_CANCELLED (-2)

/* Data of the read job; the pool frees it. */
typedef struct {
    char *path;         /* NULL: the embedded font, nothing to read */
    int bitmap;         /* image and .dat, decoded by loadFont() */
    void *data;         /* TrueType file */
    int length;
    GSFONT *decoded;    /* bitmap font, CPU memory only */
} FontRead;

static int font_is_bitmap_path(const char *path)
{
    const char *extension = path ? strrchr(path, '.') : NULL;

    return extension && (!strcasecmp(extension, ".png") || !strcasecmp(extension, ".bmp") ||
        !strcasecmp(extension, ".jpg") || !strcasecmp(extension, ".jpeg"));
}

/*
 * Worker: reads the TrueType bytes, or decodes the bitmap font's image and
 * widths. FreeType and the texture manager are not thread-safe, so the face
 * is created, and the texture uploaded, on the script thread.
 */
static int font_read_run(AthenaJob *job, void *arg)
{
    FontRead *read = arg;
    void *data = NULL;
    GSFONT *decoded = NULL;
    int length = 0;

    if (!read->path)
        return 0;
    if (read->bitmap)
        decoded = loadFont(read->path);
    else
        data = fntReadFile(read->path, &length);
    if (athena_job_should_stop(job)) {
        free(data);
        athena_bitmap_font_discard(decoded);
        return FONT_READ_CANCELLED;
    }
    if (!data && !decoded)
        return FONT_READ_FAILED;
    athena_job_lock(job);
    read->data = data;
    read->length = length;
    read->decoded = decoded;
    athena_job_unlock(job);
    return 0;
}

static void font_read_free(void *arg)
{
    FontRead *read = arg;

    free(read->data);
    athena_bitmap_font_discard(read->decoded);
    free(read->path);
    free(read);
}

/* Reading a file mostly waits for the IOP; decoding an image uses the CPU. */
static const AthenaJobType font_read_type = {
    "Font", font_read_run, font_read_free, FONT_READ_CANCELLED, ATHENA_JOB_PRIORITY_IO,
};
static const AthenaJobType font_decode_type = {
    "Font", font_read_run, font_read_free, FONT_READ_CANCELLED, ATHENA_JOB_PRIORITY_CPU,
};

/* What the Job object keeps: the options, and the font while its glyphs are preloaded. */
typedef struct {
    char *path;
    int size;
    char *preload;          /* characters to rasterize before resolving, or NULL */
    float budget_ms;
    int offset;             /* progress in `preload` */
    int created;            /* the script-thread half started */
    int error;              /* ATHENA_FONT_ERR_* when creating the font failed */
    AthenaFont *font;       /* created, not yet handed to a Font object */
} FontJobInfo;

/* Creates the font from what the worker read, on the script thread. */
static void font_job_create(JSContext *ctx, AthenaJob *job, FontJobInfo *info)
{
    FontRead *read = athena_job_data(job);
    int error = ATHENA_FONT_OK;

    info->created = 1;
    if (read->decoded) {
        info->font = athena_font_from_bitmap(read->decoded);
        if (info->font)
            read->decoded = NULL;
        else
            error = ATHENA_FONT_ERR_MEMORY;
    } else if (read->data) {
        /* The font takes the bytes on success; on failure they stay ours. */
        info->font = athena_font_from_memory(info->path, read->data, read->length, info->size, &error);
        if (!info->font && (error == ATHENA_FONT_ERR_SLOTS || error == ATHENA_FONT_ERR_MEMORY)) {
            JS_RunGC(JS_GetRuntime(ctx));
            info->font = athena_font_from_memory(info->path, read->data, read->length, info->size, &error);
        }
        if (info->font)
            read->data = NULL;
    } else {
        info->font = athena_font_load_ex(NULL, info->size, &error);
    }
    info->error = info->font ? ATHENA_FONT_OK : error;
}

/* Script-thread half: creates the font, then rasterizes the `preload` glyphs a slice per tick. */
static int font_job_advance(JSContext *ctx, AthenaJob *job, void *user)
{
    FontJobInfo *info = user;

    if (!info->created)
        font_job_create(ctx, job, info);
    if (!info->font || !info->preload)
        return 0;
    return athena_font_preload(info->font, info->preload, &info->offset, info->budget_ms) ? 0 : 1;
}

/* Hands the Font to the script, or the reason it could not be loaded. */
static int font_job_settle(JSContext *ctx, AthenaJob *job, AthenaJobState state, int result,
    void *user, JSValue *outcome, bool *failed)
{
    FontJobInfo *info = user;
    const char *name = info->path ? info->path : "default";
    AthenaFont *font;

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
    if (!info->created)
        font_job_create(ctx, job, info);
    if (!info->font) {
        font_load_error(ctx, info->path, info->error);
        *outcome = JS_GetException(ctx);
        return 0;
    }

    font = info->font;
    info->font = NULL;          /* font_wrap() takes it, or destroys it */
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

    athena_font_destroy(info->font);
    free(info->preload);
    free(info->path);
    free(info);
}

static const AthenaJsJobKind font_job_kind = {
    "Font", font_job_settle, NULL, font_job_free_info, NULL, NULL, font_job_advance,
};

/*
 * Reads the loadAsync options: { size, preload, budgetMs }. `preload` is a
 * string of characters, or true for Font.ASCII. 0 on error, with an
 * exception pending.
 */
static int font_async_options(JSContext *ctx, JSValueConst options, FontJobInfo *info,
    const char *name)
{
    double budget = FONT_PRELOAD_BUDGET_MS;
    JSValue value;
    const char *chars;

    if (!font_options(ctx, options, &info->size))
        return 0;
    info->budget_ms = (float)budget;
    if (JS_IsUndefined(options))
        return 1;
    if (!font_budget_option(ctx, options, &budget, name))
        return 0;
    info->budget_ms = (float)budget;

    value = JS_GetPropertyStr(ctx, options, "preload");
    if (JS_IsException(value))
        return 0;
    if (JS_IsUndefined(value) || JS_IsBool(value)) {
        if (JS_ToBool(ctx, value) && !(info->preload = strdup(FONT_ASCII))) {
            JS_ThrowOutOfMemory(ctx);
            return 0;
        }
        return 1;
    }
    if (!JS_IsString(value)) {
        JS_FreeValue(ctx, value);
        JS_ThrowTypeError(ctx, "%s preload must be a string of characters or true", name);
        return 0;
    }
    chars = JS_ToCString(ctx, value);
    JS_FreeValue(ctx, value);
    if (!chars)
        return 0;
    info->preload = strdup(chars);
    JS_FreeCString(ctx, chars);
    if (!info->preload) {
        JS_ThrowOutOfMemory(ctx);
        return 0;
    }
    return 1;
}

/*
 * Font.loadAsync(path?, { size, preload, budgetMs }): a Job that resolves
 * with the Font, after rasterizing the `preload` glyphs when given.
 */
static JSValue font_load_async(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    const char *name = "Font.loadAsync";
    const char *path = NULL;
    FontJobInfo *info;
    FontRead *read;
    int bitmap;

    if (!font_argc(ctx, argc, 0, 2, name))
        return JS_EXCEPTION;
    if (argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0]) && !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "%s path must be a string", name);

    info = calloc(1, sizeof(*info));
    if (!info)
        return JS_ThrowOutOfMemory(ctx);
    if (!font_async_options(ctx, argc == 2 ? argv[1] : JS_UNDEFINED, info, name)) {
        font_job_free_info(JS_GetRuntime(ctx), info);
        return JS_EXCEPTION;
    }
    if (argc >= 1 && JS_IsString(argv[0])) {
        path = JS_ToCString(ctx, argv[0]);
        if (!path) {
            font_job_free_info(JS_GetRuntime(ctx), info);
            return JS_EXCEPTION;
        }
        if (!strcmp(path, "default")) {
            JS_FreeCString(ctx, path);
            path = NULL;
        }
    }
    bitmap = font_is_bitmap_path(path);

    read = calloc(1, sizeof(*read));
    if (!read || (path && (!(info->path = strdup(path)) || !(read->path = strdup(path))))) {
        if (path)
            JS_FreeCString(ctx, path);
        if (read)
            free(read->path);
        free(read);
        font_job_free_info(JS_GetRuntime(ctx), info);
        return JS_ThrowOutOfMemory(ctx);
    }
    if (path)
        JS_FreeCString(ctx, path);
    read->bitmap = bitmap;
    return athena_js_job_new(ctx, &font_job_kind,
        athena_job_submit(bitmap ? &font_decode_type : &font_read_type, read), info, name);
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
    JS_CFUNC_DEF("preload", 2, font_preload),
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
    JS_PROP_STRING_DEF("ASCII", FONT_ASCII, JS_PROP_CONFIGURABLE),
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
