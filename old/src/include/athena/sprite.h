#ifndef ATHENA_SPRITE_H
#define ATHENA_SPRITE_H

#include <stdint.h>
#include <stddef.h>

#include <tile_render.h>
#include <athena/image.h>

typedef struct AthenaTilemapDescriptor {
    athena_tilemap_descriptor descriptor;
    AthenaImage **owned_textures;
    uint32_t owned_texture_count;
} AthenaTilemapDescriptor;

typedef struct AthenaTilemapInstance {
    AthenaTilemapDescriptor *descriptor;
    athena_sprite_data *sprites;
    uint32_t sprite_count;
    void *owned_sprite_buffer;
} AthenaTilemapInstance;

void athena_sprite_init(void);
void athena_sprite_begin(void);
void athena_sprite_set_camera(float x, float y);
const athena_tilemap_layout *athena_sprite_layout(void);

AthenaTilemapDescriptor *athena_sprite_descriptor_create(
    AthenaImage **textures, uint32_t texture_count,
    const athena_sprite_material *materials, uint32_t material_count);
void athena_sprite_descriptor_destroy(AthenaTilemapDescriptor *descriptor);
uint32_t athena_sprite_descriptor_material_count(const AthenaTilemapDescriptor *descriptor);

AthenaTilemapInstance *athena_sprite_instance_create(
    AthenaTilemapDescriptor *descriptor,
    athena_sprite_data *sprites, uint32_t sprite_count,
    bool copy_sprites);
void athena_sprite_instance_destroy(AthenaTilemapInstance *instance);
void athena_sprite_instance_render(AthenaTilemapInstance *instance, float x, float y, float z);
int athena_sprite_instance_replace_buffer(AthenaTilemapInstance *instance,
    athena_sprite_data *sprites, uint32_t sprite_count, bool copy_sprites);
athena_sprite_data *athena_sprite_instance_get_sprites(AthenaTilemapInstance *instance,
    uint32_t *sprite_count);
int athena_sprite_instance_update_sprites(AthenaTilemapInstance *instance,
    uint32_t dst_offset, const athena_sprite_data *src, uint32_t copy_count);

athena_sprite_data *athena_sprite_buffer_create(uint32_t sprite_count);
athena_sprite_data *athena_sprite_buffer_from_data(const athena_sprite_data *sprites,
    uint32_t sprite_count, bool *out_owned);

#endif /* ATHENA_SPRITE_H */
