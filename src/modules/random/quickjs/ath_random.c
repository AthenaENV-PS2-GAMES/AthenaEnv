#include <athena/math.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include <ath_env.h>

#include <athena/random.h>
#include "ath_random.h"

/*
 * Random.Generator wraps an AthenaRandom. The module functions (Random.int,
 * Random.seed...) call the same methods on a Generator of their own context,
 * seeded from the clock, bound to them as function data: workers and scripts
 * run after std.reload() each get a separate sequence, freed with the context.
 */

/* Arrays up to this length are weighted and shuffled without malloc. */
#define RANDOM_STACK_ITEMS 32

static JSClassID random_class_id;

static void random_finalizer(JSRuntime *rt, JSValue value) {
    js_free_rt(rt, JS_GetOpaque(value, random_class_id));
}

static JSClassDef random_class = {
    "Generator",
    .finalizer = random_finalizer,
};

static AthenaRandom *random_this(JSContext *ctx, JSValueConst this_val) {
    return JS_GetOpaque2(ctx, this_val, random_class_id);
}

/*
 * Seeds from a number (integers up to 2^53 map one-to-one, other numbers by
 * their bits), a string (hashed) or undefined (the clock).
 */
static int random_seed_value(JSContext *ctx, JSValueConst value,
    uint64_t *seed, const char *name) {
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
        if (number == trunc(number) && fabs(number) <= 9007199254740992.0) {
            *seed = (uint64_t)(int64_t)number;
        } else {
            memcpy(seed, &number, sizeof(*seed));
        }
        return 0;
    }
    JS_ThrowTypeError(ctx, "%s seed must be a number or a string", name);
    return -1;
}

/* Exponent all ones: Infinity or NaN. The EE's FPU cannot tell by comparing. */
static inline bool random_float_bits_finite(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

static int random_int32(JSContext *ctx, JSValueConst value, int32_t *out,
    const char *name) {
    double number;

    /* Fast path: an int value needs no conversion (doubles are soft-float). */
    if (JS_VALUE_GET_TAG(value) == JS_TAG_INT) {
        *out = JS_VALUE_GET_INT(value);
        return 0;
    }
    if (JS_ToFloat64(ctx, &number, value))
        return -1;
    if (number != trunc(number) || number < -2147483648.0 ||
        number > 2147483647.0) {
        JS_ThrowRangeError(ctx, "%s bounds must be integers in the int32 range",
            name);
        return -1;
    }
    *out = (int32_t)number;
    return 0;
}

/*
 * Checked as a double (soft-float, IEEE): the EE's FPU has no infinities nor
 * NaN, so isfinite() on a float takes Infinity for a large number there.
 */
static int random_to_float(JSContext *ctx, JSValueConst value, float *out,
    const char *name) {
    double number;

    /* Fast paths without soft-float doubles: int and float32 values. */
    switch (JS_VALUE_GET_TAG(value)) {
    case JS_TAG_INT:
        *out = (float)JS_VALUE_GET_INT(value);
        return 0;
    case JS_CUSTOM_TAG_FLOAT32:
        *out = JS_VALUE_GET_FLOAT32(value);
        if (random_float_bits_finite(*out))
            return 0;
        JS_ThrowRangeError(ctx, "%s arguments must be finite numbers", name);
        return -1;
    }
    if (JS_ToFloat64(ctx, &number, value))
        return -1;
    if (!isfinite(number) || fabs(number) > ATHENA_FLOAT_MAX) {
        JS_ThrowRangeError(ctx, "%s arguments must be finite numbers", name);
        return -1;
    }
    *out = (float)number;
    return 0;
}

static int random_float_arg(JSContext *ctx, int argc, JSValueConst *argv,
    int index, float fallback, float *out, const char *name) {
    if (index >= argc || JS_IsUndefined(argv[index])) {
        *out = fallback;
        return 0;
    }
    return random_to_float(ctx, argv[index], out, name);
}

static int random_length(JSContext *ctx, JSValueConst value, uint32_t *length,
    const char *name) {
    JSValue property;
    int result;

    if (!JS_IsObject(value)) {
        JS_ThrowTypeError(ctx, "%s expects an array", name);
        return -1;
    }
    property = JS_GetPropertyStr(ctx, value, "length");
    if (JS_IsException(property))
        return -1;
    result = JS_ToUint32(ctx, length, property);
    JS_FreeValue(ctx, property);
    return result;
}

/*
 * Bytes of a typed array: 1 with `*data` set, 0 for any other value, -1 on
 * error (detached buffer). Runs no JavaScript.
 */
static int random_typed_array(JSContext *ctx, JSValueConst value,
    uint8_t **data, size_t *byte_length, size_t *element_size) {
    size_t offset = 0, buffer_length = 0;
    JSValue buffer;

    if (JS_GetTypedArrayType(value) < 0)
        return 0;
    buffer = JS_GetTypedArrayBuffer(ctx, value, &offset, byte_length,
        element_size);
    if (JS_IsException(buffer))
        return -1;
    *data = JS_GetArrayBuffer(ctx, &buffer_length, buffer);
    JS_FreeValue(ctx, buffer);
    if (!*data)
        return -1;
    if (offset + *byte_length > buffer_length) {
        JS_ThrowRangeError(ctx, "the typed array is out of bounds of its buffer");
        return -1;
    }
    *data += offset;
    return 1;
}

static JSValue random_new(JSContext *ctx, JSValueConst new_target,
    const AthenaRandom *state) {
    JSValue proto = JS_UNDEFINED, object;
    AthenaRandom *rng;

    if (!JS_IsUndefined(new_target)) {
        proto = JS_GetPropertyStr(ctx, new_target, "prototype");
        if (JS_IsException(proto))
            return JS_EXCEPTION;
        object = JS_NewObjectProtoClass(ctx, proto, random_class_id);
        JS_FreeValue(ctx, proto);
    } else {
        object = JS_NewObjectClass(ctx, random_class_id);
    }
    if (JS_IsException(object))
        return object;
    rng = js_malloc(ctx, sizeof(*rng));
    if (!rng) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    *rng = *state;
    JS_SetOpaque(object, rng);
    return object;
}

static JSValue random_ctor(JSContext *ctx, JSValueConst new_target, int argc,
    JSValueConst *argv) {
    AthenaRandom rng;
    uint64_t seed;

    if (random_seed_value(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &seed,
            "Random.Generator") < 0)
        return JS_EXCEPTION;
    athena_random_seed(&rng, seed);
    return random_new(ctx, new_target, &rng);
}

static JSValue random_seed(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    AthenaRandom *rng = random_this(ctx, this_val);
    uint64_t seed;

    if (!rng || random_seed_value(ctx, argc > 0 ? argv[0] : JS_UNDEFINED,
            &seed, "Random.seed") < 0)
        return JS_EXCEPTION;
    athena_random_seed(rng, seed);
    return JS_UNDEFINED;
}

static JSValue random_int(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    AthenaRandom *rng = random_this(ctx, this_val);
    int32_t min, max;

    if (!rng)
        return JS_EXCEPTION;
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Random.int expects min and max");
    if (random_int32(ctx, argv[0], &min, "Random.int") < 0 ||
        random_int32(ctx, argv[1], &max, "Random.int") < 0)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, athena_random_int(rng, min, max));
}

static JSValue random_float(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    AthenaRandom *rng = random_this(ctx, this_val);
    float min, max;

    if (!rng)
        return JS_EXCEPTION;
    if (argc == 0)
        return JS_NewFloat32(ctx, athena_random_float(rng));
    if (argc == 1) {
        /* float(max): [0, max). */
        min = 0.0f;
        if (random_to_float(ctx, argv[0], &max, "Random.float"))
            return JS_EXCEPTION;
    } else if (random_to_float(ctx, argv[0], &min, "Random.float") ||
        random_to_float(ctx, argv[1], &max, "Random.float")) {
        return JS_EXCEPTION;
    }
    return JS_NewFloat32(ctx, athena_random_range(rng, min, max));
}

static JSValue random_bool(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    AthenaRandom *rng = random_this(ctx, this_val);
    float p;

    if (!rng || random_float_arg(ctx, argc, argv, 0, 0.5f, &p, "Random.bool"))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, athena_random_bool(rng, p));
}

static JSValue random_gaussian(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    AthenaRandom *rng = random_this(ctx, this_val);
    float mean, stddev;

    if (!rng || random_float_arg(ctx, argc, argv, 0, 0.0f, &mean, "Random.gaussian") ||
        random_float_arg(ctx, argc, argv, 1, 1.0f, &stddev, "Random.gaussian"))
        return JS_EXCEPTION;
    return JS_NewFloat32(ctx, athena_random_gaussian(rng, mean, stddev));
}

static JSValue random_shuffle(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    AthenaRandom *rng = random_this(ctx, this_val);
    JSValueConst array = argc > 0 ? argv[0] : JS_UNDEFINED;
    JSValue stack[RANDOM_STACK_ITEMS], *items;
    size_t byte_length, element_size;
    uint32_t length, i;
    uint8_t *data;
    int failed = 0;

    if (!rng)
        return JS_EXCEPTION;
    switch (random_typed_array(ctx, array, &data, &byte_length, &element_size)) {
    case -1:
        return JS_EXCEPTION;
    case 1:
        athena_random_shuffle(rng, data, byte_length / element_size,
            element_size);
        return JS_DupValue(ctx, array);
    }
    if (!JS_IsArray(ctx, array))
        return JS_ThrowTypeError(ctx, "Random.shuffle expects an array or a typed array");
    if (random_length(ctx, array, &length, "Random.shuffle") < 0)
        return JS_EXCEPTION;
    items = length <= RANDOM_STACK_ITEMS ? stack :
        js_malloc(ctx, (size_t)length * sizeof(*items));
    if (!items)
        return JS_EXCEPTION;
    for (i = 0; i < length && !failed; i++) {
        items[i] = JS_GetPropertyUint32(ctx, array, i);
        failed = JS_IsException(items[i]);
    }
    if (failed) {
        for (i--; i > 0; )
            JS_FreeValue(ctx, items[--i]);
    } else {
        athena_random_shuffle(rng, items, length, sizeof(*items));
        /* JS_SetPropertyUint32 takes ownership of each value, even on error. */
        for (i = 0; i < length; i++) {
            if (failed)
                JS_FreeValue(ctx, items[i]);
            else
                failed = JS_SetPropertyUint32(ctx, array, i, items[i]) < 0;
        }
    }
    if (items != stack)
        js_free(ctx, items);
    return failed ? JS_EXCEPTION : JS_DupValue(ctx, array);
}

/*
 * Index drawn from the weights in `array`, or -1 with an exception. With
 * `expected` > 0, the weights must be that many (one per item of pick()).
 */
static int random_weighted_index(JSContext *ctx, AthenaRandom *rng,
    JSValueConst array, uint32_t expected, const char *name) {
    float stack[RANDOM_STACK_ITEMS], *weights;
    uint32_t length, i;
    int index = -1;

    if (random_length(ctx, array, &length, name) < 0)
        return -1;
    if (length == 0) {
        JS_ThrowRangeError(ctx, "%s needs at least one weight", name);
        return -1;
    }
    if (expected && length != expected) {
        JS_ThrowRangeError(ctx, "%s needs one weight per item (%u items, %u weights)",
            name, (unsigned int)expected, (unsigned int)length);
        return -1;
    }
    weights = length <= RANDOM_STACK_ITEMS ? stack :
        js_malloc(ctx, (size_t)length * sizeof(*weights));
    if (!weights)
        return -1;
    for (i = 0; i < length; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, array, i);
        int failed = JS_IsException(item) ||
            random_to_float(ctx, item, &weights[i], name);
        JS_FreeValue(ctx, item);
        if (failed)
            break;
    }
    if (i == length) {
        index = athena_random_weighted(rng, weights, length);
        if (index < 0)
            JS_ThrowRangeError(ctx, "%s weights must be finite, "
                "non-negative and not all zero", name);
    }
    if (weights != stack)
        js_free(ctx, weights);
    return index;
}

static JSValue random_weighted(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    AthenaRandom *rng = random_this(ctx, this_val);
    int index;

    if (!rng)
        return JS_EXCEPTION;
    index = random_weighted_index(ctx, rng, argc > 0 ? argv[0] : JS_UNDEFINED,
        0, "Random.weighted");
    return index < 0 ? JS_EXCEPTION : JS_NewInt32(ctx, index);
}

/* pick(items) or pick(items, weights): an item, undefined for no items. */
static JSValue random_pick(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    AthenaRandom *rng = random_this(ctx, this_val);
    uint32_t length;
    int index;

    if (!rng)
        return JS_EXCEPTION;
    if (random_length(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &length,
            "Random.pick") < 0)
        return JS_EXCEPTION;
    if (length == 0)
        return JS_UNDEFINED;
    if (argc < 2 || JS_IsUndefined(argv[1]))
        return JS_GetPropertyUint32(ctx, argv[0],
            athena_random_below(rng, length));
    index = random_weighted_index(ctx, rng, argv[1], length, "Random.pick");
    return index < 0 ? JS_EXCEPTION :
        JS_GetPropertyUint32(ctx, argv[0], (uint32_t)index);
}

static JSValue random_angle(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    AthenaRandom *rng = random_this(ctx, this_val);

    if (!rng)
        return JS_EXCEPTION;
    return JS_NewFloat32(ctx, athena_random_angle(rng));
}

/*
 * sample(items, k): k different items in random order, as a new array. The
 * indices are partially shuffled (k draws), then read: getters of `items`
 * run only after every draw.
 */
static JSValue random_sample(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    AthenaRandom *rng = random_this(ctx, this_val);
    uint32_t stack[RANDOM_STACK_ITEMS], *indices, length, k, i;
    double requested;
    JSValue result;

    if (!rng)
        return JS_EXCEPTION;
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Random.sample expects items and a count");
    if (random_length(ctx, argv[0], &length, "Random.sample") < 0 ||
        JS_ToFloat64(ctx, &requested, argv[1]))
        return JS_EXCEPTION;
    if (requested != trunc(requested) || requested < 0 || requested > length)
        return JS_ThrowRangeError(ctx, "Random.sample count must be an integer from 0 to %u",
            (unsigned int)length);
    k = (uint32_t)requested;
    indices = length <= RANDOM_STACK_ITEMS ? stack :
        js_malloc(ctx, (size_t)length * sizeof(*indices));
    if (!indices)
        return JS_EXCEPTION;
    for (i = 0; i < length; i++)
        indices[i] = i;
    athena_random_partial_shuffle(rng, indices, length, k, sizeof(*indices));
    result = JS_NewArray(ctx);
    for (i = 0; i < k && !JS_IsException(result); i++) {
        JSValue item = JS_GetPropertyUint32(ctx, argv[0], indices[i]);
        if (JS_IsException(item) ||
            JS_SetPropertyUint32(ctx, result, i, item) < 0) {
            JS_FreeValue(ctx, result);
            result = JS_EXCEPTION;
        }
    }
    if (indices != stack)
        js_free(ctx, indices);
    return result;
}

/* Bounds of the element types that fill() writes as integers. */
static int random_int_type_bounds(int type, double *low, double *high) {
    switch (type) {
    case JS_TYPED_ARRAY_INT8: *low = -128; *high = 127; return 1;
    case JS_TYPED_ARRAY_UINT8:
    case JS_TYPED_ARRAY_UINT8C: *low = 0; *high = 255; return 1;
    case JS_TYPED_ARRAY_INT16: *low = -32768; *high = 32767; return 1;
    case JS_TYPED_ARRAY_UINT16: *low = 0; *high = 65535; return 1;
    case JS_TYPED_ARRAY_INT32: *low = -2147483648.0; *high = 2147483647.0; return 1;
    case JS_TYPED_ARRAY_UINT32: *low = 0; *high = 4294967295.0; return 1;
    default: return 0;
    }
}

/*
 * fill(array, min?, max?) and fillGaussian(array, mean?, stddev?); magic 1
 * is the gaussian. Float arrays get floats ([0, 1) by default, [0, max) with
 * one bound); integer arrays get integers in [min, max], by default the whole
 * range of the type. Bounds are read first: the buffer pointer is taken
 * after the last call that can run JavaScript.
 */
static JSValue random_fill_common(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int gaussian) {
    const char *name = gaussian ? "Random.fillGaussian" : "Random.fill";
    AthenaRandom *rng = random_this(ctx, this_val);
    JSValueConst array = argc > 0 ? argv[0] : JS_UNDEFINED;
    int type = JS_GetTypedArrayType(array);
    size_t byte_length, element_size, count;
    double low, high, a, b;
    float fa, fb;
    uint8_t *data;

    if (!rng)
        return JS_EXCEPTION;
    if (type == JS_TYPED_ARRAY_FLOAT32 || type == JS_TYPED_ARRAY_FLOAT64) {
        if (gaussian) {
            if (random_float_arg(ctx, argc, argv, 1, 0.0f, &fa, name) ||
                random_float_arg(ctx, argc, argv, 2, 1.0f, &fb, name))
                return JS_EXCEPTION;
        } else if (argc < 2 || JS_IsUndefined(argv[1])) {
            fa = 0.0f;
            fb = 1.0f;
        } else if (argc < 3 || JS_IsUndefined(argv[2])) {
            fa = 0.0f;
            if (random_to_float(ctx, argv[1], &fb, name))
                return JS_EXCEPTION;
        } else if (random_to_float(ctx, argv[1], &fa, name) ||
            random_to_float(ctx, argv[2], &fb, name)) {
            return JS_EXCEPTION;
        }
    } else if (!gaussian && random_int_type_bounds(type, &low, &high)) {
        a = low;
        b = high;
        if ((argc > 1 && !JS_IsUndefined(argv[1]) && JS_ToFloat64(ctx, &a, argv[1])) ||
            (argc > 2 && !JS_IsUndefined(argv[2]) && JS_ToFloat64(ctx, &b, argv[2])))
            return JS_EXCEPTION;
        if (a != trunc(a) || b != trunc(b) || a < low || b > high || a > b)
            return JS_ThrowRangeError(ctx, "%s bounds must be integers with "
                "min <= max, within %.0f to %.0f for this array", name, low, high);
    } else {
        return JS_ThrowTypeError(ctx, gaussian ?
            "%s expects a Float32Array or a Float64Array" :
            "%s expects a typed array of numbers (not BigInt)", name);
    }

    /* No JavaScript runs from here on. */
    if (random_typed_array(ctx, array, &data, &byte_length, &element_size) != 1)
        return JS_EXCEPTION;
    count = byte_length / element_size;
    if (type == JS_TYPED_ARRAY_FLOAT32) {
        if (gaussian)
            athena_random_fill_gaussian(rng, (float *)data, count, fa, fb);
        else
            athena_random_fill_range(rng, (float *)data, count, fa, fb);
    } else if (type == JS_TYPED_ARRAY_FLOAT64) {
        double *out = (double *)data;
        for (size_t i = 0; i < count; i++)
            out[i] = gaussian ? athena_random_gaussian(rng, fa, fb) :
                athena_random_range(rng, fa, fb);
    } else {
        /* span = max - min fits 32 bits; the full span takes all 32 bits. */
        int64_t min = (int64_t)a;
        uint32_t span = (uint32_t)((int64_t)b - min);
        for (size_t i = 0; i < count; i++) {
            uint32_t draw = span == UINT32_MAX ? athena_random_u32(rng) :
                athena_random_below(rng, span + 1);
            int64_t value = min + draw;
            switch (element_size) {
            case 1: data[i] = (uint8_t)value; break;
            case 2: ((uint16_t *)data)[i] = (uint16_t)value; break;
            default: ((uint32_t *)data)[i] = (uint32_t)value; break;
            }
        }
    }
    return JS_DupValue(ctx, array);
}

static JSValue random_fill(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    return random_fill_common(ctx, this_val, argc, argv, 0);
}

static JSValue random_fill_gaussian(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    return random_fill_common(ctx, this_val, argc, argv, 1);
}

static JSValue random_state(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    AthenaRandom *rng = random_this(ctx, this_val);
    JSValue array;

    if (!rng)
        return JS_EXCEPTION;
    array = JS_NewArray(ctx);
    if (JS_IsException(array))
        return array;
    for (uint32_t i = 0; i < ATHENA_RANDOM_STATE_WORDS; i++) {
        if (JS_SetPropertyUint32(ctx, array, i,
                JS_NewUint32(ctx, rng->s[i])) < 0) {
            JS_FreeValue(ctx, array);
            return JS_EXCEPTION;
        }
    }
    return array;
}

static JSValue random_set_state(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    AthenaRandom *rng = random_this(ctx, this_val);
    JSValueConst array = argc > 0 ? argv[0] : JS_UNDEFINED;
    uint32_t state[ATHENA_RANDOM_STATE_WORDS], length;

    if (!rng || random_length(ctx, array, &length, "Random.setState") < 0)
        return JS_EXCEPTION;
    if (length != ATHENA_RANDOM_STATE_WORDS)
        return JS_ThrowRangeError(ctx, "Random.setState expects the %d numbers of state()",
            ATHENA_RANDOM_STATE_WORDS);
    for (uint32_t i = 0; i < ATHENA_RANDOM_STATE_WORDS; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, array, i);
        double number;
        int failed = JS_IsException(item) || JS_ToFloat64(ctx, &number, item);

        JS_FreeValue(ctx, item);
        if (failed)
            return JS_EXCEPTION;
        if (number != trunc(number) || number < 0 || number > 4294967295.0)
            return JS_ThrowRangeError(ctx, "Random.setState words must be integers in [0, 2^32)");
        state[i] = (uint32_t)number;
    }
    if (!athena_random_set_state(rng, state))
        return JS_ThrowRangeError(ctx, "Random.setState: the all-zero state is invalid");
    return JS_UNDEFINED;
}

static JSValue random_clone(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    AthenaRandom *rng = random_this(ctx, this_val);

    if (!rng)
        return JS_EXCEPTION;
    return random_new(ctx, JS_UNDEFINED, rng);
}

/* Methods of Random.Generator, also exported as module functions. */
#define RANDOM_METHODS(X) \
    X("seed", 1, random_seed) \
    X("int", 2, random_int) \
    X("float", 2, random_float) \
    X("bool", 1, random_bool) \
    X("gaussian", 2, random_gaussian) \
    X("pick", 1, random_pick) \
    X("shuffle", 1, random_shuffle) \
    X("weighted", 1, random_weighted) \
    X("state", 0, random_state) \
    X("setState", 1, random_set_state) \
    X("angle", 0, random_angle) \
    X("sample", 2, random_sample) \
    X("fill", 3, random_fill) \
    X("fillGaussian", 3, random_fill_gaussian)

#define RANDOM_PROTO_ENTRY(name, length, func) JS_CFUNC_DEF(name, length, func),
#define RANDOM_METHOD_ENTRY(name, length, func) { name, length, func },

static const JSCFunctionListEntry random_proto_funcs[] = {
    RANDOM_METHODS(RANDOM_PROTO_ENTRY)
    JS_CFUNC_DEF("clone", 0, random_clone),
};

static const struct {
    const char *name;
    int length;
    JSCFunction *func;
} random_methods[] = {
    RANDOM_METHODS(RANDOM_METHOD_ENTRY)
};

/* Random.<method>(...): the method on the context's Generator (func_data[0]). */
static JSValue random_shared_call(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic, JSValue *func_data) {
    return random_methods[magic].func(ctx, func_data[0], argc, argv);
}

static int random_module_init(JSContext *ctx, JSModuleDef *m) {
    JSValue proto, constructor, shared;
    AthenaRandom seeded;

    if (athena_register_class(ctx, &random_class_id, &random_class) < 0)
        return -1;
    proto = JS_NewObject(ctx);
    if (JS_IsException(proto))
        return -1;
    JS_SetPropertyFunctionList(ctx, proto, random_proto_funcs,
        countof(random_proto_funcs));
    constructor = JS_NewCFunction2(ctx, random_ctor, "Generator", 1,
        JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, constructor, proto);
    JS_SetClassProto(ctx, random_class_id, proto);
    if (JS_SetModuleExport(ctx, m, "Generator", constructor) < 0)
        return -1;

    athena_random_seed(&seeded, athena_random_entropy());
    shared = random_new(ctx, JS_UNDEFINED, &seeded);
    if (JS_IsException(shared))
        return -1;
    for (size_t i = 0; i < countof(random_methods); i++) {
        JSValue func = JS_NewCFunctionData(ctx, random_shared_call,
            random_methods[i].length, (int)i, 1, &shared);
        if (JS_IsException(func) ||
            JS_SetModuleExport(ctx, m, random_methods[i].name, func) < 0) {
            JS_FreeValue(ctx, shared);
            return -1;
        }
    }
    JS_FreeValue(ctx, shared);   /* the functions keep it alive */
    return 0;
}

JSModuleDef *athena_random_init(JSContext *ctx) {
    JSModuleDef *module = athena_push_module(ctx, random_module_init, NULL, 0,
        "Random");

    if (module) {
        JS_AddModuleExport(ctx, module, "Generator");
        for (size_t i = 0; i < countof(random_methods); i++)
            JS_AddModuleExport(ctx, module, random_methods[i].name);
    }
    return module;
}
