#include <stdlib.h>
#include <string.h>

#include <graphics.h>
#include <dbgprintf.h>

#include "image.h"

static GSSURFACE *image_alloc_surface(bool delayed)
{
	GSSURFACE *surface = malloc(sizeof(*surface));
	if (!surface)
		return NULL;

	if (graphics_surface_init(surface) < 0) {
		free(surface);
		return NULL;
	}

	surface->Delayed = delayed;
	return surface;
}

static void image_release_surface(GSSURFACE *surface)
{
	if (!surface)
		return;

	graphics_surface_release(surface);
	free(surface->Mem);
	free(surface->Clut);
	free(surface);
}

AthenaImage *athena_image_create_empty(bool delayed)
{
	AthenaImage *image = calloc(1, sizeof(*image));
	if (!image)
		return NULL;

	image->surface = image_alloc_surface(delayed);
	if (!image->surface) {
		free(image);
		return NULL;
	}

	image->owns_surface = true;
	image->delayed = delayed;
	image->color = 0x80808080;
	return image;
}

AthenaImage *athena_image_create(const char *path, bool delayed)
{
	AthenaImage *image = athena_image_create_empty(delayed);
	if (!image)
		return NULL;

	if (path) {
		image->path = strdup(path);
		dbgprintf("[Image] calling load_image\n");
		if (!image->path || load_image(image->surface, path, delayed) < 0) {
			athena_image_destroy(image);
			return NULL;
		}

		image->loaded = true;
		image->width = (float)image->surface->Width;
		image->height = (float)image->surface->Height;
		image->endx = image->width;
		image->endy = image->height;
		dbgprintf("[Image] load_image completed (%ux%u)\n",
			image->surface->Width, image->surface->Height);
	}

	return image;
}

AthenaImage *athena_image_wrap(GSSURFACE *surface, bool delayed)
{
	AthenaImage *image;

	if (!surface)
		return NULL;

	image = calloc(1, sizeof(*image));
	if (!image)
		return NULL;

	image->surface = surface;
	image->owns_surface = false;
	image->delayed = delayed;
	image->loaded = true;
	image->width = (float)surface->Width;
	image->height = (float)surface->Height;
	image->endx = image->width;
	image->endy = image->height;
	image->color = 0x80808080;
	return image;
}

void athena_image_destroy(AthenaImage *image)
{
	if (!image)
		return;

	if (image->owns_surface)
		image_release_surface(image->surface);
	free((void *)image->path);
	free(image);
}

bool athena_image_is_loaded(const AthenaImage *image)
{
	return image && image->loaded && image->surface;
}

void athena_image_draw(AthenaImage *image, float x, float y, float width,
	float height, float startx, float starty, float endx, float endy,
	float angle, uint32_t color)
{
	if (!athena_image_is_loaded(image))
		return;

	if (angle != 0.0f) {
		draw_image_rotate(image->surface, x, y, width, height,
			startx, starty, endx, endy, angle, color);
	} else {
		draw_image(image->surface, x, y, width, height,
			startx, starty, endx, endy, color);
	}
	dbgprintf("[Image] draw command queued\n");
}

bool athena_image_lock(AthenaImage *image)
{
	int result;

	if (!athena_image_is_loaded(image))
		return false;

	/*
	 * Locking must leave the texture resident. An asynchronous bind only
	 * queues an upload; its VIF marker is normally emitted by draw_image().
	 * Since a locked texture is already treated as resident by subsequent
	 * draws, that marker would never be emitted and the upload could remain
	 * pending forever.
	 */
	result = graphics_surface_bind_sync(image->surface);
	dbgprintf("[Image] synchronous bind result=%d vram=0x%08x psm=%u tbw=%u\n",
		result, image->surface->Vram, image->surface->PSM, image->surface->TBW);
	if (result == GRAPHICS_BIND_ERROR)
		return false;

	result = graphics_surface_lock(image->surface);
	dbgprintf("[Image] lock result=%d\n", result);
	return result != 0;
}

bool athena_image_unlock(AthenaImage *image)
{
	if (!athena_image_is_loaded(image))
		return false;
	return graphics_surface_unlock(image->surface) != 0;
}

bool athena_image_locked(const AthenaImage *image)
{
	return athena_image_is_loaded(image) &&
		graphics_surface_is_locked(image->surface);
}

bool athena_image_optimize(AthenaImage *image)
{
	if (!athena_image_is_loaded(image) ||
		image->surface->PSM != GS_PSM_CT24)
		return false;

	athena_texture_optimize(image->surface);
	return true;
}

bool athena_image_copy_vram_block(AthenaImage *source, int source_x,
	int source_y, AthenaImage *destination, int destination_x, int destination_y)
{
	if (!athena_image_is_loaded(source) || !athena_image_is_loaded(destination))
		return false;

	return graphics_surface_copy_block(source->surface, source_x, source_y,
		destination->surface, destination_x, destination_y) == 0;
}

void athena_image_set_dimensions(AthenaImage *image, float width, float height)
{
	if (!image)
		return;
	image->width = width;
	image->height = height;
}

void athena_image_set_src_rect(AthenaImage *image, float startx, float starty,
	float endx, float endy)
{
	if (!image)
		return;
	image->startx = startx;
	image->starty = starty;
	image->endx = endx;
	image->endy = endy;
}

void athena_image_set_angle(AthenaImage *image, float angle)
{
	if (image)
		image->angle = angle;
}

void athena_image_set_color(AthenaImage *image, Color color)
{
	if (image)
		image->color = color;
}

void athena_image_set_filter(AthenaImage *image, uint32_t filter)
{
	if (athena_image_is_loaded(image))
		image->surface->Filter = filter;
}
