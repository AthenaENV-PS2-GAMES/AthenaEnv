#include <stdlib.h>
#include <malloc.h>
#include <string.h>

#include <ath_env.h>
#include <athena/sprite.h>
#include <athena/image.h>
#include <ath_bindings.h>

static JSClassID js_tilemap_descriptor_class_id;
static JSClassID js_tilemap_instance_class_id;

typedef struct {
	AthenaTilemapDescriptor *descriptor;
	JSValue textures_array;
} JSTilemapDescriptor;

typedef struct {
	JSValue descriptor_obj;
	AthenaTilemapDescriptor *descriptor;
	JSValue sprite_buffer;
	AthenaTilemapInstance *instance;
} JSTilemapInstance;

static void js_free_value_safe(JSContext *ctx, JSValue value) {
	if (!JS_IsUndefined(value))
		JS_FreeValue(ctx, value);
}

static void js_assign_float_prop(JSContext *ctx, JSValue obj, const char *name, float *out) {
	JSValue val = JS_GetPropertyStr(ctx, obj, name);
	if (!JS_IsUndefined(val))
		JS_ToFloat32(ctx, out, val);
	JS_FreeValue(ctx, val);
}

static void js_assign_uint32_prop(JSContext *ctx, JSValue obj, const char *name, uint32_t *out) {
	JSValue val = JS_GetPropertyStr(ctx, obj, name);
	if (!JS_IsUndefined(val))
		JS_ToUint32(ctx, out, val);
	JS_FreeValue(ctx, val);
}

static void js_assign_int32_prop(JSContext *ctx, JSValue obj, const char *name, int32_t *out) {
	JSValue val = JS_GetPropertyStr(ctx, obj, name);
	if (!JS_IsUndefined(val))
		JS_ToInt32(ctx, out, val);
	JS_FreeValue(ctx, val);
}

static void js_assign_uint64_prop(JSContext *ctx, JSValue obj, const char *name, uint64_t *out) {
	JSValue val = JS_GetPropertyStr(ctx, obj, name);
	if (!JS_IsUndefined(val)) {
		int64_t tmp = 0;
		JS_ToInt64(ctx, &tmp, val);
		*out = (uint64_t)tmp;
	}
	JS_FreeValue(ctx, val);
}

static uint8_t *js_get_buffer_data(JSContext *ctx, JSValueConst val, size_t *length) {
	uint8_t *ptr = JS_GetArrayBuffer(ctx, length, val);
	if (ptr)
		return ptr;

	size_t byte_offset = 0;
	size_t byte_length = 0;
	size_t bytes_per_element = 0;
	JSValue array_buffer = JS_GetTypedArrayBuffer(ctx, val, &byte_offset, &byte_length, &bytes_per_element);
	if (JS_IsException(array_buffer))
		return NULL;

	size_t buffer_size = 0;
	uint8_t *data = JS_GetArrayBuffer(ctx, &buffer_size, array_buffer);
	JS_FreeValue(ctx, array_buffer);
	if (!data)
		return NULL;
	if (byte_offset + byte_length > buffer_size)
		return NULL;

	*length = byte_length;
	return data + byte_offset;
}

static JSValue js_sprite_build_layout_object(JSContext *ctx) {
	const athena_tilemap_layout *layout = athena_sprite_layout();
	JSValue layout_obj = JS_NewObject(ctx);
	JSValue offsets = JS_NewObject(ctx);
	if (JS_IsException(layout_obj) || JS_IsException(offsets)) {
		js_free_value_safe(ctx, layout_obj);
		js_free_value_safe(ctx, offsets);
		return JS_EXCEPTION;
	}

	JS_SetPropertyStr(ctx, layout_obj, "stride", JS_NewUint32(ctx, layout->stride));
	JS_SetPropertyStr(ctx, offsets, "x", JS_NewUint32(ctx, layout->offset_x));
	JS_SetPropertyStr(ctx, offsets, "y", JS_NewUint32(ctx, layout->offset_y));
	JS_SetPropertyStr(ctx, offsets, "w", JS_NewUint32(ctx, layout->offset_w));
	JS_SetPropertyStr(ctx, offsets, "h", JS_NewUint32(ctx, layout->offset_h));
	JS_SetPropertyStr(ctx, offsets, "u1", JS_NewUint32(ctx, layout->offset_u1));
	JS_SetPropertyStr(ctx, offsets, "v1", JS_NewUint32(ctx, layout->offset_v1));
	JS_SetPropertyStr(ctx, offsets, "u2", JS_NewUint32(ctx, layout->offset_u2));
	JS_SetPropertyStr(ctx, offsets, "v2", JS_NewUint32(ctx, layout->offset_v2));
	JS_SetPropertyStr(ctx, offsets, "r", JS_NewUint32(ctx, layout->offset_r));
	JS_SetPropertyStr(ctx, offsets, "g", JS_NewUint32(ctx, layout->offset_g));
	JS_SetPropertyStr(ctx, offsets, "b", JS_NewUint32(ctx, layout->offset_b));
	JS_SetPropertyStr(ctx, offsets, "a", JS_NewUint32(ctx, layout->offset_a));
	JS_SetPropertyStr(ctx, offsets, "zindex", JS_NewUint32(ctx, layout->offset_zindex));
	JS_SetPropertyStr(ctx, layout_obj, "offsets", offsets);
	return layout_obj;
}

static int js_sprite_fill_materials_from_array(JSContext *ctx, JSValue array,
    athena_sprite_material **out_materials, uint32_t *out_count) {
	uint32_t count = 0;
	JSValue length_prop = JS_GetPropertyStr(ctx, array, "length");
	if (JS_IsException(length_prop))
		return -1;
	JS_ToUint32(ctx, &count, length_prop);
	JS_FreeValue(ctx, length_prop);
	if (!count)
		return 0;

	athena_sprite_material *materials = malloc(sizeof(athena_sprite_material) * count);
	if (!materials)
		return -1;

	for (uint32_t i = 0; i < count; i++) {
		JSValue material = JS_GetPropertyUint32(ctx, array, i);
		if (JS_IsException(material) || !JS_IsObject(material)) {
			JS_FreeValue(ctx, material);
			free(materials);
			return -1;
		}
		js_assign_int32_prop(ctx, material, "texture_index", &materials[i].texture_index);
		js_assign_uint64_prop(ctx, material, "blend_mode", &materials[i].blend_mode);
		js_assign_uint32_prop(ctx, material, "end_offset", &materials[i].end);
		JS_FreeValue(ctx, material);
	}

	*out_materials = materials;
	*out_count = count;
	return 0;
}

static int js_sprite_fill_materials_from_buffer(JSContext *ctx, JSValue buffer,
    athena_sprite_material **out_materials, uint32_t *out_count) {
	size_t length = 0;
	uint8_t *ptr = js_get_buffer_data(ctx, buffer, &length);
	if (!ptr) {
		JS_ThrowTypeError(ctx, "materials buffer must be ArrayBuffer or TypedArray");
		return -1;
	}
	if (length % sizeof(athena_sprite_material) != 0) {
		JS_ThrowRangeError(ctx, "invalid material buffer size");
		return -1;
	}
	athena_sprite_material *materials = malloc(length);
	if (!materials)
		return -1;
	memcpy(materials, ptr, length);
	*out_materials = materials;
	*out_count = length / sizeof(athena_sprite_material);
	return 0;
}

static JSValue js_sprite_import_texture(JSContext *ctx, JSValue texture_val,
    JSValue textures_array, AthenaImage **textures, uint32_t index) {
	if (JS_IsString(texture_val)) {
		const char *texture_path = JS_ToCString(ctx, texture_val);
		if (!texture_path)
			return JS_EXCEPTION;

		AthenaImage *image = athena_image_create(texture_path, true);
		if (!image) {
			JS_FreeCString(ctx, texture_path);
			return JS_EXCEPTION;
		}

		JSValue img_obj = JS_NewObjectClass(ctx, get_img_class_id());
		if (JS_IsException(img_obj)) {
			athena_image_destroy(image);
			JS_FreeCString(ctx, texture_path);
			return img_obj;
		}

		JS_FreeCString(ctx, texture_path);
		JS_SetOpaque(img_obj, image);
		JS_SetPropertyUint32(ctx, textures_array, index, img_obj);
		textures[index] = image;
		return JS_UNDEFINED;
	}

	AthenaImage *image = JS_GetOpaque2(ctx, texture_val, get_img_class_id());
	if (!image)
		return JS_EXCEPTION;

	JS_SetPropertyUint32(ctx, textures_array, index, texture_val);
	JS_DupValue(ctx, texture_val);
	textures[index] = image;
	return JS_UNDEFINED;
}

static void js_tilemap_descriptor_release_ctx(JSContext *ctx, JSTilemapDescriptor *descriptor) {
	if (!descriptor)
		return;
	athena_sprite_descriptor_destroy(descriptor->descriptor);
	js_free_value_safe(ctx, descriptor->textures_array);
	js_free(ctx, descriptor);
}

static void js_tilemap_descriptor_dtor(JSRuntime *rt, JSValue val) {
	JSTilemapDescriptor *wrapper = JS_GetOpaque(val, js_tilemap_descriptor_class_id);
	if (!wrapper)
		return;
	athena_sprite_descriptor_destroy(wrapper->descriptor);
	JS_FreeValueRT(rt, wrapper->textures_array);
	js_free_rt(rt, wrapper);
	JS_SetOpaque(val, NULL);
}

static JSValue js_tilemap_descriptor_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
	JSTilemapDescriptor *wrapper = js_mallocz(ctx, sizeof(*wrapper));
	AthenaImage **texture_images = NULL;
	athena_sprite_material *materials = NULL;
	uint32_t texture_count = 0;
	uint32_t material_count = 0;
	JSValue opts, textures = JS_UNDEFINED, materials_val = JS_UNDEFINED;
	JSValue proto, obj;

	if (!wrapper)
		return JS_EXCEPTION;
	wrapper->textures_array = JS_UNDEFINED;

	opts = argc > 0 ? argv[0] : JS_UNDEFINED;
	if (!JS_IsObject(opts)) {
		js_tilemap_descriptor_release_ctx(ctx, wrapper);
		return JS_ThrowTypeError(ctx, "descriptor options must be object");
	}

	textures = JS_GetPropertyStr(ctx, opts, "textures");
	if (JS_IsException(textures))
		goto fail;
	if (JS_IsArray(ctx, textures)) {
		JSValue length_prop = JS_GetPropertyStr(ctx, textures, "length");
		JS_ToUint32(ctx, &texture_count, length_prop);
		JS_FreeValue(ctx, length_prop);
		if (texture_count) {
			texture_images = calloc(texture_count, sizeof(AthenaImage *));
			if (!texture_images)
				goto fail;
			wrapper->textures_array = JS_NewArray(ctx);
			if (JS_IsException(wrapper->textures_array))
				goto fail;
			for (uint32_t i = 0; i < texture_count; i++) {
				JSValue texture_val = JS_GetPropertyUint32(ctx, textures, i);
				if (JS_IsUndefined(texture_val)) {
					JS_FreeValue(ctx, texture_val);
					continue;
				}
				if (JS_IsException(js_sprite_import_texture(ctx, texture_val, wrapper->textures_array,
				                                            texture_images, i)))
					goto fail_texture;
				JS_FreeValue(ctx, texture_val);
			}
		}
	}
	JS_FreeValue(ctx, textures);

	materials_val = JS_GetPropertyStr(ctx, opts, "materials");
	if (JS_IsException(materials_val))
		goto fail;
	if (!JS_IsUndefined(materials_val)) {
		if (JS_IsArray(ctx, materials_val)) {
			if (js_sprite_fill_materials_from_array(ctx, materials_val, &materials, &material_count) < 0)
				goto fail_materials;
		} else if (js_sprite_fill_materials_from_buffer(ctx, materials_val, &materials, &material_count) < 0) {
			goto fail_materials;
		}
	}
	JS_FreeValue(ctx, materials_val);

	wrapper->descriptor = athena_sprite_descriptor_create(texture_images, texture_count, materials, material_count);
	free(texture_images);
	if (!wrapper->descriptor)
		goto fail;

	proto = JS_GetPropertyStr(ctx, new_target, "prototype");
	if (JS_IsException(proto))
		goto fail;
	obj = JS_NewObjectProtoClass(ctx, proto, js_tilemap_descriptor_class_id);
	JS_FreeValue(ctx, proto);
	if (JS_IsException(obj))
		goto fail;
	JS_SetOpaque(obj, wrapper);
	return obj;

fail_materials:
	JS_FreeValue(ctx, materials_val);
fail_texture:
	JS_FreeValue(ctx, textures);
fail:
	free(texture_images);
	free(materials);
	js_tilemap_descriptor_release_ctx(ctx, wrapper);
	return JS_EXCEPTION;
}

static JSValue js_tilemap_descriptor_get_material_count(JSContext *ctx, JSValueConst this_val, int magic) {
	JSTilemapDescriptor *descriptor = JS_GetOpaque2(ctx, this_val, js_tilemap_descriptor_class_id);
	if (!descriptor)
		return JS_EXCEPTION;
	return JS_NewUint32(ctx, athena_sprite_descriptor_material_count(descriptor->descriptor));
}

static void js_tilemap_instance_release_ctx(JSContext *ctx, JSTilemapInstance *instance) {
	if (!instance)
		return;
	athena_sprite_instance_destroy(instance->instance);
	js_free_value_safe(ctx, instance->descriptor_obj);
	js_free_value_safe(ctx, instance->sprite_buffer);
	js_free(ctx, instance);
}

static void js_tilemap_instance_dtor(JSRuntime *rt, JSValue val) {
	JSTilemapInstance *wrapper = JS_GetOpaque(val, js_tilemap_instance_class_id);
	if (!wrapper)
		return;
	athena_sprite_instance_destroy(wrapper->instance);
	JS_FreeValueRT(rt, wrapper->descriptor_obj);
	JS_FreeValueRT(rt, wrapper->sprite_buffer);
	js_free_rt(rt, wrapper);
	JS_SetOpaque(val, NULL);
}

static JSValue js_tilemap_instance_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
	JSTilemapInstance *wrapper = js_mallocz(ctx, sizeof(*wrapper));
	JSValue opts, descriptor_val, sprite_buffer, proto, obj;
	size_t length = 0;
	uint8_t *ptr;

	if (!wrapper)
		return JS_EXCEPTION;
	wrapper->descriptor_obj = JS_UNDEFINED;
	wrapper->sprite_buffer = JS_UNDEFINED;

	opts = argc > 0 ? argv[0] : JS_UNDEFINED;
	if (!JS_IsObject(opts)) {
		js_tilemap_instance_release_ctx(ctx, wrapper);
		return JS_ThrowTypeError(ctx, "instance options must be object");
	}

	descriptor_val = JS_GetPropertyStr(ctx, opts, "descriptor");
	if (JS_IsException(descriptor_val))
		goto fail;
	{
		JSTilemapDescriptor *descriptor = JS_GetOpaque2(ctx, descriptor_val, js_tilemap_descriptor_class_id);
		if (!descriptor) {
			JS_FreeValue(ctx, descriptor_val);
			goto fail;
		}
		wrapper->descriptor = descriptor->descriptor;
	}
	wrapper->descriptor_obj = descriptor_val;

	sprite_buffer = JS_GetPropertyStr(ctx, opts, "spriteBuffer");
	if (JS_IsException(sprite_buffer))
		goto fail;
	if (!JS_IsUndefined(sprite_buffer)) {
		ptr = js_get_buffer_data(ctx, sprite_buffer, &length);
		if (!ptr || length % sizeof(athena_sprite_data) != 0) {
			JS_FreeValue(ctx, sprite_buffer);
			goto fail;
		}
		wrapper->instance = athena_sprite_instance_create(wrapper->descriptor,
			(athena_sprite_data *)ptr, length / sizeof(athena_sprite_data), false);
		if (!wrapper->instance) {
			JS_FreeValue(ctx, sprite_buffer);
			goto fail;
		}
		wrapper->sprite_buffer = JS_DupValue(ctx, sprite_buffer);
	} else {
		wrapper->instance = athena_sprite_instance_create(wrapper->descriptor, NULL, 0, false);
		if (!wrapper->instance)
			goto fail;
	}
	JS_FreeValue(ctx, sprite_buffer);

	proto = JS_GetPropertyStr(ctx, new_target, "prototype");
	if (JS_IsException(proto))
		goto fail;
	obj = JS_NewObjectProtoClass(ctx, proto, js_tilemap_instance_class_id);
	JS_FreeValue(ctx, proto);
	if (JS_IsException(obj))
		goto fail;
	JS_SetOpaque(obj, wrapper);
	return obj;

fail:
	js_tilemap_instance_release_ctx(ctx, wrapper);
	return JS_EXCEPTION;
}

static JSValue js_tilemap_instance_render(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	JSTilemapInstance *instance = JS_GetOpaque2(ctx, this_val, js_tilemap_instance_class_id);
	float x = 0.0f, y = 0.0f, z = 0.0f;
	if (!instance || !instance->instance)
		return JS_EXCEPTION;
	JS_ToFloat32(ctx, &x, argc > 0 ? argv[0] : JS_UNDEFINED);
	JS_ToFloat32(ctx, &y, argc > 1 ? argv[1] : JS_UNDEFINED);
	if (argc > 2)
		JS_ToFloat32(ctx, &z, argv[2]);
	athena_sprite_instance_render(instance->instance, x, y, z);
	return JS_UNDEFINED;
}

static JSValue js_tilemap_instance_replace_buffer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	JSTilemapInstance *instance = JS_GetOpaque2(ctx, this_val, js_tilemap_instance_class_id);
	size_t length = 0;
	uint8_t *ptr;
	if (!instance || !instance->instance)
		return JS_EXCEPTION;
	if (argc < 1)
		return JS_ThrowTypeError(ctx, "replaceSpriteBuffer expects buffer argument");
	ptr = js_get_buffer_data(ctx, argv[0], &length);
	if (!ptr || length % sizeof(athena_sprite_data) != 0)
		return JS_EXCEPTION;
	if (athena_sprite_instance_replace_buffer(instance->instance, (athena_sprite_data *)ptr,
		length / sizeof(athena_sprite_data), false) < 0)
		return JS_EXCEPTION;
	js_free_value_safe(ctx, instance->sprite_buffer);
	instance->sprite_buffer = JS_DupValue(ctx, argv[0]);
	return JS_UNDEFINED;
}

static JSValue js_tilemap_instance_get_buffer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	JSTilemapInstance *instance = JS_GetOpaque2(ctx, this_val, js_tilemap_instance_class_id);
	if (!instance)
		return JS_EXCEPTION;
	if (JS_IsUndefined(instance->sprite_buffer))
		return JS_UNDEFINED;
	return JS_DupValue(ctx, instance->sprite_buffer);
}

static JSValue js_tilemap_instance_update(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	JSTilemapInstance *instance = JS_GetOpaque2(ctx, this_val, js_tilemap_instance_class_id);
	uint32_t dst_offset = 0, copy_count;
	size_t src_length = 0;
	uint8_t *src_ptr;
	if (!instance || !instance->instance)
		return JS_EXCEPTION;
	if (argc < 2)
		return JS_ThrowTypeError(ctx, "updateSprites expects dstOffset and source buffer");
	JS_ToUint32(ctx, &dst_offset, argv[0]);
	src_ptr = js_get_buffer_data(ctx, argv[1], &src_length);
	if (!src_ptr || src_length % sizeof(athena_sprite_data) != 0)
		return JS_EXCEPTION;
	copy_count = src_length / sizeof(athena_sprite_data);
	if (argc > 2) {
		JS_ToUint32(ctx, &copy_count, argv[2]);
		if (copy_count > src_length / sizeof(athena_sprite_data))
			copy_count = src_length / sizeof(athena_sprite_data);
	}
	if (athena_sprite_instance_update_sprites(instance->instance, dst_offset,
		(const athena_sprite_data *)src_ptr, copy_count) < 0)
		return JS_ThrowRangeError(ctx, "updateSprites exceeds buffer");
	return JS_UNDEFINED;
}

static void js_sprite_buffer_free(JSRuntime *rt, void *opaque, void *ptr) {
	if (ptr)
		js_free_rt(rt, ptr);
}

static JSValue js_sprite_buffer_create(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	uint32_t sprite_count = 0;
	size_t length;
	uint8_t *data;
	JSValue buffer;
	if (argc < 1)
		return JS_ThrowTypeError(ctx, "create(count) requires count");
	JS_ToUint32(ctx, &sprite_count, argv[0]);
	length = sprite_count * sizeof(athena_sprite_data);
	data = js_mallocz(ctx, length);
	if (!data)
		return JS_EXCEPTION;
	buffer = JS_NewArrayBuffer(ctx, data, length, js_sprite_buffer_free, NULL, 0);
	if (JS_IsException(buffer)) {
		js_free(ctx, data);
		return buffer;
	}
	return buffer;
}

static JSValue js_sprite_buffer_from_objects(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
	uint32_t count = 0;
	size_t length;
	athena_sprite_data *sprites;
	JSValue buffer;
	JSValue arr, length_prop;
	if (argc < 1)
		return JS_ThrowTypeError(ctx, "fromObjects requires array");
	arr = argv[0];
	if (!JS_IsArray(ctx, arr))
		return JS_ThrowTypeError(ctx, "fromObjects expects array");

	length_prop = JS_GetPropertyStr(ctx, arr, "length");
	if (JS_IsException(length_prop))
		return length_prop;
	JS_ToUint32(ctx, &count, length_prop);
	JS_FreeValue(ctx, length_prop);

	length = count * sizeof(athena_sprite_data);
	sprites = (athena_sprite_data *)js_mallocz(ctx, length);
	if (!sprites)
		return JS_EXCEPTION;

	for (uint32_t i = 0; i < count; i++) {
		JSValue sprite = JS_GetPropertyUint32(ctx, arr, i);
		if (JS_IsException(sprite) || !JS_IsObject(sprite)) {
			JS_FreeValue(ctx, sprite);
			js_free(ctx, sprites);
			return JS_ThrowTypeError(ctx, "sprite entry must be object");
		}
		js_assign_float_prop(ctx, sprite, "x", &sprites[i].x);
		js_assign_float_prop(ctx, sprite, "y", &sprites[i].y);
		js_assign_float_prop(ctx, sprite, "zindex", &sprites[i].zindex);
		js_assign_float_prop(ctx, sprite, "w", &sprites[i].w);
		js_assign_float_prop(ctx, sprite, "h", &sprites[i].h);
		js_assign_float_prop(ctx, sprite, "u1", &sprites[i].u1);
		js_assign_float_prop(ctx, sprite, "v1", &sprites[i].v1);
		js_assign_float_prop(ctx, sprite, "u2", &sprites[i].u2);
		js_assign_float_prop(ctx, sprite, "v2", &sprites[i].v2);
		js_assign_uint32_prop(ctx, sprite, "r", &sprites[i].r);
		js_assign_uint32_prop(ctx, sprite, "g", &sprites[i].g);
		js_assign_uint32_prop(ctx, sprite, "b", &sprites[i].b);
		js_assign_uint32_prop(ctx, sprite, "a", &sprites[i].a);
		JS_FreeValue(ctx, sprite);
	}

	buffer = JS_NewArrayBuffer(ctx, (uint8_t *)sprites, length, js_sprite_buffer_free, NULL, 0);
	if (JS_IsException(buffer)) {
		js_free(ctx, sprites);
		return buffer;
	}
	return buffer;
}

static JSValue js_sprite_set_camera(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	float src_x = 0.0f, src_y = 0.0f;
	JS_ToFloat32(ctx, &src_x, argv[0]);
	JS_ToFloat32(ctx, &src_y, argv[1]);
	athena_sprite_set_camera(src_x, src_y);
	return JS_UNDEFINED;
}

static JSValue js_sprite_init_fn(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	athena_sprite_init();
	return JS_UNDEFINED;
}

static JSValue js_sprite_begin(JSContext *ctx, JSValue this_val, int argc, JSValueConst *argv) {
	athena_sprite_begin();
	return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_tilemap_descriptor_proto_funcs[] = {
	JS_CGETSET_DEF("materialCount", js_tilemap_descriptor_get_material_count, NULL),
};

static const JSCFunctionListEntry js_tilemap_instance_proto_funcs[] = {
	JS_CFUNC_DEF("render", 3, js_tilemap_instance_render),
	JS_CFUNC_DEF("replaceSpriteBuffer", 1, js_tilemap_instance_replace_buffer),
	JS_CFUNC_DEF("getSpriteBuffer", 0, js_tilemap_instance_get_buffer),
	JS_CFUNC_DEF("updateSprites", 3, js_tilemap_instance_update),
};

static const JSCFunctionListEntry js_tilemap_spritebuffer_funcs[] = {
	JS_CFUNC_DEF("create", 1, js_sprite_buffer_create),
	JS_CFUNC_DEF("fromObjects", 1, js_sprite_buffer_from_objects),
};

static const JSCFunctionListEntry js_tilemap_funcs[] = {
	JS_CFUNC_DEF("init", 0, js_sprite_init_fn),
	JS_CFUNC_DEF("begin", 0, js_sprite_begin),
	JS_CFUNC_DEF("setCamera", 2, js_sprite_set_camera),
};

static int js_tilemap_module_init(JSContext *ctx, JSModuleDef *m) {
	JS_NewClassID(&js_tilemap_descriptor_class_id);
	JSClassDef descriptor_class = {
		"TileMapDescriptor",
		.finalizer = js_tilemap_descriptor_dtor,
	};
	JS_NewClass(JS_GetRuntime(ctx), js_tilemap_descriptor_class_id, &descriptor_class);

	JS_NewClassID(&js_tilemap_instance_class_id);
	JSClassDef instance_class = {
		"TileMapInstance",
		.finalizer = js_tilemap_instance_dtor,
	};
	JS_NewClass(JS_GetRuntime(ctx), js_tilemap_instance_class_id, &instance_class);

	JSValue descriptor_proto = JS_NewObject(ctx);
	JS_SetPropertyFunctionList(ctx, descriptor_proto, js_tilemap_descriptor_proto_funcs,
		countof(js_tilemap_descriptor_proto_funcs));
	JSValue descriptor_ctor = JS_NewCFunction2(ctx, js_tilemap_descriptor_ctor, "Descriptor", 1, JS_CFUNC_constructor, 0);
	JS_SetConstructor(ctx, descriptor_ctor, descriptor_proto);
	JS_SetClassProto(ctx, js_tilemap_descriptor_class_id, descriptor_proto);

	JSValue instance_proto = JS_NewObject(ctx);
	JS_SetPropertyFunctionList(ctx, instance_proto, js_tilemap_instance_proto_funcs,
		countof(js_tilemap_instance_proto_funcs));
	JSValue instance_ctor = JS_NewCFunction2(ctx, js_tilemap_instance_ctor, "Instance", 1, JS_CFUNC_constructor, 0);
	JS_SetConstructor(ctx, instance_ctor, instance_proto);
	JS_SetClassProto(ctx, js_tilemap_instance_class_id, instance_proto);

	JSValue spritebuffer_obj = JS_NewObject(ctx);
	JS_SetPropertyFunctionList(ctx, spritebuffer_obj, js_tilemap_spritebuffer_funcs,
		countof(js_tilemap_spritebuffer_funcs));

	JSValue tilemap_obj = JS_NewObject(ctx);
	JS_SetPropertyFunctionList(ctx, tilemap_obj, js_tilemap_funcs, countof(js_tilemap_funcs));
	JS_SetPropertyStr(ctx, tilemap_obj, "Descriptor", descriptor_ctor);
	JS_SetPropertyStr(ctx, tilemap_obj, "Instance", instance_ctor);
	JS_SetPropertyStr(ctx, tilemap_obj, "SpriteBuffer", spritebuffer_obj);
	{
		JSValue layout_obj = js_sprite_build_layout_object(ctx);
		if (JS_IsException(layout_obj)) {
			JS_FreeValue(ctx, tilemap_obj);
			return -1;
		}
		JS_SetPropertyStr(ctx, tilemap_obj, "layout", layout_obj);
	}

	JS_SetModuleExport(ctx, m, "default", tilemap_obj);
	return 0;
}

JSModuleDef *athena_tilemap_init(JSContext *ctx) {
	JSModuleDef *m = JS_NewCModule(ctx, "TileMap", js_tilemap_module_init);
	if (!m)
		return NULL;
	JS_AddModuleExport(ctx, m, "default");
	return m;
}
