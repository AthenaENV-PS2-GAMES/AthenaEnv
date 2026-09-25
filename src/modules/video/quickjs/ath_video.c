#include <math.h>

#include <ath_env.h>
#include <athena/image.h>
#include <athena/js/image.h>
#include <athena/video.h>

#include "ath_video.h"

static JSClassID video_class_id;

typedef struct VideoHolder {
	AthenaVideo *video;
	/* Image returned by `frame`, created on first access. */
	JSValue frame;
	/* Sound.Stream set as `audio`: the clock playback follows, or undefined. */
	JSValue audio;
} VideoHolder;

/*
 * The frame Image borrows the video's surface. Before the surface goes away,
 * leave the Image empty so drawing it afterwards is a no-op instead of a
 * use-after-free.
 */
static void video_release(JSRuntime *rt, VideoHolder *holder)
{
	AthenaImage *image = athena_image_peek(holder->frame);

	if (image) {
		image->surface = NULL;
		image->loaded = false;
	}
	JS_FreeValueRT(rt, holder->frame);
	holder->frame = JS_UNDEFINED;
	/* The stream belongs to the script: it is released, not stopped. */
	JS_FreeValueRT(rt, holder->audio);
	holder->audio = JS_UNDEFINED;
	athena_video_destroy(holder->video);
	holder->video = NULL;
}

static void video_finalizer(JSRuntime *rt, JSValue value)
{
	VideoHolder *holder = JS_GetOpaque(value, video_class_id);

	if (holder) {
		video_release(rt, holder);
		js_free_rt(rt, holder);
	}
}

static void video_gc_mark(JSRuntime *rt, JSValueConst value,
	JS_MarkFunc *mark_func)
{
	VideoHolder *holder = JS_GetOpaque(value, video_class_id);

	if (holder) {
		JS_MarkValue(rt, holder->frame, mark_func);
		JS_MarkValue(rt, holder->audio, mark_func);
	}
}

static JSClassDef video_class = {
	"Video",
	.finalizer = video_finalizer,
	.gc_mark = video_gc_mark,
};

static int video_argc(JSContext *ctx, int argc, int minimum, int maximum,
	const char *name)
{
	if (argc < minimum || argc > maximum) {
		if (minimum == maximum)
			JS_ThrowTypeError(ctx, "%s expects %d argument%s", name,
				minimum, minimum == 1 ? "" : "s");
		else
			JS_ThrowTypeError(ctx, "%s expects between %d and %d arguments",
				name, minimum, maximum);
		return 0;
	}
	return 1;
}

static VideoHolder *video_holder(JSContext *ctx, JSValueConst value)
{
	VideoHolder *holder = JS_GetOpaque2(ctx, value, video_class_id);

	if (!holder)
		return NULL;
	if (!holder->video) {
		JS_ThrowTypeError(ctx, "Video has already been freed");
		return NULL;
	}
	return holder;
}

static AthenaVideo *video_this(JSContext *ctx, JSValueConst value)
{
	VideoHolder *holder = video_holder(ctx, value);
	return holder ? holder->video : NULL;
}

/* Returns the path argument (free with JS_FreeCString), or NULL after throwing. */
static const char *video_path_arg(JSContext *ctx, JSValueConst value,
	const char *name)
{
	const char *path;

	if (!JS_IsString(value)) {
		JS_ThrowTypeError(ctx, "%s path must be a string", name);
		return NULL;
	}
	path = JS_ToCString(ctx, value);
	if (path && !*path) {
		JS_FreeCString(ctx, path);
		JS_ThrowTypeError(ctx, "%s path must not be empty", name);
		return NULL;
	}
	return path;
}

/* Throws an InternalError with a stable `error.code`, like Sound and Archive. */
static JSValue video_throw(JSContext *ctx, AthenaVideoError error,
	const char *path)
{
	JSValue exception;

	JS_ThrowInternalError(ctx, "Video '%s': %s", path,
		athena_video_error_message(error));
	exception = JS_GetException(ctx);
	JS_SetPropertyStr(ctx, exception, "code",
		JS_NewString(ctx, athena_video_error_name(error)));
	return JS_Throw(ctx, exception);
}

static JSValue video_constructor(JSContext *ctx, JSValueConst new_target,
	int argc, JSValueConst *argv)
{
	AthenaVideoError error;
	VideoHolder *holder;
	JSValue prototype, object;
	const char *path;

	if (!video_argc(ctx, argc, 1, 1, "Video"))
		return JS_EXCEPTION;
	path = video_path_arg(ctx, argv[0], "Video");
	if (!path)
		return JS_EXCEPTION;

	holder = js_mallocz(ctx, sizeof(*holder));
	if (!holder) {
		JS_FreeCString(ctx, path);
		return JS_EXCEPTION;
	}
	holder->frame = JS_UNDEFINED;
	holder->audio = JS_UNDEFINED;

	holder->video = athena_video_create(path, &error);
	if (!holder->video) {
		js_free(ctx, holder);
		video_throw(ctx, error, path);
		JS_FreeCString(ctx, path);
		return JS_EXCEPTION;
	}
	JS_FreeCString(ctx, path);

	prototype = JS_GetPropertyStr(ctx, new_target, "prototype");
	if (JS_IsException(prototype))
		goto fail;
	object = JS_NewObjectProtoClass(ctx, prototype, video_class_id);
	JS_FreeValue(ctx, prototype);
	if (JS_IsException(object))
		goto fail;
	JS_SetOpaque(object, holder);
	return object;

fail:
	athena_video_destroy(holder->video);
	js_free(ctx, holder);
	return JS_EXCEPTION;
}

/* Methods a Sound.Stream has that `audio` relies on. */
static const char *const video_audio_methods[] = { "play", "pause", "stop", "rewind" };

/* Calls audio[name](); returns -1 with the exception pending. */
static int video_audio_call(JSContext *ctx, JSValueConst audio, const char *name)
{
	JSValue method = JS_GetPropertyStr(ctx, audio, name);
	JSValue result;

	if (JS_IsException(method))
		return -1;
	if (!JS_IsFunction(ctx, method)) {
		JS_FreeValue(ctx, method);
		JS_ThrowTypeError(ctx, "Video.audio.%s is not a function", name);
		return -1;
	}
	result = JS_Call(ctx, method, audio, 0, NULL);
	JS_FreeValue(ctx, method);
	if (JS_IsException(result))
		return -1;
	JS_FreeValue(ctx, result);
	return 0;
}

static bool video_has_audio(const VideoHolder *holder)
{
	return !JS_IsUndefined(holder->audio);
}

/* Reads audio.position (ms heard) and audio.ended. */
static int video_audio_clock(JSContext *ctx, JSValueConst audio,
	uint32_t *position, int *ended)
{
	JSValue value = JS_GetPropertyStr(ctx, audio, "position");
	double ms;

	if (JS_IsException(value))
		return -1;
	if (JS_ToFloat64(ctx, &ms, value)) {
		JS_FreeValue(ctx, value);
		return -1;
	}
	JS_FreeValue(ctx, value);
	if (!isfinite(ms) || ms < 0) {
		JS_ThrowRangeError(ctx, "Video.audio.position must be a non-negative number");
		return -1;
	}
	*position = ms > UINT32_MAX ? UINT32_MAX : (uint32_t)ms;

	value = JS_GetPropertyStr(ctx, audio, "ended");
	if (JS_IsException(value))
		return -1;
	*ended = JS_ToBool(ctx, value);
	JS_FreeValue(ctx, value);
	return *ended < 0 ? -1 : 0;
}

static JSValue video_play(JSContext *ctx, JSValueConst this_val, int argc,
	JSValueConst *argv)
{
	VideoHolder *holder = video_holder(ctx, this_val);

	if (!holder || !video_argc(ctx, argc, 0, 0, "Video.play"))
		return JS_EXCEPTION;
	if (video_has_audio(holder) && !athena_video_is_playing(holder->video)) {
		/* The video starts over after the end or stop(): so does the audio. */
		bool from_start = athena_video_is_ended(holder->video) ||
			athena_video_get_state(holder->video) == ATHENA_VIDEO_STOPPED;

		if ((from_start && video_audio_call(ctx, holder->audio, "rewind") < 0) ||
			video_audio_call(ctx, holder->audio, "play") < 0)
			return JS_EXCEPTION;
	}
	athena_video_play(holder->video);
	return JS_UNDEFINED;
}

static JSValue video_pause(JSContext *ctx, JSValueConst this_val, int argc,
	JSValueConst *argv)
{
	VideoHolder *holder = video_holder(ctx, this_val);

	if (!holder || !video_argc(ctx, argc, 0, 0, "Video.pause"))
		return JS_EXCEPTION;
	athena_video_pause(holder->video);
	if (video_has_audio(holder) && video_audio_call(ctx, holder->audio, "pause") < 0)
		return JS_EXCEPTION;
	return JS_UNDEFINED;
}

static JSValue video_stop(JSContext *ctx, JSValueConst this_val, int argc,
	JSValueConst *argv)
{
	VideoHolder *holder = video_holder(ctx, this_val);

	if (!holder || !video_argc(ctx, argc, 0, 0, "Video.stop"))
		return JS_EXCEPTION;
	athena_video_stop(holder->video);
	if (video_has_audio(holder) && video_audio_call(ctx, holder->audio, "stop") < 0)
		return JS_EXCEPTION;
	return JS_UNDEFINED;
}

/*
 * Calls this[name](...args) when it is set. Returns -1 with the exception
 * pending when the handler is not a function or throws.
 */
static int video_call_handler(JSContext *ctx, JSValueConst this_val,
	const char *name, int argc, JSValueConst *argv)
{
	JSValue handler = JS_GetPropertyStr(ctx, this_val, name);
	JSValue result;

	if (JS_IsException(handler))
		return -1;
	if (JS_IsUndefined(handler) || JS_IsNull(handler))
		return 0;
	if (!JS_IsFunction(ctx, handler)) {
		JS_FreeValue(ctx, handler);
		JS_ThrowTypeError(ctx, "Video.%s must be a function", name);
		return -1;
	}
	result = JS_Call(ctx, handler, this_val, argc, argv);
	JS_FreeValue(ctx, handler);
	if (JS_IsException(result))
		return -1;
	JS_FreeValue(ctx, result);
	return 0;
}

static JSValue video_update(JSContext *ctx, JSValueConst this_val, int argc,
	JSValueConst *argv)
{
	VideoHolder *holder = video_holder(ctx, this_val);
	AthenaVideo *video;
	unsigned int events;
	bool changed;
	int loops;

	if (!holder || !video_argc(ctx, argc, 0, 0, "Video.update"))
		return JS_EXCEPTION;
	video = holder->video;
	if (video_has_audio(holder)) {
		uint32_t position;
		int ended;

		if (video_audio_clock(ctx, holder->audio, &position, &ended) < 0)
			return JS_EXCEPTION;
		changed = athena_video_update_synced(video, position, ended != 0);
	} else {
		changed = athena_video_update(video);
	}
	events = athena_video_take_events(video);
	loops = athena_video_get_loop_count(video);

	/* Handlers may free the video: nothing below touches it. */
	if (events & ATHENA_VIDEO_EVENT_LOOP) {
		JSValue count = JS_NewInt32(ctx, loops);
		if (video_call_handler(ctx, this_val, "onLoop", 1, &count) < 0)
			return JS_EXCEPTION;
	}
	if ((events & ATHENA_VIDEO_EVENT_END) &&
		video_call_handler(ctx, this_val, "onEnd", 0, NULL) < 0)
		return JS_EXCEPTION;
	return JS_NewBool(ctx, changed);
}

static int video_option(JSContext *ctx, JSValueConst options, const char *name,
	float *target, bool nonnegative)
{
	JSValue value = JS_GetPropertyStr(ctx, options, name);
	double number;

	if (JS_IsException(value))
		return 0;
	if (JS_IsUndefined(value))
		return 1;
	if (JS_ToFloat64(ctx, &number, value)) {
		JS_FreeValue(ctx, value);
		return 0;
	}
	JS_FreeValue(ctx, value);
	if (!isfinite(number) || (nonnegative && number < 0)) {
		JS_ThrowRangeError(ctx, "Video.draw options.%s must be %s", name,
			nonnegative ? "finite and non-negative" : "finite");
		return 0;
	}
	*target = (float)number;
	return 1;
}

/* draw(x, y, options): destination size, source rectangle, angle, color. */
static JSValue video_draw_options(JSContext *ctx, AthenaVideo *video,
	JSValueConst *argv)
{
	AthenaVideoDrawOptions options;
	double x, y;
	JSValue value;

	if (JS_ToFloat64(ctx, &x, argv[0]) || JS_ToFloat64(ctx, &y, argv[1]))
		return JS_EXCEPTION;
	if (!isfinite(x) || !isfinite(y))
		return JS_ThrowRangeError(ctx, "Video.draw coordinates must be finite");

	athena_video_draw_options_init(&options);
	if (!video_option(ctx, argv[2], "width", &options.width, true) ||
		!video_option(ctx, argv[2], "height", &options.height, true) ||
		!video_option(ctx, argv[2], "startx", &options.startx, true) ||
		!video_option(ctx, argv[2], "starty", &options.starty, true) ||
		!video_option(ctx, argv[2], "endx", &options.endx, true) ||
		!video_option(ctx, argv[2], "endy", &options.endy, true) ||
		!video_option(ctx, argv[2], "angle", &options.angle, false))
		return JS_EXCEPTION;
	value = JS_GetPropertyStr(ctx, argv[2], "color");
	if (JS_IsException(value))
		return JS_EXCEPTION;
	if (!JS_IsUndefined(value) && JS_ToUint32(ctx, &options.color, value)) {
		JS_FreeValue(ctx, value);
		return JS_EXCEPTION;
	}
	JS_FreeValue(ctx, value);

	if (athena_video_is_ready(video) &&
		!athena_video_draw_ex(video, (float)x, (float)y, &options))
		return JS_ThrowRangeError(ctx,
			"Video.draw source rectangle is empty or outside the %dx%d picture",
			athena_video_get_width(video), athena_video_get_height(video));
	return JS_UNDEFINED;
}

static JSValue video_draw(JSContext *ctx, JSValueConst this_val, int argc,
	JSValueConst *argv)
{
	static const char *const names[] = { "x", "y", "width", "height" };
	AthenaVideo *video = video_this(ctx, this_val);
	double values[4] = { 0, 0, 0, 0 };

	if (!video || !video_argc(ctx, argc, 0, 4, "Video.draw"))
		return JS_EXCEPTION;
	if (argc == 3 && JS_IsObject(argv[2])) {
		if (JS_IsArray(ctx, argv[2]) || JS_IsFunction(ctx, argv[2]))
			return JS_ThrowTypeError(ctx, "Video.draw options must be an object");
		return video_draw_options(ctx, video, argv);
	}

	/* draw(x, y, width, height): 0 or omitted sizes use the picture's. */
	for (int i = 0; i < argc; i++) {
		if (JS_ToFloat64(ctx, &values[i], argv[i]))
			return JS_EXCEPTION;
		if (!isfinite(values[i]))
			return JS_ThrowRangeError(ctx, "Video.draw %s must be finite",
				names[i]);
	}
	athena_video_draw(video, (float)values[0], (float)values[1],
		(float)values[2], (float)values[3]);
	return JS_UNDEFINED;
}

static JSValue video_free(JSContext *ctx, JSValueConst this_val, int argc,
	JSValueConst *argv)
{
	VideoHolder *holder = JS_GetOpaque2(ctx, this_val, video_class_id);

	if (!holder)
		return JS_EXCEPTION;
	if (!video_argc(ctx, argc, 0, 0, "Video.free"))
		return JS_EXCEPTION;
	/* The holder stays attached (empty) so later calls throw cleanly. */
	if (holder->video)
		video_release(JS_GetRuntime(ctx), holder);
	return JS_UNDEFINED;
}

enum {
	VIDEO_PROP_WIDTH,
	VIDEO_PROP_HEIGHT,
	VIDEO_PROP_FPS,
	VIDEO_PROP_READY,
	VIDEO_PROP_ENDED,
	VIDEO_PROP_PLAYING,
	VIDEO_PROP_LOOP,
	VIDEO_PROP_CURRENT_FRAME,
	VIDEO_PROP_LOOP_COUNT,
};

static JSValue video_get(JSContext *ctx, JSValueConst this_val, int magic)
{
	AthenaVideo *video = video_this(ctx, this_val);

	if (!video)
		return JS_EXCEPTION;
	switch (magic) {
	case VIDEO_PROP_WIDTH:
		return JS_NewInt32(ctx, athena_video_get_width(video));
	case VIDEO_PROP_HEIGHT:
		return JS_NewInt32(ctx, athena_video_get_height(video));
	case VIDEO_PROP_FPS:
		return JS_NewFloat64(ctx, athena_video_get_fps(video));
	case VIDEO_PROP_READY:
		return JS_NewBool(ctx, athena_video_is_ready(video));
	case VIDEO_PROP_ENDED:
		return JS_NewBool(ctx, athena_video_is_ended(video));
	case VIDEO_PROP_PLAYING:
		return JS_NewBool(ctx, athena_video_is_playing(video));
	case VIDEO_PROP_LOOP:
		return JS_NewBool(ctx, athena_video_get_loop(video));
	case VIDEO_PROP_CURRENT_FRAME:
		return JS_NewInt32(ctx, athena_video_get_current_frame(video));
	case VIDEO_PROP_LOOP_COUNT:
		return JS_NewInt32(ctx, athena_video_get_loop_count(video));
	}
	return JS_UNDEFINED;
}

static JSValue video_get_loop(JSContext *ctx, JSValueConst this_val)
{
	return video_get(ctx, this_val, VIDEO_PROP_LOOP);
}

static JSValue video_set_loop(JSContext *ctx, JSValueConst this_val,
	JSValueConst value)
{
	VideoHolder *holder = video_holder(ctx, this_val);
	int loop;

	if (!holder)
		return JS_EXCEPTION;
	loop = JS_ToBool(ctx, value);
	if (loop < 0)
		return JS_EXCEPTION;
	/* Synced, the audio loops and the video follows its wraps. */
	if (video_has_audio(holder) &&
		JS_SetPropertyStr(ctx, holder->audio, "loop", JS_NewBool(ctx, loop)) < 0)
		return JS_EXCEPTION;
	athena_video_set_loop(holder->video, loop != 0);
	return JS_UNDEFINED;
}

static JSValue video_get_audio(JSContext *ctx, JSValueConst this_val)
{
	VideoHolder *holder = video_holder(ctx, this_val);

	if (!holder)
		return JS_EXCEPTION;
	return video_has_audio(holder) ? JS_DupValue(ctx, holder->audio) : JS_NULL;
}

/*
 * video.audio = stream: playback follows the stream's position, and play,
 * pause, stop and loop drive both. Any object with the Sound.Stream members
 * used here works (the module does not depend on Sound). null detaches.
 */
static JSValue video_set_audio(JSContext *ctx, JSValueConst this_val,
	JSValueConst value)
{
	VideoHolder *holder = video_holder(ctx, this_val);

	if (!holder)
		return JS_EXCEPTION;
	if (athena_video_is_playing(holder->video))
		return JS_ThrowTypeError(ctx,
			"Video.audio can only be changed while the video is not playing");

	if (JS_IsUndefined(value) || JS_IsNull(value)) {
		JS_FreeValue(ctx, holder->audio);
		holder->audio = JS_UNDEFINED;
		athena_video_set_synced(holder->video, false);
		return JS_UNDEFINED;
	}
	if (!JS_IsObject(value))
		return JS_ThrowTypeError(ctx, "Video.audio must be a Sound.Stream or null");
	for (size_t i = 0; i < countof(video_audio_methods); i++) {
		JSValue method = JS_GetPropertyStr(ctx, value, video_audio_methods[i]);
		bool callable;

		if (JS_IsException(method))
			return JS_EXCEPTION;
		callable = JS_IsFunction(ctx, method);
		JS_FreeValue(ctx, method);
		if (!callable)
			return JS_ThrowTypeError(ctx,
				"Video.audio must be a Sound.Stream or null (no %s())",
				video_audio_methods[i]);
	}
	if (JS_SetPropertyStr(ctx, value, "loop",
		JS_NewBool(ctx, athena_video_get_loop(holder->video))) < 0)
		return JS_EXCEPTION;

	JS_FreeValue(ctx, holder->audio);
	holder->audio = JS_DupValue(ctx, value);
	athena_video_set_synced(holder->video, true);
	return JS_UNDEFINED;
}

/*
 * The same Image is returned on every access; it follows the decoded frames
 * because it draws the video's own surface.
 */
static JSValue video_get_frame(JSContext *ctx, JSValueConst this_val)
{
	VideoHolder *holder = video_holder(ctx, this_val);
	GSSURFACE *surface;
	AthenaImage *image;
	JSValue object;

	if (!holder)
		return JS_EXCEPTION;
	if (athena_image_peek(holder->frame))
		return JS_DupValue(ctx, holder->frame);

	surface = athena_video_get_texture(holder->video);
	if (!surface)
		return JS_NULL;

	image = athena_image_wrap(surface, false);
	if (!image)
		return JS_ThrowOutOfMemory(ctx);
	/* Show the picture only, not the macroblock padding of the texture. */
	image->width = image->endx = (float)athena_video_get_width(holder->video);
	image->height = image->endy = (float)athena_video_get_height(holder->video);
	object = athena_image_to_value(ctx, image);
	if (JS_IsException(object)) {
		athena_image_destroy(image);
		return object;
	}

	/* A previous frame Image freed by the script is replaced. */
	JS_FreeValue(ctx, holder->frame);
	holder->frame = JS_DupValue(ctx, object);
	return object;
}

static const JSCFunctionListEntry video_proto_funcs[] = {
	JS_CFUNC_DEF("play", 0, video_play),
	JS_CFUNC_DEF("pause", 0, video_pause),
	JS_CFUNC_DEF("stop", 0, video_stop),
	JS_CFUNC_DEF("update", 0, video_update),
	JS_CFUNC_DEF("draw", 4, video_draw),
	JS_CFUNC_DEF("free", 0, video_free),
	JS_CGETSET_MAGIC_DEF("width", video_get, NULL, VIDEO_PROP_WIDTH),
	JS_CGETSET_MAGIC_DEF("height", video_get, NULL, VIDEO_PROP_HEIGHT),
	JS_CGETSET_MAGIC_DEF("fps", video_get, NULL, VIDEO_PROP_FPS),
	JS_CGETSET_MAGIC_DEF("ready", video_get, NULL, VIDEO_PROP_READY),
	JS_CGETSET_MAGIC_DEF("ended", video_get, NULL, VIDEO_PROP_ENDED),
	JS_CGETSET_MAGIC_DEF("playing", video_get, NULL, VIDEO_PROP_PLAYING),
	JS_CGETSET_MAGIC_DEF("currentFrame", video_get, NULL,
		VIDEO_PROP_CURRENT_FRAME),
	JS_CGETSET_MAGIC_DEF("loopCount", video_get, NULL, VIDEO_PROP_LOOP_COUNT),
	JS_CGETSET_DEF("loop", video_get_loop, video_set_loop),
	JS_CGETSET_DEF("frame", video_get_frame, NULL),
	JS_CGETSET_DEF("audio", video_get_audio, video_set_audio),
};

static const char *video_chroma_name(int format)
{
	switch (format) {
	case 1: return "4:2:0";
	case 2: return "4:2:2";
	case 3: return "4:4:4";
	}
	return "unknown";
}

/* Video.probe(path): stream information without opening the decoder. */
static JSValue video_probe(JSContext *ctx, JSValueConst this_val, int argc,
	JSValueConst *argv)
{
	AthenaVideoInfo info;
	AthenaVideoError error;
	const char *path;
	JSValue result;

	if (!video_argc(ctx, argc, 1, 1, "Video.probe"))
		return JS_EXCEPTION;
	path = video_path_arg(ctx, argv[0], "Video.probe");
	if (!path)
		return JS_EXCEPTION;
	error = athena_video_probe(path, &info);
	if (error != ATHENA_VIDEO_OK) {
		video_throw(ctx, error, path);
		JS_FreeCString(ctx, path);
		return JS_EXCEPTION;
	}
	JS_FreeCString(ctx, path);

	result = JS_NewObject(ctx);
	if (JS_IsException(result))
		return result;
	JS_SetPropertyStr(ctx, result, "width", JS_NewInt32(ctx, info.width));
	JS_SetPropertyStr(ctx, result, "height", JS_NewInt32(ctx, info.height));
	JS_SetPropertyStr(ctx, result, "codedWidth", JS_NewInt32(ctx, info.coded_width));
	JS_SetPropertyStr(ctx, result, "codedHeight", JS_NewInt32(ctx, info.coded_height));
	JS_SetPropertyStr(ctx, result, "fps", JS_NewFloat64(ctx, info.fps));
	JS_SetPropertyStr(ctx, result, "frames", JS_NewInt32(ctx, info.frames));
	JS_SetPropertyStr(ctx, result, "duration", JS_NewFloat64(ctx, info.duration));
	JS_SetPropertyStr(ctx, result, "mpeg2", JS_NewBool(ctx, info.mpeg2));
	JS_SetPropertyStr(ctx, result, "progressive", JS_NewBool(ctx, info.progressive));
	JS_SetPropertyStr(ctx, result, "chroma",
		JS_NewString(ctx, video_chroma_name(info.chroma_format)));
	JS_SetPropertyStr(ctx, result, "supported", JS_NewBool(ctx, info.supported));
	return result;
}

static const JSCFunctionListEntry video_static_funcs[] = {
	JS_CFUNC_DEF("probe", 1, video_probe),
};

static int video_module_init(JSContext *ctx, JSModuleDef *module)
{
	JSValue prototype, constructor;

	if (athena_register_class(ctx, &video_class_id, &video_class) < 0)
		return -1;

	prototype = JS_NewObject(ctx);
	if (JS_IsException(prototype))
		return -1;
	JS_SetPropertyFunctionList(ctx, prototype, video_proto_funcs,
		countof(video_proto_funcs));
	constructor = JS_NewCFunction2(ctx, video_constructor, "Video", 1,
		JS_CFUNC_constructor, 0);
	JS_SetConstructor(ctx, constructor, prototype);
	JS_SetClassProto(ctx, video_class_id, prototype);
	JS_SetPropertyFunctionList(ctx, constructor, video_static_funcs,
		countof(video_static_funcs));
	return JS_SetModuleExport(ctx, module, "Video", constructor);
}

JSModuleDef *athena_video_init(JSContext *ctx)
{
	JSModuleDef *module = JS_NewCModule(ctx, "Video", video_module_init);

	if (!module)
		return NULL;
	JS_AddModuleExport(ctx, module, "Video");
	return module;
}
