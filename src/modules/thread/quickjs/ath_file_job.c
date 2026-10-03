#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ath_env.h>
#include <athena/file_job.h>
#include <athena/js/job.h>
#include "ath_thread.h"

/*
 * Thread.readFileAsync(path, { text, maxBytes }): a Job of the file's
 * contents, read on the job pool. Text is decoded as UTF-8 on the script
 * thread; binary data becomes an ArrayBuffer over the bytes read, without a
 * copy.
 */

typedef struct {
    bool text;
    char path[256];
} FileReadUser;

static void file_read_free_buffer(JSRuntime *rt, void *opaque, void *ptr)
{
    free(ptr);
}

static const char *file_read_message(int result)
{
    switch (result) {
    case ATHENA_FILE_READ_OPEN:
        return "cannot open";
    case ATHENA_FILE_READ_TOO_LARGE:
        return "the file is larger than maxBytes:";
    case ATHENA_FILE_READ_NOMEM:
        return "out of memory reading";
    case ATHENA_FILE_READ_CANCELLED:
        return "cancelled reading";
    default:
        return "cannot read";
    }
}

static int file_read_settle(JSContext *ctx, AthenaJob *job, AthenaJobState state, int result,
    void *user, JSValue *outcome, bool *failed)
{
    FileReadUser *read = user;
    uint8_t *data;
    size_t size;

    if (state != ATHENA_JOB_DONE) {
        char message[320];

        snprintf(message, sizeof(message), "Thread.readFileAsync: %s '%s'",
            state == ATHENA_JOB_CANCELLED ? "cancelled reading" : file_read_message(result),
            read->path);
        *failed = true;
        *outcome = JS_NewError(ctx);
        if (JS_IsException(*outcome))
            return -1;
        JS_DefinePropertyValueStr(ctx, *outcome, "message", JS_NewString(ctx, message),
            JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
        JS_DefinePropertyValueStr(ctx, *outcome, "path", JS_NewString(ctx, read->path),
            JS_PROP_C_W_E);
        return 0;
    }
    data = athena_file_read_take(job, &size);
    if (!data) {
        JS_ThrowInternalError(ctx, "Thread.readFileAsync: the data was already taken");
        return -1;
    }
    if (read->text) {
        *outcome = JS_NewStringLen(ctx, (const char *)data, size);
        free(data);
    } else {
        *outcome = JS_NewArrayBuffer(ctx, data, size, file_read_free_buffer, NULL, false);
        if (JS_IsException(*outcome))
            free(data);
    }
    return JS_IsException(*outcome) ? -1 : 0;
}

static void file_read_status(JSContext *ctx, AthenaJob *job, void *user, JSValue object)
{
    size_t done, total;

    athena_file_read_progress(job, &done, &total);
    JS_DefinePropertyValueStr(ctx, object, "bytesDone", JS_NewInt64(ctx, (int64_t)done),
        JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, object, "bytesTotal", JS_NewInt64(ctx, (int64_t)total),
        JS_PROP_C_W_E);
}

static void file_read_free_user(JSRuntime *rt, void *user)
{
    free(user);
}

static const AthenaJsJobKind file_read_kind = {
    .owner = "Thread",
    .settle = file_read_settle,
    .status = file_read_status,
    .free_user = file_read_free_user,
};

JSValue athena_thread_read_file_async(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    const char *name = "Thread.readFileAsync";
    JSValueConst options = argc > 1 ? argv[1] : JS_UNDEFINED;
    FileReadUser *user;
    const char *path;
    double max = 0;
    size_t length;
    AthenaJob *job;

    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "%s expects a file path", name);
    if (!JS_IsUndefined(options) && (!JS_IsObject(options) || JS_IsArray(ctx, options)))
        return JS_ThrowTypeError(ctx, "%s options must be an object", name);
    user = calloc(1, sizeof(*user));
    if (!user)
        return JS_ThrowOutOfMemory(ctx);
    if (JS_IsObject(options)) {
        JSValue text = JS_GetPropertyStr(ctx, options, "text");
        JSValue max_value = JS_GetPropertyStr(ctx, options, "maxBytes");
        int failed = JS_IsException(text) || JS_IsException(max_value);

        if (!failed)
            user->text = JS_ToBool(ctx, text) != 0;
        if (!failed && !JS_IsUndefined(max_value) &&
            (JS_ToFloat64(ctx, &max, max_value) || !(max >= 1.0 && max <= 268435456.0))) {
            if (!JS_IsException(max_value))
                JS_ThrowRangeError(ctx, "%s maxBytes must be from 1 to 268435456", name);
            failed = 1;
        }
        JS_FreeValue(ctx, text);
        JS_FreeValue(ctx, max_value);
        if (failed) {
            free(user);
            return JS_EXCEPTION;
        }
    }
    path = JS_ToCStringLen(ctx, &length, argv[0]);
    if (!path) {
        free(user);
        return JS_EXCEPTION;
    }
    snprintf(user->path, sizeof(user->path), "%s", path);
    job = athena_file_read_submit(path, (size_t)max);
    JS_FreeCString(ctx, path);
    return athena_js_job_new(ctx, &file_read_kind, job, user, name);
}
