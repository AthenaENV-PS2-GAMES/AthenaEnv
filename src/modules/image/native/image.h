#ifndef ATHENA_IMAGE_NATIVE_H
#define ATHENA_IMAGE_NATIVE_H

#include <stdbool.h>
#include <stdint.h>

#include <graphics.h>

typedef struct AthenaImage {
	char *path;
	GSSURFACE *surface;
	bool delayed;
	bool loaded;
	bool owns_surface;
	Color color;
	float width;
	float height;
	float startx;
	float starty;
	float endx;
	float endy;
	float angle;
} AthenaImage;

AthenaImage *athena_image_create(const char *path, bool delayed);
AthenaImage *athena_image_create_empty(bool delayed);
AthenaImage *athena_image_wrap(GSSURFACE *surface, bool delayed);
void athena_image_destroy(AthenaImage *image);
bool athena_image_is_loaded(const AthenaImage *image);
void athena_image_draw(AthenaImage *image, float x, float y, float width,
	float height, float startx, float starty, float endx, float endy,
	float angle, uint32_t color);
bool athena_image_lock(AthenaImage *image);
bool athena_image_unlock(AthenaImage *image);
bool athena_image_locked(const AthenaImage *image);
bool athena_image_optimize(AthenaImage *image);
bool athena_image_copy_vram_block(AthenaImage *source, int source_x,
	int source_y, AthenaImage *destination, int destination_x, int destination_y);

#endif
