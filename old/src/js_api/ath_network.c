#include <ath_env.h>
#include <athena/net.h>
#include <macros.h>

static JSValue js_nw_init(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
	if (argc != 0 && argc != 4)
		return JS_ThrowInternalError(ctx, "wrong number of arguments.");

	int ret;
	if (argc == 4) {
		const char *ip = JS_ToCString(ctx, argv[0]);
		const char *nm = JS_ToCString(ctx, argv[1]);
		const char *gw = JS_ToCString(ctx, argv[2]);
		const char *dns = JS_ToCString(ctx, argv[3]);
		ret = athena_net_init(ip, nm, gw, dns);
		JS_FreeCString(ctx, ip);
		JS_FreeCString(ctx, nm);
		JS_FreeCString(ctx, gw);
		JS_FreeCString(ctx, dns);
	} else {
		ret = athena_net_init_dhcp();
	}

	if (ret != 0)
		return JS_ThrowInternalError(ctx, "Network initialization failed.");

	return JS_UNDEFINED;
}

static JSValue js_nw_get_config(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
	AthenaNetConfig config;
	if (athena_net_get_config(&config) != 0)
		return JS_ThrowInternalError(ctx, "Unable to read network info.\n");

	JSValue obj = JS_NewObject(ctx);
	JS_DefinePropertyValueStr(ctx, obj, "ip", JS_NewString(ctx, config.ip), JS_PROP_C_W_E);
	JS_DefinePropertyValueStr(ctx, obj, "netmask", JS_NewString(ctx, config.netmask), JS_PROP_C_W_E);
	JS_DefinePropertyValueStr(ctx, obj, "gateway", JS_NewString(ctx, config.gateway), JS_PROP_C_W_E);
	JS_DefinePropertyValueStr(ctx, obj, "dns", JS_NewString(ctx, config.dns), JS_PROP_C_W_E);
	return obj;
}

static JSValue js_nw_deinit(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
	athena_net_deinit();
	return JS_UNDEFINED;
}

static JSValue js_nw_gethostbyname(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv)
{
	const char *host = JS_ToCString(ctx, argv[0]);
	char ip[16];

	if (athena_net_resolve(host, ip, sizeof(ip)) != 0) {
		JS_FreeCString(ctx, host);
		return JS_ThrowInternalError(ctx, "Unable to resolve address.\n");
	}

	JSValue result = JS_NewString(ctx, ip);
	JS_FreeCString(ctx, host);
	return result;
}

static const JSCFunctionListEntry module_funcs[] = {
	JS_CFUNC_DEF("init", 4, js_nw_init),
	JS_CFUNC_DEF("getConfig", 0, js_nw_get_config),
	JS_CFUNC_DEF("getHostbyName", 1, js_nw_gethostbyname),
	JS_CFUNC_DEF("deinit", 0, js_nw_deinit),
};

static int module_init(JSContext *ctx, JSModuleDef *m)
{
	return JS_SetModuleExportList(ctx, m, module_funcs, countof(module_funcs));
}

JSModuleDef *athena_network_init(JSContext *ctx)
{
	return athena_push_module(ctx, module_init, module_funcs, countof(module_funcs), "Network");
}
