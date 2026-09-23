#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ath_env.h>

#include "../../image/native/image.h"
#include "../../image/quickjs/ath_image.h"
#include "../native/tilemap.h"
#include "ath_tilemap.h"

/* Keeps sprite byte offsets representable in 32 bits. */
#define TILEMAP_MAX_SPRITES (UINT32_MAX / sizeof(AthenaTileSprite))
/* The VU1 program reads each sprite with 128-bit loads. */
#define TILEMAP_DMA_ALIGN 16

static JSClassID descriptor_class_id;
static JSClassID instance_class_id;

typedef struct {
    AthenaTileMaterial *materials;
    uint32_t material_count;
    /* Image objects the descriptor keeps alive. */
    JSValue *textures;
    /* Scratch array filled with the textures' current surfaces per render. */
    GSSURFACE **surfaces;
    uint32_t texture_count;
    /* Tileset geometry for setTiles() and fromGrid(). */
    bool has_atlas;
    AthenaTileAtlas atlas;
} TileMapDescriptor;

typedef struct {
    JSValue descriptor;
    /* ArrayBuffer or typed array holding AthenaTileSprite records. */
    JSValue buffer;
    /* TILEMAP_STORAGE_* resolved for `buffer`. */
    int buffer_kind;
    /* Rendered since the last sync, so queued DMA may reference `buffer`. */
    bool rendered;
    /* Set by fromGrid(): the buffer is a row-major grid, so render() culls. */
    bool has_grid;
    AthenaTileGrid grid;
    /* Scratch for visible ranges, one per grid row. */
    AthenaTileRange *ranges;
    uint32_t last_drawn;
} TileMapInstance;

typedef struct {
    const char *name;
    uint32_t offset;
    bool color;
} TileMapSpriteField;

static const TileMapSpriteField sprite_fields[] = {
    { "x", offsetof(AthenaTileSprite, x), false },
    { "y", offsetof(AthenaTileSprite, y), false },
    { "w", offsetof(AthenaTileSprite, w), false },
    { "h", offsetof(AthenaTileSprite, h), false },
    { "u1", offsetof(AthenaTileSprite, u1), false },
    { "v1", offsetof(AthenaTileSprite, v1), false },
    { "u2", offsetof(AthenaTileSprite, u2), false },
    { "v2", offsetof(AthenaTileSprite, v2), false },
    { "zindex", offsetof(AthenaTileSprite, zindex), false },
    { "r", offsetof(AthenaTileSprite, r), true },
    { "g", offsetof(AthenaTileSprite, g), true },
    { "b", offsetof(AthenaTileSprite, b), true },
    { "a", offsetof(AthenaTileSprite, a), true },
};

static int tilemap_argc(JSContext *ctx, int argc, int minimum, int maximum,
    const char *name)
{
    if (argc < minimum || argc > maximum) {
        if (minimum == maximum)
            JS_ThrowTypeError(ctx, "%s expects exactly %d arguments", name,
                minimum);
        else
            JS_ThrowTypeError(ctx, "%s expects between %d and %d arguments",
                name, minimum, maximum);
        return 0;
    }
    return 1;
}

static int tilemap_float(JSContext *ctx, JSValueConst value, float *out,
    const char *name)
{
    double number;

    if (JS_ToFloat64(ctx, &number, value))
        return 0;
    if (!isfinite(number)) {
        JS_ThrowRangeError(ctx, "%s must be finite", name);
        return 0;
    }
    *out = (float)number;
    return 1;
}

static int tilemap_uint(JSContext *ctx, JSValueConst value, double maximum,
    uint32_t *out, const char *name)
{
    double number;

    if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &number, value) ||
        !(number >= 0.0 && number <= maximum) || number != floor(number)) {
        JS_ThrowRangeError(ctx, "%s must be an integer between 0 and %.0f",
            name, maximum);
        return 0;
    }
    *out = (uint32_t)number;
    return 1;
}

static int tilemap_array_length(JSContext *ctx, JSValueConst array,
    uint32_t *length)
{
    JSValue value = JS_GetPropertyStr(ctx, array, "length");
    int result;

    if (JS_IsException(value))
        return 0;
    result = JS_ToUint32(ctx, length, value) == 0;
    JS_FreeValue(ctx, value);
    return result;
}

/* What a sprite storage value was resolved as; see tilemap_sprite_storage. */
#define TILEMAP_STORAGE_UNKNOWN 0
#define TILEMAP_STORAGE_ARRAY_BUFFER 1
#define TILEMAP_STORAGE_TYPED_ARRAY 2

static uint8_t *tilemap_array_buffer_data(JSContext *ctx, JSValueConst value,
    size_t *length)
{
    uint8_t *data = JS_GetArrayBuffer(ctx, length, value);

    if (!data)
        JS_FreeValue(ctx, JS_GetException(ctx));
    return data;
}

static uint8_t *tilemap_typed_array_data(JSContext *ctx, JSValueConst value,
    size_t *length)
{
    size_t offset = 0;
    size_t byte_length = 0;
    size_t element_size = 0;
    size_t buffer_length = 0;
    uint8_t *data;
    JSValue array_buffer = JS_GetTypedArrayBuffer(ctx, value, &offset,
        &byte_length, &element_size);

    if (JS_IsException(array_buffer)) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return NULL;
    }
    data = JS_GetArrayBuffer(ctx, &buffer_length, array_buffer);
    JS_FreeValue(ctx, array_buffer);
    if (!data || offset + byte_length > buffer_length) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return NULL;
    }
    *length = byte_length;
    return data + offset;
}

/*
 * Resolves an ArrayBuffer or typed array into sprite storage. Buffers drawn
 * by VU1 must be 16-byte aligned; `aligned` is false for copy sources.
 *
 * QuickJS can only tell the two apart by throwing, and building an Error
 * costs about 0.2 ms on the EE. `kind` caches what the value resolved as, so
 * repeated calls take the right path without an exception.
 */
static int tilemap_sprite_storage(JSContext *ctx, JSValueConst value,
    bool aligned, int *kind, AthenaTileSprite **sprites, uint32_t *count,
    const char *name)
{
    int resolved = kind ? *kind : TILEMAP_STORAGE_UNKNOWN;
    size_t length = 0;
    uint8_t *data = NULL;

    if (JS_IsObject(value)) {
        if (resolved != TILEMAP_STORAGE_TYPED_ARRAY) {
            data = tilemap_array_buffer_data(ctx, value, &length);
            if (data)
                resolved = TILEMAP_STORAGE_ARRAY_BUFFER;
        }
        if (!data && resolved != TILEMAP_STORAGE_ARRAY_BUFFER) {
            data = tilemap_typed_array_data(ctx, value, &length);
            if (data)
                resolved = TILEMAP_STORAGE_TYPED_ARRAY;
        }
    }
    if (!data) {
        JS_ThrowTypeError(ctx,
            "%s must be an ArrayBuffer or typed array that is not detached",
            name);
        return 0;
    }
    if (length == 0 || length % sizeof(AthenaTileSprite) != 0) {
        JS_ThrowRangeError(ctx,
            "%s length must be a positive multiple of %u bytes", name,
            (unsigned int)sizeof(AthenaTileSprite));
        return 0;
    }
    if (aligned && ((uintptr_t)data & (TILEMAP_DMA_ALIGN - 1)) != 0) {
        JS_ThrowRangeError(ctx,
            "%s must be 16-byte aligned; allocate it with TileMap.SpriteBuffer",
            name);
        return 0;
    }
    *sprites = (AthenaTileSprite *)data;
    *count = (uint32_t)(length / sizeof(AthenaTileSprite));
    if (kind)
        *kind = resolved;
    return 1;
}

static void tilemap_buffer_free(JSRuntime *rt, void *opaque, void *ptr)
{
    free(ptr);
}

static JSValue tilemap_new_buffer(JSContext *ctx, AthenaTileSprite *sprites,
    uint32_t count)
{
    JSValue buffer = JS_NewArrayBuffer(ctx, (uint8_t *)sprites,
        (size_t)count * sizeof(*sprites), tilemap_buffer_free, NULL, 0);

    if (JS_IsException(buffer))
        free(sprites);
    return buffer;
}

/* ---- Descriptor ---------------------------------------------------- */

static void descriptor_free(JSRuntime *rt, TileMapDescriptor *descriptor)
{
    uint32_t i;

    if (!descriptor)
        return;
    for (i = 0; i < descriptor->texture_count; ++i)
        JS_FreeValueRT(rt, descriptor->textures[i]);
    free(descriptor->textures);
    free(descriptor->surfaces);
    free(descriptor->materials);
    free((void *)descriptor->atlas.uv);
    free(descriptor);
}

static void descriptor_finalizer(JSRuntime *rt, JSValue value)
{
    TileMapDescriptor *descriptor = JS_GetOpaque(value, descriptor_class_id);

    descriptor_free(rt, descriptor);
    JS_SetOpaque(value, NULL);
}

static JSValue descriptor_texture(JSContext *ctx, JSValue item, uint32_t index)
{
    const char *path;
    AthenaImage *image;
    JSValue value;

    if (athena_image_peek(item))
        return item;
    if (!JS_IsString(item)) {
        JS_FreeValue(ctx, item);
        return JS_ThrowTypeError(ctx,
            "TileMap.Descriptor options.textures[%u] must be a path or an Image",
            (unsigned int)index);
    }
    path = JS_ToCString(ctx, item);
    JS_FreeValue(ctx, item);
    if (!path)
        return JS_EXCEPTION;
    image = athena_image_create(path, true);
    if (!image) {
        value = JS_ThrowInternalError(ctx,
            "Unable to load TileMap texture \"%s\"", path);
        JS_FreeCString(ctx, path);
        return value;
    }
    JS_FreeCString(ctx, path);
    value = athena_image_to_value(ctx, image);
    if (JS_IsException(value))
        athena_image_destroy(image);
    return value;
}

static int descriptor_textures(JSContext *ctx, TileMapDescriptor *descriptor,
    JSValueConst options)
{
    JSValue textures = JS_GetPropertyStr(ctx, options, "textures");
    uint32_t length;
    uint32_t i;

    if (JS_IsException(textures))
        return 0;
    if (JS_IsUndefined(textures))
        return 1;
    if (!JS_IsArray(ctx, textures)) {
        JS_FreeValue(ctx, textures);
        JS_ThrowTypeError(ctx,
            "TileMap.Descriptor options.textures must be an array");
        return 0;
    }
    if (!tilemap_array_length(ctx, textures, &length)) {
        JS_FreeValue(ctx, textures);
        return 0;
    }
    if (length > 0) {
        descriptor->textures = malloc(length * sizeof(*descriptor->textures));
        descriptor->surfaces = calloc(length, sizeof(*descriptor->surfaces));
        if (!descriptor->textures || !descriptor->surfaces) {
            JS_FreeValue(ctx, textures);
            JS_ThrowOutOfMemory(ctx);
            return 0;
        }
    }
    for (i = 0; i < length; ++i) {
        JSValue item = JS_GetPropertyUint32(ctx, textures, i);

        if (!JS_IsException(item))
            item = descriptor_texture(ctx, item, i);
        if (JS_IsException(item)) {
            JS_FreeValue(ctx, textures);
            return 0;
        }
        descriptor->textures[i] = item;
        descriptor->texture_count = i + 1;
    }
    JS_FreeValue(ctx, textures);
    return 1;
}

static int descriptor_material(JSContext *ctx, TileMapDescriptor *descriptor,
    JSValueConst item, uint32_t index)
{
    AthenaTileMaterial *material = &descriptor->materials[index];
    JSValue value;
    char name[64];
    double number;
    int64_t blend;

    if (!JS_IsObject(item) || JS_IsArray(ctx, item)) {
        JS_ThrowTypeError(ctx,
            "TileMap.Descriptor options.materials[%u] must be an object",
            (unsigned int)index);
        return 0;
    }

    material->texture_index = descriptor->texture_count > 0 ?
        0 : ATHENA_TILEMAP_NO_TEXTURE;
    value = JS_GetPropertyStr(ctx, item, "textureIndex");
    if (JS_IsException(value))
        return 0;
    if (!JS_IsUndefined(value)) {
        if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &number, value) ||
            number != floor(number) || (number != -1.0 &&
                (number < 0.0 ||
                    number >= (double)descriptor->texture_count))) {
            JS_FreeValue(ctx, value);
            JS_ThrowRangeError(ctx,
                "TileMap.Descriptor options.materials[%u].textureIndex must be -1 or an index into textures",
                (unsigned int)index);
            return 0;
        }
        material->texture_index = (int32_t)number;
    }
    JS_FreeValue(ctx, value);

    value = JS_GetPropertyStr(ctx, item, "blendMode");
    if (JS_IsException(value))
        return 0;
    if (!JS_IsUndefined(value)) {
        if (!JS_IsNumber(value) || JS_ToInt64(ctx, &blend, value) ||
            blend < 0) {
            JS_FreeValue(ctx, value);
            JS_ThrowRangeError(ctx,
                "TileMap.Descriptor options.materials[%u].blendMode must be a Screen.alphaEquation() value",
                (unsigned int)index);
            return 0;
        }
        material->has_blend_mode = true;
        material->blend_mode = (uint64_t)blend;
    }
    JS_FreeValue(ctx, value);

    value = JS_GetPropertyStr(ctx, item, "endOffset");
    if (JS_IsException(value))
        return 0;
    snprintf(name, sizeof(name),
        "TileMap.Descriptor options.materials[%u].endOffset",
        (unsigned int)index);
    if (!tilemap_uint(ctx, value, (double)TILEMAP_MAX_SPRITES - 1.0,
        &material->end, name)) {
        JS_FreeValue(ctx, value);
        return 0;
    }
    JS_FreeValue(ctx, value);
    if (index > 0 && material->end < descriptor->materials[index - 1].end) {
        JS_ThrowRangeError(ctx,
            "TileMap.Descriptor materials must be ordered by endOffset");
        return 0;
    }
    return 1;
}

static int descriptor_materials(JSContext *ctx, TileMapDescriptor *descriptor,
    JSValueConst options)
{
    JSValue materials = JS_GetPropertyStr(ctx, options, "materials");
    uint32_t length;
    uint32_t i;

    if (JS_IsException(materials))
        return 0;
    if (!JS_IsArray(ctx, materials)) {
        JS_FreeValue(ctx, materials);
        JS_ThrowTypeError(ctx,
            "TileMap.Descriptor options.materials must be an array");
        return 0;
    }
    if (!tilemap_array_length(ctx, materials, &length)) {
        JS_FreeValue(ctx, materials);
        return 0;
    }
    if (length == 0) {
        JS_FreeValue(ctx, materials);
        JS_ThrowRangeError(ctx,
            "TileMap.Descriptor options.materials must not be empty");
        return 0;
    }
    descriptor->materials = calloc(length, sizeof(*descriptor->materials));
    if (!descriptor->materials) {
        JS_FreeValue(ctx, materials);
        JS_ThrowOutOfMemory(ctx);
        return 0;
    }
    descriptor->material_count = length;
    for (i = 0; i < length; ++i) {
        JSValue item = JS_GetPropertyUint32(ctx, materials, i);
        int valid;

        if (JS_IsException(item)) {
            JS_FreeValue(ctx, materials);
            return 0;
        }
        valid = descriptor_material(ctx, descriptor, item, i);
        JS_FreeValue(ctx, item);
        if (!valid) {
            JS_FreeValue(ctx, materials);
            return 0;
        }
    }
    JS_FreeValue(ctx, materials);
    return 1;
}

/* Largest atlas tile and column count; GS textures are at most 1024 wide. */
#define TILEMAP_MAX_ATLAS 1024

static int tilemap_uint_property(JSContext *ctx, JSValueConst object,
    const char *property, double minimum, double maximum, bool optional,
    uint32_t *out, const char *name)
{
    JSValue value = JS_GetPropertyStr(ctx, object, property);
    double number = 0.0;

    if (JS_IsException(value))
        return 0;
    if (optional && JS_IsUndefined(value))
        return 1;
    if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &number, value) ||
        !(number >= minimum && number <= maximum) ||
        number != floor(number)) {
        JS_FreeValue(ctx, value);
        JS_ThrowRangeError(ctx, "%s.%s must be an integer between %.0f and %.0f",
            name, property, minimum, maximum);
        return 0;
    }
    JS_FreeValue(ctx, value);
    *out = (uint32_t)number;
    return 1;
}

static int descriptor_atlas(JSContext *ctx, TileMapDescriptor *descriptor,
    JSValueConst options)
{
    JSValue atlas = JS_GetPropertyStr(ctx, options, "atlas");
    const char *name = "TileMap.Descriptor options.atlas";
    int valid;

    if (JS_IsException(atlas))
        return 0;
    if (JS_IsUndefined(atlas))
        return 1;
    if (!JS_IsObject(atlas) || JS_IsArray(ctx, atlas)) {
        JS_FreeValue(ctx, atlas);
        JS_ThrowTypeError(ctx, "%s must be an object", name);
        return 0;
    }
    valid = tilemap_uint_property(ctx, atlas, "tileWidth", 1,
            TILEMAP_MAX_ATLAS, false, &descriptor->atlas.tile_width, name) &&
        tilemap_uint_property(ctx, atlas, "tileHeight", 1,
            TILEMAP_MAX_ATLAS, false, &descriptor->atlas.tile_height, name) &&
        tilemap_uint_property(ctx, atlas, "columns", 1,
            TILEMAP_MAX_ATLAS, false, &descriptor->atlas.columns, name) &&
        tilemap_uint_property(ctx, atlas, "rows", 1,
            TILEMAP_MAX_ATLAS, true, &descriptor->atlas.rows, name);
    JS_FreeValue(ctx, atlas);
    if (valid && descriptor->atlas.rows > 0 &&
        descriptor->atlas.columns * descriptor->atlas.rows >
            ATHENA_TILEMAP_EMPTY) {
        JS_ThrowRangeError(ctx, "%s has more tiles than tile ids", name);
        return 0;
    }
    if (valid)
        /* NULL (no rows, huge atlas, low memory) falls back to dividing. */
        descriptor->atlas.uv = athena_tilemap_atlas_table(&descriptor->atlas);
    descriptor->has_atlas = valid;
    return valid;
}

static JSValue descriptor_ctor(JSContext *ctx, JSValueConst new_target,
    int argc, JSValueConst *argv)
{
    TileMapDescriptor *descriptor;
    JSValue proto, object;

    if (!tilemap_argc(ctx, argc, 1, 1, "TileMap.Descriptor"))
        return JS_EXCEPTION;
    if (!JS_IsObject(argv[0]) || JS_IsArray(ctx, argv[0]))
        return JS_ThrowTypeError(ctx,
            "TileMap.Descriptor options must be an object");
    descriptor = calloc(1, sizeof(*descriptor));
    if (!descriptor)
        return JS_ThrowOutOfMemory(ctx);
    if (!descriptor_textures(ctx, descriptor, argv[0]) ||
        !descriptor_materials(ctx, descriptor, argv[0]) ||
        !descriptor_atlas(ctx, descriptor, argv[0])) {
        descriptor_free(JS_GetRuntime(ctx), descriptor);
        return JS_EXCEPTION;
    }
    proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    if (JS_IsException(proto)) {
        descriptor_free(JS_GetRuntime(ctx), descriptor);
        return proto;
    }
    object = JS_NewObjectProtoClass(ctx, proto, descriptor_class_id);
    JS_FreeValue(ctx, proto);
    if (JS_IsException(object)) {
        descriptor_free(JS_GetRuntime(ctx), descriptor);
        return object;
    }
    JS_SetOpaque(object, descriptor);
    return object;
}

static JSValue descriptor_get(JSContext *ctx, JSValueConst this_val,
    int magic)
{
    TileMapDescriptor *descriptor =
        JS_GetOpaque2(ctx, this_val, descriptor_class_id);
    JSValue textures;
    uint32_t i;

    if (!descriptor)
        return JS_EXCEPTION;
    if (magic == 0)
        return JS_NewUint32(ctx, descriptor->material_count);
    if (magic == 2) {
        JSValue atlas;

        if (!descriptor->has_atlas)
            return JS_UNDEFINED;
        atlas = JS_NewObject(ctx);
        if (JS_IsException(atlas))
            return atlas;
        JS_DefinePropertyValueStr(ctx, atlas, "tileWidth",
            JS_NewUint32(ctx, descriptor->atlas.tile_width), JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, atlas, "tileHeight",
            JS_NewUint32(ctx, descriptor->atlas.tile_height), JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, atlas, "columns",
            JS_NewUint32(ctx, descriptor->atlas.columns), JS_PROP_C_W_E);
        if (descriptor->atlas.rows > 0)
            JS_DefinePropertyValueStr(ctx, atlas, "rows",
                JS_NewUint32(ctx, descriptor->atlas.rows), JS_PROP_C_W_E);
        return atlas;
    }
    textures = JS_NewArray(ctx);
    if (JS_IsException(textures))
        return textures;
    for (i = 0; i < descriptor->texture_count; ++i)
        JS_SetPropertyUint32(ctx, textures, i,
            JS_DupValue(ctx, descriptor->textures[i]));
    return textures;
}

/* ---- Instance ------------------------------------------------------ */

static void instance_sync(TileMapInstance *instance)
{
    if (instance->rendered) {
        athena_tilemap_sync();
        instance->rendered = false;
    }
}

static void instance_finalizer(JSRuntime *rt, JSValue value)
{
    TileMapInstance *instance = JS_GetOpaque(value, instance_class_id);

    if (!instance)
        return;
    /* Queued draws may still read the buffer that is released here. */
    instance_sync(instance);
    JS_FreeValueRT(rt, instance->descriptor);
    JS_FreeValueRT(rt, instance->buffer);
    free(instance->ranges);
    free(instance);
    JS_SetOpaque(value, NULL);
}

static TileMapInstance *instance_this(JSContext *ctx, JSValueConst value)
{
    return JS_GetOpaque2(ctx, value, instance_class_id);
}

/*
 * Wraps `descriptor` and `buffer`, both owned and released on failure, in a
 * new Instance. An undefined `new_target` uses the class prototype.
 */
static JSValue instance_new(JSContext *ctx, JSValueConst new_target,
    JSValue descriptor, JSValue buffer, int kind, TileMapInstance **out)
{
    TileMapInstance *instance = calloc(1, sizeof(*instance));
    JSValue proto, object;

    if (!instance) {
        JS_FreeValue(ctx, descriptor);
        JS_FreeValue(ctx, buffer);
        return JS_ThrowOutOfMemory(ctx);
    }
    instance->descriptor = descriptor;
    instance->buffer = buffer;
    instance->buffer_kind = kind;
    if (JS_IsUndefined(new_target)) {
        object = JS_NewObjectClass(ctx, instance_class_id);
    } else {
        proto = JS_GetPropertyStr(ctx, new_target, "prototype");
        object = JS_IsException(proto) ? proto :
            JS_NewObjectProtoClass(ctx, proto, instance_class_id);
        JS_FreeValue(ctx, proto);
    }
    if (JS_IsException(object)) {
        JS_FreeValue(ctx, instance->descriptor);
        JS_FreeValue(ctx, instance->buffer);
        free(instance);
        return object;
    }
    JS_SetOpaque(object, instance);
    if (out)
        *out = instance;
    return object;
}

static JSValue instance_ctor(JSContext *ctx, JSValueConst new_target,
    int argc, JSValueConst *argv)
{
    AthenaTileSprite *sprites;
    uint32_t count;
    int kind = TILEMAP_STORAGE_UNKNOWN;
    JSValue descriptor, buffer;

    if (!tilemap_argc(ctx, argc, 1, 1, "TileMap.Instance"))
        return JS_EXCEPTION;
    if (!JS_IsObject(argv[0]) || JS_IsArray(ctx, argv[0]))
        return JS_ThrowTypeError(ctx,
            "TileMap.Instance options must be an object");
    descriptor = JS_GetPropertyStr(ctx, argv[0], "descriptor");
    if (JS_IsException(descriptor))
        return descriptor;
    if (!JS_GetOpaque(descriptor, descriptor_class_id)) {
        JS_FreeValue(ctx, descriptor);
        return JS_ThrowTypeError(ctx,
            "TileMap.Instance options.descriptor must be a TileMap.Descriptor");
    }
    buffer = JS_GetPropertyStr(ctx, argv[0], "spriteBuffer");
    if (JS_IsException(buffer) || (!JS_IsUndefined(buffer) &&
        !tilemap_sprite_storage(ctx, buffer, true, &kind, &sprites, &count,
            "TileMap.Instance options.spriteBuffer"))) {
        JS_FreeValue(ctx, descriptor);
        JS_FreeValue(ctx, buffer);
        return JS_EXCEPTION;
    }
    return instance_new(ctx, new_target, descriptor, buffer, kind, NULL);
}

/* Reads an optional non-negative integer option; `*present` tells if set. */
static int tilemap_optional_uint(JSContext *ctx, JSValueConst options,
    const char *property, uint32_t *out, bool *present, const char *name)
{
    JSValue value = JS_GetPropertyStr(ctx, options, property);
    char label[96];
    int valid;

    if (JS_IsException(value))
        return 0;
    *present = !JS_IsUndefined(value);
    if (!*present)
        return 1;
    snprintf(label, sizeof(label), "%s.%s", name, property);
    valid = tilemap_uint(ctx, value, (double)TILEMAP_MAX_SPRITES, out, label);
    JS_FreeValue(ctx, value);
    return valid;
}

static JSValue instance_render(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    const char *name = "TileMap.Instance.render";
    TileMapInstance *instance = instance_this(ctx, this_val);
    TileMapDescriptor *descriptor;
    AthenaTileSprite *sprites;
    AthenaTileRange manual;
    const AthenaTileRange *ranges = NULL;
    uint32_t range_count = 0;
    uint32_t count;
    uint32_t first = 0;
    uint32_t span = 0;
    bool has_first = false;
    bool has_span = false;
    bool cull = true;
    uint32_t i;
    float x, y;

    if (!instance || !tilemap_argc(ctx, argc, 2, 3, name) ||
        !tilemap_float(ctx, argv[0], &x, "TileMap.Instance.render x") ||
        !tilemap_float(ctx, argv[1], &y, "TileMap.Instance.render y"))
        return JS_EXCEPTION;
    /* Read options before the buffer: a getter could replace it. */
    if (argc == 3 && !JS_IsUndefined(argv[2])) {
        JSValue value;

        if (!JS_IsObject(argv[2]) || JS_IsArray(ctx, argv[2]))
            return JS_ThrowTypeError(ctx,
                "TileMap.Instance.render options must be an object");
        if (!tilemap_optional_uint(ctx, argv[2], "first", &first, &has_first,
                "TileMap.Instance.render options") ||
            !tilemap_optional_uint(ctx, argv[2], "count", &span, &has_span,
                "TileMap.Instance.render options"))
            return JS_EXCEPTION;
        value = JS_GetPropertyStr(ctx, argv[2], "cull");
        if (JS_IsException(value))
            return value;
        if (!JS_IsUndefined(value)) {
            if (!JS_IsBool(value)) {
                JS_FreeValue(ctx, value);
                return JS_ThrowTypeError(ctx,
                    "TileMap.Instance.render options.cull must be a boolean");
            }
            cull = JS_ToBool(ctx, value) != 0;
        }
        JS_FreeValue(ctx, value);
    }
    instance->last_drawn = 0;
    if (JS_IsUndefined(instance->buffer))
        return JS_UNDEFINED;
    if (!tilemap_sprite_storage(ctx, instance->buffer, true,
        &instance->buffer_kind, &sprites, &count,
        "TileMap.Instance sprite buffer"))
        return JS_EXCEPTION;

    if (has_first || has_span) {
        if (first > count)
            return JS_ThrowRangeError(ctx,
                "%s options.first is past the sprite buffer (%u sprites)",
                name, (unsigned int)count);
        if (!has_span)
            span = count - first;
        if (span > count - first)
            return JS_ThrowRangeError(ctx,
                "%s options.count exceeds the sprite buffer (%u sprites)",
                name, (unsigned int)count);
        if (span == 0)
            return JS_UNDEFINED;
        manual.first = first;
        manual.count = span;
        ranges = &manual;
        range_count = 1;
    } else if (instance->has_grid && cull) {
        range_count = athena_tilemap_visible_ranges(&instance->grid, x, y,
            instance->ranges);
        if (range_count == 0)
            return JS_UNDEFINED;
        ranges = instance->ranges;
    }

    descriptor = JS_GetOpaque(instance->descriptor, descriptor_class_id);
    /* Resolve textures now: an Image may have been freed or reloaded. */
    for (i = 0; i < descriptor->texture_count; ++i) {
        AthenaImage *image = athena_image_peek(descriptor->textures[i]);
        descriptor->surfaces[i] = athena_image_is_loaded(image) ?
            image->surface : NULL;
    }
    instance->last_drawn = athena_tilemap_render(descriptor->materials,
        descriptor->material_count, descriptor->surfaces,
        descriptor->texture_count, sprites, count, ranges, range_count, x, y);
    instance->rendered = true;
    return JS_UNDEFINED;
}

/* Resolves the instance buffer and checks sprites [first, first + count). */
static AthenaTileSprite *instance_range(JSContext *ctx,
    TileMapInstance *instance, uint32_t first, uint32_t count,
    const char *name)
{
    AthenaTileSprite *sprites;
    uint32_t total;

    if (JS_IsUndefined(instance->buffer)) {
        JS_ThrowTypeError(ctx, "%s requires a sprite buffer", name);
        return NULL;
    }
    if (!tilemap_sprite_storage(ctx, instance->buffer, true,
            &instance->buffer_kind, &sprites, &total,
            "TileMap.Instance sprite buffer"))
        return NULL;
    if (first > total || count > total - first) {
        JS_ThrowRangeError(ctx, "%s range exceeds the sprite buffer (%u sprites)",
            name, (unsigned int)total);
        return NULL;
    }
    return sprites;
}

static JSValue instance_translate(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    const char *name = "TileMap.Instance.translate";
    TileMapInstance *instance = instance_this(ctx, this_val);
    AthenaTileSprite *sprites;
    uint32_t first, count;
    float dx, dy;

    if (!instance || !tilemap_argc(ctx, argc, 4, 4, name) ||
        !tilemap_uint(ctx, argv[0], (double)TILEMAP_MAX_SPRITES, &first,
            "TileMap.Instance.translate first") ||
        !tilemap_uint(ctx, argv[1], (double)TILEMAP_MAX_SPRITES, &count,
            "TileMap.Instance.translate count") ||
        !tilemap_float(ctx, argv[2], &dx, "TileMap.Instance.translate dx") ||
        !tilemap_float(ctx, argv[3], &dy, "TileMap.Instance.translate dy"))
        return JS_EXCEPTION;
    sprites = instance_range(ctx, instance, first, count, name);
    if (!sprites)
        return JS_EXCEPTION;
    athena_tilemap_translate(sprites, first, count, dx, dy);
    return JS_UNDEFINED;
}

static JSValue instance_set_color(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    const char *name = "TileMap.Instance.setColor";
    TileMapInstance *instance = instance_this(ctx, this_val);
    AthenaTileSprite *sprites;
    uint32_t first, count, r, g, b, a = 0x80;

    if (!instance || !tilemap_argc(ctx, argc, 5, 6, name) ||
        !tilemap_uint(ctx, argv[0], (double)TILEMAP_MAX_SPRITES, &first,
            "TileMap.Instance.setColor first") ||
        !tilemap_uint(ctx, argv[1], (double)TILEMAP_MAX_SPRITES, &count,
            "TileMap.Instance.setColor count") ||
        !tilemap_uint(ctx, argv[2], 255.0, &r, "TileMap.Instance.setColor r") ||
        !tilemap_uint(ctx, argv[3], 255.0, &g, "TileMap.Instance.setColor g") ||
        !tilemap_uint(ctx, argv[4], 255.0, &b, "TileMap.Instance.setColor b") ||
        (argc == 6 &&
            !tilemap_uint(ctx, argv[5], 255.0, &a, "TileMap.Instance.setColor a")))
        return JS_EXCEPTION;
    sprites = instance_range(ctx, instance, first, count, name);
    if (!sprites)
        return JS_EXCEPTION;
    athena_tilemap_set_color(sprites, first, count, r, g, b, a);
    return JS_UNDEFINED;
}

/*
 * Reads tile ids from a Uint16Array/Int16Array, used in place, or from an
 * array of numbers, copied into `*copy` for the caller to free. Every id is
 * checked against the atlas before anything is written.
 */
static int tilemap_tile_ids(JSContext *ctx, JSValueConst value,
    const AthenaTileAtlas *atlas, const uint16_t **ids, uint16_t **copy,
    uint32_t *count, const char *name)
{
    uint32_t limit = atlas->rows ? atlas->columns * atlas->rows : 0;
    uint32_t length;
    uint32_t i;

    *copy = NULL;
    if (JS_IsArray(ctx, value)) {
        if (!tilemap_array_length(ctx, value, &length))
            return 0;
        if (length > TILEMAP_MAX_SPRITES) {
            JS_ThrowRangeError(ctx, "%s has too many tile ids", name);
            return 0;
        }
        *copy = malloc((length ? length : 1) * sizeof(**copy));
        if (!*copy) {
            JS_ThrowOutOfMemory(ctx);
            return 0;
        }
        for (i = 0; i < length; ++i) {
            JSValue item = JS_GetPropertyUint32(ctx, value, i);
            uint32_t id = 0;
            int valid = !JS_IsException(item) &&
                tilemap_uint(ctx, item, 65535.0, &id, name);

            JS_FreeValue(ctx, item);
            if (!valid) {
                free(*copy);
                *copy = NULL;
                return 0;
            }
            (*copy)[i] = (uint16_t)id;
        }
        *ids = *copy;
    } else {
        size_t offset = 0, byte_length = 0, element = 0, buffer_length = 0;
        uint8_t *data = NULL;
        JSValue array_buffer = JS_IsObject(value) ?
            JS_GetTypedArrayBuffer(ctx, value, &offset, &byte_length,
                &element) : JS_EXCEPTION;

        if (JS_IsException(array_buffer)) {
            JS_FreeValue(ctx, JS_GetException(ctx));
        } else {
            data = JS_GetArrayBuffer(ctx, &buffer_length, array_buffer);
            JS_FreeValue(ctx, array_buffer);
            if (!data)
                JS_FreeValue(ctx, JS_GetException(ctx));
        }
        if (!data || element != sizeof(uint16_t) ||
            offset + byte_length > buffer_length) {
            JS_ThrowTypeError(ctx,
                "%s must be a Uint16Array or an array of tile ids", name);
            return 0;
        }
        *ids = (const uint16_t *)(data + offset);
        length = (uint32_t)(byte_length / sizeof(uint16_t));
    }
    for (i = 0; limit && i < length; ++i) {
        if ((*ids)[i] != ATHENA_TILEMAP_EMPTY && (*ids)[i] >= limit) {
            free(*copy);
            *copy = NULL;
            JS_ThrowRangeError(ctx,
                "%s[%u] = %u is not a tile of the atlas (%u tiles)", name,
                (unsigned int)i, (unsigned int)(*ids)[i],
                (unsigned int)limit);
            return 0;
        }
    }
    *count = length;
    return 1;
}

static JSValue instance_set_tiles(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    const char *name = "TileMap.Instance.setTiles";
    TileMapInstance *instance = instance_this(ctx, this_val);
    TileMapDescriptor *descriptor;
    AthenaTileSprite *sprites;
    const uint16_t *ids;
    uint16_t *copy;
    uint32_t first, count;
    float width, height;

    if (!instance || !tilemap_argc(ctx, argc, 2, 2, name) ||
        !tilemap_uint(ctx, argv[0], (double)TILEMAP_MAX_SPRITES, &first,
            "TileMap.Instance.setTiles first"))
        return JS_EXCEPTION;
    descriptor = JS_GetOpaque(instance->descriptor, descriptor_class_id);
    if (!descriptor->has_atlas)
        return JS_ThrowTypeError(ctx,
            "%s requires a descriptor created with an atlas", name);
    /* Read ids before resolving the buffer: array getters run user code. */
    if (!tilemap_tile_ids(ctx, argv[1], &descriptor->atlas, &ids, &copy,
            &count, "TileMap.Instance.setTiles tiles"))
        return JS_EXCEPTION;
    sprites = instance_range(ctx, instance, first, count, name);
    if (sprites) {
        width = instance->has_grid ? instance->grid.tile_width :
            (float)descriptor->atlas.tile_width;
        height = instance->has_grid ? instance->grid.tile_height :
            (float)descriptor->atlas.tile_height;
        athena_tilemap_set_tiles(sprites, first, ids, count,
            &descriptor->atlas, width, height);
    }
    free(copy);
    return sprites ? JS_UNDEFINED : JS_EXCEPTION;
}

static int tilemap_optional_float(JSContext *ctx, JSValueConst options,
    const char *property, float *out, bool positive, const char *name)
{
    JSValue value = JS_GetPropertyStr(ctx, options, property);
    char label[96];
    int valid;

    if (JS_IsException(value))
        return 0;
    if (JS_IsUndefined(value))
        return 1;
    snprintf(label, sizeof(label), "%s.%s", name, property);
    valid = tilemap_float(ctx, value, out, label);
    JS_FreeValue(ctx, value);
    if (valid && positive && !(*out > 0.0f)) {
        JS_ThrowRangeError(ctx, "%s must be positive", label);
        return 0;
    }
    return valid;
}

static JSValue instance_from_grid(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    const char *name = "TileMap.Instance.fromGrid options";
    TileMapDescriptor *descriptor;
    TileMapInstance *instance = NULL;
    AthenaTileGrid grid;
    AthenaTileSprite *sprites;
    AthenaTileRange *ranges;
    const uint16_t *ids = NULL;
    uint16_t *copy = NULL;
    uint32_t id_count = 0;
    uint32_t total;
    float zindex = 0.0f;
    JSValue descriptor_value, tiles, buffer, object;

    if (!tilemap_argc(ctx, argc, 1, 1, "TileMap.Instance.fromGrid"))
        return JS_EXCEPTION;
    if (!JS_IsObject(argv[0]) || JS_IsArray(ctx, argv[0]))
        return JS_ThrowTypeError(ctx,
            "TileMap.Instance.fromGrid options must be an object");
    descriptor_value = JS_GetPropertyStr(ctx, argv[0], "descriptor");
    if (JS_IsException(descriptor_value))
        return descriptor_value;
    descriptor = JS_GetOpaque(descriptor_value, descriptor_class_id);
    if (!descriptor || !descriptor->has_atlas) {
        JS_FreeValue(ctx, descriptor_value);
        return JS_ThrowTypeError(ctx,
            "%s.descriptor must be a TileMap.Descriptor with an atlas", name);
    }
    memset(&grid, 0, sizeof(grid));
    grid.tile_width = (float)descriptor->atlas.tile_width;
    grid.tile_height = (float)descriptor->atlas.tile_height;
    if (!tilemap_uint_property(ctx, argv[0], "columns", 1, 65535, false,
            &grid.columns, name) ||
        !tilemap_uint_property(ctx, argv[0], "rows", 1, 65535, false,
            &grid.rows, name) ||
        !tilemap_optional_float(ctx, argv[0], "tileWidth", &grid.tile_width,
            true, name) ||
        !tilemap_optional_float(ctx, argv[0], "tileHeight",
            &grid.tile_height, true, name) ||
        !tilemap_optional_float(ctx, argv[0], "zindex", &zindex, false,
            name)) {
        JS_FreeValue(ctx, descriptor_value);
        return JS_EXCEPTION;
    }
    if ((double)grid.columns * grid.rows > (double)TILEMAP_MAX_SPRITES) {
        JS_FreeValue(ctx, descriptor_value);
        return JS_ThrowRangeError(ctx, "%s describe too many cells", name);
    }
    total = grid.columns * grid.rows;

    tiles = JS_GetPropertyStr(ctx, argv[0], "tiles");
    if (JS_IsException(tiles) || (!JS_IsUndefined(tiles) &&
        !tilemap_tile_ids(ctx, tiles, &descriptor->atlas, &ids, &copy,
            &id_count, "TileMap.Instance.fromGrid options.tiles"))) {
        JS_FreeValue(ctx, tiles);
        JS_FreeValue(ctx, descriptor_value);
        return JS_EXCEPTION;
    }
    if (ids && id_count != total) {
        free(copy);
        JS_FreeValue(ctx, tiles);
        JS_FreeValue(ctx, descriptor_value);
        return JS_ThrowRangeError(ctx,
            "%s.tiles has %u ids but the grid has %u cells", name,
            (unsigned int)id_count, (unsigned int)total);
    }

    sprites = athena_tilemap_buffer_alloc(total);
    ranges = malloc(grid.rows * sizeof(*ranges));
    if (!sprites || !ranges) {
        free(sprites);
        free(ranges);
        free(copy);
        JS_FreeValue(ctx, tiles);
        JS_FreeValue(ctx, descriptor_value);
        return JS_ThrowOutOfMemory(ctx);
    }
    athena_tilemap_fill_grid(sprites, &grid, ids, &descriptor->atlas, zindex);
    free(copy);
    /* `ids` may point into `tiles`; it is not used past this point. */
    JS_FreeValue(ctx, tiles);

    buffer = tilemap_new_buffer(ctx, sprites, total);
    if (JS_IsException(buffer)) {
        free(ranges);
        JS_FreeValue(ctx, descriptor_value);
        return buffer;
    }
    object = instance_new(ctx, JS_UNDEFINED, descriptor_value, buffer,
        TILEMAP_STORAGE_ARRAY_BUFFER, &instance);
    if (JS_IsException(object)) {
        free(ranges);
        return object;
    }
    instance->has_grid = true;
    instance->grid = grid;
    instance->ranges = ranges;
    return object;
}

static JSValue instance_replace_buffer(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    TileMapInstance *instance = instance_this(ctx, this_val);
    AthenaTileSprite *sprites;
    uint32_t count;
    int kind = TILEMAP_STORAGE_UNKNOWN;

    if (!instance || !tilemap_argc(ctx, argc, 1, 1,
            "TileMap.Instance.replaceSpriteBuffer") ||
        !tilemap_sprite_storage(ctx, argv[0], true, &kind, &sprites, &count,
            "TileMap.Instance.replaceSpriteBuffer buffer"))
        return JS_EXCEPTION;
    instance_sync(instance);
    JS_FreeValue(ctx, instance->buffer);
    instance->buffer = JS_DupValue(ctx, argv[0]);
    instance->buffer_kind = kind;
    return JS_UNDEFINED;
}

static JSValue instance_get_buffer(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    TileMapInstance *instance = instance_this(ctx, this_val);

    if (!instance || !tilemap_argc(ctx, argc, 0, 0,
            "TileMap.Instance.getSpriteBuffer"))
        return JS_EXCEPTION;
    return JS_DupValue(ctx, instance->buffer);
}

static JSValue instance_update(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    TileMapInstance *instance = instance_this(ctx, this_val);
    AthenaTileSprite *destination;
    AthenaTileSprite *source;
    uint32_t destination_count;
    uint32_t source_count;
    uint32_t offset;
    uint32_t count;

    if (!instance || !tilemap_argc(ctx, argc, 2, 3,
            "TileMap.Instance.updateSprites"))
        return JS_EXCEPTION;
    if (JS_IsUndefined(instance->buffer))
        return JS_ThrowTypeError(ctx,
            "TileMap.Instance.updateSprites requires a sprite buffer");
    if (!tilemap_sprite_storage(ctx, instance->buffer, true,
            &instance->buffer_kind, &destination, &destination_count,
            "TileMap.Instance sprite buffer") ||
        !tilemap_uint(ctx, argv[0], (double)destination_count - 1.0, &offset,
            "TileMap.Instance.updateSprites dstOffset") ||
        !tilemap_sprite_storage(ctx, argv[1], false, NULL, &source,
            &source_count, "TileMap.Instance.updateSprites source"))
        return JS_EXCEPTION;
    count = source_count;
    if (argc == 3 && !tilemap_uint(ctx, argv[2], (double)source_count, &count,
            "TileMap.Instance.updateSprites count"))
        return JS_EXCEPTION;
    if (count > destination_count - offset)
        return JS_ThrowRangeError(ctx,
            "TileMap.Instance.updateSprites exceeds the sprite buffer");
    memmove(&destination[offset], source, (size_t)count * sizeof(*source));
    return JS_UNDEFINED;
}

static JSValue instance_sprite_count(JSContext *ctx, JSValueConst this_val)
{
    TileMapInstance *instance = instance_this(ctx, this_val);
    AthenaTileSprite *sprites;
    uint32_t count = 0;

    if (!instance)
        return JS_EXCEPTION;
    if (!JS_IsUndefined(instance->buffer) &&
        !tilemap_sprite_storage(ctx, instance->buffer, true,
            &instance->buffer_kind, &sprites, &count,
            "TileMap.Instance sprite buffer"))
        return JS_EXCEPTION;
    return JS_NewUint32(ctx, count);
}

static JSValue instance_descriptor(JSContext *ctx, JSValueConst this_val)
{
    TileMapInstance *instance = instance_this(ctx, this_val);

    if (!instance)
        return JS_EXCEPTION;
    return JS_DupValue(ctx, instance->descriptor);
}

static JSValue instance_last_draw_count(JSContext *ctx, JSValueConst this_val)
{
    TileMapInstance *instance = instance_this(ctx, this_val);

    if (!instance)
        return JS_EXCEPTION;
    return JS_NewUint32(ctx, instance->last_drawn);
}

static JSValue instance_grid(JSContext *ctx, JSValueConst this_val)
{
    TileMapInstance *instance = instance_this(ctx, this_val);
    JSValue grid;

    if (!instance)
        return JS_EXCEPTION;
    if (!instance->has_grid)
        return JS_UNDEFINED;
    grid = JS_NewObject(ctx);
    if (JS_IsException(grid))
        return grid;
    JS_DefinePropertyValueStr(ctx, grid, "columns",
        JS_NewUint32(ctx, instance->grid.columns), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, grid, "rows",
        JS_NewUint32(ctx, instance->grid.rows), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, grid, "tileWidth",
        JS_NewFloat64(ctx, instance->grid.tile_width), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, grid, "tileHeight",
        JS_NewFloat64(ctx, instance->grid.tile_height), JS_PROP_C_W_E);
    return grid;
}

/* ---- SpriteBuffer and module functions ----------------------------- */

static JSValue sprite_buffer_create(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    AthenaTileSprite *sprites;
    uint32_t count;

    if (!tilemap_argc(ctx, argc, 1, 1, "TileMap.SpriteBuffer.create") ||
        !tilemap_uint(ctx, argv[0], (double)TILEMAP_MAX_SPRITES, &count,
            "TileMap.SpriteBuffer.create count"))
        return JS_EXCEPTION;
    if (count == 0)
        return JS_ThrowRangeError(ctx,
            "TileMap.SpriteBuffer.create count must be at least 1");
    sprites = athena_tilemap_buffer_alloc(count);
    if (!sprites)
        return JS_ThrowOutOfMemory(ctx);
    return tilemap_new_buffer(ctx, sprites, count);
}

static int sprite_from_object(JSContext *ctx, JSValueConst object,
    AthenaTileSprite *sprite, uint32_t index)
{
    unsigned int i;
    char name[64];

    if (!JS_IsObject(object) || JS_IsArray(ctx, object)) {
        JS_ThrowTypeError(ctx,
            "TileMap.SpriteBuffer.fromObjects entry %u must be an object",
            (unsigned int)index);
        return 0;
    }
    for (i = 0; i < countof(sprite_fields); ++i) {
        const TileMapSpriteField *field = &sprite_fields[i];
        uint8_t *target = (uint8_t *)sprite + field->offset;
        JSValue value = JS_GetPropertyStr(ctx, object, field->name);
        int valid;

        if (JS_IsException(value))
            return 0;
        if (JS_IsUndefined(value))
            continue;
        snprintf(name, sizeof(name), "TileMap.SpriteBuffer.fromObjects[%u].%s",
            (unsigned int)index, field->name);
        if (field->color)
            valid = tilemap_uint(ctx, value, 255.0, (uint32_t *)target, name);
        else
            valid = tilemap_float(ctx, value, (float *)target, name);
        JS_FreeValue(ctx, value);
        if (!valid)
            return 0;
    }
    return 1;
}

static JSValue sprite_buffer_from_objects(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv)
{
    AthenaTileSprite *sprites;
    uint32_t count;
    uint32_t i;

    if (!tilemap_argc(ctx, argc, 1, 1, "TileMap.SpriteBuffer.fromObjects"))
        return JS_EXCEPTION;
    if (!JS_IsArray(ctx, argv[0]))
        return JS_ThrowTypeError(ctx,
            "TileMap.SpriteBuffer.fromObjects expects an array");
    if (!tilemap_array_length(ctx, argv[0], &count))
        return JS_EXCEPTION;
    if (count == 0 || count > TILEMAP_MAX_SPRITES)
        return JS_ThrowRangeError(ctx,
            "TileMap.SpriteBuffer.fromObjects needs between 1 and %lu sprites",
            (unsigned long)TILEMAP_MAX_SPRITES);
    sprites = athena_tilemap_buffer_alloc(count);
    if (!sprites)
        return JS_ThrowOutOfMemory(ctx);
    for (i = 0; i < count; ++i) {
        JSValue object = JS_GetPropertyUint32(ctx, argv[0], i);
        int valid;

        /* Neutral color: 0x80 is 1.0 when the GS modulates a texture. */
        sprites[i].r = sprites[i].g = sprites[i].b = sprites[i].a = 0x80;
        valid = !JS_IsException(object) &&
            sprite_from_object(ctx, object, &sprites[i], i);
        JS_FreeValue(ctx, object);
        if (!valid) {
            free(sprites);
            return JS_EXCEPTION;
        }
    }
    return tilemap_new_buffer(ctx, sprites, count);
}

static JSValue tilemap_set_camera(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    float x, y;

    if (!tilemap_argc(ctx, argc, 2, 2, "TileMap.setCamera") ||
        !tilemap_float(ctx, argv[0], &x, "TileMap.setCamera x") ||
        !tilemap_float(ctx, argv[1], &y, "TileMap.setCamera y"))
        return JS_EXCEPTION;
    athena_tilemap_set_camera(x, y);
    return JS_UNDEFINED;
}

static JSValue tilemap_get_camera(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    JSValue camera;
    float x, y;

    if (!tilemap_argc(ctx, argc, 0, 0, "TileMap.getCamera"))
        return JS_EXCEPTION;
    athena_tilemap_get_camera(&x, &y);
    camera = JS_NewObject(ctx);
    if (JS_IsException(camera))
        return camera;
    JS_DefinePropertyValueStr(ctx, camera, "x", JS_NewFloat64(ctx, x),
        JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, camera, "y", JS_NewFloat64(ctx, y),
        JS_PROP_C_W_E);
    return camera;
}

static int tilemap_bool_option(JSContext *ctx, JSValueConst options,
    const char *name, bool *out)
{
    JSValue value = JS_GetPropertyStr(ctx, options, name);

    if (JS_IsException(value))
        return 0;
    if (!JS_IsUndefined(value)) {
        if (!JS_IsBool(value)) {
            JS_FreeValue(ctx, value);
            JS_ThrowTypeError(ctx, "TileMap.setDiagnostics %s must be a boolean",
                name);
            return 0;
        }
        *out = JS_ToBool(ctx, value) != 0;
    }
    JS_FreeValue(ctx, value);
    return 1;
}

static JSValue tilemap_set_diagnostics(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    AthenaTileDiagnostics diagnostics;
    JSValue value;

    if (!tilemap_argc(ctx, argc, 1, 1, "TileMap.setDiagnostics"))
        return JS_EXCEPTION;
    if (!JS_IsObject(argv[0]) || JS_IsArray(ctx, argv[0]))
        return JS_ThrowTypeError(ctx,
            "TileMap.setDiagnostics options must be an object");
    /* Unspecified switches keep their current value. */
    athena_tilemap_get_diagnostics(&diagnostics);
    if (!tilemap_bool_option(ctx, argv[0], "flushEachBatch",
            &diagnostics.flush_each_batch) ||
        !tilemap_bool_option(ctx, argv[0], "fullCacheFlush",
            &diagnostics.full_cache_flush))
        return JS_EXCEPTION;
    value = JS_GetPropertyStr(ctx, argv[0], "batchSize");
    if (JS_IsException(value))
        return value;
    if (!JS_IsUndefined(value)) {
        double number = 0.0;

        if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &number, value) ||
            !(number >= 1.0 && number <= ATHENA_TILEMAP_MAX_BATCH) ||
            number != floor(number)) {
            JS_FreeValue(ctx, value);
            return JS_ThrowRangeError(ctx,
                "TileMap.setDiagnostics batchSize must be an integer between 1 and %d",
                ATHENA_TILEMAP_MAX_BATCH);
        }
        JS_FreeValue(ctx, value);
        diagnostics.batch_size = (uint32_t)number;
    }
    athena_tilemap_set_diagnostics(&diagnostics);
    return JS_UNDEFINED;
}

static JSValue tilemap_get_diagnostics(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    AthenaTileDiagnostics diagnostics;
    JSValue object;

    if (!tilemap_argc(ctx, argc, 0, 0, "TileMap.getDiagnostics"))
        return JS_EXCEPTION;
    athena_tilemap_get_diagnostics(&diagnostics);
    object = JS_NewObject(ctx);
    if (JS_IsException(object))
        return object;
    JS_DefinePropertyValueStr(ctx, object, "flushEachBatch",
        JS_NewBool(ctx, diagnostics.flush_each_batch), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, object, "fullCacheFlush",
        JS_NewBool(ctx, diagnostics.full_cache_flush), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, object, "batchSize",
        JS_NewUint32(ctx, diagnostics.batch_size), JS_PROP_C_W_E);
    return object;
}

static JSValue tilemap_layout_value(JSContext *ctx)
{
    const AthenaTileLayout *layout = athena_tilemap_layout();
    JSValue object = JS_NewObject(ctx);
    JSValue offsets = JS_NewObject(ctx);

    if (JS_IsException(object) || JS_IsException(offsets)) {
        JS_FreeValue(ctx, object);
        JS_FreeValue(ctx, offsets);
        return JS_EXCEPTION;
    }
    JS_DefinePropertyValueStr(ctx, offsets, "x",
        JS_NewUint32(ctx, layout->offset_x), JS_PROP_ENUMERABLE);
    JS_DefinePropertyValueStr(ctx, offsets, "y",
        JS_NewUint32(ctx, layout->offset_y), JS_PROP_ENUMERABLE);
    JS_DefinePropertyValueStr(ctx, offsets, "w",
        JS_NewUint32(ctx, layout->offset_w), JS_PROP_ENUMERABLE);
    JS_DefinePropertyValueStr(ctx, offsets, "h",
        JS_NewUint32(ctx, layout->offset_h), JS_PROP_ENUMERABLE);
    JS_DefinePropertyValueStr(ctx, offsets, "u1",
        JS_NewUint32(ctx, layout->offset_u1), JS_PROP_ENUMERABLE);
    JS_DefinePropertyValueStr(ctx, offsets, "v1",
        JS_NewUint32(ctx, layout->offset_v1), JS_PROP_ENUMERABLE);
    JS_DefinePropertyValueStr(ctx, offsets, "u2",
        JS_NewUint32(ctx, layout->offset_u2), JS_PROP_ENUMERABLE);
    JS_DefinePropertyValueStr(ctx, offsets, "v2",
        JS_NewUint32(ctx, layout->offset_v2), JS_PROP_ENUMERABLE);
    JS_DefinePropertyValueStr(ctx, offsets, "r",
        JS_NewUint32(ctx, layout->offset_r), JS_PROP_ENUMERABLE);
    JS_DefinePropertyValueStr(ctx, offsets, "g",
        JS_NewUint32(ctx, layout->offset_g), JS_PROP_ENUMERABLE);
    JS_DefinePropertyValueStr(ctx, offsets, "b",
        JS_NewUint32(ctx, layout->offset_b), JS_PROP_ENUMERABLE);
    JS_DefinePropertyValueStr(ctx, offsets, "a",
        JS_NewUint32(ctx, layout->offset_a), JS_PROP_ENUMERABLE);
    JS_DefinePropertyValueStr(ctx, offsets, "zindex",
        JS_NewUint32(ctx, layout->offset_zindex), JS_PROP_ENUMERABLE);
    JS_DefinePropertyValueStr(ctx, object, "stride",
        JS_NewUint32(ctx, layout->stride), JS_PROP_ENUMERABLE);
    JS_DefinePropertyValueStr(ctx, object, "offsets", offsets,
        JS_PROP_ENUMERABLE);
    return object;
}

static JSClassDef descriptor_class = {
    "TileMapDescriptor",
    .finalizer = descriptor_finalizer,
};

static JSClassDef instance_class = {
    "TileMapInstance",
    .finalizer = instance_finalizer,
};

static const JSCFunctionListEntry descriptor_proto[] = {
    JS_CGETSET_MAGIC_DEF("materialCount", descriptor_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("textures", descriptor_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("atlas", descriptor_get, NULL, 2),
};

static const JSCFunctionListEntry instance_proto[] = {
    JS_CFUNC_DEF("render", 3, instance_render),
    JS_CFUNC_DEF("replaceSpriteBuffer", 1, instance_replace_buffer),
    JS_CFUNC_DEF("getSpriteBuffer", 0, instance_get_buffer),
    JS_CFUNC_DEF("updateSprites", 3, instance_update),
    JS_CFUNC_DEF("translate", 4, instance_translate),
    JS_CFUNC_DEF("setColor", 6, instance_set_color),
    JS_CFUNC_DEF("setTiles", 2, instance_set_tiles),
    JS_CGETSET_DEF("spriteCount", instance_sprite_count, NULL),
    JS_CGETSET_DEF("descriptor", instance_descriptor, NULL),
    JS_CGETSET_DEF("lastDrawCount", instance_last_draw_count, NULL),
    JS_CGETSET_DEF("grid", instance_grid, NULL),
};

static const JSCFunctionListEntry instance_static[] = {
    JS_CFUNC_DEF("fromGrid", 1, instance_from_grid),
};

static const JSCFunctionListEntry sprite_buffer_funcs[] = {
    JS_CFUNC_DEF("create", 1, sprite_buffer_create),
    JS_CFUNC_DEF("fromObjects", 1, sprite_buffer_from_objects),
};

static const JSCFunctionListEntry tilemap_funcs[] = {
    JS_CFUNC_DEF("setCamera", 2, tilemap_set_camera),
    JS_CFUNC_DEF("getCamera", 0, tilemap_get_camera),
    JS_CFUNC_DEF("setDiagnostics", 1, tilemap_set_diagnostics),
    JS_CFUNC_DEF("getDiagnostics", 0, tilemap_get_diagnostics),
    JS_PROP_INT32_DEF("EMPTY", ATHENA_TILEMAP_EMPTY, 0),
};

static JSValue tilemap_class(JSContext *ctx, JSClassID *class_id,
    JSClassDef *class_def, JSCFunction *ctor, const char *name,
    const JSCFunctionListEntry *proto_funcs, int proto_count)
{
    JSValue proto, constructor;

    JS_NewClassID(class_id);
    JS_NewClass(JS_GetRuntime(ctx), *class_id, class_def);
    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, proto_funcs, proto_count);
    JS_SetClassProto(ctx, *class_id, proto);
    constructor = JS_NewCFunction2(ctx, ctor, name, 1, JS_CFUNC_constructor,
        0);
    JS_SetConstructor(ctx, constructor, proto);
    return constructor;
}

static int tilemap_module_init(JSContext *ctx, JSModuleDef *module)
{
    JSValue sprite_buffer, layout, instance;

    layout = tilemap_layout_value(ctx);
    if (JS_IsException(layout))
        return -1;
    sprite_buffer = JS_NewObject(ctx);
    if (JS_IsException(sprite_buffer)) {
        JS_FreeValue(ctx, layout);
        return -1;
    }
    JS_SetPropertyFunctionList(ctx, sprite_buffer, sprite_buffer_funcs,
        countof(sprite_buffer_funcs));

    JS_SetModuleExport(ctx, module, "Descriptor",
        tilemap_class(ctx, &descriptor_class_id, &descriptor_class,
            descriptor_ctor, "Descriptor", descriptor_proto,
            countof(descriptor_proto)));
    instance = tilemap_class(ctx, &instance_class_id, &instance_class,
        instance_ctor, "Instance", instance_proto, countof(instance_proto));
    JS_SetPropertyFunctionList(ctx, instance, instance_static,
        countof(instance_static));
    JS_SetModuleExport(ctx, module, "Instance", instance);
    JS_SetModuleExport(ctx, module, "SpriteBuffer", sprite_buffer);
    JS_SetModuleExport(ctx, module, "layout", layout);
    return JS_SetModuleExportList(ctx, module, tilemap_funcs,
        countof(tilemap_funcs));
}

JSModuleDef *athena_tilemap_init(JSContext *ctx)
{
    JSModuleDef *module = athena_push_module(ctx, tilemap_module_init,
        tilemap_funcs, countof(tilemap_funcs), "TileMap");

    if (!module)
        return NULL;
    JS_AddModuleExport(ctx, module, "Descriptor");
    JS_AddModuleExport(ctx, module, "Instance");
    JS_AddModuleExport(ctx, module, "SpriteBuffer");
    JS_AddModuleExport(ctx, module, "layout");
    return module;
}
