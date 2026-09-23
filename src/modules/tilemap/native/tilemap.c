#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <kernel.h>

#include <graphics.h>
#include <owl_packet.h>
#include <mpg_manager.h>
#include <vif.h>

#include "tilemap.h"

/*
 * VU1 program built from old/src/vu1/draw_2D_tile_list.vcl. Only the
 * generated .vsm is part of the module because vcl is not in the toolchain.
 */
register_vu_program(VU1Draw2D_TileList);

/* Sprites per VU1 batch; each sprite unpacks to four quadwords. */
#define TILEMAP_BATCH_SIZE ATHENA_TILEMAP_MAX_BATCH
/*
 * VU1 data memory. Quadwords 0..1 hold the camera and origin. Each batch
 * buffer, relative to TOP, holds the GIF tag at +0, 200 input quadwords at
 * +1..+200, the output GIF tag at +202 and 250 output quadwords at
 * +203..+452: 453 quadwords. The buffers must not overlap. With OFFSET 452
 * the last output quadword of a full batch in the first buffer landed on the
 * GIF tag the VIF unpacks for the next batch, which corrupts primitives on
 * hardware where VU and VIF truly run concurrently.
 */
#define TILEMAP_VU1_BASE 2
#define TILEMAP_VU1_BUFFER_QWC 453
#define TILEMAP_VU1_OFFSET TILEMAP_VU1_BUFFER_QWC
_Static_assert(TILEMAP_VU1_BASE + 2 * TILEMAP_VU1_BUFFER_QWC <= 1024,
	"TileMap VU1 buffers exceed VU1 data memory");

static vu_mpg *tile_program;
/* GS drawing-area origin in .xy, camera offset in .zw; loaded with lq. */
static float tile_camera[4] __attribute__((aligned(16))) = {
	2047.35f, 2047.35f, 0.0f, 0.0f
};

static AthenaTileDiagnostics tile_diagnostics = {
	.flush_each_batch = false,
	.full_cache_flush = false,
	.batch_size = TILEMAP_BATCH_SIZE,
};

void athena_tilemap_set_diagnostics(const AthenaTileDiagnostics *diagnostics)
{
	if (!diagnostics)
		return;
	tile_diagnostics = *diagnostics;
	if (tile_diagnostics.batch_size == 0 ||
		tile_diagnostics.batch_size > TILEMAP_BATCH_SIZE)
		tile_diagnostics.batch_size = TILEMAP_BATCH_SIZE;
}

void athena_tilemap_get_diagnostics(AthenaTileDiagnostics *diagnostics)
{
	if (diagnostics)
		*diagnostics = tile_diagnostics;
}

static const AthenaTileLayout tile_layout = {
	.stride = sizeof(AthenaTileSprite),
	.offset_x = offsetof(AthenaTileSprite, x),
	.offset_y = offsetof(AthenaTileSprite, y),
	.offset_w = offsetof(AthenaTileSprite, w),
	.offset_h = offsetof(AthenaTileSprite, h),
	.offset_u1 = offsetof(AthenaTileSprite, u1),
	.offset_v1 = offsetof(AthenaTileSprite, v1),
	.offset_u2 = offsetof(AthenaTileSprite, u2),
	.offset_v2 = offsetof(AthenaTileSprite, v2),
	.offset_r = offsetof(AthenaTileSprite, r),
	.offset_g = offsetof(AthenaTileSprite, g),
	.offset_b = offsetof(AthenaTileSprite, b),
	.offset_a = offsetof(AthenaTileSprite, a),
	.offset_zindex = offsetof(AthenaTileSprite, zindex),
};

const AthenaTileLayout *athena_tilemap_layout(void)
{
	return &tile_layout;
}

void athena_tilemap_set_camera(float x, float y)
{
	tile_camera[2] = x;
	tile_camera[3] = y;
}

void athena_tilemap_get_camera(float *x, float *y)
{
	if (x)
		*x = tile_camera[2];
	if (y)
		*y = tile_camera[3];
}

AthenaTileSprite *athena_tilemap_buffer_alloc(uint32_t sprite_count)
{
	AthenaTileSprite *sprites;
	size_t bytes;

	if (sprite_count == 0 || sprite_count > SIZE_MAX / sizeof(*sprites))
		return NULL;
	bytes = (size_t)sprite_count * sizeof(*sprites);
	sprites = memalign(ATHENA_TILEMAP_BUFFER_ALIGN, bytes);
	if (sprites)
		memset(sprites, 0, bytes);
	return sprites;
}

void athena_tilemap_sync(void)
{
	/*
	 * Draws reference sprite memory by DMA and are only sent when the packet
	 * is flushed, normally by Screen.flip(). Send them now and wait until
	 * VIF1 has consumed them.
	 */
	owl_flush_packet();
	dmaKit_wait(DMA_CHANNEL_VIF1, 0);
}

/* Registers written per sprite: color, then UV/XYZ2 for both corners. */
#define TILEMAP_GIF_REGS (((u64)GS_RGBAQ) << 0 | ((u64)GS_UV) << 4 | \
	((u64)GS_XYZ2) << 8 | ((u64)GS_UV) << 12 | ((u64)GS_XYZ2) << 16)

/* GIF tag the VU program copies in front of each batch's output. */
static uint64_t tilemap_giftag(bool texture_mapping)
{
	prim_reg_t prim_data = {
		.PRIM = GS_PRIM_PRIM_SPRITE,
		.IIP = 0,
		.TME = texture_mapping,
		.FGE = gsGlobal->PrimFogEnable,
		.ABE = gsGlobal->PrimAlphaEnable,
		.AA1 = gsGlobal->PrimAAEnable,
		.FST = 1,
		.CTXT = gsGlobal->PrimContext,
		.FIX = 0
	};

	giftag_t prim_tag = {
		.NLOOP = 0,
		.EOP = 1,
		.PRE = 1,
		.PRIM = prim_data.data,
		.FLG = 0,
		.NREG = 5
	};

	return prim_tag.data;
}

/*
 * Makes the VIF wait until queued batches have finished on VU1 and their
 * primitives have left PATH1. Needed before a GS register changes through
 * PATH2, which could otherwise overtake sprites the VU has not kicked yet.
 */
static void tilemap_flush(void)
{
	owl_packet *packet = owl_query_packet(CHANNEL_VIF1, 1);

	owl_add_cnt_tag(packet, 0, owl_vif_code_double(VIF_CODE(0, 0, VIF_NOP, 0),
		VIF_CODE(0, 0, VIF_FLUSH, 0)));
}

/* Emits the VIF MARK that makes the texture manager upload `texture_id`. */
static void tilemap_upload_tags(owl_packet *packet, int texture_id)
{
	owl_add_cnt_tag(packet, 4, 0);
	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(0, 0, VIF_FLUSH, 0));
	owl_add_uint(packet, VIF_CODE(2, 0, VIF_DIRECT, 0));

	owl_add_tag(packet, GIF_AD, GIFTAG(1, 1, 0, 0, 0, 1));
	owl_add_tag(packet, GIF_NOP, 0);

	owl_add_uint(packet, VIF_CODE(0, 0, VIF_FLUSHA, 0));
	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(texture_id, 0, VIF_MARK, 0));
	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 1));
}

/* Sets TEX0/TEX1 once earlier draws, which may use another texture, end. */
static void tilemap_texture_tags(owl_packet *packet, GSSURFACE *tex)
{
	int tw, th;

	owl_add_cnt_tag(packet, 4, owl_vif_code_double(VIF_CODE(0, 0, VIF_NOP, 0),
		VIF_CODE(0, 0, VIF_NOP, 0)));
	owl_add_uint(packet, VIF_CODE(0, 0, VIF_FLUSHA, 0));
	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(3, 0, VIF_DIRECT, 0));

	owl_add_tag(packet, GIF_AD, GIFTAG(2, 1, 0, 0, 0, 1));

	athena_set_tw_th(tex, &tw, &th);

	owl_add_tag(packet,
		GS_TEX0_1 + gsGlobal->PrimContext,
		GS_SETREG_TEX0((tex->Vram & ~GRAPHICS_TRANSFER_REQUEST_MASK) / 256,
			tex->TBW,
			tex->PSM,
			tw, th,
			gsGlobal->PrimAlphaEnable,
			COLOR_MODULATE,
			(tex->VramClut & ~GRAPHICS_TRANSFER_REQUEST_MASK) / 256,
			tex->ClutPSM,
			0, 0,
			tex->VramClut ? GS_CLUT_STOREMODE_LOAD : GS_CLUT_STOREMODE_NOLOAD));

	owl_add_tag(packet, GS_TEX1_1 + gsGlobal->PrimContext,
		GS_SETREG_TEX1(1, 0, tex->Filter, tex->Filter, 0, 0, 0));
}

void athena_tilemap_translate(AthenaTileSprite *sprites, uint32_t first,
	uint32_t count, float dx, float dy)
{
	AthenaTileSprite *sprite = sprites + first;
	AthenaTileSprite *end = sprite + count;

	for (; sprite < end; ++sprite) {
		sprite->x += dx;
		sprite->y += dy;
	}
}

void athena_tilemap_set_color(AthenaTileSprite *sprites, uint32_t first,
	uint32_t count, uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
	AthenaTileSprite *sprite = sprites + first;
	AthenaTileSprite *end = sprite + count;

	for (; sprite < end; ++sprite) {
		sprite->r = r;
		sprite->g = g;
		sprite->b = b;
		sprite->a = a;
	}
}

float *athena_tilemap_atlas_table(const AthenaTileAtlas *atlas)
{
	uint32_t count;
	uint32_t id;
	float *table;

	if (!atlas || atlas->rows == 0 || atlas->columns == 0)
		return NULL;
	count = atlas->columns * atlas->rows;
	if (count > ATHENA_TILEMAP_MAX_TABLE_TILES)
		return NULL;
	table = malloc(count * 2 * sizeof(*table));
	if (!table)
		return NULL;
	for (id = 0; id < count; ++id) {
		table[2 * id] = (float)((id % atlas->columns) * atlas->tile_width);
		table[2 * id + 1] = (float)((id / atlas->columns) * atlas->tile_height);
	}
	return table;
}

/* Hoisted per-call constants for tilemap_point_at_tile(). */
typedef struct {
	const AthenaTileAtlas *atlas;
	float tile_width;
	float tile_height;
	float width;
	float height;
} TileLookup;

static void tilemap_lookup_init(TileLookup *lookup,
	const AthenaTileAtlas *atlas, float width, float height)
{
	lookup->atlas = atlas;
	lookup->tile_width = (float)atlas->tile_width;
	lookup->tile_height = (float)atlas->tile_height;
	lookup->width = width;
	lookup->height = height;
}

static inline void tilemap_point_at_tile(AthenaTileSprite *sprite,
	uint16_t id, const TileLookup *lookup)
{
	const AthenaTileAtlas *atlas = lookup->atlas;
	float u;
	float v;

	if (id == ATHENA_TILEMAP_EMPTY) {
		sprite->w = 0.0f;
		sprite->h = 0.0f;
		return;
	}
	if (atlas->uv) {
		/* Ids were validated against columns * rows, the table size. */
		u = atlas->uv[2 * id];
		v = atlas->uv[2 * id + 1];
	} else {
		u = (float)((id % atlas->columns) * atlas->tile_width);
		v = (float)((id / atlas->columns) * atlas->tile_height);
	}
	sprite->u1 = u;
	sprite->v1 = v;
	sprite->u2 = u + lookup->tile_width;
	sprite->v2 = v + lookup->tile_height;
	sprite->w = lookup->width;
	sprite->h = lookup->height;
}

void athena_tilemap_set_tiles(AthenaTileSprite *sprites, uint32_t first,
	const uint16_t *tiles, uint32_t count, const AthenaTileAtlas *atlas,
	float width, float height)
{
	AthenaTileSprite *sprite = sprites + first;
	TileLookup lookup;
	uint32_t i;

	tilemap_lookup_init(&lookup, atlas, width, height);
	for (i = 0; i < count; ++i, ++sprite)
		tilemap_point_at_tile(sprite, tiles[i], &lookup);
}

void athena_tilemap_fill_grid(AthenaTileSprite *sprites,
	const AthenaTileGrid *grid, const uint16_t *tiles,
	const AthenaTileAtlas *atlas, float zindex)
{
	uint32_t row;
	uint32_t column;
	uint32_t index = 0;
	TileLookup lookup;

	tilemap_lookup_init(&lookup, atlas, grid->tile_width, grid->tile_height);
	for (row = 0; row < grid->rows; ++row) {
		for (column = 0; column < grid->columns; ++column, ++index) {
			AthenaTileSprite *sprite = &sprites[index];

			memset(sprite, 0, sizeof(*sprite));
			sprite->x = (float)column * grid->tile_width;
			sprite->y = (float)row * grid->tile_height;
			sprite->r = sprite->g = sprite->b = sprite->a = 0x80;
			sprite->zindex = zindex;
			tilemap_point_at_tile(sprite, tiles ? tiles[index] : 0,
				&lookup);
		}
	}
}

/* Clamps floor(value) to [low, high] without overflowing an int. */
static int tilemap_clamp_cell(double value, int low, int high)
{
	value = floor(value);
	if (value < (double)low)
		return low;
	if (value > (double)high)
		return high;
	return (int)value;
}

uint32_t athena_tilemap_visible_ranges(const AthenaTileGrid *grid,
	float x, float y, AthenaTileRange *ranges)
{
	double left;
	double top;
	int first_column, last_column, first_row, last_row;
	int row;
	uint32_t count = 0;

	if (!grid || grid->columns == 0 || grid->rows == 0 ||
		grid->tile_width <= 0.0f || grid->tile_height <= 0.0f || !gsGlobal)
		return 0;
	/* Screen position of the grid's top-left corner. */
	left = (double)x + tile_camera[2];
	top = (double)y + tile_camera[3];
	/* One extra cell on each side covers sprites nudged off their cell. */
	first_column = tilemap_clamp_cell(-left / grid->tile_width - 1.0,
		0, (int)grid->columns);
	last_column = tilemap_clamp_cell((gsGlobal->Width - left) /
		grid->tile_width + 1.0, -1, (int)grid->columns - 1);
	first_row = tilemap_clamp_cell(-top / grid->tile_height - 1.0,
		0, (int)grid->rows);
	last_row = tilemap_clamp_cell((gsGlobal->Height - top) /
		grid->tile_height + 1.0, -1, (int)grid->rows - 1);
	if (first_column > last_column || first_row > last_row)
		return 0;

	for (row = first_row; row <= last_row; ++row) {
		uint32_t first = (uint32_t)row * grid->columns +
			(uint32_t)first_column;
		uint32_t span = (uint32_t)(last_column - first_column + 1);

		/* Full-width rows are contiguous: merge them into one range. */
		if (count > 0 &&
			ranges[count - 1].first + ranges[count - 1].count == first) {
			ranges[count - 1].count += span;
		} else {
			ranges[count].first = first;
			ranges[count].count = span;
			count++;
		}
	}
	return count;
}

/* State shared by every span queued during one render call. */
typedef struct {
	uint64_t giftags[2];
	/* Texture bound for this render, and the one TEX0 currently names. */
	GSSURFACE *bound;
	GSSURFACE *sent;
	int texture_id;
	uint64_t old_alpha;
	uint64_t alpha;
	bool started;
	int mpg_addr;
	uint32_t drawn;
} TileRenderState;

/* Queues sprites [first, last] with `material`, unless it must be skipped. */
static void tilemap_draw_span(TileRenderState *state,
	const AthenaTileMaterial *material, GSSURFACE *const *textures,
	uint32_t texture_count, const AthenaTileSprite *sprites,
	uint32_t first, uint32_t last)
{
	uint32_t remaining = last - first + 1;
	uint32_t drawn = 0;
	uint64_t material_alpha;
	bool texture_mapping =
		material->texture_index != ATHENA_TILEMAP_NO_TEXTURE;
	bool upload_pending = false;

	if (texture_mapping) {
		GSSURFACE *current = NULL;

		if (textures && material->texture_index >= 0 &&
			(uint32_t)material->texture_index < texture_count)
			current = textures[material->texture_index];
		/* A texture that is not loaded skips its sprites. */
		if (!current)
			return;
		if (current != state->bound) {
			state->texture_id = graphics_surface_bind(current, true);
			state->bound = state->texture_id == GRAPHICS_BIND_ERROR ?
				NULL : current;
			upload_pending = state->texture_id >= 0;
			/* A rebind may have moved the texture in VRAM. */
			state->sent = NULL;
		}
		if (!state->bound)
			return;
	}

	material_alpha = material->has_blend_mode ?
		material->blend_mode : state->old_alpha;
	if (material_alpha != state->alpha) {
		if (state->started)
			tilemap_flush();
		set_screen_param(ALPHA_BLEND_EQUATION, material_alpha);
		state->alpha = material_alpha;
	}

	while (remaining > 0) {
		uint32_t count = remaining < tile_diagnostics.batch_size ?
			remaining : tile_diagnostics.batch_size;
		/* Upload marker 5, texture registers 5, batch 4 (+1) quadwords. */
		owl_packet *packet = owl_query_packet(CHANNEL_VIF1, 15);

		if (tile_diagnostics.flush_each_batch)
			state->sent = NULL;
		if (upload_pending) {
			tilemap_upload_tags(packet, state->texture_id);
			upload_pending = false;
		}
		if (texture_mapping && state->sent != state->bound) {
			tilemap_texture_tags(packet, state->bound);
			state->sent = state->bound;
		}

		owl_add_unpack_data_cnt(packet, 0, 1, 1);
		owl_add_ulong(packet, state->giftags[texture_mapping]);
		owl_add_ulong(packet, TILEMAP_GIF_REGS);

		owl_add_unpack_data_ref(packet, 1,
			(void *)&sprites[first + drawn], count * 4, 1);

		/*
		 * No FLUSHA between batches: the VIF waits for the previous
		 * program before MSCNT, and the double-buffered layout keeps this
		 * unpack clear of the buffer VU1 or PATH1 may still use, so VU work
		 * overlaps GS drawing. The first batch runs the program setup;
		 * later ones resume at --cont.
		 */
		if (tile_diagnostics.flush_each_batch) {
			owl_add_cnt_tag(packet, 1, owl_vif_code_double(
				VIF_CODE(0, 0, VIF_NOP, 0), VIF_CODE(0, 0, VIF_NOP, 0)));
			owl_add_uint(packet, VIF_CODE(0, 0, VIF_FLUSHA, 0));
			owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
			owl_add_uint(packet, VIF_CODE(count, 0, VIF_ITOP, 0));
			owl_add_uint(packet, VIF_CODE(state->mpg_addr, 0,
				state->started ? VIF_MSCNT : VIF_MSCALF, 0));
		} else {
			owl_add_cnt_tag(packet, 0, owl_vif_code_double(
				VIF_CODE(state->mpg_addr, 0,
					state->started ? VIF_MSCNT : VIF_MSCALF, 0),
				VIF_CODE(count, 0, VIF_ITOP, 0)));
		}
		state->started = true;

		remaining -= count;
		drawn += count;
	}
	state->drawn += drawn;
}

uint32_t athena_tilemap_render(const AthenaTileMaterial *materials,
	uint32_t material_count, GSSURFACE *const *textures,
	uint32_t texture_count, const AthenaTileSprite *sprites,
	uint32_t sprite_count, const AthenaTileRange *ranges,
	uint32_t range_count, float x, float y)
{
	float origin[4] __attribute__((aligned(16))) = { x, y, 0.0f, 0.0f };
	AthenaTileRange whole;
	TileRenderState state;
	owl_packet *packet;
	/* Material `material` covers sprites [material_first, its end]. */
	uint32_t material = 0;
	uint32_t material_first = 0;
	uint32_t r;

	if (!materials || material_count == 0 || !sprites || sprite_count == 0)
		return 0;
	if (!ranges) {
		whole.first = 0;
		whole.count = sprite_count;
		ranges = &whole;
		range_count = 1;
	}
	if (range_count == 0)
		return 0;
	graphics_service_init();
	if (!tile_program) {
		tile_program = vu_mpg_load_buffer(
			embed_vu_code_ptr(VU1Draw2D_TileList),
			embed_vu_code_size(VU1Draw2D_TileList), VECTOR_UNIT_1, false);
		if (!tile_program)
			return 0;
	}

	/*
	 * VU1 reads the sprites by DMA: write back CPU writes still cached.
	 * The kernel walks the 8 KB data cache by index, so the cost does not
	 * grow with the buffer, and it includes the line holding `end`.
	 */
	if (tile_diagnostics.full_cache_flush)
		FlushCache(0);
	else
		SyncDCache((void *)sprites, (void *)(sprites + sprite_count));

	memset(&state, 0, sizeof(state));
	vu1_set_double_buffer_settings(TILEMAP_VU1_BASE, TILEMAP_VU1_OFFSET);
	state.mpg_addr = vu_mpg_preload(tile_program, true);
	state.old_alpha = get_screen_param(ALPHA_BLEND_EQUATION);
	state.alpha = state.old_alpha;
	state.texture_id = GRAPHICS_BIND_RESIDENT;
	state.giftags[0] = tilemap_giftag(false);
	state.giftags[1] = tilemap_giftag(true);

	/*
	 * VU1 static addresses 0..1 now hold the tile camera and origin; other
	 * renderers that cache constants there must upload them again.
	 */
	vu1_invalidate_static_data();
	packet = owl_query_packet(CHANNEL_VIF1, 3);
	owl_add_unpack_data_cnt(packet, 0, 2, 0);
	owl_add_uquad_ptr(packet, (__uint128_t *)tile_camera);
	owl_add_uquad_ptr(packet, (__uint128_t *)origin);

	/* Ranges ascend, so one pass over the materials serves all of them. */
	for (r = 0; r < range_count && material < material_count; ++r) {
		uint32_t start = ranges[r].first;
		uint32_t end;

		if (start >= sprite_count)
			break;
		end = ranges[r].count > sprite_count - start ?
			sprite_count : start + ranges[r].count;

		while (start < end && material < material_count) {
			const AthenaTileMaterial *current = &materials[material];
			uint32_t span_first;
			uint32_t span_last;

			if (current->end < material_first || current->end < start) {
				/* Empty, or entirely before this range. */
				if (current->end >= material_first)
					material_first = current->end + 1;
				++material;
				continue;
			}
			span_first = start > material_first ? start : material_first;
			span_last = current->end < end - 1 ? current->end : end - 1;
			tilemap_draw_span(&state, current, textures, texture_count,
				sprites, span_first, span_last);
			start = span_last + 1;
			if (span_last == current->end) {
				material_first = current->end + 1;
				++material;
			}
		}
	}

	packet = owl_query_packet(CHANNEL_VIF1, 1);
	owl_add_cnt_tag(packet, 0, owl_vif_code_double(
		VIF_CODE(0, 0, VIF_FLUSH, 0), VIF_CODE(0, 0, VIF_FLUSH, 0)));

	if (state.alpha != state.old_alpha)
		set_screen_param(ALPHA_BLEND_EQUATION, state.old_alpha);
	return state.drawn;
}
