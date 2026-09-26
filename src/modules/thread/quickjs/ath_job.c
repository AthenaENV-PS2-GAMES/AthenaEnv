#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include <ath_env.h>
#include <ath_gil.h>
#include <athena/js/job.h>

/* Delay between two checks of a job someone awaits. */
#define JOB_TICK_MS 4

/*
 * A Job object. The worker never runs script code: the script polls, or
 * awaits the job, which polls from a timer. The outcome is converted once
 * (kind->settle) and kept.
 */
typedef struct {
    const AthenaJsJobKind *kind;
    AthenaJob *job;
    void *user;
    bool settled;
    AthenaJobState state;       /* once settled; FAILED also when settle() reported a failure */
    JSValue outcome;            /* value or error once settled */
    JSValue promise;            /* created by the first then() */
    JSValue resolve, reject;    /* until the promise is settled */
} JsJob;

static JSClassID js_job_class_id;

static void js_job_finalizer(JSRuntime *rt, JSValue value) {
    JsJob *handle = JS_GetOpaque(value, js_job_class_id);

    if (!handle)
        return;
    if (handle->kind->release)
        handle->kind->release(handle->job);
    else
        athena_job_release(handle->job);
    if (handle->kind->free_user)
        handle->kind->free_user(rt, handle->user);
    JS_FreeValueRT(rt, handle->outcome);
    JS_FreeValueRT(rt, handle->promise);
    JS_FreeValueRT(rt, handle->resolve);
    JS_FreeValueRT(rt, handle->reject);
    free(handle);
}

static void js_job_mark(JSRuntime *rt, JSValueConst value, JS_MarkFunc *mark) {
    JsJob *handle = JS_GetOpaque(value, js_job_class_id);

    if (!handle)
        return;
    JS_MarkValue(rt, handle->outcome, mark);
    JS_MarkValue(rt, handle->promise, mark);
    JS_MarkValue(rt, handle->resolve, mark);
    JS_MarkValue(rt, handle->reject, mark);
}

static JSClassDef js_job_class = {
    "Job",
    .finalizer = js_job_finalizer,
    .gc_mark = js_job_mark,
};

static JsJob *js_job_get(JSContext *ctx, JSValueConst value, const AthenaJsJobKind *kind,
    const char *name) {
    JsJob *handle = JS_GetOpaque(value, js_job_class_id);

    if (!handle || (kind && handle->kind != kind)) {
        JS_ThrowTypeError(ctx, "%s: expected a %s job", name, kind ? kind->owner : "background");
        return NULL;
    }
    return handle;
}

JSValue athena_js_job_new(JSContext *ctx, const AthenaJsJobKind *kind, AthenaJob *job,
    void *user, const char *name) {
    JsJob *handle;
    JSValue object;

    if (!job) {
        if (kind->free_user)
            kind->free_user(JS_GetRuntime(ctx), user);
        return JS_ThrowInternalError(ctx, "%s: cannot start the background job", name);
    }
    handle = calloc(1, sizeof(*handle));
    object = handle ? JS_NewObjectClass(ctx, js_job_class_id) : JS_EXCEPTION;
    if (JS_IsException(object)) {
        free(handle);
        if (kind->release)
            kind->release(job);
        else
            athena_job_release(job);
        if (kind->free_user)
            kind->free_user(JS_GetRuntime(ctx), user);
        return handle ? object : JS_ThrowOutOfMemory(ctx);
    }
    handle->kind = kind;
    handle->job = job;
    handle->user = user;
    handle->state = ATHENA_JOB_RUNNING;
    handle->outcome = JS_UNDEFINED;
    handle->promise = JS_UNDEFINED;
    handle->resolve = JS_UNDEFINED;
    handle->reject = JS_UNDEFINED;
    JS_SetOpaque(object, handle);
    return object;
}

bool athena_js_job_is(JSValueConst value, const AthenaJsJobKind *kind) {
    JsJob *handle = JS_GetOpaque(value, js_job_class_id);
    return handle && (!kind || handle->kind == kind);
}

AthenaJob *athena_js_job_native(JSContext *ctx, JSValueConst value, const AthenaJsJobKind *kind,
    const char *name) {
    JsJob *handle = js_job_get(ctx, value, kind, name);
    return handle ? handle->job : NULL;
}

/* Converts the outcome once, the first time the job is seen settled; -1 with an exception pending. */
static int js_job_settle(JSContext *ctx, JsJob *handle) {
    AthenaJobState state;
    JSValue outcome = JS_UNDEFINED;
    bool failed = false;
    int result;

    if (handle->settled)
        return 0;
    state = athena_job_state(handle->job, &result);
    if (state == ATHENA_JOB_RUNNING)
        return 0;
    if (handle->kind->settle(ctx, handle->job, state, result, handle->user, &outcome, &failed) < 0) {
        JS_FreeValue(ctx, outcome);
        return -1;
    }
    handle->outcome = outcome;
    handle->state = state == ATHENA_JOB_DONE && failed ? ATHENA_JOB_FAILED : state;
    handle->settled = true;
    return 0;
}

static const char *js_job_state_name(AthenaJobState state) {
    switch (state) {
    case ATHENA_JOB_RUNNING: return "running";
    case ATHENA_JOB_DONE: return "done";
    case ATHENA_JOB_CANCELLED: return "cancelled";
    default: return "failed";
    }
}

static JSValue js_job_status(JSContext *ctx, JsJob *handle) {
    JSValue object;

    if (js_job_settle(ctx, handle) < 0)
        return JS_EXCEPTION;
    object = JS_NewObject(ctx);
    if (JS_IsException(object))
        return object;
    JS_DefinePropertyValueStr(ctx, object, "state",
        JS_NewString(ctx, js_job_state_name(handle->state)), JS_PROP_C_W_E);
    if (handle->kind->status)
        handle->kind->status(ctx, handle->job, handle->user, object);
    if (handle->settled)
        JS_DefinePropertyValueStr(ctx, object,
            handle->state == ATHENA_JOB_DONE ? "result" : "error",
            JS_DupValue(ctx, handle->outcome), JS_PROP_C_W_E);
    return object;
}

static JSValue js_job_do_wait(JSContext *ctx, JsJob *handle, int argc, JSValueConst *argv,
    const char *name) {
    int32_t timeout = -1;

    if (argc > 0 && !JS_IsUndefined(argv[0])) {
        double number;
        if (!JS_IsNumber(argv[0]) || JS_ToFloat64(ctx, &number, argv[0]) ||
            !isfinite(number) || number < 0 || number > INT32_MAX)
            return JS_ThrowRangeError(ctx,
                "%s timeout must be a non-negative number of milliseconds", name);
        timeout = (int32_t)number;
    }
    athena_js_gil_unlock();
    athena_job_wait(handle->job, timeout);
    athena_js_gil_lock();
    return js_job_status(ctx, handle);
}

JSValue athena_js_job_poll(JSContext *ctx, JSValueConst job, const AthenaJsJobKind *kind,
    const char *name) {
    JsJob *handle = js_job_get(ctx, job, kind, name);
    return handle ? js_job_status(ctx, handle) : JS_EXCEPTION;
}

JSValue athena_js_job_wait(JSContext *ctx, JSValueConst job, int argc, JSValueConst *argv,
    const AthenaJsJobKind *kind, const char *name) {
    JsJob *handle = js_job_get(ctx, job, kind, name);
    return handle ? js_job_do_wait(ctx, handle, argc, argv, name) : JS_EXCEPTION;
}

JSValue athena_js_job_cancel(JSContext *ctx, JSValueConst job, const AthenaJsJobKind *kind,
    const char *name) {
    JsJob *handle = js_job_get(ctx, job, kind, name);

    if (!handle)
        return JS_EXCEPTION;
    if (handle->kind->cancel)
        handle->kind->cancel(handle->job, handle->user);
    else
        athena_job_cancel(handle->job);
    return JS_UNDEFINED;
}

static JSValue js_job_tick(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
    int magic, JSValue *data);

/* Checks the job again in `delay_ms`, through the event loop's timers. */
static int js_job_schedule(JSContext *ctx, JSValueConst job, int delay_ms) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue set_timeout = JS_GetPropertyStr(ctx, global, "setTimeout");
    JSValue args[2], ret;

    JS_FreeValue(ctx, global);
    if (!JS_IsFunction(ctx, set_timeout)) {
        JS_FreeValue(ctx, set_timeout);
        JS_ThrowInternalError(ctx, "awaiting a job needs setTimeout");
        return -1;
    }
    args[0] = JS_NewCFunctionData(ctx, js_job_tick, 0, 0, 1, (JSValue *)&job);
    args[1] = JS_NewInt32(ctx, delay_ms);
    ret = JS_IsException(args[0]) ? JS_EXCEPTION : JS_Call(ctx, set_timeout, JS_UNDEFINED, 2, args);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, set_timeout);
    if (JS_IsException(ret))
        return -1;
    JS_FreeValue(ctx, ret);
    return 0;
}

/* Timer callback of an awaited job: settles its promise or checks again later. */
static JSValue js_job_tick(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
    int magic, JSValue *data) {
    JsJob *handle = JS_GetOpaque(data[0], js_job_class_id);
    JSValue func, value, ret;

    if (!handle || JS_IsUndefined(handle->resolve))
        return JS_UNDEFINED;
    if (!handle->settled && athena_job_state(handle->job, NULL) == ATHENA_JOB_RUNNING)
        return js_job_schedule(ctx, data[0], JOB_TICK_MS) < 0 ? JS_EXCEPTION : JS_UNDEFINED;

    if (js_job_settle(ctx, handle) < 0) {
        func = handle->reject;
        value = JS_GetException(ctx);
    } else {
        func = handle->state == ATHENA_JOB_DONE ? handle->resolve : handle->reject;
        value = JS_DupValue(ctx, handle->outcome);
    }
    func = JS_DupValue(ctx, func);
    JS_FreeValue(ctx, handle->resolve);
    JS_FreeValue(ctx, handle->reject);
    handle->resolve = JS_UNDEFINED;
    handle->reject = JS_UNDEFINED;
    ret = JS_Call(ctx, func, JS_UNDEFINED, 1, (JSValueConst *)&value);
    JS_FreeValue(ctx, func);
    JS_FreeValue(ctx, value);
    if (JS_IsException(ret))
        return ret;
    JS_FreeValue(ctx, ret);
    return JS_UNDEFINED;
}

/* job.then(): makes a job awaitable. The job is checked every few milliseconds. */
static JSValue js_job_then(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JsJob *handle = js_job_get(ctx, this_val, NULL, "Job.then");
    JSValue then, ret;

    if (!handle)
        return JS_EXCEPTION;
    if (JS_IsUndefined(handle->promise)) {
        JSValue funcs[2];
        JSValue promise = JS_NewPromiseCapability(ctx, funcs);
        if (JS_IsException(promise))
            return promise;
        handle->promise = promise;
        handle->resolve = funcs[0];
        handle->reject = funcs[1];
        if (js_job_schedule(ctx, this_val, 0) < 0)
            return JS_EXCEPTION;
    }
    then = JS_GetPropertyStr(ctx, handle->promise, "then");
    if (JS_IsException(then))
        return then;
    ret = JS_Call(ctx, then, handle->promise, argc, argv);
    JS_FreeValue(ctx, then);
    return ret;
}

static JSValue js_job_poll_method(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    return athena_js_job_poll(ctx, this_val, NULL, "Job.poll");
}

static JSValue js_job_wait_method(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    return athena_js_job_wait(ctx, this_val, argc, argv, NULL, "Job.wait");
}

static JSValue js_job_cancel_method(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    return athena_js_job_cancel(ctx, this_val, NULL, "Job.cancel");
}

static const JSCFunctionListEntry js_job_proto[] = {
    JS_CFUNC_DEF("then", 2, js_job_then),
    JS_CFUNC_DEF("poll", 0, js_job_poll_method),
    JS_CFUNC_DEF("wait", 1, js_job_wait_method),
    JS_CFUNC_DEF("cancel", 0, js_job_cancel_method),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Job", JS_PROP_CONFIGURABLE),
};

void athena_js_job_class_init(JSContext *ctx) {
    JSValue proto;

    if (athena_register_class(ctx, &js_job_class_id, &js_job_class) < 0)
        return;
    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, js_job_proto, countof(js_job_proto));
    JS_SetClassProto(ctx, js_job_class_id, proto);
}
