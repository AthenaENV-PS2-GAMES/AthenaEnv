#include <time.h>

#include <ath_env.h>
#include "../native/timer.h"

static JSClassID athena_timer_class_id;
static bool athena_timer_class_registered;

static void athena_timer_finalizer(JSRuntime *rt, JSValue value) {
    AthenaTimer *timer = JS_GetOpaque(value, athena_timer_class_id);
    if (timer) athena_timer_core_destroy(timer);
}

static JSClassDef athena_timer_class = {
    "AthenaTimer",
    .finalizer = athena_timer_finalizer,
};

static int timer_require_argc(JSContext *ctx, int argc, int expected, const char *name) {
    if (argc != expected) {
        JS_ThrowTypeError(ctx, "%s expects %d argument%s",
            name, expected, expected == 1 ? "" : "s");
        return 0;
    }
    return 1;
}

static AthenaTimer *timer_from_value(JSContext *ctx, JSValueConst value) {
    return JS_GetOpaque2(ctx, value, athena_timer_class_id);
}

static JSValue athena_timer_new(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (!timer_require_argc(ctx, argc, 0, "Timer.new")) return JS_EXCEPTION;

    AthenaTimer *timer = athena_timer_core_create();
    if (!timer) return JS_ThrowInternalError(ctx, "Unable to allocate timer");

    JSValue object = JS_NewObjectClass(ctx, athena_timer_class_id);
    if (JS_IsException(object)) {
        athena_timer_core_destroy(timer);
        return object;
    }
    JS_SetOpaque(object, timer);
    return object;
}

static JSValue athena_timer_get_time(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (!timer_require_argc(ctx, argc, 1, "Timer.getTime")) return JS_EXCEPTION;
    AthenaTimer *timer = timer_from_value(ctx, argv[0]);
    if (!timer) return JS_EXCEPTION;

    return JS_NewInt64(ctx, (int64_t)athena_timer_core_get_time(timer));
}

static JSValue athena_timer_pause(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (!timer_require_argc(ctx, argc, 1, "Timer.pause")) return JS_EXCEPTION;
    AthenaTimer *timer = timer_from_value(ctx, argv[0]);
    if (!timer) return JS_EXCEPTION;

    athena_timer_core_pause(timer);
    return JS_UNDEFINED;
}

static JSValue athena_timer_resume(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (!timer_require_argc(ctx, argc, 1, "Timer.resume")) return JS_EXCEPTION;
    AthenaTimer *timer = timer_from_value(ctx, argv[0]);
    if (!timer) return JS_EXCEPTION;

    athena_timer_core_resume(timer);
    return JS_UNDEFINED;
}

static JSValue athena_timer_reset(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (!timer_require_argc(ctx, argc, 1, "Timer.reset")) return JS_EXCEPTION;
    AthenaTimer *timer = timer_from_value(ctx, argv[0]);
    if (!timer) return JS_EXCEPTION;

    athena_timer_core_reset(timer);
    return JS_UNDEFINED;
}

static JSValue athena_timer_set_time(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (!timer_require_argc(ctx, argc, 2, "Timer.setTime")) return JS_EXCEPTION;
    AthenaTimer *timer = timer_from_value(ctx, argv[0]);
    if (!timer) return JS_EXCEPTION;

    uint32_t value;
    if (JS_ToUint32(ctx, &value, argv[1])) return JS_EXCEPTION;
    athena_timer_core_set_time(timer, (clock_t)value);
    return JS_UNDEFINED;
}

static JSValue athena_timer_is_playing(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (!timer_require_argc(ctx, argc, 1, "Timer.isPlaying")) return JS_EXCEPTION;
    AthenaTimer *timer = timer_from_value(ctx, argv[0]);
    if (!timer) return JS_EXCEPTION;
    return JS_NewBool(ctx, athena_timer_core_is_playing(timer));
}

static JSValue athena_timer_destroy(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (!timer_require_argc(ctx, argc, 1, "Timer.destroy")) return JS_EXCEPTION;
    AthenaTimer *timer = timer_from_value(ctx, argv[0]);
    if (!timer) return JS_EXCEPTION;
    athena_timer_core_destroy(timer);
    JS_SetOpaque((JSValue)argv[0], NULL);
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry timer_module_funcs[] = {
    JS_CFUNC_DEF("new", 0, athena_timer_new),
    JS_CFUNC_DEF("getTime", 1, athena_timer_get_time),
    JS_CFUNC_DEF("setTime", 2, athena_timer_set_time),
    JS_CFUNC_DEF("destroy", 1, athena_timer_destroy),
    JS_CFUNC_DEF("pause", 1, athena_timer_pause),
    JS_CFUNC_DEF("resume", 1, athena_timer_resume),
    JS_CFUNC_DEF("reset", 1, athena_timer_reset),
    JS_CFUNC_DEF("isPlaying", 1, athena_timer_is_playing)
};

static int athena_timer_module_init(JSContext *ctx, JSModuleDef *m) {
    if (!athena_timer_class_registered) {
        JS_NewClassID(&athena_timer_class_id);
        if (JS_NewClass(JS_GetRuntime(ctx), athena_timer_class_id, &athena_timer_class) < 0) {
            return -1;
        }
        athena_timer_class_registered = true;
    }
    return JS_SetModuleExportList(ctx, m, timer_module_funcs, countof(timer_module_funcs));
}

JSModuleDef *athena_timer_init(JSContext *ctx) {
    return athena_push_module(ctx, athena_timer_module_init,
        timer_module_funcs, countof(timer_module_funcs), "Timer");
}
