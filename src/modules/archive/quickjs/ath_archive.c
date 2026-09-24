#include <math.h>
#include <stdarg.h>
#include <stdlib.h>

#include <ath_env.h>
#include <ath_gil.h>
#include <athena/archive.h>

/*
 * Long operations (indexing a tar.gz, reading and extracting entries) run
 * with the GIL released so other script threads keep going. The handle is
 * marked busy meanwhile: any other use of it, including close and the
 * finalizer path, is rejected until the operation returns.
 */
typedef struct ArchiveHandle {
    AthenaArchive *archive;
    bool busy;
} ArchiveHandle;

static JSClassID athena_archive_class_id;

static void athena_archive_finalizer(JSRuntime *rt, JSValue value) {
    ArchiveHandle *handle = JS_GetOpaque(value, athena_archive_class_id);
    if (!handle) return;
    athena_archive_close(handle->archive);
    free(handle);
}

static JSClassDef athena_archive_class = {
    "AthenaArchive",
    .finalizer = athena_archive_finalizer,
};

/* ------------------------------------------------------------------------ */
/* Errors                                                                   */
/* ------------------------------------------------------------------------ */

/* The error object for `code`, with a stable `error.code` string, not thrown. */
static JSValue archive_error_va(JSContext *ctx, int code, const char *error_code, const char *fmt, va_list args) {
    char message[640];
    JSValue error;

    vsnprintf(message, sizeof(message), fmt, args);
    if (code == ATHENA_ARCHIVE_ERR_ARGUMENT || code == ATHENA_ARCHIVE_ERR_UNSUPPORTED)
        JS_ThrowTypeError(ctx, "%s", message);
    else if (code == ATHENA_ARCHIVE_ERR_TOO_LARGE)
        JS_ThrowRangeError(ctx, "%s", message);
    else
        JS_ThrowInternalError(ctx, "%s", message);

    error = JS_GetException(ctx);
    JS_SetPropertyStr(ctx, error, "code",
        JS_NewString(ctx, error_code ? error_code : athena_archive_error_code(code)));
    return error;
}

static JSValue archive_throw(JSContext *ctx, int code, const char *error_code, const char *fmt, ...) {
    va_list args;
    JSValue error;

    va_start(args, fmt);
    error = archive_error_va(ctx, code, error_code, fmt, args);
    va_end(args);
    return JS_Throw(ctx, error);
}

static JSValue archive_error(JSContext *ctx, int code, const char *fmt, ...) {
    va_list args;
    JSValue error;

    va_start(args, fmt);
    error = archive_error_va(ctx, code, NULL, fmt, args);
    va_end(args);
    return error;
}

static JSValue archive_throw_result(JSContext *ctx, const char *name, int code, const char *detail) {
    if (detail && detail[0])
        return archive_throw(ctx, code, NULL, "%s: %s: %s", name, athena_archive_strerror(code), detail);
    return archive_throw(ctx, code, NULL, "%s: %s", name, athena_archive_strerror(code));
}

static JSValue archive_throw_type(JSContext *ctx, const char *fmt, const char *name) {
    return archive_throw(ctx, ATHENA_ARCHIVE_ERR_ARGUMENT, NULL, fmt, name);
}

/* ------------------------------------------------------------------------ */
/* Arguments                                                                */
/* ------------------------------------------------------------------------ */

static int archive_argc(JSContext *ctx, int argc, int minimum, int maximum, const char *name) {
    if (argc < minimum || argc > maximum) {
        if (minimum == maximum)
            archive_throw(ctx, ATHENA_ARCHIVE_ERR_ARGUMENT, NULL, "%s expects %d argument%s",
                name, minimum, minimum == 1 ? "" : "s");
        else
            archive_throw(ctx, ATHENA_ARCHIVE_ERR_ARGUMENT, NULL, "%s expects between %d and %d arguments",
                name, minimum, maximum);
        return 0;
    }
    return 1;
}

static bool archive_arg_present(int argc, JSValueConst *argv, int index) {
    return argc > index && !JS_IsUndefined(argv[index]);
}

/* A usable handle: open and not in the middle of another operation. */
static ArchiveHandle *archive_handle(JSContext *ctx, JSValueConst value, const char *name) {
    ArchiveHandle *handle = JS_GetOpaque2(ctx, value, athena_archive_class_id);
    if (!handle) {
        archive_throw(ctx, ATHENA_ARCHIVE_ERR_ARGUMENT, "CLOSED",
            "%s: the archive is closed or is not an Archive handle", name);
        return NULL;
    }
    if (handle->busy) {
        archive_throw(ctx, ATHENA_ARCHIVE_ERR_IO, "BUSY",
            "%s: the archive is in use by another operation", name);
        return NULL;
    }
    return handle;
}

/* Non-empty string. The caller frees it with JS_FreeCString. */
static const char *archive_string_arg(JSContext *ctx, JSValueConst value, const char *name) {
    const char *text;

    if (!JS_IsString(value)) {
        archive_throw_type(ctx, "%s must be a string", name);
        return NULL;
    }
    text = JS_ToCString(ctx, value);
    if (text && !text[0]) {
        JS_FreeCString(ctx, text);
        archive_throw_type(ctx, "%s must not be empty", name);
        return NULL;
    }
    return text;
}

static int archive_options_arg(JSContext *ctx, int argc, JSValueConst *argv, int index, const char *name) {
    if (!archive_arg_present(argc, argv, index))
        return 0;
    if (!JS_IsObject(argv[index]) || JS_IsFunction(ctx, argv[index]) || JS_IsArray(ctx, argv[index])) {
        archive_throw_type(ctx, "%s options must be an object", name);
        return -1;
    }
    return 1;
}

/* Reads options.maxSize: a non-negative integer, 0 meaning the default. */
static int archive_max_size_option(JSContext *ctx, JSValueConst options, const char *name, uint64_t *out) {
    JSValue value = JS_GetPropertyStr(ctx, options, "maxSize");
    double number;
    int ret = 0;

    if (JS_IsException(value)) return -1;
    if (!JS_IsUndefined(value)) {
        if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &number, value) ||
            !isfinite(number) || number < 0 || floor(number) != number || number > 9007199254740991.0) {
            archive_throw(ctx, ATHENA_ARCHIVE_ERR_TOO_LARGE, "INVALID_ARGUMENT",
                "%s options.maxSize must be a non-negative integer", name);
            ret = -1;
        } else {
            *out = (uint64_t)number;
        }
    }
    JS_FreeValue(ctx, value);
    return ret;
}

static int archive_bool_option(JSContext *ctx, JSValueConst options, const char *key,
    const char *name, bool *out) {
    JSValue value = JS_GetPropertyStr(ctx, options, key);
    int ret = 0;

    if (JS_IsException(value)) return -1;
    if (!JS_IsUndefined(value)) {
        if (!JS_IsBool(value)) {
            archive_throw(ctx, ATHENA_ARCHIVE_ERR_ARGUMENT, NULL, "%s options.%s must be a boolean", name, key);
            ret = -1;
        } else {
            *out = JS_ToBool(ctx, value);
        }
    }
    JS_FreeValue(ctx, value);
    return ret;
}

/* Returns the function (owned) or JS_UNDEFINED; JS_EXCEPTION on a bad type. */
static JSValue archive_function_option(JSContext *ctx, JSValueConst options, const char *key, const char *name) {
    JSValue value = JS_GetPropertyStr(ctx, options, key);

    if (JS_IsException(value) || JS_IsUndefined(value)) return value;
    if (!JS_IsFunction(ctx, value)) {
        JS_FreeValue(ctx, value);
        archive_throw(ctx, ATHENA_ARCHIVE_ERR_ARGUMENT, NULL, "%s options.%s must be a function", name, key);
        return JS_EXCEPTION;
    }
    return value;
}

/* Bytes of an ArrayBuffer or a typed array. The value keeps them alive. */
static int archive_bytes_arg(JSContext *ctx, JSValueConst value, const char *name,
    const uint8_t **out, size_t *out_size) {
    size_t offset, length, element;
    size_t buffer_size;
    uint8_t *data;
    JSValue buffer;

    if (JS_IsObject(value)) {
        data = JS_GetArrayBuffer(ctx, &buffer_size, value);
        if (data) {
            *out = data;
            *out_size = buffer_size;
            return 0;
        }
        /* Not an ArrayBuffer: drop that error and try a typed array. */
        JS_FreeValue(ctx, JS_GetException(ctx));

        buffer = JS_GetTypedArrayBuffer(ctx, value, &offset, &length, &element);
        if (!JS_IsException(buffer)) {
            data = JS_GetArrayBuffer(ctx, &buffer_size, buffer);
            JS_FreeValue(ctx, buffer);
            if (!data && length) return -1;
            *out = data ? data + offset : NULL;
            *out_size = length;
            return 0;
        }
        JS_FreeValue(ctx, JS_GetException(ctx));
    }
    archive_throw_type(ctx, "%s must be an ArrayBuffer or a typed array", name);
    return -1;
}

static void archive_free_buffer(JSRuntime *rt, void *opaque, void *ptr) {
    free(ptr);
}

static JSValue archive_new_buffer(JSContext *ctx, void *data, size_t size) {
    JSValue buffer = JS_NewArrayBuffer(ctx, data, size, archive_free_buffer, NULL, 0);
    if (JS_IsException(buffer)) free(data);
    return buffer;
}

static JSValue archive_entry_object(JSContext *ctx, const AthenaArchiveEntry *entry) {
    JSValue item = JS_NewObject(ctx);
    if (JS_IsException(item)) return item;
    JS_DefinePropertyValueStr(ctx, item, "name", JS_NewString(ctx, entry->name), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, item, "size", JS_NewInt64(ctx, (int64_t)entry->size), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, item, "compressedSize",
        JS_NewInt64(ctx, (int64_t)entry->compressed_size), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, item, "mtime", JS_NewUint32(ctx, entry->mtime), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, item, "dir", JS_NewBool(ctx, entry->dir), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, item, "encrypted", JS_NewBool(ctx, entry->encrypted), JS_PROP_C_W_E);
    return item;
}

/* ------------------------------------------------------------------------ */
/* Handles                                                                  */
/* ------------------------------------------------------------------------ */

static JSValue athena_archive_open_js(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaArchive *archive = NULL;
    ArchiveHandle *handle;
    const char *path;
    JSValue object;
    int ret;

    if (!archive_argc(ctx, argc, 1, 1, "Archive.open")) return JS_EXCEPTION;
    path = archive_string_arg(ctx, argv[0], "Archive.open path");
    if (!path) return JS_EXCEPTION;

    athena_js_gil_unlock();
    ret = athena_archive_open(path, &archive);
    athena_js_gil_lock();
    if (ret < 0) {
        archive_throw_result(ctx, "Archive.open", ret, path);
        JS_FreeCString(ctx, path);
        return JS_EXCEPTION;
    }
    JS_FreeCString(ctx, path);

    handle = calloc(1, sizeof(*handle));
    if (!handle) {
        athena_archive_close(archive);
        return JS_ThrowOutOfMemory(ctx);
    }
    handle->archive = archive;

    object = JS_NewObjectClass(ctx, athena_archive_class_id);
    if (JS_IsException(object)) {
        athena_archive_close(archive);
        free(handle);
        return object;
    }
    JS_SetOpaque(object, handle);
    return object;
}

static JSValue athena_archive_close_js(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    ArchiveHandle *handle;
    int ret;

    if (!archive_argc(ctx, argc, 1, 1, "Archive.close")) return JS_EXCEPTION;
    handle = archive_handle(ctx, argv[0], "Archive.close");
    if (!handle) return JS_EXCEPTION;

    JS_SetOpaque((JSValue)argv[0], NULL);
    ret = athena_archive_close(handle->archive);
    free(handle);
    if (ret < 0) return archive_throw_result(ctx, "Archive.close", ret, NULL);
    return JS_UNDEFINED;
}

static JSValue athena_archive_type_js(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    ArchiveHandle *handle;

    if (!archive_argc(ctx, argc, 1, 1, "Archive.type")) return JS_EXCEPTION;
    handle = archive_handle(ctx, argv[0], "Archive.type");
    if (!handle) return JS_EXCEPTION;
    switch (athena_archive_type(handle->archive)) {
    case ATHENA_ARCHIVE_ZIP: return JS_NewString(ctx, "zip");
    case ATHENA_ARCHIVE_TAR: return JS_NewString(ctx, "tar");
    default: return JS_NewString(ctx, "gz");
    }
}

static JSValue athena_archive_list_js(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const AthenaArchiveEntry *entries;
    ArchiveHandle *handle;
    JSValue array;
    int count, ret;

    if (!archive_argc(ctx, argc, 1, 1, "Archive.list")) return JS_EXCEPTION;
    handle = archive_handle(ctx, argv[0], "Archive.list");
    if (!handle) return JS_EXCEPTION;

    handle->busy = true;
    athena_js_gil_unlock();
    ret = athena_archive_entries(handle->archive, &entries, &count);
    athena_js_gil_lock();
    handle->busy = false;
    if (ret < 0)
        return archive_throw_result(ctx, "Archive.list", ret, athena_archive_error_detail(handle->archive));

    array = JS_NewArray(ctx);
    if (JS_IsException(array)) return array;
    for (int i = 0; i < count; i++) {
        JSValue item = archive_entry_object(ctx, &entries[i]);
        if (JS_IsException(item)) {
            JS_FreeValue(ctx, array);
            return item;
        }
        JS_DefinePropertyValueUint32(ctx, array, (uint32_t)i, item, JS_PROP_C_W_E);
    }
    return array;
}

static JSValue athena_archive_read_js(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    ArchiveHandle *handle;
    const char *name = NULL;
    uint64_t max_size = 0;
    void *data = NULL;
    size_t size = 0;
    int ret;

    if (!archive_argc(ctx, argc, 1, 3, "Archive.read")) return JS_EXCEPTION;
    handle = archive_handle(ctx, argv[0], "Archive.read");
    if (!handle) return JS_EXCEPTION;
    ret = archive_options_arg(ctx, argc, argv, 2, "Archive.read");
    if (ret < 0) return JS_EXCEPTION;
    if (ret > 0 && archive_max_size_option(ctx, argv[2], "Archive.read", &max_size) < 0)
        return JS_EXCEPTION;
    if (max_size > SIZE_MAX)
        max_size = SIZE_MAX;
    if (archive_arg_present(argc, argv, 1)) {
        name = archive_string_arg(ctx, argv[1], "Archive.read name");
        if (!name) return JS_EXCEPTION;
    }

    handle->busy = true;
    athena_js_gil_unlock();
    ret = athena_archive_read(handle->archive, name, (size_t)max_size, &data, &size);
    athena_js_gil_lock();
    handle->busy = false;
    if (name) JS_FreeCString(ctx, name);
    if (ret < 0)
        return archive_throw_result(ctx, "Archive.read", ret, athena_archive_error_detail(handle->archive));
    return archive_new_buffer(ctx, data, size);
}

/* ------------------------------------------------------------------------ */
/* Extraction                                                               */
/* ------------------------------------------------------------------------ */

/* JS callbacks run from native code with the GIL released around them. */
typedef struct ExtractHooks {
    JSContext *ctx;
    JSValue filter;
    JSValue progress;
    bool threw;
} ExtractHooks;

static int archive_hook_filter(const AthenaArchiveEntry *entry, void *user) {
    ExtractHooks *hooks = user;
    JSContext *ctx = hooks->ctx;
    JSValue item, result;
    int keep;

    athena_js_gil_lock();
    item = archive_entry_object(ctx, entry);
    result = JS_IsException(item) ? JS_EXCEPTION : JS_Call(ctx, hooks->filter, JS_UNDEFINED, 1, &item);
    JS_FreeValue(ctx, item);
    if (JS_IsException(result)) {
        hooks->threw = true;
        keep = -1;
    } else {
        keep = JS_ToBool(ctx, result) ? 1 : 0;
    }
    JS_FreeValue(ctx, result);
    athena_js_gil_unlock();
    return keep;
}

static int archive_hook_progress(const AthenaArchiveEntry *entry, int index, int count, void *user) {
    ExtractHooks *hooks = user;
    JSContext *ctx = hooks->ctx;
    JSValue args[3], result;
    int ret = 0;

    athena_js_gil_lock();
    args[0] = archive_entry_object(ctx, entry);
    args[1] = JS_NewInt32(ctx, index);
    args[2] = JS_NewInt32(ctx, count);
    result = JS_IsException(args[0]) ? JS_EXCEPTION : JS_Call(ctx, hooks->progress, JS_UNDEFINED, 3, args);
    JS_FreeValue(ctx, args[0]);
    if (JS_IsException(result)) {
        hooks->threw = true;
        ret = -1;
    } else if (JS_IsBool(result) && !JS_ToBool(ctx, result)) {
        ret = -1; /* returning false cancels */
    }
    JS_FreeValue(ctx, result);
    athena_js_gil_unlock();
    return ret;
}

/* Parses (destination?, options?) at argv[first] and runs the extraction. */
static JSValue archive_extract_common(JSContext *ctx, ArchiveHandle *handle, int argc, JSValueConst *argv,
    int first, const char *name) {
    AthenaArchiveExtractOptions options = { .overwrite = true };
    ExtractHooks hooks = { ctx, JS_UNDEFINED, JS_UNDEFINED, false };
    const char *dest = NULL;
    JSValue result = JS_EXCEPTION;
    int ret;

    ret = archive_options_arg(ctx, argc, argv, first + 1, name);
    if (ret < 0) return JS_EXCEPTION;
    if (ret > 0) {
        JSValueConst object = argv[first + 1];
        if (archive_bool_option(ctx, object, "overwrite", name, &options.overwrite) < 0 ||
            archive_max_size_option(ctx, object, name, &options.max_size) < 0)
            return JS_EXCEPTION;
        hooks.filter = archive_function_option(ctx, object, "filter", name);
        if (JS_IsException(hooks.filter)) return JS_EXCEPTION;
        hooks.progress = archive_function_option(ctx, object, "onProgress", name);
        if (JS_IsException(hooks.progress)) goto out;
    }
    if (archive_arg_present(argc, argv, first)) {
        dest = archive_string_arg(ctx, argv[first], "destination");
        if (!dest) goto out;
    }

    options.user = &hooks;
    if (!JS_IsUndefined(hooks.filter)) options.filter = archive_hook_filter;
    if (!JS_IsUndefined(hooks.progress)) options.progress = archive_hook_progress;

    handle->busy = true;
    athena_js_gil_unlock();
    ret = athena_archive_extract(handle->archive, dest, &options);
    athena_js_gil_lock();
    handle->busy = false;

    if (hooks.threw)
        result = JS_EXCEPTION; /* the callback's exception is still pending */
    else if (ret < 0)
        result = archive_throw_result(ctx, name, ret, athena_archive_error_detail(handle->archive));
    else
        result = JS_NewInt32(ctx, ret);

out:
    if (dest) JS_FreeCString(ctx, dest);
    JS_FreeValue(ctx, hooks.filter);
    JS_FreeValue(ctx, hooks.progress);
    return result;
}

static JSValue athena_archive_extract_all_js(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    ArchiveHandle *handle;

    if (!archive_argc(ctx, argc, 1, 3, "Archive.extractAll")) return JS_EXCEPTION;
    handle = archive_handle(ctx, argv[0], "Archive.extractAll");
    if (!handle) return JS_EXCEPTION;
    return archive_extract_common(ctx, handle, argc, argv, 1, "Archive.extractAll");
}

/* Archive.extract(path, destination?, options?): open, extract, close. */
static JSValue athena_archive_extract_js(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
    int magic) {
    const char *name = magic ? "Archive.untar" : "Archive.extract";
    ArchiveHandle handle = { 0 };
    const char *path;
    JSValue result;
    int ret;

    if (!archive_argc(ctx, argc, 1, 3, name)) return JS_EXCEPTION;
    path = archive_string_arg(ctx, argv[0], "path");
    if (!path) return JS_EXCEPTION;

    athena_js_gil_unlock();
    ret = athena_archive_open(path, &handle.archive);
    athena_js_gil_lock();
    if (ret < 0) {
        archive_throw_result(ctx, name, ret, path);
        JS_FreeCString(ctx, path);
        return JS_EXCEPTION;
    }
    JS_FreeCString(ctx, path);

    result = archive_extract_common(ctx, &handle, argc, argv, 1, name);
    athena_archive_close(handle.archive);
    return result;
}

/* ------------------------------------------------------------------------ */
/* In-memory gzip                                                           */
/* ------------------------------------------------------------------------ */

/* The input belongs to the script, so these keep the GIL while they run. */
static JSValue athena_archive_gunzip_js(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const uint8_t *input;
    size_t input_size, size = 0;
    uint64_t max_size = 0;
    void *data = NULL;
    int ret;

    if (!archive_argc(ctx, argc, 1, 2, "Archive.gunzip")) return JS_EXCEPTION;
    if (archive_bytes_arg(ctx, argv[0], "Archive.gunzip data", &input, &input_size) < 0)
        return JS_EXCEPTION;
    ret = archive_options_arg(ctx, argc, argv, 1, "Archive.gunzip");
    if (ret < 0) return JS_EXCEPTION;
    if (ret > 0 && archive_max_size_option(ctx, argv[1], "Archive.gunzip", &max_size) < 0)
        return JS_EXCEPTION;
    if (max_size > SIZE_MAX)
        max_size = SIZE_MAX;

    ret = athena_archive_gunzip(input, input_size, (size_t)max_size, &data, &size);
    if (ret < 0) return archive_throw_result(ctx, "Archive.gunzip", ret, NULL);
    return archive_new_buffer(ctx, data, size);
}

static JSValue athena_archive_gzip_js(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const uint8_t *input;
    size_t input_size, size = 0;
    void *data = NULL;
    int32_t level = -1;
    int ret;

    if (!archive_argc(ctx, argc, 1, 2, "Archive.gzip")) return JS_EXCEPTION;
    if (archive_bytes_arg(ctx, argv[0], "Archive.gzip data", &input, &input_size) < 0)
        return JS_EXCEPTION;
    ret = archive_options_arg(ctx, argc, argv, 1, "Archive.gzip");
    if (ret < 0) return JS_EXCEPTION;
    if (ret > 0) {
        JSValue value = JS_GetPropertyStr(ctx, argv[1], "level");
        if (JS_IsException(value)) return JS_EXCEPTION;
        if (!JS_IsUndefined(value)) {
            double number;
            if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &number, value) ||
                floor(number) != number || number < 0 || number > 9) {
                JS_FreeValue(ctx, value);
                return archive_throw(ctx, ATHENA_ARCHIVE_ERR_TOO_LARGE, "INVALID_ARGUMENT",
                    "Archive.gzip options.level must be an integer from 0 to 9");
            }
            level = (int32_t)number;
        }
        JS_FreeValue(ctx, value);
    }

    ret = athena_archive_gzip(input, input_size, level, &data, &size);
    if (ret < 0) return archive_throw_result(ctx, "Archive.gzip", ret, NULL);
    return archive_new_buffer(ctx, data, size);
}

/* ------------------------------------------------------------------------ */
/* Background jobs                                                          */
/* ------------------------------------------------------------------------ */

/*
 * The worker owns its own archive and never runs script code: the script
 * polls. The result is converted once and kept, so every later poll()
 * returns the same ArrayBuffer or error object.
 */
typedef struct JobHandle {
    AthenaArchiveJob *job;
    const char *name;           /* "Archive.extractAsync" / "Archive.readAsync" */
    bool read;
    bool settled;
    JSValue outcome;            /* result or error once settled */
} JobHandle;

static JSClassID athena_archive_job_class_id;

static void athena_archive_job_finalizer(JSRuntime *rt, JSValue value) {
    JobHandle *handle = JS_GetOpaque(value, athena_archive_job_class_id);
    if (!handle) return;
    /* Cancels and joins: at most one block of work remains. */
    athena_archive_job_destroy(handle->job);
    JS_FreeValueRT(rt, handle->outcome);
    free(handle);
}

static JSClassDef athena_archive_job_class = {
    "AthenaArchiveJob",
    .finalizer = athena_archive_job_finalizer,
};

static JobHandle *archive_job_handle(JSContext *ctx, JSValueConst value, const char *name) {
    JobHandle *handle = JS_GetOpaque2(ctx, value, athena_archive_job_class_id);
    if (!handle)
        archive_throw(ctx, ATHENA_ARCHIVE_ERR_ARGUMENT, NULL, "%s: expected an Archive job", name);
    return handle;
}

/* Callbacks cannot run on the worker thread: say so instead of ignoring them. */
static int archive_reject_callbacks(JSContext *ctx, JSValueConst options, const char *name) {
    static const char *const keys[] = { "filter", "onProgress" };
    for (size_t i = 0; i < countof(keys); i++) {
        JSValue value = JS_GetPropertyStr(ctx, options, keys[i]);
        bool present = !JS_IsUndefined(value);
        JS_FreeValue(ctx, value);
        if (present) {
            archive_throw(ctx, ATHENA_ARCHIVE_ERR_ARGUMENT, NULL,
                "%s options.%s is not supported: use options.include and Archive.poll()", name, keys[i]);
            return -1;
        }
    }
    return 0;
}

/* options.include: array of entry names or "dir/" prefixes. Frees with archive_free_include. */
static int archive_include_option(JSContext *ctx, JSValueConst options, const char *name,
    const char ***out, int *out_count) {
    JSValue value = JS_GetPropertyStr(ctx, options, "include");
    JSValue length_value;
    uint32_t length;
    const char **items;

    *out = NULL;
    *out_count = 0;
    if (JS_IsException(value)) return -1;
    if (JS_IsUndefined(value)) return 0;
    if (!JS_IsArray(ctx, value)) {
        JS_FreeValue(ctx, value);
        archive_throw(ctx, ATHENA_ARCHIVE_ERR_ARGUMENT, NULL, "%s options.include must be an array of strings", name);
        return -1;
    }
    length_value = JS_GetPropertyStr(ctx, value, "length");
    if (JS_ToUint32(ctx, &length, length_value)) {
        JS_FreeValue(ctx, length_value);
        JS_FreeValue(ctx, value);
        return -1;
    }
    JS_FreeValue(ctx, length_value);
    if (length == 0 || length > 4096) {
        JS_FreeValue(ctx, value);
        archive_throw(ctx, ATHENA_ARCHIVE_ERR_ARGUMENT, NULL, "%s options.include must list 1 to 4096 names", name);
        return -1;
    }
    items = calloc(length, sizeof(*items));
    if (!items) {
        JS_FreeValue(ctx, value);
        JS_ThrowOutOfMemory(ctx);
        return -1;
    }
    for (uint32_t i = 0; i < length; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, value, i);
        items[i] = JS_IsString(item) ? JS_ToCString(ctx, item) : NULL;
        JS_FreeValue(ctx, item);
        *out_count = (int)i + 1;
        if (!items[i] || !items[i][0]) {
            *out = items;
            JS_FreeValue(ctx, value);
            archive_throw(ctx, ATHENA_ARCHIVE_ERR_ARGUMENT, NULL,
                "%s options.include must contain non-empty strings", name);
            return -1;
        }
    }
    JS_FreeValue(ctx, value);
    *out = items;
    return 0;
}

static void archive_free_include(JSContext *ctx, const char **items, int count) {
    for (int i = 0; i < count; i++)
        if (items[i]) JS_FreeCString(ctx, items[i]);
    free(items);
}

static JSValue archive_new_job(JSContext *ctx, AthenaArchiveJob *job, const char *name, bool read) {
    JobHandle *handle;
    JSValue object;

    if (!job)
        return archive_throw(ctx, ATHENA_ARCHIVE_ERR_MEMORY, NULL, "%s: cannot start the worker thread", name);
    handle = calloc(1, sizeof(*handle));
    if (!handle) {
        athena_archive_job_destroy(job);
        return JS_ThrowOutOfMemory(ctx);
    }
    handle->job = job;
    handle->name = name;
    handle->read = read;
    handle->outcome = JS_UNDEFINED;
    object = JS_NewObjectClass(ctx, athena_archive_job_class_id);
    if (JS_IsException(object)) {
        athena_archive_job_destroy(job);
        free(handle);
        return object;
    }
    JS_SetOpaque(object, handle);
    return object;
}

/* Archive.extractAsync(path, destination?, { overwrite, maxSize, include }) */
static JSValue athena_archive_extract_async_js(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = "Archive.extractAsync";
    AthenaArchiveJobOptions options = { .overwrite = true };
    const char **include = NULL;
    const char *path = NULL, *dest = NULL;
    int include_count = 0;
    JSValue result = JS_EXCEPTION;
    int ret;

    if (!archive_argc(ctx, argc, 1, 3, name)) return JS_EXCEPTION;
    ret = archive_options_arg(ctx, argc, argv, 2, name);
    if (ret < 0) return JS_EXCEPTION;
    if (ret > 0) {
        if (archive_reject_callbacks(ctx, argv[2], name) < 0 ||
            archive_bool_option(ctx, argv[2], "overwrite", name, &options.overwrite) < 0 ||
            archive_max_size_option(ctx, argv[2], name, &options.max_size) < 0 ||
            archive_include_option(ctx, argv[2], name, &include, &include_count) < 0)
            goto out;
    }
    path = archive_string_arg(ctx, argv[0], "path");
    if (!path) goto out;
    if (archive_arg_present(argc, argv, 1)) {
        dest = archive_string_arg(ctx, argv[1], "destination");
        if (!dest) goto out;
    }
    options.include = include;
    options.include_count = include_count;
    result = archive_new_job(ctx, athena_archive_job_extract(path, dest, &options), name, false);

out:
    if (path) JS_FreeCString(ctx, path);
    if (dest) JS_FreeCString(ctx, dest);
    archive_free_include(ctx, include, include_count);
    return result;
}

/* Archive.readAsync(path, name?, { maxSize }) */
static JSValue athena_archive_read_async_js(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = "Archive.readAsync";
    AthenaArchiveJobOptions options = { 0 };
    const char *path, *entry = NULL;
    JSValue result;
    int ret;

    if (!archive_argc(ctx, argc, 1, 3, name)) return JS_EXCEPTION;
    ret = archive_options_arg(ctx, argc, argv, 2, name);
    if (ret < 0) return JS_EXCEPTION;
    if (ret > 0 && archive_max_size_option(ctx, argv[2], name, &options.max_size) < 0)
        return JS_EXCEPTION;
    if (options.max_size > SIZE_MAX)
        options.max_size = SIZE_MAX;
    path = archive_string_arg(ctx, argv[0], "path");
    if (!path) return JS_EXCEPTION;
    if (archive_arg_present(argc, argv, 1)) {
        entry = archive_string_arg(ctx, argv[1], "Archive.readAsync name");
        if (!entry) {
            JS_FreeCString(ctx, path);
            return JS_EXCEPTION;
        }
    }
    result = archive_new_job(ctx, athena_archive_job_read(path, entry, &options), name, true);
    JS_FreeCString(ctx, path);
    if (entry) JS_FreeCString(ctx, entry);
    return result;
}

static const char *archive_job_state_name(AthenaArchiveJobState state) {
    switch (state) {
    case ATHENA_ARCHIVE_JOB_RUNNING: return "running";
    case ATHENA_ARCHIVE_JOB_DONE: return "done";
    case ATHENA_ARCHIVE_JOB_CANCELLED: return "cancelled";
    default: return "failed";
    }
}

/* Converts the outcome once, the first time the job is seen settled. */
static int archive_job_settle(JSContext *ctx, JobHandle *handle, const AthenaArchiveJobStatus *status) {
    if (handle->settled || status->state == ATHENA_ARCHIVE_JOB_RUNNING)
        return 0;
    if (status->state == ATHENA_ARCHIVE_JOB_DONE) {
        if (handle->read) {
            void *data = NULL;
            size_t size = 0;
            athena_archive_job_take_data(handle->job, &data, &size);
            if (!data)
                data = malloc(1);
            if (!data) {
                JS_ThrowOutOfMemory(ctx);
                return -1;
            }
            handle->outcome = archive_new_buffer(ctx, data, size);
        } else {
            handle->outcome = JS_NewInt32(ctx, status->result);
        }
    } else if (status->detail[0]) {
        handle->outcome = archive_error(ctx, status->result, "%s: %s: %s", handle->name,
            athena_archive_strerror(status->result), status->detail);
    } else {
        handle->outcome = archive_error(ctx, status->result, "%s: %s", handle->name,
            athena_archive_strerror(status->result));
    }
    if (JS_IsException(handle->outcome)) {
        handle->outcome = JS_UNDEFINED;
        return -1;
    }
    handle->settled = true;
    return 0;
}

static JSValue archive_job_status_object(JSContext *ctx, JobHandle *handle) {
    AthenaArchiveJobStatus status;
    JSValue object;

    athena_archive_job_status(handle->job, &status);
    if (archive_job_settle(ctx, handle, &status) < 0)
        return JS_EXCEPTION;

    object = JS_NewObject(ctx);
    if (JS_IsException(object)) return object;
    JS_DefinePropertyValueStr(ctx, object, "state",
        JS_NewString(ctx, archive_job_state_name(status.state)), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, object, "entry", JS_NewString(ctx, status.entry), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, object, "entriesDone", JS_NewInt32(ctx, status.entries_done), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, object, "entriesTotal", JS_NewInt32(ctx, status.entries_total), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, object, "bytesDone",
        JS_NewInt64(ctx, (int64_t)status.bytes_done), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, object, "bytesTotal",
        JS_NewInt64(ctx, (int64_t)status.bytes_total), JS_PROP_C_W_E);
    if (handle->settled) {
        JS_DefinePropertyValueStr(ctx, object,
            status.state == ATHENA_ARCHIVE_JOB_DONE ? "result" : "error",
            JS_DupValue(ctx, handle->outcome), JS_PROP_C_W_E);
    }
    return object;
}

static JSValue athena_archive_poll_js(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JobHandle *handle;

    if (!archive_argc(ctx, argc, 1, 1, "Archive.poll")) return JS_EXCEPTION;
    handle = archive_job_handle(ctx, argv[0], "Archive.poll");
    if (!handle) return JS_EXCEPTION;
    return archive_job_status_object(ctx, handle);
}

static JSValue athena_archive_cancel_js(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JobHandle *handle;

    if (!archive_argc(ctx, argc, 1, 1, "Archive.cancel")) return JS_EXCEPTION;
    handle = archive_job_handle(ctx, argv[0], "Archive.cancel");
    if (!handle) return JS_EXCEPTION;
    athena_archive_job_cancel(handle->job);
    return JS_UNDEFINED;
}

/* Archive.wait(job, timeoutMs?): blocks with the GIL released, then polls. */
static JSValue athena_archive_wait_js(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JobHandle *handle;
    int32_t timeout = -1;

    if (!archive_argc(ctx, argc, 1, 2, "Archive.wait")) return JS_EXCEPTION;
    handle = archive_job_handle(ctx, argv[0], "Archive.wait");
    if (!handle) return JS_EXCEPTION;
    if (archive_arg_present(argc, argv, 1)) {
        double number;
        if (!JS_IsNumber(argv[1]) || JS_ToFloat64(ctx, &number, argv[1]) ||
            !isfinite(number) || number < 0 || number > INT32_MAX)
            return archive_throw(ctx, ATHENA_ARCHIVE_ERR_TOO_LARGE, "INVALID_ARGUMENT",
                "Archive.wait timeout must be a non-negative number of milliseconds");
        timeout = (int32_t)number;
    }

    athena_js_gil_unlock();
    athena_archive_job_wait(handle->job, timeout);
    athena_js_gil_lock();
    return archive_job_status_object(ctx, handle);
}

/* ------------------------------------------------------------------------ */
/* Registration                                                             */
/* ------------------------------------------------------------------------ */

static const JSCFunctionListEntry archive_module_funcs[] = {
    JS_CFUNC_DEF("open", 1, athena_archive_open_js),
    JS_CFUNC_DEF("close", 1, athena_archive_close_js),
    JS_CFUNC_DEF("type", 1, athena_archive_type_js),
    JS_CFUNC_DEF("list", 1, athena_archive_list_js),
    JS_CFUNC_DEF("read", 3, athena_archive_read_js),
    JS_CFUNC_DEF("extractAll", 3, athena_archive_extract_all_js),
    JS_CFUNC_MAGIC_DEF("extract", 3, athena_archive_extract_js, 0),
    JS_CFUNC_MAGIC_DEF("untar", 3, athena_archive_extract_js, 1),
    JS_CFUNC_DEF("gunzip", 2, athena_archive_gunzip_js),
    JS_CFUNC_DEF("gzip", 2, athena_archive_gzip_js),
    JS_CFUNC_DEF("extractAsync", 3, athena_archive_extract_async_js),
    JS_CFUNC_DEF("readAsync", 3, athena_archive_read_async_js),
    JS_CFUNC_DEF("poll", 1, athena_archive_poll_js),
    JS_CFUNC_DEF("wait", 2, athena_archive_wait_js),
    JS_CFUNC_DEF("cancel", 1, athena_archive_cancel_js),
};

static int athena_archive_module_init(JSContext *ctx, JSModuleDef *m) {
    if (athena_register_class(ctx, &athena_archive_class_id, &athena_archive_class) < 0 ||
        athena_register_class(ctx, &athena_archive_job_class_id, &athena_archive_job_class) < 0)
        return -1;
    return JS_SetModuleExportList(ctx, m, archive_module_funcs, countof(archive_module_funcs));
}

JSModuleDef *athena_archive_init(JSContext *ctx) {
    return athena_push_module(ctx, athena_archive_module_init,
        archive_module_funcs, countof(archive_module_funcs), "Archive");
}
