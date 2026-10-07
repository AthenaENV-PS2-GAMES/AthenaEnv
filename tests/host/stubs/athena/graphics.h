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

/* Image load errors, for athena/image.h (Sprite tests). */
typedef enum {
	ATHENA_IMAGE_LOAD_OK = 0,
	ATHENA_IMAGE_LOAD_OPEN = 1,
	ATHENA_IMAGE_LOAD_FORMAT = 2,
	ATHENA_IMAGE_LOAD_DECODE = 3,
	ATHENA_IMAGE_LOAD_SURFACE = 4,
	ATHENA_IMAGE_LOAD_UPLOAD = 5
} AthenaImageLoadError;

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

/* Primitives and the GS context, for the Debug binding in the JS runner. */
typedef struct GSCONTEXT GSCONTEXT;
GSCONTEXT *getGSGLOBAL(void);
void draw_sprite(float x, float y, int width, int height, Color color);
void draw_rect_f(float x, float y, float width, float height, Color color);
void draw_line(float x, float y, float x2, float y2, Color color);
void draw_circle(float x, float y, float radius, Color color, uint8_t filled);

/* Bitmap fonts and the graphics service, for the Font binding in the JS runner. */
typedef struct gsFont GSFONT;
GSFONT *loadFont(const char *path);
void graphics_service_init(void);

typedef struct {
	float x;
	float y;
	float x2;
	float y2;
	Color rgba;
} prim_line;
void draw_line_list(float x, float y, prim_line *list, int list_size);

/* Textured sprites of one texture, for Image.drawList and Sprite batches. */
typedef struct {
	float x;
	float y;
	float u1;
	float v1;

	float w;
	float h;
	float u2;
	float v2;

	Color rgba;
} prim_tex_sprite;

typedef struct {
	float x, y, u, v;
	Color rgba;
	float x2, y2, u2, v2;
	Color rgba2;
	float x3, y3, u3, v3;
	Color rgba3;
} prim_tex_gouraud_triangle;
