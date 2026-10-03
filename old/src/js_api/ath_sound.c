#include <ath_env.h>
#include <ath_bindings.h>
#include <athena/sound.h>

static JSValue js_set_volume(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv){
	int volume;
	JS_ToInt32(ctx, &volume, argv[0]);
	athena_sound_set_volume(volume);
	return JS_UNDEFINED;
}

static JSValue js_find_channel(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv){
	return JS_NewInt32(ctx, athena_sound_find_channel());
}

static JSClassID js_sound_stream_class_id;

static void js_sound_stream_dtor(JSRuntime *rt, JSValue val) {
	JSSoundStream *snd = JS_GetOpaque(val, js_sound_stream_class_id);
	if (!snd)
		return;
	athena_sound_free(snd->sound);
	js_free_rt(rt, snd);
	JS_SetOpaque(val, NULL);
}

static JSValue js_sound_stream_load(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv){
	JSSoundStream* snd = NULL;
    JSValue obj = JS_UNDEFINED;
	const char* path;

    obj = JS_NewObjectClass(ctx, js_sound_stream_class_id);
	if (JS_IsException(obj))
        goto stream_fail;

	snd = js_mallocz(ctx, sizeof(*snd));
    if (!snd)
        return JS_EXCEPTION;

	path = JS_ToCString(ctx, argv[0]);
	snd->sound = athena_sound_load(path);
	JS_FreeCString(ctx, path);

	if (!snd->sound)
		goto stream_fail;

    JS_SetOpaque(obj, snd);
    return obj;

 stream_fail:
    js_free(ctx, snd);
    return JS_EXCEPTION;
}

static JSValue js_sound_stream_play(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	JSSoundStream* snd = JS_GetOpaque2(ctx, this_val, js_sound_stream_class_id);
	athena_sound_play(snd->sound);
	return JS_UNDEFINED;
}

static JSValue js_sound_stream_free(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv){
	js_sound_stream_dtor(JS_GetRuntime(ctx), this_val);
	return JS_UNDEFINED;
}

static JSValue js_is_playing(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv){
	JSSoundStream* snd = JS_GetOpaque2(ctx, this_val, js_sound_stream_class_id);
	return JS_NewBool(ctx, athena_sound_is_playing(snd->sound));
}

static JSValue js_get_duration(JSContext *ctx, JSValueConst this_val){
	JSSoundStream* snd = JS_GetOpaque2(ctx, this_val, js_sound_stream_class_id);
	return JS_NewInt32(ctx, athena_sound_get_duration(snd->sound));
}

static JSValue js_sound_stream_pause(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv){
	JSSoundStream* snd = JS_GetOpaque2(ctx, this_val, js_sound_stream_class_id);
	(void)snd;
	athena_sound_pause();
	return JS_UNDEFINED;
}

static JSValue js_rewind(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv){
	JSSoundStream* snd = JS_GetOpaque2(ctx, this_val, js_sound_stream_class_id);
	athena_sound_rewind(snd->sound);
	return JS_UNDEFINED;
}

static JSValue js_set_position(JSContext *ctx, JSValueConst this_val, JSValue val){
    JSSoundStream* snd = JS_GetOpaque2(ctx, this_val, js_sound_stream_class_id);
    uint32_t position;
    JS_ToUint32(ctx, &position, val);
    athena_sound_set_position(snd->sound, position);
    return JS_UNDEFINED;
}

static JSValue js_get_position(JSContext *ctx, JSValueConst this_val){
    JSSoundStream* snd = JS_GetOpaque2(ctx, this_val, js_sound_stream_class_id);
    return JS_NewInt32(ctx, athena_sound_get_position(snd->sound));
}

static JSValue js_set_loop(JSContext *ctx, JSValueConst this_val, JSValue val){
    JSSoundStream* snd = JS_GetOpaque2(ctx, this_val, js_sound_stream_class_id);
    athena_sound_set_loop(snd->sound, JS_ToBool(ctx, val));
    return JS_UNDEFINED;
}

static JSValue js_get_loop(JSContext *ctx, JSValueConst this_val){
    JSSoundStream* snd = JS_GetOpaque2(ctx, this_val, js_sound_stream_class_id);
    return JS_NewBool(ctx, athena_sound_get_loop(snd->sound));
}

static const JSCFunctionListEntry js_sound_stream_proto_funcs[] = {
	JS_CFUNC_DEF("play", 0, js_sound_stream_play),
	JS_CFUNC_DEF("free", 0, js_sound_stream_free),
	JS_CFUNC_DEF("pause", 0, js_sound_stream_pause),
	JS_CFUNC_DEF("playing", 0, js_is_playing),
	JS_CFUNC_DEF("rewind", 0, js_rewind),
	JS_CGETSET_DEF("loop", js_get_loop, js_set_loop),
	JS_CGETSET_DEF("position", js_get_position, js_set_position),
	JS_CGETSET_DEF("length", js_get_duration, NULL),
};

static JSClassDef js_sound_stream_class = {
    "Stream",
    .finalizer = js_sound_stream_dtor,
};

static JSClassID js_sound_sfx_class_id;

static void js_sound_sfx_dtor(JSRuntime *rt, JSValue val) {
	JSSoundEffect *snd = JS_GetOpaque(val, js_sound_sfx_class_id);
	if (!snd)
		return;
	athena_sfx_free(snd->sfx);
	js_free_rt(rt, snd);
	JS_SetOpaque(val, NULL);
}

static JSValue js_sound_sfx_load(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv){
	JSSoundEffect* snd = NULL;
    JSValue obj = JS_UNDEFINED;
	const char* path;

    obj = JS_NewObjectClass(ctx, js_sound_sfx_class_id);
	if (JS_IsException(obj))
        goto sfx_fail;

	snd = js_mallocz(ctx, sizeof(*snd));
    if (!snd)
        return JS_EXCEPTION;

	path = JS_ToCString(ctx, argv[0]);
	snd->sfx = athena_sfx_load(path);
	JS_FreeCString(ctx, path);

	if (!snd->sfx)
		goto sfx_fail;

    JS_SetOpaque(obj, snd);
    return obj;

 sfx_fail:
    js_free(ctx, snd);
    return JS_EXCEPTION;
}

static JSValue js_sound_sfx_play(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	JSSoundEffect* snd = JS_GetOpaque2(ctx, this_val, js_sound_sfx_class_id);
	int channel = -1;
	if (argc > 0)
		JS_ToInt32(ctx, &channel, argv[0]);
	return JS_NewInt32(ctx, athena_sfx_play(snd->sfx, channel));
}

static JSValue js_sound_sfx_free(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv){
	js_sound_sfx_dtor(JS_GetRuntime(ctx), this_val);
	return JS_UNDEFINED;
}

static JSValue js_sound_sfx_get_duration(JSContext *ctx, JSValueConst this_val){
	JSSoundEffect* snd = JS_GetOpaque2(ctx, this_val, js_sound_sfx_class_id);
	return JS_NewInt32(ctx, athena_sfx_get_length(snd->sfx));
}

static JSValue js_sound_sfx_is_playing(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	JSSoundEffect* snd = JS_GetOpaque2(ctx, this_val, js_sound_sfx_class_id);
	int channel = -1;
	JS_ToInt32(ctx, &channel, argv[0]);
	return JS_NewBool(ctx, athena_sfx_is_playing(snd->sfx, channel));
}

static JSValue js_sound_sfx_get_prop(JSContext *ctx, JSValueConst this_val, int magic)
{
    JSSoundEffect *snd = JS_GetOpaque2(ctx, this_val, js_sound_sfx_class_id);
    if (!snd)
        return JS_EXCEPTION;

	switch (magic) {
		case 0: return JS_NewUint32(ctx, athena_sfx_get_volume(snd->sfx));
		case 1: return JS_NewUint32(ctx, athena_sfx_get_pan(snd->sfx));
		case 2: return JS_NewBool(ctx, athena_sfx_get_loop(snd->sfx));
		case 3: return JS_NewInt32(ctx, athena_sfx_get_pitch(snd->sfx));
	}
	return JS_UNDEFINED;
}

static JSValue js_sound_sfx_set_prop(JSContext *ctx, JSValueConst this_val, JSValue val, int magic)
{
    JSSoundEffect *snd = JS_GetOpaque2(ctx, this_val, js_sound_sfx_class_id);
    int value = 0;

    if (!snd)
        return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &value, val))
        return JS_EXCEPTION;

	switch (magic) {
		case 0: athena_sfx_set_volume(snd->sfx, (uint32_t)value); break;
		case 1: athena_sfx_set_pan(snd->sfx, (uint32_t)value); break;
		case 2: athena_sfx_set_loop(snd->sfx, value != 0); break;
		case 3: athena_sfx_set_pitch(snd->sfx, value); break;
	}
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_sound_sfx_proto_funcs[] = {
	JS_CFUNC_DEF("play", 1, js_sound_sfx_play),
	JS_CFUNC_DEF("free", 0, js_sound_sfx_free),
	JS_CFUNC_DEF("playing", 1, js_sound_sfx_is_playing),
	JS_CGETSET_DEF("length", js_sound_sfx_get_duration, NULL),
	JS_CGETSET_MAGIC_DEF("volume", js_sound_sfx_get_prop, js_sound_sfx_set_prop, 0),
	JS_CGETSET_MAGIC_DEF("pan",    js_sound_sfx_get_prop, js_sound_sfx_set_prop, 1),
	JS_CGETSET_MAGIC_DEF("loop",   js_sound_sfx_get_prop, js_sound_sfx_set_prop, 2),
	JS_CGETSET_MAGIC_DEF("pitch",  js_sound_sfx_get_prop, js_sound_sfx_set_prop, 3),
};

static JSClassDef js_sound_sfx_class = {
    "Sfx",
    .finalizer = js_sound_sfx_dtor,
};

static const JSCFunctionListEntry module_funcs[] = {
	JS_CFUNC_DEF("setVolume",   1, js_set_volume),
	JS_CFUNC_DEF("findChannel", 0, js_find_channel),
	JS_CFUNC_SPECIAL_DEF("Stream", 1, constructor_or_func, js_sound_stream_load),
	JS_CFUNC_SPECIAL_DEF("Sfx",    1, constructor_or_func, js_sound_sfx_load),
};

static int js_sound_module_init(JSContext *ctx, JSModuleDef *m) {
    JSValue proto;

    JS_NewClassID(&js_sound_stream_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_sound_stream_class_id, &js_sound_stream_class);
    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, js_sound_stream_proto_funcs,
                               countof(js_sound_stream_proto_funcs));
    JS_SetClassProto(ctx, js_sound_stream_class_id, proto);

    JS_NewClassID(&js_sound_sfx_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_sound_sfx_class_id, &js_sound_sfx_class);
    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, js_sound_sfx_proto_funcs,
                               countof(js_sound_sfx_proto_funcs));
    JS_SetClassProto(ctx, js_sound_sfx_class_id, proto);

    return JS_SetModuleExportList(ctx, m, module_funcs, countof(module_funcs));
}

JSModuleDef *athena_sound_init(JSContext* ctx){
	return athena_push_module(ctx, js_sound_module_init, module_funcs, countof(module_funcs), "Sound");
}
