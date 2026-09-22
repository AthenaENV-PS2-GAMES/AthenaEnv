#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <ath_env.h>
#include <graphics.h>

#include "../native/image.h"
#include "ath_image.h"

static JSClassID image_class_id;

static AthenaImage *image_this(JSContext *ctx, JSValueConst value)
{
	return JS_GetOpaque2(ctx, value, image_class_id);
}

static int image_argc(JSContext *ctx, int argc, int minimum, int maximum,
	const char *name)
{
	if (argc < minimum || argc > maximum) {
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
	if (image && image->surface && image->surface->Mem &&
		image->surface->Width && image->surface->Height)
		image->loaded = true;
}

static int image_set_buffer(JSContext *ctx, AthenaImage *image,
	JSValueConst value, uint32_t **destination, uint32_t size,
	const char *name)
{
	size_t source_size;
	uint8_t *source = JS_GetArrayBuffer(ctx, &source_size, value);
	uint32_t *copy;

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

static JSValue image_constructor(JSContext *ctx, JSValueConst new_target,
	int argc, JSValueConst *argv)
{
	AthenaImage *image;
	JSValue prototype, object;
	const char *path = NULL;

	if (!image_argc(ctx, argc, 0, 1, "Image"))
		return JS_EXCEPTION;
	if (argc == 1) {
		path = JS_ToCString(ctx, argv[0]);
		if (!path)
			return JS_EXCEPTION;
		dbgprintf("[Image] loading '%s'\n", path);
	}

	image = path ? athena_image_create(path, true) :
		athena_image_create_empty(true);
	if (path)
		JS_FreeCString(ctx, path);
	if (!image)
		return JS_ThrowInternalError(ctx, "failed to create Image");
	dbgprintf("[Image] image object created\n");

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
	case 1: surface->Filter = number; break;
	case 3:
		switch (number) {
		case 4: surface->PSM = GS_PSM_T4; break;
		case 8: surface->PSM = GS_PSM_T8; break;
		case 16: surface->PSM = GS_PSM_CT16S; break;
		case 24: surface->PSM = GS_PSM_CT24; break;
		case 32: surface->PSM = GS_PSM_CT32; break;
		default: return JS_ThrowRangeError(ctx, "unsupported Image.bpp");
		}
		break;
	case 5:
		if (!image_set_buffer(ctx, image, value, &surface->Mem,
			image_size(image), "Image.pixels"))
			return JS_EXCEPTION;
		image_update_ready(image);
		break;
	case 6: {
		uint32_t size = surface->PSM == GS_PSM_T4 ?
			athena_surface_size(8, 2, GS_PSM_CT32) :
			(surface->PSM == GS_PSM_T8 ?
				athena_surface_size(16, 16, GS_PSM_CT32) : 0);
		if (!size || !image_set_buffer(ctx, image, value, &surface->Clut,
			size, "Image.palette"))
			return JS_EXCEPTION;
		break;
	}
	case 7: surface->Width = number; image_update_ready(image); break;
	case 8: surface->Height = number; image_update_ready(image); break;
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
	if (!athena_image_copy_vram_block(source, source_x, source_y,
		destination, destination_x, destination_y))
		return JS_ThrowInternalError(ctx, "failed to copy Image VRAM block");
	return JS_UNDEFINED;
}

static JSClassDef image_class = {
	"Image",
	.finalizer = image_finalizer,
};

static const JSCFunctionListEntry image_proto_funcs[] = {
	JS_CFUNC_DEF("draw", 2, image_draw),
	JS_CFUNC_DEF("ready", 0, image_ready),
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
	dbgprintf("[Image] module initialization started\n");
	JS_NewClassID(&image_class_id);
	JS_NewClass(JS_GetRuntime(ctx), image_class_id, &image_class);
	dbgprintf("[Image] class registered\n");
	prototype = JS_NewObject(ctx);
	JS_SetPropertyFunctionList(ctx, prototype, image_proto_funcs,
		countof(image_proto_funcs));
	dbgprintf("[Image] prototype configured\n");
	constructor = JS_NewCFunction2(ctx, image_constructor, "Image", 1,
		JS_CFUNC_constructor, 0);
	JS_SetConstructor(ctx, constructor, prototype);
	JS_SetClassProto(ctx, image_class_id, prototype);
	JS_SetPropertyFunctionList(ctx, constructor, image_static_funcs,
		countof(image_static_funcs));
	dbgprintf("[Image] constructor configured\n");
	JS_SetModuleExport(ctx, module, "Image", constructor);
	dbgprintf("[Image] module initialization finished\n");
	return 0;
}

JSModuleDef *athena_image_init(JSContext *ctx)
{
	dbgprintf("[Image] module registered\n");
	JSModuleDef *module = JS_NewCModule(ctx, "Image", image_module_init);
	if (!module)
		return NULL;
	JS_AddModuleExport(ctx, module, "Image");
	return module;
}
