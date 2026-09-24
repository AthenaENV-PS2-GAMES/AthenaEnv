#include <math.h>

#include <ath_env.h>
#include <athena/sound.h>

#include "ath_sound.h"

static JSClassID stream_class_id;
static JSClassID sfx_class_id;

static void stream_finalizer(JSRuntime *rt, JSValue value) {
    AthenaSoundStream *stream = JS_GetOpaque(value, stream_class_id);
    if (stream) athena_sound_stream_destroy(stream);
}

static void sfx_finalizer(JSRuntime *rt, JSValue value) {
    AthenaSfx *sfx = JS_GetOpaque(value, sfx_class_id);
    if (sfx) athena_sfx_destroy(sfx);
}

static JSClassDef stream_class = {
    "Stream",
    .finalizer = stream_finalizer,
};

static JSClassDef sfx_class = {
    "Sfx",
    .finalizer = sfx_finalizer,
};

static int sound_argc(JSContext *ctx, int argc, int minimum, int maximum, const char *name) {
    if (argc < minimum || argc > maximum) {
        if (minimum == maximum)
            JS_ThrowTypeError(ctx, "%s expects %d argument%s", name, minimum,
                minimum == 1 ? "" : "s");
        else
            JS_ThrowTypeError(ctx, "%s expects between %d and %d arguments", name,
                minimum, maximum);
        return 0;
    }
    return 1;
}

/* Integer in [minimum, maximum]; rejects NaN, fractions and non-numbers. */
static int sound_int(JSContext *ctx, JSValueConst value, int minimum, int maximum,
    const char *name, int *out) {
    double number;

    if (!JS_IsNumber(value)) {
        JS_ThrowTypeError(ctx, "%s must be a number", name);
        return 0;
    }
    if (JS_ToFloat64(ctx, &number, value))
        return 0;
    if (!isfinite(number) || floor(number) != number || number < minimum || number > maximum) {
        JS_ThrowRangeError(ctx, "%s must be an integer between %d and %d", name, minimum, maximum);
        return 0;
    }
    *out = (int)number;
    return 1;
}

static JSValue sound_throw(JSContext *ctx, const char *name, int result, const char *path) {
    if (result == ATHENA_SOUND_ERR_ARGS)
        return JS_ThrowRangeError(ctx, "%s: %s", name, athena_sound_result_string(result));
    if (path)
        return JS_ThrowInternalError(ctx, "%s: %s: %s", name,
            athena_sound_result_string(result), path);
    return JS_ThrowInternalError(ctx, "%s: %s", name, athena_sound_result_string(result));
}

static const char *sound_path(JSContext *ctx, JSValueConst value, const char *name) {
    if (!JS_IsString(value)) {
        JS_ThrowTypeError(ctx, "%s: path must be a string", name);
        return NULL;
    }
    return JS_ToCString(ctx, value);
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
        return sound_throw(ctx, "Sound.setVolume", result, NULL);
    return JS_UNDEFINED;
}

static JSValue js_sound_get_volume(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (!sound_argc(ctx, argc, 0, 0, "Sound.getVolume"))
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, athena_sound_get_volume());
}

static JSValue js_sound_find_channel(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    int result;

    if (!sound_argc(ctx, argc, 0, 0, "Sound.findChannel"))
        return JS_EXCEPTION;
    result = athena_sound_ensure();
    if (result < 0)
        return sound_throw(ctx, "Sound.findChannel", result, NULL);
    return JS_NewInt32(ctx, athena_sfx_find_channel());
}

/* --- Stream ------------------------------------------------------------- */

static AthenaSoundStream *stream_this(JSContext *ctx, JSValueConst value) {
    AthenaSoundStream *stream = JS_GetOpaque(value, stream_class_id);

    /* Also NULL after free(). */
    if (!stream)
        JS_ThrowTypeError(ctx, "not a Sound.Stream, or it was freed");
    return stream;
}

static JSValue js_stream_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    AthenaSoundStream *stream;
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
        object = sound_throw(ctx, "Sound.Stream", result, path);
        JS_FreeCString(ctx, path);
        return object;
    }
    JS_FreeCString(ctx, path);

    object = sound_new_object(ctx, new_target, stream_class_id);
    if (JS_IsException(object)) {
        athena_sound_stream_destroy(stream);
        return object;
    }
    JS_SetOpaque(object, stream);
    return object;
}

static JSValue js_stream_play(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaSoundStream *stream = stream_this(ctx, this_val);
    int result;

    if (!stream || !sound_argc(ctx, argc, 0, 0, "Stream.play"))
        return JS_EXCEPTION;
    result = athena_sound_stream_play(stream);
    if (result < 0)
        return sound_throw(ctx, "Stream.play", result, NULL);
    return JS_UNDEFINED;
}

static JSValue js_stream_pause(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaSoundStream *stream = stream_this(ctx, this_val);

    if (!stream || !sound_argc(ctx, argc, 0, 0, "Stream.pause"))
        return JS_EXCEPTION;
    athena_sound_stream_pause(stream);
    return JS_UNDEFINED;
}

static JSValue js_stream_stop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaSoundStream *stream = stream_this(ctx, this_val);

    if (!stream || !sound_argc(ctx, argc, 0, 0, "Stream.stop"))
        return JS_EXCEPTION;
    athena_sound_stream_pause(stream);
    athena_sound_stream_rewind(stream);
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
    AthenaSoundStream *stream = stream_this(ctx, this_val);

    if (!stream || !sound_argc(ctx, argc, 0, 0, "Stream.free"))
        return JS_EXCEPTION;
    /* Cleared first so the finalizer does not free it again. */
    JS_SetOpaque((JSValue)this_val, NULL);
    athena_sound_stream_destroy(stream);
    return JS_UNDEFINED;
}

enum {
    STREAM_PROP_LOOP,
    STREAM_PROP_POSITION,
    STREAM_PROP_LENGTH,
    STREAM_PROP_RATE,
    STREAM_PROP_CHANNELS,
    STREAM_PROP_FORMAT,
};

static JSValue js_stream_get(JSContext *ctx, JSValueConst this_val, int magic) {
    AthenaSoundStream *stream = stream_this(ctx, this_val);

    if (!stream)
        return JS_EXCEPTION;
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
    default:
        return JS_NewString(ctx,
            athena_sound_stream_get_type(stream) == ATHENA_SOUND_STREAM_OGG ? "ogg" : "wav");
    }
}

static JSValue js_stream_set(JSContext *ctx, JSValueConst this_val, JSValueConst value, int magic) {
    AthenaSoundStream *stream = stream_this(ctx, this_val);
    double position;

    if (!stream)
        return JS_EXCEPTION;
    if (magic == STREAM_PROP_LOOP) {
        int loop = JS_ToBool(ctx, value);
        if (loop < 0)
            return JS_EXCEPTION;
        athena_sound_stream_set_loop(stream, loop != 0);
        return JS_UNDEFINED;
    }
    if (!JS_IsNumber(value))
        return JS_ThrowTypeError(ctx, "Stream.position must be a number");
    if (JS_ToFloat64(ctx, &position, value))
        return JS_EXCEPTION;
    if (!isfinite(position))
        return JS_ThrowRangeError(ctx, "Stream.position must be finite");
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
    JS_CGETSET_MAGIC_DEF("length", js_stream_get, NULL, STREAM_PROP_LENGTH),
    JS_CGETSET_MAGIC_DEF("rate", js_stream_get, NULL, STREAM_PROP_RATE),
    JS_CGETSET_MAGIC_DEF("channels", js_stream_get, NULL, STREAM_PROP_CHANNELS),
    JS_CGETSET_MAGIC_DEF("format", js_stream_get, NULL, STREAM_PROP_FORMAT),
};

/* --- Sfx ---------------------------------------------------------------- */

static AthenaSfx *sfx_this(JSContext *ctx, JSValueConst value) {
    AthenaSfx *sfx = JS_GetOpaque(value, sfx_class_id);

    /* Also NULL after free(). */
    if (!sfx)
        JS_ThrowTypeError(ctx, "not a Sound.Sfx, or it was freed");
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
        object = sound_throw(ctx, "Sound.Sfx", result, path);
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
    if (result < -1)
        return sound_throw(ctx, "Sfx.play", result, NULL);
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
        return sound_throw(ctx, "Sfx.playing", result, NULL);
    return JS_NewBool(ctx, result);
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
        return JS_ThrowTypeError(ctx,
            "Sfx.loop is read-only: looping is encoded in the .adp file (adpenc -L)");
    default:
        return JS_ThrowTypeError(ctx,
            "Sfx.pitch is not supported: audsrv plays samples at their encoded rate");
    }
}

static const JSCFunctionListEntry sfx_proto[] = {
    JS_CFUNC_DEF("play", 1, js_sfx_play),
    JS_CFUNC_DEF("playing", 1, js_sfx_playing),
    JS_CFUNC_DEF("free", 0, js_sfx_free),
    JS_CGETSET_MAGIC_DEF("volume", js_sfx_get, js_sfx_set, SFX_PROP_VOLUME),
    JS_CGETSET_MAGIC_DEF("pan", js_sfx_get, js_sfx_set, SFX_PROP_PAN),
    JS_CGETSET_MAGIC_DEF("loop", js_sfx_get, js_sfx_set, SFX_PROP_LOOP),
    JS_CGETSET_MAGIC_DEF("pitch", js_sfx_get, js_sfx_set, SFX_PROP_PITCH),
    JS_CGETSET_MAGIC_DEF("length", js_sfx_get, NULL, SFX_PROP_LENGTH),
    JS_CGETSET_MAGIC_DEF("rate", js_sfx_get, NULL, SFX_PROP_RATE),
};

/* --- Module ------------------------------------------------------------- */

static const JSCFunctionListEntry sound_module_funcs[] = {
    JS_CFUNC_DEF("setVolume", 1, js_sound_set_volume),
    JS_CFUNC_DEF("getVolume", 0, js_sound_get_volume),
    JS_CFUNC_DEF("findChannel", 0, js_sound_find_channel),
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
