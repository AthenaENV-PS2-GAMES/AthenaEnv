#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ath_env.h>
#include <athena/sound.h>
#include <athena/js/job.h>

#include "ath_sound.h"

static JSClassID stream_class_id;
static JSClassID sfx_class_id;

/*
 * A Stream object. Streams with pending callbacks are found by
 * Sound.process() through this list; `object` is not a counted reference,
 * the finalizer unlinks the entry before the object goes away.
 */
typedef struct JSSoundStream {
    AthenaSoundStream *stream;
    JSValue object;
    JSValue on_end;
    JSValue on_loop;
    uint32_t seen_ends;
    uint32_t seen_loops;
    struct JSSoundStream *prev;
    struct JSSoundStream *next;
} JSSoundStream;

static JSSoundStream *js_streams;

/* --- Errors ------------------------------------------------------------- */

typedef enum {
    SOUND_TYPE_ERROR,
    SOUND_RANGE_ERROR,
    SOUND_INTERNAL_ERROR,
} SoundErrorKind;

/* Throws with a stable `error.code`, like Archive. */
static JSValue sound_raise(JSContext *ctx, SoundErrorKind kind, const char *code, const char *fmt, ...) {
    char message[384];
    va_list args;
    JSValue error;

    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    if (kind == SOUND_TYPE_ERROR)
        JS_ThrowTypeError(ctx, "%s", message);
    else if (kind == SOUND_RANGE_ERROR)
        JS_ThrowRangeError(ctx, "%s", message);
    else
        JS_ThrowInternalError(ctx, "%s", message);
    error = JS_GetException(ctx);
    JS_SetPropertyStr(ctx, error, "code", JS_NewString(ctx, code));
    return JS_Throw(ctx, error);
}

static int sound_argc(JSContext *ctx, int argc, int minimum, int maximum, const char *name) {
    if (argc < minimum || argc > maximum) {
        if (minimum == maximum)
            sound_raise(ctx, SOUND_TYPE_ERROR, "INVALID_ARGUMENT", "%s expects %d argument%s",
                name, minimum, minimum == 1 ? "" : "s");
        else
            sound_raise(ctx, SOUND_TYPE_ERROR, "INVALID_ARGUMENT",
                "%s expects between %d and %d arguments", name, minimum, maximum);
        return 0;
    }
    return 1;
}

/* Integer in [minimum, maximum]; rejects NaN, fractions and non-numbers. */
static int sound_int(JSContext *ctx, JSValueConst value, int minimum, int maximum,
    const char *name, int *out) {
    double number;

    if (!JS_IsNumber(value)) {
        sound_raise(ctx, SOUND_TYPE_ERROR, "INVALID_ARGUMENT", "%s must be a number", name);
        return 0;
    }
    if (JS_ToFloat64(ctx, &number, value))
        return 0;
    if (!isfinite(number) || floor(number) != number || number < minimum || number > maximum) {
        sound_raise(ctx, SOUND_RANGE_ERROR, "INVALID_ARGUMENT",
            "%s must be an integer between %d and %d", name, minimum, maximum);
        return 0;
    }
    *out = (int)number;
    return 1;
}

/*
 * Throws for a native failure. With `detail` the message also carries
 * athena_sound_error_detail(), set by the open/load that just failed.
 */
static JSValue sound_throw(JSContext *ctx, const char *name, int result, const char *path, bool detail) {
    const char *extra = detail ? athena_sound_error_detail() : "";
    const char *code = athena_sound_result_code(result);
    const char *what = athena_sound_result_string(result);

    if (result == ATHENA_SOUND_ERR_ARGS)
        return sound_raise(ctx, SOUND_RANGE_ERROR, code, "%s: %s", name, what);
    if (extra[0] && path)
        return sound_raise(ctx, SOUND_INTERNAL_ERROR, code, "%s: %s (%s): %s", name, what, extra, path);
    if (extra[0])
        return sound_raise(ctx, SOUND_INTERNAL_ERROR, code, "%s: %s (%s)", name, what, extra);
    if (path)
        return sound_raise(ctx, SOUND_INTERNAL_ERROR, code, "%s: %s: %s", name, what, path);
    return sound_raise(ctx, SOUND_INTERNAL_ERROR, code, "%s: %s", name, what);
}

static const char *sound_path(JSContext *ctx, JSValueConst value, const char *name) {
    if (!JS_IsString(value)) {
        sound_raise(ctx, SOUND_TYPE_ERROR, "INVALID_ARGUMENT", "%s: path must be a string", name);
        return NULL;
    }
    return JS_ToCString(ctx, value);
}

/* Optional `{ fade: ms }` of play/pause/stop. */
static int sound_fade_option(JSContext *ctx, int argc, JSValueConst *argv, const char *name,
    uint32_t *fade_ms) {
    char what[48];
    JSValue fade;
    int ms = 0;
    int ok;

    *fade_ms = 0;
    if (!sound_argc(ctx, argc, 0, 1, name))
        return 0;
    if (argc == 0 || JS_IsUndefined(argv[0]))
        return 1;
    if (!JS_IsObject(argv[0]) || JS_IsFunction(ctx, argv[0])) {
        sound_raise(ctx, SOUND_TYPE_ERROR, "INVALID_ARGUMENT", "%s: options must be an object", name);
        return 0;
    }
    fade = JS_GetPropertyStr(ctx, argv[0], "fade");
    if (JS_IsException(fade))
        return 0;
    snprintf(what, sizeof(what), "%s options.fade", name);
    ok = JS_IsUndefined(fade) || sound_int(ctx, fade, 0, ATHENA_SOUND_MAX_FADE_MS, what, &ms);
    JS_FreeValue(ctx, fade);
    *fade_ms = (uint32_t)ms;
    return ok;
}

/* Creates an object of `class_id` whose prototype follows `new_target`. */
static JSValue sound_new_object(JSContext *ctx, JSValueConst new_target, JSClassID class_id) {
    JSValue proto, object;

    if (JS_IsUndefined(new_target))
        return JS_NewObjectClass(ctx, (int)class_id);
    proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    if (JS_IsException(proto))
        return proto;
    object = JS_NewObjectProtoClass(ctx, proto, class_id);
    JS_FreeValue(ctx, proto);
    return object;
}

/* --- Module functions --------------------------------------------------- */

static JSValue js_sound_set_volume(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    int volume, result;

    if (!sound_argc(ctx, argc, 1, 1, "Sound.setVolume") ||
        !sound_int(ctx, argv[0], 0, ATHENA_SOUND_MAX_VOLUME, "Sound.setVolume volume", &volume))
        return JS_EXCEPTION;
    result = athena_sound_set_volume(volume);
    if (result < 0)
        return sound_throw(ctx, "Sound.setVolume", result, NULL, false);
    return JS_UNDEFINED;
}

static JSValue js_sound_get_volume(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (!sound_argc(ctx, argc, 0, 0, "Sound.getVolume"))
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, athena_sound_get_volume());
}

static JSValue js_sound_set_sfx_volume(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    int volume, result;

    if (!sound_argc(ctx, argc, 1, 1, "Sound.setSfxVolume") ||
        !sound_int(ctx, argv[0], 0, ATHENA_SOUND_MAX_VOLUME, "Sound.setSfxVolume volume", &volume))
        return JS_EXCEPTION;
    result = athena_sfx_set_master_volume(volume);
    if (result < 0)
        return sound_throw(ctx, "Sound.setSfxVolume", result, NULL, false);
    return JS_UNDEFINED;
}

static JSValue js_sound_get_sfx_volume(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (!sound_argc(ctx, argc, 0, 0, "Sound.getSfxVolume"))
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, athena_sfx_get_master_volume());
}

static JSValue js_sound_find_channel(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    int result;

    if (!sound_argc(ctx, argc, 0, 0, "Sound.findChannel"))
        return JS_EXCEPTION;
    result = athena_sound_ensure();
    if (result < 0)
        return sound_throw(ctx, "Sound.findChannel", result, NULL, false);
    return JS_NewInt32(ctx, athena_sfx_find_channel());
}

static JSValue js_sound_get_memory_stats(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaSoundMemoryStats stats;
    JSValue object;

    if (!sound_argc(ctx, argc, 0, 0, "Sound.getMemoryStats"))
        return JS_EXCEPTION;
    athena_sfx_get_memory_stats(&stats);
    object = JS_NewObject(ctx);
    if (JS_IsException(object))
        return object;
    JS_SetPropertyStr(ctx, object, "total", JS_NewUint32(ctx, stats.total));
    JS_SetPropertyStr(ctx, object, "used", JS_NewUint32(ctx, stats.used));
    JS_SetPropertyStr(ctx, object, "free", JS_NewUint32(ctx, stats.free));
    JS_SetPropertyStr(ctx, object, "wasted", JS_NewUint32(ctx, stats.wasted));
    JS_SetPropertyStr(ctx, object, "samples", JS_NewUint32(ctx, stats.samples));
    return object;
}

/*
 * Runs the onLoop/onEnd callbacks of streams that looped or ended since the
 * last call; each at most once per call. Returns how many ran. A callback
 * may free or create streams: the calls are collected first.
 */
static JSValue js_sound_process(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JSValue *calls;
    int count = 0, pending = 0;

    if (!sound_argc(ctx, argc, 0, 0, "Sound.process"))
        return JS_EXCEPTION;
    for (JSSoundStream *entry = js_streams; entry; entry = entry->next)
        count++;
    if (count == 0)
        return JS_NewInt32(ctx, 0);
    /* Pairs of (stream object, callback). */
    calls = js_malloc(ctx, sizeof(JSValue) * 4 * (size_t)count);
    if (!calls)
        return JS_EXCEPTION;

    for (JSSoundStream *entry = js_streams; entry; entry = entry->next) {
        uint32_t ends = entry->seen_ends, loops = entry->seen_loops;
        bool looped, ended;

        athena_sound_stream_get_events(entry->stream, &ends, &loops);
        looped = loops != entry->seen_loops;
        ended = ends != entry->seen_ends;
        entry->seen_loops = loops;
        entry->seen_ends = ends;
        if (looped && JS_IsFunction(ctx, entry->on_loop)) {
            calls[pending++] = JS_DupValue(ctx, entry->object);
            calls[pending++] = JS_DupValue(ctx, entry->on_loop);
        }
        if (ended && JS_IsFunction(ctx, entry->on_end)) {
            calls[pending++] = JS_DupValue(ctx, entry->object);
            calls[pending++] = JS_DupValue(ctx, entry->on_end);
        }
    }

    for (int i = 0; i < pending; i += 2) {
        JSValue result = JS_Call(ctx, calls[i + 1], calls[i], 0, NULL);

        if (JS_IsException(result)) {
            /* The exception propagates as is; the other callbacks are dropped. */
            for (int j = i; j < pending; j++)
                JS_FreeValue(ctx, calls[j]);
            js_free(ctx, calls);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, calls[i]);
        JS_FreeValue(ctx, calls[i + 1]);
    }
    js_free(ctx, calls);
    return JS_NewInt32(ctx, pending / 2);
}

/* --- Stream ------------------------------------------------------------- */

static void stream_entry_release(JSRuntime *rt, JSSoundStream *entry) {
    if (entry->prev)
        entry->prev->next = entry->next;
    else
        js_streams = entry->next;
    if (entry->next)
        entry->next->prev = entry->prev;
    athena_sound_stream_destroy(entry->stream);
    JS_FreeValueRT(rt, entry->on_end);
    JS_FreeValueRT(rt, entry->on_loop);
    free(entry);
}

static void stream_finalizer(JSRuntime *rt, JSValue value) {
    JSSoundStream *entry = JS_GetOpaque(value, stream_class_id);
    if (entry) stream_entry_release(rt, entry);
}

/* The callbacks often capture the stream itself: let the GC see the cycle. */
static void stream_gc_mark(JSRuntime *rt, JSValueConst value, JS_MarkFunc *mark_func) {
    JSSoundStream *entry = JS_GetOpaque(value, stream_class_id);

    if (entry) {
        JS_MarkValue(rt, entry->on_end, mark_func);
        JS_MarkValue(rt, entry->on_loop, mark_func);
    }
}

static JSClassDef stream_class = {
    "Stream",
    .finalizer = stream_finalizer,
    .gc_mark = stream_gc_mark,
};

static JSSoundStream *stream_entry(JSContext *ctx, JSValueConst value) {
    JSSoundStream *entry = JS_GetOpaque(value, stream_class_id);

    /* Also NULL after free(). */
    if (!entry)
        sound_raise(ctx, SOUND_TYPE_ERROR, "FREED", "not a Sound.Stream, or it was freed");
    return entry;
}

static AthenaSoundStream *stream_this(JSContext *ctx, JSValueConst value) {
    JSSoundStream *entry = stream_entry(ctx, value);
    return entry ? entry->stream : NULL;
}

static JSValue js_stream_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    AthenaSoundStream *stream;
    JSSoundStream *entry;
    const char *path;
    JSValue object;
    int result;

    if (!sound_argc(ctx, argc, 1, 1, "Sound.Stream"))
        return JS_EXCEPTION;
    path = sound_path(ctx, argv[0], "Sound.Stream");
    if (!path)
        return JS_EXCEPTION;
    stream = athena_sound_stream_open(path, &result);
    if (!stream) {
        object = sound_throw(ctx, "Sound.Stream", result, path, true);
        JS_FreeCString(ctx, path);
        return object;
    }
    JS_FreeCString(ctx, path);

    entry = calloc(1, sizeof(*entry));
    if (!entry) {
        athena_sound_stream_destroy(stream);
        return JS_ThrowOutOfMemory(ctx);
    }
    object = sound_new_object(ctx, new_target, stream_class_id);
    if (JS_IsException(object)) {
        athena_sound_stream_destroy(stream);
        free(entry);
        return object;
    }
    entry->stream = stream;
    entry->object = object;
    entry->on_end = JS_NULL;
    entry->on_loop = JS_NULL;
    entry->next = js_streams;
    if (js_streams)
        js_streams->prev = entry;
    js_streams = entry;
    JS_SetOpaque(object, entry);
    return object;
}

static JSValue js_stream_play(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaSoundStream *stream = stream_this(ctx, this_val);
    uint32_t fade_ms;
    int result;

    if (!stream || !sound_fade_option(ctx, argc, argv, "Stream.play", &fade_ms))
        return JS_EXCEPTION;
    result = athena_sound_stream_play(stream, fade_ms);
    if (result < 0)
        return sound_throw(ctx, "Stream.play", result, NULL, false);
    return JS_UNDEFINED;
}

static JSValue js_stream_pause(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaSoundStream *stream = stream_this(ctx, this_val);
    uint32_t fade_ms;

    if (!stream || !sound_fade_option(ctx, argc, argv, "Stream.pause", &fade_ms))
        return JS_EXCEPTION;
    athena_sound_stream_pause(stream, fade_ms);
    return JS_UNDEFINED;
}

static JSValue js_stream_stop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaSoundStream *stream = stream_this(ctx, this_val);
    uint32_t fade_ms;

    if (!stream || !sound_fade_option(ctx, argc, argv, "Stream.stop", &fade_ms))
        return JS_EXCEPTION;
    athena_sound_stream_stop(stream, fade_ms);
    return JS_UNDEFINED;
}

static JSValue js_stream_playing(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaSoundStream *stream = stream_this(ctx, this_val);

    if (!stream || !sound_argc(ctx, argc, 0, 0, "Stream.playing"))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, athena_sound_stream_is_playing(stream));
}

static JSValue js_stream_rewind(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaSoundStream *stream = stream_this(ctx, this_val);

    if (!stream || !sound_argc(ctx, argc, 0, 0, "Stream.rewind"))
        return JS_EXCEPTION;
    athena_sound_stream_rewind(stream);
    return JS_UNDEFINED;
}

static JSValue js_stream_free(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JSSoundStream *entry = stream_entry(ctx, this_val);

    if (!entry || !sound_argc(ctx, argc, 0, 0, "Stream.free"))
        return JS_EXCEPTION;
    /* Cleared first so the finalizer does not free it again. */
    JS_SetOpaque((JSValue)this_val, NULL);
    stream_entry_release(JS_GetRuntime(ctx), entry);
    return JS_UNDEFINED;
}

enum {
    STREAM_PROP_LOOP,
    STREAM_PROP_POSITION,
    STREAM_PROP_LENGTH,
    STREAM_PROP_RATE,
    STREAM_PROP_CHANNELS,
    STREAM_PROP_FORMAT,
    STREAM_PROP_ENDED,
    STREAM_PROP_CONVERTED,
    STREAM_PROP_ON_END,
    STREAM_PROP_ON_LOOP,
};

static JSValue js_stream_get(JSContext *ctx, JSValueConst this_val, int magic) {
    JSSoundStream *entry = stream_entry(ctx, this_val);
    AthenaSoundStream *stream;

    if (!entry)
        return JS_EXCEPTION;
    stream = entry->stream;
    switch (magic) {
    case STREAM_PROP_LOOP:
        return JS_NewBool(ctx, athena_sound_stream_get_loop(stream));
    case STREAM_PROP_POSITION:
        return JS_NewUint32(ctx, athena_sound_stream_get_position(stream));
    case STREAM_PROP_LENGTH:
        return JS_NewUint32(ctx, athena_sound_stream_get_length(stream));
    case STREAM_PROP_RATE:
        return JS_NewInt32(ctx, athena_sound_stream_get_rate(stream));
    case STREAM_PROP_CHANNELS:
        return JS_NewInt32(ctx, athena_sound_stream_get_channels(stream));
    case STREAM_PROP_ENDED:
        return JS_NewBool(ctx, athena_sound_stream_ended(stream));
    case STREAM_PROP_CONVERTED:
        return JS_NewBool(ctx, athena_sound_stream_is_converted(stream));
    case STREAM_PROP_ON_END:
        return JS_DupValue(ctx, entry->on_end);
    case STREAM_PROP_ON_LOOP:
        return JS_DupValue(ctx, entry->on_loop);
    default:
        return JS_NewString(ctx,
            athena_sound_stream_get_type(stream) == ATHENA_SOUND_STREAM_OGG ? "ogg" : "wav");
    }
}

static JSValue stream_set_callback(JSContext *ctx, JSValue *slot, JSValueConst value, const char *name) {
    if (JS_IsUndefined(value))
        value = JS_NULL;
    if (!JS_IsNull(value) && !JS_IsFunction(ctx, value))
        return sound_raise(ctx, SOUND_TYPE_ERROR, "INVALID_ARGUMENT",
            "%s must be a function or null", name);
    JS_FreeValue(ctx, *slot);
    *slot = JS_DupValue(ctx, value);
    return JS_UNDEFINED;
}

static JSValue js_stream_set(JSContext *ctx, JSValueConst this_val, JSValueConst value, int magic) {
    JSSoundStream *entry = stream_entry(ctx, this_val);
    AthenaSoundStream *stream;
    double position;

    if (!entry)
        return JS_EXCEPTION;
    stream = entry->stream;
    if (magic == STREAM_PROP_ON_END)
        return stream_set_callback(ctx, &entry->on_end, value, "Stream.onEnd");
    if (magic == STREAM_PROP_ON_LOOP)
        return stream_set_callback(ctx, &entry->on_loop, value, "Stream.onLoop");
    if (magic == STREAM_PROP_LOOP) {
        int loop = JS_ToBool(ctx, value);
        if (loop < 0)
            return JS_EXCEPTION;
        athena_sound_stream_set_loop(stream, loop != 0);
        return JS_UNDEFINED;
    }
    if (!JS_IsNumber(value))
        return sound_raise(ctx, SOUND_TYPE_ERROR, "INVALID_ARGUMENT", "Stream.position must be a number");
    if (JS_ToFloat64(ctx, &position, value))
        return JS_EXCEPTION;
    if (!isfinite(position))
        return sound_raise(ctx, SOUND_RANGE_ERROR, "INVALID_ARGUMENT", "Stream.position must be finite");
    /* Past either end clamps, so `position -= 5000` near the start is fine. */
    if (position < 0)
        position = 0;
    if (position > (double)athena_sound_stream_get_length(stream))
        position = (double)athena_sound_stream_get_length(stream);
    athena_sound_stream_set_position(stream, (uint32_t)position);
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry stream_proto[] = {
    JS_CFUNC_DEF("play", 0, js_stream_play),
    JS_CFUNC_DEF("pause", 0, js_stream_pause),
    JS_CFUNC_DEF("stop", 0, js_stream_stop),
    JS_CFUNC_DEF("playing", 0, js_stream_playing),
    JS_CFUNC_DEF("rewind", 0, js_stream_rewind),
    JS_CFUNC_DEF("free", 0, js_stream_free),
    JS_CGETSET_MAGIC_DEF("loop", js_stream_get, js_stream_set, STREAM_PROP_LOOP),
    JS_CGETSET_MAGIC_DEF("position", js_stream_get, js_stream_set, STREAM_PROP_POSITION),
    JS_CGETSET_MAGIC_DEF("onEnd", js_stream_get, js_stream_set, STREAM_PROP_ON_END),
    JS_CGETSET_MAGIC_DEF("onLoop", js_stream_get, js_stream_set, STREAM_PROP_ON_LOOP),
    JS_CGETSET_MAGIC_DEF("ended", js_stream_get, NULL, STREAM_PROP_ENDED),
    JS_CGETSET_MAGIC_DEF("length", js_stream_get, NULL, STREAM_PROP_LENGTH),
    JS_CGETSET_MAGIC_DEF("rate", js_stream_get, NULL, STREAM_PROP_RATE),
    JS_CGETSET_MAGIC_DEF("channels", js_stream_get, NULL, STREAM_PROP_CHANNELS),
    JS_CGETSET_MAGIC_DEF("format", js_stream_get, NULL, STREAM_PROP_FORMAT),
    JS_CGETSET_MAGIC_DEF("converted", js_stream_get, NULL, STREAM_PROP_CONVERTED),
};

/* --- Sfx ---------------------------------------------------------------- */

static void sfx_finalizer(JSRuntime *rt, JSValue value) {
    AthenaSfx *sfx = JS_GetOpaque(value, sfx_class_id);
    if (sfx) athena_sfx_destroy(sfx);
}

static JSClassDef sfx_class = {
    "Sfx",
    .finalizer = sfx_finalizer,
};

static AthenaSfx *sfx_this(JSContext *ctx, JSValueConst value) {
    AthenaSfx *sfx = JS_GetOpaque(value, sfx_class_id);

    /* Also NULL after free(). */
    if (!sfx)
        sound_raise(ctx, SOUND_TYPE_ERROR, "FREED", "not a Sound.Sfx, or it was freed");
    return sfx;
}

static JSValue js_sfx_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    AthenaSfx *sfx;
    const char *path;
    JSValue object;
    int result;

    if (!sound_argc(ctx, argc, 1, 1, "Sound.Sfx"))
        return JS_EXCEPTION;
    path = sound_path(ctx, argv[0], "Sound.Sfx");
    if (!path)
        return JS_EXCEPTION;
    sfx = athena_sfx_load(path, &result);
    if (!sfx) {
        object = sound_throw(ctx, "Sound.Sfx", result, path, true);
        JS_FreeCString(ctx, path);
        return object;
    }
    JS_FreeCString(ctx, path);

    object = sound_new_object(ctx, new_target, sfx_class_id);
    if (JS_IsException(object)) {
        athena_sfx_destroy(sfx);
        return object;
    }
    JS_SetOpaque(object, sfx);
    return object;
}

static JSValue js_sfx_play(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaSfx *sfx = sfx_this(ctx, this_val);
    int channel = -1;
    int result;

    if (!sfx || !sound_argc(ctx, argc, 0, 1, "Sfx.play"))
        return JS_EXCEPTION;
    if (argc > 0 && !JS_IsUndefined(argv[0]) &&
        !sound_int(ctx, argv[0], 0, ATHENA_SOUND_CHANNELS - 1, "Sfx.play channel", &channel))
        return JS_EXCEPTION;
    result = athena_sfx_play(sfx, channel);
    /* Below -1: the upload after an IOP reset may have failed, with detail. */
    if (result < -1)
        return sound_throw(ctx, "Sfx.play", result, NULL, true);
    return JS_NewInt32(ctx, result);
}

static JSValue js_sfx_playing(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaSfx *sfx = sfx_this(ctx, this_val);
    int channel, result;

    if (!sfx || !sound_argc(ctx, argc, 1, 1, "Sfx.playing") ||
        !sound_int(ctx, argv[0], 0, ATHENA_SOUND_CHANNELS - 1, "Sfx.playing channel", &channel))
        return JS_EXCEPTION;
    result = athena_sfx_is_playing(sfx, channel);
    if (result < 0)
        return sound_throw(ctx, "Sfx.playing", result, NULL, false);
    return JS_NewBool(ctx, result);
}

static JSValue js_sfx_stop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaSfx *sfx = sfx_this(ctx, this_val);
    int channel = -1;
    int result;

    if (!sfx || !sound_argc(ctx, argc, 0, 1, "Sfx.stop"))
        return JS_EXCEPTION;
    if (argc > 0 && !JS_IsUndefined(argv[0]) &&
        !sound_int(ctx, argv[0], 0, ATHENA_SOUND_CHANNELS - 1, "Sfx.stop channel", &channel))
        return JS_EXCEPTION;
    result = athena_sfx_stop(sfx, channel);
    if (result < 0)
        return sound_throw(ctx, "Sfx.stop", result, NULL, false);
    return JS_UNDEFINED;
}

static JSValue js_sfx_free(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaSfx *sfx = sfx_this(ctx, this_val);

    if (!sfx || !sound_argc(ctx, argc, 0, 0, "Sfx.free"))
        return JS_EXCEPTION;
    JS_SetOpaque((JSValue)this_val, NULL);
    athena_sfx_destroy(sfx);
    return JS_UNDEFINED;
}

enum {
    SFX_PROP_VOLUME,
    SFX_PROP_PAN,
    SFX_PROP_LOOP,
    SFX_PROP_PITCH,
    SFX_PROP_LENGTH,
    SFX_PROP_RATE,
};

static JSValue js_sfx_get(JSContext *ctx, JSValueConst this_val, int magic) {
    AthenaSfx *sfx = sfx_this(ctx, this_val);

    if (!sfx)
        return JS_EXCEPTION;
    switch (magic) {
    case SFX_PROP_VOLUME: return JS_NewInt32(ctx, athena_sfx_get_volume(sfx));
    case SFX_PROP_PAN: return JS_NewInt32(ctx, athena_sfx_get_pan(sfx));
    case SFX_PROP_LOOP: return JS_NewBool(ctx, athena_sfx_get_loop(sfx));
    /* The sample always plays at the pitch it was encoded with. */
    case SFX_PROP_PITCH: return JS_NewInt32(ctx, 0);
    case SFX_PROP_LENGTH: return JS_NewUint32(ctx, athena_sfx_get_length(sfx));
    default: return JS_NewInt32(ctx, athena_sfx_get_rate(sfx));
    }
}

static JSValue js_sfx_set(JSContext *ctx, JSValueConst this_val, JSValueConst value, int magic) {
    AthenaSfx *sfx = sfx_this(ctx, this_val);
    int number;

    if (!sfx)
        return JS_EXCEPTION;
    switch (magic) {
    case SFX_PROP_VOLUME:
        if (!sound_int(ctx, value, 0, ATHENA_SOUND_MAX_VOLUME, "Sfx.volume", &number))
            return JS_EXCEPTION;
        athena_sfx_set_volume(sfx, number);
        return JS_UNDEFINED;
    case SFX_PROP_PAN:
        if (!sound_int(ctx, value, ATHENA_SOUND_MIN_PAN, ATHENA_SOUND_MAX_PAN, "Sfx.pan", &number))
            return JS_EXCEPTION;
        athena_sfx_set_pan(sfx, number);
        return JS_UNDEFINED;
    case SFX_PROP_LOOP:
        return sound_raise(ctx, SOUND_TYPE_ERROR, "UNSUPPORTED",
            "Sfx.loop is read-only: looping is encoded in the .adp file (wav2adp -L)");
    default:
        return sound_raise(ctx, SOUND_TYPE_ERROR, "UNSUPPORTED",
            "Sfx.pitch is not supported: audsrv plays samples at their encoded rate");
    }
}

static const JSCFunctionListEntry sfx_proto[] = {
    JS_CFUNC_DEF("play", 1, js_sfx_play),
    JS_CFUNC_DEF("playing", 1, js_sfx_playing),
    JS_CFUNC_DEF("stop", 0, js_sfx_stop),
    JS_CFUNC_DEF("free", 0, js_sfx_free),
    JS_CGETSET_MAGIC_DEF("volume", js_sfx_get, js_sfx_set, SFX_PROP_VOLUME),
    JS_CGETSET_MAGIC_DEF("pan", js_sfx_get, js_sfx_set, SFX_PROP_PAN),
    JS_CGETSET_MAGIC_DEF("loop", js_sfx_get, js_sfx_set, SFX_PROP_LOOP),
    JS_CGETSET_MAGIC_DEF("pitch", js_sfx_get, js_sfx_set, SFX_PROP_PITCH),
    JS_CGETSET_MAGIC_DEF("length", js_sfx_get, NULL, SFX_PROP_LENGTH),
    JS_CGETSET_MAGIC_DEF("rate", js_sfx_get, NULL, SFX_PROP_RATE),
};

/* --- Loading sound effects on a worker ---------------------------------- */

/*
 * A Sound.loadSfxAsync() job, on the shared Job layer (athena/js/job.h). The
 * layer follows the read on the pool; settling uploads the sample on the
 * script thread through athena_sfx_job_poll().
 */
typedef struct {
    AthenaSfxJob *job;
    char *path;
} SfxJobInfo;

static int sfx_job_settle(JSContext *ctx, AthenaJob *read, AthenaJobState state, int unused,
    void *user, JSValue *outcome, bool *failed) {
    SfxJobInfo *info = user;
    AthenaSfxJobState sfx_state;
    AthenaSfx *sfx;
    int result;

    sfx_state = athena_sfx_job_poll(info->job, &sfx, &result);
    if (sfx) {
        *outcome = sound_new_object(ctx, JS_UNDEFINED, sfx_class_id);
        if (JS_IsException(*outcome)) {
            athena_sfx_destroy(sfx);
            *outcome = JS_UNDEFINED;
            return -1;
        }
        JS_SetOpaque(*outcome, sfx);
        return 0;
    }
    *failed = true;
    if (sfx_state == ATHENA_SFX_JOB_CANCELLED)
        sound_raise(ctx, SOUND_INTERNAL_ERROR, "CANCELLED", "Sound.loadSfxAsync: cancelled: %s",
            info->path ? info->path : "");
    else
        sound_throw(ctx, "Sound.loadSfxAsync", result, info->path, true);
    *outcome = JS_GetException(ctx);
    return 0;
}

static void sfx_job_free_info(JSRuntime *rt, void *user) {
    SfxJobInfo *info = user;

    athena_sfx_job_destroy(info->job);   /* releases the read job too */
    free(info->path);
    free(info);
}

/* The read job belongs to the AthenaSfxJob, released by sfx_job_free_info. */
static void sfx_job_release_none(AthenaJob *read) {
    (void)read;
}

/* Through the AthenaSfxJob, so a read already done is not uploaded either. */
static void sfx_job_cancel(AthenaJob *read, void *user) {
    athena_sfx_job_cancel(((SfxJobInfo *)user)->job);
}

static const AthenaJsJobKind sfx_job_kind = {
    "Sound", sfx_job_settle, NULL, sfx_job_free_info, sfx_job_release_none, sfx_job_cancel,
};

static JSValue js_sound_load_sfx_async(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    SfxJobInfo *info;
    AthenaSfxJob *job;
    const char *path;
    JSValue object;
    int result;

    if (!sound_argc(ctx, argc, 1, 1, "Sound.loadSfxAsync"))
        return JS_EXCEPTION;
    path = sound_path(ctx, argv[0], "Sound.loadSfxAsync");
    if (!path)
        return JS_EXCEPTION;
    info = calloc(1, sizeof(*info));
    job = info ? athena_sfx_load_async(path, &result) : NULL;
    if (!job) {
        object = info ? sound_throw(ctx, "Sound.loadSfxAsync", result, path, false) :
            JS_ThrowOutOfMemory(ctx);
        free(info);
        JS_FreeCString(ctx, path);
        return object;
    }
    info->job = job;
    info->path = strdup(path);
    JS_FreeCString(ctx, path);
    return athena_js_job_new(ctx, &sfx_job_kind, athena_sfx_job_core(job), info, "Sound.loadSfxAsync");
}

/* Module functions: the same as the job's own poll(), wait() and cancel(). */
static int sfx_job_arg(JSContext *ctx, int argc, JSValueConst *argv, int max, const char *name) {
    if (!sound_argc(ctx, argc, 1, max, name))
        return -1;
    if (!athena_js_job_is(argv[0], &sfx_job_kind)) {
        sound_raise(ctx, SOUND_TYPE_ERROR, "INVALID_ARGUMENT",
            "%s: not a job from Sound.loadSfxAsync()", name);
        return -1;
    }
    return 0;
}

static JSValue js_sound_poll(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (sfx_job_arg(ctx, argc, argv, 1, "Sound.poll") < 0)
        return JS_EXCEPTION;
    return athena_js_job_poll(ctx, argv[0], &sfx_job_kind, "Sound.poll");
}

static JSValue js_sound_wait(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    int timeout = -1;

    if (sfx_job_arg(ctx, argc, argv, 2, "Sound.wait") < 0)
        return JS_EXCEPTION;
    if (argc > 1 && !JS_IsUndefined(argv[1]) &&
        !sound_int(ctx, argv[1], 0, INT32_MAX, "Sound.wait timeoutMs", &timeout))
        return JS_EXCEPTION;
    return athena_js_job_wait(ctx, argv[0], argc - 1, argv + 1, &sfx_job_kind, "Sound.wait");
}

static JSValue js_sound_cancel(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (sfx_job_arg(ctx, argc, argv, 1, "Sound.cancel") < 0)
        return JS_EXCEPTION;
    return athena_js_job_cancel(ctx, argv[0], &sfx_job_kind, "Sound.cancel");
}

/* --- Module ------------------------------------------------------------- */

static const JSCFunctionListEntry sound_module_funcs[] = {
    JS_CFUNC_DEF("setVolume", 1, js_sound_set_volume),
    JS_CFUNC_DEF("getVolume", 0, js_sound_get_volume),
    JS_CFUNC_DEF("setSfxVolume", 1, js_sound_set_sfx_volume),
    JS_CFUNC_DEF("getSfxVolume", 0, js_sound_get_sfx_volume),
    JS_CFUNC_DEF("findChannel", 0, js_sound_find_channel),
    JS_CFUNC_DEF("getMemoryStats", 0, js_sound_get_memory_stats),
    JS_CFUNC_DEF("process", 0, js_sound_process),
    JS_CFUNC_DEF("loadSfxAsync", 1, js_sound_load_sfx_async),
    JS_CFUNC_DEF("poll", 1, js_sound_poll),
    JS_CFUNC_DEF("wait", 1, js_sound_wait),
    JS_CFUNC_DEF("cancel", 1, js_sound_cancel),
    JS_PROP_INT32_DEF("CHANNELS", ATHENA_SOUND_CHANNELS, JS_PROP_ENUMERABLE),
};

static int sound_define_class(JSContext *ctx, JSModuleDef *m, JSClassID *class_id,
    const JSClassDef *class_def, JSCFunction *ctor, const char *name,
    const JSCFunctionListEntry *proto_funcs, int proto_count) {
    JSValue proto, constructor;

    if (athena_register_class(ctx, class_id, class_def) < 0)
        return -1;
    proto = JS_NewObject(ctx);
    if (JS_IsException(proto))
        return -1;
    JS_SetPropertyFunctionList(ctx, proto, proto_funcs, proto_count);
    /* Callable with or without `new`, like the previous API: Sound.Stream(path). */
    constructor = JS_NewCFunction2(ctx, ctor, name, 1, JS_CFUNC_constructor_or_func, 0);
    if (JS_IsException(constructor)) {
        JS_FreeValue(ctx, proto);
        return -1;
    }
    JS_SetConstructor(ctx, constructor, proto);
    JS_SetClassProto(ctx, *class_id, proto);
    return JS_SetModuleExport(ctx, m, name, constructor);
}

static int sound_module_init(JSContext *ctx, JSModuleDef *m) {
    if (sound_define_class(ctx, m, &stream_class_id, &stream_class, js_stream_ctor, "Stream",
            stream_proto, countof(stream_proto)) < 0 ||
        sound_define_class(ctx, m, &sfx_class_id, &sfx_class, js_sfx_ctor, "Sfx",
            sfx_proto, countof(sfx_proto)) < 0)
        return -1;
    return JS_SetModuleExportList(ctx, m, sound_module_funcs, countof(sound_module_funcs));
}

JSModuleDef *athena_sound_init(JSContext *ctx) {
    JSModuleDef *m = athena_push_module(ctx, sound_module_init, sound_module_funcs,
        countof(sound_module_funcs), "Sound");

    if (!m)
        return NULL;
    JS_AddModuleExport(ctx, m, "Stream");
    JS_AddModuleExport(ctx, m, "Sfx");
    return m;
}
