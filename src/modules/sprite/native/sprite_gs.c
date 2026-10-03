#include <stddef.h>

#include <athena/sprite.h>

/*
 * Drawing sprite frames through the Image module, which applies the 2D view
 * (Camera2D) and culls. The placement itself is athena_sprite_quad() and
 * athena_sprite_corners(), tested on a host.
 */

/* A frame turned in the atlas: two triangles with its corners' texels. */
static void draw_turned(AthenaImage *image, const AthenaSpriteSheet *sheet,
	uint32_t frame, const AthenaSpriteDraw *draw)
{
	AthenaSpriteCorners corners;

	if (athena_sprite_corners(&sheet->frames[frame], draw, sheet->inset, &corners))
		athena_image_draw_quad(image, corners.x, corners.y, corners.u, corners.v,
			draw->color);
}

void athena_sprite_draw(AthenaImage *image, const AthenaSpriteSheet *sheet,
	uint32_t frame, const AthenaSpriteDraw *draw)
{
	AthenaSpriteQuad quad;

	if (!image || frame >= sheet->frame_count)
		return;
	if (sheet->frames[frame].rotated) {
		draw_turned(image, sheet, frame, draw);
		return;
	}
	if (!athena_sprite_quad(&sheet->frames[frame], draw, sheet->inset, &quad))
		return;
	athena_image_draw(image, quad.x, quad.y, quad.w, quad.h, quad.u1, quad.v1,
		quad.u2, quad.v2, quad.angle, draw->color);
}

static void batch_flush(AthenaSpriteBatch *batch)
{
	if (batch->queued > 0 && batch->image)
		athena_image_draw_list(batch->image, 0.0f, 0.0f, batch->chunk, batch->queued);
	batch->queued = 0;
}

void athena_sprite_batch_begin(AthenaSpriteBatch *batch)
{
	batch->image = NULL;
	batch->queued = 0;
	batch->drawn = batch->culled = 0;
	/* The view is read once: a batch belongs to one camera. */
	batch->cull = athena_view_culler_init(&batch->culler);
}

void athena_sprite_batch_add(AthenaSpriteBatch *batch, AthenaImage *image,
	const AthenaSpriteSheet *sheet, uint32_t frame, const AthenaSpriteDraw *draw)
{
	AthenaSpriteQuad quad;
	prim_tex_sprite *sprite;

	if (!image || !athena_image_is_loaded(image) || frame >= sheet->frame_count)
		return;
	/* Turned sprites and frames cannot be GS sprites: drawn alone, keeping the order. */
	if (sheet->frames[frame].rotated) {
		batch_flush(batch);
		draw_turned(image, sheet, frame, draw);
		batch->drawn++;
		return;
	}
	if (!athena_sprite_quad(&sheet->frames[frame], draw, sheet->inset, &quad))
		return;
	if (quad.angle != 0.0f) {
		batch_flush(batch);
		athena_image_draw(image, quad.x, quad.y, quad.w, quad.h, quad.u1, quad.v1,
			quad.u2, quad.v2, quad.angle, draw->color);
		batch->drawn++;
		return;
	}
	if (batch->cull && !athena_view_culler_visible(&batch->culler, quad.x, quad.y,
			quad.w, quad.h)) {
		athena_view_count_culled(1);
		batch->culled++;
		return;
	}
	if (image != batch->image || batch->queued == ATHENA_SPRITE_BATCH_CHUNK) {
		batch_flush(batch);
		batch->image = image;
	}
	sprite = &batch->chunk[batch->queued++];
	sprite->x = quad.x;
	sprite->y = quad.y;
	sprite->w = quad.w;
	sprite->h = quad.h;
	sprite->u1 = quad.u1;
	sprite->v1 = quad.v1;
	sprite->u2 = quad.u2;
	sprite->v2 = quad.v2;
	sprite->rgba = draw->color;
	batch->drawn++;
}

void athena_sprite_batch_end(AthenaSpriteBatch *batch)
{
	batch_flush(batch);
	batch->image = NULL;
}

/* Debug colors, as the GS takes them: red in the low byte, alpha 0x80 opaque. */
#define DEBUG_OUTLINE 0x8000FF00u   /* green */
#define DEBUG_ORIGIN  0x800000FFu   /* red */
#define DEBUG_SLICE   0x8000FFFFu   /* yellow */
#define DEBUG_CROSS   4.0f
/* Lines: 4 for the outline, 2 for the origin, 4 per slice. */
#define DEBUG_MAX_LINES 64

static void debug_line(prim_line *lines, int *count, float x, float y, float x2,
	float y2, uint32_t color)
{
	if (*count >= DEBUG_MAX_LINES)
		return;
	lines[*count].x = x;
	lines[*count].y = y;
	lines[*count].x2 = x2;
	lines[*count].y2 = y2;
	lines[*count].rgba = color;
	(*count)++;
}

void athena_sprite_draw_debug(const AthenaSpriteSheet *sheet, uint32_t frame,
	const AthenaSpriteDraw *draw)
{
	static const int edges[4][2] = { { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 } };
	/* Off the stack: scripts run on 8 KB stacks. Main thread only. */
	static prim_line lines[DEBUG_MAX_LINES];
	AthenaSpriteCorners corners;
	AthenaSpriteRect rect, placed;
	int count = 0, i;
	uint32_t s;

	if (frame >= sheet->frame_count)
		return;
	/* The outline follows rotation; slices, like getSlice(), do not. */
	if (athena_sprite_corners(&sheet->frames[frame], draw, 0.0f, &corners))
		for (i = 0; i < 4; i++)
			debug_line(lines, &count, corners.x[edges[i][0]], corners.y[edges[i][0]],
				corners.x[edges[i][1]], corners.y[edges[i][1]], DEBUG_OUTLINE);
	debug_line(lines, &count, draw->x - DEBUG_CROSS, draw->y, draw->x + DEBUG_CROSS,
		draw->y, DEBUG_ORIGIN);
	debug_line(lines, &count, draw->x, draw->y - DEBUG_CROSS, draw->x,
		draw->y + DEBUG_CROSS, DEBUG_ORIGIN);
	for (s = 0; s < sheet->slice_count; s++) {
		if (!athena_sprite_sheet_slice(sheet, (int)s, frame, &rect) ||
			!athena_sprite_place_rect(&sheet->frames[frame], draw, &rect, &placed))
			continue;
		debug_line(lines, &count, placed.x, placed.y, placed.x + placed.w, placed.y,
			DEBUG_SLICE);
		debug_line(lines, &count, placed.x + placed.w, placed.y, placed.x + placed.w,
			placed.y + placed.h, DEBUG_SLICE);
		debug_line(lines, &count, placed.x + placed.w, placed.y + placed.h, placed.x,
			placed.y + placed.h, DEBUG_SLICE);
		debug_line(lines, &count, placed.x, placed.y + placed.h, placed.x, placed.y,
			DEBUG_SLICE);
	}
	if (count > 0)
		draw_line_list(0.0f, 0.0f, lines, count);
}
