#include <ath_env.h>
#include <athena/mutex.h>

static JSClassID js_mutex_class_id;

static void js_mutex_dtor(JSRuntime *rt, JSValue val)
{
    AthenaMutex *mutex = JS_GetOpaque(val, js_mutex_class_id);

    if (mutex) {
        athena_mutex_destroy(mutex);
        JS_SetOpaque(val, NULL);
    }
}

static JSValue js_mutex_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)
{
    JSValue obj;
    AthenaMutex *mutex = athena_mutex_create();

    if (!mutex)
        return JS_EXCEPTION;

    obj = JS_NewObjectClass(ctx, js_mutex_class_id);
    if (JS_IsException(obj)) {
        athena_mutex_destroy(mutex);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, mutex);
    return obj;
}

static JSValue js_mutex_lock(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    AthenaMutex *mutex = JS_GetOpaque(this_val, js_mutex_class_id);
    athena_mutex_lock(mutex);
    return JS_UNDEFINED;
}

static JSValue js_mutex_unlock(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
    AthenaMutex *mutex = JS_GetOpaque(this_val, js_mutex_class_id);
    athena_mutex_unlock(mutex);
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_mutex_proto_funcs[] = {
    JS_CFUNC_DEF("lock", 0, js_mutex_lock),
    JS_CFUNC_DEF("unlock", 0, js_mutex_unlock),
};

static JSClassDef js_mutex_class = {
    "Mutex",
    .finalizer = js_mutex_dtor,
};

static int mutex_init(JSContext *ctx, JSModuleDef *m)
{
    JSValue mutex_proto, mutex_class;

    JS_NewClassID(&js_mutex_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_mutex_class_id, &js_mutex_class);

    mutex_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, mutex_proto, js_mutex_proto_funcs, countof(js_mutex_proto_funcs));

    mutex_class = JS_NewCFunction2(ctx, js_mutex_ctor, "Mutex", 0, JS_CFUNC_constructor_or_func, 0);
    JS_SetConstructor(ctx, mutex_class, mutex_proto);
    JS_SetClassProto(ctx, js_mutex_class_id, mutex_proto);

    return JS_SetModuleExport(ctx, m, "default", mutex_class);
}

JSModuleDef *athena_mutex_init(JSContext *ctx)
{
    JSModuleDef *m = JS_NewCModule(ctx, "Mutex", mutex_init);
    if (!m)
        return NULL;
    JS_AddModuleExport(ctx, m, "default");
    return m;
}
