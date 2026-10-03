#include <ath_env.h>
#include <athena/timer.h>

static JSValue js_timer_new(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    if (argc != 0)
        return JS_ThrowSyntaxError(ctx, "wrong number of arguments");
    AthenaTimer *timer = athena_timer_create();
    if (!timer)
        return JS_EXCEPTION;
    return JS_NewUint32(ctx, (uint32_t)timer);
}

static JSValue js_timer_get_time(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    AthenaTimer *timer;
    if (argc != 1)
        return JS_ThrowSyntaxError(ctx, "wrong number of arguments");
    JS_ToUint32(ctx, (uint32_t *)&timer, argv[0]);
    return JS_NewInt32(ctx, (int32_t)athena_timer_get_time(timer));
}

static JSValue js_timer_pause(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    AthenaTimer *timer;
    if (argc != 1)
        return JS_ThrowSyntaxError(ctx, "wrong number of arguments");
    JS_ToUint32(ctx, (uint32_t *)&timer, argv[0]);
    athena_timer_pause(timer);
    return JS_UNDEFINED;
}

static JSValue js_timer_resume(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    AthenaTimer *timer;
    if (argc != 1)
        return JS_ThrowSyntaxError(ctx, "wrong number of arguments");
    JS_ToUint32(ctx, (uint32_t *)&timer, argv[0]);
    athena_timer_resume(timer);
    return JS_UNDEFINED;
}

static JSValue js_timer_reset(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    AthenaTimer *timer;
    if (argc != 1)
        return JS_ThrowSyntaxError(ctx, "wrong number of arguments");
    JS_ToUint32(ctx, (uint32_t *)&timer, argv[0]);
    athena_timer_reset(timer);
    return JS_UNDEFINED;
}

static JSValue js_timer_set_time(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    AthenaTimer *timer;
    uint32_t val;
    if (argc != 2)
        return JS_ThrowSyntaxError(ctx, "wrong number of arguments");
    JS_ToUint32(ctx, (uint32_t *)&timer, argv[0]);
    JS_ToUint32(ctx, &val, argv[1]);
    athena_timer_set_time(timer, val);
    return JS_UNDEFINED;
}

static JSValue js_timer_is_playing(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    AthenaTimer *timer;
    if (argc != 1)
        return JS_ThrowSyntaxError(ctx, "wrong number of arguments");
    JS_ToUint32(ctx, (uint32_t *)&timer, argv[0]);
    return JS_NewBool(ctx, athena_timer_is_playing(timer));
}

static JSValue js_timer_destroy(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    AthenaTimer *timer;
    if (argc != 1)
        return JS_ThrowSyntaxError(ctx, "wrong number of arguments");
    JS_ToUint32(ctx, (uint32_t *)&timer, argv[0]);
    athena_timer_destroy(timer);
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry module_funcs[] = {
    JS_CFUNC_DEF("new", 0, js_timer_new),
    JS_CFUNC_DEF("getTime", 1, js_timer_get_time),
    JS_CFUNC_DEF("setTime", 2, js_timer_set_time),
    JS_CFUNC_DEF("destroy", 1, js_timer_destroy),
    JS_CFUNC_DEF("pause", 1, js_timer_pause),
    JS_CFUNC_DEF("resume", 1, js_timer_resume),
    JS_CFUNC_DEF("reset", 1, js_timer_reset),
    JS_CFUNC_DEF("isPlaying", 1, js_timer_is_playing),
};

static int module_init(JSContext *ctx, JSModuleDef *m)
{
    return JS_SetModuleExportList(ctx, m, module_funcs, countof(module_funcs));
}

JSModuleDef *athena_timer_init(JSContext *ctx)
{
    return athena_push_module(ctx, module_init, module_funcs, countof(module_funcs), "Timer");
}
