#include <math.h>
#include <stddef.h>

#include <athena/collision.h>
#include <athena/graphics.h>
#include <athena/graphics/view.h>

/*
 * Debug outlines of a world, drawn as line lists (and circles) through the
 * 2D view, so they follow the Camera2D. Only what the view shows is drawn.
 */

/* Colors as the GS takes them: red in the low byte, alpha 0x80 opaque. */
#define COLOR_STATIC    0x80FFFFFFu   /* white */
#define COLOR_KINEMATIC 0x80FF8000u   /* blue */
#define COLOR_DYNAMIC   0x8000FF00u   /* green */
#define COLOR_SENSOR    0x8000FFFFu   /* yellow */
#define COLOR_TILE      0x800000FFu   /* red */
#define COLOR_ONE_WAY   0x800080FFu   /* orange */

#define DEBUG_LINES 128
/* Bodies and tiles drawn at most: zoomed far out, the rest is skipped. */
#define DEBUG_MAX_BODIES 1024
#define DEBUG_MAX_TILES 16384

typedef struct {
	prim_line lines[DEBUG_LINES];
	int count;
} LineBatch;

/* Off the stack: scripts run on 8 KB stacks. Main thread only. */
static LineBatch batch;
static AthenaBodyId visible[DEBUG_MAX_BODIES];

static void flush(void)
{
	if (batch.count > 0)
		draw_line_list(0.0f, 0.0f, batch.lines, batch.count);
	batch.count = 0;
}

static void line(float x, float y, float x2, float y2, uint32_t color)
{
	prim_line *item;

	if (batch.count == DEBUG_LINES)
		flush();
	item = &batch.lines[batch.count++];
	item->x = x;
	item->y = y;
	item->x2 = x2;
	item->y2 = y2;
	item->rgba = color;
}

static bool solid_at(const AthenaCollisionGridDesc *grid, int32_t column, int32_t row)
{
	uint16_t id;

	if (column < 0 || row < 0 || (uint32_t)column >= grid->columns ||
		(uint32_t)row >= grid->rows)
		return false;
	id = grid->tiles[(size_t)row * grid->columns + (uint32_t)column];
	return id < grid->kind_count && grid->kinds[id].type == ATHENA_TILE_SOLID;
}

/* Cell of a coordinate, kept far from the int32 limits. */
static int32_t cell(float v, float origin, float size)
{
	float c = floorf((v - origin) / size);

	return (int32_t)(c < -1.0e8f ? -1.0e8f : c > 1.0e8f ? 1.0e8f : c);
}

/* Solid tiles show only the edges they share with non-solid cells. */
static void draw_tiles(const AthenaCollisionGridDesc *grid, const AthenaRect2D *view)
{
	float tw = grid->tile_width, th = grid->tile_height;
	int32_t c0 = cell(view->x0, grid->x, tw) - 1, r0 = cell(view->y0, grid->y, th) - 1;
	int32_t c1 = cell(view->x1, grid->x, tw) + 1, r1 = cell(view->y1, grid->y, th) + 1;
	int32_t c, r;

	if (c0 < 0)
		c0 = 0;
	if (r0 < 0)
		r0 = 0;
	if (c1 >= (int32_t)grid->columns)
		c1 = (int32_t)grid->columns - 1;
	if (r1 >= (int32_t)grid->rows)
		r1 = (int32_t)grid->rows - 1;
	if (c0 > c1 || r0 > r1 ||
		(int64_t)(c1 - c0 + 1) * (r1 - r0 + 1) > DEBUG_MAX_TILES)
		return;
	for (r = r0; r <= r1; r++) {
		for (c = c0; c <= c1; c++) {
			uint16_t id = grid->tiles[(size_t)r * grid->columns + (uint32_t)c];
			const AthenaTileKind *kind;
			float x0 = grid->x + (float)c * tw, y0 = grid->y + (float)r * th;
			float x1 = x0 + tw, y1 = y0 + th;

			if (id >= grid->kind_count)
				continue;
			kind = &grid->kinds[id];
			switch (kind->type) {
			case ATHENA_TILE_SOLID:
				if (!solid_at(grid, c, r - 1))
					line(x0, y0, x1, y0, COLOR_TILE);
				if (!solid_at(grid, c, r + 1))
					line(x0, y1, x1, y1, COLOR_TILE);
				if (!solid_at(grid, c - 1, r))
					line(x0, y0, x0, y1, COLOR_TILE);
				if (!solid_at(grid, c + 1, r))
					line(x1, y0, x1, y1, COLOR_TILE);
				break;
			case ATHENA_TILE_ONE_WAY:
				line(x0, y0, x1, y0, COLOR_ONE_WAY);
				break;
			case ATHENA_TILE_SLOPE: {
				float yl = y1 - kind->left * th, yr = y1 - kind->right * th;

				line(x0, yl, x1, yr, COLOR_TILE);
				if (kind->left > kind->right && yl < y1)
					line(x0, yl, x0, y1, COLOR_TILE);
				if (kind->right > kind->left && yr < y1)
					line(x1, yr, x1, y1, COLOR_TILE);
				break;
			}
			default:
				break;
			}
		}
	}
}

static uint32_t body_color(const AthenaBody *body)
{
	if (body->sensor)
		return COLOR_SENSOR;
	switch (body->type) {
	case ATHENA_BODY_KINEMATIC:
		return COLOR_KINEMATIC;
	case ATHENA_BODY_DYNAMIC:
		return COLOR_DYNAMIC;
	default:
		return COLOR_STATIC;
	}
}

static void draw_bodies(AthenaCollisionWorld *world, const AthenaRect2D *view)
{
	uint32_t count = athena_collision_query_rect(world, view->x0, view->y0,
		view->x1 - view->x0, view->y1 - view->y0, 0xFFFFFFFFu, visible, DEBUG_MAX_BODIES);
	uint32_t i;

	if (count > DEBUG_MAX_BODIES)
		count = DEBUG_MAX_BODIES;
	for (i = 0; i < count; i++) {
		const AthenaBody *body = athena_collision_get(world, visible[i]);
		uint32_t color;

		if (!body)
			continue;
		color = body_color(body);
		if (body->shape == ATHENA_SHAPE_CIRCLE) {
			/* Circles are not lines: keep the order with the batch. */
			flush();
			draw_circle(body->x, body->y, body->r, color, 0);
			continue;
		}
		line(body->x, body->y, body->x + body->w, body->y, color);
		line(body->x + body->w, body->y, body->x + body->w, body->y + body->h, color);
		line(body->x + body->w, body->y + body->h, body->x, body->y + body->h, color);
		line(body->x, body->y + body->h, body->x, body->y, color);
		if (body->on_ground)
			line(body->x + 2.0f, body->y + body->h - 2.0f, body->x + body->w - 2.0f,
				body->y + body->h - 2.0f, color);
	}
}

void athena_collision_draw_debug(AthenaCollisionWorld *world,
	const AthenaCollisionDebug *debug)
{
	AthenaCollisionGridDesc grid;
	AthenaRect2D view;

	if (!athena_view_visible_bounds(&view))
		return;
	batch.count = 0;
	if (debug->tiles && athena_collision_get_grid(world, &grid))
		draw_tiles(&grid, &view);
	if (debug->bodies)
		draw_bodies(world, &view);
	flush();
}
