#include <stdlib.h>
#include <malloc.h>

#include <athena/image_list.h>
#include <dbgprintf.h>
#include <ath_bindings.h>

static JSClassID js_imagelist_class_id;

JSClassID get_imglist_class_id(void) {
    return js_imagelist_class_id;
}

static void js_imagelist_dtor(JSRuntime *rt, JSValue val) {
    AthenaImageList *list = JS_GetOpaque(val, js_imagelist_class_id);
    if (!list)
        return;
    athena_image_list_destroy(list);
    JS_SetOpaque(val, NULL);
}

static JSValue js_imagelist_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    AthenaImageList *list = athena_image_list_create();
    JSValue obj = JS_UNDEFINED;
    JSValue proto;

    if (!list)
        return JS_EXCEPTION;

    proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    if (JS_IsException(proto))
        goto fail;
    obj = JS_NewObjectProtoClass(ctx, proto, js_imagelist_class_id);
    JS_FreeValue(ctx, proto);
    if (JS_IsException(obj))
        goto fail;
    JS_SetOpaque(obj, list);
    return obj;

 fail:
    athena_image_list_destroy(list);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue js_imagelist_process(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
    AthenaImageList *handle = JS_GetOpaque2(ctx, this_val, js_imagelist_class_id);
    athena_image_list_process(handle);
    return JS_UNDEFINED;
}

static JSClassDef js_imagelist_class = {
    "ImageList",
    .finalizer = js_imagelist_dtor,
};

static const JSCFunctionListEntry js_imagelist_proto_funcs[] = {
    JS_CFUNC_DEF("process", 0, js_imagelist_process),
};

static int js_imagelist_init(JSContext *ctx, JSModuleDef *m) {
    JSValue imagelist_proto, imagelist_class;
    JS_NewClassID(&js_imagelist_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_imagelist_class_id, &js_imagelist_class);
    imagelist_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, imagelist_proto, js_imagelist_proto_funcs, countof(js_imagelist_proto_funcs));
    imagelist_class = JS_NewCFunction2(ctx, js_imagelist_ctor, "ImageList", 0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, imagelist_class, imagelist_proto);
    JS_SetClassProto(ctx, js_imagelist_class_id, imagelist_proto);
    JS_SetModuleExport(ctx, m, "ImageList", imagelist_class);
    return 0;
}

JSModuleDef *athena_imagelist_init(JSContext *ctx) {
    JSModuleDef *m = JS_NewCModule(ctx, "ImageList", js_imagelist_init);
    if (!m)
        return NULL;
    JS_AddModuleExport(ctx, m, "ImageList");
    dbgprintf("AthenaEnv: %s module pushed at 0x%x\n", "ImageList", m);
    return m;
}
