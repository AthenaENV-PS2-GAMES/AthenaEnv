#include <ath_env.h>
#include <athena/iop_facade.h>
#include <macros.h>

static JSValue js_sifregistermodule(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
	void *data = NULL;
	size_t size = 0;
	size_t name_len = 0;
	uint8_t dependencies[4] = { EMPTY_ENTRY, EMPTY_ENTRY, EMPTY_ENTRY, EMPTY_ENTRY };
	JSValue init_fun = NULL, end_fun = NULL;
	iopman_func init = NULL, end = NULL;
	char *name = JS_ToCStringLen(ctx, &name_len, argv[0]);

	if (JS_IsString(argv[1])) {
		data = (void *)JS_ToCString(ctx, argv[1]);
		size = 0; /* size == 0 tells iopman_load_module this is a file path, not a buffer */
	} else {
		data = JS_GetArrayBuffer(ctx, &size, argv[1]);
	}

	if (argc > 2) {
		JSValue arg_array = JS_GetPropertyStr(ctx, argv[2], "deps");
		uint32_t num_deps = 0;
		JS_ToUint32(ctx, &num_deps, JS_GetPropertyStr(ctx, arg_array, "length"));

		for (int i = 0; i < (int)num_deps; i++) {
			uint32_t dep_id = EMPTY_ENTRY;
			JS_ToUint32(ctx, &dep_id, JS_GetPropertyUint32(ctx, arg_array, i));
			dependencies[i] = dep_id;
		}
		JS_FreeValue(ctx, arg_array);

		init_fun = JS_GetPropertyStr(ctx, argv[2], "init");
		if (JS_IsFunction(ctx, init_fun)) {
			init = lambda(int, (module_entry *module) {
				JS_Call(ctx, (JSValue)module->init_args, JS_UNDEFINED, 0, NULL);
				return 0;
			});
		} else if (JS_IsNumber(init_fun)) {
			JS_ToUint32(ctx, (uint32_t *)&init, init_fun);
		}

		end_fun = JS_GetPropertyStr(ctx, argv[2], "end");
		if (JS_IsFunction(ctx, end_fun)) {
			end = lambda(int, (module_entry *module) {
				JS_Call(ctx, (JSValue)module->end_args, JS_UNDEFINED, 0, NULL);
				return 0;
			});
		} else if (JS_IsNumber(end_fun)) {
			JS_ToUint32(ctx, (uint32_t *)&end, end_fun);
		}
	}

	module_entry *result = athena_iop_register_module(name, data, size, dependencies, init, end);
	if (!result)
		return JS_ThrowInternalError(ctx, "newModule: IOP module registry is full.");

	if (init_fun)
		result->init_args = (void *)init_fun;
	if (end_fun)
		result->end_args = (void *)end_fun;

	return JS_NewInt32(ctx, (int32_t)(intptr_t)result);
}

static JSValue js_sifloadmodule(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
	module_entry *module = NULL;

	if (JS_IsString(argv[0])) {
		char *module_name = JS_ToCString(ctx, argv[0]);
		module = athena_iop_get_module_by_name(module_name);
		JS_FreeCString(ctx, module_name);
	} else {
		uint32_t ptr = 0;
		JS_ToUint32(ctx, &ptr, argv[0]);
		module = (module_entry *)(intptr_t)ptr;
	}

	if (!module)
		return JS_ThrowInternalError(ctx, "loadModule: module not found.");

	int arg_len = 0;
	const char *args = NULL;
	if (argc > 1 && !JS_IsUndefined(argv[1])) {
		size_t len = 0;
		args = JS_ToCStringLen(ctx, &len, argv[1]);
		arg_len = (int)len;
	}

	int ret = athena_iop_load_module(module, arg_len, args);

	if (args)
		JS_FreeCString(ctx, args);

	if (ret == MODULE_STATUS_INCOMPATIBILITY) {
		return JS_ThrowInternalError(ctx, "loadModule: %s module is incompatible with %s, which is actually loaded. Keep or reset IOP.",
			module->name, iopman_get_incompatible_module()->name);
	}
	if (ret == MODULE_STATUS_ERROR)
		return JS_ThrowInternalError(ctx, "loadModule: error while loading %s.", module->name);

	return JS_UNDEFINED;
}

static JSValue js_sifgetmodule(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
	if (JS_IsString(argv[0])) {
		char *module_name = JS_ToCString(ctx, argv[0]);
		module_entry *module = athena_iop_get_module_by_name(module_name);
		JS_FreeCString(ctx, module_name);
		if (module)
			return JS_NewUint32(ctx, (uint32_t)(intptr_t)module);
	} else {
		uint32_t id = 0xFF;
		JS_ToUint32(ctx, &id, argv[0]);
		return JS_NewUint32(ctx, (uint32_t)(intptr_t)athena_iop_get_module_by_id((uint8_t)id));
	}
	return JS_UNDEFINED;
}

static JSValue js_sifgetmodules(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
	AthenaIopModuleListing *listing = athena_iop_list_modules();
	if (!listing || listing->count == 0) {
		athena_iop_module_listing_free(listing);
		return JS_UNDEFINED;
	}

	JSValue arr = JS_NewArray(ctx);
	for (int i = 0; i < listing->count; i++) {
		JSValue obj = JS_NewObject(ctx);
		JS_DefinePropertyValueStr(ctx, obj, "name", JS_NewString(ctx, listing->modules[i].name), JS_PROP_C_W_E);
		JS_DefinePropertyValueStr(ctx, obj, "id", JS_NewUint32(ctx, listing->modules[i].id), JS_PROP_C_W_E);
		JS_DefinePropertyValueUint32(ctx, arr, i, obj, JS_PROP_C_W_E);
	}

	athena_iop_module_listing_free(listing);
	return arr;
}

static JSValue js_resetiop(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
	athena_iop_reset();
	return JS_UNDEFINED;
}

static JSValue js_getiopmemory(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
	if (argc != 0)
		return JS_ThrowSyntaxError(ctx, "Wrong number of arguments");

	AthenaIopMemoryStats stats;
	athena_iop_get_memory_stats(&stats);

	JSValue obj = JS_NewObject(ctx);
	JS_DefinePropertyValueStr(ctx, obj, "free", JS_NewUint32(ctx, stats.free), JS_PROP_C_W_E);
	JS_DefinePropertyValueStr(ctx, obj, "used", JS_NewUint32(ctx, stats.used), JS_PROP_C_W_E);
	return obj;
}

static const JSCFunctionListEntry sif_funcs[] = {
	JS_CFUNC_DEF("newModule", 3, js_sifregistermodule),
	JS_CFUNC_DEF("loadModule", 2, js_sifloadmodule),
	JS_CFUNC_DEF("getModule", 1, js_sifgetmodule),
	JS_CFUNC_DEF("getModules", 0, js_sifgetmodules),
	JS_CFUNC_DEF("reset", 0, js_resetiop),
	JS_CFUNC_DEF("getMemoryStats", 0, js_getiopmemory),
};

static int sif_init(JSContext *ctx, JSModuleDef *m)
{
	return JS_SetModuleExportList(ctx, m, sif_funcs, countof(sif_funcs));
}

JSModuleDef *athena_iop_init(JSContext *ctx)
{
	return athena_push_module(ctx, sif_init, sif_funcs, countof(sif_funcs), "IOP");
}
