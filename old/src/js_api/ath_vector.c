#include <ath_env.h>
#include <stdio.h>

#include <athena/vec2.h>
#include <athena/vec3.h>

static JSClassID js_vector2_class_id;

JSClassID get_vector2_class_id(){
	return js_vector2_class_id;
}

static void js_vector2_finalizer(JSRuntime *rt, JSValue val) {
    AthenaVec2 *s = JS_GetOpaque(val, js_vector2_class_id);
    athena_vec2_destroy(s);
}

static JSValue js_vector2_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    float x, y;
    AthenaVec2 *s;
    JSValue obj = JS_UNDEFINED;

    if (JS_ToFloat32(ctx, &x, argv[0]))
        return JS_EXCEPTION;
    if (JS_ToFloat32(ctx, &y, argv[1]))
        return JS_EXCEPTION;

    s = athena_vec2_create(x, y);
    if (!s)
        return JS_EXCEPTION;

    obj = JS_NewObjectClass(ctx, js_vector2_class_id);
    if (JS_IsException(obj)) {
        athena_vec2_destroy(s);
        return obj;
    }

    JS_SetOpaque(obj, s);
    return obj;
}

static JSValue js_vector2_get_xy(JSContext *ctx, JSValueConst this_val, int magic) {
    AthenaVec2 *s = JS_GetOpaque2(ctx, this_val, js_vector2_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewFloat32(ctx, magic == 0 ? athena_vec2_get_x(s) : athena_vec2_get_y(s));
}

static JSValue js_vector2_set_xy(JSContext *ctx, JSValueConst this_val, JSValue val, int magic) {
    AthenaVec2 *s = JS_GetOpaque2(ctx, this_val, js_vector2_class_id);
    float v;
    if (!s || JS_ToFloat32(ctx, &v, val))
        return JS_EXCEPTION;
    if (magic == 0)
        athena_vec2_set_x(s, v);
    else
        athena_vec2_set_y(s, v);
    return JS_UNDEFINED;
}

static JSValue js_vector2_norm(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaVec2 *s = JS_GetOpaque2(ctx, this_val, js_vector2_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewFloat32(ctx, athena_vec2_norm(s));
}

static JSValue js_vector2_dotproduct(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaVec2 *v1 = JS_GetOpaque2(ctx, this_val, js_vector2_class_id);
    AthenaVec2 *v2 = JS_GetOpaque2(ctx, argv[0], js_vector2_class_id);
    if (!v1 || !v2)
        return JS_EXCEPTION;
    return JS_NewFloat32(ctx, athena_vec2_dot(v1, v2));
}

static JSValue js_vector2_crossproduct(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaVec2 *v1 = JS_GetOpaque2(ctx, this_val, js_vector2_class_id);
    AthenaVec2 *v2 = JS_GetOpaque2(ctx, argv[0], js_vector2_class_id);
    if (!v1 || !v2)
        return JS_EXCEPTION;
    return JS_NewFloat32(ctx, athena_vec2_cross(v1, v2));
}

static JSValue js_vector2_distance(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaVec2 *v1 = JS_GetOpaque2(ctx, this_val, js_vector2_class_id);
    AthenaVec2 *v2 = JS_GetOpaque2(ctx, argv[0], js_vector2_class_id);
    if (!v1 || !v2)
        return JS_EXCEPTION;
    return JS_NewFloat32(ctx, athena_vec2_dist(v1, v2));
}

static JSValue js_vector2_distancesqr(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaVec2 *v1 = JS_GetOpaque2(ctx, this_val, js_vector2_class_id);
    AthenaVec2 *v2 = JS_GetOpaque2(ctx, argv[0], js_vector2_class_id);
    if (!v1 || !v2)
        return JS_EXCEPTION;
    return JS_NewFloat32(ctx, athena_vec2_distsqr(v1, v2));
}

static JSValue js_vector2_tostring(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    char str[32];
    AthenaVec2 *v = JS_GetOpaque2(ctx, this_val, js_vector2_class_id);
    if (!v)
        return JS_EXCEPTION;
    athena_vec2_tostring(v, str, sizeof(str));
    return JS_NewString(ctx, str);
}

static JSValue js_vector2_wrap_binary(JSContext *ctx, AthenaVec2 *(*op)(const AthenaVec2 *, const AthenaVec2 *),
                                      JSValueConst new_target, int argc, JSValueConst *argv) {
    AthenaVec2 *v1 = JS_GetOpaque2(ctx, argv[0], js_vector2_class_id);
    AthenaVec2 *v2 = JS_GetOpaque2(ctx, argv[1], js_vector2_class_id);
    AthenaVec2 *result;
    JSValue obj;

    if (!v1 || !v2)
        return JS_EXCEPTION;

    result = op(v1, v2);
    if (!result)
        return JS_EXCEPTION;

    obj = JS_NewObjectClass(ctx, js_vector2_class_id);
    if (JS_IsException(obj)) {
        athena_vec2_destroy(result);
        return obj;
    }
    JS_SetOpaque(obj, result);
    return obj;
}

static JSValue js_vector2_add(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    return js_vector2_wrap_binary(ctx, athena_vec2_add, new_target, argc, argv);
}

static JSValue js_vector2_sub(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    return js_vector2_wrap_binary(ctx, athena_vec2_sub, new_target, argc, argv);
}

static JSValue js_vector2_mul(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    return js_vector2_wrap_binary(ctx, athena_vec2_mul, new_target, argc, argv);
}

static JSValue js_vector2_div(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    return js_vector2_wrap_binary(ctx, athena_vec2_div, new_target, argc, argv);
}

static JSClassDef js_vector2_class = {
    "Vector2",
    .finalizer = js_vector2_finalizer,
};

/* Method forms of the arithmetic helpers. These used to be reachable only as
 * overloaded operators (v1 + v2), which required QuickJS's operator-overloading
 * support - and that lives behind CONFIG_BIGNUM, dragging in BigInt/BigFloat/
 * BigDecimal and all of libbf. As methods they work with plain QuickJS, so the
 * whole bignum layer could be dropped. The underlying binary helpers take both
 * operands in argv, so the shims just put `this` in front. */
#define VEC2_BINARY_METHOD(name, cfunc)                                        \
    static JSValue js_vector2_##name##_method(JSContext *ctx, JSValueConst this_val, \
                                              int argc, JSValueConst *argv) {  \
        JSValueConst a[2];                                                     \
        a[0] = this_val;                                                       \
        a[1] = (argc > 0) ? argv[0] : JS_UNDEFINED;                            \
        return cfunc(ctx, this_val, 2, a);                                     \
    }
VEC2_BINARY_METHOD(add, js_vector2_add)
VEC2_BINARY_METHOD(sub, js_vector2_sub)
VEC2_BINARY_METHOD(mul, js_vector2_mul)
VEC2_BINARY_METHOD(div, js_vector2_div)
#undef VEC2_BINARY_METHOD

static const JSCFunctionListEntry js_vector2_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("x", js_vector2_get_xy, js_vector2_set_xy, 0),
    JS_CGETSET_MAGIC_DEF("y", js_vector2_get_xy, js_vector2_set_xy, 1),
    JS_CFUNC_DEF("norm", 0, js_vector2_norm),
    JS_CFUNC_DEF("dot", 1, js_vector2_dotproduct),
    JS_CFUNC_DEF("cross", 1, js_vector2_crossproduct),
    JS_CFUNC_DEF("dist", 1, js_vector2_distance),
    JS_CFUNC_DEF("distsqr", 1, js_vector2_distancesqr),
    JS_CFUNC_DEF("toString", 0, js_vector2_tostring),
    JS_CFUNC_DEF("add", 1, js_vector2_add_method),
    JS_CFUNC_DEF("sub", 1, js_vector2_sub_method),
    JS_CFUNC_DEF("mul", 1, js_vector2_mul_method),
    JS_CFUNC_DEF("div", 1, js_vector2_div_method),
};

static int js_vector2_init(JSContext *ctx, JSModuleDef *m) {
    JSValue vector2_proto, vector2_class;
    JS_NewClassID(&js_vector2_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_vector2_class_id, &js_vector2_class);
    vector2_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, vector2_proto, js_vector2_proto_funcs, countof(js_vector2_proto_funcs));
    vector2_class = JS_NewCFunction2(ctx, js_vector2_ctor, "Vector2", 2, JS_CFUNC_constructor_or_func, 0);
    JS_SetConstructor(ctx, vector2_class, vector2_proto);
    JS_SetClassProto(ctx, js_vector2_class_id, vector2_proto);
    return JS_SetModuleExport(ctx, m, "Vector2", vector2_class);
}

static JSClassID js_vector3_class_id;

JSClassID get_vector3_class_id(){
	return js_vector3_class_id;
}

static void js_vector3_finalizer(JSRuntime *rt, JSValue val) {
    AthenaVec3 *s = JS_GetOpaque(val, js_vector3_class_id);
    athena_vec3_destroy(s);
}

static JSValue js_vector3_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    float x, y, z;
    AthenaVec3 *s;
    JSValue obj = JS_UNDEFINED;

    if (JS_ToFloat32(ctx, &x, argv[0]) || JS_ToFloat32(ctx, &y, argv[1]) || JS_ToFloat32(ctx, &z, argv[2]))
        return JS_EXCEPTION;

    s = athena_vec3_create(x, y, z);
    if (!s)
        return JS_EXCEPTION;

    obj = JS_NewObjectClass(ctx, js_vector3_class_id);
    if (JS_IsException(obj)) {
        athena_vec3_destroy(s);
        return obj;
    }
    JS_SetOpaque(obj, s);
    return obj;
}

static JSValue js_vector3_get_xyz(JSContext *ctx, JSValueConst this_val, int magic) {
    AthenaVec3 *s = JS_GetOpaque2(ctx, this_val, js_vector3_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (magic == 0) return JS_NewFloat32(ctx, athena_vec3_get_x(s));
    if (magic == 1) return JS_NewFloat32(ctx, athena_vec3_get_y(s));
    return JS_NewFloat32(ctx, athena_vec3_get_z(s));
}

static JSValue js_vector3_set_xyz(JSContext *ctx, JSValueConst this_val, JSValue val, int magic) {
    AthenaVec3 *s = JS_GetOpaque2(ctx, this_val, js_vector3_class_id);
    float v;
    if (!s || JS_ToFloat32(ctx, &v, val))
        return JS_EXCEPTION;
    if (magic == 0) athena_vec3_set_x(s, v);
    else if (magic == 1) athena_vec3_set_y(s, v);
    else athena_vec3_set_z(s, v);
    return JS_UNDEFINED;
}

static JSValue js_vector3_norm(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaVec3 *v = JS_GetOpaque2(ctx, this_val, js_vector3_class_id);
    if (!v) return JS_EXCEPTION;
    return JS_NewFloat32(ctx, athena_vec3_norm(v));
}

static JSValue js_vector3_dotproduct(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaVec3 *v1 = JS_GetOpaque2(ctx, this_val, js_vector3_class_id);
    AthenaVec3 *v2 = JS_GetOpaque2(ctx, argv[0], js_vector3_class_id);
    if (!v1 || !v2) return JS_EXCEPTION;
    return JS_NewFloat32(ctx, athena_vec3_dot(v1, v2));
}

static JSValue js_vector3_dist(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaVec3 *v1 = JS_GetOpaque2(ctx, this_val, js_vector3_class_id);
    AthenaVec3 *v2 = JS_GetOpaque2(ctx, argv[0], js_vector3_class_id);
    if (!v1 || !v2) return JS_EXCEPTION;
    return JS_NewFloat32(ctx, athena_vec3_dist(v1, v2));
}

static JSValue js_vector3_distsqr(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaVec3 *v1 = JS_GetOpaque2(ctx, this_val, js_vector3_class_id);
    AthenaVec3 *v2 = JS_GetOpaque2(ctx, argv[0], js_vector3_class_id);
    if (!v1 || !v2) return JS_EXCEPTION;
    return JS_NewFloat32(ctx, athena_vec3_distsqr(v1, v2));
}

static JSValue js_vector3_tostring(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    char str[32];
    AthenaVec3 *v = JS_GetOpaque2(ctx, this_val, js_vector3_class_id);
    if (!v) return JS_EXCEPTION;
    athena_vec3_tostring(v, str, sizeof(str));
    return JS_NewString(ctx, str);
}

static JSValue js_vector3_wrap_binary(JSContext *ctx, AthenaVec3 *(*op)(const AthenaVec3 *, const AthenaVec3 *),
                                      JSValueConst new_target, int argc, JSValueConst *argv) {
    AthenaVec3 *v1 = JS_GetOpaque2(ctx, argv[0], js_vector3_class_id);
    AthenaVec3 *v2 = JS_GetOpaque2(ctx, argv[1], js_vector3_class_id);
    AthenaVec3 *result;
    JSValue obj;
    if (!v1 || !v2) return JS_EXCEPTION;
    result = op(v1, v2);
    if (!result) return JS_EXCEPTION;
    obj = JS_NewObjectClass(ctx, js_vector3_class_id);
    if (JS_IsException(obj)) { athena_vec3_destroy(result); return obj; }
    JS_SetOpaque(obj, result);
    return obj;
}

static JSValue js_vector3_add(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    return js_vector3_wrap_binary(ctx, athena_vec3_add, new_target, argc, argv);
}
static JSValue js_vector3_sub(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    return js_vector3_wrap_binary(ctx, athena_vec3_sub, new_target, argc, argv);
}
static JSValue js_vector3_mul(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    return js_vector3_wrap_binary(ctx, athena_vec3_mul, new_target, argc, argv);
}
static JSValue js_vector3_div(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    return js_vector3_wrap_binary(ctx, athena_vec3_div, new_target, argc, argv);
}

static JSValue js_vector3_cross(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaVec3 *v1 = JS_GetOpaque2(ctx, this_val, js_vector3_class_id);
    AthenaVec3 *v2 = JS_GetOpaque2(ctx, argv[0], js_vector3_class_id);
    AthenaVec3 *result;
    JSValue obj;
    if (!v1 || !v2) return JS_EXCEPTION;
    result = athena_vec3_cross(v1, v2);
    if (!result) return JS_EXCEPTION;
    obj = JS_NewObjectClass(ctx, js_vector3_class_id);
    if (JS_IsException(obj)) { athena_vec3_destroy(result); return obj; }
    JS_SetOpaque(obj, result);
    return obj;
}

static JSClassDef js_vector3_class = {
    "Vector3",
    .finalizer = js_vector3_finalizer,
};

/* Same treatment as Vector2 above - see the comment there. */
#define VEC3_BINARY_METHOD(name, cfunc)                                        \
    static JSValue js_vector3_##name##_method(JSContext *ctx, JSValueConst this_val, \
                                              int argc, JSValueConst *argv) {  \
        JSValueConst a[2];                                                     \
        a[0] = this_val;                                                       \
        a[1] = (argc > 0) ? argv[0] : JS_UNDEFINED;                            \
        return cfunc(ctx, this_val, 2, a);                                     \
    }
VEC3_BINARY_METHOD(add, js_vector3_add)
VEC3_BINARY_METHOD(sub, js_vector3_sub)
VEC3_BINARY_METHOD(mul, js_vector3_mul)
VEC3_BINARY_METHOD(div, js_vector3_div)
#undef VEC3_BINARY_METHOD

static const JSCFunctionListEntry js_vector3_proto_funcs[] = {
    JS_CFUNC_DEF("norm", 0, js_vector3_norm),
    JS_CFUNC_DEF("dot", 1, js_vector3_dotproduct),
    JS_CFUNC_DEF("cross", 1, js_vector3_cross),
    JS_CFUNC_DEF("dist", 1, js_vector3_dist),
    JS_CFUNC_DEF("distsqr", 1, js_vector3_distsqr),
    JS_CFUNC_DEF("toString", 0, js_vector3_tostring),
    JS_CGETSET_MAGIC_DEF("x", js_vector3_get_xyz, js_vector3_set_xyz, 0),
    JS_CGETSET_MAGIC_DEF("y", js_vector3_get_xyz, js_vector3_set_xyz, 1),
    JS_CGETSET_MAGIC_DEF("z", js_vector3_get_xyz, js_vector3_set_xyz, 2),
    JS_CFUNC_DEF("add", 1, js_vector3_add_method),
    JS_CFUNC_DEF("sub", 1, js_vector3_sub_method),
    JS_CFUNC_DEF("mul", 1, js_vector3_mul_method),
    JS_CFUNC_DEF("div", 1, js_vector3_div_method),
};

static int js_vector3_init(JSContext *ctx, JSModuleDef *m) {
    JSValue vector3_proto, vector3_class;
    JS_NewClassID(&js_vector3_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_vector3_class_id, &js_vector3_class);
    vector3_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, vector3_proto, js_vector3_proto_funcs, countof(js_vector3_proto_funcs));
    vector3_class = JS_NewCFunction2(ctx, js_vector3_ctor, "Vector3", 3, JS_CFUNC_constructor_or_func, 0);
    JS_SetConstructor(ctx, vector3_class, vector3_proto);
    JS_SetClassProto(ctx, js_vector3_class_id, vector3_proto);
    return JS_SetModuleExport(ctx, m, "Vector3", vector3_class);
}

JSModuleDef *athena_vector_init(JSContext *ctx) {
    JSModuleDef *m;
    m = JS_NewCModule(ctx, "Vector2", js_vector2_init);
    if (!m) return NULL;
    JS_AddModuleExport(ctx, m, "Vector2");
    m = JS_NewCModule(ctx, "Vector3", js_vector3_init);
    if (!m) return NULL;
    JS_AddModuleExport(ctx, m, "Vector3");
    return m;
}
