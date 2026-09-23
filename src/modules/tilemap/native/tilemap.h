#ifndef ATHENA_TILEMAP_H
#define ATHENA_TILEMAP_H

#include <stdbool.h>
#include <stdint.h>

#include <graphics.h>

/*
 * One sprite as the VU1 tile program reads it: four quadwords, unpacked as
 * V4-32. Buffers of these are sent to VU1 by DMA reference, so they must be
 * 16-byte aligned and must not move or be freed until the frame is sent.
 */
typedef struct {
	float x;
	float y;
	float w;
	float h;

	float u1;
	float v1;
	float u2;
	float v2;

	uint32_t r;
	uint32_t g;
	uint32_t b;
	uint32_t a;

	uint32_t pad0;
	uint32_t pad1;
	float zindex;
	uint32_t pad2;
} AthenaTileSprite;

#define ATHENA_TILEMAP_BUFFER_ALIGN 64
#define ATHENA_TILEMAP_NO_TEXTURE (-1)

/*
 * Draw state for a contiguous run of sprites. Materials are ordered; each
 * one covers the sprites after the previous material's `end` up to and
 * including its own `end`.
 */
typedef struct {
	int32_t texture_index;
	/* When false the current Screen alpha equation is used. */
	bool has_blend_mode;
	uint64_t blend_mode;
	uint32_t end;
} AthenaTileMaterial;

typedef struct {
	uint32_t stride;
	uint32_t offset_x;
	uint32_t offset_y;
	uint32_t offset_w;
	uint32_t offset_h;
	uint32_t offset_u1;
	uint32_t offset_v1;
	uint32_t offset_u2;
	uint32_t offset_v2;
	uint32_t offset_r;
	uint32_t offset_g;
	uint32_t offset_b;
	uint32_t offset_a;
	uint32_t offset_zindex;
} AthenaTileLayout;

const AthenaTileLayout *athena_tilemap_layout(void);

/*
 * Hardware debugging switches. Each one restores a more conservative path
 * so a problem seen only on a real console can be bisected at runtime.
 */
typedef struct {
	/* FLUSHA and TEX0/TEX1 before every batch, as the original renderer. */
	bool flush_each_batch;
	/* Write back the whole data cache instead of the sprite range. */
	bool full_cache_flush;
	/* Sprites per VU1 batch, 1..ATHENA_TILEMAP_MAX_BATCH. */
	uint32_t batch_size;
} AthenaTileDiagnostics;

#define ATHENA_TILEMAP_MAX_BATCH 50

void athena_tilemap_set_diagnostics(const AthenaTileDiagnostics *diagnostics);
void athena_tilemap_get_diagnostics(AthenaTileDiagnostics *diagnostics);

void athena_tilemap_set_camera(float x, float y);
void athena_tilemap_get_camera(float *x, float *y);

/*
 * Waits until every queued tile draw has been read by DMA. Call it before
 * releasing sprite memory that was rendered since the last Screen.flip().
 */
void athena_tilemap_sync(void);

/* Allocates a zeroed, DMA-aligned sprite buffer; release it with free(). */
AthenaTileSprite *athena_tilemap_buffer_alloc(uint32_t sprite_count);

/*
 * Queues `sprites` for drawing at (x, y) plus the camera offset. Depth comes
 * only from each sprite's zindex: the VU program ignores an origin z.
 * `textures[i]` may be NULL for a texture that is not available, in which
 * case the sprites of materials using it are skipped. Sprites past
 * `sprite_count` are never read, even if a material's end is larger.
 */
void athena_tilemap_render(const AthenaTileMaterial *materials,
	uint32_t material_count, GSSURFACE *const *textures,
	uint32_t texture_count, const AthenaTileSprite *sprites,
	uint32_t sprite_count, float x, float y);

#endif
