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

/* Tile id that hides a sprite: setTiles/fillGrid give it zero size. */
#define ATHENA_TILEMAP_EMPTY 0xFFFFu

/* Tileset geometry: tile `id` is at column id % columns, row id / columns. */
typedef struct {
	uint32_t tile_width;
	uint32_t tile_height;
	uint32_t columns;
	/* 0 when unknown; otherwise ids must be below columns * rows. */
	uint32_t rows;
	/*
	 * Optional (u, v) pairs per tile id, from athena_tilemap_atlas_table().
	 * The EE has no fast integer divide, so looking tiles up here instead of
	 * computing id / columns and id % columns makes setTiles about 3x faster.
	 */
	const float *uv;
} AthenaTileAtlas;

/* Largest atlas that gets a UV table: 16384 tiles, 128 KB. */
#define ATHENA_TILEMAP_MAX_TABLE_TILES 16384

/*
 * Builds the UV table for an atlas with known rows and at most
 * ATHENA_TILEMAP_MAX_TABLE_TILES tiles; release it with free(). Returns NULL
 * when the atlas does not qualify or memory is short, in which case tiles
 * are computed on the fly.
 */
float *athena_tilemap_atlas_table(const AthenaTileAtlas *atlas);

/* A row-major grid of cells; sprite (row * columns + column) is one cell. */
typedef struct {
	uint32_t columns;
	uint32_t rows;
	float tile_width;
	float tile_height;
} AthenaTileGrid;

/* Sprites [first, first + count) of a buffer. */
typedef struct {
	uint32_t first;
	uint32_t count;
} AthenaTileRange;

/* Bulk edits of sprites [first, first + count); the caller checks bounds. */
void athena_tilemap_translate(AthenaTileSprite *sprites, uint32_t first,
	uint32_t count, float dx, float dy);
void athena_tilemap_set_color(AthenaTileSprite *sprites, uint32_t first,
	uint32_t count, uint32_t r, uint32_t g, uint32_t b, uint32_t a);
/*
 * Points sprites at atlas tiles and sets their size to width x height, or
 * to zero for ATHENA_TILEMAP_EMPTY. `tiles` must already be validated.
 */
void athena_tilemap_set_tiles(AthenaTileSprite *sprites, uint32_t first,
	const uint16_t *tiles, uint32_t count, const AthenaTileAtlas *atlas,
	float width, float height);
/* Lays out columns * rows sprites as cells; NULL `tiles` uses tile 0. */
void athena_tilemap_fill_grid(AthenaTileSprite *sprites,
	const AthenaTileGrid *grid, const uint16_t *tiles,
	const AthenaTileAtlas *atlas, float zindex);
/*
 * Computes the cells of `grid` visible on screen when it is drawn at (x, y)
 * with the current camera, plus a one-tile margin. Writes at most
 * grid->rows ascending ranges and returns how many.
 */
uint32_t athena_tilemap_visible_ranges(const AthenaTileGrid *grid,
	float x, float y, AthenaTileRange *ranges);

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
 *
 * `ranges` limits drawing to ascending, non-overlapping sprite ranges; NULL
 * draws the whole buffer. Returns the number of sprites queued.
 */
uint32_t athena_tilemap_render(const AthenaTileMaterial *materials,
	uint32_t material_count, GSSURFACE *const *textures,
	uint32_t texture_count, const AthenaTileSprite *sprites,
	uint32_t sprite_count, const AthenaTileRange *ranges,
	uint32_t range_count, float x, float y);

#endif
