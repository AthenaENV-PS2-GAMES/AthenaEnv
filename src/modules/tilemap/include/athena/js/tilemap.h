#ifndef ATHENA_TILEMAP_QUICKJS_API_H
#define ATHENA_TILEMAP_QUICKJS_API_H

#include <stdbool.h>
#include <stdint.h>

#include <ath_env.h>
#include <athena/tilemap.h>

/* True when `value` is a TileMap.Instance. */
bool athena_tilemap_is_instance(JSValueConst value);

/*
 * The sprite buffer of a TileMap.Instance, as other modules write it (the
 * Sprite animator). Returns NULL without throwing when `value` is not an
 * Instance or has no usable buffer (none, detached, misaligned). Resolve it
 * again each frame: replaceSpriteBuffer() changes it.
 */
AthenaTileSprite *athena_tilemap_instance_sprites(JSContext *ctx,
	JSValueConst value, uint32_t *count);

#endif
