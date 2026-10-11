#ifndef ATHENA_SPRITEBATCH_H
#define ATHENA_SPRITEBATCH_H

/*
 * Native 2D sprite queue: sprites of one image gather in a buffer that goes
 * to the GS in one packet, either right away (the queue) or recorded into a
 * Layer (a capture) that draws the same list again each frame. On top of it
 * sit an atlas (named sprites, flips, tints, nine-patch boxes) and a bitmap
 * font (cached layouts), so a scene's sprites cost a few microseconds each
 * instead of the ~100 us the same work takes in JavaScript on the EE.
 *
 * No QuickJS in here: the adapter (quickjs/ath_spritebatch.c) keeps the
 * images alive and validates arguments.
 */

#include <stdbool.h>
#include <stdint.h>

#include <athena/image.h>

/* Packs channels (128 is neutral on the GS, up to 255 brightens) into a sprite color. */
static inline uint32_t athena_sb_color(int r, int g, int b, int a)
{
	if (r < 0) r = 0; else if (r > 255) r = 255;
	if (g < 0) g = 0; else if (g > 255) g = 255;
	if (b < 0) b = 0; else if (b > 255) b = 255;
	if (a < 0) a = 0; else if (a > 255) a = 255;
	return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}

/* Sprites and batches (one GS packet each) sent since the last call; reading resets them. */
void athena_sb_stats(uint32_t *sprites, uint32_t *batches);

/* ---- queue and recorder ---------------------------------------------- */

/* Queues a sprite (or records it during a capture). x, y, w, h in pixels, u/v in texels. */
void athena_sb_sprite(AthenaImage *image, float x, float y, float w, float h,
	float u1, float v1, float u2, float v2, uint32_t color);
/* Sends what is queued. Draws that must come after something drawn directly call it first. */
void athena_sb_flush(void);
/* A flat rectangle / gouraud quad, in order with the sprites. */
void athena_sb_rect(float x, float y, float w, float h, uint32_t color);
void athena_sb_quad(const float xy[8], const uint32_t color[4]);

typedef struct AthenaSbLayer AthenaSbLayer;

/* Starts recording; false if already recording. Everything drawn until _end goes to the layer. */
bool athena_sb_capture_begin(void);
bool athena_sb_capturing(void);
/* Stops recording; NULL only when memory ran out (an empty capture is an empty layer). */
AthenaSbLayer *athena_sb_capture_end(void);
void athena_sb_capture_abort(void);
void athena_sb_layer_destroy(AthenaSbLayer *layer);

/* The next sprite recorded gets this mark (> 0), so athena_sb_layer_tint can find it. 0 clears. */
void athena_sb_mark(int id);

/* Sprites of a layer, by step: image of each, count, and the first sprite. */
uint32_t athena_sb_layer_steps(const AthenaSbLayer *layer);
AthenaImage *athena_sb_layer_step_image(const AthenaSbLayer *layer, uint32_t step);
uint32_t athena_sb_layer_step_count(const AthenaSbLayer *layer, uint32_t step);
/* Writes sprite `index` of `step` in the 16-float record of Image.drawList (x y w h u1 v1 u2 v2, r g b a as uint32). */
void athena_sb_layer_step_record(const AthenaSbLayer *layer, uint32_t step, uint32_t index, float out[16]);

/* valid(user, image) says whether an image of the layer is still alive. */
typedef bool (*AthenaSbValidFn)(void *user, const AthenaImage *image);
void athena_sb_layer_draw(const AthenaSbLayer *layer, AthenaSbValidFn valid, void *user);
/* Changes the tint (0..128) of the sprite marked `id`; false when there is none. */
bool athena_sb_layer_tint(AthenaSbLayer *layer, int id, int r, int g, int b);

/* ---- atlas ------------------------------------------------------------- */

typedef struct AthenaSbAtlas AthenaSbAtlas;

typedef struct {
	const char *name;
	float w, h;             /* trimmed size in texels */
	float u1, v1, u2, v2;
	float offset_x, offset_y;   /* where the trimmed part sits in the original */
	float source_w, source_h;   /* original size */
	float draw_scale;       /* screen pixels per texel */
	float native_scale;
} AthenaSbSpriteDef;

AthenaSbAtlas *athena_sb_atlas_create(const AthenaSbSpriteDef *sprites, int count,
	float ratio, float screen_w, float screen_h);
void athena_sb_atlas_destroy(AthenaSbAtlas *atlas);
/* Index of a sprite by name, or -1. */
int athena_sb_atlas_find(const AthenaSbAtlas *atlas, const char *name);
/* Size in native pixels (the original UI's): source size * draw scale / ratio. */
void athena_sb_atlas_size(const AthenaSbAtlas *atlas, int id, float *w, float *h);
/* rgb: three 0..255 channels, or NULL for white. mul scales around the sprite's center. */
void athena_sb_atlas_draw(const AthenaSbAtlas *atlas, AthenaImage *image, int id,
	float nx, float ny, float mul, float alpha, const int *rgb, bool flip);
/* Stretches the whole sprite (before trimming) over the native rectangle. */
void athena_sb_atlas_draw_rect(const AthenaSbAtlas *atlas, AthenaImage *image, int id,
	float nx, float ny, float nw, float nh, float alpha, const int *rgb);
/* 3x3 box from sprite `id` (untrimmed): corner in native pixels. */
void athena_sb_atlas_draw_nine(const AthenaSbAtlas *atlas, AthenaImage *image, int id,
	float nx, float ny, float nw, float nh, float corner, float alpha, const int *rgb);

/* ---- bitmap font --------------------------------------------------------- */

typedef struct AthenaSbFont AthenaSbFont;

typedef struct {
	uint32_t codepoint;
	float x, y, w, h;       /* rectangle in the texture */
	float crop_x, crop_y;   /* where it is drawn relative to the pen */
	float left, advance, right; /* bearings (SpriteFont's kerning triple) */
} AthenaSbGlyph;

AthenaSbFont *athena_sb_font_create(const AthenaSbGlyph *glyphs, int count,
	uint32_t default_codepoint, float spacing, float line_spacing, float ratio,
	float screen_w, float screen_h, const int color[3], const int shadow[3],
	float shadow_offset);
void athena_sb_font_destroy(AthenaSbFont *font);
/* Width of the widest line, native pixels. */
float athena_sb_font_measure(AthenaSbFont *font, const char *text);
float athena_sb_font_height(const AthenaSbFont *font, const char *text);
/* rgb: 0..255 or NULL for the default color. */
void athena_sb_font_draw(AthenaSbFont *font, AthenaImage *image, const char *text,
	float nx, float ny, const int *rgb, float alpha);
void athena_sb_font_draw_shadow(AthenaSbFont *font, AthenaImage *image, const char *text,
	float nx, float ny, const int *rgb, float alpha);
void athena_sb_font_draw_centered(AthenaSbFont *font, AthenaImage *image, const char *text,
	float cx, float ny, const int *rgb, float alpha, bool shadow);

#endif
