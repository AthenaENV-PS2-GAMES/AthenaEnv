#include <stdlib.h>

#include <ath_env.h>
#include <athena/archive.h>

static JSClassID athena_archive_class_id;

static void athena_archive_finalizer(JSRuntime *rt, JSValue value) {
    AthenaArchive *archive = JS_GetOpaque(value, athena_archive_class_id);
    if (archive) athena_archive_close(archive);
}

static JSClassDef athena_archive_class = {
    "AthenaArchive",
    .finalizer = athena_archive_finalizer,
};

static int archive_argc(JSContext *ctx, int argc, int minimum, int maximum, const char *name) {
    if (argc < minimum || argc > maximum) {
        if (minimum == maximum)
            JS_ThrowTypeError(ctx, "%s expects %d argument%s", name, minimum, minimum == 1 ? "" : "s");
        else
            JS_ThrowTypeError(ctx, "%s expects between %d and %d arguments", name, minimum, maximum);
        return 0;
    }
    return 1;
}

static JSValue archive_throw(JSContext *ctx, const char *name, int code) {
    if (code == ATHENA_ARCHIVE_ERR_ARGUMENT || code == ATHENA_ARCHIVE_ERR_TYPE)
        return JS_ThrowTypeError(ctx, "%s: %s", name, athena_archive_strerror(code));
    if (code == ATHENA_ARCHIVE_ERR_MEMORY)
        return JS_ThrowOutOfMemory(ctx);
    return JS_ThrowInternalError(ctx, "%s: %s", name, athena_archive_strerror(code));
}

static AthenaArchive *archive_from_value(JSContext *ctx, JSValueConst value) {
    AthenaArchive *archive = JS_GetOpaque2(ctx, value, athena_archive_class_id);
    if (!archive) {
        JS_ThrowTypeError(ctx, "Archive has already been closed");
        return NULL;
    }
    return archive;
}

/* Non-empty string argument. The caller frees it with JS_FreeCString. */
static const char *archive_path_arg(JSContext *ctx, JSValueConst value, const char *name) {
    const char *path;

    if (!JS_IsString(value)) {
        JS_ThrowTypeError(ctx, "%s must be a string", name);
        return NULL;
    }
    path = JS_ToCString(ctx, value);
    if (path && !path[0]) {
        JS_FreeCString(ctx, path);
        JS_ThrowTypeError(ctx, "%s must not be empty", name);
        return NULL;
    }
    return path;
}

/* Optional destination directory: undefined means the current directory. */
static int archive_dest_arg(JSContext *ctx, int argc, JSValueConst *argv, int index,
    const char *name, const char **out) {
    *out = NULL;
    if (argc <= index || JS_IsUndefined(argv[index]))
        return 1;
    *out = archive_path_arg(ctx, argv[index], name);
    return *out != NULL;
}

static JSValue athena_archive_open_js(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaArchive *archive = NULL;
    const char *path;
    JSValue object;
    int ret;

    if (!archive_argc(ctx, argc, 1, 1, "Archive.open")) return JS_EXCEPTION;
    path = archive_path_arg(ctx, argv[0], "Archive.open path");
    if (!path) return JS_EXCEPTION;

    ret = athena_archive_open(path, &archive);
    if (ret < 0) {
        JS_ThrowInternalError(ctx, "Archive.open: cannot open '%s': %s", path, athena_archive_strerror(ret));
        JS_FreeCString(ctx, path);
        return JS_EXCEPTION;
    }
    JS_FreeCString(ctx, path);

    object = JS_NewObjectClass(ctx, athena_archive_class_id);
    if (JS_IsException(object)) {
        athena_archive_close(archive);
        return object;
    }
    JS_SetOpaque(object, archive);
    return object;
}

static JSValue athena_archive_type_js(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaArchive *archive;

    if (!archive_argc(ctx, argc, 1, 1, "Archive.type")) return JS_EXCEPTION;
    archive = archive_from_value(ctx, argv[0]);
    if (!archive) return JS_EXCEPTION;
    return JS_NewString(ctx, athena_archive_type(archive) == ATHENA_ARCHIVE_ZIP ? "zip" : "gz");
}

static JSValue athena_archive_list_js(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaArchiveListing *listing = NULL;
    AthenaArchive *archive;
    JSValue array;
    int ret;

    if (!archive_argc(ctx, argc, 1, 1, "Archive.list")) return JS_EXCEPTION;
    archive = archive_from_value(ctx, argv[0]);
    if (!archive) return JS_EXCEPTION;

    ret = athena_archive_list(archive, &listing);
    if (ret < 0) return archive_throw(ctx, "Archive.list", ret);

    array = JS_NewArray(ctx);
    if (JS_IsException(array)) {
        athena_archive_listing_free(listing);
        return array;
    }
    for (int i = 0; i < listing->count; i++) {
        const AthenaArchiveEntry *entry = &listing->entries[i];
        JSValue item = JS_NewObject(ctx);
        if (JS_IsException(item)) {
            JS_FreeValue(ctx, array);
            athena_archive_listing_free(listing);
            return item;
        }
        JS_DefinePropertyValueStr(ctx, item, "name", JS_NewString(ctx, entry->name), JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, item, "size", JS_NewInt64(ctx, (int64_t)entry->size), JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, item, "mtime", JS_NewUint32(ctx, entry->mtime), JS_PROP_C_W_E);
        JS_DefinePropertyValueUint32(ctx, array, (uint32_t)i, item, JS_PROP_C_W_E);
    }

    athena_archive_listing_free(listing);
    return array;
}

static void archive_free_buffer(JSRuntime *rt, void *opaque, void *ptr) {
    free(ptr);
}

static JSValue athena_archive_extract_all_js(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaArchive *archive;
    const char *dest;
    int ret;

    if (!archive_argc(ctx, argc, 1, 2, "Archive.extractAll")) return JS_EXCEPTION;
    archive = archive_from_value(ctx, argv[0]);
    if (!archive) return JS_EXCEPTION;

    if (athena_archive_type(archive) == ATHENA_ARCHIVE_GZ) {
        void *data = NULL;
        size_t size = 0;
        JSValue buffer;

        if (argc == 2 && !JS_IsUndefined(argv[1]))
            return JS_ThrowTypeError(ctx, "Archive.extractAll: gzip archives are returned in memory and take no destination");
        ret = athena_archive_read_all(archive, &data, &size);
        if (ret < 0) return archive_throw(ctx, "Archive.extractAll", ret);
        buffer = JS_NewArrayBuffer(ctx, data, size, archive_free_buffer, NULL, 0);
        if (JS_IsException(buffer)) free(data);
        return buffer;
    }

    if (!archive_dest_arg(ctx, argc, argv, 1, "Archive.extractAll destination", &dest))
        return JS_EXCEPTION;
    ret = athena_archive_extract_all(archive, dest);
    if (dest) JS_FreeCString(ctx, dest);
    if (ret < 0) return archive_throw(ctx, "Archive.extractAll", ret);
    return JS_UNDEFINED;
}

static JSValue athena_archive_close_js(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AthenaArchive *archive;
    int ret;

    if (!archive_argc(ctx, argc, 1, 1, "Archive.close")) return JS_EXCEPTION;
    archive = archive_from_value(ctx, argv[0]);
    if (!archive) return JS_EXCEPTION;

    JS_SetOpaque((JSValue)argv[0], NULL);
    ret = athena_archive_close(archive);
    if (ret < 0) return archive_throw(ctx, "Archive.close", ret);
    return JS_UNDEFINED;
}

static JSValue athena_archive_untar_js(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *path;
    const char *dest;
    int ret;

    if (!archive_argc(ctx, argc, 1, 2, "Archive.untar")) return JS_EXCEPTION;
    path = archive_path_arg(ctx, argv[0], "Archive.untar path");
    if (!path) return JS_EXCEPTION;
    if (!archive_dest_arg(ctx, argc, argv, 1, "Archive.untar destination", &dest)) {
        JS_FreeCString(ctx, path);
        return JS_EXCEPTION;
    }

    ret = athena_archive_untar(path, dest);
    JS_FreeCString(ctx, path);
    if (dest) JS_FreeCString(ctx, dest);
    if (ret < 0) return archive_throw(ctx, "Archive.untar", ret);
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry archive_module_funcs[] = {
    JS_CFUNC_DEF("open", 1, athena_archive_open_js),
    JS_CFUNC_DEF("type", 1, athena_archive_type_js),
    JS_CFUNC_DEF("list", 1, athena_archive_list_js),
    JS_CFUNC_DEF("extractAll", 2, athena_archive_extract_all_js),
    JS_CFUNC_DEF("close", 1, athena_archive_close_js),
    JS_CFUNC_DEF("untar", 2, athena_archive_untar_js),
};

static int athena_archive_module_init(JSContext *ctx, JSModuleDef *m) {
    if (athena_register_class(ctx, &athena_archive_class_id, &athena_archive_class) < 0)
        return -1;
    return JS_SetModuleExportList(ctx, m, archive_module_funcs, countof(archive_module_funcs));
}

JSModuleDef *athena_archive_init(JSContext *ctx) {
    return athena_push_module(ctx, athena_archive_module_init,
        archive_module_funcs, countof(archive_module_funcs), "Archive");
}
