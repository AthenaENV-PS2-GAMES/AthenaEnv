#include <athena/math.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include <ath_env.h>

#include <athena/job.h>
#include <athena/js/job.h>
#include <athena/noise.h>
#include <athena/random.h>
#include "ath_noise.h"

/*
 * Noise.Generator wraps the tables of one seed (AthenaNoise). The module
 * functions (Noise.perlin2, Noise.seed...) call the same methods on a
 * Generator of their own context, seed 0, bound to them as function data:
 * workers and scripts run after std.reload() each start from seed 0, and the
 * tables are freed with the context.
 */

/* Beyond this, the lattice cell (an int) of a coordinate could overflow. */
#define NOISE_MAX_COORD 1.0e9f

/* Thresholds of toTiles() read without malloc. */
#define NOISE_STACK_LEVELS 32

static JSClassID noise_class_id;

static void noise_finalizer(JSRuntime *rt, JSValue value) {
    js_free_rt(rt, JS_GetOpaque(value, noise_class_id));
}

static JSClassDef noise_class = {
    "Generator",
    .finalizer = noise_finalizer,
};

static AthenaNoise *noise_this(JSContext *ctx, JSValueConst this_val) {
    return JS_GetOpaque2(ctx, this_val, noise_class_id);
}

/*
 * Seeds from a number (integers up to 2^53 map one-to-one, other numbers by
 * their bits), a string (hashed) or undefined (the clock).
 */
static int noise_seed_value(JSContext *ctx, JSValueConst value, uint64_t *seed,
    const char *name) {
    if (JS_IsUndefined(value)) {
        *seed = athena_random_entropy();
        return 0;
    }
    if (JS_IsString(value)) {
        size_t length;
        const char *text = JS_ToCStringLen(ctx, &length, value);
        if (!text)
            return -1;
        *seed = athena_random_hash(text, length);
        JS_FreeCString(ctx, text);
        return 0;
    }
    if (JS_IsNumber(value)) {
        double number;
        if (JS_ToFloat64(ctx, &number, value))
            return -1;
        if (!isfinite(number)) {
            JS_ThrowRangeError(ctx, "%s seed must be finite", name);
            return -1;
        }
        if (number == trunc(number) && fabs(number) <= 9007199254740992.0)
            *seed = (uint64_t)(int64_t)number;
        else
            memcpy(seed, &number, sizeof(*seed));
        return 0;
    }
    JS_ThrowTypeError(ctx, "%s seed must be a number or a string", name);
    return -1;
}

/*
 * A number within +-limit into a float: 0, 1 when out of range or not finite,
 * -1 on error. The EE's FPU has no infinities nor NaN (comparisons take them
 * for large numbers), so int and float32 values are checked by their bits and
 * other numbers as doubles, which are soft-float (IEEE, but slow).
 */
static int noise_to_float(JSContext *ctx, JSValueConst value, float *out,
    float limit) {
    double number;
    uint32_t bits;

    switch (JS_VALUE_GET_TAG(value)) {
    case JS_TAG_INT:
        *out = (float)JS_VALUE_GET_INT(value);
        return fabsf(*out) <= limit ? 0 : 1;
    case JS_CUSTOM_TAG_FLOAT32:
        *out = JS_VALUE_GET_FLOAT32(value);
        memcpy(&bits, out, sizeof(bits));
        if ((bits & 0x7F800000u) == 0x7F800000u)
            return 1;
        return fabsf(*out) <= limit ? 0 : 1;
    }
    if (JS_ToFloat64(ctx, &number, value))
        return -1;
    if (!(fabs(number) <= (double)limit))
        return 1;
    *out = (float)number;
    return 0;
}

static int noise_coords(JSContext *ctx, int argc, JSValueConst *argv,
    float *out, int count, const char *name) {
    if (argc < count) {
        JS_ThrowTypeError(ctx, "%s expects %d coordinates", name, count);
        return -1;
    }
    for (int i = 0; i < count; i++) {
        int result = noise_to_float(ctx, argv[i], &out[i], NOISE_MAX_COORD);
        if (result < 0)
            return -1;
        if (result > 0) {
            JS_ThrowRangeError(ctx, "%s coordinates must be finite and within +-1e9",
                name);
            return -1;
        }
    }
    return 0;
}

/* Optional property: 0 when absent (`*out` untouched), 1 when read, -1 on error. */
static int noise_option(JSContext *ctx, JSValueConst options, const char *key,
    JSValue *out) {
    JSValue value;

    if (JS_IsUndefined(options))
        return 0;
    value = JS_GetPropertyStr(ctx, options, key);
    if (JS_IsException(value))
        return -1;
    if (JS_IsUndefined(value))
        return 0;
    *out = value;
    return 1;
}

static int noise_option_float(JSContext *ctx, JSValueConst options,
    const char *key, float *out, const char *name) {
    JSValue value;
    int found = noise_option(ctx, options, key, &value);

    if (found <= 0)
        return found;
    found = noise_to_float(ctx, value, out, ATHENA_FLOAT_MAX);
    JS_FreeValue(ctx, value);
    if (found < 0)
        return -1;
    if (found > 0) {
        JS_ThrowRangeError(ctx, "%s option %s must be finite", name, key);
        return -1;
    }
    return 1;
}

/*
 * Optional string option, matched against `names` (NULL-terminated): its
 * index in `*out`. Other strings throw a RangeError listing `expected`.
 */
static int noise_option_enum(JSContext *ctx, JSValueConst options,
    const char *key, const char *const *names, int *out, const char *expected,
    const char *name) {
    JSValue value;
    const char *text;
    int found = noise_option(ctx, options, key, &value);

    if (found <= 0)
        return found;
    text = JS_ToCString(ctx, value);
    JS_FreeValue(ctx, value);
    if (!text)
        return -1;
    for (int i = 0; names[i]; i++) {
        if (!strcmp(text, names[i])) {
            JS_FreeCString(ctx, text);
            *out = i;
            return 1;
        }
    }
    JS_FreeCString(ctx, text);
    JS_ThrowRangeError(ctx, "%s %s must be %s", name, key, expected);
    return -1;
}

/* In the order of AthenaNoiseType and AthenaNoiseMode. */
static const char *const noise_types[] = { "perlin", "simplex", "worley", NULL };
static const char *const noise_modes[] = { "fbm", "ridged", "billow", NULL };

/* Reads the fractal options over the defaults; `options` may be undefined. */
static int noise_fractal(JSContext *ctx, JSValueConst options,
    AthenaNoiseFractal *fractal, const char *name) {
    JSValue value;
    int found, type = ATHENA_NOISE_SIMPLEX, mode = ATHENA_NOISE_FBM;

    athena_noise_fractal_default(fractal);
    if (!JS_IsUndefined(options) && !JS_IsObject(options)) {
        JS_ThrowTypeError(ctx, "%s options must be an object", name);
        return -1;
    }
    if (noise_option_enum(ctx, options, "type", noise_types, &type,
            "\"perlin\", \"simplex\" or \"worley\"", name) < 0 ||
        noise_option_enum(ctx, options, "mode", noise_modes, &mode,
            "\"fbm\", \"ridged\" or \"billow\"", name) < 0)
        return -1;
    fractal->type = (AthenaNoiseType)type;
    fractal->mode = (AthenaNoiseMode)mode;
    if (fractal->type == ATHENA_NOISE_WORLEY && fractal->mode != ATHENA_NOISE_FBM) {
        JS_ThrowRangeError(ctx, "%s mode \"%s\" needs a signed noise (perlin or simplex)",
            name, noise_modes[mode]);
        return -1;
    }
    found = noise_option(ctx, options, "octaves", &value);
    if (found < 0)
        return -1;
    if (found) {
        int32_t octaves;
        int failed = JS_ToInt32(ctx, &octaves, value);
        JS_FreeValue(ctx, value);
        if (failed)
            return -1;
        if (octaves < 1 || octaves > ATHENA_NOISE_MAX_OCTAVES) {
            JS_ThrowRangeError(ctx, "%s octaves must be from 1 to %d", name,
                ATHENA_NOISE_MAX_OCTAVES);
            return -1;
        }
        fractal->octaves = octaves;
    }
    if (noise_option_float(ctx, options, "lacunarity", &fractal->lacunarity, name) < 0 ||
        noise_option_float(ctx, options, "gain", &fractal->gain, name) < 0 ||
        noise_option_float(ctx, options, "warp", &fractal->warp, name) < 0)
        return -1;
    return 0;
}

/*
 * Whether |coord| (in noise units, before the warp) stays below 1e9 through
 * the warp and every octave: the same float products as the fractal sum, so
 * no pow() nor double math (soft-float on the EE) per sample. Each bound is
 * below 1e9 before the next product, so nothing overflows.
 */
static int noise_fractal_fits(const AthenaNoiseFractal *fractal, float coord) {
    float lacunarity = fabsf(fractal->lacunarity);

    /* The warp moves a point by less than |warp| * 1.01 (simplex <= 1). */
    coord = fabsf(coord) + fabsf(fractal->warp) * 1.01f;
    if (!(coord < NOISE_MAX_COORD))
        return 0;
    for (int octave = 1; octave < fractal->octaves; octave++) {
        coord *= lacunarity;
        if (!(coord < NOISE_MAX_COORD))
            return 0;
    }
    return 1;
}

static JSValue noise_throw_reach(JSContext *ctx, const char *name) {
    return JS_ThrowRangeError(ctx,
        "%s: coordinates of the last octave (with the warp) exceed +-1e9", name);
}

/*
 * Elements of a typed array of `type`, named `ctor` in errors. Runs no
 * JavaScript (no instanceof, no getters), so a pointer taken here stays valid
 * until the next call that can: read every other argument first, and take
 * every pointer of a call in a row.
 */
static void *noise_typed_array(JSContext *ctx, JSValueConst value,
    JSTypedArrayEnum type, const char *ctor, size_t *count, const char *name) {
    size_t offset = 0, byte_length = 0, element_size = 0, buffer_length = 0;
    JSValue buffer;
    uint8_t *data;

    if (JS_GetTypedArrayType(value) != (int)type) {
        JS_ThrowTypeError(ctx, "%s must be a %s", name, ctor);
        return NULL;
    }
    buffer = JS_GetTypedArrayBuffer(ctx, value, &offset, &byte_length,
        &element_size);
    if (JS_IsException(buffer))
        return NULL;
    data = JS_GetArrayBuffer(ctx, &buffer_length, buffer);
    JS_FreeValue(ctx, buffer);
    if (!data)
        return NULL;
    if (offset + byte_length > buffer_length) {
        JS_ThrowRangeError(ctx, "%s is out of bounds of its buffer", name);
        return NULL;
    }
    *count = byte_length / element_size;
    return data + offset;
}

/* Finite numbers of an array or a typed array into floats. */
static int noise_floats(JSContext *ctx, JSValueConst array, float *out,
    uint32_t length, const char *name) {
    for (uint32_t i = 0; i < length; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, array, i);
        int failed = JS_IsException(item) ? -1 :
            noise_to_float(ctx, item, &out[i], ATHENA_FLOAT_MAX);
        JS_FreeValue(ctx, item);
        if (failed > 0)
            JS_ThrowRangeError(ctx, "%s must be finite numbers", name);
        if (failed)
            return -1;
    }
    return 0;
}

static int noise_length(JSContext *ctx, JSValueConst value, uint32_t *length,
    const char *name) {
    JSValue property;
    int failed;

    if (!JS_IsObject(value)) {
        JS_ThrowTypeError(ctx, "%s must be an array", name);
        return -1;
    }
    property = JS_GetPropertyStr(ctx, value, "length");
    if (JS_IsException(property))
        return -1;
    failed = JS_ToUint32(ctx, length, property);
    JS_FreeValue(ctx, property);
    return failed ? -1 : 0;
}

static JSValue noise_new(JSContext *ctx, JSValueConst new_target,
    uint64_t seed) {
    JSValue proto = JS_UNDEFINED, object;
    AthenaNoise *noise;

    if (!JS_IsUndefined(new_target)) {
        proto = JS_GetPropertyStr(ctx, new_target, "prototype");
        if (JS_IsException(proto))
            return JS_EXCEPTION;
        object = JS_NewObjectProtoClass(ctx, proto, noise_class_id);
        JS_FreeValue(ctx, proto);
    } else {
        object = JS_NewObjectClass(ctx, noise_class_id);
    }
    if (JS_IsException(object))
        return object;
    noise = js_malloc(ctx, sizeof(*noise));
    if (!noise) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    athena_noise_seed(noise, seed);
    JS_SetOpaque(object, noise);
    return object;
}

static JSValue noise_ctor(JSContext *ctx, JSValueConst new_target, int argc,
    JSValueConst *argv) {
    uint64_t seed;

    if (noise_seed_value(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &seed,
            "Noise.Generator") < 0)
        return JS_EXCEPTION;
    return noise_new(ctx, new_target, seed);
}

static JSValue noise_seed(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int magic) {
    AthenaNoise *noise = noise_this(ctx, this_val);
    uint64_t seed;

    if (!noise || noise_seed_value(ctx, argc > 0 ? argv[0] : JS_UNDEFINED,
            &seed, "Noise.seed") < 0)
        return JS_EXCEPTION;
    athena_noise_seed(noise, seed);
    return JS_UNDEFINED;
}

#define NOISE_SAMPLE_FUNCTION(name, dims, js_name, ...) \
static JSValue noise_##name(JSContext *ctx, JSValueConst this_val, int argc, \
    JSValueConst *argv, int magic) { \
    AthenaNoise *noise = noise_this(ctx, this_val); \
    float c[3]; \
    if (!noise || noise_coords(ctx, argc, argv, c, dims, js_name) < 0) \
        return JS_EXCEPTION; \
    return JS_NewFloat32(ctx, athena_noise_##name(noise, __VA_ARGS__)); \
}

NOISE_SAMPLE_FUNCTION(perlin2, 2, "Noise.perlin2", c[0], c[1])
NOISE_SAMPLE_FUNCTION(perlin3, 3, "Noise.perlin3", c[0], c[1], c[2])
NOISE_SAMPLE_FUNCTION(simplex2, 2, "Noise.simplex2", c[0], c[1])
NOISE_SAMPLE_FUNCTION(simplex3, 3, "Noise.simplex3", c[0], c[1], c[2])
NOISE_SAMPLE_FUNCTION(worley2, 2, "Noise.worley2", c[0], c[1])
NOISE_SAMPLE_FUNCTION(worley3, 3, "Noise.worley3", c[0], c[1], c[2])

/* fbm2(x, y, options?) and fbm3(x, y, z, options?); magic = dimensions. */
static JSValue noise_fbm(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int dims) {
    const char *name = dims == 2 ? "Noise.fbm2" : "Noise.fbm3";
    AthenaNoise *noise = noise_this(ctx, this_val);
    AthenaNoiseFractal fractal;
    float c[3];

    if (!noise || noise_coords(ctx, argc, argv, c, dims, name) < 0 ||
        noise_fractal(ctx, argc > dims ? argv[dims] : JS_UNDEFINED, &fractal,
            name) < 0)
        return JS_EXCEPTION;
    for (int i = 0; i < dims; i++)
        if (!noise_fractal_fits(&fractal, c[i]))
            return noise_throw_reach(ctx, name);
    return JS_NewFloat32(ctx, dims == 2 ?
        athena_noise_fbm2(noise, &fractal, c[0], c[1]) :
        athena_noise_fbm3(noise, &fractal, c[0], c[1], c[2]));
}

/*
 * Arguments of fill() and fillAsync(): the options into `fill`, the size,
 * and the tables to sample, the generator's or `*own` (built for the `seed`
 * option; js_free() it). Runs getters: take no buffer pointer before it.
 */
static int noise_fill_args(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, const char *name, AthenaNoiseFill *fill,
    uint32_t *width, uint32_t *height, const AthenaNoise **noise,
    AthenaNoise **own) {
    JSValueConst options = argc > 3 ? argv[3] : JS_UNDEFINED;
    JSValue value;
    int found;

    *own = NULL;
    *noise = noise_this(ctx, this_val);
    if (!*noise)
        return -1;
    if (argc < 3) {
        JS_ThrowTypeError(ctx, "%s expects an array, a width and a height", name);
        return -1;
    }
    if (JS_ToUint32(ctx, width, argv[1]) || JS_ToUint32(ctx, height, argv[2]))
        return -1;
    memset(fill, 0, sizeof(*fill));
    fill->scale = 1.0f / 16.0f;
    fill->max = 1.0f;
    if (noise_fractal(ctx, options, &fill->fractal, name) < 0 ||
        noise_option_float(ctx, options, "scale", &fill->scale, name) < 0 ||
        noise_option_float(ctx, options, "x", &fill->x, name) < 0 ||
        noise_option_float(ctx, options, "y", &fill->y, name) < 0 ||
        noise_option_float(ctx, options, "min", &fill->min, name) < 0 ||
        noise_option_float(ctx, options, "max", &fill->max, name) < 0)
        return -1;
    found = noise_option_float(ctx, options, "z", &fill->z, name);
    if (found < 0)
        return -1;
    fill->use_z = found;
    found = noise_option(ctx, options, "normalize", &value);
    if (found < 0)
        return -1;
    if (found) {
        fill->normalize = JS_ToBool(ctx, value);
        JS_FreeValue(ctx, value);
    }
    /*
     * The farthest cell of each axis, in noise units. Checked as doubles
     * (once per fill): x + width can exceed what a float holds exactly.
     */
    {
        double scale = fabs((double)fill->scale);
        double far_x = (fabs((double)fill->x) + *width) * scale;
        double far_y = (fabs((double)fill->y) + *height) * scale;
        double far_z = fabs((double)fill->z) * scale;
        if (!(far_x < NOISE_MAX_COORD && far_y < NOISE_MAX_COORD &&
                far_z < NOISE_MAX_COORD) ||
            !noise_fractal_fits(&fill->fractal, (float)far_x) ||
            !noise_fractal_fits(&fill->fractal, (float)far_y) ||
            !noise_fractal_fits(&fill->fractal, (float)far_z)) {
            noise_throw_reach(ctx, name);
            return -1;
        }
    }
    found = noise_option(ctx, options, "seed", &value);
    if (found < 0)
        return -1;
    if (found) {
        uint64_t seed;
        int failed = noise_seed_value(ctx, value, &seed, name);
        JS_FreeValue(ctx, value);
        if (failed)
            return -1;
        *own = js_malloc(ctx, sizeof(**own));
        if (!*own)
            return -1;
        athena_noise_seed(*own, seed);
        *noise = *own;
    }
    return 0;
}

/* The output array of a fill, checked to hold width * height values. */
static float *noise_fill_output(JSContext *ctx, JSValueConst array,
    uint32_t width, uint32_t height, const char *name) {
    size_t count;
    float *out = noise_typed_array(ctx, array, JS_TYPED_ARRAY_FLOAT32,
        "Float32Array", &count, name);

    if (out && (uint64_t)width * height > count) {
        JS_ThrowRangeError(ctx, "%s: %u x %u values do not fit in %u", name,
            (unsigned int)width, (unsigned int)height, (unsigned int)count);
        return NULL;
    }
    return out;
}

static JSValue noise_fill(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int magic) {
    const AthenaNoise *noise;
    AthenaNoise *own;
    AthenaNoiseFill fill;
    uint32_t width, height;
    float *out;

    if (noise_fill_args(ctx, this_val, argc, argv, "Noise.fill", &fill, &width,
            &height, &noise, &own) < 0) {
        js_free(ctx, own);
        return JS_EXCEPTION;
    }
    out = noise_fill_output(ctx, argv[0], width, height, "Noise.fill");
    if (out)
        athena_noise_fill(noise, &fill, out, width, height);
    js_free(ctx, own);
    return out ? JS_DupValue(ctx, argv[0]) : JS_EXCEPTION;
}

/*
 * fillAsync: the worker fills a buffer of its own; settle() copies it into
 * the array on the script thread, after checking the array still holds the
 * grid. The Job keeps the array alive meanwhile.
 */
typedef struct {
    JSValue array;
    uint32_t width, height;
} NoiseFillUser;

static int noise_job_settle(JSContext *ctx, AthenaJob *job,
    AthenaJobState state, int result, void *user, JSValue *outcome,
    bool *failed) {
    NoiseFillUser *fill = user;
    float *grid, *out;

    if (state != ATHENA_JOB_DONE) {
        *failed = true;
        *outcome = JS_NewError(ctx);
        if (JS_IsException(*outcome))
            return -1;
        JS_DefinePropertyValueStr(ctx, *outcome, "message", JS_NewString(ctx,
            state == ATHENA_JOB_CANCELLED ? "Noise.fillAsync: cancelled" :
            "Noise.fillAsync: failed"), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
        return 0;
    }
    grid = athena_noise_fill_job_take(job);
    if (!grid) {
        JS_ThrowInternalError(ctx, "Noise.fillAsync: the grid was already taken");
        return -1;
    }
    out = noise_fill_output(ctx, fill->array, fill->width, fill->height,
        "Noise.fillAsync array");
    if (out)
        memcpy(out, grid, (size_t)fill->width * fill->height * sizeof(float));
    free(grid);
    if (!out)
        return -1;
    *outcome = JS_DupValue(ctx, fill->array);
    return 0;
}

static void noise_job_status(JSContext *ctx, AthenaJob *job, void *user,
    JSValue object) {
    NoiseFillUser *fill = user;
    size_t rows = athena_noise_fill_job_rows(job);

    JS_DefinePropertyValueStr(ctx, object, "rowsDone",
        JS_NewUint32(ctx, (uint32_t)rows), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, object, "rows",
        JS_NewUint32(ctx, fill->height), JS_PROP_C_W_E);
}

static void noise_job_free_user(JSRuntime *rt, void *user) {
    NoiseFillUser *fill = user;

    JS_FreeValueRT(rt, fill->array);
    free(fill);
}

static const AthenaJsJobKind noise_job_kind = {
    "Noise", noise_job_settle, noise_job_status, noise_job_free_user,
};

static JSValue noise_fill_async(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic) {
    const char *name = "Noise.fillAsync";
    const AthenaNoise *noise;
    AthenaNoise *own;
    AthenaNoiseFill fill;
    uint32_t width, height;
    NoiseFillUser *user;
    AthenaJob *job;

    if (noise_fill_args(ctx, this_val, argc, argv, name, &fill, &width,
            &height, &noise, &own) < 0 ||
        !noise_fill_output(ctx, argv[0], width, height, name)) {
        js_free(ctx, own);
        return JS_EXCEPTION;
    }
    user = malloc(sizeof(*user));
    if (!user) {
        js_free(ctx, own);
        return JS_ThrowOutOfMemory(ctx);
    }
    user->array = JS_DupValue(ctx, argv[0]);
    user->width = width;
    user->height = height;
    /* The job copies the tables and options. */
    job = athena_noise_fill_submit(noise, &fill, width, height);
    js_free(ctx, own);
    return athena_js_job_new(ctx, &noise_job_kind, job, user, name);
}

static JSValue noise_to_tiles(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    float stack[NOISE_STACK_LEVELS], *thresholds;
    uint16_t stack_tiles[NOISE_STACK_LEVELS + 1], *tiles;
    uint32_t levels, tile_count, i;
    size_t out_count, count;
    const float *values;
    uint16_t *out;
    int failed = 0;

    if (argc < 4)
        return JS_ThrowTypeError(ctx, "Noise.toTiles expects out, values, thresholds and tiles");
    if (noise_length(ctx, argv[2], &levels, "Noise.toTiles thresholds") < 0 ||
        noise_length(ctx, argv[3], &tile_count, "Noise.toTiles tiles") < 0)
        return JS_EXCEPTION;
    if (tile_count != levels + 1)
        return JS_ThrowRangeError(ctx, "Noise.toTiles needs one tile more than thresholds (%u thresholds, %u tiles)",
            (unsigned int)levels, (unsigned int)tile_count);
    if (levels > 0xFFFF)
        return JS_ThrowRangeError(ctx, "Noise.toTiles has too many thresholds");
    thresholds = levels <= NOISE_STACK_LEVELS ? stack :
        js_malloc(ctx, levels * sizeof(*thresholds));
    tiles = levels <= NOISE_STACK_LEVELS ? stack_tiles :
        js_malloc(ctx, tile_count * sizeof(*tiles));
    if (!thresholds || !tiles)
        failed = 1;
    if (!failed)
        failed = noise_floats(ctx, argv[2], thresholds, levels,
            "Noise.toTiles thresholds") < 0;
    for (i = 1; !failed && i < levels; i++) {
        if (!(thresholds[i] >= thresholds[i - 1])) {
            JS_ThrowRangeError(ctx, "Noise.toTiles thresholds must be ascending");
            failed = 1;
        }
    }
    for (i = 0; !failed && i < tile_count; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, argv[3], i);
        double id = 0;
        failed = JS_IsException(item) || JS_ToFloat64(ctx, &id, item);
        JS_FreeValue(ctx, item);
        if (!failed && (id != trunc(id) || id < 0 || id > 65535)) {
            JS_ThrowRangeError(ctx, "Noise.toTiles tiles must be integers from 0 to 65535");
            failed = 1;
        }
        if (!failed)
            tiles[i] = (uint16_t)id;
    }
    if (!failed) {
        /* Both pointers in a row: nothing between them runs JavaScript. */
        out = noise_typed_array(ctx, argv[0], JS_TYPED_ARRAY_UINT16,
            "Uint16Array", &out_count, "Noise.toTiles out");
        values = out ? noise_typed_array(ctx, argv[1], JS_TYPED_ARRAY_FLOAT32,
            "Float32Array", &count, "Noise.toTiles values") : NULL;
        if (values && out_count < count) {
            JS_ThrowRangeError(ctx, "Noise.toTiles: out holds %u of %u values",
                (unsigned int)out_count, (unsigned int)count);
            values = NULL;
        }
        if (values)
            athena_noise_to_tiles(out, values, count, thresholds, tiles, levels);
        else
            failed = 1;
    }
    if (thresholds != stack)
        js_free(ctx, thresholds);
    if (tiles != stack_tiles)
        js_free(ctx, tiles);
    return failed ? JS_EXCEPTION : JS_DupValue(ctx, argv[0]);
}

/* Methods of Noise.Generator, also exported as module functions. */
static const struct {
    const char *name;
    int length;
    JSCFunctionMagic *func;
    int magic;
} noise_methods[] = {
    { "seed", 1, noise_seed, 0 },
    { "perlin2", 2, noise_perlin2, 0 },
    { "perlin3", 3, noise_perlin3, 0 },
    { "simplex2", 2, noise_simplex2, 0 },
    { "simplex3", 3, noise_simplex3, 0 },
    { "worley2", 2, noise_worley2, 0 },
    { "worley3", 3, noise_worley3, 0 },
    { "fbm2", 3, noise_fbm, 2 },
    { "fbm3", 4, noise_fbm, 3 },
    { "fill", 4, noise_fill, 0 },
    { "fillAsync", 4, noise_fill_async, 0 },
};

/* Noise.<method>(...): the method on the context's Generator (func_data[0]). */
static JSValue noise_shared_call(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int index, JSValue *func_data) {
    return noise_methods[index].func(ctx, func_data[0], argc, argv,
        noise_methods[index].magic);
}

static int noise_module_init(JSContext *ctx, JSModuleDef *m) {
    JSValue proto, constructor, shared, func;

    if (athena_register_class(ctx, &noise_class_id, &noise_class) < 0)
        return -1;
    proto = JS_NewObject(ctx);
    if (JS_IsException(proto))
        return -1;
    for (size_t i = 0; i < countof(noise_methods); i++) {
        func = JS_NewCFunctionMagic(ctx, noise_methods[i].func,
            noise_methods[i].name, noise_methods[i].length,
            JS_CFUNC_generic_magic, noise_methods[i].magic);
        if (JS_IsException(func) ||
            JS_DefinePropertyValueStr(ctx, proto, noise_methods[i].name, func,
                JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE) < 0) {
            JS_FreeValue(ctx, proto);
            return -1;
        }
    }
    constructor = JS_NewCFunction2(ctx, noise_ctor, "Generator", 1,
        JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, constructor, proto);
    JS_SetClassProto(ctx, noise_class_id, proto);
    if (JS_SetModuleExport(ctx, m, "Generator", constructor) < 0)
        return -1;

    shared = noise_new(ctx, JS_UNDEFINED, 0);
    if (JS_IsException(shared))
        return -1;
    for (size_t i = 0; i < countof(noise_methods); i++) {
        func = JS_NewCFunctionData(ctx, noise_shared_call,
            noise_methods[i].length, (int)i, 1, &shared);
        if (JS_IsException(func) ||
            JS_SetModuleExport(ctx, m, noise_methods[i].name, func) < 0) {
            JS_FreeValue(ctx, shared);
            return -1;
        }
    }
    JS_FreeValue(ctx, shared);   /* the functions keep it alive */
    func = JS_NewCFunction(ctx, noise_to_tiles, "toTiles", 4);
    if (JS_IsException(func))
        return -1;
    return JS_SetModuleExport(ctx, m, "toTiles", func);
}

JSModuleDef *athena_noise_init(JSContext *ctx) {
    JSModuleDef *module = athena_push_module(ctx, noise_module_init, NULL, 0,
        "Noise");

    if (module) {
        JS_AddModuleExport(ctx, module, "Generator");
        JS_AddModuleExport(ctx, module, "toTiles");
        for (size_t i = 0; i < countof(noise_methods); i++)
            JS_AddModuleExport(ctx, module, noise_methods[i].name);
    }
    return module;
}
