#ifndef ATHENA_GRAPHICS_VIEW_H
#define ATHENA_GRAPHICS_VIEW_H

#include <stdbool.h>
#include <stdint.h>

/*
 * 2D view: the transform every 2D draw (Draw, Image, Font, TileMap) applies
 * to its coordinates before they reach the GS. It is the identity unless a
 * camera sets it, and draws take their original code path while it is, so
 * code that never touches the view is unaffected.
 *
 *     screen.x = xx * x + xy * y + tx
 *     screen.y = yx * x + yy * y + ty
 *
 * The view is global to the main thread, like the GS state. It is not reset
 * by a flip: whoever sets it restores it (Camera2D does, at the end of each
 * camera's drawing).
 *
 * This header has no PS2 dependencies, so the math is testable on a host.
 */
typedef struct {
	float xx, xy;
	float yx, yy;
	float tx, ty;
} AthenaAffine2D;

typedef enum {
	/* Draws are sent untouched. */
	ATHENA_VIEW_IDENTITY,
	/* Translation and scale: rectangles stay GS sprites. */
	ATHENA_VIEW_AXIS,
	/* Rotation or shear: rectangles become two triangles. */
	ATHENA_VIEW_ROTATED,
} AthenaViewKind;

/* An axis-aligned rectangle, x0 <= x1 and y0 <= y1. */
typedef struct {
	float x0, y0;
	float x1, y1;
} AthenaRect2D;

extern AthenaAffine2D athena_view_matrix;
extern AthenaViewKind athena_view_current_kind;

/*
 * Draws skipped by culling since the counter was last reset (Camera2D
 * does at the start of each frame's drawing): images, rectangles and
 * circles, sprites of drawList, whole texts and TileMap grid cells.
 */
extern uint32_t athena_view_culled;

static inline void athena_view_count_culled(uint32_t count)
{
	athena_view_culled += count;
}

static inline AthenaViewKind athena_view_kind(void)
{
	return athena_view_current_kind;
}

static inline void athena_affine_apply(const AthenaAffine2D *m, float x,
	float y, float *out_x, float *out_y)
{
	float sx = m->xx * x + m->xy * y + m->tx;
	float sy = m->yx * x + m->yy * y + m->ty;

	*out_x = sx;
	*out_y = sy;
}

/* Current view applied to one point. */
static inline void athena_view_apply(float x, float y, float *out_x,
	float *out_y)
{
	athena_affine_apply(&athena_view_matrix, x, y, out_x, out_y);
}

void athena_affine_identity(AthenaAffine2D *m);

/* out = a after b: out(p) = a(b(p)). `out` may alias either argument. */
void athena_affine_multiply(const AthenaAffine2D *a, const AthenaAffine2D *b,
	AthenaAffine2D *out);

/* false (and `out` untouched) when `m` is not invertible. */
bool athena_affine_invert(const AthenaAffine2D *m, AthenaAffine2D *out);

AthenaViewKind athena_affine_classify(const AthenaAffine2D *m);

/* Bounding box of the rectangle `rect` transformed by `m`. */
void athena_affine_bounds(const AthenaAffine2D *m, const AthenaRect2D *rect,
	AthenaRect2D *out);

/*
 * Sets the view; NULL restores the identity. Matrices within a rounding
 * error of the identity or of an axis-aligned transform are classified as
 * such, so they keep the faster paths.
 */
void athena_view_set(const AthenaAffine2D *m);
void athena_view_get(AthenaAffine2D *m);

/* sqrt(|determinant|): how much the view scales lengths such as radii. */
float athena_view_scale(void);

/*
 * The world view of the frame: the transform of the camera the game draws
 * its world with (Camera2D.current), published so screen-space overlays such
 * as Debug can place world-space shapes. NULL clears it.
 */
void athena_view_set_world(const AthenaAffine2D *m);
bool athena_view_get_world(AthenaAffine2D *m);

/*
 * Clip rectangle: the part of the framebuffer draws may touch, set through
 * the GS scissor (graphics.c; the GS keeps it until the next flip, which
 * restores the whole framebuffer). Pixels from (x, y) to (x + width - 1,
 * y + height - 1); a width or height <= 0 means the whole framebuffer.
 * Values are clamped to the framebuffer.
 */
void athena_view_set_clip(int x, int y, int width, int height);
void athena_view_get_clip(int *x, int *y, int *width, int *height);

/* Size of the framebuffer draws go to (640x448 before the GS starts). */
void athena_view_screen_size(int *width, int *height);

/*
 * World-space bounding box of what the current view shows inside the clip
 * rectangle: what a draw must overlap to be visible. false when the view is
 * not invertible.
 */
bool athena_view_visible_bounds(AthenaRect2D *world);

/* Whether a screen-space box overlaps the clip rectangle. */
bool athena_view_screen_box_visible(float x0, float y0, float x1, float y1);

/*
 * Culling many world rectangles against the current view: init reads the
 * view and the clip rectangle once, then each test is a few multiplies.
 * init returns false while the view is the identity, when draws are never
 * culled (they behave as before any camera existed).
 */
typedef struct {
	AthenaAffine2D m;
	AthenaRect2D clip;
	bool rotated;
} AthenaViewCuller;

bool athena_view_culler_init(AthenaViewCuller *culler);

/* Whether the world rectangle (x, y, w, h) shows inside the clip rectangle. */
static inline bool athena_view_culler_visible(const AthenaViewCuller *culler,
	float x, float y, float w, float h)
{
	const AthenaAffine2D *m = &culler->m;
	float x0, x1, y0, y1;

	if (culler->rotated) {
		AthenaRect2D rect = { x, y, x + w, y + h }, box;

		athena_affine_bounds(m, &rect, &box);
		x0 = box.x0; x1 = box.x1; y0 = box.y0; y1 = box.y1;
	} else {
		x0 = m->xx * x + m->tx;
		x1 = m->xx * (x + w) + m->tx;
		y0 = m->yy * y + m->ty;
		y1 = m->yy * (y + h) + m->ty;
		if (x0 > x1) { float t = x0; x0 = x1; x1 = t; }
		if (y0 > y1) { float t = y0; y0 = y1; y1 = t; }
	}
	return x1 >= culler->clip.x0 && x0 <= culler->clip.x1 &&
		y1 >= culler->clip.y0 && y0 <= culler->clip.y1;
}

#endif
