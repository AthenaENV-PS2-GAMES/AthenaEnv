#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <ath_env.h>
#include <iop_manager.h>
#include <smem.h>

#include <athena_module.h>

#include "ath_iop.h"

static int iop_require_argc(JSContext *ctx, int argc, int minimum,
    int maximum, const char *name) {
    if (argc < minimum || (maximum >= 0 && argc > maximum)) {
        if (maximum == minimum) {
            JS_ThrowTypeError(ctx, "%s expects %d argument%s",
                name, minimum, minimum == 1 ? "" : "s");
        } else {
            JS_ThrowTypeError(ctx, "%s expects between %d and %d arguments",
                name, minimum, maximum);
        }
        return 0;
    }
    return 1;
}

static module_entry *iop_module_from_value(JSContext *ctx, JSValueConst value) {
    if (JS_IsString(value)) {
        const char *name = JS_ToCString(ctx, value);
        module_entry *module;
        if (!name) return NULL;
        module = iopman_search_module(name);
        JS_FreeCString(ctx, name);
        if (!module) {
            JS_ThrowReferenceError(ctx, "IOP module not found");
        }
        return module;
    }

    uint32_t id;
    uint32_t module_count;
    if (JS_ToUint32(ctx, &id, value) < 0) return NULL;
    iopman_get_modules(&module_count);
    if (id >= module_count) {
        JS_ThrowRangeError(ctx, "IOP module id out of range");
        return NULL;
    }
    module_entry *module = iopman_get_module((uint8_t)id);
    if (!module) JS_ThrowReferenceError(ctx, "IOP module not found");
    return module;
}

static JSValue iop_module_to_value(JSContext *ctx, const module_entry *module) {
    JSValue result = JS_NewObject(ctx);
    if (JS_IsException(result)) return result;

    JS_SetPropertyStr(ctx, result, "id", JS_NewUint32(ctx, module->id));
    JS_SetPropertyStr(ctx, result, "name", JS_NewString(ctx, module->name));
    JS_SetPropertyStr(ctx, result, "started", JS_NewBool(ctx, module->started));
    JS_SetPropertyStr(ctx, result, "startAtBoot",
        JS_NewBool(ctx, module->start_at_boot));
    return result;
}

static JSValue athena_iop_get_modules(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    uint32_t count = 0;
    module_entry *modules;
    JSValue result;

    if (!iop_require_argc(ctx, argc, 0, 0, "IOP.getModules"))
        return JS_EXCEPTION;

    modules = iopman_get_modules(&count);
    result = JS_NewArray(ctx);
    if (JS_IsException(result)) return result;

    for (uint32_t i = 0; i < count; i++) {
        JSValue item = iop_module_to_value(ctx, &modules[i]);
        if (JS_IsException(item)) {
            JS_FreeValue(ctx, result);
            return item;
        }
        JS_SetPropertyUint32(ctx, result, i, item);
    }
    return result;
}

static JSValue athena_iop_get_module(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    module_entry *module;

    if (!iop_require_argc(ctx, argc, 1, 1, "IOP.getModule"))
        return JS_EXCEPTION;
    module = iop_module_from_value(ctx, argv[0]);
    if (!module) return JS_EXCEPTION;
    return iop_module_to_value(ctx, module);
}

static JSValue athena_iop_load_module(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    module_entry *module;
    int result;

    if (!iop_require_argc(ctx, argc, 1, 1, "IOP.loadModule"))
        return JS_EXCEPTION;
    module = iop_module_from_value(ctx, argv[0]);
    if (!module) return JS_EXCEPTION;

    result = iopman_load_module(module, 0, NULL);
    if (result == MODULE_STATUS_INCOMPATIBILITY) {
        module_entry *incompatible = iopman_get_incompatible_module();
        return JS_ThrowInternalError(ctx,
            "IOP module '%s' is incompatible with '%s'",
            module->name, incompatible ? incompatible->name : "unknown");
    }
    if (result == MODULE_STATUS_ERROR) {
        return JS_ThrowInternalError(ctx, "Unable to load IOP module '%s'",
            module->name);
    }
    return JS_NewInt32(ctx, result);
}

static JSValue athena_iop_reset(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    if (!iop_require_argc(ctx, argc, 0, 0, "IOP.reset"))
        return JS_EXCEPTION;
    iopman_reset();
    return JS_UNDEFINED;
}

static JSValue athena_iop_get_memory_stats(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv) {
    int32_t free_memory = 0;
    int32_t used_memory;
    module_entry *freeram;

    if (!iop_require_argc(ctx, argc, 0, 0, "IOP.getMemoryStats"))
        return JS_EXCEPTION;

    freeram = iopman_search_module("freeram");
    if (!freeram || iopman_load_module(freeram, 0, NULL) == MODULE_STATUS_ERROR) {
        return JS_ThrowInternalError(ctx, "Unable to load IOP freeram module");
    }
    if (smem_read(IOP_FREERAM_ADDR, &free_memory,
            sizeof(free_memory)) < 0) {
        return JS_ThrowInternalError(ctx, "Unable to read IOP memory statistics");
    }
    if (free_memory < 0 || free_memory > IOP_TOTAL_RAM) {
        return JS_ThrowInternalError(ctx, "Invalid IOP memory statistics");
    }
    used_memory = IOP_TOTAL_RAM - free_memory;

    JSValue result = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, result, "free", JS_NewInt32(ctx, free_memory));
    JS_SetPropertyStr(ctx, result, "used", JS_NewInt32(ctx, used_memory));
    return result;
}

static const JSCFunctionListEntry iop_module_funcs[] = {
    JS_CFUNC_DEF("getModules", 0, athena_iop_get_modules),
    JS_CFUNC_DEF("getModule", 1, athena_iop_get_module),
    JS_CFUNC_DEF("loadModule", 1, athena_iop_load_module),
    JS_CFUNC_DEF("reset", 0, athena_iop_reset),
    JS_CFUNC_DEF("getMemoryStats", 0, athena_iop_get_memory_stats)
};

static int athena_iop_module_init(JSContext *ctx, JSModuleDef *m) {
    return JS_SetModuleExportList(ctx, m, iop_module_funcs,
        countof(iop_module_funcs));
}

JSModuleDef *athena_iop_init(JSContext *ctx) {
    return athena_push_module(ctx, athena_iop_module_init,
        iop_module_funcs, countof(iop_module_funcs), "IOP");
}
