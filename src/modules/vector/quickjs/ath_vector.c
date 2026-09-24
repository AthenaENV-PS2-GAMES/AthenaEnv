#include <stdio.h>
#include <string.h>
#include <math.h>
#include <malloc.h>

#include <ath_env.h>
#include <athena/module.h>

#include <athena/vector.h>
#include "ath_vector.h"

typedef struct {
    int components;
    JSClassID class_id;
    const char *name;
} AthenaVectorClass;

static AthenaVectorClass vector2_class = { 2, 0, "Vector2" };
static AthenaVectorClass vector3_class = { 3, 0, "Vector3" };
static AthenaVectorClass vector4_class = { 4, 0, "Vector4" };

static int vector_require_argc(JSContext *ctx, int argc, int expected, const char *name) {
    if (argc != expected) {
        JS_ThrowTypeError(ctx, "%s expects %d argument%s", name, expected,
            expected == 1 ? "" : "s");
        return 0;
    }
    return 1;
}

static AthenaVector4 *vector_from_value(JSContext *ctx, JSValueConst value,
    AthenaVectorClass *klass) {
    AthenaVector4 *vector = JS_GetOpaque2(ctx, value, klass->class_id);
    if (!vector) JS_ThrowTypeError(ctx, "Invalid %s value", klass->name);
    return vector;
}

static JSValue vector_new(JSContext *ctx, AthenaVectorClass *klass,
    const AthenaVector4 *value, JSValueConst new_target) {
    JSValue prototype = JS_UNDEFINED;
    JSValue object;
    if (!JS_IsUndefined(new_target)) {
        prototype = JS_GetPropertyStr(ctx, new_target, "prototype");
        if (JS_IsException(prototype)) return JS_EXCEPTION;
        object = JS_NewObjectProtoClass(ctx, prototype, klass->class_id);
        JS_FreeValue(ctx, prototype);
    } else {
        object = JS_NewObjectClass(ctx, klass->class_id);
    }
    if (JS_IsException(object)) return object;

    AthenaVector4 *copy = memalign(16, sizeof(*copy));
    if (!copy) {
        JS_FreeValue(ctx, object);
        return JS_ThrowOutOfMemory(ctx);
    }
    *copy = *value;
    JS_SetOpaque(object, copy);
    return object;
}

static void vector_finalizer(JSRuntime *rt, JSValue value) {
    AthenaVector4 *vector = JS_GetOpaque(value, vector4_class.class_id);
    if (!vector) vector = JS_GetOpaque(value, vector3_class.class_id);
    if (!vector) vector = JS_GetOpaque(value, vector2_class.class_id);
    free(vector);
}

static JSValue vector_ctor(JSContext *ctx, JSValueConst new_target,
    int argc, JSValueConst *argv, AthenaVectorClass *klass) {
    if (argc != klass->components)
        return JS_ThrowTypeError(ctx, "%s expects %d arguments",
            klass->name, klass->components);

    AthenaVector4 value = { 0, 0, 0, 0 };
    if (JS_ToFloat32(ctx, &value.x, argv[0]) ||
        JS_ToFloat32(ctx, &value.y, argv[1])) return JS_EXCEPTION;
    if (klass->components >= 3 && JS_ToFloat32(ctx, &value.z, argv[2]))
        return JS_EXCEPTION;
    if (klass->components >= 4 && JS_ToFloat32(ctx, &value.w, argv[3]))
        return JS_EXCEPTION;
    return vector_new(ctx, klass, &value, new_target);
}

static JSValue vector2_ctor(JSContext *ctx, JSValueConst nt, int argc, JSValueConst *argv) {
    return vector_ctor(ctx, nt, argc, argv, &vector2_class);
}
static JSValue vector3_ctor(JSContext *ctx, JSValueConst nt, int argc, JSValueConst *argv) {
    return vector_ctor(ctx, nt, argc, argv, &vector3_class);
}
static JSValue vector4_ctor(JSContext *ctx, JSValueConst nt, int argc, JSValueConst *argv) {
    return vector_ctor(ctx, nt, argc, argv, &vector4_class);
}

static JSValue vector_get(JSContext *ctx, JSValueConst this_val, int magic,
    AthenaVectorClass *klass) {
    AthenaVector4 *v = vector_from_value(ctx, this_val, klass);
    if (!v) return JS_EXCEPTION;
    return JS_NewFloat32(ctx, ((float *)v)[magic]);
}

static JSValue vector_set(JSContext *ctx, JSValueConst this_val, JSValue value,
    int magic, AthenaVectorClass *klass) {
    AthenaVector4 *v = vector_from_value(ctx, this_val, klass);
    float number;
    if (!v || JS_ToFloat32(ctx, &number, value)) return JS_EXCEPTION;
    ((float *)v)[magic] = number;
    return JS_UNDEFINED;
}

#define VECTOR_GETSET(name, index, klass) \
static JSValue name##_get(JSContext *ctx, JSValueConst v, int magic) { \
    return vector_get(ctx, v, index, klass); \
} \
static JSValue name##_set(JSContext *ctx, JSValueConst v, JSValue value, int magic) { \
    return vector_set(ctx, v, value, index, klass); \
}

VECTOR_GETSET(v2_x, 0, &vector2_class)
VECTOR_GETSET(v2_y, 1, &vector2_class)
VECTOR_GETSET(v3_x, 0, &vector3_class)
VECTOR_GETSET(v3_y, 1, &vector3_class)
VECTOR_GETSET(v3_z, 2, &vector3_class)
VECTOR_GETSET(v4_x, 0, &vector4_class)
VECTOR_GETSET(v4_y, 1, &vector4_class)
VECTOR_GETSET(v4_z, 2, &vector4_class)
VECTOR_GETSET(v4_w, 3, &vector4_class)

static JSValue vector_norm(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, AthenaVectorClass *klass) {
    if (!vector_require_argc(ctx, argc, 0, "Vector.norm")) return JS_EXCEPTION;
    AthenaVector4 *v = vector_from_value(ctx, this_val, klass);
    if (!v) return JS_EXCEPTION;
    return JS_NewFloat32(ctx, klass->components == 4 ?
        ath_vector_length4(v) : ath_vector_length3(v));
}

static JSValue vector_dot(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, AthenaVectorClass *klass) {
    if (!vector_require_argc(ctx, argc, 1, "Vector.dot")) return JS_EXCEPTION;
    AthenaVector4 *a = vector_from_value(ctx, this_val, klass);
    AthenaVector4 *b = vector_from_value(ctx, argv[0], klass);
    if (!a || !b) return JS_EXCEPTION;
    if (klass->components == 4)
        return JS_NewFloat32(ctx, a->x*b->x + a->y*b->y + a->z*b->z + a->w*b->w);
    return JS_NewFloat32(ctx, ath_vector_dot3(a, b));
}

static JSValue vector_distance(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, AthenaVectorClass *klass, bool squared) {
    if (!vector_require_argc(ctx, argc, 1, "Vector.distance")) return JS_EXCEPTION;
    AthenaVector4 *a = vector_from_value(ctx, this_val, klass);
    AthenaVector4 *b = vector_from_value(ctx, argv[0], klass);
    if (!a || !b) return JS_EXCEPTION;
    float dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;
    float result = dx*dx + dy*dy + dz*dz;
    if (klass->components == 4) {
        float dw = a->w - b->w;
        result += dw*dw;
    }
    return JS_NewFloat32(ctx, squared ? result : sqrtf(result));
}

static JSValue vector_binary(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, AthenaVectorClass *klass, int operation) {
    if (!vector_require_argc(ctx, argc, 1, "Vector operation")) return JS_EXCEPTION;
    AthenaVector4 *a = vector_from_value(ctx, this_val, klass);
    AthenaVector4 *b = vector_from_value(ctx, argv[0], klass);
    AthenaVector4 result;
    if (!a || !b) return JS_EXCEPTION;
    if (operation == 0) ath_vector_add(&result, a, b);
    else if (operation == 1) ath_vector_sub(&result, a, b);
    else if (operation == 2) ath_vector_mul(&result, a, b);
    else {
        if (b->x == 0.0f || b->y == 0.0f ||
            (klass->components >= 3 && b->z == 0.0f) ||
            (klass->components >= 4 && b->w == 0.0f))
            return JS_ThrowRangeError(ctx, "Vector.div cannot divide by zero");
        ath_vector_div(&result, a, b);
    }
    return vector_new(ctx, klass, &result, JS_UNDEFINED);
}

#define VECTOR_METHODS(prefix, klass) \
static JSValue prefix##_norm(JSContext *c, JSValueConst t, int a, JSValueConst *v) { return vector_norm(c,t,a,v,klass); } \
static JSValue prefix##_dot(JSContext *c, JSValueConst t, int a, JSValueConst *v) { return vector_dot(c,t,a,v,klass); } \
static JSValue prefix##_distance(JSContext *c, JSValueConst t, int a, JSValueConst *v) { return vector_distance(c,t,a,v,klass,false); } \
static JSValue prefix##_distance2(JSContext *c, JSValueConst t, int a, JSValueConst *v) { return vector_distance(c,t,a,v,klass,true); } \
static JSValue prefix##_add(JSContext *c, JSValueConst t, int a, JSValueConst *v) { return vector_binary(c,t,a,v,klass,0); } \
static JSValue prefix##_sub(JSContext *c, JSValueConst t, int a, JSValueConst *v) { return vector_binary(c,t,a,v,klass,1); } \
static JSValue prefix##_mul(JSContext *c, JSValueConst t, int a, JSValueConst *v) { return vector_binary(c,t,a,v,klass,2); } \
static JSValue prefix##_div(JSContext *c, JSValueConst t, int a, JSValueConst *v) { return vector_binary(c,t,a,v,klass,3); }

VECTOR_METHODS(v2, &vector2_class)
VECTOR_METHODS(v3, &vector3_class)
VECTOR_METHODS(v4, &vector4_class)

static JSValue v3_cross(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (!vector_require_argc(ctx, argc, 1, "Vector3.cross")) return JS_EXCEPTION;
    AthenaVector4 *a = vector_from_value(ctx, this_val, &vector3_class);
    AthenaVector4 *b = vector_from_value(ctx, argv[0], &vector3_class);
    AthenaVector4 result;
    if (!a || !b) return JS_EXCEPTION;
    ath_vector_cross(&result, a, b);
    return vector_new(ctx, &vector3_class, &result, JS_UNDEFINED);
}

static JSValue v4_cross(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (!vector_require_argc(ctx, argc, 1, "Vector4.cross")) return JS_EXCEPTION;
    AthenaVector4 *a = vector_from_value(ctx, this_val, &vector4_class);
    AthenaVector4 *b = vector_from_value(ctx, argv[0], &vector4_class);
    AthenaVector4 result;
    if (!a || !b) return JS_EXCEPTION;
    ath_vector_cross(&result, a, b);
    return vector_new(ctx, &vector4_class, &result, JS_UNDEFINED);
}

static JSValue vector_to_string(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, AthenaVectorClass *klass) {
    AthenaVector4 *v = vector_from_value(ctx, this_val, klass);
    char text[100];
    if (!v) return JS_EXCEPTION;
    if (klass->components == 2) snprintf(text, sizeof(text), "{x:%g, y:%g}", v->x, v->y);
    else if (klass->components == 3) snprintf(text, sizeof(text), "{x:%g, y:%g, z:%g}", v->x, v->y, v->z);
    else snprintf(text, sizeof(text), "{x:%g, y:%g, z:%g, w:%g}", v->x, v->y, v->z, v->w);
    return JS_NewString(ctx, text);
}

#define VECTOR_STRING(prefix, klass) \
static JSValue prefix##_string(JSContext *c, JSValueConst t, int a, JSValueConst *v) { return vector_to_string(c,t,a,v,klass); }
VECTOR_STRING(v2, &vector2_class)
VECTOR_STRING(v3, &vector3_class)
VECTOR_STRING(v4, &vector4_class)

static JSClassDef vector2_js_class = { "Vector2", .finalizer = vector_finalizer };
static JSClassDef vector3_js_class = { "Vector3", .finalizer = vector_finalizer };
static JSClassDef vector4_js_class = { "Vector4", .finalizer = vector_finalizer };

static const JSCFunctionListEntry vector2_funcs[] = {
    JS_CFUNC_DEF("norm", 0, v2_norm), JS_CFUNC_DEF("dot", 1, v2_dot),
    JS_CFUNC_DEF("distance", 1, v2_distance), JS_CFUNC_DEF("distance2", 1, v2_distance2),
    JS_CFUNC_DEF("toString", 0, v2_string), JS_CFUNC_DEF("add", 1, v2_add),
    JS_CFUNC_DEF("sub", 1, v2_sub), JS_CFUNC_DEF("mul", 1, v2_mul), JS_CFUNC_DEF("div", 1, v2_div),
    JS_CGETSET_MAGIC_DEF("x", v2_x_get, v2_x_set, 0),
    JS_CGETSET_MAGIC_DEF("y", v2_y_get, v2_y_set, 1)
};
static const JSCFunctionListEntry vector3_funcs[] = {
    JS_CFUNC_DEF("norm", 0, v3_norm), JS_CFUNC_DEF("dot", 1, v3_dot),
    JS_CFUNC_DEF("cross", 1, v3_cross), JS_CFUNC_DEF("distance", 1, v3_distance),
    JS_CFUNC_DEF("distance2", 1, v3_distance2), JS_CFUNC_DEF("toString", 0, v3_string),
    JS_CFUNC_DEF("add", 1, v3_add), JS_CFUNC_DEF("sub", 1, v3_sub),
    JS_CFUNC_DEF("mul", 1, v3_mul), JS_CFUNC_DEF("div", 1, v3_div),
    JS_CGETSET_MAGIC_DEF("x", v3_x_get, v3_x_set, 0),
    JS_CGETSET_MAGIC_DEF("y", v3_y_get, v3_y_set, 1),
    JS_CGETSET_MAGIC_DEF("z", v3_z_get, v3_z_set, 2)
};
static const JSCFunctionListEntry vector4_funcs[] = {
    JS_CFUNC_DEF("norm", 0, v4_norm), JS_CFUNC_DEF("dot", 1, v4_dot),
    JS_CFUNC_DEF("cross", 1, v4_cross), JS_CFUNC_DEF("distance", 1, v4_distance),
    JS_CFUNC_DEF("distance2", 1, v4_distance2), JS_CFUNC_DEF("toString", 0, v4_string),
    JS_CFUNC_DEF("add", 1, v4_add), JS_CFUNC_DEF("sub", 1, v4_sub),
    JS_CFUNC_DEF("mul", 1, v4_mul), JS_CFUNC_DEF("div", 1, v4_div),
    JS_CGETSET_MAGIC_DEF("x", v4_x_get, v4_x_set, 0),
    JS_CGETSET_MAGIC_DEF("y", v4_y_get, v4_y_set, 1),
    JS_CGETSET_MAGIC_DEF("z", v4_z_get, v4_z_set, 2),
    JS_CGETSET_MAGIC_DEF("w", v4_w_get, v4_w_set, 3)
};

static int init_vector_class(JSContext *ctx, JSModuleDef *m,
    AthenaVectorClass *klass, JSClassDef *class_def,
    JSValue (*ctor)(JSContext *, JSValueConst, int, JSValueConst *),
    const JSCFunctionListEntry *funcs, int func_count) {
    if (athena_register_class(ctx, &klass->class_id, class_def) < 0)
        return -1;
    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, funcs, func_count);
    JSValue constructor = JS_NewCFunction2(ctx, ctor, klass->name,
        klass->components, JS_CFUNC_constructor_or_func, 0);
    JS_SetConstructor(ctx, constructor, proto);
    JS_SetClassProto(ctx, klass->class_id, proto);
    return JS_SetModuleExport(ctx, m, klass->name, constructor);
}

static int vector2_module_init(JSContext *ctx, JSModuleDef *m) {
    return init_vector_class(ctx, m, &vector2_class, &vector2_js_class,
        vector2_ctor, vector2_funcs, countof(vector2_funcs));
}
static int vector3_module_init(JSContext *ctx, JSModuleDef *m) {
    return init_vector_class(ctx, m, &vector3_class, &vector3_js_class,
        vector3_ctor, vector3_funcs, countof(vector3_funcs));
}
static int vector4_module_init(JSContext *ctx, JSModuleDef *m) {
    return init_vector_class(ctx, m, &vector4_class, &vector4_js_class,
        vector4_ctor, vector4_funcs, countof(vector4_funcs));
}

static int vector_module_init(JSContext *ctx, JSModuleDef *m) {
    if (init_vector_class(ctx, m, &vector2_class, &vector2_js_class,
            vector2_ctor, vector2_funcs, countof(vector2_funcs)) < 0)
        return -1;
    if (init_vector_class(ctx, m, &vector3_class, &vector3_js_class,
            vector3_ctor, vector3_funcs, countof(vector3_funcs)) < 0)
        return -1;
    if (init_vector_class(ctx, m, &vector4_class, &vector4_js_class,
            vector4_ctor, vector4_funcs, countof(vector4_funcs)) < 0)
        return -1;
    return 0;
}

JSModuleDef *athena_vector_init(JSContext *ctx) {
    JSModuleDef *module = athena_push_module(ctx, vector_module_init,
        NULL, 0, "Vector");
    if (module) {
        JS_AddModuleExport(ctx, module, "Vector2");
        JS_AddModuleExport(ctx, module, "Vector3");
        JS_AddModuleExport(ctx, module, "Vector4");
    }
    return module;
}
