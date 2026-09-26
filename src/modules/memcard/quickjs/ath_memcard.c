#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ath_env.h>
#include <ath_gil.h>
#include <athena/memcard.h>

#include "ath_memcard.h"

/* Delay between two checks of a job someone awaits. */
#define MC_JOB_TICK_MS 4

/*
 * Every call that talks to the card runs with the GIL released, so other
 * script threads keep going; the native layer serializes the card itself.
 * The strings and buffers it uses are copies owned by C while unlocked.
 */

/* ------------------------------------------------------------------------ */
/* Errors                                                                   */
/* ------------------------------------------------------------------------ */

/* The error object for `code`, with a stable `error.code` string, not thrown. */
static JSValue mc_error_va(JSContext *ctx, int code, const char *fmt, va_list args) {
    char message[640];
    JSValue error;

    vsnprintf(message, sizeof(message), fmt, args);
    if (code == ATHENA_MEMCARD_ERR_ARGUMENT)
        JS_ThrowTypeError(ctx, "%s", message);
    else
        JS_ThrowInternalError(ctx, "%s", message);
    error = JS_GetException(ctx);
    JS_SetPropertyStr(ctx, error, "code", JS_NewString(ctx, athena_memcard_error_code(code)));
    return error;
}

static JSValue mc_throw(JSContext *ctx, int code, const char *fmt, ...) {
    va_list args;
    JSValue error;

    va_start(args, fmt);
    error = mc_error_va(ctx, code, fmt, args);
    va_end(args);
    return JS_Throw(ctx, error);
}

static JSValue mc_error(JSContext *ctx, int code, const char *fmt, ...) {
    va_list args;
    JSValue error;

    va_start(args, fmt);
    error = mc_error_va(ctx, code, fmt, args);
    va_end(args);
    return error;
}

static int mc_argc(JSContext *ctx, int argc, int minimum, int maximum, const char *name) {
    if (argc >= minimum && argc <= maximum)
        return 1;
    if (minimum == maximum)
        mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s expects %d argument%s",
            name, minimum, minimum == 1 ? "" : "s");
    else
        mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s expects between %d and %d arguments",
            name, minimum, maximum);
    return 0;
}

static bool mc_present(int argc, JSValueConst *argv, int index) {
    return argc > index && !JS_IsUndefined(argv[index]);
}

/* ------------------------------------------------------------------------ */
/* Arguments                                                                */
/* ------------------------------------------------------------------------ */

typedef struct {
    int port;
    char path[ATHENA_MEMCARD_PATH_MAX + 1];
} McJsPath;

/* "mc0:/DIR/file" into a port and a normalized card path. */
static int mc_path_arg(JSContext *ctx, JSValueConst value, const char *name, McJsPath *out) {
    const char *text;
    int ret;

    if (!JS_IsString(value)) {
        mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s path must be a string", name);
        return -1;
    }
    text = JS_ToCString(ctx, value);
    if (!text)
        return -1;
    ret = athena_memcard_parse_path(text, &out->port, out->path, sizeof(out->path));
    if (ret < 0)
        mc_throw(ctx, ret, "%s: invalid memory card path \"%s\" (expected \"mc0:/DIR/NAME\" or "
            "\"mc1:/...\"; names are 1-%d bytes without * or ?)", name, text, ATHENA_MEMCARD_NAME_MAX);
    JS_FreeCString(ctx, text);
    return ret < 0 ? -1 : 0;
}

/* Optional port argument, 0 when absent. */
static int mc_port_arg(JSContext *ctx, int argc, JSValueConst *argv, int index,
    const char *name, int *port) {
    int32_t value = 0;

    *port = 0;
    if (!mc_present(argc, argv, index))
        return 0;
    if (!JS_IsNumber(argv[index]) || JS_ToInt32(ctx, &value, argv[index])) {
        mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s port must be 0 or 1", name);
        return -1;
    }
    if (value < 0 || value >= ATHENA_MEMCARD_PORTS) {
        JS_ThrowRangeError(ctx, "%s port must be 0 or 1", name);
        return -1;
    }
    *port = value;
    return 0;
}

/* Optional options object: 1 when present, 0 when absent, -1 on error. */
static int mc_options_arg(JSContext *ctx, int argc, JSValueConst *argv, int index, const char *name) {
    if (!mc_present(argc, argv, index))
        return 0;
    if (!JS_IsObject(argv[index]) || JS_IsArray(ctx, argv[index]) || JS_IsFunction(ctx, argv[index])) {
        mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s options must be an object", name);
        return -1;
    }
    return 1;
}

/* Required options object; -1 with an exception when it is not one. */
static int mc_required_options_arg(JSContext *ctx, int argc, JSValueConst *argv, int index,
    const char *name) {
    if (!mc_present(argc, argv, index)) {
        mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s options must be an object", name);
        return -1;
    }
    return mc_options_arg(ctx, argc, argv, index, name) < 0 ? -1 : 0;
}

static int mc_bool_option(JSContext *ctx, JSValueConst options, const char *key,
    const char *name, bool *out) {
    JSValue value = JS_GetPropertyStr(ctx, options, key);

    if (JS_IsException(value))
        return -1;
    if (!JS_IsUndefined(value)) {
        if (!JS_IsBool(value)) {
            JS_FreeValue(ctx, value);
            mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s options.%s must be a boolean", name, key);
            return -1;
        }
        *out = JS_ToBool(ctx, value);
    }
    JS_FreeValue(ctx, value);
    return 0;
}

/*
 * Copies a string (as UTF-8) or binary data into a malloc'd buffer, so it
 * survives the GIL being released. Free it with free().
 */
static int mc_data_arg(JSContext *ctx, JSValueConst value, const char *name,
    void **out, size_t *out_size) {
    const uint8_t *data = NULL;
    size_t size = 0;
    const char *text = NULL;
    void *copy;

    if (JS_IsString(value)) {
        text = JS_ToCStringLen(ctx, &size, value);
        if (!text)
            return -1;
        data = (const uint8_t *)text;
    } else if (JS_IsObject(value)) {
        size_t buffer_size, offset, length, element;
        uint8_t *buffer = JS_GetArrayBuffer(ctx, &buffer_size, value);
        if (buffer) {
            data = buffer;
            size = buffer_size;
        } else {
            JSValue array_buffer;
            JS_FreeValue(ctx, JS_GetException(ctx));
            array_buffer = JS_GetTypedArrayBuffer(ctx, value, &offset, &length, &element);
            if (JS_IsException(array_buffer)) {
                JS_FreeValue(ctx, JS_GetException(ctx));
            } else {
                buffer = JS_GetArrayBuffer(ctx, &buffer_size, array_buffer);
                JS_FreeValue(ctx, array_buffer);
                if (!buffer && length)
                    return -1;
                data = buffer ? buffer + offset : NULL;
                size = length;
                if (!data)
                    data = (const uint8_t *)"";
            }
        }
    }
    if (!data) {
        mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT,
            "%s data must be a string, an ArrayBuffer or a typed array", name);
        return -1;
    }
    if (size > INT32_MAX) {
        if (text)
            JS_FreeCString(ctx, text);
        JS_ThrowRangeError(ctx, "%s data is too large", name);
        return -1;
    }
    copy = malloc(size ? size : 1);
    if (copy && size)
        memcpy(copy, data, size);
    if (text)
        JS_FreeCString(ctx, text);
    if (!copy) {
        JS_ThrowOutOfMemory(ctx);
        return -1;
    }
    *out = copy;
    *out_size = size;
    return 0;
}

/* Drivers stopped at boot are started here, with the GIL held. */
static int mc_ready(JSContext *ctx, const char *name) {
    int ret = athena_memcard_prepare();
    if (ret < 0) {
        mc_throw(ctx, ret, "%s: %s (mcserv is not running)", name, athena_memcard_strerror(ret));
        return -1;
    }
    return 0;
}

/* Throws with `error.path` set to the card path, e.g. "mc0:/SAVE/data.bin". */
static JSValue mc_throw_path(JSContext *ctx, const char *name, int code, const McJsPath *path) {
    char target[ATHENA_MEMCARD_PATH_MAX + 8];
    JSValue error;

    snprintf(target, sizeof(target), "mc%d:%s", path->port, path->path);
    error = mc_error(ctx, code, "%s: %s: %s", name, athena_memcard_strerror(code), target);
    JS_SetPropertyStr(ctx, error, "path", JS_NewString(ctx, target));
    return JS_Throw(ctx, error);
}

static void mc_free_buffer(JSRuntime *rt, void *opaque, void *ptr) {
    free(ptr);
}

static JSValue mc_new_buffer(JSContext *ctx, void *data, size_t size) {
    JSValue buffer = JS_NewArrayBuffer(ctx, data, size, mc_free_buffer, NULL, 0);
    if (JS_IsException(buffer))
        free(data);
    return buffer;
}

/* ------------------------------------------------------------------------ */
/* Values                                                                   */
/* ------------------------------------------------------------------------ */

static double mc_time_ms(const AthenaMemcardTime *time) {
    return (double)athena_memcard_time_to_unix(time) * 1000.0;
}

static JSValue mc_entry_object(JSContext *ctx, const AthenaMemcardEntry *entry, int port,
    const char *dir) {
    char path[ATHENA_MEMCARD_PATH_MAX + 40];
    JSValue item = JS_NewObject(ctx);

    if (JS_IsException(item))
        return item;
    if (dir)
        snprintf(path, sizeof(path), "mc%d:%s%s%s", port, dir, dir[1] ? "/" : "", entry->name);
    JS_DefinePropertyValueStr(ctx, item, "name", JS_NewString(ctx, entry->name), JS_PROP_C_W_E);
    if (dir)
        JS_DefinePropertyValueStr(ctx, item, "path", JS_NewString(ctx, path), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, item, "size", JS_NewUint32(ctx, entry->size), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, item, "directory",
        JS_NewBool(ctx, athena_memcard_entry_is_dir(entry)), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, item, "attributes", JS_NewInt32(ctx, entry->attributes), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, item, "created",
        JS_NewFloat64(ctx, mc_time_ms(&entry->created)), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, item, "modified",
        JS_NewFloat64(ctx, mc_time_ms(&entry->modified)), JS_PROP_C_W_E);
    return item;
}

static const char *mc_type_name(AthenaMemcardType type) {
    switch (type) {
    case ATHENA_MEMCARD_TYPE_PS1: return "ps1";
    case ATHENA_MEMCARD_TYPE_PS2: return "ps2";
    case ATHENA_MEMCARD_TYPE_POCKETSTATION: return "pocketstation";
    default: return "none";
    }
}

/* ------------------------------------------------------------------------ */
/* Card                                                                     */
/* ------------------------------------------------------------------------ */

static JSValue mc_js_get_info(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = "MemoryCard.getInfo";
    AthenaMemcardInfo info;
    JSValue object;
    int port, ret;

    if (!mc_argc(ctx, argc, 0, 1, name) || mc_port_arg(ctx, argc, argv, 0, name, &port) < 0 ||
        mc_ready(ctx, name) < 0)
        return JS_EXCEPTION;
    athena_js_gil_unlock();
    ret = athena_memcard_get_info(port, &info);
    athena_js_gil_lock();
    if (ret < 0)
        return mc_throw(ctx, ret, "%s: %s", name, athena_memcard_strerror(ret));

    object = JS_NewObject(ctx);
    if (JS_IsException(object))
        return object;
    JS_DefinePropertyValueStr(ctx, object, "port", JS_NewInt32(ctx, port), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, object, "type", JS_NewString(ctx, mc_type_name(info.type)), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, object, "connected",
        JS_NewBool(ctx, info.type != ATHENA_MEMCARD_TYPE_NONE), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, object, "formatted", JS_NewBool(ctx, info.formatted), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, object, "freeClusters", JS_NewUint32(ctx, info.free_clusters), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, object, "freeBytes",
        JS_NewFloat64(ctx, (double)info.free_clusters * ATHENA_MEMCARD_CLUSTER_SIZE), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, object, "changed", JS_NewBool(ctx, info.changed), JS_PROP_C_W_E);
    return object;
}

/* MemoryCard.format(port) / unformat(port) */
static JSValue mc_js_format(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    const char *name = magic ? "MemoryCard.unformat" : "MemoryCard.format";
    int32_t port;
    int ret;

    if (!mc_argc(ctx, argc, 1, 1, name))
        return JS_EXCEPTION;
    if (!JS_IsNumber(argv[0]) || JS_ToInt32(ctx, &port, argv[0]) || port < 0 || port >= ATHENA_MEMCARD_PORTS)
        return mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s port must be 0 or 1", name);
    if (mc_ready(ctx, name) < 0)
        return JS_EXCEPTION;
    athena_js_gil_unlock();
    ret = magic ? athena_memcard_unformat(port) : athena_memcard_format(port);
    athena_js_gil_lock();
    if (ret < 0)
        return mc_throw(ctx, ret, "%s: %s: mc%d:", name, athena_memcard_strerror(ret), port);
    return JS_UNDEFINED;
}

/* ------------------------------------------------------------------------ */
/* Entries                                                                  */
/* ------------------------------------------------------------------------ */

/* Parses argv[0] as a path and makes sure the drivers run. */
static int mc_begin(JSContext *ctx, int argc, JSValueConst *argv, int minimum, int maximum,
    const char *name, McJsPath *path) {
    if (!mc_argc(ctx, argc, minimum, maximum, name) || mc_path_arg(ctx, argv[0], name, path) < 0 ||
        mc_ready(ctx, name) < 0)
        return -1;
    return 0;
}

static JSValue mc_js_stat(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = "MemoryCard.stat";
    AthenaMemcardEntry entry;
    McJsPath path;
    char dir[ATHENA_MEMCARD_PATH_MAX + 1];
    int ret;

    if (mc_begin(ctx, argc, argv, 1, 1, name, &path) < 0)
        return JS_EXCEPTION;
    athena_js_gil_unlock();
    ret = athena_memcard_stat(path.port, path.path, &entry);
    athena_js_gil_lock();
    if (ret < 0)
        return mc_throw_path(ctx, name, ret, &path);

    /* The entry's own directory, for its `path`. */
    strcpy(dir, path.path);
    *strrchr(dir, '/') = '\0';
    if (!dir[0])
        strcpy(dir, "/");
    if (!path.path[1])
        return mc_entry_object(ctx, &entry, path.port, NULL);
    return mc_entry_object(ctx, &entry, path.port, dir);
}

static JSValue mc_js_exists(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = "MemoryCard.exists";
    AthenaMemcardEntry entry;
    McJsPath path;
    int ret;

    if (mc_begin(ctx, argc, argv, 1, 1, name, &path) < 0)
        return JS_EXCEPTION;
    athena_js_gil_unlock();
    ret = athena_memcard_stat(path.port, path.path, &entry);
    athena_js_gil_lock();
    if (ret == ATHENA_MEMCARD_ERR_NOT_FOUND)
        return JS_FALSE;
    if (ret < 0)
        return mc_throw_path(ctx, name, ret, &path);
    return JS_TRUE;
}

static JSValue mc_js_list(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = "MemoryCard.list";
    AthenaMemcardEntry *entries = NULL;
    McJsPath path;
    JSValue array;
    int count = 0, ret;

    if (mc_begin(ctx, argc, argv, 1, 1, name, &path) < 0)
        return JS_EXCEPTION;
    athena_js_gil_unlock();
    ret = athena_memcard_list(path.port, path.path, &entries, &count);
    athena_js_gil_lock();
    if (ret < 0)
        return mc_throw_path(ctx, name, ret, &path);

    array = JS_NewArray(ctx);
    for (int i = 0; i < count && !JS_IsException(array); i++) {
        JSValue item = mc_entry_object(ctx, &entries[i], path.port, path.path);
        if (JS_IsException(item)) {
            JS_FreeValue(ctx, array);
            array = JS_EXCEPTION;
            break;
        }
        JS_SetPropertyUint32(ctx, array, (uint32_t)i, item);
    }
    free(entries);
    return array;
}

/* MemoryCard.mkdir(path, { recursive }) -> true when created */
static JSValue mc_js_mkdir(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = "MemoryCard.mkdir";
    bool recursive = false;
    McJsPath path;
    int ret;

    if (!mc_argc(ctx, argc, 1, 2, name))
        return JS_EXCEPTION;
    ret = mc_options_arg(ctx, argc, argv, 1, name);
    if (ret < 0 || (ret > 0 && mc_bool_option(ctx, argv[1], "recursive", name, &recursive) < 0) ||
        mc_begin(ctx, argc, argv, 1, 2, name, &path) < 0)
        return JS_EXCEPTION;
    athena_js_gil_unlock();
    ret = athena_memcard_mkdir(path.port, path.path, recursive);
    athena_js_gil_lock();
    if (ret < 0)
        return mc_throw_path(ctx, name, ret, &path);
    return JS_NewBool(ctx, ret > 0);
}

static JSValue mc_js_remove(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = "MemoryCard.remove";
    bool recursive = false;
    McJsPath path;
    int ret;

    if (!mc_argc(ctx, argc, 1, 2, name))
        return JS_EXCEPTION;
    ret = mc_options_arg(ctx, argc, argv, 1, name);
    if (ret < 0 || (ret > 0 && mc_bool_option(ctx, argv[1], "recursive", name, &recursive) < 0) ||
        mc_begin(ctx, argc, argv, 1, 2, name, &path) < 0)
        return JS_EXCEPTION;
    if (!path.path[1])
        return mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s: the root cannot be removed (use format)", name);
    athena_js_gil_unlock();
    ret = athena_memcard_remove(path.port, path.path, recursive, NULL, NULL);
    athena_js_gil_lock();
    if (ret < 0)
        return mc_throw_path(ctx, name, ret, &path);
    return JS_UNDEFINED;
}

/* MemoryCard.rename(path, newName): same directory only. */
static JSValue mc_js_rename(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = "MemoryCard.rename";
    char new_name[ATHENA_MEMCARD_NAME_MAX + 1];
    const char *text;
    McJsPath path;
    size_t length;
    int ret;

    if (mc_begin(ctx, argc, argv, 2, 2, name, &path) < 0)
        return JS_EXCEPTION;
    if (!JS_IsString(argv[1]))
        return mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s newName must be a string", name);
    text = JS_ToCStringLen(ctx, &length, argv[1]);
    if (!text)
        return JS_EXCEPTION;
    if (length == 0 || length > ATHENA_MEMCARD_NAME_MAX || strchr(text, '/') || strchr(text, ':')) {
        JS_FreeCString(ctx, text);
        return mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT,
            "%s newName must be a name of 1-%d bytes, not a path", name, ATHENA_MEMCARD_NAME_MAX);
    }
    memcpy(new_name, text, length + 1);
    JS_FreeCString(ctx, text);

    athena_js_gil_unlock();
    ret = athena_memcard_rename(path.port, path.path, new_name);
    athena_js_gil_lock();
    if (ret == ATHENA_MEMCARD_ERR_ARGUMENT)
        return mc_throw(ctx, ret, "%s: invalid name \"%s\"", name, new_name);
    if (ret < 0)
        return mc_throw_path(ctx, name, ret, &path);
    return JS_UNDEFINED;
}

static JSValue mc_js_free_entries(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = "MemoryCard.getFreeEntries";
    McJsPath path;
    int ret;

    if (mc_begin(ctx, argc, argv, 1, 1, name, &path) < 0)
        return JS_EXCEPTION;
    athena_js_gil_unlock();
    ret = athena_memcard_free_entries(path.port, path.path);
    athena_js_gil_lock();
    if (ret < 0)
        return mc_throw_path(ctx, name, ret, &path);
    return JS_NewInt32(ctx, ret);
}

/* A Date or milliseconds since 1970 into card time. */
static int mc_time_option(JSContext *ctx, JSValueConst options, const char *key, const char *name,
    AthenaMemcardTime *out, bool *present) {
    JSValue value = JS_GetPropertyStr(ctx, options, key);
    double ms;

    *present = false;
    if (JS_IsException(value))
        return -1;
    if (JS_IsUndefined(value))
        return 0;
    if (!JS_IsNumber(value) && !JS_IsObject(value)) {
        JS_FreeValue(ctx, value);
        mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s options.%s must be a Date or milliseconds", name, key);
        return -1;
    }
    if (JS_ToFloat64(ctx, &ms, value)) {
        JS_FreeValue(ctx, value);
        return -1;
    }
    JS_FreeValue(ctx, value);
    if (!isfinite(ms) || ms < 0) {
        JS_ThrowRangeError(ctx, "%s options.%s must be a valid date after 1970", name, key);
        return -1;
    }
    athena_memcard_time_from_unix((int64_t)(ms / 1000.0), out);
    *present = true;
    return 0;
}

/* MemoryCard.setInfo(path, { attributes, created, modified }) */
static JSValue mc_js_set_info(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = "MemoryCard.setInfo";
    AthenaMemcardSetInfo info = { 0 };
    McJsPath path;
    JSValue value;
    bool present;
    int ret;

    if (!mc_argc(ctx, argc, 2, 2, name))
        return JS_EXCEPTION;
    if (mc_required_options_arg(ctx, argc, argv, 1, name) < 0)
        return JS_EXCEPTION;
    value = JS_GetPropertyStr(ctx, argv[1], "attributes");
    if (JS_IsException(value))
        return JS_EXCEPTION;
    if (!JS_IsUndefined(value)) {
        int32_t attributes;
        if (!JS_IsNumber(value) || JS_ToInt32(ctx, &attributes, value) ||
            attributes < 0 || (attributes & ~ATHENA_MEMCARD_ATTR_SETTABLE)) {
            JS_FreeValue(ctx, value);
            return mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT,
                "%s options.attributes may only combine ATTR_READABLE, ATTR_WRITABLE, "
                "ATTR_EXECUTABLE, ATTR_PROTECTED and ATTR_HIDDEN", name);
        }
        info.attributes = (uint16_t)attributes;
        info.fields |= ATHENA_MEMCARD_SET_ATTRIBUTES;
    }
    JS_FreeValue(ctx, value);
    if (mc_time_option(ctx, argv[1], "created", name, &info.created, &present) < 0)
        return JS_EXCEPTION;
    if (present)
        info.fields |= ATHENA_MEMCARD_SET_CREATED;
    if (mc_time_option(ctx, argv[1], "modified", name, &info.modified, &present) < 0)
        return JS_EXCEPTION;
    if (present)
        info.fields |= ATHENA_MEMCARD_SET_MODIFIED;
    if (mc_begin(ctx, argc, argv, 2, 2, name, &path) < 0)
        return JS_EXCEPTION;

    athena_js_gil_unlock();
    ret = athena_memcard_set_info(path.port, path.path, &info);
    athena_js_gil_lock();
    if (ret < 0)
        return mc_throw_path(ctx, name, ret, &path);
    return JS_UNDEFINED;
}

/* ------------------------------------------------------------------------ */
/* Whole files                                                              */
/* ------------------------------------------------------------------------ */

enum { MC_READ_BUFFER, MC_READ_TEXT, MC_READ_JSON };

/* MemoryCard.readFile(path) / readText(path) / readJSON(path) */
static JSValue mc_js_read_file(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int as) {
    static const char *const names[] = { "MemoryCard.readFile", "MemoryCard.readText", "MemoryCard.readJSON" };
    const char *name = names[as];
    char target[ATHENA_MEMCARD_PATH_MAX + 8];
    McJsPath path;
    void *data = NULL;
    size_t size = 0;
    JSValue result;
    int ret;

    if (mc_begin(ctx, argc, argv, 1, 1, name, &path) < 0)
        return JS_EXCEPTION;
    athena_js_gil_unlock();
    ret = athena_memcard_read_file(path.port, path.path, &data, &size, NULL, NULL);
    athena_js_gil_lock();
    if (ret < 0)
        return mc_throw_path(ctx, name, ret, &path);
    if (as == MC_READ_BUFFER)
        return mc_new_buffer(ctx, data, size);
    if (as == MC_READ_TEXT) {
        result = JS_NewStringLen(ctx, data, size);
        free(data);
        return result;
    }

    /* The data ends with a '\0', as the parser needs. */
    snprintf(target, sizeof(target), "mc%d:%s", path.port, path.path);
    result = JS_ParseJSON(ctx, data, size, target);
    free(data);
    if (JS_IsException(result)) {
        JSValue error = JS_GetException(ctx);
        JS_SetPropertyStr(ctx, error, "path", JS_NewString(ctx, target));
        return JS_Throw(ctx, error);
    }
    return result;
}

typedef struct {
    McJsPath path;
    void *data;
    size_t size;
    AthenaMemcardWriteOptions options;
} McWriteArgs;

/* (path, data, { createDirs = true, atomic = false }) */
static int mc_write_args(JSContext *ctx, int argc, JSValueConst *argv, const char *name, McWriteArgs *out) {
    int ret;

    memset(out, 0, sizeof(*out));
    out->options.create_dirs = true;
    if (!mc_argc(ctx, argc, 2, 3, name))
        return -1;
    ret = mc_options_arg(ctx, argc, argv, 2, name);
    if (ret < 0 || (ret > 0 &&
        (mc_bool_option(ctx, argv[2], "createDirs", name, &out->options.create_dirs) < 0 ||
         mc_bool_option(ctx, argv[2], "atomic", name, &out->options.atomic) < 0)))
        return -1;
    if (mc_begin(ctx, argc, argv, 2, 3, name, &out->path) < 0)
        return -1;
    return mc_data_arg(ctx, argv[1], name, &out->data, &out->size);
}

static JSValue mc_write_run(JSContext *ctx, const char *name, McWriteArgs *args);

static JSValue mc_js_write_file(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = "MemoryCard.writeFile";
    McWriteArgs args;

    if (mc_write_args(ctx, argc, argv, name, &args) < 0)
        return JS_EXCEPTION;
    return mc_write_run(ctx, name, &args);
}

/* MemoryCard.writeJSON(path, value, { indent, createDirs, atomic }) */
static JSValue mc_write_start(JSContext *ctx, const char *name, McWriteArgs *args);

/* MemoryCard.writeJSON / writeJSONAsync: stringified now, written now or on a worker. */
static JSValue mc_js_write_json(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
    int async) {
    const char *name = async ? "MemoryCard.writeJSONAsync" : "MemoryCard.writeJSON";
    JSValue space = JS_UNDEFINED, text;
    JSValueConst args_argv[3];
    McWriteArgs args;
    int ret;

    if (!mc_argc(ctx, argc, 2, 3, name))
        return JS_EXCEPTION;
    ret = mc_options_arg(ctx, argc, argv, 2, name);
    if (ret < 0)
        return JS_EXCEPTION;
    if (ret > 0) {
        int32_t indent;
        JSValue value = JS_GetPropertyStr(ctx, argv[2], "indent");
        if (JS_IsException(value))
            return JS_EXCEPTION;
        if (!JS_IsUndefined(value)) {
            ret = !JS_IsNumber(value) || JS_ToInt32(ctx, &indent, value) || indent < 0 || indent > 10;
            JS_FreeValue(ctx, value);
            if (ret)
                return mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s options.indent must be 0-10", name);
            space = JS_NewInt32(ctx, indent);
        }
    }
    text = JS_JSONStringify(ctx, argv[1], JS_UNDEFINED, space);
    if (JS_IsException(text))
        return text;
    if (!JS_IsString(text)) {
        JS_FreeValue(ctx, text);
        return mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s value cannot be written as JSON", name);
    }
    args_argv[0] = argv[0];
    args_argv[1] = text;
    args_argv[2] = argc > 2 ? argv[2] : JS_UNDEFINED;
    ret = mc_write_args(ctx, argc, args_argv, name, &args);
    JS_FreeValue(ctx, text);
    if (ret < 0)
        return JS_EXCEPTION;
    return async ? mc_write_start(ctx, name, &args) : mc_write_run(ctx, name, &args);
}

/* Writes the parsed arguments with the GIL released, then frees their copy. */
static JSValue mc_write_run(JSContext *ctx, const char *name, McWriteArgs *args) {
    int ret;

    athena_js_gil_unlock();
    ret = athena_memcard_write_file(args->path.port, args->path.path, args->data, args->size, &args->options);
    athena_js_gil_lock();
    free(args->data);
    if (ret < 0)
        return mc_throw_path(ctx, name, ret, &args->path);
    return JS_NewInt32(ctx, ret);
}

/* ------------------------------------------------------------------------ */
/* Open files                                                               */
/* ------------------------------------------------------------------------ */

/*
 * An open file. `busy` is set while the GIL is released for an operation,
 * so another script thread cannot close or reuse it meanwhile.
 */
typedef struct {
    AthenaMemcardFile *file;
    bool busy;
} McFileHandle;

static JSClassID mc_file_class_id;

static void mc_file_finalizer(JSRuntime *rt, JSValue value) {
    McFileHandle *handle = JS_GetOpaque(value, mc_file_class_id);
    if (!handle)
        return;
    /* Gives the driver handle back: there are only three for every card. */
    if (handle->file)
        athena_memcard_close(handle->file);
    free(handle);
}

static JSClassDef mc_file_class = {
    "MemoryCardFile",
    .finalizer = mc_file_finalizer,
};

static McFileHandle *mc_file_handle(JSContext *ctx, JSValueConst value, const char *name) {
    McFileHandle *handle = JS_GetOpaque2(ctx, value, mc_file_class_id);

    if (!handle)
        return NULL;
    if (!handle->file) {
        mc_throw(ctx, ATHENA_MEMCARD_ERR_CLOSED, "%s: the file is closed", name);
        return NULL;
    }
    if (handle->busy) {
        mc_throw(ctx, ATHENA_MEMCARD_ERR_BUSY, "%s: the file is in use by another operation", name);
        return NULL;
    }
    return handle;
}

static JSValue mc_file_throw(JSContext *ctx, const char *name, int code) {
    return mc_throw(ctx, code, "%s: %s", name, athena_memcard_strerror(code));
}

/* MemoryCard.open(path, mode = "r") */
static JSValue mc_js_open(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const struct { const char *mode; int flags; } modes[] = {
        { "r", ATHENA_MEMCARD_OPEN_READ },
        { "r+", ATHENA_MEMCARD_OPEN_READ | ATHENA_MEMCARD_OPEN_WRITE },
        { "w", ATHENA_MEMCARD_OPEN_WRITE | ATHENA_MEMCARD_OPEN_CREATE },
        { "w+", ATHENA_MEMCARD_OPEN_READ | ATHENA_MEMCARD_OPEN_WRITE | ATHENA_MEMCARD_OPEN_CREATE },
        { "a", ATHENA_MEMCARD_OPEN_WRITE | ATHENA_MEMCARD_OPEN_APPEND },
        { "a+", ATHENA_MEMCARD_OPEN_READ | ATHENA_MEMCARD_OPEN_WRITE | ATHENA_MEMCARD_OPEN_APPEND },
    };
    const char *name = "MemoryCard.open";
    AthenaMemcardFile *file = NULL;
    McFileHandle *handle;
    McJsPath path;
    JSValue object;
    int flags = ATHENA_MEMCARD_OPEN_READ, ret;

    if (!mc_argc(ctx, argc, 1, 2, name))
        return JS_EXCEPTION;
    if (mc_present(argc, argv, 1)) {
        const char *mode = JS_IsString(argv[1]) ? JS_ToCString(ctx, argv[1]) : NULL;
        flags = 0;
        for (size_t i = 0; mode && i < countof(modes); i++)
            if (!strcmp(mode, modes[i].mode))
                flags = modes[i].flags;
        if (mode)
            JS_FreeCString(ctx, mode);
        if (!flags)
            return mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT,
                "%s mode must be \"r\", \"r+\", \"w\", \"w+\", \"a\" or \"a+\"", name);
    }
    if (mc_begin(ctx, argc, argv, 1, 2, name, &path) < 0)
        return JS_EXCEPTION;

    handle = calloc(1, sizeof(*handle));
    if (!handle)
        return JS_ThrowOutOfMemory(ctx);
    object = JS_NewObjectClass(ctx, mc_file_class_id);
    if (JS_IsException(object)) {
        free(handle);
        return object;
    }
    JS_SetOpaque(object, handle);

    athena_js_gil_unlock();
    ret = athena_memcard_open(path.port, path.path, flags, &file);
    athena_js_gil_lock();
    if (ret < 0) {
        JS_FreeValue(ctx, object);
        return mc_throw_path(ctx, name, ret, &path);
    }
    handle->file = file;
    return object;
}

/* file.read(size = rest of the file) -> ArrayBuffer, shorter at the end of the file */
static JSValue mc_file_read(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = "MemoryCardFile.read";
    McFileHandle *handle;
    double size;
    uint8_t *buffer, *shrunk;
    int ret;

    if (!mc_argc(ctx, argc, 0, 1, name) || !(handle = mc_file_handle(ctx, this_val, name)))
        return JS_EXCEPTION;
    if (!mc_present(argc, argv, 0)) {
        int total = athena_memcard_size(handle->file), position = athena_memcard_tell(handle->file);
        if (total < 0 || position < 0)
            return mc_file_throw(ctx, name, total < 0 ? total : position);
        size = total > position ? total - position : 0;
    } else if (!JS_IsNumber(argv[0]) || JS_ToFloat64(ctx, &size, argv[0]) || size != floor(size) ||
        size < 0 || size > INT32_MAX) {
        return mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s size must be a non-negative integer", name);
    }
    buffer = malloc(size ? (size_t)size : 1);
    if (!buffer)
        return JS_ThrowOutOfMemory(ctx);

    handle->busy = true;
    athena_js_gil_unlock();
    ret = athena_memcard_read(handle->file, buffer, (size_t)size);
    athena_js_gil_lock();
    handle->busy = false;
    if (ret < 0) {
        free(buffer);
        return mc_file_throw(ctx, name, ret);
    }
    if ((size_t)ret < (size_t)size && ret > 0 && (shrunk = realloc(buffer, (size_t)ret)))
        buffer = shrunk;
    return mc_new_buffer(ctx, buffer, (size_t)ret);
}

/* file.write(data) -> bytes written */
static JSValue mc_file_write(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = "MemoryCardFile.write";
    McFileHandle *handle;
    void *data;
    size_t size;
    int ret;

    if (!mc_argc(ctx, argc, 1, 1, name) || !(handle = mc_file_handle(ctx, this_val, name)) ||
        mc_data_arg(ctx, argv[0], name, &data, &size) < 0)
        return JS_EXCEPTION;
    handle->busy = true;
    athena_js_gil_unlock();
    ret = athena_memcard_write(handle->file, data, size);
    athena_js_gil_lock();
    handle->busy = false;
    free(data);
    if (ret < 0)
        return mc_file_throw(ctx, name, ret);
    return JS_NewInt32(ctx, ret);
}

/* file.seek(offset, whence = "set") -> new position */
static JSValue mc_file_seek(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = "MemoryCardFile.seek";
    McFileHandle *handle;
    int whence = ATHENA_MEMCARD_SEEK_SET, ret;
    int32_t offset;

    if (!mc_argc(ctx, argc, 1, 2, name) || !(handle = mc_file_handle(ctx, this_val, name)))
        return JS_EXCEPTION;
    if (!JS_IsNumber(argv[0]) || JS_ToInt32(ctx, &offset, argv[0]))
        return mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s offset must be a number", name);
    if (mc_present(argc, argv, 1)) {
        const char *text = JS_IsString(argv[1]) ? JS_ToCString(ctx, argv[1]) : NULL;
        whence = -1;
        if (text) {
            if (!strcmp(text, "set")) whence = ATHENA_MEMCARD_SEEK_SET;
            else if (!strcmp(text, "cur")) whence = ATHENA_MEMCARD_SEEK_CUR;
            else if (!strcmp(text, "end")) whence = ATHENA_MEMCARD_SEEK_END;
            JS_FreeCString(ctx, text);
        }
        if (whence < 0)
            return mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s whence must be \"set\", \"cur\" or \"end\"", name);
    }
    handle->busy = true;
    athena_js_gil_unlock();
    ret = athena_memcard_seek(handle->file, offset, whence);
    athena_js_gil_lock();
    handle->busy = false;
    if (ret < 0)
        return mc_file_throw(ctx, name, ret);
    return JS_NewInt32(ctx, ret);
}

static JSValue mc_file_tell(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = "MemoryCardFile.tell";
    McFileHandle *handle;
    int ret;

    if (!mc_argc(ctx, argc, 0, 0, name) || !(handle = mc_file_handle(ctx, this_val, name)))
        return JS_EXCEPTION;
    ret = athena_memcard_tell(handle->file);
    if (ret < 0)
        return mc_file_throw(ctx, name, ret);
    return JS_NewInt32(ctx, ret);
}

static JSValue mc_file_flush(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = "MemoryCardFile.flush";
    McFileHandle *handle;
    int ret;

    if (!mc_argc(ctx, argc, 0, 0, name) || !(handle = mc_file_handle(ctx, this_val, name)))
        return JS_EXCEPTION;
    handle->busy = true;
    athena_js_gil_unlock();
    ret = athena_memcard_flush(handle->file);
    athena_js_gil_lock();
    handle->busy = false;
    if (ret < 0)
        return mc_file_throw(ctx, name, ret);
    return JS_UNDEFINED;
}

/* Closing twice is allowed; the second call does nothing. */
static JSValue mc_file_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = "MemoryCardFile.close";
    McFileHandle *handle;
    AthenaMemcardFile *file;
    int ret;

    if (!mc_argc(ctx, argc, 0, 0, name))
        return JS_EXCEPTION;
    handle = JS_GetOpaque2(ctx, this_val, mc_file_class_id);
    if (!handle)
        return JS_EXCEPTION;
    if (!handle->file)
        return JS_UNDEFINED;
    if (handle->busy)
        return mc_throw(ctx, ATHENA_MEMCARD_ERR_BUSY, "%s: the file is in use by another operation", name);
    file = handle->file;
    handle->file = NULL;
    athena_js_gil_unlock();
    ret = athena_memcard_close(file);
    athena_js_gil_lock();
    /* A file lost with its card or to an IOP reset is released all the same. */
    if (ret < 0 && ret != ATHENA_MEMCARD_ERR_CLOSED && ret != ATHENA_MEMCARD_ERR_CHANGED)
        return mc_file_throw(ctx, name, ret);
    return JS_UNDEFINED;
}

static JSValue mc_file_closed(JSContext *ctx, JSValueConst this_val) {
    McFileHandle *handle = JS_GetOpaque2(ctx, this_val, mc_file_class_id);
    return handle ? JS_NewBool(ctx, !handle->file) : JS_EXCEPTION;
}

/* file.size: kept by the handle, no command. */
static JSValue mc_file_size(JSContext *ctx, JSValueConst this_val) {
    McFileHandle *handle = mc_file_handle(ctx, this_val, "MemoryCardFile.size");
    int ret;

    if (!handle)
        return JS_EXCEPTION;
    ret = athena_memcard_size(handle->file);
    return ret < 0 ? mc_file_throw(ctx, "MemoryCardFile.size", ret) : JS_NewInt32(ctx, ret);
}

static const JSCFunctionListEntry mc_file_proto[] = {
    JS_CFUNC_DEF("read", 1, mc_file_read),
    JS_CFUNC_DEF("write", 1, mc_file_write),
    JS_CFUNC_DEF("seek", 2, mc_file_seek),
    JS_CFUNC_DEF("tell", 0, mc_file_tell),
    JS_CFUNC_DEF("flush", 0, mc_file_flush),
    JS_CFUNC_DEF("close", 0, mc_file_close),
    JS_CGETSET_DEF("closed", mc_file_closed, NULL),
    JS_CGETSET_DEF("size", mc_file_size, NULL),
};

/* ------------------------------------------------------------------------ */
/* icon.sys                                                                 */
/* ------------------------------------------------------------------------ */

/* Reads options[key] as a string into `out` (freed by the caller with JS_FreeCString). */
static int mc_string_option(JSContext *ctx, JSValueConst options, const char *key, const char *name,
    bool required, const char **out) {
    JSValue value = JS_GetPropertyStr(ctx, options, key);

    *out = NULL;
    if (JS_IsException(value))
        return -1;
    if (JS_IsUndefined(value)) {
        if (required)
            mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s options.%s is required", name, key);
        return required ? -1 : 0;
    }
    if (!JS_IsString(value)) {
        JS_FreeValue(ctx, value);
        mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s options.%s must be a string", name, key);
        return -1;
    }
    *out = JS_ToCString(ctx, value);
    JS_FreeValue(ctx, value);
    return *out ? 0 : -1;
}

/* Reads options[key] as an array of `count` numbers in [minimum, maximum]. */
static int mc_numbers_option(JSContext *ctx, JSValueConst value, const char *key, const char *name,
    int count, double minimum, double maximum, double *out) {
    JSValue length_value;
    uint32_t length;

    if (!JS_IsArray(ctx, value))
        goto invalid;
    length_value = JS_GetPropertyStr(ctx, value, "length");
    if (JS_ToUint32(ctx, &length, length_value)) {
        JS_FreeValue(ctx, length_value);
        return -1;
    }
    JS_FreeValue(ctx, length_value);
    if (length != (uint32_t)count)
        goto invalid;
    for (int i = 0; i < count; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, value, (uint32_t)i);
        int bad = !JS_IsNumber(item) || JS_ToFloat64(ctx, &out[i], item) ||
            !(out[i] >= minimum && out[i] <= maximum);
        JS_FreeValue(ctx, item);
        if (bad)
            goto invalid;
    }
    return 0;

invalid:
    mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s options.%s must be %d numbers between %g and %g",
        name, key, count, minimum, maximum);
    return -1;
}

/* options[key]: `rows` arrays of 3 numbers each. Absent leaves `out` as is. */
static int mc_matrix_option(JSContext *ctx, JSValueConst options, const char *key, const char *name,
    int rows, double minimum, double maximum, double out[][3]) {
    JSValue value = JS_GetPropertyStr(ctx, options, key);
    bool shape_ok;
    int ret = 0;

    if (JS_IsException(value))
        return -1;
    if (JS_IsUndefined(value))
        return 0;
    shape_ok = JS_IsArray(ctx, value);
    if (shape_ok) {
        JSValue extra = JS_GetPropertyUint32(ctx, value, (uint32_t)rows);
        shape_ok = JS_IsUndefined(extra);
        JS_FreeValue(ctx, extra);
    }
    /* mc_numbers_option throws for a bad row, a missing one included. */
    for (int i = 0; shape_ok && i < rows && ret == 0; i++) {
        JSValue row = JS_GetPropertyUint32(ctx, value, (uint32_t)i);
        ret = mc_numbers_option(ctx, row, key, name, 3, minimum, maximum, out[i]);
        JS_FreeValue(ctx, row);
    }
    JS_FreeValue(ctx, value);
    if (!shape_ok) {
        mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s options.%s must hold %d arrays of 3 numbers",
            name, key, rows);
        return -1;
    }
    return ret;
}

/* MemoryCard.createIconSys({ title, icon, copyIcon, deleteIcon, ... }) -> ArrayBuffer */
static JSValue mc_js_create_icon_sys(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = "MemoryCard.createIconSys";
    const char *title = NULL, *icon = NULL, *copy_icon = NULL, *delete_icon = NULL;
    AthenaMemcardIconSys sys;
    double background[4][3], light_dir[3][3], light_color[3][3], ambient[1][3];
    JSValue result = JS_EXCEPTION, value;
    uint8_t *data;
    int ret;

    if (!mc_argc(ctx, argc, 1, 1, name))
        return JS_EXCEPTION;
    if (mc_required_options_arg(ctx, argc, argv, 0, name) < 0)
        return JS_EXCEPTION;

    athena_memcard_icon_sys_defaults(&sys);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 3; j++)
            background[i][j] = sys.background[i][j];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            light_dir[i][j] = sys.light_dir[i][j];
            light_color[i][j] = sys.light_color[i][j];
        }
        ambient[0][i] = sys.ambient[i];
    }

    if (mc_string_option(ctx, argv[0], "title", name, true, &title) < 0 ||
        mc_string_option(ctx, argv[0], "icon", name, true, &icon) < 0 ||
        mc_string_option(ctx, argv[0], "copyIcon", name, false, &copy_icon) < 0 ||
        mc_string_option(ctx, argv[0], "deleteIcon", name, false, &delete_icon) < 0 ||
        mc_matrix_option(ctx, argv[0], "background", name, 4, 0, 255, background) < 0 ||
        mc_matrix_option(ctx, argv[0], "lightDirections", name, 3, -1, 1, light_dir) < 0 ||
        mc_matrix_option(ctx, argv[0], "lightColors", name, 3, 0, 1, light_color) < 0)
        goto out;

    value = JS_GetPropertyStr(ctx, argv[0], "ambient");
    if (JS_IsException(value))
        goto out;
    ret = JS_IsUndefined(value) ? 0 :
        mc_numbers_option(ctx, value, "ambient", name, 3, 0, 1, ambient[0]);
    JS_FreeValue(ctx, value);
    if (ret < 0)
        goto out;

    value = JS_GetPropertyStr(ctx, argv[0], "backgroundAlpha");
    if (JS_IsException(value))
        goto out;
    if (!JS_IsUndefined(value)) {
        int32_t alpha;
        ret = !JS_IsNumber(value) || JS_ToInt32(ctx, &alpha, value) || alpha < 0 || alpha > 128;
        JS_FreeValue(ctx, value);
        if (ret) {
            mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s options.backgroundAlpha must be 0-128", name);
            goto out;
        }
        sys.background_alpha = (uint8_t)alpha;
    }

    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 3; j++)
            sys.background[i][j] = (int)background[i][j];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            sys.light_dir[i][j] = (float)light_dir[i][j];
            sys.light_color[i][j] = (float)light_color[i][j];
        }
        sys.ambient[i] = (float)ambient[0][i];
    }
    sys.title = title;
    sys.icon = icon;
    sys.copy_icon = copy_icon;
    sys.delete_icon = delete_icon;

    data = malloc(ATHENA_MEMCARD_ICON_SYS_SIZE);
    if (!data) {
        JS_ThrowOutOfMemory(ctx);
        goto out;
    }
    if (athena_memcard_build_icon_sys(&sys, data) < 0) {
        free(data);
        mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT,
            "%s: the title must be 1-33 printable ASCII characters with at most one \"\\n\", "
            "and icon names 1-63 characters without \"/\"", name);
        goto out;
    }
    result = mc_new_buffer(ctx, data, ATHENA_MEMCARD_ICON_SYS_SIZE);

out:
    if (title) JS_FreeCString(ctx, title);
    if (icon) JS_FreeCString(ctx, icon);
    if (copy_icon) JS_FreeCString(ctx, copy_icon);
    if (delete_icon) JS_FreeCString(ctx, delete_icon);
    return result;
}

/* ------------------------------------------------------------------------ */
/* Background jobs                                                          */
/* ------------------------------------------------------------------------ */

typedef enum {
    MC_JOB_READ,                /* as MC_READ_BUFFER / TEXT / JSON: see mc_js_read_file_async */
    MC_JOB_READ_TEXT,
    MC_JOB_READ_JSON,
    MC_JOB_WRITE,
    MC_JOB_REMOVE,
    MC_JOB_FORMAT,
} McJobKind;

/*
 * The worker never runs script code: the script polls, or awaits the job,
 * which polls from a timer. The outcome is converted once and kept.
 */
typedef struct {
    AthenaMemcardJob *job;
    const char *name;
    McJobKind kind;
    char target[ATHENA_MEMCARD_PATH_MAX + 8];  /* "mc0:/..." for error messages */
    bool settled;
    bool failed;                /* settled with an error, a JSON parse error included */
    JSValue outcome;            /* result or error once settled */
    JSValue promise;            /* created by the first then() */
    JSValue resolve, reject;    /* until the promise is settled */
} McJobHandle;

static JSClassID mc_job_class_id;

static void mc_job_finalizer(JSRuntime *rt, JSValue value) {
    McJobHandle *handle = JS_GetOpaque(value, mc_job_class_id);
    if (!handle)
        return;
    /* Cancels and joins: at most one block of work remains (a format runs to the end). */
    athena_memcard_job_destroy(handle->job);
    JS_FreeValueRT(rt, handle->outcome);
    JS_FreeValueRT(rt, handle->promise);
    JS_FreeValueRT(rt, handle->resolve);
    JS_FreeValueRT(rt, handle->reject);
    free(handle);
}

static void mc_job_mark(JSRuntime *rt, JSValueConst value, JS_MarkFunc *mark) {
    McJobHandle *handle = JS_GetOpaque(value, mc_job_class_id);
    if (!handle)
        return;
    JS_MarkValue(rt, handle->outcome, mark);
    JS_MarkValue(rt, handle->promise, mark);
    JS_MarkValue(rt, handle->resolve, mark);
    JS_MarkValue(rt, handle->reject, mark);
}

static JSClassDef mc_job_class = {
    "MemoryCardJob",
    .finalizer = mc_job_finalizer,
    .gc_mark = mc_job_mark,
};

static McJobHandle *mc_job_handle(JSContext *ctx, JSValueConst value, const char *name) {
    McJobHandle *handle = JS_GetOpaque(value, mc_job_class_id);
    if (!handle)
        mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s: expected a MemoryCard job", name);
    return handle;
}

static JSValue mc_new_job(JSContext *ctx, AthenaMemcardJob *job, const char *name, McJobKind kind,
    int port, const char *path) {
    McJobHandle *handle;
    JSValue object;

    if (!job)
        return mc_throw(ctx, ATHENA_MEMCARD_ERR_MEMORY, "%s: cannot start the worker thread", name);
    handle = calloc(1, sizeof(*handle));
    if (!handle) {
        athena_memcard_job_destroy(job);
        return JS_ThrowOutOfMemory(ctx);
    }
    handle->job = job;
    handle->name = name;
    handle->kind = kind;
    snprintf(handle->target, sizeof(handle->target), "mc%d:%s", port, path ? path : "");
    handle->outcome = JS_UNDEFINED;
    handle->promise = JS_UNDEFINED;
    handle->resolve = JS_UNDEFINED;
    handle->reject = JS_UNDEFINED;
    object = JS_NewObjectClass(ctx, mc_job_class_id);
    if (JS_IsException(object)) {
        athena_memcard_job_destroy(job);
        free(handle);
        return object;
    }
    JS_SetOpaque(object, handle);
    return object;
}

/* readFileAsync / readTextAsync / readJSONAsync: the worker reads, the script converts. */
static JSValue mc_js_read_file_async(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
    int as) {
    static const char *const names[] = {
        "MemoryCard.readFileAsync", "MemoryCard.readTextAsync", "MemoryCard.readJSONAsync",
    };
    static const McJobKind kinds[] = { MC_JOB_READ, MC_JOB_READ_TEXT, MC_JOB_READ_JSON };
    McJsPath path;

    if (mc_begin(ctx, argc, argv, 1, 1, names[as], &path) < 0)
        return JS_EXCEPTION;
    return mc_new_job(ctx, athena_memcard_job_read(path.port, path.path), names[as], kinds[as],
        path.port, path.path);
}

/* Starts a write job with the parsed arguments, then frees their copy (the job has its own). */
static JSValue mc_write_start(JSContext *ctx, const char *name, McWriteArgs *args) {
    AthenaMemcardJob *job = athena_memcard_job_write(args->path.port, args->path.path, args->data,
        args->size, &args->options);
    free(args->data);
    return mc_new_job(ctx, job, name, MC_JOB_WRITE, args->path.port, args->path.path);
}

static JSValue mc_js_write_file_async(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = "MemoryCard.writeFileAsync";
    McWriteArgs args;

    if (mc_write_args(ctx, argc, argv, name, &args) < 0)
        return JS_EXCEPTION;
    return mc_write_start(ctx, name, &args);
}

static JSValue mc_js_remove_async(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = "MemoryCard.removeAsync";
    bool recursive = false;
    McJsPath path;
    int ret;

    if (!mc_argc(ctx, argc, 1, 2, name))
        return JS_EXCEPTION;
    ret = mc_options_arg(ctx, argc, argv, 1, name);
    if (ret < 0 || (ret > 0 && mc_bool_option(ctx, argv[1], "recursive", name, &recursive) < 0) ||
        mc_begin(ctx, argc, argv, 1, 2, name, &path) < 0)
        return JS_EXCEPTION;
    if (!path.path[1])
        return mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s: the root cannot be removed (use formatAsync)", name);
    return mc_new_job(ctx, athena_memcard_job_remove(path.port, path.path, recursive), name,
        MC_JOB_REMOVE, path.port, path.path);
}

static JSValue mc_js_format_async(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = "MemoryCard.formatAsync";
    int32_t port;

    if (!mc_argc(ctx, argc, 1, 1, name))
        return JS_EXCEPTION;
    if (!JS_IsNumber(argv[0]) || JS_ToInt32(ctx, &port, argv[0]) || port < 0 || port >= ATHENA_MEMCARD_PORTS)
        return mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT, "%s port must be 0 or 1", name);
    if (mc_ready(ctx, name) < 0)
        return JS_EXCEPTION;
    return mc_new_job(ctx, athena_memcard_job_format(port), name, MC_JOB_FORMAT, port, NULL);
}

static const char *mc_job_state_name(AthenaMemcardJobState state) {
    switch (state) {
    case ATHENA_MEMCARD_JOB_RUNNING: return "running";
    case ATHENA_MEMCARD_JOB_DONE: return "done";
    case ATHENA_MEMCARD_JOB_CANCELLED: return "cancelled";
    default: return "failed";
    }
}

/* Converts the outcome once, the first time the job is seen settled. */
static int mc_job_settle(JSContext *ctx, McJobHandle *handle, const AthenaMemcardJobStatus *status) {
    if (handle->settled || status->state == ATHENA_MEMCARD_JOB_RUNNING)
        return 0;
    if (status->state == ATHENA_MEMCARD_JOB_DONE) {
        if (handle->kind == MC_JOB_READ || handle->kind == MC_JOB_READ_TEXT ||
            handle->kind == MC_JOB_READ_JSON) {
            void *data = NULL;
            size_t size = 0;
            /* The data ends with a ' ' (athena_memcard_read_file). */
            athena_memcard_job_take_data(handle->job, &data, &size);
            if (!data && !(data = calloc(1, 1))) {
                JS_ThrowOutOfMemory(ctx);
                return -1;
            }
            if (handle->kind == MC_JOB_READ) {
                handle->outcome = mc_new_buffer(ctx, data, size);
            } else {
                handle->outcome = handle->kind == MC_JOB_READ_TEXT ?
                    JS_NewStringLen(ctx, data, size) : JS_ParseJSON(ctx, data, size, handle->target);
                free(data);
                /* A parse error settles the job as failed, with the SyntaxError. */
                if (JS_IsException(handle->outcome) && handle->kind == MC_JOB_READ_JSON) {
                    handle->outcome = JS_GetException(ctx);
                    JS_SetPropertyStr(ctx, handle->outcome, "path", JS_NewString(ctx, handle->target));
                    handle->failed = true;
                }
            }
        } else if (handle->kind == MC_JOB_WRITE) {
            handle->outcome = JS_NewInt32(ctx, status->result);
        } else {
            handle->outcome = JS_UNDEFINED;
        }
    } else {
        handle->failed = true;
        handle->outcome = mc_error(ctx, status->result, "%s: %s: %s", handle->name,
            athena_memcard_strerror(status->result), handle->target);
        if (!JS_IsException(handle->outcome))
            JS_SetPropertyStr(ctx, handle->outcome, "path", JS_NewString(ctx, handle->target));
    }
    if (JS_IsException(handle->outcome)) {
        handle->outcome = JS_UNDEFINED;
        return -1;
    }
    handle->settled = true;
    return 0;
}

static JSValue mc_job_status_object(JSContext *ctx, McJobHandle *handle) {
    AthenaMemcardJobStatus status;
    JSValue object;

    athena_memcard_job_status(handle->job, &status);
    if (mc_job_settle(ctx, handle, &status) < 0)
        return JS_EXCEPTION;
    object = JS_NewObject(ctx);
    if (JS_IsException(object))
        return object;
    JS_DefinePropertyValueStr(ctx, object, "state", JS_NewString(ctx,
        handle->settled && handle->failed && status.state == ATHENA_MEMCARD_JOB_DONE ? "failed" :
        mc_job_state_name(status.state)), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, object, "bytesDone",
        JS_NewInt64(ctx, (int64_t)status.bytes_done), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, object, "bytesTotal",
        JS_NewInt64(ctx, (int64_t)status.bytes_total), JS_PROP_C_W_E);
    if (handle->settled)
        JS_DefinePropertyValueStr(ctx, object,
            handle->failed ? "error" : "result",
            JS_DupValue(ctx, handle->outcome), JS_PROP_C_W_E);
    return object;
}

static JSValue mc_js_poll(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    McJobHandle *handle;

    if (!mc_argc(ctx, argc, 1, 1, "MemoryCard.poll") ||
        !(handle = mc_job_handle(ctx, argv[0], "MemoryCard.poll")))
        return JS_EXCEPTION;
    return mc_job_status_object(ctx, handle);
}

static JSValue mc_js_cancel(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    McJobHandle *handle;

    if (!mc_argc(ctx, argc, 1, 1, "MemoryCard.cancel") ||
        !(handle = mc_job_handle(ctx, argv[0], "MemoryCard.cancel")))
        return JS_EXCEPTION;
    athena_memcard_job_cancel(handle->job);
    return JS_UNDEFINED;
}

/* MemoryCard.wait(job, timeoutMs?): blocks with the GIL released, then polls. */
static JSValue mc_js_wait(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    McJobHandle *handle;
    int32_t timeout = -1;

    if (!mc_argc(ctx, argc, 1, 2, "MemoryCard.wait") ||
        !(handle = mc_job_handle(ctx, argv[0], "MemoryCard.wait")))
        return JS_EXCEPTION;
    if (mc_present(argc, argv, 1)) {
        double number;
        if (!JS_IsNumber(argv[1]) || JS_ToFloat64(ctx, &number, argv[1]) ||
            !isfinite(number) || number < 0 || number > INT32_MAX)
            return mc_throw(ctx, ATHENA_MEMCARD_ERR_ARGUMENT,
                "MemoryCard.wait timeout must be a non-negative number of milliseconds");
        timeout = (int32_t)number;
    }
    athena_js_gil_unlock();
    athena_memcard_job_wait(handle->job, timeout);
    athena_js_gil_lock();
    return mc_job_status_object(ctx, handle);
}

static JSValue mc_job_tick(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
    int magic, JSValue *data);

/* Checks the job again in `delay_ms`, through the event loop's timers. */
static int mc_job_schedule(JSContext *ctx, JSValueConst job, int delay_ms) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue set_timeout = JS_GetPropertyStr(ctx, global, "setTimeout");
    JSValue args[2], ret;

    JS_FreeValue(ctx, global);
    if (!JS_IsFunction(ctx, set_timeout)) {
        JS_FreeValue(ctx, set_timeout);
        mc_throw(ctx, ATHENA_MEMCARD_ERR_NOT_READY, "MemoryCard: awaiting a job needs setTimeout");
        return -1;
    }
    args[0] = JS_NewCFunctionData(ctx, mc_job_tick, 0, 0, 1, (JSValue *)&job);
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
static JSValue mc_job_tick(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
    int magic, JSValue *data) {
    McJobHandle *handle = JS_GetOpaque(data[0], mc_job_class_id);
    AthenaMemcardJobStatus status;
    JSValue func, value, ret;

    if (!handle || JS_IsUndefined(handle->resolve))
        return JS_UNDEFINED;
    athena_memcard_job_status(handle->job, &status);
    if (status.state == ATHENA_MEMCARD_JOB_RUNNING)
        return mc_job_schedule(ctx, data[0], MC_JOB_TICK_MS) < 0 ? JS_EXCEPTION : JS_UNDEFINED;

    if (mc_job_settle(ctx, handle, &status) < 0) {
        func = handle->reject;
        value = JS_GetException(ctx);
    } else {
        func = handle->failed ? handle->reject : handle->resolve;
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
static JSValue mc_job_then(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    McJobHandle *handle = JS_GetOpaque2(ctx, this_val, mc_job_class_id);
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
        if (mc_job_schedule(ctx, this_val, 0) < 0)
            return JS_EXCEPTION;
    }
    then = JS_GetPropertyStr(ctx, handle->promise, "then");
    if (JS_IsException(then))
        return then;
    ret = JS_Call(ctx, then, handle->promise, argc, argv);
    JS_FreeValue(ctx, then);
    return ret;
}

static const JSCFunctionListEntry mc_job_proto[] = {
    JS_CFUNC_DEF("then", 2, mc_job_then),
};

/* ------------------------------------------------------------------------ */
/* Registration                                                             */
/* ------------------------------------------------------------------------ */

static const JSCFunctionListEntry mc_module_funcs[] = {
    JS_CFUNC_DEF("getInfo", 1, mc_js_get_info),
    JS_CFUNC_MAGIC_DEF("format", 1, mc_js_format, 0),
    JS_CFUNC_MAGIC_DEF("unformat", 1, mc_js_format, 1),
    JS_CFUNC_DEF("stat", 1, mc_js_stat),
    JS_CFUNC_DEF("exists", 1, mc_js_exists),
    JS_CFUNC_DEF("list", 1, mc_js_list),
    JS_CFUNC_DEF("mkdir", 2, mc_js_mkdir),
    JS_CFUNC_DEF("remove", 2, mc_js_remove),
    JS_CFUNC_DEF("rename", 2, mc_js_rename),
    JS_CFUNC_DEF("setInfo", 2, mc_js_set_info),
    JS_CFUNC_DEF("getFreeEntries", 1, mc_js_free_entries),
    JS_CFUNC_MAGIC_DEF("readFile", 1, mc_js_read_file, MC_READ_BUFFER),
    JS_CFUNC_MAGIC_DEF("readText", 1, mc_js_read_file, MC_READ_TEXT),
    JS_CFUNC_MAGIC_DEF("readJSON", 1, mc_js_read_file, MC_READ_JSON),
    JS_CFUNC_DEF("writeFile", 3, mc_js_write_file),
    JS_CFUNC_MAGIC_DEF("writeJSON", 3, mc_js_write_json, 0),
    JS_CFUNC_DEF("open", 2, mc_js_open),
    JS_CFUNC_DEF("createIconSys", 1, mc_js_create_icon_sys),
    JS_CFUNC_MAGIC_DEF("readFileAsync", 1, mc_js_read_file_async, MC_READ_BUFFER),
    JS_CFUNC_MAGIC_DEF("readTextAsync", 1, mc_js_read_file_async, MC_READ_TEXT),
    JS_CFUNC_MAGIC_DEF("readJSONAsync", 1, mc_js_read_file_async, MC_READ_JSON),
    JS_CFUNC_DEF("writeFileAsync", 3, mc_js_write_file_async),
    JS_CFUNC_MAGIC_DEF("writeJSONAsync", 3, mc_js_write_json, 1),
    JS_CFUNC_DEF("removeAsync", 2, mc_js_remove_async),
    JS_CFUNC_DEF("formatAsync", 1, mc_js_format_async),
    JS_CFUNC_DEF("poll", 1, mc_js_poll),
    JS_CFUNC_DEF("wait", 2, mc_js_wait),
    JS_CFUNC_DEF("cancel", 1, mc_js_cancel),
    JS_PROP_INT32_DEF("ATTR_READABLE", ATHENA_MEMCARD_ATTR_READABLE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ATTR_WRITABLE", ATHENA_MEMCARD_ATTR_WRITABLE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ATTR_EXECUTABLE", ATHENA_MEMCARD_ATTR_EXECUTABLE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ATTR_PROTECTED", ATHENA_MEMCARD_ATTR_PROTECTED, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ATTR_FILE", ATHENA_MEMCARD_ATTR_FILE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ATTR_DIRECTORY", ATHENA_MEMCARD_ATTR_DIRECTORY, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ATTR_CLOSED", ATHENA_MEMCARD_ATTR_CLOSED, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ATTR_PDA_EXEC", ATHENA_MEMCARD_ATTR_PDA_EXEC, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ATTR_PS1", ATHENA_MEMCARD_ATTR_PS1, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ATTR_HIDDEN", ATHENA_MEMCARD_ATTR_HIDDEN, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ATTR_EXISTS", ATHENA_MEMCARD_ATTR_EXISTS, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("NAME_MAX", ATHENA_MEMCARD_NAME_MAX, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MAX_OPEN_FILES", ATHENA_MEMCARD_MAX_OPEN, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("CLUSTER_SIZE", ATHENA_MEMCARD_CLUSTER_SIZE, JS_PROP_ENUMERABLE),
};

static int mc_module_init(JSContext *ctx, JSModuleDef *m) {
    JSValue proto;

    if (athena_register_class(ctx, &mc_file_class_id, &mc_file_class) < 0 ||
        athena_register_class(ctx, &mc_job_class_id, &mc_job_class) < 0)
        return -1;
    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, mc_file_proto, countof(mc_file_proto));
    JS_SetClassProto(ctx, mc_file_class_id, proto);
    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, mc_job_proto, countof(mc_job_proto));
    JS_SetClassProto(ctx, mc_job_class_id, proto);
    return JS_SetModuleExportList(ctx, m, mc_module_funcs, countof(mc_module_funcs));
}

JSModuleDef *athena_memcard_init(JSContext *ctx) {
    return athena_push_module(ctx, mc_module_init, mc_module_funcs, countof(mc_module_funcs), "MemoryCard");
}
