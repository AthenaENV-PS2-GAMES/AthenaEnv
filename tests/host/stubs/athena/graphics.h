/*
 * Host stub of the graphics module API video.c uses: the surface type and
 * the calls, implemented by the test so they can be observed.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef uint32_t Color;

struct gsSurface {
	uint32_t Width;
	uint32_t Height;
	uint8_t PSM;
	uint8_t ClutPSM;
	uint32_t TBW;
	uint32_t *Mem;
	uint32_t *Clut;
	uint32_t Vram;
	uint32_t VramClut;
	uint32_t Filter;
	uint8_t ClutStorageMode;
	uint8_t Delayed;
	uint8_t PageAligned;
	uint8_t Macroblock;
	uint32_t Mask;
};
typedef struct gsSurface GSSURFACE;

#define GS_PSM_CT32 0x00
#define GS_FILTER_NEAREST 0
#define GS_FILTER_LINEAR 1

int graphics_surface_init(GSSURFACE *surface);
void graphics_surface_release(GSSURFACE *surface);
void graphics_surface_invalidate(GSSURFACE *surface);
void athena_calculate_tbw(GSSURFACE *surface);
void draw_image(GSSURFACE *source, float x, float y, float width, float height,
	float startx, float starty, float endx, float endy, Color color);
void draw_image_rotate(GSSURFACE *source, float x, float y, float width,
	float height, float startx, float starty, float endx, float endy,
	float angle, Color color);
