#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <ath_env.h>
#include <athena/graphics.h>

#include <athena/image.h>
#include <athena/js/image.h>

static JSClassID image_class_id;

static AthenaImage *image_this(JSContext *ctx, JSValueConst value)
{
	return JS_GetOpaque2(ctx, value, image_class_id);
}

AthenaImage *athena_image_from_value(JSContext *ctx, JSValueConst value)
{
	return image_this(ctx, value);
}

AthenaImage *athena_image_peek(JSValueConst value)
{
	return JS_GetOpaque(value, image_class_id);
}

JSValue athena_image_to_value(JSContext *ctx, AthenaImage *image)
{
	JSValue object = JS_NewObjectClass(ctx, image_class_id);
	if (JS_IsException(object))
		return object;
	JS_SetOpaque(object, image);
	return object;
}

static int image_argc(JSContext *ctx, int argc, int minimum, int maximum,
	const char *name)
{
	if (argc < minimum || argc > maximum) {
		if (minimum == maximum)
			JS_ThrowTypeError(ctx, "%s expects exactly %d arguments",
				name, minimum);
		else
			JS_ThrowTypeError(ctx, "%s expects between %d and %d arguments",
				name, minimum, maximum);
		return 0;
	}
	return 1;
}

static uint32_t image_size(const AthenaImage *image)
{
	if (!image || !image->surface || !image->surface->Width ||
		!image->surface->Height)
		return 0;
	{
		uint32_t size = athena_surface_size(image->surface->Width,
			image->surface->Height, image->surface->PSM);
		return size == UINT32_MAX ? 0 : size;
	}
}

static void image_update_ready(AthenaImage *image)
{
	uint32_t size;

	if (!image || !image->surface)
		return;

	size = image_size(image);
	image->loaded = image->surface->Mem != NULL &&
		image->surface->Width > 0 &&
		image->surface->Height > 0 &&
		size > 0 &&
		size != UINT32_MAX &&
		(image->surface->PSM != GS_PSM_T4 &&
		 image->surface->PSM != GS_PSM_T8 ||
		 image->surface->Clut != NULL);
}

/*
 * A borrowed Image (athena_image_wrap, e.g. Video.frame) draws a surface its
 * owner keeps writing to and frees. Its storage must not be replaced here.
 */
static int image_check_owned(JSContext *ctx, const AthenaImage *image)
{
	if (!image->owns_surface) {
		JS_ThrowTypeError(ctx, "cannot change the storage of a borrowed Image");
		return 0;
	}
	return 1;
}

static int image_invalidate_storage(JSContext *ctx, AthenaImage *image)
{
	if (!image || !image->surface)
		return 0;
	if (!image_check_owned(ctx, image))
		return 0;
	if (graphics_surface_is_locked(image->surface)) {
		JS_ThrowTypeError(ctx, "cannot change a locked Image");
		return 0;
	}

	graphics_surface_release(image->surface);
	free(image->surface->Mem);
	free(image->surface->Clut);
	image->surface->Mem = NULL;
	image->surface->Clut = NULL;
	image->surface->Vram = 0;
	image->surface->VramClut = 0;
	image->loaded = false;
	return 1;
}

static int image_float_valid(float value, int nonnegative)
{
	return isfinite(value) && (!nonnegative || value >= 0.0f);
}

static int image_set_buffer(JSContext *ctx, AthenaImage *image,
	JSValueConst value, uint32_t **destination, uint32_t size,
	const char *name)
{
	size_t source_size;
	uint8_t *source = JS_GetArrayBuffer(ctx, &source_size, value);
	uint32_t *copy;

	if (!image_check_owned(ctx, image))
		return 0;
	if (!source)
		return 0;
	if (source_size != size) {
		JS_ThrowRangeError(ctx, "%s must contain exactly %lu bytes", name,
			(unsigned long)size);
		return 0;
	}

	copy = malloc(size);
	if (!copy) {
		JS_ThrowOutOfMemory(ctx);
		return 0;
	}
	memcpy(copy, source, size);
	free(*destination);
	*destination = copy;
	return 1;
}

static void image_finalizer(JSRuntime *rt, JSValue value)
{
	AthenaImage *image = JS_GetOpaque(value, image_class_id);
	if (image) {
		athena_image_destroy(image);
		JS_SetOpaque(value, NULL);
	}
}

static int image_parse_options(JSContext *ctx, JSValueConst value,
	bool *delayed)
{
	JSValue delayed_value;

	if (!JS_IsObject(value) || JS_IsArray(ctx, value))
		return JS_ThrowTypeError(ctx, "Image options must be an object");

	delayed_value = JS_GetPropertyStr(ctx, value, "delayed");
	if (JS_IsException(delayed_value))
		return 0;
	if (!JS_IsUndefined(delayed_value)) {
		if (!JS_IsBool(delayed_value)) {
			JS_FreeValue(ctx, delayed_value);
			JS_ThrowTypeError(ctx, "Image options.delayed must be a boolean");
			return 0;
		}
		*delayed = JS_ToBool(ctx, delayed_value) != 0;
	}
	JS_FreeValue(ctx, delayed_value);
	return 1;
}

static JSValue image_constructor(JSContext *ctx, JSValueConst new_target,
	int argc, JSValueConst *argv)
{
	AthenaImage *image;
	JSValue prototype, object;
	const char *path = NULL;
	bool delayed = true;
	int options_index = -1;

	if (!image_argc(ctx, argc, 0, 2, "Image"))
		return JS_EXCEPTION;
	if (argc >= 1 && JS_IsObject(argv[0]) && !JS_IsArray(ctx, argv[0]))
		options_index = 0;
	else if (argc >= 1 && !JS_IsUndefined(argv[0])) {
		path = JS_ToCString(ctx, argv[0]);
		if (!path)
			return JS_EXCEPTION;
	}
	if (argc == 2) {
		if (options_index != -1) {
			if (!JS_IsUndefined(argv[1]))
				return JS_ThrowTypeError(ctx,
					"Image accepts either options or path plus options");
		} else {
			options_index = 1;
		}
	}
	if (options_index != -1 &&
		!image_parse_options(ctx, argv[options_index], &delayed)) {
		if (path)
			JS_FreeCString(ctx, path);
		return JS_EXCEPTION;
	}

	image = path ? athena_image_create(path, delayed) :
		athena_image_create_empty(delayed);
	if (path)
		JS_FreeCString(ctx, path);
	if (!image)
		return JS_ThrowInternalError(ctx, "failed to create Image");

	prototype = JS_GetPropertyStr(ctx, new_target, "prototype");
	if (JS_IsException(prototype)) {
		athena_image_destroy(image);
		return JS_EXCEPTION;
	}
	object = JS_NewObjectProtoClass(ctx, prototype, image_class_id);
	JS_FreeValue(ctx, prototype);
	if (JS_IsException(object)) {
		athena_image_destroy(image);
		return JS_EXCEPTION;
	}
	JS_SetOpaque(object, image);
	return object;
}

static JSValue image_ready(JSContext *ctx, JSValueConst this_val,
	int argc, JSValueConst *argv)
{
	return JS_NewBool(ctx, athena_image_is_loaded(image_this(ctx, this_val)));
}

static JSValue image_loading(JSContext *ctx, JSValueConst this_val,
	int argc, JSValueConst *argv)
{
	AthenaImage *image = image_this(ctx, this_val);
	return JS_NewBool(ctx, image && image->loading);
}

static JSValue image_failed(JSContext *ctx, JSValueConst this_val,
	int argc, JSValueConst *argv)
{
	AthenaImage *image = image_this(ctx, this_val);
	return JS_NewBool(ctx, image && image->failed);
}

static JSValue image_status(JSContext *ctx, JSValueConst this_val,
	int argc, JSValueConst *argv)
{
	AthenaImage *image = image_this(ctx, this_val);

	if (!image)
		return JS_EXCEPTION;
	athena_image_refresh_status(image);
	switch (image->status) {
	case ATHENA_IMAGE_STATUS_LOADING:
		return JS_NewString(ctx, "loading");
	case ATHENA_IMAGE_STATUS_READY:
		return JS_NewString(ctx, "ready");
	case ATHENA_IMAGE_STATUS_DECODED:
		return JS_NewString(ctx, "decoded");
	case ATHENA_IMAGE_STATUS_UPLOAD_PENDING:
		return JS_NewString(ctx, "upload_pending");
	case ATHENA_IMAGE_STATUS_FAILED:
		return JS_NewString(ctx, "failed");
	case ATHENA_IMAGE_STATUS_CANCELLED:
		return JS_NewString(ctx, "cancelled");
	case ATHENA_IMAGE_STATUS_QUEUED:
	default:
		return JS_NewString(ctx, "queued");
	}
}

JSValue athena_image_error_value(JSContext *ctx, const AthenaImage *image,
	const char *path)
{
	JSValue error = JS_NewObject(ctx);

	if (JS_IsException(error))
		return error;
	if (path)
		JS_SetPropertyStr(ctx, error, "path", JS_NewString(ctx, path));
	JS_SetPropertyStr(ctx, error, "code",
		JS_NewString(ctx, athena_image_error_code_name(image->error_code)));
	JS_SetPropertyStr(ctx, error, "stage",
		JS_NewString(ctx, image->error_stage));
	JS_SetPropertyStr(ctx, error, "message",
		JS_NewString(ctx, image->error_message));
	return error;
}

static JSValue image_error(JSContext *ctx, JSValueConst this_val,
	int argc, JSValueConst *argv)
{
	AthenaImage *image = image_this(ctx, this_val);

	if (!image)
		return JS_EXCEPTION;
	if (!image->failed)
		return JS_UNDEFINED;
	return athena_image_error_value(ctx, image, image->path);
}

static JSValue image_free(JSContext *ctx, JSValueConst this_val,
	int argc, JSValueConst *argv)
{
	AthenaImage *image = image_this(ctx, this_val);
	if (image) {
		athena_image_destroy(image);
		JS_SetOpaque(this_val, NULL);
	}
	return JS_UNDEFINED;
}

static JSValue image_draw(JSContext *ctx, JSValueConst this_val, int argc,
	JSValueConst *argv)
{
	AthenaImage *image = image_this(ctx, this_val);
	float x, y, width, height, startx, starty, endx, endy, angle;
	uint32_t color;

	if (!image_argc(ctx, argc, 2, 3, "Image.draw"))
		return JS_EXCEPTION;
	if (!athena_image_is_loaded(image))
		return JS_ThrowInternalError(ctx, "image is not loaded");
	if (JS_ToFloat32(ctx, &x, argv[0]) || JS_ToFloat32(ctx, &y, argv[1]))
		return JS_EXCEPTION;
	if (!image_float_valid(x, 0) || !image_float_valid(y, 0))
		return JS_ThrowRangeError(ctx, "Image.draw coordinates must be finite");

	width = image->width;
	height = image->height;
	startx = image->startx;
	starty = image->starty;
	endx = image->endx;
	endy = image->endy;
	angle = image->angle;
	color = image->color;

	if (argc == 3) {
		JSValue value;
		if (!JS_IsObject(argv[2]) || JS_IsArray(ctx, argv[2]))
			return JS_ThrowTypeError(ctx, "Image.draw options must be an object");
#define IMAGE_OPTION_FLOAT(name, target) \
		value = JS_GetPropertyStr(ctx, argv[2], name); \
		if (JS_IsException(value)) return JS_EXCEPTION; \
		if (!JS_IsUndefined(value) && JS_ToFloat32(ctx, &(target), value)) { \
			JS_FreeValue(ctx, value); return JS_EXCEPTION; \
		} \
		JS_FreeValue(ctx, value)
		IMAGE_OPTION_FLOAT("width", width);
		IMAGE_OPTION_FLOAT("height", height);
		IMAGE_OPTION_FLOAT("startx", startx);
		IMAGE_OPTION_FLOAT("starty", starty);
		IMAGE_OPTION_FLOAT("endx", endx);
		IMAGE_OPTION_FLOAT("endy", endy);
		IMAGE_OPTION_FLOAT("angle", angle);
#undef IMAGE_OPTION_FLOAT
		value = JS_GetPropertyStr(ctx, argv[2], "color");
		if (JS_IsException(value))
			return JS_EXCEPTION;
		if (!JS_IsUndefined(value) && JS_ToUint32(ctx, &color, value)) {
			JS_FreeValue(ctx, value);
			return JS_EXCEPTION;
		}
		JS_FreeValue(ctx, value);
	}

	if (!image_float_valid(width, 1) || !image_float_valid(height, 1) ||
		!image_float_valid(startx, 1) || !image_float_valid(starty, 1) ||
		!image_float_valid(endx, 1) || !image_float_valid(endy, 1) ||
		!image_float_valid(angle, 0))
		return JS_ThrowRangeError(ctx, "Image.draw options must be finite and non-negative");
	if (endx < startx || endy < starty ||
		endx > image->surface->Width || endy > image->surface->Height)
		return JS_ThrowRangeError(ctx, "Image.draw source rectangle is outside the image");

	athena_image_draw(image, x, y, width, height, startx, starty, endx, endy,
		angle, color);
	return JS_UNDEFINED;
}

static JSValue image_bool_method(JSContext *ctx, JSValueConst this_val,
	int magic)
{
	AthenaImage *image = image_this(ctx, this_val);
	switch (magic) {
	case 0: return JS_NewBool(ctx, athena_image_lock(image));
	case 1: return JS_NewBool(ctx, athena_image_unlock(image));
	case 2: return JS_NewBool(ctx, athena_image_locked(image));
	case 3: return JS_NewBool(ctx, athena_image_optimize(image));
	default: return JS_EXCEPTION;
	}
}

static JSValue image_lock(JSContext *ctx, JSValueConst this_val, int argc,
	JSValueConst *argv) { return image_bool_method(ctx, this_val, 0); }
static JSValue image_unlock(JSContext *ctx, JSValueConst this_val, int argc,
	JSValueConst *argv) { return image_bool_method(ctx, this_val, 1); }
static JSValue image_locked(JSContext *ctx, JSValueConst this_val, int argc,
	JSValueConst *argv) { return image_bool_method(ctx, this_val, 2); }
static JSValue image_optimize(JSContext *ctx, JSValueConst this_val, int argc,
	JSValueConst *argv) { return image_bool_method(ctx, this_val, 3); }

static JSValue image_get(JSContext *ctx, JSValueConst this_val, int magic)
{
	AthenaImage *image = image_this(ctx, this_val);
	if (!image)
		return JS_EXCEPTION;
	switch (magic) {
	case 0: return JS_NewFloat64(ctx, image->width);
	case 1: return JS_NewFloat64(ctx, image->height);
	case 2: return JS_NewFloat64(ctx, image->startx);
	case 3: return JS_NewFloat64(ctx, image->starty);
	case 4: return JS_NewFloat64(ctx, image->endx);
	case 5: return JS_NewFloat64(ctx, image->endy);
	case 6: return JS_NewFloat64(ctx, image->angle);
	}
	return JS_UNDEFINED;
}

static JSValue image_set(JSContext *ctx, JSValueConst this_val, JSValue value,
	int magic)
{
	AthenaImage *image = image_this(ctx, this_val);
	float number;
	if (!image || JS_ToFloat32(ctx, &number, value))
		return JS_EXCEPTION;
	if (!image_float_valid(number, magic >= 0 && magic <= 5))
		return JS_ThrowRangeError(ctx, "Image property must be finite and non-negative");
	switch (magic) {
	case 0: image->width = number; break;
	case 1: image->height = number; break;
	case 2: image->startx = number; break;
	case 3: image->starty = number; break;
	case 4: image->endx = number; break;
	case 5: image->endy = number; break;
	case 6: image->angle = number; break;
	}
	return JS_UNDEFINED;
}

static JSValue image_get_uint(JSContext *ctx, JSValueConst this_val, int magic)
{
	AthenaImage *image = image_this(ctx, this_val);
	GSSURFACE *surface;
	uint32_t size;
	if (!image || !image->surface)
		return JS_EXCEPTION;
	surface = image->surface;
	switch (magic) {
	case 0: return JS_NewUint32(ctx, image->color);
	case 1: return JS_NewUint32(ctx, surface->Filter);
	case 2: return JS_NewUint32(ctx, image_size(image));
	case 3:
		switch (surface->PSM) {
		case GS_PSM_T4: return JS_NewUint32(ctx, 4);
		case GS_PSM_T8: return JS_NewUint32(ctx, 8);
		case GS_PSM_CT16:
		case GS_PSM_CT16S: return JS_NewUint32(ctx, 16);
		case GS_PSM_CT24: return JS_NewUint32(ctx, 24);
		case GS_PSM_CT32: return JS_NewUint32(ctx, 32);
		default: return JS_NewUint32(ctx, 0);
		}
	case 4: return JS_NewBool(ctx, image->delayed);
	case 5:
		size = image_size(image);
		return size && surface->Mem ? JS_NewArrayBufferCopy(ctx,
			(const uint8_t *)surface->Mem, size) : JS_UNDEFINED;
	case 6:
		size = surface->PSM == GS_PSM_T4 ?
			athena_surface_size(8, 2, GS_PSM_CT32) :
			(surface->PSM == GS_PSM_T8 ?
				athena_surface_size(16, 16, GS_PSM_CT32) : 0);
		return size && surface->Clut ? JS_NewArrayBufferCopy(ctx,
			(const uint8_t *)surface->Clut, size) : JS_UNDEFINED;
	case 7: return JS_NewUint32(ctx, surface->Width);
	case 8: return JS_NewUint32(ctx, surface->Height);
	case 9: return JS_NewBool(ctx, surface->PageAligned);
	}
	return JS_UNDEFINED;
}

static JSValue image_set_uint(JSContext *ctx, JSValueConst this_val,
	JSValue value, int magic)
{
	AthenaImage *image = image_this(ctx, this_val);
	GSSURFACE *surface;
	uint32_t number;
	if (!image || !image->surface || JS_ToUint32(ctx, &number, value))
		return JS_EXCEPTION;
	surface = image->surface;
	switch (magic) {
	case 0: image->color = number; break;
	case 1:
		if (number != GS_FILTER_NEAREST && number != GS_FILTER_LINEAR)
			return JS_ThrowRangeError(ctx,
				"Image.filter must be GS_FILTER_NEAREST or GS_FILTER_LINEAR");
		surface->Filter = number;
		break;
	case 3:
		if (number != 4 && number != 8 && number != 16 &&
			number != 24 && number != 32)
			return JS_ThrowRangeError(ctx, "unsupported Image.bpp");
		if ((number == 4 && surface->PSM == GS_PSM_T4) ||
			(number == 8 && surface->PSM == GS_PSM_T8) ||
			(number == 16 && (surface->PSM == GS_PSM_CT16 ||
				surface->PSM == GS_PSM_CT16S)) ||
			(number == 24 && surface->PSM == GS_PSM_CT24) ||
			(number == 32 && surface->PSM == GS_PSM_CT32))
			break;
		if (!image_invalidate_storage(ctx, image))
			return JS_EXCEPTION;
		switch (number) {
		case 4: surface->PSM = GS_PSM_T4; break;
		case 8: surface->PSM = GS_PSM_T8; break;
		case 16: surface->PSM = GS_PSM_CT16S; break;
		case 24: surface->PSM = GS_PSM_CT24; break;
		case 32: surface->PSM = GS_PSM_CT32; break;
		}
		break;
	case 5:
		if (graphics_surface_is_locked(surface)) {
			JS_ThrowTypeError(ctx, "cannot change a locked Image");
			return JS_EXCEPTION;
		}
		if (!image_set_buffer(ctx, image, value, &surface->Mem,
			image_size(image), "Image.pixels"))
			return JS_EXCEPTION;
		graphics_surface_release(surface);
		image_update_ready(image);
		break;
	case 6: {
		if (surface->PSM != GS_PSM_T4 && surface->PSM != GS_PSM_T8)
			return JS_ThrowTypeError(ctx,
				"Image.palette is only valid for indexed images");
		if (graphics_surface_is_locked(surface)) {
			JS_ThrowTypeError(ctx, "cannot change a locked Image");
			return JS_EXCEPTION;
		}
		uint32_t size = surface->PSM == GS_PSM_T4 ?
			athena_surface_size(8, 2, GS_PSM_CT32) :
			(surface->PSM == GS_PSM_T8 ?
				athena_surface_size(16, 16, GS_PSM_CT32) : 0);
		if (!size || !image_set_buffer(ctx, image, value, &surface->Clut,
			size, "Image.palette"))
			return JS_EXCEPTION;
		graphics_surface_release(surface);
		image_update_ready(image);
		break;
	}
	case 7:
		if (number < 1 || number > 1024)
			return JS_ThrowRangeError(ctx, "Image.texWidth must be between 1 and 1024");
		if (number != surface->Width && !image_invalidate_storage(ctx, image))
			return JS_EXCEPTION;
		surface->Width = number;
		image_update_ready(image);
		break;
	case 8:
		if (number < 1 || number > 1024)
			return JS_ThrowRangeError(ctx, "Image.texHeight must be between 1 and 1024");
		if (number != surface->Height && !image_invalidate_storage(ctx, image))
			return JS_EXCEPTION;
		surface->Height = number;
		image_update_ready(image);
		break;
	case 9: surface->PageAligned = number != 0; break;
	default: break;
	}
	return JS_UNDEFINED;
}

static JSValue image_copy_vram_block(JSContext *ctx, JSValueConst this_val,
	int argc, JSValueConst *argv)
{
	AthenaImage *source, *destination;
	int32_t source_x, source_y, destination_x, destination_y;
	if (!image_argc(ctx, argc, 6, 6, "Image.copyVRAMBlock"))
		return JS_EXCEPTION;
	source = image_this(ctx, argv[0]);
	destination = image_this(ctx, argv[3]);
	if (!source || !destination ||
		JS_ToInt32(ctx, &source_x, argv[1]) ||
		JS_ToInt32(ctx, &source_y, argv[2]) ||
		JS_ToInt32(ctx, &destination_x, argv[4]) ||
		JS_ToInt32(ctx, &destination_y, argv[5]))
		return JS_EXCEPTION;
	if (source_x < 0 || source_y < 0 || destination_x < 0 || destination_y < 0)
		return JS_ThrowRangeError(ctx, "Image.copyVRAMBlock coordinates must be non-negative");
	if (!athena_image_copy_vram_block(source, source_x, source_y,
		destination, destination_x, destination_y))
		return JS_ThrowInternalError(ctx,
			"failed to copy Image VRAM block; images must be resident and coordinates must fit");
	return JS_UNDEFINED;
}

static JSClassDef image_class = {
	"Image",
	.finalizer = image_finalizer,
};

static const JSCFunctionListEntry image_proto_funcs[] = {
	JS_CFUNC_DEF("draw", 2, image_draw),
	JS_CFUNC_DEF("ready", 0, image_ready),
	JS_CFUNC_DEF("loading", 0, image_loading),
	JS_CFUNC_DEF("failed", 0, image_failed),
	JS_CFUNC_DEF("status", 0, image_status),
	JS_CFUNC_DEF("error", 0, image_error),
	JS_CFUNC_DEF("free", 0, image_free),
	JS_CFUNC_DEF("lock", 0, image_lock),
	JS_CFUNC_DEF("unlock", 0, image_unlock),
	JS_CFUNC_DEF("locked", 0, image_locked),
	JS_CFUNC_DEF("optimize", 0, image_optimize),
	JS_CGETSET_MAGIC_DEF("width", image_get, image_set, 0),
	JS_CGETSET_MAGIC_DEF("height", image_get, image_set, 1),
	JS_CGETSET_MAGIC_DEF("startx", image_get, image_set, 2),
	JS_CGETSET_MAGIC_DEF("starty", image_get, image_set, 3),
	JS_CGETSET_MAGIC_DEF("endx", image_get, image_set, 4),
	JS_CGETSET_MAGIC_DEF("endy", image_get, image_set, 5),
	JS_CGETSET_MAGIC_DEF("angle", image_get, image_set, 6),
	JS_CGETSET_MAGIC_DEF("color", image_get_uint, image_set_uint, 0),
	JS_CGETSET_MAGIC_DEF("filter", image_get_uint, image_set_uint, 1),
	JS_CGETSET_MAGIC_DEF("size", image_get_uint, image_set_uint, 2),
	JS_CGETSET_MAGIC_DEF("bpp", image_get_uint, image_set_uint, 3),
	JS_CGETSET_MAGIC_DEF("pixels", image_get_uint, image_set_uint, 5),
	JS_CGETSET_MAGIC_DEF("palette", image_get_uint, image_set_uint, 6),
	JS_CGETSET_MAGIC_DEF("texWidth", image_get_uint, image_set_uint, 7),
	JS_CGETSET_MAGIC_DEF("texHeight", image_get_uint, image_set_uint, 8),
	JS_CGETSET_MAGIC_DEF("renderable", image_get_uint, image_set_uint, 9),
};

static const JSCFunctionListEntry image_static_funcs[] = {
	JS_CFUNC_DEF("copyVRAMBlock", 6, image_copy_vram_block),
};

static int image_module_init(JSContext *ctx, JSModuleDef *module)
{
	JSValue prototype, constructor;
	JS_NewClassID(&image_class_id);
	JS_NewClass(JS_GetRuntime(ctx), image_class_id, &image_class);
	prototype = JS_NewObject(ctx);
	JS_SetPropertyFunctionList(ctx, prototype, image_proto_funcs,
		countof(image_proto_funcs));
	constructor = JS_NewCFunction2(ctx, image_constructor, "Image", 2,
		JS_CFUNC_constructor, 0);
	JS_SetConstructor(ctx, constructor, prototype);
	JS_SetClassProto(ctx, image_class_id, prototype);
	JS_SetPropertyFunctionList(ctx, constructor, image_static_funcs,
		countof(image_static_funcs));
	JS_SetModuleExport(ctx, module, "Image", constructor);
	return 0;
}

JSModuleDef *athena_image_init(JSContext *ctx)
{
	JSModuleDef *module = JS_NewCModule(ctx, "Image", image_module_init);
	if (!module)
		return NULL;
	JS_AddModuleExport(ctx, module, "Image");
	return module;
}
