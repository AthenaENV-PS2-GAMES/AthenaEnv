#include <stdlib.h>
#include <string.h>

#include <athena/sprite.h>

void athena_sprite_init(void) {
    tile_render_init();
}

void athena_sprite_begin(void) {
    tile_render_begin();
}

void athena_sprite_set_camera(float x, float y) {
    tile_render_set_camera(x, y);
}

const athena_tilemap_layout *athena_sprite_layout(void) {
    return tile_render_layout();
}

AthenaTilemapDescriptor *athena_sprite_descriptor_create(
    AthenaImage **textures, uint32_t texture_count,
    const athena_sprite_material *materials, uint32_t material_count) {

    AthenaTilemapDescriptor *descriptor = calloc(1, sizeof(*descriptor));
    if (!descriptor)
        return NULL;

    descriptor->descriptor.texture_count = texture_count;
    if (texture_count) {
        descriptor->descriptor.textures = calloc(texture_count, sizeof(GSSURFACE *));
        if (!descriptor->descriptor.textures) {
            free(descriptor);
            return NULL;
        }
        for (uint32_t i = 0; i < texture_count; i++) {
            if (textures && textures[i])
                descriptor->descriptor.textures[i] = textures[i]->tex;
        }
    }

    descriptor->descriptor.material_count = material_count;
    if (material_count && materials) {
        descriptor->descriptor.materials = malloc(sizeof(athena_sprite_material) * material_count);
        if (!descriptor->descriptor.materials) {
            free(descriptor->descriptor.textures);
            free(descriptor);
            return NULL;
        }
        memcpy(descriptor->descriptor.materials, materials,
               sizeof(athena_sprite_material) * material_count);
    }

    return descriptor;
}

void athena_sprite_descriptor_destroy(AthenaTilemapDescriptor *descriptor) {
    if (!descriptor)
        return;
    if (descriptor->descriptor.textures)
        free(descriptor->descriptor.textures);
    if (descriptor->descriptor.materials)
        free(descriptor->descriptor.materials);
    if (descriptor->owned_textures) {
        for (uint32_t i = 0; i < descriptor->owned_texture_count; i++)
            athena_image_destroy(descriptor->owned_textures[i]);
        free(descriptor->owned_textures);
    }
    free(descriptor);
}

uint32_t athena_sprite_descriptor_material_count(const AthenaTilemapDescriptor *descriptor) {
    return descriptor ? descriptor->descriptor.material_count : 0;
}

static int athena_sprite_instance_apply_buffer(AthenaTilemapInstance *instance,
    athena_sprite_data *sprites, uint32_t sprite_count, bool copy_sprites) {

    if (instance->owned_sprite_buffer) {
        free(instance->owned_sprite_buffer);
        instance->owned_sprite_buffer = NULL;
    }

    if (copy_sprites) {
        size_t length = sprite_count * sizeof(athena_sprite_data);
        void *copy = malloc(length);
        if (!copy)
            return -1;
        memcpy(copy, sprites, length);
        instance->owned_sprite_buffer = copy;
        instance->sprites = copy;
    } else {
        instance->sprites = sprites;
    }

    instance->sprite_count = sprite_count;
    return 0;
}

AthenaTilemapInstance *athena_sprite_instance_create(
    AthenaTilemapDescriptor *descriptor,
    athena_sprite_data *sprites, uint32_t sprite_count,
    bool copy_sprites) {

    AthenaTilemapInstance *instance = calloc(1, sizeof(*instance));
    if (!instance)
        return NULL;

    instance->descriptor = descriptor;
    if (sprites && sprite_count) {
        if (athena_sprite_instance_apply_buffer(instance, sprites, sprite_count, copy_sprites) < 0) {
            free(instance);
            return NULL;
        }
    }

    return instance;
}

void athena_sprite_instance_destroy(AthenaTilemapInstance *instance) {
    if (!instance)
        return;
    if (instance->owned_sprite_buffer)
        free(instance->owned_sprite_buffer);
    free(instance);
}

void athena_sprite_instance_render(AthenaTilemapInstance *instance, float x, float y, float z) {
    if (!instance || !instance->descriptor || !instance->sprites)
        return;
    tile_render_render(&instance->descriptor->descriptor, instance->sprites, x, y, z);
}

int athena_sprite_instance_replace_buffer(AthenaTilemapInstance *instance,
    athena_sprite_data *sprites, uint32_t sprite_count, bool copy_sprites) {
    if (!instance || !sprites || !sprite_count)
        return -1;
    return athena_sprite_instance_apply_buffer(instance, sprites, sprite_count, copy_sprites);
}

athena_sprite_data *athena_sprite_instance_get_sprites(AthenaTilemapInstance *instance,
    uint32_t *sprite_count) {
    if (!instance)
        return NULL;
    if (sprite_count)
        *sprite_count = instance->sprite_count;
    return instance->sprites;
}

int athena_sprite_instance_update_sprites(AthenaTilemapInstance *instance,
    uint32_t dst_offset, const athena_sprite_data *src, uint32_t copy_count) {
    if (!instance || !instance->sprites || !src)
        return -1;
    if ((uint64_t)dst_offset + copy_count > instance->sprite_count)
        return -1;
    memcpy(&instance->sprites[dst_offset], src, copy_count * sizeof(athena_sprite_data));
    return 0;
}

athena_sprite_data *athena_sprite_buffer_create(uint32_t sprite_count) {
    if (!sprite_count)
        return NULL;
    return calloc(sprite_count, sizeof(athena_sprite_data));
}

athena_sprite_data *athena_sprite_buffer_from_data(const athena_sprite_data *sprites,
    uint32_t sprite_count, bool *out_owned) {
    if (!sprites || !sprite_count)
        return NULL;
    athena_sprite_data *buffer = malloc(sprite_count * sizeof(athena_sprite_data));
    if (!buffer)
        return NULL;
    memcpy(buffer, sprites, sprite_count * sizeof(athena_sprite_data));
    if (out_owned)
        *out_owned = true;
    return buffer;
}
