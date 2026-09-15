#include <ath_env.h>
#include <ath_gil.h>
#include "../native/mutex.h"

static JSClassID athena_mutex_class_id;
static bool athena_mutex_class_registered;

static void athena_mutex_finalizer(JSRuntime *rt, JSValue value) {
    AthenaMutex *mutex = JS_GetOpaque(value, athena_mutex_class_id);
    if (mutex) {
        athena_mutex_core_request_destroy(mutex);
    }
}

static JSClassDef athena_mutex_class = {
    "AthenaMutex",
    .finalizer = athena_mutex_finalizer,
};

static int mutex_require_argc(JSContext *ctx, int argc, int expected, const char *name) {
    if (argc != expected) {
        JS_ThrowTypeError(ctx, "%s expects %d argument%s",
            name, expected, expected == 1 ? "" : "s");
        return 0;
    }
    return 1;
}

static AthenaMutex *mutex_from_value(JSContext *ctx, JSValueConst value) {
    AthenaMutex *mutex = JS_GetOpaque2(ctx, value, athena_mutex_class_id);
    if (!mutex) {
        JS_ThrowTypeError(ctx, "Mutex has already been destroyed");
        return NULL;
    }
    return mutex;
}

static JSValue athena_mutex_new(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (!mutex_require_argc(ctx, argc, 0, "Mutex.new")) return JS_EXCEPTION;

    AthenaMutex *mutex = athena_mutex_core_create();
    if (!mutex) return JS_ThrowInternalError(ctx, "Unable to create mutex");

    JSValue object = JS_NewObjectClass(ctx, athena_mutex_class_id);
    if (JS_IsException(object)) {
        athena_mutex_core_destroy(mutex);
        return object;
    }
    JS_SetOpaque(object, mutex);
    return object;
}

static JSValue athena_mutex_lock(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (!mutex_require_argc(ctx, argc, 1, "Mutex.lock")) return JS_EXCEPTION;
    AthenaMutex *mutex = mutex_from_value(ctx, argv[0]);
    if (!mutex) return JS_EXCEPTION;
    athena_js_gil_unlock();
    int result = athena_mutex_core_lock(mutex);
    athena_js_gil_lock();
    return JS_NewInt32(ctx, result);
}

static JSValue athena_mutex_unlock(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (!mutex_require_argc(ctx, argc, 1, "Mutex.unlock")) return JS_EXCEPTION;
    AthenaMutex *mutex = mutex_from_value(ctx, argv[0]);
    if (!mutex) return JS_EXCEPTION;
    return JS_NewInt32(ctx, athena_mutex_core_unlock(mutex));
}

static JSValue athena_mutex_destroy(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (!mutex_require_argc(ctx, argc, 1, "Mutex.destroy")) return JS_EXCEPTION;
    AthenaMutex *mutex = mutex_from_value(ctx, argv[0]);
    if (!mutex) return JS_EXCEPTION;
    if (athena_mutex_core_destroy(mutex) < 0)
        return JS_ThrowInternalError(ctx, "Mutex is still in use");
    JS_SetOpaque((JSValue)argv[0], NULL);
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry mutex_module_funcs[] = {
    JS_CFUNC_DEF("new", 0, athena_mutex_new),
    JS_CFUNC_DEF("lock", 1, athena_mutex_lock),
    JS_CFUNC_DEF("unlock", 1, athena_mutex_unlock),
    JS_CFUNC_DEF("destroy", 1, athena_mutex_destroy)
};

static int athena_mutex_module_init(JSContext *ctx, JSModuleDef *m) {
    if (!athena_mutex_class_registered) {
        JS_NewClassID(&athena_mutex_class_id);
        if (JS_NewClass(JS_GetRuntime(ctx), athena_mutex_class_id,
                &athena_mutex_class) < 0) {
            return -1;
        }
        athena_mutex_class_registered = true;
    }
    return JS_SetModuleExportList(ctx, m, mutex_module_funcs,
        countof(mutex_module_funcs));
}

JSModuleDef *athena_mutex_init(JSContext *ctx) {
    return athena_push_module(ctx, athena_mutex_module_init,
        mutex_module_funcs, countof(mutex_module_funcs), "Mutex");
}
