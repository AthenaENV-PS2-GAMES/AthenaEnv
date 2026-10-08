#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>
#include <math.h>
#include <fcntl.h>

#include <time.h>

#include <athena/graphics.h>
#include <athena/math.h>
#include <athena/debug.h>

#include <athena/graphics/owl_packet.h>
#include <athena/graphics/texture_manager.h>

static int texture_upload_pending(int texture_id)
{
	return texture_id >= 0;
}

static int draw_list_capacity(size_t base_size, size_t item_size)
{
	owl_controller *controller = owl_get_controller();

	if (!controller || item_size == 0 || controller->size <= base_size + 1)
		return 0;

	return (int)((controller->size - base_size - 1) / item_size);
}

static int draw_coord(float value)
{
	return (int)lroundf(value);
}

/*
 * The 2D view (athena/graphics/view.h). While it is the identity every draw
 * takes its original path; otherwise coordinates go through it here, and
 * rectangles become triangles when it rotates.
 */
static inline bool view_active(void)
{
	return athena_view_kind() != ATHENA_VIEW_IDENTITY;
}

/*
 * Screen coordinates are packed as int16 before the GS range clamp: keep
 * transformed ones where they cannot wrap. Anything past the GS range
 * (+-2048 around the origin) is clamped by the packing anyway.
 */
#define VIEW_COORD_LIMIT 16383.0f

static inline float view_clamp(float v)
{
	return v < -VIEW_COORD_LIMIT ? -VIEW_COORD_LIMIT :
		(v > VIEW_COORD_LIMIT ? VIEW_COORD_LIMIT : v);
}

/* The view applied to a point in place, within the packable range. */
static inline void view_apply_safe(float *x, float *y)
{
	athena_view_apply(*x, *y, x, y);
	*x = view_clamp(*x);
	*y = view_clamp(*y);
}

/* A point through the view, rounded like draw_coord(). */
static inline void view_point(float x, float y, int *sx, int *sy)
{
	if (view_active())
		view_apply_safe(&x, &y);
	*sx = draw_coord(x);
	*sy = draw_coord(y);
}

/* A point through the view, truncated like the float to int arguments of the textured lists. */
static inline void view_point_trunc(float x, float y, int *sx, int *sy)
{
	if (view_active())
		view_apply_safe(&x, &y);
	*sx = (int)x;
	*sy = (int)y;
}

/* Screen box of `count` transformed points. */
static void view_box(const float *xs, const float *ys, int count, float box[4])
{
	box[0] = box[2] = xs[0];
	box[1] = box[3] = ys[0];
	for (int i = 1; i < count; i++) {
		if (xs[i] < box[0]) box[0] = xs[i];
		if (xs[i] > box[2]) box[2] = xs[i];
		if (ys[i] < box[1]) box[1] = ys[i];
		if (ys[i] > box[3]) box[3] = ys[i];
	}
}

/*
 * Corners of the rectangle (x, y)-(x + w, y + h) through the view, in
 * triangle strip order: top-left, bottom-left, top-right, bottom-right.
 * Returns false when the result misses the clip rectangle entirely.
 */
static bool view_rect_corners(float x, float y, float w, float h,
	float xs[4], float ys[4])
{
	float box[4];

	xs[0] = x;
	ys[0] = y;
	view_apply_safe(&xs[0], &ys[0]);
	xs[1] = x;
	ys[1] = y + h;
	view_apply_safe(&xs[1], &ys[1]);
	xs[2] = x + w;
	ys[2] = y;
	view_apply_safe(&xs[2], &ys[2]);
	xs[3] = x + w;
	ys[3] = y + h;
	view_apply_safe(&xs[3], &ys[3]);
	view_box(xs, ys, 4, box);
	if (!athena_view_screen_box_visible(box[0], box[1], box[2], box[3])) {
		athena_view_count_culled(1);
		return false;
	}
	return true;
}

const int16_t OWL_XYOFFSET[8] qw_aligned = { 2048, 2048, 0, 0, 2048, 2048, 0, 0 };
const uint16_t OWL_XYMAX[8] qw_aligned =   { 4095, 4095, 0, 0, 4095, 4095, 0, 0 };

const int16_t OWL_XYOFFSET_FIXED[8] qw_aligned = { 0, 0, 0, 0, 0, 0, 0, 0 };
const uint16_t OWL_XYMAX_FIXED[8] qw_aligned =   { ftoi4(int, 4095), ftoi4(int, 4095), 0, 0, ftoi4(int, 4095), ftoi4(int, 4095), 0, 0 };

void draw_point_list(float x, float y, prim_point *list, int list_size)
{
	int offset = 0;
	int capacity;

	if (!list || list_size <= 0)
		return;
	capacity = draw_list_capacity(5, 1);
	if (capacity <= 0)
		return;

	while (offset < list_size) {
		int count = list_size - offset;
		owl_packet *packet;
		if (count > capacity)
			count = capacity;
		packet = owl_query_packet(CHANNEL_VIF1, 5 + count);

		owl_add_cnt_tag_fill(packet, 4 + count);
		owl_add_direct(packet, 3 + count);
	
	owl_add_tag(packet, GIF_AD, GIFTAG(1, 1, 0, 0, 0, 1));

	owl_add_tag(packet, GS_PRIM, VU_GS_PRIM(GS_PRIM_PRIM_POINT, 0, 0, gsGlobal->PrimFogEnable, gsGlobal->PrimAlphaEnable, gsGlobal->PrimAAEnable, 1, gsGlobal->PrimContext, 0));

	owl_add_tag(packet, 
					   ((uint64_t)(GS_RGBAQ) << 0 | (uint64_t)(GS_XYZ2) << 4), 
						VU_GS_GIFTAG(count,
							1, NO_CUSTOM_DATA, 0, 
							0,
    						1, 2)
						);

		for (int i = 0; i < count; i++) {
			int sx, sy;
			view_point(x + list[offset + i].x, y + list[offset + i].y,
				&sx, &sy);
			owl_add_rgba_xy(packet, list[offset + i].rgba, sx, sy);
		}
		offset += count;
	}
} 

void draw_line_list(float x, float y, prim_line *list, int list_size) 
{
	int offset = 0;
	int capacity;

	if (!list || list_size <= 0)
		return;
	capacity = draw_list_capacity(3, 2);
	if (capacity <= 0)
		return;

	while (offset < list_size) {
		int count = list_size - offset;
		owl_packet *packet;
		if (count > capacity)
			count = capacity;
		packet = owl_query_packet(CHANNEL_VIF1, 3 + count * 2);

		owl_add_cnt_tag_fill(packet, 2 + count * 2);
		owl_add_direct(packet, 1 + count * 2);

		owl_add_tag(packet,
					   ((uint64_t)(GS_PRIM)  << 0 | (uint64_t)(GS_RGBAQ)  << 4 | (uint64_t)(GS_XYZ2) << 8 | (uint64_t)(GS_XYZ2) << 12), 
						VU_GS_GIFTAG(count,
							1, NO_CUSTOM_DATA, 0, 
							0,
    						1, 4)
						);

	uint64_t prim = VU_GS_PRIM(GS_PRIM_PRIM_LINE, 0, 0, gsGlobal->PrimFogEnable, gsGlobal->PrimAlphaEnable, gsGlobal->PrimAAEnable, 1, gsGlobal->PrimContext, 0);

		for (int i = 0; i < count; i++) {
			prim_line *item = &list[offset + i];
			int x1, y1, x2, y2;
			view_point(x + item->x, y + item->y, &x1, &y1);
			view_point(x + item->x2, y + item->y2, &x2, &y2);
			owl_add_tag(packet, item->rgba, prim);
			owl_add_xy_2x(packet, x1, y1, x2, y2);
		}
		offset += count;
	}
}

void draw_line_gouraud_list(float x, float y, prim_gouraud_line *list, int list_size) 
{
	int offset = 0;
	int capacity;

	if (!list || list_size <= 0)
		return;
	capacity = draw_list_capacity(5, 2);
	if (capacity <= 0)
		return;

	while (offset < list_size) {
		int count = list_size - offset;
		owl_packet *packet;
		if (count > capacity)
			count = capacity;
		packet = owl_query_packet(CHANNEL_VIF1, 5 + count * 2);

		owl_add_cnt_tag_fill(packet, 4 + count * 2);
		owl_add_direct(packet, 3 + count * 2);
	
	owl_add_tag(packet, GIF_AD, GIFTAG(1, 1, 0, 0, 0, 1));

	owl_add_tag(packet, GS_PRIM, VU_GS_PRIM(GS_PRIM_PRIM_LINE, 1, 0, gsGlobal->PrimFogEnable, gsGlobal->PrimAlphaEnable, gsGlobal->PrimAAEnable, 1, gsGlobal->PrimContext, 0));

	owl_add_tag(packet, 
					   ((uint64_t)(GS_RGBAQ)  << 0 | (uint64_t)(GS_XYZ2) << 4), 
						VU_GS_GIFTAG(count * 2,
							1, NO_CUSTOM_DATA, 0, 
							0,
    						1, 2)
						);

		for (int i = 0; i < count; i++) {
			prim_gouraud_line *item = &list[offset + i];
			int sx, sy;
			view_point(x + item->x, y + item->y, &sx, &sy);
			owl_add_rgba_xy(packet, item->rgba, sx, sy);
			view_point(x + item->x2, y + item->y2, &sx, &sy);
			owl_add_rgba_xy(packet, item->rgba2, sx, sy);
		}
		offset += count;
	}
}

void draw_triangle_list(float x, float y, prim_triangle *list, int list_size) {
	int offset = 0;
	int capacity;

	if (!list || list_size <= 0)
		return;
	capacity = draw_list_capacity(5, 2);
	if (capacity <= 0)
		return;

	while (offset < list_size) {
		int count = list_size - offset;
		owl_packet *packet;
		if (count > capacity)
			count = capacity;
		packet = owl_query_packet(CHANNEL_VIF1, 5 + count * 2);

		owl_add_cnt_tag_fill(packet, 4 + count * 2);
		owl_add_direct(packet, 3 + count * 2);
	
	owl_add_tag(packet, GIF_AD, GIFTAG(1, 1, 0, 0, 0, 1));

	owl_add_tag(packet, GS_PRIM, VU_GS_PRIM(GS_PRIM_PRIM_TRIANGLE, 0, 0, gsGlobal->PrimFogEnable, gsGlobal->PrimAlphaEnable, gsGlobal->PrimAAEnable, 1, gsGlobal->PrimContext, 0));

	owl_add_tag(packet, 
					   ((uint64_t)(GS_RGBAQ) << 0 | (uint64_t)(GS_XYZ2) << 4 | (uint64_t)(GS_XYZ2) << 8 | (uint64_t)(GS_XYZ2) << 12), 
						VU_GS_GIFTAG(count,
							1, NO_CUSTOM_DATA, 0, 
							0,
    						1, 4)
						);


		for (int i = 0; i < count; i++) {
			prim_triangle *item = &list[offset + i];
			int x1, y1, x2, y2, x3, y3;
			view_point(x + item->x, y + item->y, &x1, &y1);
			view_point(x + item->x2, y + item->y2, &x2, &y2);
			view_point(x + item->x3, y + item->y3, &x3, &y3);
			owl_add_ulong(packet, item->rgba);
			owl_add_xy(packet, x1, y1);
			owl_add_xy_2x(packet, x2, y2, x3, y3);
		}
		offset += count;
	}
}

void draw_triangle_gouraud_list(float x, float y, prim_gouraud_triangle *list, int list_size) {
	int offset = 0;
	int capacity;

	if (!list || list_size <= 0)
		return;
	capacity = draw_list_capacity(5, 3);
	if (capacity <= 0)
		return;

	while (offset < list_size) {
		int count = list_size - offset;
		owl_packet *packet;
		if (count > capacity)
			count = capacity;
		packet = owl_query_packet(CHANNEL_VIF1, 5 + count * 3);

		owl_add_cnt_tag_fill(packet, 4 + count * 3);
		owl_add_direct(packet, 3 + count * 3);
	
	owl_add_tag(packet, GIF_AD, GIFTAG(1, 1, 0, 0, 0, 1));

	owl_add_tag(packet, GS_PRIM, VU_GS_PRIM(GS_PRIM_PRIM_TRIANGLE, 1, 0, gsGlobal->PrimFogEnable, gsGlobal->PrimAlphaEnable, gsGlobal->PrimAAEnable, 1, gsGlobal->PrimContext, 0));

	owl_add_tag(packet, 
					   ((uint64_t)(GS_RGBAQ)  << 0 | (uint64_t)(GS_XYZ2) << 4), 
						VU_GS_GIFTAG(count * 3,
							1, NO_CUSTOM_DATA, 0, 
							0,
    						1, 2)
						);

		for (int i = 0; i < count; i++) {
			prim_gouraud_triangle *item = &list[offset + i];
			int sx, sy;
			view_point(x + item->x, y + item->y, &sx, &sy);
			owl_add_rgba_xy(packet, item->rgba, sx, sy);
			view_point(x + item->x2, y + item->y2, &sx, &sy);
			owl_add_rgba_xy(packet, item->rgba2, sx, sy);
			view_point(x + item->x3, y + item->y3, &sx, &sy);
			owl_add_rgba_xy(packet, item->rgba3, sx, sy);
		}
		offset += count;
	}
}

void draw_tex_triangle_list(GSSURFACE* source, float x, float y, prim_tex_triangle *list, int list_size) {
	int capacity;
	if (!source || !list || list_size <= 0)
		return;
	capacity = draw_list_capacity(11, 3);
	if (capacity <= 0)
		return;
	if (list_size > capacity) {
		int offset = 0;
		while (offset < list_size) {
			int count = list_size - offset;
			if (count > capacity)
				count = capacity;
			draw_tex_triangle_list(source, x, y, list + offset, count);
			offset += count;
		}
		return;
	}
    int texture_id = graphics_surface_bind(source, true);
	if (texture_id == GRAPHICS_BIND_ERROR)
		return;

	owl_packet *packet = owl_query_packet(CHANNEL_VIF1, (texture_upload_pending(texture_id) ? 11 : 7)+(list_size*3));

	owl_add_cnt_tag(packet, (texture_upload_pending(texture_id) ? 10 : 6)+(list_size*3), 0);

	if (texture_upload_pending(texture_id)) {
		texture_manager_add_mark(packet, texture_id);
	}

	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(0, 0,
		texture_upload_pending(texture_id) ? VIF_FLUSHA : VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(5+(list_size*3), 0, VIF_DIRECT, 0)); 
	
	owl_add_tag(packet, GIF_AD, GIFTAG(3, 1, 0, 0, 0, 1));

	int tw, th;
	athena_set_tw_th(source, &tw, &th);

	owl_add_tag(packet, 
		GS_TEX0_1+gsGlobal->PrimContext, 
		GS_SETREG_TEX0((source->Vram & ~GRAPHICS_TRANSFER_REQUEST_MASK)/256,
					  source->TBW, 
					  source->PSM,
					  tw, th, 
					  gsGlobal->PrimAlphaEnable, 
					  COLOR_MODULATE,
					  (source->VramClut & ~GRAPHICS_TRANSFER_REQUEST_MASK)/256,
					  source->ClutPSM, 
					  0, 0, 
					  source->VramClut? GS_CLUT_STOREMODE_LOAD : GS_CLUT_STOREMODE_NOLOAD)
	);
	
	owl_add_tag(packet, GS_TEX1_1+gsGlobal->PrimContext, GS_SETREG_TEX1(1, 0, source->Filter, source->Filter, 0, 0, 0));

	owl_add_tag(packet, GS_PRIM, VU_GS_PRIM(GS_PRIM_PRIM_TRIANGLE, 1, 1, gsGlobal->PrimFogEnable, gsGlobal->PrimAlphaEnable, gsGlobal->PrimAAEnable, 1, gsGlobal->PrimContext, 0));

	owl_add_tag(packet, 
					   ((uint64_t)(GS_UV)  << 0 | (uint64_t)(GS_XYZ2) << 4), 
					   	VU_GS_GIFTAG(list_size*3, 
							1, NO_CUSTOM_DATA, 0, 
							0,
    						1, 2)
						);

	for (int i = 0; i < list_size; i++) {
		int sx, sy;
		view_point_trunc(x + list[i].x, y + list[i].y, &sx, &sy);
		owl_add_xy_uv(packet, sx, sy, list[i].u, list[i].v);
		view_point_trunc(x + list[i].x2, y + list[i].y2, &sx, &sy);
		owl_add_xy_uv(packet, sx, sy, list[i].u2, list[i].v2);
		view_point_trunc(x + list[i].x3, y + list[i].y3, &sx, &sy);
		owl_add_xy_uv(packet, sx, sy, list[i].u3, list[i].v3);
	}
}

void draw_tex_triangle_gouraud_list(GSSURFACE* source, float x, float y, prim_tex_gouraud_triangle *list, int list_size) {
	int capacity;
	if (!source || !list || list_size <= 0)
		return;
	capacity = draw_list_capacity(11, 5);
	if (capacity <= 0)
		return;
	if (list_size > capacity) {
		int offset = 0;
		while (offset < list_size) {
			int count = list_size - offset;
			if (count > capacity)
				count = capacity;
			draw_tex_triangle_gouraud_list(source, x, y, list + offset, count);
			offset += count;
		}
		return;
	}
    int texture_id = graphics_surface_bind(source, true);
	if (texture_id == GRAPHICS_BIND_ERROR)
		return;

	uint32_t packet_list_size = ceilf(list_size*4.5f);

	owl_packet *packet = owl_query_packet(CHANNEL_VIF1, (texture_upload_pending(texture_id) ? 11 : 7)+packet_list_size);

	owl_add_cnt_tag(packet, (texture_upload_pending(texture_id) ? 10 : 6)+packet_list_size, 0);

	if (texture_upload_pending(texture_id)) {
		texture_manager_add_mark(packet, texture_id);
	}

	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(0, 0,
		texture_upload_pending(texture_id) ? VIF_FLUSHA : VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(5+packet_list_size, 0, VIF_DIRECT, 0)); 
	
	owl_add_tag(packet, GIF_AD, GIFTAG(3, 1, 0, 0, 0, 1));

	int tw, th;
	athena_set_tw_th(source, &tw, &th);

	owl_add_tag(packet, 
		GS_TEX0_1+gsGlobal->PrimContext, 
		GS_SETREG_TEX0((source->Vram & ~GRAPHICS_TRANSFER_REQUEST_MASK)/256,
					  source->TBW, 
					  source->PSM,
					  tw, th, 
					  gsGlobal->PrimAlphaEnable, 
					  COLOR_MODULATE,
					  (source->VramClut & ~GRAPHICS_TRANSFER_REQUEST_MASK)/256,
					  source->ClutPSM, 
					  0, 0, 
					  source->VramClut? GS_CLUT_STOREMODE_LOAD : GS_CLUT_STOREMODE_NOLOAD)
	);
	
	owl_add_tag(packet, GS_TEX1_1+gsGlobal->PrimContext, GS_SETREG_TEX1(1, 0, source->Filter, source->Filter, 0, 0, 0));

	owl_add_tag(packet, GS_PRIM, VU_GS_PRIM(GS_PRIM_PRIM_TRIANGLE, 0, 1, gsGlobal->PrimFogEnable, gsGlobal->PrimAlphaEnable, gsGlobal->PrimAAEnable, 1, gsGlobal->PrimContext, 0));

	owl_add_tag(packet, 
					   ((uint64_t)(GS_RGBAQ)  << 0 | (uint64_t)(GS_UV)  << 4 | (uint64_t)(GS_XYZ2) << 8), 
					   	VU_GS_GIFTAG(list_size*3, 
							1, NO_CUSTOM_DATA, 0, 
							0,
    						1, 3)
						);

	for (int i = 0; i < list_size; i++) {
		int sx, sy;

		owl_add_ulong(packet, list[i].rgba);
		owl_add_uv(packet, list[i].u, list[i].v);
		view_point_trunc(x + list[i].x, y + list[i].y, &sx, &sy);
		owl_add_xy(packet, sx, sy);

		owl_add_ulong(packet, list[i].rgba2);
		owl_add_uv(packet, list[i].u2, list[i].v2);
		view_point_trunc(x + list[i].x2, y + list[i].y2, &sx, &sy);
		owl_add_xy(packet, sx, sy);

		owl_add_ulong(packet, list[i].rgba3);
		owl_add_uv(packet, list[i].u3, list[i].v3);
		view_point_trunc(x + list[i].x3, y + list[i].y3, &sx, &sy);
		owl_add_xy(packet, sx, sy);
	}

	owl_align_packet(packet);
}

/* Sprites per batch when a rotating view turns them into triangles. */
#define ROTATED_SPRITE_BATCH 64

/* Under a rotating view: each sprite as two triangles, which draw through the view. */
static void draw_image_list_rotated(GSSURFACE *source, float x, float y,
	const prim_tex_sprite *list, int list_size)
{
	static prim_tex_gouraud_triangle triangles[ROTATED_SPRITE_BATCH * 2];
	int count = 0;

	for (int i = 0; i < list_size; i++) {
		const prim_tex_sprite *s = &list[i];
		float x2 = s->x + s->w, y2 = s->y + s->h;
		prim_tex_gouraud_triangle *a = &triangles[count++];
		prim_tex_gouraud_triangle *b = &triangles[count++];

		*a = (prim_tex_gouraud_triangle){
			s->x, s->y, s->u1, s->v1, s->rgba,
			s->x, y2, s->u1, s->v2, s->rgba,
			x2, s->y, s->u2, s->v1, s->rgba,
		};
		*b = (prim_tex_gouraud_triangle){
			x2, s->y, s->u2, s->v1, s->rgba,
			s->x, y2, s->u1, s->v2, s->rgba,
			x2, y2, s->u2, s->v2, s->rgba,
		};
		if (count == ROTATED_SPRITE_BATCH * 2) {
			draw_tex_triangle_gouraud_list(source, x, y, triangles, count);
			count = 0;
		}
	}
	if (count)
		draw_tex_triangle_gouraud_list(source, x, y, triangles, count);
}

void draw_image_list(GSSURFACE* source, float x, float y, prim_tex_sprite *list, int list_size)
{
	int capacity;
	if (!source || !list || list_size <= 0)
		return;
	if (athena_view_kind() == ATHENA_VIEW_ROTATED) {
		draw_image_list_rotated(source, x, y, list, list_size);
		return;
	}
	capacity = draw_list_capacity(10, 3);
	if (capacity <= 0)
		return;
	if (list_size > capacity) {
		int offset = 0;
		while (offset < list_size) {
			int count = list_size - offset;
			if (count > capacity)
				count = capacity;
			draw_image_list(source, x, y, list + offset, count);
			offset += count;
		}
		return;
	}
    int texture_id = graphics_surface_bind(source, true);
	if (texture_id == GRAPHICS_BIND_ERROR)
		return;

	owl_packet *packet = owl_query_packet(CHANNEL_VIF1, texture_upload_pending(texture_id) ? (10+(list_size*3)) : (6+(list_size*3)));

	owl_add_cnt_tag(packet, texture_upload_pending(texture_id) ? (9+(list_size*3)) : (5+(list_size*3)), 0);

	if (texture_upload_pending(texture_id)) {
		texture_manager_add_mark(packet, texture_id);
	}

	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(0, 0,
		texture_upload_pending(texture_id) ? VIF_FLUSHA : VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(4+(list_size*3), 0, VIF_DIRECT, 0)); 
	
	owl_add_tag(packet, GIF_AD, GIFTAG(2, 1, 0, 0, 0, 1));

	int tw, th;
	athena_set_tw_th(source, &tw, &th);

	owl_add_tag(packet, 
		GS_TEX0_1+gsGlobal->PrimContext, 
		GS_SETREG_TEX0((source->Vram & ~GRAPHICS_TRANSFER_REQUEST_MASK)/256,
					  source->TBW, 
					  source->PSM,
					  tw, th, 
					  gsGlobal->PrimAlphaEnable, 
					  COLOR_MODULATE,
					  (source->VramClut & ~GRAPHICS_TRANSFER_REQUEST_MASK)/256,
					  source->ClutPSM, 
					  0, 0, 
					  source->VramClut? GS_CLUT_STOREMODE_LOAD : GS_CLUT_STOREMODE_NOLOAD)
	);
	
	owl_add_tag(packet, GS_TEX1_1+gsGlobal->PrimContext, GS_SETREG_TEX1(1, 0, source->Filter, source->Filter, 0, 0, 0));

	owl_add_tag(packet, 
		((uint64_t)(GS_PRIM) << 0 | (uint64_t)(GS_RGBAQ) << 4 | (uint64_t)(GS_UV) << 8 | (uint64_t)(GS_XYZ2) << 12 | (uint64_t)(GS_UV) << 16 | (uint64_t)(GS_XYZ2) << 20), 
			VU_GS_GIFTAG(list_size, 
			 1, NO_CUSTOM_DATA, 0, 
			 0,
			 1, 6) // REGLIST
		 );

	uint64_t prim = VU_GS_PRIM(GS_PRIM_PRIM_SPRITE, 0, 1, gsGlobal->PrimFogEnable, gsGlobal->PrimAlphaEnable, gsGlobal->PrimAAEnable, 1, gsGlobal->PrimContext, 0);

	for (int i = 0; i < list_size; i++) {
		int x1, y1, x2, y2;

		view_point_trunc(x + list[i].x, y + list[i].y, &x1, &y1);
		view_point_trunc(x + (list[i].w + list[i].x),
			y + (list[i].h + list[i].y), &x2, &y2);
		owl_add_tag(packet, list[i].rgba, prim);

		owl_add_xy_uv_2x(
			packet,
			x1,
			y1,
			list[i].u1,
			list[i].v1,
			x2,
			y2,
			list[i].u2,
			list[i].v2
		);
	}
}

const float XYUV_MAX_FLOAT[4] qw_aligned = { 4095.9375f, 4095.9375f, 1023.9375f, 1023.9375f };
const float OWL_XYOFFSET_FLOAT[4] qw_aligned = { 320.0f, 224.0f, 1023.9375f, 1023.9375f };

/*
 * A textured quad from screen-space corners in triangle strip order
 * (top-left, bottom-left, top-right, bottom-right of the texture rectangle),
 * for images under a rotating view.
 */
static void emit_tex_quad(GSSURFACE *source, const float xs[4], const float ys[4],
	float startx, float starty, float endx, float endy, Color color)
{
	const float us[4] = { startx, startx, endx, endx };
	const float vs[4] = { starty, endy, starty, endy };

	int texture_id = graphics_surface_bind(source, true);
	if (texture_id == GRAPHICS_BIND_ERROR)
		return;

	owl_packet *packet = owl_query_packet(CHANNEL_VIF1, texture_upload_pending(texture_id) ? 19 : 15);

	owl_add_cnt_tag(packet, texture_upload_pending(texture_id) ? 18 : 14, 0);

	if (texture_upload_pending(texture_id)) {
		texture_manager_add_mark(packet, texture_id);
	}

	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(0, 0,
		texture_upload_pending(texture_id) ? VIF_FLUSHA : VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(13, 0, VIF_DIRECT, 0));

	owl_add_tag(packet, GIF_AD, GIFTAG(3, 1, 0, 0, 0, 1));

	int tw, th;
	athena_set_tw_th(source, &tw, &th);

	owl_add_tag(packet,
		GS_TEX0_1+gsGlobal->PrimContext,
		GS_SETREG_TEX0((source->Vram & ~GRAPHICS_TRANSFER_REQUEST_MASK)/256,
					  source->TBW,
					  source->PSM,
					  tw, th,
					  gsGlobal->PrimAlphaEnable,
					  COLOR_MODULATE,
					  (source->VramClut & ~GRAPHICS_TRANSFER_REQUEST_MASK)/256,
					  source->ClutPSM,
					  0, 0,
					  source->VramClut? GS_CLUT_STOREMODE_LOAD : GS_CLUT_STOREMODE_NOLOAD)
	);

	owl_add_tag(packet, GS_TEX1_1+gsGlobal->PrimContext, GS_SETREG_TEX1(1, 0, source->Filter, source->Filter, 0, 0, 0));

	owl_add_tag(packet, GS_RGBAQ, color);

	owl_add_tag(packet,
					   ((uint64_t)(GS_UV) << 0 | (uint64_t)(GS_XYZ2) << 4),
					   	VU_GS_GIFTAG(4,
							1, NO_CUSTOM_DATA, 1,
							VU_GS_PRIM(GS_PRIM_PRIM_TRISTRIP,
									   0, 1,
									   gsGlobal->PrimFogEnable,
									   gsGlobal->PrimAlphaEnable, gsGlobal->PrimAAEnable, 1, gsGlobal->PrimContext, 0),
    						0, 2)
						);

	/* PACKED UV and XYZ2: U/X in bits 0..15, V/Y in bits 32..47. */
	for (int i = 0; i < 4; i++) {
		owl_add_tag(packet, 0,
			(uint64_t)owl_uv_transform(us[i], source->Width) |
			((uint64_t)owl_uv_transform(vs[i], source->Height) << 32));
		owl_add_tag(packet, 1,
			(uint64_t)owl_coord_transform(xs[i], gsGlobal->OffsetX) |
			((uint64_t)owl_coord_transform(ys[i], gsGlobal->OffsetY) << 32));
	}
}

/* One vertex of draw_tex_rect_list(): REGLIST UV then XYZ2, one quadword. */
static inline void emit_tex_vertex(owl_packet *packet, GSSURFACE *source,
	float x, float y, float u, float v)
{
	view_apply_safe(&x, &y);
	owl_add_tag(packet,
		(uint64_t)(owl_coord_transform(x, gsGlobal->OffsetX)) |
		((uint64_t)(owl_coord_transform(y, gsGlobal->OffsetY)) << 16),
		GS_SETREG_UV(owl_uv_transform(u, source->Width),
			owl_uv_transform(v, source->Height)));
}

/* Rectangles [0, count) as two triangles each, in one packet. */
static void emit_tex_rect_triangles(GSSURFACE *source, const prim_tex_rect *list,
	int count, Color color)
{
	int texture_id = graphics_surface_bind(source, true);
	int upload, payload, tw, th;
	owl_packet *packet;

	if (texture_id == GRAPHICS_BIND_ERROR)
		return;
	upload = texture_upload_pending(texture_id);
	/* A+D tag, TEX0, TEX1, PRIM, RGBAQ, REGLIST tag, then 6 vertices each. */
	payload = 6 + 6 * count;
	packet = owl_query_packet(CHANNEL_VIF1, (upload ? 4 : 0) + 2 + payload);
	owl_add_cnt_tag(packet, (upload ? 4 : 0) + 1 + payload, 0);

	if (upload) {
		texture_manager_add_mark(packet, texture_id);
	}

	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(0, 0, upload ? VIF_FLUSHA : VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(payload, 0, VIF_DIRECT, 0));

	owl_add_tag(packet, GIF_AD, GIFTAG(4, 1, 0, 0, 0, 1));

	athena_set_tw_th(source, &tw, &th);
	owl_add_tag(packet,
		GS_TEX0_1+gsGlobal->PrimContext,
		GS_SETREG_TEX0((source->Vram & ~GRAPHICS_TRANSFER_REQUEST_MASK)/256,
					  source->TBW,
					  source->PSM,
					  tw, th,
					  gsGlobal->PrimAlphaEnable,
					  COLOR_MODULATE,
					  (source->VramClut & ~GRAPHICS_TRANSFER_REQUEST_MASK)/256,
					  source->ClutPSM,
					  0, 0,
					  source->VramClut? GS_CLUT_STOREMODE_LOAD : GS_CLUT_STOREMODE_NOLOAD)
	);
	owl_add_tag(packet, GS_TEX1_1+gsGlobal->PrimContext, GS_SETREG_TEX1(1, 0, source->Filter, source->Filter, 0, 0, 0));
	owl_add_tag(packet, GS_PRIM, VU_GS_PRIM(GS_PRIM_PRIM_TRIANGLE, 0, 1,
		gsGlobal->PrimFogEnable, gsGlobal->PrimAlphaEnable, gsGlobal->PrimAAEnable,
		1, gsGlobal->PrimContext, 0));
	owl_add_tag(packet, GS_RGBAQ, color);

	owl_add_tag(packet,
		((uint64_t)(GS_UV) << 0 | (uint64_t)(GS_XYZ2) << 4),
		VU_GS_GIFTAG(6 * count, 1, NO_CUSTOM_DATA, 0, 0, 1, 2));

	for (int i = 0; i < count; i++) {
		const prim_tex_rect *r = &list[i];

		emit_tex_vertex(packet, source, r->x1, r->y1, r->u1, r->v1);
		emit_tex_vertex(packet, source, r->x1, r->y2, r->u1, r->v2);
		emit_tex_vertex(packet, source, r->x2, r->y1, r->u2, r->v1);
		emit_tex_vertex(packet, source, r->x2, r->y1, r->u2, r->v1);
		emit_tex_vertex(packet, source, r->x1, r->y2, r->u1, r->v2);
		emit_tex_vertex(packet, source, r->x2, r->y2, r->u2, r->v2);
	}
}

void draw_tex_rect_list(GSSURFACE *source, const prim_tex_rect *list, int count,
	Color color)
{
	int capacity, offset = 0;

	if (!source || !list || count <= 0)
		return;
	/* Fixed part: DMA, upload marker, DIRECT and the six tags. */
	capacity = draw_list_capacity(12, 6);
	if (capacity <= 0)
		return;
	while (offset < count) {
		int n = count - offset;

		if (n > capacity)
			n = capacity;
		emit_tex_rect_triangles(source, list + offset, n, color);
		offset += n;
	}
}

void draw_image(GSSURFACE* source, float x, float y, float width, float height, float startx, float starty, float endx, float endy, Color color)
{
	if (source == cur_screen_buffer[0] || source == cur_screen_buffer[1] || source == cur_screen_buffer[2]) {
		flush_gs_texcache();
	}

	if (view_active()) {
		float xs[4], ys[4];

		if (!view_rect_corners(x, y, width, height, xs, ys))
			return;
		if (athena_view_kind() == ATHENA_VIEW_ROTATED) {
			emit_tex_quad(source, xs, ys, startx, starty, endx, endy, color);
			return;
		}
		/* Axis-aligned: still one sprite, from its transformed corners. */
		x = xs[0];
		y = ys[0];
		width = xs[3] - xs[0];
		height = ys[3] - ys[0];
	}

    int texture_id = graphics_surface_bind(source, true);
	if (texture_id == GRAPHICS_BIND_ERROR)
		return;

	owl_packet *packet = owl_query_packet(CHANNEL_VIF1, texture_upload_pending(texture_id) ? 13 : 9);

	owl_add_cnt_tag(packet, texture_upload_pending(texture_id) ? 12 : 8, 0);

	if (texture_upload_pending(texture_id)) {
		texture_manager_add_mark(packet, texture_id);
	}

	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(0, 0,
		texture_upload_pending(texture_id) ? VIF_FLUSHA : VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(7, 0, VIF_DIRECT, 0));
	
	owl_add_tag(packet, GIF_AD, GIFTAG(2, 1, 0, 0, 0, 1));

	int tw, th;
	athena_set_tw_th(source, &tw, &th);

	owl_add_tag(packet, 
		GS_TEX0_1+gsGlobal->PrimContext, 
		GS_SETREG_TEX0((source->Vram & ~GRAPHICS_TRANSFER_REQUEST_MASK)/256,
					  source->TBW, 
					  source->PSM,
					  tw, th, 
					  gsGlobal->PrimAlphaEnable, 
					  COLOR_MODULATE,
					  (source->VramClut & ~GRAPHICS_TRANSFER_REQUEST_MASK)/256,
					  source->ClutPSM, 
					  0, 0, 
					  source->VramClut? GS_CLUT_STOREMODE_LOAD : GS_CLUT_STOREMODE_NOLOAD)
	);
	
	owl_add_tag(packet, GS_TEX1_1+gsGlobal->PrimContext, GS_SETREG_TEX1(1, 0, source->Filter, source->Filter, 0, 0, 0));

	owl_add_tag(packet, 
					   ((uint64_t)(GS_PRIM) << 0 | (uint64_t)(GS_RGBAQ) << 4 | (uint64_t)(GS_UV) << 8 | (uint64_t)(GS_XYZ2) << 12 | (uint64_t)(GS_UV) << 16 | (uint64_t)(GS_XYZ2) << 20), 
					   	VU_GS_GIFTAG(1, 
							1, NO_CUSTOM_DATA, 0, 
							0,
    						1, 6) // REGLIST
						);
 
	owl_add_tag(packet, color, VU_GS_PRIM(GS_PRIM_PRIM_SPRITE, 0, 1, gsGlobal->PrimFogEnable, gsGlobal->PrimAlphaEnable, gsGlobal->PrimAAEnable, 1, gsGlobal->PrimContext, 0));
     
	owl_add_tag(packet, (uint64_t)(owl_coord_transform(x, gsGlobal->OffsetX)) | ((uint64_t)(owl_coord_transform(y, gsGlobal->OffsetY)) << 16), GS_SETREG_UV( owl_uv_transform(startx, source->Width), owl_uv_transform(starty, source->Height)));
	owl_add_tag(packet, (uint64_t)(owl_coord_transform(x+width, gsGlobal->OffsetX)) | ((uint64_t)(owl_coord_transform(y+height, gsGlobal->OffsetY)) << 16), GS_SETREG_UV( owl_uv_transform(endx, source->Width), owl_uv_transform(endy, source->Height)));
}

void draw_image_rotate(GSSURFACE* source, float x, float y, float width, float height, float startx, float starty, float endx, float endy, float angle, Color color){

	float c = cosf(angle);
	float s = sinf(angle);

	x += width/2;
	y += height/2;

	if (view_active()) {
		/* Corners rotated about the center, as below, then through the view. */
		const float dx[4] = { -width/2, -width/2, width/2, width/2 };
		const float dy[4] = { -height/2, height/2, -height/2, height/2 };
		float xs[4], ys[4], box[4];

		for (int i = 0; i < 4; i++) {
			xs[i] = dx[i]*c - dy[i]*s + x;
			ys[i] = dy[i]*c + dx[i]*s + y;
			view_apply_safe(&xs[i], &ys[i]);
		}
		view_box(xs, ys, 4, box);
		if (athena_view_screen_box_visible(box[0], box[1], box[2], box[3]))
			emit_tex_quad(source, xs, ys, startx, starty, endx, endy, color);
		else
			athena_view_count_culled(1);
		return;
	}

    int texture_id = graphics_surface_bind(source, true);
	if (texture_id == GRAPHICS_BIND_ERROR)
		return;

	owl_packet *packet = owl_query_packet(CHANNEL_VIF1, texture_upload_pending(texture_id) ? 19 : 15);

	owl_add_cnt_tag(packet, texture_upload_pending(texture_id) ? 18 : 14, 0);

	if (texture_upload_pending(texture_id)) {
		texture_manager_add_mark(packet, texture_id);
	}

	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(0, 0,
		texture_upload_pending(texture_id) ? VIF_FLUSHA : VIF_NOP, 0));
	owl_add_uint(packet, VIF_CODE(13, 0, VIF_DIRECT, 0)); 
	
	owl_add_tag(packet, GIF_AD, GIFTAG(3, 1, 0, 0, 0, 1));

	int tw, th;
	athena_set_tw_th(source, &tw, &th);

	owl_add_tag(packet, 
		GS_TEX0_1+gsGlobal->PrimContext, 
		GS_SETREG_TEX0((source->Vram & ~GRAPHICS_TRANSFER_REQUEST_MASK)/256,
					  source->TBW, 
					  source->PSM,
					  tw, th, 
					  gsGlobal->PrimAlphaEnable, 
					  COLOR_MODULATE,
					  (source->VramClut & ~GRAPHICS_TRANSFER_REQUEST_MASK)/256,
					  source->ClutPSM, 
					  0, 0, 
					  source->VramClut? GS_CLUT_STOREMODE_LOAD : GS_CLUT_STOREMODE_NOLOAD)
	);
	
	owl_add_tag(packet, GS_TEX1_1+gsGlobal->PrimContext, GS_SETREG_TEX1(1, 0, source->Filter, source->Filter, 0, 0, 0));

	owl_add_tag(packet, GS_RGBAQ, color);

	owl_add_tag(packet, 
					   ((uint64_t)(GS_UV) << 0 | (uint64_t)(GS_XYZ2) << 4), 
					   	VU_GS_GIFTAG(4, 
							1, NO_CUSTOM_DATA, 1, 
							VU_GS_PRIM(GS_PRIM_PRIM_TRISTRIP, 
									   0, 1, 
									   gsGlobal->PrimFogEnable, 
									   gsGlobal->PrimAlphaEnable, gsGlobal->PrimAAEnable, 1, gsGlobal->PrimContext, 0),
    						0, 2)
						);

	owl_add_tag(packet, 0, GS_SETREG_STQ( (int)(startx) << 4, (int)(starty) << 4 ));

	owl_add_tag(packet, 1, (uint64_t)((int)gsGlobal->OffsetX+((int)((-width/2)*c - (-height/2)*s+x) << 4)) | ((uint64_t)((int)gsGlobal->OffsetY+((int)((-height/2)*c + (-width/2)*s+y) << 4)) << 32));

	owl_add_tag(packet, 0, GS_SETREG_STQ( (int)(startx) << 4, (int)(endy) << 4 ));

	owl_add_tag(packet, 1, (uint64_t)((int)gsGlobal->OffsetX+((int)((-width/2)*c - height/2*s+x) << 4)) | ((uint64_t)((int)gsGlobal->OffsetY+((int)(height/2*c + (-width/2)*s+y) << 4)) << 32));

	owl_add_tag(packet, 0, GS_SETREG_STQ( (int)(endx) << 4, (int)(starty) << 4 ));

	owl_add_tag(packet, 1, (uint64_t)((int)gsGlobal->OffsetX+((int)(width/2*c - (-height/2)*s+x) << 4)) | ((uint64_t)((int)gsGlobal->OffsetY+((int)((-height/2)*c + width/2*s+y) << 4)) << 32));

	owl_add_tag(packet, 0, GS_SETREG_STQ( (int)(endx) << 4, (int)(endy) << 4 ));

	owl_add_tag(packet, 1, (uint64_t)((int)gsGlobal->OffsetX+((int)(width/2*c - height/2*s+x) << 4)) | ((uint64_t)((int)gsGlobal->OffsetY+((int)(height/2*c + width/2*s+y) << 4)) << 32));
}

void draw_point(float x, float y, Color color)
{
	if (view_active())
		view_apply_safe(&x, &y);

	owl_packet *packet = owl_query_packet(CHANNEL_VIF1, 5);

	owl_add_cnt_tag_fill(packet, 4); 
	owl_add_direct(packet, 3);

	owl_add_tag(packet, 
					   ((uint64_t)(GIF_NOP) << 0 | (uint64_t)(GS_PRIM) << 4 | (uint64_t)(GS_RGBAQ) << 8 | (uint64_t)(GS_XYZ2) << 12), 
					   	VU_GS_GIFTAG(1, 
							1, NO_CUSTOM_DATA, 0, 
							0,
    						1, 4)
						);

	owl_add_tag(packet, VU_GS_PRIM(GS_PRIM_PRIM_POINT, 0, 0, gsGlobal->PrimFogEnable, gsGlobal->PrimAlphaEnable, gsGlobal->PrimAAEnable, 1, gsGlobal->PrimContext, 0), 0);

	owl_add_rgba_xy(packet, color, draw_coord(x), draw_coord(y));
} 

void draw_line(float x, float y, float x2, float y2, Color color) 
{
	if (view_active()) {
		view_apply_safe(&x, &y);
		view_apply_safe(&x2, &y2);
	}

	owl_packet *packet = owl_query_packet(CHANNEL_VIF1, 5);

	owl_add_cnt_tag_fill(packet, 4); 
	owl_add_direct(packet, 3);

	owl_add_tag(packet, 
					   ((uint64_t)(GS_PRIM)  << 0 | (uint64_t)(GS_RGBAQ)  << 4 | (uint64_t)(GS_XYZ2) << 8 | (uint64_t)(GS_XYZ2) << 12), 
					   	VU_GS_GIFTAG(1, 
							1, NO_CUSTOM_DATA, 0, 
							0,
    						1, 4)
						);

	owl_add_tag(packet, color, VU_GS_PRIM(GS_PRIM_PRIM_LINE, 0, 0, gsGlobal->PrimFogEnable, gsGlobal->PrimAlphaEnable, gsGlobal->PrimAAEnable, 1, gsGlobal->PrimContext, 0));

	owl_add_xy_2x(packet, draw_coord(x), draw_coord(y),
		draw_coord(x2), draw_coord(y2));
}


static void emit_triangle(float x, float y, float x2, float y2, float x3, float y3, Color color);

/* A flat rectangle between screen corners (x, y) and (x2, y2). */
static void emit_sprite(float x, float y, float x2, float y2, Color color)
{
	owl_packet *packet = owl_query_packet(CHANNEL_VIF1, 5);

	owl_add_cnt_tag_fill(packet, 4);
	owl_add_direct(packet, 3);

	owl_add_tag(packet, 
		((uint64_t)(GS_PRIM)  << 0 | (uint64_t)(GS_RGBAQ)  << 4 | (uint64_t)(GS_XYZ2) << 8 | (uint64_t)(GS_XYZ2) << 12), 
					   	VU_GS_GIFTAG(1, 
							1, NO_CUSTOM_DATA, 0, 
							0,
    						1, 4)
						);

	owl_add_tag(packet, color, VU_GS_PRIM(GS_PRIM_PRIM_SPRITE, 0, 0, gsGlobal->PrimFogEnable, gsGlobal->PrimAlphaEnable, gsGlobal->PrimAAEnable, 1, gsGlobal->PrimContext, 0));

	owl_add_xy_2x(packet, draw_coord(x), draw_coord(y),
		draw_coord(x2), draw_coord(y2));
}

void draw_rect_f(float x, float y, float width, float height, Color color)
{
	if (view_active()) {
		float xs[4], ys[4];

		if (!view_rect_corners(x, y, width, height, xs, ys))
			return;
		if (athena_view_kind() == ATHENA_VIEW_ROTATED) {
			emit_triangle(xs[0], ys[0], xs[1], ys[1], xs[2], ys[2], color);
			emit_triangle(xs[2], ys[2], xs[1], ys[1], xs[3], ys[3], color);
		} else {
			emit_sprite(xs[0], ys[0], xs[3], ys[3], color);
		}
		return;
	}
	emit_sprite(x, y, x + width, y + height, color);
}

void draw_sprite(float x, float y, int width, int height, Color color)
{
	draw_rect_f(x, y, (float)width, (float)height, color);
}

static void emit_triangle(float x, float y, float x2, float y2, float x3, float y3, Color color)
{
	owl_packet *packet = owl_query_packet(CHANNEL_VIF1, 6);

	owl_add_cnt_tag_fill(packet, 5); 
	owl_add_uint(packet, VIF_NOP);
	owl_add_uint(packet, VIF_NOP);
	owl_add_uint(packet, VIF_NOP);
	owl_add_uint(packet, (VIF_DIRECT << 24) | 4); 

	owl_add_tag(packet, 
					   ((uint64_t)(GS_PRIM)  << 0 | (uint64_t)(GS_RGBAQ)  << 4 | (uint64_t)(GS_XYZ2) << 8 | (uint64_t)(GS_XYZ2) << 12 | (uint64_t)(GS_XYZ2) << 16), 
					   	VU_GS_GIFTAG(1, 
							1, NO_CUSTOM_DATA, 0, 
							0,
    						1, 6)
						);

	owl_add_tag(packet, color, VU_GS_PRIM(GS_PRIM_PRIM_TRIANGLE, 0, 0, gsGlobal->PrimFogEnable, gsGlobal->PrimAlphaEnable, gsGlobal->PrimAAEnable, 1, gsGlobal->PrimContext, 0));

	owl_add_xy_2x(packet, draw_coord(x), draw_coord(y),
		draw_coord(x2), draw_coord(y2));

	owl_add_xy_2x(packet, draw_coord(x3), draw_coord(y3), 0, 0);
}

void draw_triangle(float x, float y, float x2, float y2, float x3, float y3, Color color)
{
	if (view_active()) {
		view_apply_safe(&x, &y);
		view_apply_safe(&x2, &y2);
		view_apply_safe(&x3, &y3);
	}
	emit_triangle(x, y, x2, y2, x3, y3, color);
}

void draw_triangle_gouraud(float x, float y, float x2, float y2, float x3, float y3, Color color, Color color2, Color color3)
{
	if (view_active()) {
		view_apply_safe(&x, &y);
		view_apply_safe(&x2, &y2);
		view_apply_safe(&x3, &y3);
	}

	owl_packet *packet = owl_query_packet(CHANNEL_VIF1, 8);

	owl_add_cnt_tag_fill(packet, 7); 
	owl_add_direct(packet, 6);
	
	owl_add_tag(packet, GIF_AD, GIFTAG(1, 1, 0, 0, 0, 1));

	owl_add_tag(packet, GS_PRIM, VU_GS_PRIM(GS_PRIM_PRIM_TRIANGLE, 1, 0, gsGlobal->PrimFogEnable, gsGlobal->PrimAlphaEnable, gsGlobal->PrimAAEnable, 1, gsGlobal->PrimContext, 0));

	owl_add_tag(packet, 
					   ((uint64_t)(GS_RGBAQ)  << 0 | (uint64_t)(GS_XYZ2) << 4), 
					   	VU_GS_GIFTAG(3, 
							1, NO_CUSTOM_DATA, 0, 
							0,
    						1, 2)
						);

	owl_add_rgba_xy(packet, color, draw_coord(x), draw_coord(y));
	owl_add_rgba_xy(packet, color2, draw_coord(x2), draw_coord(y2));
	owl_add_rgba_xy(packet, color3, draw_coord(x3), draw_coord(y3));
}

void draw_quad(float x, float y, float x2, float y2, float x3, float y3, float x4, float y4, Color color)
{
	draw_triangle(x, y, x2, y2, x3, y3, color);
	draw_triangle(x, y, x3, y3, x4, y4, color);
}

void draw_quad_gouraud(float x, float y, float x2, float y2, float x3, float y3, float x4, float y4, Color color, Color color2, Color color3, Color color4)
{
	draw_triangle_gouraud(x, y, x2, y2, x3, y3, color, color2, color3);
	draw_triangle_gouraud(x, y, x3, y3, x4, y4, color, color3, color4);
}

static void draw_circle_view(float x, float y, float radius, u64 color, u8 filled);

void draw_circle(float x, float y, float radius, u64 color, u8 filled)
{
	if (view_active()) {
		draw_circle_view(x, y, radius, color, filled);
		return;
	}

	int fill_factor = (18 + (int)(!filled));
	owl_packet *packet = owl_query_packet(CHANNEL_VIF1, (6 + fill_factor));

	owl_add_cnt_tag_fill(packet, (5 + fill_factor)); 
	owl_add_direct(packet, 4 + fill_factor);
	
	owl_add_tag(packet, GIF_AD, GIFTAG(2, 1, 0, 0, 0, 1));

	owl_add_tag(packet, GS_PRIM, VU_GS_PRIM(filled? GS_PRIM_PRIM_TRIFAN : GS_PRIM_PRIM_LINESTRIP, 0, 0, 
		gsGlobal->PrimFogEnable, gsGlobal->PrimAlphaEnable, gsGlobal->PrimAAEnable, 1, gsGlobal->PrimContext, 0));

	owl_add_tag(packet, GS_RGBAQ, color);

	owl_add_tag(packet, 
					   ((uint64_t)(GS_XYZ2) << 0), 
					   	VU_GS_GIFTAG((36 + (int)(!filled)), 
							1, NO_CUSTOM_DATA, 0, 
							0,
    						1, 1)
						);

	for (int a = 0; a < 36; a++) {
		float angle = (float)a * (2.0f * (float)M_PI / 36.0f);
		owl_add_xy(packet, draw_coord(athena_cosf(angle) * radius + x),
			draw_coord(athena_sinf(angle) * radius + y));
	}

	if (!filled) { // using 2x to round it to 38
		owl_add_xy_2x(packet, 
			radius + x, 
			y, 
			0, 
			0
		);
	}
}

/*
 * draw_circle() under a view: the 36 vertices are placed in world space and
 * each goes through the view, so a zoom on one axis gives an ellipse.
 */
static void draw_circle_view(float x, float y, float radius, u64 color, u8 filled)
{
	int vertex_count = 36 + (int)(!filled);
	int xs[37], ys[37];
	float box[4];
	AthenaRect2D bounds = { x - fabsf(radius), y - fabsf(radius),
		x + fabsf(radius), y + fabsf(radius) };
	AthenaRect2D screen;

	athena_affine_bounds(&athena_view_matrix, &bounds, &screen);
	box[0] = screen.x0; box[1] = screen.y0; box[2] = screen.x1; box[3] = screen.y1;
	if (!athena_view_screen_box_visible(box[0], box[1], box[2], box[3])) {
		athena_view_count_culled(1);
		return;
	}

	for (int a = 0; a < 36; a++) {
		float angle = (float)a * (2.0f * (float)M_PI / 36.0f);
		view_point(athena_cosf(angle) * radius + x,
			athena_sinf(angle) * radius + y, &xs[a], &ys[a]);
	}
	/* An outline closes on its first vertex. */
	xs[36] = xs[0];
	ys[36] = ys[0];

	owl_packet *packet = owl_query_packet(CHANNEL_VIF1, 5 + (vertex_count + 1) / 2 + 1);

	owl_add_cnt_tag_fill(packet, 5 + (vertex_count + 1) / 2);
	owl_add_direct(packet, 4 + (vertex_count + 1) / 2);

	owl_add_tag(packet, GIF_AD, GIFTAG(2, 1, 0, 0, 0, 1));

	owl_add_tag(packet, GS_PRIM, VU_GS_PRIM(filled? GS_PRIM_PRIM_TRIFAN : GS_PRIM_PRIM_LINESTRIP, 0, 0,
		gsGlobal->PrimFogEnable, gsGlobal->PrimAlphaEnable, gsGlobal->PrimAAEnable, 1, gsGlobal->PrimContext, 0));

	owl_add_tag(packet, GS_RGBAQ, color);

	/* REGLIST of XYZ2: two vertices per quadword, the last one padded. */
	owl_add_tag(packet,
					   ((uint64_t)(GS_XYZ2) << 0),
					   	VU_GS_GIFTAG(vertex_count,
							1, NO_CUSTOM_DATA, 0,
							0,
    						1, 1)
						);

	for (int i = 0; i < vertex_count; i += 2) {
		if (i + 1 < vertex_count)
			owl_add_xy_2x(packet, xs[i], ys[i], xs[i + 1], ys[i + 1]);
		else
			owl_add_xy_2x(packet, xs[i], ys[i], 0, 0);
	}
}
