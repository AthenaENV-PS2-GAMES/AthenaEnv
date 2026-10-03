#include <math.h>
#include <stddef.h>

#include <athena/graphics.h>
#include <athena/camera2d.h>

/*
 * Applies cameras to the GS through the 2D view and its clip rectangle
 * (athena/graphics/view.h). Each begin saves both; each end restores them.
 */

typedef struct {
	AthenaAffine2D view;
	int clip_x, clip_y, clip_w, clip_h;
} CameraSaved;

static CameraSaved camera_stack[ATHENA_CAMERA2D_MAX_DEPTH];
static int camera_depth;

static bool camera_push(void)
{
	CameraSaved *saved;

	if (camera_depth >= ATHENA_CAMERA2D_MAX_DEPTH)
		return false;
	saved = &camera_stack[camera_depth++];
	athena_view_get(&saved->view);
	athena_view_get_clip(&saved->clip_x, &saved->clip_y, &saved->clip_w,
		&saved->clip_h);
	return true;
}

static void camera_pop(void)
{
	CameraSaved *saved;

	if (camera_depth == 0)
		return;
	saved = &camera_stack[--camera_depth];
	athena_view_set(&saved->view);
	athena_view_set_clip(saved->clip_x, saved->clip_y, saved->clip_w,
		saved->clip_h);
}

/* Whole pixels covered by a float rectangle, for the scissor. */
static void camera_clip_rect(float x0, float y0, float x1, float y1)
{
	int ix = (int)floorf(x0 + 0.5f);
	int iy = (int)floorf(y0 + 0.5f);
	int iw = (int)floorf(x1 + 0.5f) - ix;
	int ih = (int)floorf(y1 + 0.5f) - iy;

	/* An empty viewport keeps one pixel rather than the whole screen. */
	athena_view_set_clip(ix, iy, iw > 0 ? iw : 1, ih > 0 ? ih : 1);
}

bool athena_camera2d_begin(const AthenaCamera2D *cam, float parallax_x,
	float parallax_y)
{
	AthenaAffine2D m;
	AthenaRect2D vp;

	if (!cam || !camera_push())
		return false;
	athena_camera2d_viewport(cam, &vp);
	athena_camera2d_matrix(cam, parallax_x, parallax_y, &m);
	camera_clip_rect(vp.x0, vp.y0, vp.x1, vp.y1);
	athena_view_set(&m);
	return true;
}

static Color camera_color(uint32_t rgb, float alpha)
{
	uint32_t a = (uint32_t)(alpha + 0.5f);

	if (a > 128)
		a = 128;
	return (Color)(rgb & 0xFFFFFF) | ((Color)a << 24);
}

/* Fade, flash and letterbox over the viewport, in screen space. */
static void camera_draw_effects(const AthenaCamera2D *cam)
{
	AthenaRect2D vp;
	int x, y, w, h;

	athena_camera2d_viewport(cam, &vp);
	x = (int)floorf(vp.x0 + 0.5f);
	y = (int)floorf(vp.y0 + 0.5f);
	w = (int)floorf(vp.x1 + 0.5f) - x;
	h = (int)floorf(vp.y1 + 0.5f) - y;
	if (w <= 0 || h <= 0)
		return;
	if (cam->letterbox > 0.0f) {
		int bar = (int)(cam->letterbox * (float)h + 0.5f);
		if (bar > 0) {
			Color black = camera_color(0, 128.0f);
			draw_sprite((float)x, (float)y, w, bar, black);
			draw_sprite((float)x, (float)(y + h - bar), w, bar, black);
		}
	}
	if (cam->fade_alpha >= 0.5f)
		draw_sprite((float)x, (float)y, w, h,
			camera_color(cam->fade_rgb, cam->fade_alpha));
	if (cam->flash_alpha >= 0.5f)
		draw_sprite((float)x, (float)y, w, h,
			camera_color(cam->flash_rgb, cam->flash_alpha));
}

/* Debug colors (PS2 alpha: 128 is opaque). */
#define DEBUG_DEADZONE  0x80FFFF00u   /* cyan */
#define DEBUG_ANCHOR    0x80FFFFFFu   /* white */
#define DEBUG_TARGET    0x8000FF00u   /* green */
#define DEBUG_GOAL      0x80FF8000u   /* blue */
#define DEBUG_LOOK      0x800080FFu   /* orange */
#define DEBUG_BOUNDS    0x800000FFu   /* red */
#define DEBUG_ZONE      0x60FF00FFu   /* magenta */
#define DEBUG_ZONE_ON   0x80FF00FFu

/* A world rectangle through the camera, as four screen lines. */
static void camera_debug_rect(const AthenaAffine2D *m, float x0, float y0,
	float x1, float y1, Color color)
{
	float xs[4], ys[4];

	athena_affine_apply(m, x0, y0, &xs[0], &ys[0]);
	athena_affine_apply(m, x1, y0, &xs[1], &ys[1]);
	athena_affine_apply(m, x1, y1, &xs[2], &ys[2]);
	athena_affine_apply(m, x0, y1, &xs[3], &ys[3]);
	for (int i = 0; i < 4; i++)
		draw_line(xs[i], ys[i], xs[(i + 1) % 4], ys[(i + 1) % 4], color);
}

/* What drives the camera, in screen space (identity view, viewport clip). */
static void camera_draw_debug(const AthenaCamera2D *cam)
{
	AthenaAffine2D m;
	AthenaRect2D vp;
	float ax, ay, tx, ty, gx, gy, lx, ly;
	int i;

	athena_camera2d_viewport(cam, &vp);
	athena_camera2d_matrix(cam, 1.0f, 1.0f, &m);
	ax = vp.x0 + cam->anchor_x * (vp.x1 - vp.x0);
	ay = vp.y0 + cam->anchor_y * (vp.y1 - vp.y0);

	for (i = 0; i < cam->zone_count; i++) {
		const AthenaCamera2DZone *z = &cam->zones[i];
		camera_debug_rect(&m, z->x, z->y, z->x + z->w, z->y + z->h,
			i == cam->zone ? DEBUG_ZONE_ON : DEBUG_ZONE);
	}
	if (cam->has_bounds)
		camera_debug_rect(&m, cam->bounds.x0, cam->bounds.y0,
			cam->bounds.x1, cam->bounds.y1, DEBUG_BOUNDS);

	/* The dead zone is a box of the screen, around the anchor. */
	if (cam->deadzone_w > 0.0f || cam->deadzone_h > 0.0f) {
		float hw = cam->deadzone_w * 0.5f, hh = cam->deadzone_h * 0.5f;
		draw_line(ax - hw, ay - hh, ax + hw, ay - hh, DEBUG_DEADZONE);
		draw_line(ax + hw, ay - hh, ax + hw, ay + hh, DEBUG_DEADZONE);
		draw_line(ax + hw, ay + hh, ax - hw, ay + hh, DEBUG_DEADZONE);
		draw_line(ax - hw, ay + hh, ax - hw, ay - hh, DEBUG_DEADZONE);
	}
	draw_line(ax - 6.0f, ay, ax + 6.0f, ay, DEBUG_ANCHOR);
	draw_line(ax, ay - 6.0f, ax, ay + 6.0f, DEBUG_ANCHOR);

	if (cam->following && cam->has_target) {
		athena_affine_apply(&m, cam->target_x, cam->target_y, &tx, &ty);
		athena_affine_apply(&m, cam->goal_x, cam->goal_y, &gx, &gy);
		/* The lookahead point: the target plus the lookahead (screen pixels). */
		athena_affine_apply(&m, cam->target_x + cam->look_x / cam->zoom_x,
			cam->target_y + cam->look_y / cam->zoom_y, &lx, &ly);
		draw_line(tx, ty, lx, ly, DEBUG_LOOK);
		draw_circle(tx, ty, 5.0f, DEBUG_TARGET, 0);
		draw_circle(gx, gy, 3.0f, DEBUG_GOAL, 1);
		/* Several targets: the box auto zoom fits. */
		if (cam->target_w > 0.0f || cam->target_h > 0.0f)
			camera_debug_rect(&m, cam->target_x - cam->target_w * 0.5f,
				cam->target_y - cam->target_h * 0.5f,
				cam->target_x + cam->target_w * 0.5f,
				cam->target_y + cam->target_h * 0.5f, DEBUG_TARGET);
	}
}

void athena_camera2d_end(const AthenaCamera2D *cam)
{
	if (camera_depth == 0)
		return;
	if (cam) {
		/* Effects cover the viewport: no world transform, same clip. */
		athena_view_set(NULL);
		camera_draw_effects(cam);
		if (cam->debug_draw)
			camera_draw_debug(cam);
	}
	camera_pop();
}

bool athena_camera2d_begin_screen(float x, float y, float w, float h)
{
	if (!camera_push())
		return false;
	if (w <= 0.0f || h <= 0.0f)
		athena_view_set_clip(0, 0, 0, 0);
	else
		camera_clip_rect(x, y, x + w, y + h);
	athena_view_set(NULL);
	return true;
}

void athena_camera2d_end_screen(void)
{
	camera_pop();
}

int athena_camera2d_depth(void)
{
	return camera_depth;
}

void athena_camera2d_unwind(void)
{
	while (camera_depth > 0)
		camera_pop();
}
