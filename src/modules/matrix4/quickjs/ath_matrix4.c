#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>

#include <ath_env.h>
#include <athena_module.h>

#include "../native/matrix4.h"
#include "ath_matrix4.h"

static JSClassID matrix4_class_id;
static bool matrix4_class_registered;

static AthenaMatrix4 *matrix4_from_value(JSContext *ctx, JSValueConst value) {
    AthenaMatrix4 *matrix = JS_GetOpaque2(ctx, value, matrix4_class_id);
    if (!matrix) JS_ThrowTypeError(ctx, "Invalid Matrix4 value");
    return matrix;
}

static void matrix4_finalizer(JSRuntime *rt, JSValue value) {
    free(JS_GetOpaque(value, matrix4_class_id));
}

static JSValue matrix4_ctor(JSContext *ctx, JSValueConst target,
    int argc, JSValueConst *argv) {
    if (argc != 0 && argc != 16)
        return JS_ThrowTypeError(ctx, "Matrix4 expects zero or 16 arguments");

    AthenaMatrix4 *matrix = memalign(16, sizeof(*matrix));
    if (!matrix) return JS_ThrowOutOfMemory(ctx);
    if (argc == 0) {
        ath_matrix4_identity(matrix);
    } else {
        for (int i = 0; i < 16; i++) {
            if (JS_ToFloat32(ctx, &matrix->value[i], argv[i])) {
                free(matrix);
                return JS_EXCEPTION;
            }
        }
    }

    JSValue prototype = JS_UNDEFINED;
    JSValue object;
    if (!JS_IsUndefined(target)) {
        prototype = JS_GetPropertyStr(ctx, target, "prototype");
        if (JS_IsException(prototype)) {
            free(matrix);
            return JS_EXCEPTION;
        }
        object = JS_NewObjectProtoClass(ctx, prototype, matrix4_class_id);
        JS_FreeValue(ctx, prototype);
    } else {
        object = JS_NewObjectClass(ctx, matrix4_class_id);
    }
    if (JS_IsException(object)) {
        free(matrix);
        return object;
    }
    JS_SetOpaque(object, matrix);
    return object;
}

static JSValue matrix4_to_array(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    AthenaMatrix4 *matrix = matrix4_from_value(ctx, this_val);
    JSValue array;
    if (!matrix) return JS_EXCEPTION;
    array = JS_NewArray(ctx);
    if (JS_IsException(array)) return array;
    for (uint32_t i = 0; i < 16; i++)
        JS_SetPropertyUint32(ctx, array, i, JS_NewFloat32(ctx, matrix->value[i]));
    return array;
}

static JSValue matrix4_from_array(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    AthenaMatrix4 *matrix;
    if (argc != 1) return JS_ThrowTypeError(ctx, "Matrix4.fromArray expects one argument");
    matrix = matrix4_from_value(ctx, this_val);
    if (!matrix) return JS_EXCEPTION;
    for (uint32_t i = 0; i < 16; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, argv[0], i);
        int result = JS_ToFloat32(ctx, &matrix->value[i], item);
        JS_FreeValue(ctx, item);
        if (result) return JS_EXCEPTION;
    }
    return JS_DupValue(ctx, this_val);
}

static JSValue matrix4_clone(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    AthenaMatrix4 *source = matrix4_from_value(ctx, this_val);
    AthenaMatrix4 *copy;
    JSValue object;
    if (!source) return JS_EXCEPTION;
    copy = memalign(16, sizeof(*copy));
    if (!copy) return JS_ThrowOutOfMemory(ctx);
    ath_matrix4_copy(copy, source);
    object = JS_NewObjectClass(ctx, matrix4_class_id);
    if (JS_IsException(object)) {
        free(copy);
        return object;
    }
    JS_SetOpaque(object, copy);
    return object;
}

static JSValue js_matrix4_in_place(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int operation) {
    AthenaMatrix4 *matrix = matrix4_from_value(ctx, this_val);
    if (!matrix || argc != 0) return matrix ? JS_ThrowTypeError(ctx, "Matrix4 method expects no arguments") : JS_EXCEPTION;
    if (operation == 0) ath_matrix4_identity(matrix);
    else ath_matrix4_transpose(matrix, matrix);
    return JS_DupValue(ctx, this_val);
}

static JSValue js_matrix4_identity(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    return js_matrix4_in_place(ctx, this_val, argc, argv, 0);
}

static JSValue js_matrix4_transpose(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    return js_matrix4_in_place(ctx, this_val, argc, argv, 1);
}

static JSValue js_matrix4_copy(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    AthenaMatrix4 *destination = matrix4_from_value(ctx, this_val);
    AthenaMatrix4 *source;
    if (!destination) return JS_EXCEPTION;
    if (argc != 1) return JS_ThrowTypeError(ctx, "Matrix4.copy expects one argument");
    source = matrix4_from_value(ctx, argv[0]);
    if (!source) return JS_EXCEPTION;
    if (destination != source)
        ath_matrix4_copy(destination, source);
    return JS_DupValue(ctx, this_val);
}

static JSValue js_matrix4_invert(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    AthenaMatrix4 *matrix = matrix4_from_value(ctx, this_val);
    AthenaMatrix4 result;
    if (!matrix) return JS_EXCEPTION;
    if (argc != 0) return JS_ThrowTypeError(ctx, "Matrix4.invert expects no arguments");
    if (!ath_matrix4_inverse(&result, matrix))
        return JS_ThrowRangeError(ctx, "Matrix4 is not invertible");
    *matrix = result;
    return JS_DupValue(ctx, this_val);
}

static JSValue js_matrix4_multiply(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    AthenaMatrix4 *a = matrix4_from_value(ctx, this_val);
    AthenaMatrix4 *b;
    AthenaMatrix4 result;
    if (!a || argc != 1) return a ? JS_ThrowTypeError(ctx, "Matrix4.multiply expects one argument") : JS_EXCEPTION;
    b = matrix4_from_value(ctx, argv[0]);
    if (!b) return JS_EXCEPTION;
    ath_matrix4_multiply(&result, a, b);
    AthenaMatrix4 *copy = memalign(16, sizeof(*copy));
    if (!copy) return JS_ThrowOutOfMemory(ctx);
    *copy = result;
    JSValue object = JS_NewObjectClass(ctx, matrix4_class_id);
    if (JS_IsException(object)) {
        free(copy);
        return object;
    }
    JS_SetOpaque(object, copy);
    return object;
}

static JSValue matrix4_to_string(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    AthenaMatrix4 *matrix = matrix4_from_value(ctx, this_val);
    char text[256];
    if (!matrix) return JS_EXCEPTION;
    snprintf(text, sizeof(text),
        "[%g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g]",
        matrix->value[0], matrix->value[1], matrix->value[2], matrix->value[3],
        matrix->value[4], matrix->value[5], matrix->value[6], matrix->value[7],
        matrix->value[8], matrix->value[9], matrix->value[10], matrix->value[11],
        matrix->value[12], matrix->value[13], matrix->value[14], matrix->value[15]);
    return JS_NewString(ctx, text);
}

static JSValue matrix4_get_index(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    AthenaMatrix4 *matrix = matrix4_from_value(ctx, this_val);
    int32_t index;
    if (!matrix) return JS_EXCEPTION;
    if (argc != 1 || JS_ToInt32(ctx, &index, argv[0]) ||
        index < 0 || index >= 16)
        return JS_ThrowRangeError(ctx, "Matrix4 index must be between 0 and 15");
    return JS_NewFloat32(ctx, matrix->value[index]);
}

static JSValue matrix4_equals(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    AthenaMatrix4 *matrix = matrix4_from_value(ctx, this_val);
    AthenaMatrix4 *other;
    if (!matrix) return JS_EXCEPTION;
    if (argc != 1) return JS_ThrowTypeError(ctx, "Matrix4.equals expects one argument");
    other = matrix4_from_value(ctx, argv[0]);
    if (!other) return JS_EXCEPTION;
    return JS_NewBool(ctx, ath_matrix4_equals(matrix, other));
}

static JSValue matrix4_equals_epsilon(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    AthenaMatrix4 *matrix = matrix4_from_value(ctx, this_val);
    AthenaMatrix4 *other;
    float epsilon;
    if (!matrix) return JS_EXCEPTION;
    if (argc != 2)
        return JS_ThrowTypeError(ctx, "Matrix4.equalsEpsilon expects two arguments");
    other = matrix4_from_value(ctx, argv[0]);
    if (!other) return JS_EXCEPTION;
    if (JS_ToFloat32(ctx, &epsilon, argv[1]) || epsilon < 0.0f)
        return JS_ThrowRangeError(ctx, "Matrix4 epsilon must be non-negative");
    return JS_NewBool(ctx, ath_matrix4_equals_epsilon(matrix, other, epsilon));
}

static JSValue matrix4_set_index(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    AthenaMatrix4 *matrix = matrix4_from_value(ctx, this_val);
    int32_t index;
    float value;
    if (!matrix) return JS_EXCEPTION;
    if (argc != 2 || JS_ToInt32(ctx, &index, argv[0]) ||
        index < 0 || index >= 16)
        return JS_ThrowRangeError(ctx, "Matrix4 index must be between 0 and 15");
    if (JS_ToFloat32(ctx, &value, argv[1]))
        return JS_EXCEPTION;
    matrix->value[index] = value;
    return JS_DupValue(ctx, this_val);
}

static JSClassDef matrix4_class = {
    "Matrix4",
    .finalizer = matrix4_finalizer
};

static const JSCFunctionListEntry matrix4_funcs[] = {
    JS_CFUNC_DEF("toArray", 0, matrix4_to_array),
    JS_CFUNC_DEF("fromArray", 1, matrix4_from_array),
    JS_CFUNC_DEF("clone", 0, matrix4_clone),
    JS_CFUNC_DEF("copy", 1, js_matrix4_copy),
    JS_CFUNC_DEF("multiply", 1, js_matrix4_multiply),
    JS_CFUNC_DEF("identity", 0, js_matrix4_identity),
    JS_CFUNC_DEF("transpose", 0, js_matrix4_transpose),
    JS_CFUNC_DEF("invert", 0, js_matrix4_invert),
    JS_CFUNC_DEF("get", 1, matrix4_get_index),
    JS_CFUNC_DEF("set", 2, matrix4_set_index),
    JS_CFUNC_DEF("equals", 1, matrix4_equals),
    JS_CFUNC_DEF("equalsEpsilon", 2, matrix4_equals_epsilon),
    JS_CFUNC_DEF("toString", 0, matrix4_to_string),
    JS_PROP_INT32_DEF("length", 16, JS_PROP_CONFIGURABLE)
};

static int matrix4_module_init(JSContext *ctx, JSModuleDef *module) {
    if (!matrix4_class_registered) {
        JS_NewClassID(&matrix4_class_id);
        if (JS_NewClass(JS_GetRuntime(ctx), matrix4_class_id, &matrix4_class) < 0)
            return -1;
        matrix4_class_registered = true;
    }
    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, matrix4_funcs, countof(matrix4_funcs));
    JSValue constructor = JS_NewCFunction2(ctx, matrix4_ctor, "Matrix4", 16,
        JS_CFUNC_constructor_or_func, 0);
    JS_SetConstructor(ctx, constructor, proto);
    JS_SetClassProto(ctx, matrix4_class_id, proto);
    return JS_SetModuleExport(ctx, module, "Matrix4", constructor);
}

JSModuleDef *athena_matrix4_init(JSContext *ctx) {
    JSModuleDef *module = athena_push_module(ctx, matrix4_module_init, NULL, 0, "Matrix4");
    if (module) JS_AddModuleExport(ctx, module, "Matrix4");
    return module;
}
