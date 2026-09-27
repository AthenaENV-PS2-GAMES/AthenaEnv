/*
 * Host stand-ins for what the Sprite binding needs from the Image and
 * TileMap modules, whose real bindings need the GS: an Image class whose
 * surfaces are plain structs (a path "missing*" never loads), draws recorded
 * for the script (__spriteDraws), and a TileMap module with Instance,
 * SpriteBuffer.create() and layout over real AthenaTileSprite records.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ath_env.h>
#include <athena/image.h>
#include <athena/js/image.h>
#include <athena/js/tilemap.h>
#include <athena/tilemap.h>

static JSClassID host_image_class_id;
static JSClassID host_instance_class_id;

/* ---- Image --------------------------------------------------------------- */

typedef struct {
    AthenaImage image;
    GSSURFACE surface;
} HostImage;

static int draw_count, list_calls, list_sprites, quad_count;
static float last_quad[16];
static float last_draw[10];

AthenaImage *athena_image_peek(JSValueConst value) {
    HostImage *host = JS_GetOpaque(value, host_image_class_id);
    return host ? &host->image : NULL;
}

AthenaImage *athena_image_from_value(JSContext *ctx, JSValueConst value) {
    HostImage *host = JS_GetOpaque2(ctx, value, host_image_class_id);
    return host ? &host->image : NULL;
}

bool athena_image_is_loaded(const AthenaImage *image) {
    return image && image->loaded && image->surface;
}

void athena_image_draw(AthenaImage *image, float x, float y, float width,
    float height, float startx, float starty, float endx, float endy,
    float angle, uint32_t color) {
    const float values[10] = { x, y, width, height, startx, starty, endx, endy,
        angle, (float)(color & 0xFF) };

    draw_count++;
    memcpy(last_draw, values, sizeof(values));
}

/* Records each list call, and the last sprite of it as the last draw. */
void athena_image_draw_list(AthenaImage *image, float x, float y, prim_tex_sprite *list,
    int count) {
    const prim_tex_sprite *last;

    if (!athena_image_is_loaded(image) || count <= 0)
        return;
    last = &list[count - 1];
    {
        const float values[10] = { x + last->x, y + last->y, last->w, last->h, last->u1,
            last->v1, last->u2, last->v2, 0.0f, (float)(last->rgba & 0xFF) };
        memcpy(last_draw, values, sizeof(values));
    }
    list_calls++;
    list_sprites += count;
}

/* Records turned frames: corners x, y and texels u, v, four each. */
void athena_image_draw_quad(AthenaImage *image, const float x[4], const float y[4],
    const float u[4], const float v[4], uint32_t color) {
    if (!athena_image_is_loaded(image))
        return;
    quad_count++;
    memcpy(last_quad, x, 4 * sizeof(float));
    memcpy(last_quad + 4, y, 4 * sizeof(float));
    memcpy(last_quad + 8, u, 4 * sizeof(float));
    memcpy(last_quad + 12, v, 4 * sizeof(float));
}

/*
 * Background decoding (loading jobs): a file that opens "decodes" to
 * 256 x 96, like tests/texture.png; a missing one fails at "open".
 */
int athena_image_decode(const char *path, AthenaImageBuffer *buffer) {
    FILE *file = fopen(path, "rb");

    memset(buffer, 0, sizeof(*buffer));
    if (!file) {
        buffer->error = ATHENA_IMAGE_LOAD_OPEN;
        return -1;
    }
    fclose(file);
    buffer->width = 256;
    buffer->height = 96;
    buffer->mem = calloc(1, 16);
    buffer->error = buffer->mem ? ATHENA_IMAGE_LOAD_OK : ATHENA_IMAGE_LOAD_DECODE;
    return buffer->mem ? 0 : -1;
}

void athena_image_buffer_release(AthenaImageBuffer *buffer) {
    free(buffer->mem);
    free(buffer->clut);
    buffer->mem = NULL;
    buffer->clut = NULL;
}

/* Images made from buffers live in a HostImage, as the ones new Image() makes. */
AthenaImage *athena_image_create_empty(bool delayed) {
    HostImage *host = calloc(1, sizeof(*host));

    if (!host)
        return NULL;
    host->image.delayed = delayed;
    return &host->image;
}

int athena_image_apply_buffer(AthenaImage *image, const char *path, AthenaImageBuffer *buffer) {
    HostImage *host = (HostImage *)image;   /* image is the first member */

    if (buffer->error != ATHENA_IMAGE_LOAD_OK) {
        athena_image_buffer_release(buffer);
        return -1;
    }
    host->surface.Width = buffer->width;
    host->surface.Height = buffer->height;
    host->image.surface = &host->surface;
    host->image.loaded = true;
    host->image.width = (float)buffer->width;
    host->image.height = (float)buffer->height;
    free(host->image.path);
    host->image.path = path ? strdup(path) : NULL;
    athena_image_buffer_release(buffer);
    return 0;
}

void athena_image_destroy(AthenaImage *image) {
    HostImage *host = (HostImage *)image;

    free(host->image.path);
    free(host);
}

JSValue athena_image_to_value(JSContext *ctx, AthenaImage *image) {
    JSValue object = JS_NewObjectClass(ctx, host_image_class_id);

    if (!JS_IsException(object))
        JS_SetOpaque(object, image);
    return object;
}

static void host_image_finalizer(JSRuntime *rt, JSValue value) {
    HostImage *host = JS_GetOpaque(value, host_image_class_id);

    if (host) {
        free(host->image.path);
        free(host);
    }
}

static JSClassDef host_image_class = { "Image", .finalizer = host_image_finalizer };

/* new Image(path, { width, height }): 256 x 96 by default, like tests/texture.png. */
static JSValue host_image_ctor(JSContext *ctx, JSValueConst new_target, int argc,
    JSValueConst *argv) {
    HostImage *host;
    const char *path;
    JSValue object;
    int32_t width = 256, height = 96;

    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "Image expects a path");
    if (argc > 1 && JS_IsObject(argv[1])) {
        JSValue w = JS_GetPropertyStr(ctx, argv[1], "width");
        JSValue h = JS_GetPropertyStr(ctx, argv[1], "height");

        if (!JS_IsUndefined(w))
            JS_ToInt32(ctx, &width, w);
        if (!JS_IsUndefined(h))
            JS_ToInt32(ctx, &height, h);
        JS_FreeValue(ctx, w);
        JS_FreeValue(ctx, h);
    }
    path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_EXCEPTION;
    host = calloc(1, sizeof(*host));
    object = JS_NewObjectClass(ctx, host_image_class_id);
    if (!host || JS_IsException(object)) {
        free(host);
        JS_FreeCString(ctx, path);
        JS_FreeValue(ctx, object);
        return JS_ThrowOutOfMemory(ctx);
    }
    host->image.path = strdup(path);
    host->surface.Width = (uint32_t)width;
    host->surface.Height = (uint32_t)height;
    host->image.surface = &host->surface;
    host->image.loaded = strncmp(path, "missing", 7) != 0 && !strstr(path, "/missing");
    host->image.width = (float)width;
    host->image.height = (float)height;
    JS_FreeCString(ctx, path);
    JS_SetOpaque(object, host);
    return object;
}

static JSValue host_image_path(JSContext *ctx, JSValueConst this_val) {
    AthenaImage *image = athena_image_peek(this_val);
    return image ? JS_NewString(ctx, image->path) : JS_UNDEFINED;
}

static const JSCFunctionListEntry host_image_proto[] = {
    JS_CGETSET_DEF("path", host_image_path, NULL),
};

static int host_image_module_init(JSContext *ctx, JSModuleDef *module) {
    JSValue proto, ctor;

    JS_NewClassID(&host_image_class_id);
    JS_NewClass(JS_GetRuntime(ctx), host_image_class_id, &host_image_class);
    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, host_image_proto, countof(host_image_proto));
    ctor = JS_NewCFunction2(ctx, host_image_ctor, "Image", 2, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, ctor, proto);
    JS_SetClassProto(ctx, host_image_class_id, proto);
    return JS_SetModuleExport(ctx, module, "Image", ctor);
}

/* ---- TileMap ------------------------------------------------------------- */

typedef struct {
    JSValue buffer;
} HostInstance;

bool athena_tilemap_is_instance(JSValueConst value) {
    return JS_GetOpaque(value, host_instance_class_id) != NULL;
}

AthenaTileSprite *athena_tilemap_instance_sprites(JSContext *ctx,
    JSValueConst value, uint32_t *count) {
    HostInstance *instance = JS_GetOpaque(value, host_instance_class_id);
    size_t size;
    uint8_t *data;

    if (!instance || JS_IsUndefined(instance->buffer))
        return NULL;
    data = JS_GetArrayBuffer(ctx, &size, instance->buffer);
    if (!data) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return NULL;
    }
    *count = (uint32_t)(size / sizeof(AthenaTileSprite));
    return (AthenaTileSprite *)data;
}

static void host_instance_finalizer(JSRuntime *rt, JSValue value) {
    HostInstance *instance = JS_GetOpaque(value, host_instance_class_id);

    if (instance) {
        JS_FreeValueRT(rt, instance->buffer);
        free(instance);
    }
}

static void host_instance_mark(JSRuntime *rt, JSValueConst value, JS_MarkFunc *mark) {
    HostInstance *instance = JS_GetOpaque(value, host_instance_class_id);

    if (instance)
        JS_MarkValue(rt, instance->buffer, mark);
}

static JSClassDef host_instance_class = {
    "Instance", .finalizer = host_instance_finalizer, .gc_mark = host_instance_mark,
};

/* new TileMap.Instance({ spriteBuffer }) */
static JSValue host_instance_ctor(JSContext *ctx, JSValueConst new_target, int argc,
    JSValueConst *argv) {
    HostInstance *instance;
    JSValue object;

    if (argc < 1 || !JS_IsObject(argv[0]))
        return JS_ThrowTypeError(ctx, "TileMap.Instance expects options");
    instance = calloc(1, sizeof(*instance));
    object = JS_NewObjectClass(ctx, host_instance_class_id);
    if (!instance || JS_IsException(object)) {
        free(instance);
        JS_FreeValue(ctx, object);
        return JS_ThrowOutOfMemory(ctx);
    }
    instance->buffer = JS_GetPropertyStr(ctx, argv[0], "spriteBuffer");
    JS_SetOpaque(object, instance);
    return object;
}

static JSValue host_instance_replace(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    HostInstance *instance = JS_GetOpaque2(ctx, this_val, host_instance_class_id);

    if (!instance)
        return JS_EXCEPTION;
    JS_FreeValue(ctx, instance->buffer);
    instance->buffer = argc > 0 ? JS_DupValue(ctx, argv[0]) : JS_UNDEFINED;
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry host_instance_proto[] = {
    JS_CFUNC_DEF("replaceSpriteBuffer", 1, host_instance_replace),
};

static void host_buffer_free(JSRuntime *rt, void *opaque, void *ptr) {
    free(ptr);
}

static JSValue host_buffer_create(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    uint32_t count;
    AthenaTileSprite *sprites;

    if (argc < 1 || JS_ToUint32(ctx, &count, argv[0]) || count == 0)
        return JS_ThrowRangeError(ctx, "SpriteBuffer.create expects a count");
    sprites = aligned_alloc(ATHENA_TILEMAP_BUFFER_ALIGN, count * sizeof(*sprites));
    if (!sprites)
        return JS_ThrowOutOfMemory(ctx);
    memset(sprites, 0, count * sizeof(*sprites));
    return JS_NewArrayBuffer(ctx, (uint8_t *)sprites, count * sizeof(*sprites),
        host_buffer_free, NULL, 0);
}

static int host_tilemap_module_init(JSContext *ctx, JSModuleDef *module) {
    JSValue proto, ctor, buffer, layout, offsets;

    JS_NewClassID(&host_instance_class_id);
    JS_NewClass(JS_GetRuntime(ctx), host_instance_class_id, &host_instance_class);
    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, host_instance_proto, countof(host_instance_proto));
    ctor = JS_NewCFunction2(ctx, host_instance_ctor, "Instance", 1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, ctor, proto);
    JS_SetClassProto(ctx, host_instance_class_id, proto);
    buffer = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, buffer, "create",
        JS_NewCFunction(ctx, host_buffer_create, "create", 1));
    layout = JS_NewObject(ctx);
    offsets = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, layout, "stride", JS_NewInt32(ctx, sizeof(AthenaTileSprite)));
    JS_SetPropertyStr(ctx, offsets, "x", JS_NewInt32(ctx, offsetof(AthenaTileSprite, x)));
    JS_SetPropertyStr(ctx, offsets, "u1", JS_NewInt32(ctx, offsetof(AthenaTileSprite, u1)));
    JS_SetPropertyStr(ctx, offsets, "v1", JS_NewInt32(ctx, offsetof(AthenaTileSprite, v1)));
    JS_SetPropertyStr(ctx, offsets, "u2", JS_NewInt32(ctx, offsetof(AthenaTileSprite, u2)));
    JS_SetPropertyStr(ctx, offsets, "v2", JS_NewInt32(ctx, offsetof(AthenaTileSprite, v2)));
    JS_SetPropertyStr(ctx, layout, "offsets", offsets);
    JS_SetModuleExport(ctx, module, "Instance", ctor);
    JS_SetModuleExport(ctx, module, "SpriteBuffer", buffer);
    return JS_SetModuleExport(ctx, module, "layout", layout);
}

/*
 * __spriteDraws(): { count, lists, listSprites, last: [x, y, w, h, u1, v1, u2, v2,
 * angle, red] }: single draws, list calls and the sprites in them; then resets.
 */
static JSValue js_sprite_draws(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    JSValue result = JS_NewObject(ctx), last = JS_NewArray(ctx);

    for (int i = 0; i < 10; i++)
        JS_SetPropertyUint32(ctx, last, i, JS_NewFloat64(ctx, last_draw[i]));
    JS_SetPropertyStr(ctx, result, "count", JS_NewInt32(ctx, draw_count));
    JS_SetPropertyStr(ctx, result, "lists", JS_NewInt32(ctx, list_calls));
    JS_SetPropertyStr(ctx, result, "listSprites", JS_NewInt32(ctx, list_sprites));
    JS_SetPropertyStr(ctx, result, "last", last);
    JS_SetPropertyStr(ctx, result, "quads", JS_NewInt32(ctx, quad_count));
    {
        JSValue quad = JS_NewArray(ctx);

        for (int i = 0; i < 16; i++)
            JS_SetPropertyUint32(ctx, quad, i, JS_NewFloat64(ctx, last_quad[i]));
        JS_SetPropertyStr(ctx, result, "lastQuad", quad);
    }
    draw_count = list_calls = list_sprites = quad_count = 0;
    return result;
}

void sprite_host_init(JSContext *ctx) {
    JSModuleDef *module;
    JSValue global;

    module = JS_NewCModule(ctx, "Image", host_image_module_init);
    JS_AddModuleExport(ctx, module, "Image");
    module = JS_NewCModule(ctx, "TileMap", host_tilemap_module_init);
    JS_AddModuleExport(ctx, module, "Instance");
    JS_AddModuleExport(ctx, module, "SpriteBuffer");
    JS_AddModuleExport(ctx, module, "layout");
    global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "__spriteDraws",
        JS_NewCFunction(ctx, js_sprite_draws, "__spriteDraws", 0));
    JS_FreeValue(ctx, global);
}
