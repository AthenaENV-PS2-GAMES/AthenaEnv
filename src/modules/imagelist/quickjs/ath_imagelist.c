#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <ath_env.h>

#include "../../image/native/image.h"
#include "../../image/quickjs/ath_image.h"
#include "ath_imagelist.h"

typedef struct {
    AthenaImage *image;
    JSValue image_ref;
    JSValue on_load;
    JSValue on_error;
    char *path;
} ImageListJob;

typedef struct {
    ImageListJob *jobs;
    unsigned int count;
    unsigned int capacity;
} AthenaImageList;

static JSClassID imagelist_class_id;

static int imagelist_argc(JSContext *ctx, int argc, int minimum, int maximum,
    const char *name)
{
    if (argc < minimum || argc > maximum) {
        if (minimum == maximum)
            JS_ThrowTypeError(ctx, "%s expects exactly %d arguments", name,
                minimum);
        else
            JS_ThrowTypeError(ctx, "%s expects between %d and %d arguments",
                name, minimum, maximum);
        return 0;
    }
    return 1;
}

static AthenaImageList *imagelist_this(JSContext *ctx, JSValueConst value)
{
    return JS_GetOpaque2(ctx, value, imagelist_class_id);
}

static void imagelist_job_release(JSRuntime *rt, ImageListJob *job,
    int mark_failed)
{
    if (job->image) {
        job->image->loading = false;
        if (mark_failed)
            job->image->failed = true;
    }
    JS_FreeValueRT(rt, job->image_ref);
    JS_FreeValueRT(rt, job->on_load);
    JS_FreeValueRT(rt, job->on_error);
    free(job->path);
    memset(job, 0, sizeof(*job));
}

static void imagelist_finalizer(JSRuntime *rt, JSValue value)
{
    AthenaImageList *list = JS_GetOpaque(value, imagelist_class_id);
    unsigned int i;

    if (!list)
        return;
    for (i = 0; i < list->count; ++i)
        imagelist_job_release(rt, &list->jobs[i], 1);
    free(list->jobs);
    free(list);
    JS_SetOpaque(value, NULL);
}

static int imagelist_reserve(AthenaImageList *list, unsigned int needed)
{
    ImageListJob *jobs;
    unsigned int capacity = list->capacity ? list->capacity * 2 : 4;

    while (capacity < needed)
        capacity *= 2;
    jobs = realloc(list->jobs, capacity * sizeof(*jobs));
    if (!jobs)
        return 0;
    list->jobs = jobs;
    list->capacity = capacity;
    return 1;
}

static int imagelist_options(JSContext *ctx, JSValueConst value, bool *delayed,
    JSValue *on_load, JSValue *on_error)
{
    JSValue delayed_value;

    if (JS_IsUndefined(value))
        return 1;
    if (!JS_IsObject(value) || JS_IsArray(ctx, value)) {
        JS_ThrowTypeError(ctx, "ImageList options must be an object");
        return 0;
    }
    delayed_value = JS_GetPropertyStr(ctx, value, "delayed");
    if (JS_IsException(delayed_value))
        return 0;
    if (!JS_IsUndefined(delayed_value)) {
        if (!JS_IsBool(delayed_value)) {
            JS_FreeValue(ctx, delayed_value);
            JS_ThrowTypeError(ctx, "ImageList options.delayed must be a boolean");
            return 0;
        }
        *delayed = JS_ToBool(ctx, delayed_value) != 0;
    }
    JS_FreeValue(ctx, delayed_value);
    delayed_value = JS_GetPropertyStr(ctx, value, "onLoad");
    if (JS_IsException(delayed_value))
        return 0;
    if (!JS_IsUndefined(delayed_value) && !JS_IsFunction(ctx, delayed_value)) {
        JS_FreeValue(ctx, delayed_value);
        JS_ThrowTypeError(ctx, "ImageList options.onLoad must be a function");
        return 0;
    }
    *on_load = delayed_value;
    delayed_value = JS_GetPropertyStr(ctx, value, "onError");
    if (JS_IsException(delayed_value))
        return 0;
    if (!JS_IsUndefined(delayed_value) && !JS_IsFunction(ctx, delayed_value)) {
        JS_FreeValue(ctx, delayed_value);
        JS_ThrowTypeError(ctx, "ImageList options.onError must be a function");
        return 0;
    }
    *on_error = delayed_value;
    return 1;
}

static JSValue imagelist_load(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    AthenaImageList *list = imagelist_this(ctx, this_val);
    AthenaImage *image;
    ImageListJob *job;
    JSValue image_value;
    const char *path;
    bool delayed = true;
    JSValue on_load = JS_UNDEFINED;
    JSValue on_error = JS_UNDEFINED;

    if (!list || !imagelist_argc(ctx, argc, 1, 2, "ImageList.load"))
        return JS_EXCEPTION;
    if (!JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "ImageList.load path must be a string");
    if (argc == 2 && !imagelist_options(ctx, argv[1], &delayed,
        &on_load, &on_error)) {
        JS_FreeValue(ctx, on_load);
        JS_FreeValue(ctx, on_error);
        return JS_EXCEPTION;
    }
    path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_EXCEPTION;
    image = athena_image_create_empty(delayed);
    if (!image) {
        JS_FreeCString(ctx, path);
        JS_FreeValue(ctx, on_load);
        JS_FreeValue(ctx, on_error);
        return JS_ThrowOutOfMemory(ctx);
    }
    image->loading = true;
    image_value = athena_image_to_value(ctx, image);
    if (JS_IsException(image_value)) {
        athena_image_destroy(image);
        JS_FreeCString(ctx, path);
        JS_FreeValue(ctx, on_load);
        JS_FreeValue(ctx, on_error);
        return image_value;
    }
    if (!imagelist_reserve(list, list->count + 1)) {
        JS_SetOpaque(image_value, NULL);
        athena_image_destroy(image);
        JS_FreeValue(ctx, image_value);
        JS_FreeCString(ctx, path);
        JS_FreeValue(ctx, on_load);
        JS_FreeValue(ctx, on_error);
        return JS_ThrowOutOfMemory(ctx);
    }
    job = &list->jobs[list->count++];
    job->image = image;
    job->image_ref = JS_DupValue(ctx, image_value);
    job->on_load = on_load;
    job->on_error = on_error;
    job->path = strdup(path);
    JS_FreeCString(ctx, path);
    if (!job->path) {
        list->count--;
        JS_SetOpaque(image_value, NULL);
        JS_FreeValue(ctx, image_value);
        JS_FreeValue(ctx, on_load);
        JS_FreeValue(ctx, on_error);
        athena_image_destroy(image);
        return JS_ThrowOutOfMemory(ctx);
    }
    return image_value;
}

static JSValue imagelist_process(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    AthenaImageList *list = imagelist_this(ctx, this_val);
    uint32_t budget = 1;
    unsigned int processed = 0;

    if (!list || !imagelist_argc(ctx, argc, 0, 1, "ImageList.process"))
        return JS_EXCEPTION;
    if (argc == 1 && JS_ToUint32(ctx, &budget, argv[0]))
        return JS_EXCEPTION;
    if (budget == 0)
        return JS_NewUint32(ctx, 0);

    while (processed < budget && list->count > 0) {
        ImageListJob job = list->jobs[0];
        int result;
        JSValue callback_result;

        memmove(&list->jobs[0], &list->jobs[1],
            (list->count - 1) * sizeof(*list->jobs));
        list->count--;
        job.image->loading = false;
        result = athena_image_load_path(job.image, job.path);
        if (result < 0)
            job.image->failed = true;
        if (result == 0 && !JS_IsUndefined(job.on_load)) {
            callback_result = JS_Call(ctx, job.on_load, JS_UNDEFINED, 1,
                &job.image_ref);
            if (JS_IsException(callback_result)) {
                JS_FreeValue(ctx, callback_result);
                JS_FreeValue(ctx, job.image_ref);
                JS_FreeValue(ctx, job.on_load);
                JS_FreeValue(ctx, job.on_error);
                free(job.path);
                return JS_EXCEPTION;
            }
            JS_FreeValue(ctx, callback_result);
        } else if (result < 0 && !JS_IsUndefined(job.on_error)) {
            JSValue args[2];
            args[0] = job.image_ref;
            args[1] = JS_NewString(ctx, job.path);
            callback_result = JS_Call(ctx, job.on_error, JS_UNDEFINED, 2,
                (JSValueConst *)args);
            JS_FreeValue(ctx, args[1]);
            if (JS_IsException(callback_result)) {
                JS_FreeValue(ctx, callback_result);
                JS_FreeValue(ctx, job.image_ref);
                JS_FreeValue(ctx, job.on_load);
                JS_FreeValue(ctx, job.on_error);
                free(job.path);
                return JS_EXCEPTION;
            }
            JS_FreeValue(ctx, callback_result);
        }
        JS_FreeValue(ctx, job.image_ref);
        JS_FreeValue(ctx, job.on_load);
        JS_FreeValue(ctx, job.on_error);
        free(job.path);
        processed++;
    }
    return JS_NewUint32(ctx, processed);
}

static JSValue imagelist_pending(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    AthenaImageList *list = imagelist_this(ctx, this_val);
    if (!list || !imagelist_argc(ctx, argc, 0, 0, "ImageList.pending"))
        return JS_EXCEPTION;
    return JS_NewUint32(ctx, list->count);
}

static JSValue imagelist_clear(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    AthenaImageList *list = imagelist_this(ctx, this_val);
    unsigned int i;
    if (!list || !imagelist_argc(ctx, argc, 0, 0, "ImageList.clear"))
        return JS_EXCEPTION;
    for (i = 0; i < list->count; ++i)
        imagelist_job_release(JS_GetRuntime(ctx), &list->jobs[i], 1);
    list->count = 0;
    return JS_UNDEFINED;
}

static JSClassDef imagelist_class = {
    "ImageList",
    .finalizer = imagelist_finalizer,
};

static const JSCFunctionListEntry imagelist_proto[] = {
    JS_CFUNC_DEF("load", 2, imagelist_load),
    JS_CFUNC_DEF("process", 1, imagelist_process),
    JS_CFUNC_DEF("pending", 0, imagelist_pending),
    JS_CFUNC_DEF("clear", 0, imagelist_clear),
};

static JSValue imagelist_ctor(JSContext *ctx, JSValueConst new_target,
    int argc, JSValueConst *argv)
{
    AthenaImageList *list;
    JSValue proto, object;

    if (!imagelist_argc(ctx, argc, 0, 0, "ImageList"))
        return JS_EXCEPTION;
    list = calloc(1, sizeof(*list));
    if (!list)
        return JS_ThrowOutOfMemory(ctx);
    proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    if (JS_IsException(proto)) {
        free(list);
        return proto;
    }
    object = JS_NewObjectProtoClass(ctx, proto, imagelist_class_id);
    JS_FreeValue(ctx, proto);
    if (JS_IsException(object)) {
        free(list);
        return object;
    }
    JS_SetOpaque(object, list);
    return object;
}

static int imagelist_module_init(JSContext *ctx, JSModuleDef *module)
{
    JSValue proto, constructor;

    JS_NewClassID(&imagelist_class_id);
    JS_NewClass(JS_GetRuntime(ctx), imagelist_class_id, &imagelist_class);
    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, imagelist_proto,
        countof(imagelist_proto));
    JS_SetClassProto(ctx, imagelist_class_id, proto);
    constructor = JS_NewCFunction2(ctx, imagelist_ctor, "ImageList", 0,
        JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, constructor, proto);
    JS_SetModuleExport(ctx, module, "ImageList", constructor);
    return 0;
}

JSModuleDef *athena_imagelist_init(JSContext *ctx)
{
    JSModuleDef *module = athena_push_module(ctx, imagelist_module_init, NULL,
        0, "ImageList");
    if (!module)
        return NULL;
    JS_AddModuleExport(ctx, module, "ImageList");
    return module;
}
