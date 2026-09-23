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

void athena_tilemap_render(const AthenaTileMaterial *materials,
	uint32_t material_count, GSSURFACE *const *textures,
	uint32_t texture_count, const AthenaTileSprite *sprites,
	uint32_t sprite_count, float x, float y)
{
	float origin[4] __attribute__((aligned(16))) = { x, y, 0.0f, 0.0f };
	uint64_t giftags[2];
	owl_packet *packet;
	/* Texture bound for this render, and the one TEX0 currently names. */
	GSSURFACE *bound = NULL;
	GSSURFACE *sent = NULL;
	int texture_id = GRAPHICS_BIND_RESIDENT;
	uint64_t old_alpha;
	uint64_t alpha;
	bool started = false;
	uint32_t first = 0;
	uint32_t i;
	int mpg_addr;

	if (!materials || material_count == 0 || !sprites || sprite_count == 0)
		return;
	graphics_service_init();
	if (!tile_program) {
		tile_program = vu_mpg_load_buffer(
			embed_vu_code_ptr(VU1Draw2D_TileList),
			embed_vu_code_size(VU1Draw2D_TileList), VECTOR_UNIT_1, false);
		if (!tile_program)
			return;
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

	vu1_set_double_buffer_settings(TILEMAP_VU1_BASE, TILEMAP_VU1_OFFSET);
	mpg_addr = vu_mpg_preload(tile_program, true);
	old_alpha = get_screen_param(ALPHA_BLEND_EQUATION);
	alpha = old_alpha;
	giftags[0] = tilemap_giftag(false);
	giftags[1] = tilemap_giftag(true);

	/*
	 * VU1 static addresses 0..1 now hold the tile camera and origin; other
	 * renderers that cache constants there must upload them again.
	 */
	vu1_invalidate_static_data();
	packet = owl_query_packet(CHANNEL_VIF1, 3);
	owl_add_unpack_data_cnt(packet, 0, 2, 0);
	owl_add_uquad_ptr(packet, (__uint128_t *)tile_camera);
	owl_add_uquad_ptr(packet, (__uint128_t *)origin);

	for (i = 0; i < material_count && first < sprite_count; i++) {
		const AthenaTileMaterial *material = &materials[i];
		uint32_t range_first = first;
		uint32_t remaining;
		uint32_t drawn = 0;
		uint64_t material_alpha;
		bool texture_mapping =
			material->texture_index != ATHENA_TILEMAP_NO_TEXTURE;
		bool upload_pending = false;

		if (material->end < range_first)
			continue;
		first = material->end >= sprite_count - 1 ?
			sprite_count : material->end + 1;

		if (texture_mapping) {
			GSSURFACE *current = NULL;

			if (textures && material->texture_index >= 0 &&
				(uint32_t)material->texture_index < texture_count)
				current = textures[material->texture_index];
			/* A texture that is not loaded skips its sprites. */
			if (!current)
				continue;
			if (current != bound) {
				texture_id = graphics_surface_bind(current, true);
				bound = texture_id == GRAPHICS_BIND_ERROR ? NULL : current;
				upload_pending = texture_id >= 0;
				/* A rebind may have moved the texture in VRAM. */
				sent = NULL;
			}
			if (!bound)
				continue;
		}

		material_alpha = material->has_blend_mode ?
			material->blend_mode : old_alpha;
		if (material_alpha != alpha) {
			if (started)
				tilemap_flush();
			set_screen_param(ALPHA_BLEND_EQUATION, material_alpha);
			alpha = material_alpha;
		}

		remaining = first - range_first;
		while (remaining > 0) {
			uint32_t count = remaining < tile_diagnostics.batch_size ?
				remaining : tile_diagnostics.batch_size;

			/* Upload marker 5, texture registers 5, batch 4 (+1) quadwords. */
			packet = owl_query_packet(CHANNEL_VIF1, 15);

			if (tile_diagnostics.flush_each_batch)
				sent = NULL;
			if (upload_pending) {
				tilemap_upload_tags(packet, texture_id);
				upload_pending = false;
			}
			if (texture_mapping && sent != bound) {
				tilemap_texture_tags(packet, bound);
				sent = bound;
			}

			owl_add_unpack_data_cnt(packet, 0, 1, 1);
			owl_add_ulong(packet, giftags[texture_mapping]);
			owl_add_ulong(packet, TILEMAP_GIF_REGS);

			owl_add_unpack_data_ref(packet, 1,
				(void *)&sprites[range_first + drawn], count * 4, 1);

			/*
			 * No FLUSHA between batches: the VIF waits for the previous
			 * program before MSCNT, and the double-buffered layout keeps
			 * this unpack clear of the buffer VU1 or PATH1 may still use,
			 * so VU work overlaps GS drawing. The first batch runs the
			 * program's setup; later ones resume at --cont.
			 */
			if (tile_diagnostics.flush_each_batch) {
				owl_add_cnt_tag(packet, 1, owl_vif_code_double(
					VIF_CODE(0, 0, VIF_NOP, 0), VIF_CODE(0, 0, VIF_NOP, 0)));
				owl_add_uint(packet, VIF_CODE(0, 0, VIF_FLUSHA, 0));
				owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
				owl_add_uint(packet, VIF_CODE(count, 0, VIF_ITOP, 0));
				owl_add_uint(packet, VIF_CODE(mpg_addr, 0,
					started ? VIF_MSCNT : VIF_MSCALF, 0));
			} else {
				owl_add_cnt_tag(packet, 0, owl_vif_code_double(
					VIF_CODE(mpg_addr, 0, started ? VIF_MSCNT : VIF_MSCALF, 0),
					VIF_CODE(count, 0, VIF_ITOP, 0)));
			}
			started = true;

			remaining -= count;
			drawn += count;
		}
	}

	owl_query_packet(CHANNEL_VIF1, 1);
	owl_add_cnt_tag(packet, 0, owl_vif_code_double(VIF_CODE(0, 0, VIF_FLUSH, 0),
		VIF_CODE(0, 0, VIF_FLUSH, 0)));

	if (alpha != old_alpha)
		set_screen_param(ALPHA_BLEND_EQUATION, old_alpha);
}
