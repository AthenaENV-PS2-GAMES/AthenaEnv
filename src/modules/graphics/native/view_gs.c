#include <athena/graphics.h>
#include <athena/graphics/view.h>

/*
 * The clip rectangle of the 2D view is the GS scissor of the current
 * context. It lives in the register cache, so reading it costs nothing, and
 * setactive() restores the whole framebuffer at every flip.
 */

static void view_framebuffer(int *width, int *height)
{
	*width = gsGlobal ? gsGlobal->Width : 640;
	*height = gsGlobal ? gsGlobal->Height : 448;
}

void athena_view_screen_size(int *width, int *height)
{
	view_framebuffer(width, height);
}

void athena_view_set_clip(int x, int y, int width, int height)
{
	int fb_width, fb_height, x1, y1;

	if (!gsGlobal)
		return;
	view_framebuffer(&fb_width, &fb_height);
	if (width <= 0 || height <= 0) {
		x = 0;
		y = 0;
		width = fb_width;
		height = fb_height;
	}
	x1 = x + width - 1;
	y1 = y + height - 1;
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x1 > fb_width - 1) x1 = fb_width - 1;
	if (y1 > fb_height - 1) y1 = fb_height - 1;
	/* An empty rectangle keeps one pixel: the GS has no empty scissor. */
	if (x > x1) x = x1 < 0 ? 0 : x1;
	if (y > y1) y = y1 < 0 ? 0 : y1;
	if (x1 < x) x1 = x;
	if (y1 < y) y1 = y;
	set_screen_param(SCISSOR_BOUNDS, GS_SETREG_SCISSOR_1(x, x1, y, y1));
}

void athena_view_get_clip(int *x, int *y, int *width, int *height)
{
	scissor_reg scissor;

	if (!gsGlobal) {
		*x = 0;
		*y = 0;
		view_framebuffer(width, height);
		return;
	}
	scissor.data = get_screen_param(SCISSOR_BOUNDS);
	*x = (int)scissor.fields.x0;
	*y = (int)scissor.fields.y0;
	*width = (int)scissor.fields.x1 - (int)scissor.fields.x0 + 1;
	*height = (int)scissor.fields.y1 - (int)scissor.fields.y0 + 1;
}

bool athena_view_visible_bounds(AthenaRect2D *world)
{
	AthenaAffine2D inverse;
	AthenaRect2D screen;
	int x, y, width, height;

	athena_view_get_clip(&x, &y, &width, &height);
	screen.x0 = (float)x;
	screen.y0 = (float)y;
	screen.x1 = (float)(x + width);
	screen.y1 = (float)(y + height);
	if (athena_view_kind() == ATHENA_VIEW_IDENTITY) {
		*world = screen;
		return true;
	}
	if (!athena_affine_invert(&athena_view_matrix, &inverse))
		return false;
	athena_affine_bounds(&inverse, &screen, world);
	return true;
}

bool athena_view_culler_init(AthenaViewCuller *culler)
{
	int x, y, width, height;

	if (athena_view_kind() == ATHENA_VIEW_IDENTITY)
		return false;
	culler->m = athena_view_matrix;
	culler->rotated = athena_view_kind() == ATHENA_VIEW_ROTATED;
	athena_view_get_clip(&x, &y, &width, &height);
	culler->clip.x0 = (float)x;
	culler->clip.y0 = (float)y;
	culler->clip.x1 = (float)(x + width);
	culler->clip.y1 = (float)(y + height);
	return true;
}

bool athena_view_screen_box_visible(float x0, float y0, float x1, float y1)
{
	int x, y, width, height;

	athena_view_get_clip(&x, &y, &width, &height);
	return x1 >= (float)x && x0 <= (float)(x + width) &&
		y1 >= (float)y && y0 <= (float)(y + height);
}
