#include <stdlib.h>
#include <string.h>
#include <kernel.h>

#include <ath_env.h>
#include <ath_gil.h>
#include "../native/thread.h"

static JSClassID athena_thread_class_id;
static bool athena_thread_class_registered = false;

typedef struct {
    AthenaThread *thread;
    JSContext *ctx;
    JSValue func;
} AthenaThreadJsObject;

static void athena_thread_invalidate_owner(void *owner) {
    AthenaThreadJsObject *obj = (AthenaThreadJsObject *)owner;
    if (obj) obj->thread = NULL;
}

static void athena_thread_worker(void *arg) {
    AthenaThreadJsObject *obj = (AthenaThreadJsObject *)arg;
    if (!obj || !obj->ctx) {
        if (obj && obj->thread)
            athena_thread_core_worker_finished(obj->thread);
        ExitThread();
        return;
    }

    athena_js_gil_lock();

    JSRuntime *rt = JS_GetRuntime(obj->ctx);
    JS_UpdateStackTop(rt);

    if (JS_IsFunction(obj->ctx, obj->func)) {
        JSValue ret;
        JSValue func_dup = JS_DupValueRT(rt, obj->func);
        ret = JS_Call(obj->ctx, func_dup, JS_UNDEFINED, 0, NULL);
        JS_FreeValueRT(rt, func_dup);
        JS_FreeValueRT(rt, ret);
    }

    athena_js_gil_unlock();
    athena_thread_core_worker_finished(obj->thread);
    ExitThread();
}

static void athena_thread_wait_and_finalize(AthenaThread *thread) {
    athena_thread_core_stop(thread);
    athena_js_gil_unlock();
    athena_thread_core_wait(thread);
    athena_js_gil_lock();
    athena_thread_core_finalize(thread);
}

static void athena_thread_finalizer(JSRuntime *rt, JSValue value) {
    AthenaThreadJsObject *obj = JS_GetOpaque(value, athena_thread_class_id);
    if (obj) {
        if (obj->thread) {
            if (athena_thread_core_is_current(obj->thread)) {
                athena_thread_core_stop(obj->thread);
                return;
            }
            if (athena_thread_core_get_status(obj->thread) >= 0) {
                athena_thread_wait_and_finalize(obj->thread);
            } else {
                athena_thread_core_finalize(obj->thread);
            }
            obj->thread = NULL;
        }
        JS_FreeValueRT(rt, obj->func);
        free(obj);
    }
}

static void athena_thread_gc_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func) {
    AthenaThreadJsObject *obj = JS_GetOpaque(val, athena_thread_class_id);
    if (obj && !JS_IsUndefined(obj->func)) {
        JS_MarkValue(rt, obj->func, mark_func);
    }
}

static JSClassDef athena_thread_class = {
    "AthenaThread",
    .finalizer = athena_thread_finalizer,
    .gc_mark = athena_thread_gc_mark,
};

static AthenaThreadJsObject *thread_from_value(JSContext *ctx, JSValueConst value) {
    AthenaThreadJsObject *obj = JS_GetOpaque2(ctx, value, athena_thread_class_id);
    if (!obj || !obj->thread) {
        JS_ThrowTypeError(ctx, "Thread has already been destroyed");
        return NULL;
    }
    return obj;
}

/* =========================================================
 * QuickJS Namespace Exports
 * ========================================================= */

static JSValue athena_thread_new(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1 || argc > 4) {
        return JS_ThrowTypeError(ctx, "Thread.new expects between 1 and 4 arguments");
    }

    if (!JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "Thread.new expects a callback function as first argument");
    }

    const char *name_str = "Athena: Worker thread";
    const char *allocated_name = NULL;
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        allocated_name = JS_ToCString(ctx, argv[1]);
        if (allocated_name) {
            name_str = allocated_name;
        }
    }

    uint32_t stack_size = ATHENA_THREAD_DEFAULT_STACK_SIZE;
    if (argc >= 3 && !JS_IsUndefined(argv[2])) {
        if (JS_ToUint32(ctx, &stack_size, argv[2]) < 0) {
            if (allocated_name) JS_FreeCString(ctx, allocated_name);
            return JS_EXCEPTION;
        }
        if (stack_size < 2048) {
            if (allocated_name) JS_FreeCString(ctx, allocated_name);
            return JS_ThrowRangeError(ctx, "Thread stack size must be at least 2048 bytes");
        }
    }

    int32_t priority = ATHENA_THREAD_DEFAULT_PRIORITY;
    if (argc >= 4 && !JS_IsUndefined(argv[3])) {
        if (JS_ToInt32(ctx, &priority, argv[3]) < 0) {
            if (allocated_name) JS_FreeCString(ctx, allocated_name);
            return JS_EXCEPTION;
        }
        if (priority < 1 || priority > 127) {
            if (allocated_name) JS_FreeCString(ctx, allocated_name);
            return JS_ThrowRangeError(ctx, "Thread priority must be between 1 and 127");
        }
    }

    AthenaThreadJsObject *obj = (AthenaThreadJsObject *)malloc(sizeof(AthenaThreadJsObject));
    if (!obj) {
        if (allocated_name) JS_FreeCString(ctx, allocated_name);
        return JS_ThrowInternalError(ctx, "Failed to allocate thread context");
    }

    obj->ctx = ctx;
    obj->func = JS_DupValue(ctx, argv[0]);

    obj->thread = athena_thread_core_create(name_str, athena_thread_worker, obj, stack_size, priority);
    if (allocated_name) {
        JS_FreeCString(ctx, allocated_name);
    }

    if (!obj->thread) {
        JS_FreeValue(ctx, obj->func);
        free(obj);
        return JS_ThrowInternalError(ctx, "Failed to create EE kernel thread");
    }

    JSValue js_val = JS_NewObjectClass(ctx, athena_thread_class_id);
    if (JS_IsException(js_val)) {
        athena_thread_core_destroy(obj->thread);
        JS_FreeValue(ctx, obj->func);
        free(obj);
        return js_val;
    }

    JS_SetOpaque(js_val, obj);
    athena_thread_core_set_owner(obj->thread, obj, athena_thread_invalidate_owner);
    return js_val;
}

static JSValue athena_thread_start(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc != 1) return JS_ThrowTypeError(ctx, "Thread.start expects 1 argument");
    AthenaThreadJsObject *obj = thread_from_value(ctx, argv[0]);
    if (!obj) return JS_EXCEPTION;

    return JS_NewInt32(ctx, athena_thread_core_start(obj->thread));
}

static JSValue athena_thread_stop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc != 1) return JS_ThrowTypeError(ctx, "Thread.stop expects 1 argument");
    AthenaThreadJsObject *obj = thread_from_value(ctx, argv[0]);
    if (!obj) return JS_EXCEPTION;

    return JS_NewInt32(ctx, athena_thread_core_stop(obj->thread));
}

static JSValue athena_thread_get_id(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc != 1) return JS_ThrowTypeError(ctx, "Thread.getId expects 1 argument");
    AthenaThreadJsObject *obj = thread_from_value(ctx, argv[0]);
    if (!obj) return JS_EXCEPTION;

    return JS_NewInt32(ctx, athena_thread_core_get_id(obj->thread));
}

static JSValue athena_thread_get_name(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc != 1) return JS_ThrowTypeError(ctx, "Thread.getName expects 1 argument");
    AthenaThreadJsObject *obj = thread_from_value(ctx, argv[0]);
    if (!obj) return JS_EXCEPTION;

    return JS_NewString(ctx, athena_thread_core_get_name(obj->thread));
}

static JSValue athena_thread_set_name(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc != 2) return JS_ThrowTypeError(ctx, "Thread.setName expects 2 arguments");
    AthenaThreadJsObject *obj = thread_from_value(ctx, argv[0]);
    if (!obj) return JS_EXCEPTION;

    const char *name = JS_ToCString(ctx, argv[1]);
    if (!name) return JS_EXCEPTION;

    athena_thread_core_set_name(obj->thread, name);
    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

static JSValue athena_thread_get_status(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc != 1) return JS_ThrowTypeError(ctx, "Thread.getStatus expects 1 argument");
    AthenaThreadJsObject *obj = thread_from_value(ctx, argv[0]);
    if (!obj) return JS_EXCEPTION;

    return JS_NewInt32(ctx, athena_thread_core_get_status(obj->thread));
}

static JSValue athena_thread_destroy(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc != 1) return JS_ThrowTypeError(ctx, "Thread.destroy expects 1 argument");
    AthenaThreadJsObject *obj = thread_from_value(ctx, argv[0]);
    if (!obj) return JS_EXCEPTION;

    if (athena_thread_core_is_current(obj->thread)) {
        athena_thread_core_stop(obj->thread);
        return JS_UNDEFINED;
    }
    athena_thread_wait_and_finalize(obj->thread);
    obj->thread = NULL;
    JS_FreeValue(ctx, obj->func);
    free(obj);
    JS_SetOpaque((JSValue)argv[0], NULL);

    return JS_UNDEFINED;
}

static JSValue athena_thread_list(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc != 0) return JS_ThrowTypeError(ctx, "Thread.list expects 0 arguments");

    AthenaTaskInfo tasks[ATHENA_MAX_TASKS];
    int count = athena_thread_core_get_tasks(tasks, ATHENA_MAX_TASKS);

    JSValue array = JS_NewArray(ctx);
    if (JS_IsException(array)) return array;

    for (int i = 0; i < count; i++) {
        JSValue item = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, item, "id", JS_NewInt32(ctx, tasks[i].id));
        JS_SetPropertyStr(ctx, item, "name", JS_NewString(ctx, tasks[i].name));
        JS_SetPropertyStr(ctx, item, "status", JS_NewInt32(ctx, tasks[i].status));
        JS_SetPropertyStr(ctx, item, "stack", JS_NewUint32(ctx, tasks[i].stack_size));
        JS_SetPropertyUint32(ctx, array, (uint32_t)i, item);
    }

    return array;
}

static JSValue athena_thread_kill(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc != 1) return JS_ThrowTypeError(ctx, "Thread.kill expects 1 argument");

    int32_t id;
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    if (athena_thread_core_is_system_id(id))
        return JS_NewInt32(ctx, -1);

    AthenaThread *thread = athena_thread_core_get_by_id(id);
    int result = athena_thread_core_kill_by_id(id);
    if (result == 0 && thread && !athena_thread_core_is_current(thread)) {
        athena_js_gil_unlock();
        result = athena_thread_core_wait(thread);
        athena_js_gil_lock();
        if (result >= 0)
            athena_thread_core_finalize(thread);
    }
    return JS_NewInt32(ctx, result);
}

/* =========================================================
 * Prototype methods on AthenaThread instance
 * ========================================================= */

static JSValue athena_thread_proto_start(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaThreadJsObject *obj = thread_from_value(ctx, this_val);
    if (!obj) return JS_EXCEPTION;
    return JS_NewInt32(ctx, athena_thread_core_start(obj->thread));
}

static JSValue athena_thread_proto_stop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaThreadJsObject *obj = thread_from_value(ctx, this_val);
    if (!obj) return JS_EXCEPTION;
    return JS_NewInt32(ctx, athena_thread_core_stop(obj->thread));
}

static JSValue athena_thread_proto_destroy(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaThreadJsObject *obj = thread_from_value(ctx, this_val);
    if (!obj) return JS_EXCEPTION;

    if (athena_thread_core_is_current(obj->thread)) {
        athena_thread_core_stop(obj->thread);
        return JS_UNDEFINED;
    }
    athena_thread_wait_and_finalize(obj->thread);
    obj->thread = NULL;
    JS_FreeValue(ctx, obj->func);
    free(obj);
    JS_SetOpaque((JSValue)this_val, NULL);

    return JS_UNDEFINED;
}

static JSValue athena_thread_proto_get_id(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaThreadJsObject *obj = thread_from_value(ctx, this_val);
    if (!obj) return JS_EXCEPTION;
    return JS_NewInt32(ctx, athena_thread_core_get_id(obj->thread));
}

static JSValue athena_thread_proto_get_name(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaThreadJsObject *obj = thread_from_value(ctx, this_val);
    if (!obj) return JS_EXCEPTION;
    return JS_NewString(ctx, athena_thread_core_get_name(obj->thread));
}

static JSValue athena_thread_proto_set_name(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "setName expects 1 argument");
    AthenaThreadJsObject *obj = thread_from_value(ctx, this_val);
    if (!obj) return JS_EXCEPTION;

    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;

    athena_thread_core_set_name(obj->thread, name);
    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

static JSValue athena_thread_proto_get_status(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaThreadJsObject *obj = thread_from_value(ctx, this_val);
    if (!obj) return JS_EXCEPTION;
    return JS_NewInt32(ctx, athena_thread_core_get_status(obj->thread));
}

static const JSCFunctionListEntry thread_proto_funcs[] = {
    JS_CFUNC_DEF("start", 0, athena_thread_proto_start),
    JS_CFUNC_DEF("stop", 0, athena_thread_proto_stop),
    JS_CFUNC_DEF("destroy", 0, athena_thread_proto_destroy),
    JS_CFUNC_DEF("getId", 0, athena_thread_proto_get_id),
    JS_CFUNC_DEF("getName", 0, athena_thread_proto_get_name),
    JS_CFUNC_DEF("setName", 1, athena_thread_proto_set_name),
    JS_CFUNC_DEF("getStatus", 0, athena_thread_proto_get_status),
};

static const JSCFunctionListEntry thread_module_funcs[] = {
    JS_CFUNC_DEF("new", 1, athena_thread_new),
    JS_CFUNC_DEF("start", 1, athena_thread_start),
    JS_CFUNC_DEF("stop", 1, athena_thread_stop),
    JS_CFUNC_DEF("getId", 1, athena_thread_get_id),
    JS_CFUNC_DEF("getName", 1, athena_thread_get_name),
    JS_CFUNC_DEF("setName", 2, athena_thread_set_name),
    JS_CFUNC_DEF("getStatus", 1, athena_thread_get_status),
    JS_CFUNC_DEF("destroy", 1, athena_thread_destroy),
    JS_CFUNC_DEF("list", 0, athena_thread_list),
    JS_CFUNC_DEF("kill", 1, athena_thread_kill),
};

static int athena_thread_module_init(JSContext *ctx, JSModuleDef *m) {
    if (!athena_thread_class_registered) {
        JS_NewClassID(&athena_thread_class_id);
        if (JS_NewClass(JS_GetRuntime(ctx), athena_thread_class_id, &athena_thread_class) < 0) {
            return -1;
        }

        JSValue proto = JS_NewObject(ctx);
        JS_SetPropertyFunctionList(ctx, proto, thread_proto_funcs, countof(thread_proto_funcs));
        JS_SetClassProto(ctx, athena_thread_class_id, proto);

        athena_thread_class_registered = true;
    }

    return JS_SetModuleExportList(ctx, m, thread_module_funcs, countof(thread_module_funcs));
}

JSModuleDef *athena_thread_init(JSContext *ctx) {
    return athena_push_module(ctx, athena_thread_module_init,
        thread_module_funcs, countof(thread_module_funcs), "Thread");
}
