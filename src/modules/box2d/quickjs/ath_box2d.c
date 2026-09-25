#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <athena/js/box2d.h>

#include "ath_box2d.h"
#include "ath_box2d_internal.h"

JSClassID b2js_world_class_id;
JSClassID b2js_class_ids[B2JS_KIND_COUNT];

static const char *const kind_names[B2JS_KIND_COUNT] = { "Body", "Shape", "Joint", "Chain" };

/* Chains hold no user data in Box2D; longer inputs are refused before allocating. */
#define B2JS_MAX_CHAIN_POINTS 65536
#define B2JS_MAX_SUB_STEPS 64

/* ------------------------------------------------------------------------ */
/* Arguments                                                                 */
/* ------------------------------------------------------------------------ */

int b2js_argc(JSContext *ctx, int argc, int min, int max, const char *where) {
    if (argc >= min && argc <= max)
        return 0;
    if (min == max)
        JS_ThrowTypeError(ctx, "%s expects %d argument%s", where, min, min == 1 ? "" : "s");
    else
        JS_ThrowTypeError(ctx, "%s expects between %d and %d arguments", where, min, max);
    return -1;
}

bool b2js_has(int argc, JSValueConst *argv, int index) {
    return index < argc && !JS_IsUndefined(argv[index]);
}

/*
 * A JS number as a float, without double arithmetic (software on the EE):
 * ints and float32 values convert directly, a float64 is checked on its
 * exponent bits and narrowed once. Returns false for NaN, infinities and
 * magnitudes of 2^127 or more (the EE FPU has no infinity or NaN, so a
 * narrowed value could not be checked afterwards).
 */
static bool number_to_float(JSValueConst value, float *out) {
    int tag = JS_VALUE_GET_NORM_TAG(value);
    union { double d; uint64_t u; } bits;

    if (tag == JS_TAG_INT) {
        *out = (float)JS_VALUE_GET_INT(value);
        return true;
    }
    if (tag == JS_CUSTOM_TAG_FLOAT32) {
        *out = JS_VALUE_GET_FLOAT32(value);
        return athena_box2d_valid_float(*out);
    }
    bits.d = JS_VALUE_GET_FLOAT64(value);
    if (((bits.u >> 52) & 0x7FF) >= 1023 + 127)
        return false;
    *out = (float)bits.d;
    return true;
}

/* Largest magnitude of a scalar; coordinates are held to B2_HUGE. */
#define B2JS_MAX_VALUE 1e10f

static bool within_range(float value, B2JSRange range) {
    float limit = range == B2JS_COORD ? athena_box2d_max_coordinate() : B2JS_MAX_VALUE;
    return value <= limit && value >= -limit;
}

int b2js_float(JSContext *ctx, JSValueConst value, const char *where, const char *what,
    B2JSRange range, float *out) {
    float number;

    if (!JS_IsNumber(value)) {
        JS_ThrowTypeError(ctx, "%s: %s must be a number", where, what);
        return -1;
    }
    if (!number_to_float(value, &number) || !within_range(number, range)) {
        if (range == B2JS_COORD)
            JS_ThrowRangeError(ctx, "%s: %s must be a finite number within +-%g", where, what,
                (double)athena_box2d_max_coordinate());
        else
            JS_ThrowRangeError(ctx, "%s: %s must be a finite number within +-1e10", where, what);
        return -1;
    }
    if (range == B2JS_NONNEG && number < 0.0f) {
        JS_ThrowRangeError(ctx, "%s: %s must be >= 0", where, what);
        return -1;
    }
    if (range == B2JS_POS && !(number > 0.0f)) {
        JS_ThrowRangeError(ctx, "%s: %s must be > 0", where, what);
        return -1;
    }
    *out = number;
    return 0;
}

int b2js_int(JSContext *ctx, JSValueConst value, const char *where, const char *what,
    int min, int max, int *out) {
    double number;

    if (!JS_IsNumber(value)) {
        JS_ThrowTypeError(ctx, "%s: %s must be a number", where, what);
        return -1;
    }
    /* Integers normally arrive as ints; only other numbers take the double path. */
    if (JS_VALUE_GET_NORM_TAG(value) == JS_TAG_INT) {
        int integer = JS_VALUE_GET_INT(value);
        if (integer >= min && integer <= max) {
            *out = integer;
            return 0;
        }
    }
    if (JS_ToFloat64(ctx, &number, value) < 0)
        return -1;
    if (!isfinite(number) || floor(number) != number || number < min || number > max) {
        JS_ThrowRangeError(ctx, "%s: %s must be an integer between %d and %d", where, what, min, max);
        return -1;
    }
    *out = (int)number;
    return 0;
}

int b2js_bool(JSContext *ctx, JSValueConst value, const char *where, const char *what, bool *out) {
    if (!JS_IsBool(value) && !JS_IsNumber(value)) {
        JS_ThrowTypeError(ctx, "%s: %s must be a boolean", where, what);
        return -1;
    }
    *out = JS_ToBool(ctx, value);
    return 0;
}

int b2js_vec2(JSContext *ctx, JSValueConst value, const char *where, const char *what, b2Vec2 *out) {
    JSValue x, y;
    float fx, fy;
    int ret = -1;

    if (!JS_IsObject(value)) {
        JS_ThrowTypeError(ctx, "%s: %s must be an object { x, y }", where, what);
        return -1;
    }
    x = JS_GetPropertyStr(ctx, value, "x");
    if (JS_IsException(x))
        return -1;
    y = JS_GetPropertyStr(ctx, value, "y");
    if (JS_IsException(y)) {
        JS_FreeValue(ctx, x);
        return -1;
    }

    if (!JS_IsNumber(x) || !JS_IsNumber(y)) {
        JS_ThrowTypeError(ctx, "%s: %s must be { x: number, y: number }", where, what);
    } else if (!number_to_float(x, &fx) || !number_to_float(y, &fy) ||
        !within_range(fx, B2JS_COORD) || !within_range(fy, B2JS_COORD)) {
        /* Vectors are positions, offsets, velocities or directions: all within B2_HUGE. */
        JS_ThrowRangeError(ctx, "%s: %s must have finite coordinates within +-%g", where, what,
            (double)athena_box2d_max_coordinate());
    } else {
        out->x = fx;
        out->y = fy;
        ret = 0;
    }
    JS_FreeValue(ctx, x);
    JS_FreeValue(ctx, y);
    return ret;
}

int b2js_bits(JSContext *ctx, JSValueConst value, const char *where, const char *what, uint64_t *out) {
    if (JS_IsBigInt(ctx, value)) {
        int64_t bits;
        if (JS_ToBigInt64(ctx, &bits, value) < 0)
            return -1;
        *out = (uint64_t)bits;
        return 0;
    }
    if (JS_VALUE_GET_NORM_TAG(value) == JS_TAG_INT) {
        /* Sign-extended: -1 is every bit. */
        *out = (uint64_t)(int64_t)JS_VALUE_GET_INT(value);
        return 0;
    }
    if (JS_IsNumber(value)) {
        double number;
        if (JS_ToFloat64(ctx, &number, value) < 0)
            return -1;
        /* Integers in [-2^63, 2^64): negatives are read as two's complement. */
        if (isfinite(number) && floor(number) == number &&
            number >= -9223372036854775808.0 && number < 18446744073709551616.0) {
            *out = number < 0.0 ? (uint64_t)(int64_t)number : (uint64_t)number;
            return 0;
        }
        JS_ThrowRangeError(ctx, "%s: %s must be a 64-bit integer", where, what);
        return -1;
    }
    JS_ThrowTypeError(ctx, "%s: %s must be a number or a BigInt", where, what);
    return -1;
}

static int points_length(JSContext *ctx, JSValueConst value, const char *where, const char *what,
    int64_t *length) {
    JSValue item;
    int is_array = JS_IsArray(ctx, value);

    if (is_array < 0)
        return -1;
    if (!is_array) {
        JS_ThrowTypeError(ctx, "%s: %s must be an array of { x, y }", where, what);
        return -1;
    }
    item = JS_GetPropertyStr(ctx, value, "length");
    if (JS_IsException(item))
        return -1;
    if (JS_ToInt64(ctx, length, item) < 0) {
        JS_FreeValue(ctx, item);
        return -1;
    }
    JS_FreeValue(ctx, item);
    return 0;
}

static int points_read(JSContext *ctx, JSValueConst value, const char *where, const char *what,
    b2Vec2 *out, int count) {
    char name[64];

    for (int i = 0; i < count; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, value, (uint32_t)i);
        int ret;

        if (JS_IsException(item))
            return -1;
        snprintf(name, sizeof(name), "%s[%d]", what, i);
        ret = b2js_vec2(ctx, item, where, name, &out[i]);
        JS_FreeValue(ctx, item);
        if (ret < 0)
            return -1;
    }
    return 0;
}

int b2js_points(JSContext *ctx, JSValueConst value, const char *where, const char *what,
    int min, int max, b2Vec2 *out, int *count) {
    int64_t length;

    if (points_length(ctx, value, where, what, &length) < 0)
        return -1;
    if (length < min || length > max) {
        JS_ThrowRangeError(ctx, "%s: %s must have between %d and %d points", where, what, min, max);
        return -1;
    }
    if (points_read(ctx, value, where, what, out, (int)length) < 0)
        return -1;
    *count = (int)length;
    return 0;
}

b2Vec2 *b2js_points_alloc(JSContext *ctx, JSValueConst value, const char *where, const char *what,
    int min, int *count) {
    int64_t length;
    b2Vec2 *points;

    if (points_length(ctx, value, where, what, &length) < 0)
        return NULL;
    if (length < min || length > B2JS_MAX_CHAIN_POINTS) {
        JS_ThrowRangeError(ctx, "%s: %s must have between %d and %d points", where, what,
            min, B2JS_MAX_CHAIN_POINTS);
        return NULL;
    }
    points = malloc(sizeof(*points) * (size_t)length);
    if (!points) {
        JS_ThrowOutOfMemory(ctx);
        return NULL;
    }
    if (points_read(ctx, value, where, what, points, (int)length) < 0) {
        free(points);
        return NULL;
    }
    *count = (int)length;
    return points;
}

int b2js_options(JSContext *ctx, int argc, JSValueConst *argv, int index, const char *where) {
    int is_array;

    if (!b2js_has(argc, argv, index) || JS_IsNull(argv[index]))
        return 0;
    is_array = JS_IsArray(ctx, argv[index]);
    if (is_array < 0)
        return -1;
    if (!JS_IsObject(argv[index]) || is_array || JS_IsFunction(ctx, argv[index])) {
        JS_ThrowTypeError(ctx, "%s: options must be an object", where);
        return -1;
    }
    return 1;
}

JSValue b2js_opt_value(JSContext *ctx, JSValueConst options, const char *key) {
    return JS_GetPropertyStr(ctx, options, key);
}

int b2js_opt_float(JSContext *ctx, JSValueConst options, const char *key, const char *where,
    B2JSRange range, float *out) {
    JSValue value = JS_GetPropertyStr(ctx, options, key);
    int ret;

    if (JS_IsException(value))
        return -1;
    if (JS_IsUndefined(value))
        return 0;
    ret = b2js_float(ctx, value, where, key, range, out) < 0 ? -1 : 1;
    JS_FreeValue(ctx, value);
    return ret;
}

int b2js_opt_int(JSContext *ctx, JSValueConst options, const char *key, const char *where,
    int min, int max, int *out) {
    JSValue value = JS_GetPropertyStr(ctx, options, key);
    int ret;

    if (JS_IsException(value))
        return -1;
    if (JS_IsUndefined(value))
        return 0;
    ret = b2js_int(ctx, value, where, key, min, max, out) < 0 ? -1 : 1;
    JS_FreeValue(ctx, value);
    return ret;
}

int b2js_opt_bool(JSContext *ctx, JSValueConst options, const char *key, const char *where, bool *out) {
    JSValue value = JS_GetPropertyStr(ctx, options, key);
    int ret;

    if (JS_IsException(value))
        return -1;
    if (JS_IsUndefined(value))
        return 0;
    ret = b2js_bool(ctx, value, where, key, out) < 0 ? -1 : 1;
    JS_FreeValue(ctx, value);
    return ret;
}

int b2js_opt_vec2(JSContext *ctx, JSValueConst options, const char *key, const char *where, b2Vec2 *out) {
    JSValue value = JS_GetPropertyStr(ctx, options, key);
    int ret;

    if (JS_IsException(value))
        return -1;
    if (JS_IsUndefined(value))
        return 0;
    ret = b2js_vec2(ctx, value, where, key, out) < 0 ? -1 : 1;
    JS_FreeValue(ctx, value);
    return ret;
}

static int opt_bits(JSContext *ctx, JSValueConst options, const char *key, const char *where,
    const char *what, uint64_t *out) {
    JSValue value = JS_GetPropertyStr(ctx, options, key);
    int ret;

    if (JS_IsException(value))
        return -1;
    if (JS_IsUndefined(value))
        return 0;
    ret = b2js_bits(ctx, value, where, what, out) < 0 ? -1 : 1;
    JS_FreeValue(ctx, value);
    return ret;
}

int b2js_filter(JSContext *ctx, JSValueConst value, const char *where, b2Filter *filter) {
    JSValue group;
    int ret = 0;

    if (!JS_IsObject(value)) {
        JS_ThrowTypeError(ctx, "%s: filter must be an object", where);
        return -1;
    }
    if (opt_bits(ctx, value, "categoryBits", where, "filter.categoryBits", &filter->categoryBits) < 0 ||
        opt_bits(ctx, value, "maskBits", where, "filter.maskBits", &filter->maskBits) < 0)
        return -1;

    group = JS_GetPropertyStr(ctx, value, "groupIndex");
    if (JS_IsException(group))
        return -1;
    if (!JS_IsUndefined(group)) {
        int index;
        ret = b2js_int(ctx, group, where, "filter.groupIndex", INT32_MIN, INT32_MAX, &index);
        if (ret == 0)
            filter->groupIndex = index;
    }
    JS_FreeValue(ctx, group);
    return ret;
}

int b2js_query_filter(JSContext *ctx, int argc, JSValueConst *argv, int index, const char *where,
    b2QueryFilter *filter) {
    *filter = b2DefaultQueryFilter();
    if (!b2js_has(argc, argv, index) || JS_IsNull(argv[index]))
        return 0;
    if (!JS_IsObject(argv[index])) {
        JS_ThrowTypeError(ctx, "%s: filter must be an object { categoryBits?, maskBits? }", where);
        return -1;
    }
    if (opt_bits(ctx, argv[index], "categoryBits", where, "filter.categoryBits", &filter->categoryBits) < 0 ||
        opt_bits(ctx, argv[index], "maskBits", where, "filter.maskBits", &filter->maskBits) < 0)
        return -1;
    return 0;
}

int b2js_name(JSContext *ctx, JSValueConst value, const char *where, char out[B2_NAME_LENGTH + 1]) {
    size_t length, n;
    const char *name;

    if (!JS_IsString(value)) {
        JS_ThrowTypeError(ctx, "%s: name must be a string", where);
        return -1;
    }
    name = JS_ToCStringLen(ctx, &length, value);
    if (!name)
        return -1;
    /* Box2D keeps B2_NAME_LENGTH bytes: cut at a UTF-8 character boundary. */
    n = length < B2_NAME_LENGTH ? length : B2_NAME_LENGTH;
    if (n < length) {
        while (n > 0 && ((unsigned char)name[n] & 0xC0) == 0x80)
            n--;
    }
    memcpy(out, name, n);
    out[n] = '\0';
    JS_FreeCString(ctx, name);
    return 0;
}

int b2js_material(JSContext *ctx, JSValueConst options, const char *where, b2SurfaceMaterial *material) {
    uint64_t color = material->customColor;

    if (b2js_opt_float(ctx, options, "friction", where, B2JS_NONNEG, &material->friction) < 0 ||
        b2js_opt_float(ctx, options, "restitution", where, B2JS_NONNEG, &material->restitution) < 0 ||
        b2js_opt_float(ctx, options, "rollingResistance", where, B2JS_NONNEG, &material->rollingResistance) < 0 ||
        b2js_opt_float(ctx, options, "tangentSpeed", where, B2JS_ANY, &material->tangentSpeed) < 0 ||
        opt_bits(ctx, options, "userMaterialId", where, "userMaterialId", &material->userMaterialId) < 0 ||
        opt_bits(ctx, options, "customColor", where, "customColor", &color) < 0)
        return -1;
    if (color > 0xFFFFFFFFu) {
        JS_ThrowRangeError(ctx, "%s: customColor must be a 32-bit color (0xRRGGBB, 0 = default)", where);
        return -1;
    }
    material->customColor = (uint32_t)color;
    return 0;
}

JSValue b2js_new_material(JSContext *ctx, b2SurfaceMaterial material) {
    JSValue object = JS_NewObject(ctx);

    if (JS_IsException(object))
        return object;
    JS_SetPropertyStr(ctx, object, "friction", JS_NewFloat32(ctx, material.friction));
    JS_SetPropertyStr(ctx, object, "restitution", JS_NewFloat32(ctx, material.restitution));
    JS_SetPropertyStr(ctx, object, "rollingResistance", JS_NewFloat32(ctx, material.rollingResistance));
    JS_SetPropertyStr(ctx, object, "tangentSpeed", JS_NewFloat32(ctx, material.tangentSpeed));
    JS_SetPropertyStr(ctx, object, "userMaterialId", b2js_new_bits(ctx, material.userMaterialId));
    JS_SetPropertyStr(ctx, object, "customColor", JS_NewInt64(ctx, material.customColor));
    return object;
}

int b2js_shape_def(JSContext *ctx, JSValueConst options, const char *where, b2ShapeDef *def) {
    JSValue filter;
    int ret = 0;

    if (b2js_opt_float(ctx, options, "density", where, B2JS_NONNEG, &def->density) < 0 ||
        b2js_material(ctx, options, where, &def->material) < 0 ||
        b2js_opt_bool(ctx, options, "isSensor", where, &def->isSensor) < 0 ||
        b2js_opt_bool(ctx, options, "enableSensorEvents", where, &def->enableSensorEvents) < 0 ||
        b2js_opt_bool(ctx, options, "enableContactEvents", where, &def->enableContactEvents) < 0 ||
        b2js_opt_bool(ctx, options, "enableHitEvents", where, &def->enableHitEvents) < 0)
        return -1;

    filter = JS_GetPropertyStr(ctx, options, "filter");
    if (JS_IsException(filter))
        return -1;
    if (!JS_IsUndefined(filter))
        ret = b2js_filter(ctx, filter, where, &def->filter);
    JS_FreeValue(ctx, filter);
    return ret;
}

/* ------------------------------------------------------------------------ */
/* Values                                                                    */
/* ------------------------------------------------------------------------ */

B2JSAtoms b2js_atoms;

/*
 * Atoms belong to a runtime, and the Box2D world table is process-wide: the
 * module serves one runtime at a time (the VM, which std.reload() and error
 * restarts recreate after cleaning up). A second runtime (e.g. an os.Worker
 * build) gets an error instead of atoms of another runtime.
 */
static JSRuntime *atoms_runtime;

static int atoms_init(JSContext *ctx) {
    JSRuntime *rt = JS_GetRuntime(ctx);

    if (atoms_runtime == rt)
        return 0;
    if (atoms_runtime) {
        JS_ThrowInternalError(ctx, "Box2D: the module is already in use by another JS runtime");
        return -1;
    }
#define B2JS_ATOM_NEW(name) \
    if ((b2js_atoms.name = JS_NewAtom(ctx, #name)) == JS_ATOM_NULL) goto fail;
    B2JS_ATOM_LIST(B2JS_ATOM_NEW)
#undef B2JS_ATOM_NEW
    atoms_runtime = rt;
    return 0;

fail:
#define B2JS_ATOM_FREE(name) \
    if (b2js_atoms.name != JS_ATOM_NULL) { JS_FreeAtom(ctx, b2js_atoms.name); b2js_atoms.name = JS_ATOM_NULL; }
    B2JS_ATOM_LIST(B2JS_ATOM_FREE)
    return -1;
}

/* Module cleanup (manifest quickjs.cleanup_func), before the context goes away. */
void athena_box2d_cleanup(JSContext *ctx) {
    if (atoms_runtime != JS_GetRuntime(ctx))
        return;
    B2JS_ATOM_LIST(B2JS_ATOM_FREE)
#undef B2JS_ATOM_FREE
    atoms_runtime = NULL;
    /* The next VM (std.reload(), restart after an error) starts without a limit. */
    athena_box2d_set_memory_limit(0);
}

int b2js_check_memory(JSContext *ctx, const char *where) {
    if (athena_box2d_memory_available())
        return 0;
    JS_ThrowRangeError(ctx, "%s: out of Box2D memory (%u bytes used, limit %u, reserve %u bytes of free RAM)",
        where, (unsigned)athena_box2d_memory_used(), (unsigned)athena_box2d_memory_limit(),
        (unsigned)ATHENA_BOX2D_RESERVE);
    return -1;
}

JSValue b2js_new_vec2(JSContext *ctx, b2Vec2 v) {
    JSValue object = JS_NewObject(ctx);

    if (JS_IsException(object))
        return object;
    b2js_set(ctx, object, b2js_atoms.x, JS_NewFloat32(ctx, v.x));
    b2js_set(ctx, object, b2js_atoms.y, JS_NewFloat32(ctx, v.y));
    return object;
}

JSValue b2js_new_aabb(JSContext *ctx, b2AABB aabb) {
    JSValue object = JS_NewObject(ctx);

    if (JS_IsException(object))
        return object;
    JS_SetPropertyStr(ctx, object, "lowerX", JS_NewFloat32(ctx, aabb.lowerBound.x));
    JS_SetPropertyStr(ctx, object, "lowerY", JS_NewFloat32(ctx, aabb.lowerBound.y));
    JS_SetPropertyStr(ctx, object, "upperX", JS_NewFloat32(ctx, aabb.upperBound.x));
    JS_SetPropertyStr(ctx, object, "upperY", JS_NewFloat32(ctx, aabb.upperBound.y));
    return object;
}

JSValue b2js_new_bits(JSContext *ctx, uint64_t bits) {
    int64_t value = (int64_t)bits;

    /* Doubles hold every integer up to 2^53 exactly. */
    if (value >= -9007199254740992LL && value <= 9007199254740992LL)
        return JS_NewInt64(ctx, value);
    return JS_NewBigUint64(ctx, bits);
}

JSValue b2js_new_filter(JSContext *ctx, b2Filter filter) {
    JSValue object = JS_NewObject(ctx);

    if (JS_IsException(object))
        return object;
    JS_SetPropertyStr(ctx, object, "categoryBits", b2js_new_bits(ctx, filter.categoryBits));
    JS_SetPropertyStr(ctx, object, "maskBits", b2js_new_bits(ctx, filter.maskBits));
    JS_SetPropertyStr(ctx, object, "groupIndex", JS_NewInt32(ctx, filter.groupIndex));
    return object;
}

/* ------------------------------------------------------------------------ */
/* Handles                                                                   */
/* ------------------------------------------------------------------------ */

B2JSHandle *b2js_handle_any(JSValueConst value, B2JSKind kind) {
    return JS_GetOpaque(value, b2js_class_ids[kind]);
}

B2JSHandle *b2js_handle(JSContext *ctx, JSValueConst value, B2JSKind kind, const char *where) {
    B2JSHandle *handle = b2js_handle_any(value, kind);

    if (!handle) {
        JS_ThrowTypeError(ctx, "%s: expected a Box2D %s", where, kind_names[kind]);
        return NULL;
    }
    if (handle->dead) {
        JS_ThrowTypeError(ctx, "%s: the %s has been destroyed", where, kind_names[kind]);
        return NULL;
    }
    return handle;
}

static B2JSHandle **handle_list(B2JSWorld *world, B2JSKind kind) {
    return kind == B2JS_CHAIN ? &world->chains : &world->handles;
}

static void handle_unlink(B2JSHandle *handle) {
    B2JSHandle **list = handle_list(handle->world, handle->kind);

    if (handle->prev)
        handle->prev->next = handle->next;
    else
        *list = handle->next;
    if (handle->next)
        handle->next->prev = handle->prev;
    handle->prev = handle->next = NULL;
}

/* A live handle with its script object, linked in the world. NULL on exception. */
static B2JSHandle *handle_new(JSContext *ctx, B2JSWorld *world, B2JSKind kind) {
    JSValue object = JS_NewObjectClass(ctx, b2js_class_ids[kind]);
    B2JSHandle *handle, **list = handle_list(world, kind);

    if (JS_IsException(object))
        return NULL;
    /* Non-writable, non-enumerable: keeps the world alive while the object is. */
    if (JS_DefinePropertyValueStr(ctx, object, "world", JS_DupValue(ctx, world->object), 0) < 0) {
        JS_FreeValue(ctx, object);
        return NULL;
    }
    handle = calloc(1, sizeof(*handle));
    if (!handle) {
        JS_FreeValue(ctx, object);
        JS_ThrowOutOfMemory(ctx);
        return NULL;
    }

    handle->kind = kind;
    handle->world = world;
    handle->object = object;
    handle->user_data = JS_UNDEFINED;
    handle->next = *list;
    if (*list)
        (*list)->prev = handle;
    *list = handle;
    JS_SetOpaque(object, handle);
    return handle;
}

void b2js_kill(JSRuntime *rt, B2JSHandle *handle) {
    JSValue object, user_data;

    if (handle->dead)
        return;
    handle_unlink(handle);
    handle->dead = true;
    handle->world = NULL;
    object = handle->object;
    user_data = handle->user_data;
    handle->object = JS_UNDEFINED;
    handle->user_data = JS_UNDEFINED;

    JS_FreeValueRT(rt, user_data);
    /* Last: when nothing else holds the object, its finalizer frees the handle. */
    JS_FreeValueRT(rt, object);
}

void b2js_set_user_data(JSContext *ctx, B2JSHandle *handle, JSValueConst value) {
    JSValue previous = handle->user_data;

    handle->user_data = JS_DupValue(ctx, value);
    JS_FreeValue(ctx, previous);
}

JSValue b2js_wrap_body(JSContext *ctx, B2JSWorld *world, b2BodyId id) {
    B2JSHandle *handle;

    if (!b2Body_IsValid(id))
        return JS_NULL;
    handle = b2Body_GetUserData(id);
    if (!handle) {
        handle = handle_new(ctx, world, B2JS_BODY);
        if (!handle)
            return JS_EXCEPTION;
        handle->id.body = id;
        b2Body_SetUserData(id, handle);
    }
    return JS_DupValue(ctx, handle->object);
}

JSValue b2js_wrap_shape(JSContext *ctx, B2JSWorld *world, b2ShapeId id) {
    B2JSHandle *handle;

    if (!b2Shape_IsValid(id))
        return JS_NULL;
    handle = b2Shape_GetUserData(id);
    if (!handle) {
        handle = handle_new(ctx, world, B2JS_SHAPE);
        if (!handle)
            return JS_EXCEPTION;
        handle->id.shape = id;
        b2Shape_SetUserData(id, handle);
    }
    return JS_DupValue(ctx, handle->object);
}

JSValue b2js_wrap_joint(JSContext *ctx, B2JSWorld *world, b2JointId id) {
    B2JSHandle *handle;

    if (!b2Joint_IsValid(id))
        return JS_NULL;
    handle = b2Joint_GetUserData(id);
    if (!handle) {
        handle = handle_new(ctx, world, B2JS_JOINT);
        if (!handle)
            return JS_EXCEPTION;
        handle->id.joint = id;
        b2Joint_SetUserData(id, handle);
    }
    return JS_DupValue(ctx, handle->object);
}

JSValue b2js_wrap_chain(JSContext *ctx, B2JSWorld *world, b2ChainId id) {
    B2JSHandle *handle;

    if (!b2Chain_IsValid(id))
        return JS_NULL;
    /* Box2D has no chain user data: chains have their own (short) list. */
    for (handle = world->chains; handle; handle = handle->next) {
        if (B2_ID_EQUALS(handle->id.chain, id))
            return JS_DupValue(ctx, handle->object);
    }
    handle = handle_new(ctx, world, B2JS_CHAIN);
    if (!handle)
        return JS_EXCEPTION;
    handle->id.chain = id;
    return JS_DupValue(ctx, handle->object);
}

int b2js_kill_body_attachments(JSContext *ctx, b2BodyId body) {
    JSRuntime *rt = JS_GetRuntime(ctx);
    b2ShapeId shape_buffer[16];
    b2JointId joint_buffer[16];
    int shape_count = b2Body_GetShapeCount(body);
    int joint_count = b2Body_GetJointCount(body);
    b2ShapeId *shapes = shape_buffer;
    b2JointId *joints = joint_buffer;

    if (shape_count > (int)countof(shape_buffer))
        shapes = malloc(sizeof(*shapes) * (size_t)shape_count);
    if (joint_count > (int)countof(joint_buffer))
        joints = malloc(sizeof(*joints) * (size_t)joint_count);
    if (!shapes || !joints) {
        if (shapes != shape_buffer) free(shapes);
        if (joints != joint_buffer) free(joints);
        JS_ThrowOutOfMemory(ctx);
        return -1;
    }

    /* Chain segments are part of the body's shapes. */
    shape_count = b2Body_GetShapes(body, shapes, shape_count);
    joint_count = b2Body_GetJoints(body, joints, joint_count);
    for (int i = 0; i < shape_count; i++) {
        B2JSHandle *handle = b2Shape_GetUserData(shapes[i]);
        if (handle)
            b2js_kill(rt, handle);
    }
    for (int i = 0; i < joint_count; i++) {
        B2JSHandle *handle = b2Joint_GetUserData(joints[i]);
        if (handle)
            b2js_kill(rt, handle);
    }

    if (shapes != shape_buffer) free(shapes);
    if (joints != joint_buffer) free(joints);
    return 0;
}

void b2js_kill_stale_chains(JSRuntime *rt, B2JSWorld *world) {
    B2JSHandle *handle, *next;

    for (handle = world->chains; handle; handle = next) {
        next = handle->next;
        if (!b2Chain_IsValid(handle->id.chain))
            b2js_kill(rt, handle);
    }
}

/* Finalizers of Body/Shape/Joint/Chain objects. A live handle belongs to the
 * world (this only happens when the GC collects a world with its objects). */
#define B2JS_FINALIZER(kind)                                                    \
    static void b2js_finalizer_##kind(JSRuntime *rt, JSValue value) {            \
        B2JSHandle *handle = JS_GetOpaque(value, b2js_class_ids[kind]);          \
        if (handle && handle->dead)                                              \
            free(handle);                                                        \
    }

B2JS_FINALIZER(B2JS_BODY)
B2JS_FINALIZER(B2JS_SHAPE)
B2JS_FINALIZER(B2JS_JOINT)
B2JS_FINALIZER(B2JS_CHAIN)

static const JSClassDef b2js_classes[B2JS_KIND_COUNT] = {
    { "Body", .finalizer = b2js_finalizer_B2JS_BODY },
    { "Shape", .finalizer = b2js_finalizer_B2JS_SHAPE },
    { "Joint", .finalizer = b2js_finalizer_B2JS_JOINT },
    { "Chain", .finalizer = b2js_finalizer_B2JS_CHAIN },
};

/* ------------------------------------------------------------------------ */
/* World                                                                     */
/* ------------------------------------------------------------------------ */

static void world_gc_mark(JSRuntime *rt, JSValueConst value, JS_MarkFunc *mark_func) {
    B2JSWorld *world = JS_GetOpaque(value, b2js_world_class_id);

    if (!world)
        return;
    JS_MarkValue(rt, world->user_data, mark_func);
    for (int list = 0; list < 2; list++) {
        for (B2JSHandle *handle = list ? world->chains : world->handles; handle; handle = handle->next) {
            JS_MarkValue(rt, handle->object, mark_func);
            JS_MarkValue(rt, handle->user_data, mark_func);
        }
    }
}

/* Kills every live handle: the world is destroyed or its content replaced. */
static void world_kill_all(JSRuntime *rt, B2JSWorld *world) {
    /* Pops the head: killing may run finalizers, never on this world's lists. */
    while (world->handles)
        b2js_kill(rt, world->handles);
    while (world->chains)
        b2js_kill(rt, world->chains);
}

/* World.destroy(): every object becomes "destroyed", the world stays usable for isValid(). */
static void world_destroy(JSRuntime *rt, B2JSWorld *world) {
    JSValue user_data;

    world_kill_all(rt, world);
    b2DestroyWorld(world->id);
    world->id = b2_nullWorldId;
    user_data = world->user_data;
    world->user_data = JS_UNDEFINED;
    JS_FreeValueRT(rt, user_data);
}

static void world_finalizer(JSRuntime *rt, JSValue value) {
    B2JSWorld *world = JS_GetOpaque(value, b2js_world_class_id);

    if (!world)
        return;
    /* Live handles only remain when the GC collects the world together with
     * its objects: their finalizers may already have run, so the handles are
     * freed here and detached from objects not finalized yet. */
    for (int list = 0; list < 2; list++) {
        B2JSHandle **head = list ? &world->chains : &world->handles;
        while (*head) {
            B2JSHandle *handle = *head;
            *head = handle->next;
            JS_SetOpaque(handle->object, NULL);
            JS_FreeValueRT(rt, handle->user_data);
            JS_FreeValueRT(rt, handle->object);
            free(handle);
        }
    }
    if (b2World_IsValid(world->id))
        b2DestroyWorld(world->id);
    JS_FreeValueRT(rt, world->user_data);
    free(world);
}

static const JSClassDef b2js_world_class = {
    "World",
    .finalizer = world_finalizer,
    .gc_mark = world_gc_mark,
};

B2JSWorld *b2js_this_world(JSContext *ctx, JSValueConst this_val, const char *where) {
    B2JSWorld *world = JS_GetOpaque(this_val, b2js_world_class_id);

    if (!world) {
        JS_ThrowTypeError(ctx, "%s: expected a Box2D World", where);
        return NULL;
    }
    if (B2_IS_NULL(world->id)) {
        JS_ThrowTypeError(ctx, "%s: the World has been destroyed", where);
        return NULL;
    }
    return world;
}

b2WorldId athena_box2d_js_world(JSContext *ctx, JSValueConst value, const char *where) {
    B2JSWorld *world = b2js_this_world(ctx, value, where);
    return world ? world->id : b2_nullWorldId;
}

static int world_def(JSContext *ctx, JSValueConst options, b2WorldDef *def, JSValue *user_data) {
    static const char where[] = "Box2D.createWorld options";
    int workers = 1;

    if (b2js_opt_vec2(ctx, options, "gravity", where, &def->gravity) < 0 ||
        b2js_opt_bool(ctx, options, "enableSleep", where, &def->enableSleep) < 0 ||
        b2js_opt_bool(ctx, options, "enableContinuous", where, &def->enableContinuous) < 0 ||
        b2js_opt_float(ctx, options, "restitutionThreshold", where, B2JS_NONNEG,
            &def->restitutionThreshold) < 0 ||
        b2js_opt_float(ctx, options, "hitEventThreshold", where, B2JS_NONNEG, &def->hitEventThreshold) < 0 ||
        b2js_opt_float(ctx, options, "maximumLinearSpeed", where, B2JS_POS, &def->maximumLinearSpeed) < 0 ||
        b2js_opt_float(ctx, options, "contactHertz", where, B2JS_NONNEG, &def->contactHertz) < 0 ||
        b2js_opt_float(ctx, options, "contactDampingRatio", where, B2JS_NONNEG, &def->contactDampingRatio) < 0 ||
        b2js_opt_float(ctx, options, "contactSpeed", where, B2JS_NONNEG, &def->contactSpeed) < 0 ||
        b2js_opt_bool(ctx, options, "enableContactSoftening", where, &def->enableContactSoftening) < 0 ||
        /* Validated for compatibility; the EE runs every world on one worker. */
        b2js_opt_int(ctx, options, "workerCount", where, 1, B2_MAX_WORKERS, &workers) < 0)
        return -1;

    *user_data = JS_GetPropertyStr(ctx, options, "userData");
    return JS_IsException(*user_data) ? -1 : 0;
}

static JSValue b2js_create_world(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Box2D.createWorld";
    b2WorldDef def = b2DefaultWorldDef();
    JSValue user_data = JS_UNDEFINED;
    JSValue object;
    B2JSWorld *world;
    int has_options;

    if (b2js_argc(ctx, argc, 0, 1, where) < 0)
        return JS_EXCEPTION;
    has_options = b2js_options(ctx, argc, argv, 0, where);
    if (has_options < 0 || (has_options && world_def(ctx, argv[0], &def, &user_data) < 0))
        return JS_EXCEPTION;
    if (b2js_check_memory(ctx, where) < 0) {
        JS_FreeValue(ctx, user_data);
        return JS_EXCEPTION;
    }
    def.workerCount = 1;
    def.enqueueTask = NULL;
    def.finishTask = NULL;

    world = calloc(1, sizeof(*world));
    if (!world) {
        JS_FreeValue(ctx, user_data);
        return JS_ThrowOutOfMemory(ctx);
    }
    object = JS_NewObjectClass(ctx, b2js_world_class_id);
    if (JS_IsException(object)) {
        free(world);
        JS_FreeValue(ctx, user_data);
        return object;
    }

    world->id = b2CreateWorld(&def);
    if (B2_IS_NULL(world->id)) {
        /* Every slot taken: unreachable worlds may be waiting for the GC. */
        JS_RunGC(JS_GetRuntime(ctx));
        world->id = b2CreateWorld(&def);
    }
    world->object = object;
    world->user_data = user_data;
    JS_SetOpaque(object, world);
    if (B2_IS_NULL(world->id)) {
        JS_FreeValue(ctx, object);
        return JS_ThrowRangeError(ctx, "%s: too many worlds (at most %d at a time); destroy() unused worlds",
            where, B2_MAX_WORLDS);
    }
    return object;
}

static JSValue world_step(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.step";
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);
    float time_step = 1.0f / 60.0f;
    int sub_steps = 4;

    if (!world || b2js_argc(ctx, argc, 0, 2, where) < 0)
        return JS_EXCEPTION;
    if ((b2js_has(argc, argv, 0) &&
            b2js_float(ctx, argv[0], where, "timeStep", B2JS_NONNEG, &time_step) < 0) ||
        (b2js_has(argc, argv, 1) &&
            b2js_int(ctx, argv[1], where, "subStepCount", 1, B2JS_MAX_SUB_STEPS, &sub_steps) < 0))
        return JS_EXCEPTION;
    /*
     * A step allocates too (new contacts, islands, event arrays), and Box2D
     * cannot recover from a failed allocation: refuse to step once the budget
     * is spent, leaving the world as it is, instead of risking the crash screen.
     */
    if (b2js_check_memory(ctx, where) < 0)
        return JS_EXCEPTION;
    b2World_Step(world->id, time_step, sub_steps);
    return JS_UNDEFINED;
}

/* name: buffer for options.name, which def->name points to when given. */
static int body_def(JSContext *ctx, JSValueConst options, b2BodyDef *def, JSValue *user_data,
    char name[B2_NAME_LENGTH + 1]) {
    static const char where[] = "World.createBody options";
    int type = def->type;
    float angle = 0.0f;
    bool fixed_rotation = def->motionLocks.angularZ;
    int has_angle;
    JSValue value;

    if (b2js_opt_int(ctx, options, "type", where, b2_staticBody, b2_dynamicBody, &type) < 0 ||
        b2js_opt_vec2(ctx, options, "position", where, &def->position) < 0 ||
        b2js_opt_vec2(ctx, options, "linearVelocity", where, &def->linearVelocity) < 0 ||
        b2js_opt_float(ctx, options, "angularVelocity", where, B2JS_ANY, &def->angularVelocity) < 0 ||
        b2js_opt_float(ctx, options, "linearDamping", where, B2JS_NONNEG, &def->linearDamping) < 0 ||
        b2js_opt_float(ctx, options, "angularDamping", where, B2JS_NONNEG, &def->angularDamping) < 0 ||
        b2js_opt_float(ctx, options, "gravityScale", where, B2JS_ANY, &def->gravityScale) < 0 ||
        b2js_opt_float(ctx, options, "sleepThreshold", where, B2JS_NONNEG, &def->sleepThreshold) < 0 ||
        b2js_opt_bool(ctx, options, "fixedRotation", where, &fixed_rotation) < 0 ||
        b2js_opt_bool(ctx, options, "lockLinearX", where, &def->motionLocks.linearX) < 0 ||
        b2js_opt_bool(ctx, options, "lockLinearY", where, &def->motionLocks.linearY) < 0 ||
        b2js_opt_bool(ctx, options, "isBullet", where, &def->isBullet) < 0 ||
        b2js_opt_bool(ctx, options, "enableSleep", where, &def->enableSleep) < 0 ||
        b2js_opt_bool(ctx, options, "isAwake", where, &def->isAwake) < 0 ||
        b2js_opt_bool(ctx, options, "isEnabled", where, &def->isEnabled) < 0 ||
        b2js_opt_bool(ctx, options, "allowFastRotation", where, &def->allowFastRotation) < 0 ||
        b2js_opt_bool(ctx, options, "enableContactRecycling", where, &def->enableContactRecycling) < 0)
        return -1;

    value = JS_GetPropertyStr(ctx, options, "name");
    if (JS_IsException(value))
        return -1;
    if (!JS_IsUndefined(value)) {
        int ret = b2js_name(ctx, value, where, name);
        JS_FreeValue(ctx, value);
        if (ret < 0)
            return -1;
        def->name = name;
    }

    has_angle = b2js_opt_float(ctx, options, "angle", where, B2JS_ANY, &angle);
    if (has_angle == 0)
        has_angle = b2js_opt_float(ctx, options, "rotation", where, B2JS_ANY, &angle);
    if (has_angle < 0)
        return -1;

    def->type = (b2BodyType)type;
    def->rotation = athena_box2d_make_rot(angle);
    def->motionLocks.angularZ = fixed_rotation;
    *user_data = JS_GetPropertyStr(ctx, options, "userData");
    return JS_IsException(*user_data) ? -1 : 0;
}

static JSValue world_create_body(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.createBody";
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);
    b2BodyDef def = b2DefaultBodyDef();
    JSValue user_data = JS_UNDEFINED;
    JSValue body;
    b2BodyId id;
    char name[B2_NAME_LENGTH + 1];
    int has_options;

    if (!world || b2js_argc(ctx, argc, 0, 1, where) < 0)
        return JS_EXCEPTION;
    has_options = b2js_options(ctx, argc, argv, 0, where);
    if (has_options < 0 || (has_options && body_def(ctx, argv[0], &def, &user_data, name) < 0))
        return JS_EXCEPTION;
    if (b2js_world_still_alive(ctx, world, where) < 0 || b2js_check_memory(ctx, where) < 0) {
        JS_FreeValue(ctx, user_data);
        return JS_EXCEPTION;
    }

    id = b2CreateBody(world->id, &def);
    body = b2js_wrap_body(ctx, world, id);
    if (JS_IsException(body)) {
        b2DestroyBody(id);
    } else if (!JS_IsUndefined(user_data)) {
        b2js_set_user_data(ctx, b2Body_GetUserData(id), user_data);
    }
    JS_FreeValue(ctx, user_data);
    return body;
}

static JSValue world_destroy_js(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    B2JSWorld *world = JS_GetOpaque(this_val, b2js_world_class_id);

    if (b2js_argc(ctx, argc, 0, 0, "World.destroy") < 0)
        return JS_EXCEPTION;
    if (!world)
        return JS_ThrowTypeError(ctx, "World.destroy: expected a Box2D World");
    if (B2_IS_NULL(world->id))
        return JS_FALSE;
    world_destroy(JS_GetRuntime(ctx), world);
    return JS_TRUE;
}

static JSValue world_is_valid(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    B2JSWorld *world = JS_GetOpaque(this_val, b2js_world_class_id);

    if (b2js_argc(ctx, argc, 0, 0, "World.isValid") < 0)
        return JS_EXCEPTION;
    return JS_NewBool(ctx, world && b2World_IsValid(world->id));
}

static JSValue world_get_gravity(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    B2JSWorld *world = b2js_this_world(ctx, this_val, "World.getGravity");

    if (!world || b2js_argc(ctx, argc, 0, 0, "World.getGravity") < 0)
        return JS_EXCEPTION;
    return b2js_new_vec2(ctx, b2World_GetGravity(world->id));
}

static JSValue world_set_gravity(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.setGravity";
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);
    b2Vec2 gravity;

    if (!world || b2js_argc(ctx, argc, 2, 2, where) < 0 ||
        b2js_float(ctx, argv[0], where, "x", B2JS_COORD, &gravity.x) < 0 ||
        b2js_float(ctx, argv[1], where, "y", B2JS_COORD, &gravity.y) < 0)
        return JS_EXCEPTION;
    b2World_SetGravity(world->id, gravity);
    return JS_UNDEFINED;
}

/* Scalar world settings, selected by magic. */
enum {
    WORLD_CONTINUOUS = 0,
    WORLD_SLEEPING,
    WORLD_RESTITUTION_THRESHOLD,
    WORLD_HIT_EVENT_THRESHOLD,
    WORLD_MAXIMUM_LINEAR_SPEED,
    WORLD_AWAKE_BODY_COUNT,
    WORLD_WARM_STARTING,
    WORLD_SPECULATIVE,
    WORLD_CONTACT_RECYCLE_DISTANCE,
};

static const struct {
    const char *get;
    const char *set;
    B2JSRange range; /* setters of numbers */
} world_settings[] = {
    [WORLD_CONTINUOUS] = { "World.isContinuousEnabled", "World.enableContinuous", B2JS_ANY },
    [WORLD_SLEEPING] = { "World.isSleepingEnabled", "World.enableSleeping", B2JS_ANY },
    [WORLD_WARM_STARTING] = { "World.isWarmStartingEnabled", "World.enableWarmStarting", B2JS_ANY },
    /* Box2D has no getter for it. */
    [WORLD_SPECULATIVE] = { NULL, "World.enableSpeculative", B2JS_ANY },
    [WORLD_CONTACT_RECYCLE_DISTANCE] = { "World.getContactRecycleDistance", "World.setContactRecycleDistance",
        B2JS_NONNEG },
    [WORLD_RESTITUTION_THRESHOLD] = { "World.getRestitutionThreshold", "World.setRestitutionThreshold", B2JS_NONNEG },
    [WORLD_HIT_EVENT_THRESHOLD] = { "World.getHitEventThreshold", "World.setHitEventThreshold", B2JS_NONNEG },
    [WORLD_MAXIMUM_LINEAR_SPEED] = { "World.getMaximumLinearSpeed", "World.setMaximumLinearSpeed", B2JS_POS },
    [WORLD_AWAKE_BODY_COUNT] = { "World.getAwakeBodyCount", NULL, B2JS_ANY },
};

static JSValue world_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    const char *where = world_settings[magic].get;
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);

    if (!world || b2js_argc(ctx, argc, 0, 0, where) < 0)
        return JS_EXCEPTION;
    switch (magic) {
        case WORLD_CONTINUOUS: return JS_NewBool(ctx, b2World_IsContinuousEnabled(world->id));
        case WORLD_SLEEPING: return JS_NewBool(ctx, b2World_IsSleepingEnabled(world->id));
        case WORLD_RESTITUTION_THRESHOLD: return JS_NewFloat32(ctx, b2World_GetRestitutionThreshold(world->id));
        case WORLD_HIT_EVENT_THRESHOLD: return JS_NewFloat32(ctx, b2World_GetHitEventThreshold(world->id));
        case WORLD_MAXIMUM_LINEAR_SPEED: return JS_NewFloat32(ctx, b2World_GetMaximumLinearSpeed(world->id));
        case WORLD_AWAKE_BODY_COUNT: return JS_NewInt32(ctx, b2World_GetAwakeBodyCount(world->id));
        case WORLD_WARM_STARTING: return JS_NewBool(ctx, b2World_IsWarmStartingEnabled(world->id));
        case WORLD_CONTACT_RECYCLE_DISTANCE:
            return JS_NewFloat32(ctx, b2World_GetContactRecycleDistance(world->id));
        default: return JS_UNDEFINED;
    }
}

static JSValue world_set(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    const char *where = world_settings[magic].set;
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);
    bool flag;
    float value;

    if (!world || b2js_argc(ctx, argc, 1, 1, where) < 0)
        return JS_EXCEPTION;
    if (magic == WORLD_CONTINUOUS || magic == WORLD_SLEEPING || magic == WORLD_WARM_STARTING ||
        magic == WORLD_SPECULATIVE) {
        if (b2js_bool(ctx, argv[0], where, "flag", &flag) < 0)
            return JS_EXCEPTION;
        switch (magic) {
            case WORLD_CONTINUOUS: b2World_EnableContinuous(world->id, flag); break;
            case WORLD_SLEEPING: b2World_EnableSleeping(world->id, flag); break;
            case WORLD_WARM_STARTING: b2World_EnableWarmStarting(world->id, flag); break;
            default: b2World_EnableSpeculative(world->id, flag); break;
        }
        return JS_UNDEFINED;
    }
    if (b2js_float(ctx, argv[0], where, "value", world_settings[magic].range, &value) < 0)
        return JS_EXCEPTION;
    switch (magic) {
        case WORLD_RESTITUTION_THRESHOLD: b2World_SetRestitutionThreshold(world->id, value); break;
        case WORLD_HIT_EVENT_THRESHOLD: b2World_SetHitEventThreshold(world->id, value); break;
        case WORLD_MAXIMUM_LINEAR_SPEED: b2World_SetMaximumLinearSpeed(world->id, value); break;
        case WORLD_CONTACT_RECYCLE_DISTANCE: b2World_SetContactRecycleDistance(world->id, value); break;
    }
    return JS_UNDEFINED;
}

static JSValue world_get_user_data(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    B2JSWorld *world = b2js_this_world(ctx, this_val, "World.getUserData");

    if (!world || b2js_argc(ctx, argc, 0, 0, "World.getUserData") < 0)
        return JS_EXCEPTION;
    return JS_DupValue(ctx, world->user_data);
}

static JSValue world_set_user_data(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    B2JSWorld *world = b2js_this_world(ctx, this_val, "World.setUserData");
    JSValue previous;

    if (!world || b2js_argc(ctx, argc, 1, 1, "World.setUserData") < 0)
        return JS_EXCEPTION;
    previous = world->user_data;
    world->user_data = JS_DupValue(ctx, argv[0]);
    JS_FreeValue(ctx, previous);
    return JS_UNDEFINED;
}

/* World.setContactTuning(hertz, dampingRatio, pushSpeed): contact softness (advanced). */
static JSValue world_set_contact_tuning(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.setContactTuning";
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);
    float hertz, damping, push_speed;

    if (!world || b2js_argc(ctx, argc, 3, 3, where) < 0 ||
        b2js_float(ctx, argv[0], where, "hertz", B2JS_NONNEG, &hertz) < 0 ||
        b2js_float(ctx, argv[1], where, "dampingRatio", B2JS_NONNEG, &damping) < 0 ||
        b2js_float(ctx, argv[2], where, "pushSpeed", B2JS_NONNEG, &push_speed) < 0)
        return JS_EXCEPTION;
    b2World_SetContactTuning(world->id, hertz, damping, push_speed);
    return JS_UNDEFINED;
}

/* World.rebuildStaticTree(): rebalances the tree of static shapes after many were created or moved. */
static JSValue world_rebuild_static_tree(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.rebuildStaticTree";
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);

    if (!world || b2js_argc(ctx, argc, 0, 0, where) < 0)
        return JS_EXCEPTION;
    b2World_RebuildStaticTree(world->id);
    return JS_UNDEFINED;
}

/* World.getCounters(): sizes of the simulation. */
static JSValue world_get_counters(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
#define COUNTER_FIELD(name) { #name, offsetof(b2Counters, name) }
    static const struct { const char *name; size_t offset; } fields[] = {
        COUNTER_FIELD(bodyCount), COUNTER_FIELD(shapeCount), COUNTER_FIELD(contactCount),
        COUNTER_FIELD(jointCount), COUNTER_FIELD(islandCount), COUNTER_FIELD(stackUsed),
        COUNTER_FIELD(staticTreeHeight), COUNTER_FIELD(treeHeight), COUNTER_FIELD(taskCount),
        COUNTER_FIELD(awakeContactCount), COUNTER_FIELD(recycledContactCount),
    };
#undef COUNTER_FIELD
    static const char where[] = "World.getCounters";
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);
    b2Counters counters;
    JSValue object, colors;

    if (!world || b2js_argc(ctx, argc, 0, 0, where) < 0)
        return JS_EXCEPTION;
    counters = b2World_GetCounters(world->id);
    object = JS_NewObject(ctx);
    if (JS_IsException(object))
        return object;
    for (size_t i = 0; i < countof(fields); i++) {
        int value = *(const int *)((const char *)&counters + fields[i].offset);
        JS_SetPropertyStr(ctx, object, fields[i].name, JS_NewInt32(ctx, value));
    }
    JS_SetPropertyStr(ctx, object, "byteCount", JS_NewInt64(ctx, counters.byteCount));
    /* Constraints per graph color: how well the solver can batch them. */
    colors = JS_NewArray(ctx);
    for (int i = 0; i < (int)countof(counters.colorCounts) && !JS_IsException(colors); i++)
        JS_SetPropertyUint32(ctx, colors, (uint32_t)i, JS_NewInt32(ctx, counters.colorCounts[i]));
    JS_SetPropertyStr(ctx, object, "colorCounts", colors);
    return object;
}

/* ------------------------------------------------------------------------ */
/* Batch reads, profile, snapshots                                           */
/* ------------------------------------------------------------------------ */

/*
 * value is a Float32Array: -1 with a TypeError otherwise. Int32Array and
 * Uint32Array have 4-byte elements too, so prototypes are compared. Reading
 * the global Float32Array may run script code.
 */
static int check_float32_array(JSContext *ctx, JSValueConst value, const char *where, const char *what) {
    JSValue global, constructor, proto, own;
    bool is_float32;

    if (JS_IsObject(value)) {
        global = JS_GetGlobalObject(ctx);
        constructor = JS_GetPropertyStr(ctx, global, "Float32Array");
        JS_FreeValue(ctx, global);
        if (JS_IsException(constructor))
            return -1;
        proto = JS_GetPropertyStr(ctx, constructor, "prototype");
        JS_FreeValue(ctx, constructor);
        if (JS_IsException(proto))
            return -1;
        own = JS_GetPrototype(ctx, value);
        if (JS_IsException(own)) {
            JS_FreeValue(ctx, proto);
            return -1;
        }
        is_float32 = JS_IsObject(proto) && JS_VALUE_GET_PTR(own) == JS_VALUE_GET_PTR(proto);
        JS_FreeValue(ctx, own);
        JS_FreeValue(ctx, proto);
        if (is_float32)
            return 0;
    }
    JS_ThrowTypeError(ctx, "%s: %s must be a Float32Array", where, what);
    return -1;
}

/*
 * Floats of a typed array checked by check_float32_array; runs no script
 * code. *data may be NULL when *count is 0. -1 with an exception set.
 */
static int float32_data(JSContext *ctx, JSValueConst value, const char *where, const char *what,
    float **data, size_t *count) {
    size_t offset, length, element, size = 0;
    JSValue buffer = JS_GetTypedArrayBuffer(ctx, value, &offset, &length, &element);
    uint8_t *bytes;

    if (JS_IsException(buffer))
        return -1;
    if (element != sizeof(float)) {
        JS_FreeValue(ctx, buffer);
        JS_ThrowTypeError(ctx, "%s: %s must be a Float32Array", where, what);
        return -1;
    }
    if (length == 0) {
        JS_FreeValue(ctx, buffer);
        *data = NULL;
        *count = 0;
        return 0;
    }
    bytes = JS_GetArrayBuffer(ctx, &size, buffer);
    JS_FreeValue(ctx, buffer);
    if (!bytes)
        return -1; /* detached: JS_GetArrayBuffer threw */
    if (offset + length > size) {
        JS_ThrowRangeError(ctx, "%s: %s is out of its buffer", where, what);
        return -1;
    }
    *data = (float *)(bytes + offset);
    *count = length / sizeof(float);
    return 0;
}

/*
 * World.readTransforms(bodies, out): x, y, angle of each body into out[3i..3i+2].
 * One call per frame instead of an object per body.
 */
static JSValue world_read_transforms(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.readTransforms";
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);
    b2BodyId stack_ids[64], *ids = stack_ids;
    int64_t length = 0;
    size_t capacity;
    float *out;
    int is_array;
    JSValue value;

    if (!world || b2js_argc(ctx, argc, 2, 2, where) < 0 ||
        check_float32_array(ctx, argv[1], where, "out") < 0)
        return JS_EXCEPTION;
    is_array = JS_IsArray(ctx, argv[0]);
    if (is_array < 0)
        return JS_EXCEPTION;
    if (!is_array)
        return JS_ThrowTypeError(ctx, "%s: bodies must be an array of Body", where);
    value = JS_GetPropertyStr(ctx, argv[0], "length");
    if (JS_IsException(value) || JS_ToInt64(ctx, &length, value) < 0) {
        JS_FreeValue(ctx, value);
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, value);
    if (length > 65536)
        return JS_ThrowRangeError(ctx, "%s: at most 65536 bodies per call", where);
    if (length > (int64_t)countof(stack_ids)) {
        ids = malloc(sizeof(*ids) * (size_t)length);
        if (!ids)
            return JS_ThrowOutOfMemory(ctx);
    }

    /* Ids first: reading elements may run script code, which could destroy
     * bodies or detach the output buffer. Everything is re-checked after,
     * and the buffer pointer taken last, by code that runs no script. */
    for (int64_t i = 0; i < length; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, argv[0], (uint32_t)i);
        B2JSHandle *handle = JS_IsException(item) ? NULL : b2js_handle(ctx, item, B2JS_BODY, where);

        JS_FreeValue(ctx, item);
        if (!handle)
            goto fail;
        if (handle->world != world) {
            JS_ThrowTypeError(ctx, "%s: bodies[%d] belongs to another World", where, (int)i);
            goto fail;
        }
        ids[i] = handle->id.body;
    }

    /* A getter may also have destroyed a body collected before it. */
    if (b2js_world_still_alive(ctx, world, where) < 0)
        goto fail;
    for (int64_t i = 0; i < length; i++) {
        if (!b2Body_IsValid(ids[i])) {
            JS_ThrowTypeError(ctx, "%s: bodies[%d] was destroyed while the array was read", where, (int)i);
            goto fail;
        }
    }

    if (float32_data(ctx, argv[1], where, "out", &out, &capacity) < 0)
        goto fail;
    if (capacity < (size_t)length * 3) {
        JS_ThrowRangeError(ctx, "%s: out needs %d floats (3 per body)", where, (int)length * 3);
        goto fail;
    }
    for (int64_t i = 0; i < length; i++) {
        b2Transform transform = b2Body_GetTransform(ids[i]);
        out[3 * i] = transform.p.x;
        out[3 * i + 1] = transform.p.y;
        out[3 * i + 2] = b2Rot_GetAngle(transform.q);
    }
    if (ids != stack_ids)
        free(ids);
    return JS_NewInt32(ctx, (int)length);

fail:
    if (ids != stack_ids)
        free(ids);
    return JS_EXCEPTION;
}

/* World.getProfile(): milliseconds spent in each phase of the last step. */
static JSValue world_get_profile(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
#define PROFILE_FIELD(name) { #name, offsetof(b2Profile, name) }
    static const struct { const char *name; size_t offset; } fields[] = {
        PROFILE_FIELD(step), PROFILE_FIELD(pairs), PROFILE_FIELD(collide), PROFILE_FIELD(solve),
        PROFILE_FIELD(solverSetup), PROFILE_FIELD(constraints), PROFILE_FIELD(prepareConstraints),
        PROFILE_FIELD(integrateVelocities), PROFILE_FIELD(warmStart), PROFILE_FIELD(solveImpulses),
        PROFILE_FIELD(integratePositions), PROFILE_FIELD(relaxImpulses), PROFILE_FIELD(applyRestitution),
        PROFILE_FIELD(storeImpulses), PROFILE_FIELD(splitIslands), PROFILE_FIELD(transforms),
        PROFILE_FIELD(sensorHits), PROFILE_FIELD(jointEvents), PROFILE_FIELD(hitEvents), PROFILE_FIELD(refit),
        PROFILE_FIELD(bullets), PROFILE_FIELD(sleepIslands), PROFILE_FIELD(sensors),
    };
#undef PROFILE_FIELD
    static const char where[] = "World.getProfile";
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);
    b2Profile profile;
    JSValue object;

    if (!world || b2js_argc(ctx, argc, 0, 0, where) < 0)
        return JS_EXCEPTION;
    profile = b2World_GetProfile(world->id);
    object = JS_NewObject(ctx);
    if (JS_IsException(object))
        return object;
    for (size_t i = 0; i < countof(fields); i++) {
        float ms = *(const float *)((const char *)&profile + fields[i].offset);
        JS_SetPropertyStr(ctx, object, fields[i].name, JS_NewFloat32(ctx, ms));
    }
    return object;
}

static void snapshot_free(JSRuntime *rt, void *opaque, void *data) {
    (void)rt;
    (void)opaque;
    free(data);
}

static JSValue world_snapshot(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.snapshot";
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);
    uint8_t *image;
    JSValue buffer;
    int size = 0;

    if (!world || b2js_argc(ctx, argc, 0, 0, where) < 0)
        return JS_EXCEPTION;
    image = athena_box2d_snapshot(world->id, &size);
    if (!image)
        return JS_ThrowOutOfMemory(ctx);
    buffer = JS_NewArrayBuffer(ctx, image, (size_t)size, snapshot_free, NULL, false);
    /* QuickJS does not call snapshot_free when the object cannot be created. */
    if (JS_IsException(buffer))
        free(image);
    return buffer;
}

/* Bytes of an ArrayBuffer or typed array; NULL with a TypeError otherwise. */
static const uint8_t *byte_view(JSContext *ctx, JSValueConst value, const char *where, size_t *size) {
    size_t offset, length, element, total;
    const uint8_t *data;
    JSValue buffer;

    if (JS_IsObject(value)) {
        data = JS_GetArrayBuffer(ctx, size, value);
        if (data)
            return data;
        JS_FreeValue(ctx, JS_GetException(ctx));
        buffer = JS_GetTypedArrayBuffer(ctx, value, &offset, &length, &element);
        if (!JS_IsException(buffer)) {
            data = JS_GetArrayBuffer(ctx, &total, buffer);
            JS_FreeValue(ctx, buffer);
            if (data && offset + length <= total) {
                *size = length;
                return data + offset;
            }
        }
        JS_FreeValue(ctx, JS_GetException(ctx));
    }
    JS_ThrowTypeError(ctx, "%s: expected an ArrayBuffer or typed array from World.snapshot()", where);
    return NULL;
}

/*
 * World.restore(image): the world returns to the snapshot. Objects that
 * existed then keep their script objects (user data included); objects
 * created since become "destroyed"; objects destroyed since come back with
 * new script objects.
 */
static JSValue world_restore(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.restore";
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);
    JSRuntime *rt = JS_GetRuntime(ctx);
    const uint8_t *image;
    B2JSHandle *handle, *next;
    size_t size = 0;
    int result;

    if (!world || b2js_argc(ctx, argc, 1, 1, where) < 0)
        return JS_EXCEPTION;
    image = byte_view(ctx, argv[0], where, &size);
    if (!image)
        return JS_EXCEPTION;
    if (size > INT32_MAX)
        return JS_ThrowRangeError(ctx, "%s: image too large", where);

    result = athena_box2d_restore(world->id, image, (int)size);
    if (result == ATHENA_BOX2D_RESTORE_REJECTED)
        return JS_ThrowRangeError(ctx, "%s: not a World.snapshot() image of this build", where);
    if (result == ATHENA_BOX2D_RESTORE_DAMAGED)
        return JS_ThrowRangeError(ctx, "%s: the image is damaged (checksum mismatch)", where);
    if (result == ATHENA_BOX2D_RESTORE_FAILED) {
        world_destroy(rt, world);
        return JS_ThrowInternalError(ctx, "%s: the image failed midway; the World was destroyed", where);
    }

    /* Box2D clears user data on restore: reattach the handles whose ids
     * still resolve, kill the others (their objects did not exist then). */
    for (handle = world->handles; handle; handle = next) {
        next = handle->next;
        switch (handle->kind) {
        case B2JS_BODY:
            if (b2Body_IsValid(handle->id.body)) { b2Body_SetUserData(handle->id.body, handle); continue; }
            break;
        case B2JS_SHAPE:
            if (b2Shape_IsValid(handle->id.shape)) { b2Shape_SetUserData(handle->id.shape, handle); continue; }
            break;
        case B2JS_JOINT:
            if (b2Joint_IsValid(handle->id.joint)) { b2Joint_SetUserData(handle->id.joint, handle); continue; }
            break;
        default:
            break;
        }
        b2js_kill(rt, handle);
    }
    b2js_kill_stale_chains(rt, world);
    return JS_TRUE;
}

static const JSCFunctionListEntry world_proto_funcs[] = {
    JS_CFUNC_DEF("readTransforms", 2, world_read_transforms),
    JS_CFUNC_DEF("getProfile", 0, world_get_profile),
    JS_CFUNC_DEF("snapshot", 0, world_snapshot),
    JS_CFUNC_DEF("restore", 1, world_restore),
    JS_CFUNC_DEF("step", 2, world_step),
    JS_CFUNC_DEF("createBody", 1, world_create_body),
    JS_CFUNC_DEF("getGravity", 0, world_get_gravity),
    JS_CFUNC_DEF("setGravity", 2, world_set_gravity),
    JS_CFUNC_MAGIC_DEF("isContinuousEnabled", 0, world_get, WORLD_CONTINUOUS),
    JS_CFUNC_MAGIC_DEF("enableContinuous", 1, world_set, WORLD_CONTINUOUS),
    JS_CFUNC_MAGIC_DEF("isSleepingEnabled", 0, world_get, WORLD_SLEEPING),
    JS_CFUNC_MAGIC_DEF("enableSleeping", 1, world_set, WORLD_SLEEPING),
    JS_CFUNC_MAGIC_DEF("getRestitutionThreshold", 0, world_get, WORLD_RESTITUTION_THRESHOLD),
    JS_CFUNC_MAGIC_DEF("setRestitutionThreshold", 1, world_set, WORLD_RESTITUTION_THRESHOLD),
    JS_CFUNC_MAGIC_DEF("getHitEventThreshold", 0, world_get, WORLD_HIT_EVENT_THRESHOLD),
    JS_CFUNC_MAGIC_DEF("setHitEventThreshold", 1, world_set, WORLD_HIT_EVENT_THRESHOLD),
    JS_CFUNC_MAGIC_DEF("getMaximumLinearSpeed", 0, world_get, WORLD_MAXIMUM_LINEAR_SPEED),
    JS_CFUNC_MAGIC_DEF("setMaximumLinearSpeed", 1, world_set, WORLD_MAXIMUM_LINEAR_SPEED),
    JS_CFUNC_MAGIC_DEF("getAwakeBodyCount", 0, world_get, WORLD_AWAKE_BODY_COUNT),
    JS_CFUNC_MAGIC_DEF("isWarmStartingEnabled", 0, world_get, WORLD_WARM_STARTING),
    JS_CFUNC_MAGIC_DEF("enableWarmStarting", 1, world_set, WORLD_WARM_STARTING),
    JS_CFUNC_MAGIC_DEF("enableSpeculative", 1, world_set, WORLD_SPECULATIVE),
    JS_CFUNC_MAGIC_DEF("getContactRecycleDistance", 0, world_get, WORLD_CONTACT_RECYCLE_DISTANCE),
    JS_CFUNC_MAGIC_DEF("setContactRecycleDistance", 1, world_set, WORLD_CONTACT_RECYCLE_DISTANCE),
    JS_CFUNC_DEF("setContactTuning", 3, world_set_contact_tuning),
    JS_CFUNC_DEF("getCounters", 0, world_get_counters),
    JS_CFUNC_DEF("rebuildStaticTree", 0, world_rebuild_static_tree),
    JS_CFUNC_DEF("getUserData", 0, world_get_user_data),
    JS_CFUNC_DEF("setUserData", 1, world_set_user_data),
    JS_CFUNC_DEF("destroy", 0, world_destroy_js),
    JS_CFUNC_DEF("isValid", 0, world_is_valid),
};

/* ------------------------------------------------------------------------ */
/* Module                                                                    */
/* ------------------------------------------------------------------------ */

static JSValue b2js_set_memory_limit(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Box2D.setMemoryLimit";
    double bytes;

    if (b2js_argc(ctx, argc, 1, 1, where) < 0)
        return JS_EXCEPTION;
    /* Byte counts beyond 2^31 do not exist on the EE; a double here is a one-off. */
    if (!JS_IsNumber(argv[0]) || JS_ToFloat64(ctx, &bytes, argv[0]) < 0 ||
        !(bytes >= 0.0 && bytes <= 4294967295.0) || floor(bytes) != bytes)
        return JS_ThrowRangeError(ctx, "%s: bytes must be an integer between 0 (no limit) and 2^32 - 1", where);
    athena_box2d_set_memory_limit((size_t)bytes);
    return JS_UNDEFINED;
}

enum {
    MEMORY_LIMIT = 0,
    MEMORY_USAGE,
};

static JSValue b2js_get_memory(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    if (b2js_argc(ctx, argc, 0, 0, magic == MEMORY_LIMIT ? "Box2D.getMemoryLimit" : "Box2D.getMemoryUsage") < 0)
        return JS_EXCEPTION;
    return JS_NewInt64(ctx, (int64_t)(magic == MEMORY_LIMIT ? athena_box2d_memory_limit() : athena_box2d_memory_used()));
}

static const JSCFunctionListEntry box2d_module_funcs[] = {
    JS_CFUNC_DEF("setMemoryLimit", 1, b2js_set_memory_limit),
    JS_CFUNC_MAGIC_DEF("getMemoryLimit", 0, b2js_get_memory, MEMORY_LIMIT),
    JS_CFUNC_MAGIC_DEF("getMemoryUsage", 0, b2js_get_memory, MEMORY_USAGE),
    JS_CFUNC_DEF("solvePlanes", 3, b2js_solve_planes),
    JS_CFUNC_DEF("clipVector", 3, b2js_clip_vector),
    JS_PROP_STRING_DEF("version", "3.2.0", JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("STATIC_BODY", b2_staticBody, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("KINEMATIC_BODY", b2_kinematicBody, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DYNAMIC_BODY", b2_dynamicBody, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MAX_POLYGON_VERTICES", B2_MAX_POLYGON_VERTICES, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MAX_WORLDS", B2_MAX_WORLDS, JS_PROP_CONFIGURABLE),
    JS_CFUNC_DEF("createWorld", 1, b2js_create_world),
};

int b2js_class_proto(JSContext *ctx, JSClassID class_id, const JSCFunctionListEntry *funcs, int count) {
    JSValue proto = JS_NewObject(ctx);

    if (JS_IsException(proto))
        return -1;
    JS_SetPropertyFunctionList(ctx, proto, funcs, count);
    JS_SetClassProto(ctx, class_id, proto);
    return 0;
}

static int b2js_world_init(JSContext *ctx) {
    JSValue proto;

    if (athena_register_class(ctx, &b2js_world_class_id, &b2js_world_class) < 0)
        return -1;
    proto = JS_NewObject(ctx);
    if (JS_IsException(proto))
        return -1;
    /* The joint and query methods live in their own files. */
    JS_SetPropertyFunctionList(ctx, proto, world_proto_funcs, countof(world_proto_funcs));
    JS_SetPropertyFunctionList(ctx, proto, b2js_world_joint_funcs, b2js_world_joint_funcs_count);
    JS_SetPropertyFunctionList(ctx, proto, b2js_world_query_funcs, b2js_world_query_funcs_count);
    JS_SetClassProto(ctx, b2js_world_class_id, proto);
    return 0;
}

static int box2d_module_init(JSContext *ctx, JSModuleDef *m) {
    if (atoms_init(ctx) < 0)
        return -1;
    for (int kind = 0; kind < B2JS_KIND_COUNT; kind++) {
        if (athena_register_class(ctx, &b2js_class_ids[kind], &b2js_classes[kind]) < 0)
            return -1;
    }
    if (b2js_world_init(ctx) < 0 || b2js_body_init(ctx) < 0 ||
        b2js_shape_init(ctx) < 0 || b2js_joint_init(ctx) < 0)
        return -1;
    return JS_SetModuleExportList(ctx, m, box2d_module_funcs, countof(box2d_module_funcs));
}

JSModuleDef *athena_box2d_init(JSContext *ctx) {
    return athena_push_module(ctx, box2d_module_init, box2d_module_funcs,
        countof(box2d_module_funcs), "Box2D");
}
