#include <stdlib.h>
#include <string.h>

#include <ath_env.h>
#include <athena/anim3d.h>

static JSClassID js_animation_collection_class_id;

typedef struct {
	AthenaAnim3dCollection *collection;
} JSAnimCollection;

static void js_animation_collection_dtor(JSRuntime *rt, JSValue val){
	JSAnimCollection* ro = JS_GetOpaque(val, js_animation_collection_class_id);

	if (!ro)
		return;

	athena_anim3d_free(ro->collection);
	js_free_rt(rt, ro);
	JS_SetOpaque(val, NULL);
}

static JSValue js_animation_collection_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
	JSValue obj = JS_UNDEFINED;
    JSValue proto;
    JSAnimCollection* ro;
	const char *file_tbo;

    ro = js_mallocz(ctx, sizeof(JSAnimCollection));
    if (!ro)
        return JS_EXCEPTION;

	file_tbo = JS_ToCString(ctx, argv[0]);
	ro->collection = athena_anim3d_load(file_tbo);
	JS_FreeCString(ctx, file_tbo);

	if (!ro->collection)
		return JS_EXCEPTION;

register_3d_animation_collection:
    proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    obj = JS_NewObjectProtoClass(ctx, proto, js_animation_collection_class_id);
    JS_FreeValue(ctx, proto);
    JS_SetOpaque(obj, ro);
    return obj;
}

static JSValue js_animation_collection_get_prop(JSContext *ctx, JSValueConst obj, JSAtom prop, JSValueConst receiver) {
    JSAnimCollection *m = JS_GetOpaque2(ctx, obj, js_animation_collection_class_id);
    const char *prop_name;
    char *endptr;
    long index;

    if (!m || !m->collection)
        return JS_EXCEPTION;

    prop_name = JS_AtomToCString(ctx, prop);
    if (!prop_name) return JS_EXCEPTION;

    index = strtol(prop_name, &endptr, 10);

    if (*endptr == '\0' && index >= 0) {
        athena_animation *anim = athena_anim3d_get(m->collection, (uint32_t)index);
        JS_FreeCString(ctx, prop_name);
        if (anim)
            return JS_NewUint32(ctx, anim);
        return JS_EXCEPTION;
    }

    {
        athena_animation *anim = athena_anim3d_find(m->collection, prop_name);
        JS_FreeCString(ctx, prop_name);
        if (anim)
            return JS_NewUint32(ctx, anim);
    }

    return JS_UNDEFINED;
}

static int js_animation_collection_set_prop(JSContext *ctx, JSValueConst obj, JSAtom prop, JSValueConst val, JSValueConst receiver, int flags) {
    JSAnimCollection *m = JS_GetOpaque2(ctx, obj, js_animation_collection_class_id);
    const char *prop_name;
    char *endptr;
    long index;
    uint32_t value;

    if (!m || !m->collection)
        return -1;

    prop_name = JS_AtomToCString(ctx, prop);
    if (!prop_name) return -1;

    index = strtol(prop_name, &endptr, 10);

    if (*endptr == '\0' && index >= 0) {
        JS_FreeCString(ctx, prop_name);
        JS_ToUint32(ctx, &value, val);
        if (index < (long)athena_anim3d_count(m->collection)) {
            athena_animation *dst = athena_anim3d_get(m->collection, (uint32_t)index);
            memcpy(dst, (void *)(uintptr_t)value, sizeof(athena_animation));
            return 1;
        }
        return 0;
    }

    JS_ToUint32(ctx, &value, val);
    {
        athena_animation *dst = athena_anim3d_find(m->collection, prop_name);
        JS_FreeCString(ctx, prop_name);
        if (dst) {
            memcpy(dst, (void *)(uintptr_t)value, sizeof(athena_animation));
            return 1;
        }
    }

    return 2;
}

static const JSClassExoticMethods js_anim_collection_exotic = {
    .get_property = js_animation_collection_get_prop,
    .set_property = js_animation_collection_set_prop
};

static JSClassDef js_animation_collection_class = {
    "AnimCollection",
    .finalizer = js_animation_collection_dtor,
    .exotic = &js_anim_collection_exotic
};

static JSValue js_rdfree(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv){
	js_animation_collection_dtor(JS_GetRuntime(ctx), this_val);
	return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_animation_collection_proto_funcs[] = {
	JS_CFUNC_DEF("free",  0,  js_rdfree),
};

static int js_animation_collection_init(JSContext *ctx, JSModuleDef *m)
{
    JSValue animation_collection_proto, animation_collection_class;

    JS_NewClassID(&js_animation_collection_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_animation_collection_class_id, &js_animation_collection_class);

    animation_collection_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, animation_collection_proto, js_animation_collection_proto_funcs, countof(js_animation_collection_proto_funcs));

    animation_collection_class = JS_NewCFunction2(ctx, js_animation_collection_ctor, "AnimCollection", 1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, animation_collection_class, animation_collection_proto);
    JS_SetClassProto(ctx, js_animation_collection_class_id, animation_collection_proto);

    JS_SetModuleExport(ctx, m, "AnimCollection", animation_collection_class);
    return 0;
}

JSModuleDef *athena_anim_3d_init(JSContext* ctx){
    JSModuleDef *m;
    m = JS_NewCModule(ctx, "AnimCollection", js_animation_collection_init);
    if (!m)
        return NULL;
    JS_AddModuleExport(ctx, m, "AnimCollection");
	return m;
}
