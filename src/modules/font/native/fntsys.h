#ifndef __FNTSYS_H
#define __FNTSYS_H

#include <tamtypes.h>
#include <gsKit.h>

#include <athena/font.h> /* Coords */

/// Maximal count of atlases per font
#define ATLAS_MAX    8

/// Default rasterization size, in pixels
#define FNTSYS_CHAR_SIZE 26
#define FNTSYS_MIN_SIZE 6
#define FNTSYS_MAX_SIZE 128

#define ALIGN_TOP     (0 << 0)
#define ALIGN_BOTTOM  (1 << 0)
#define ALIGN_VCENTER (2 << 0)
#define ALIGN_LEFT    (0 << 2)
#define ALIGN_RIGHT   (1 << 2)
#define ALIGN_HCENTER (2 << 2)
#define ALIGN_NONE    (ALIGN_TOP | ALIGN_LEFT)
#define ALIGN_CENTER  (ALIGN_VCENTER | ALIGN_HCENTER)

/// Errors returned by fntLoadFile() and fntLoadMemory()
#define FNT_ERROR       (-1)    /* unreadable file or not a font */
#define FNT_ERROR_SLOTS (-2)    /* FNT_MAX_COUNT distinct fonts are loaded */
#define FNT_ERROR_MEMORY (-3)

/// Distinct fonts (file and size) loaded at once; equal ones are shared
#define FNT_MAX_COUNT (16)

/** Initializes the font subsystem */
void fntInit();

/** Terminates the font subsystem */
void fntEnd();

/**
 * Loads a font file (NULL: the embedded Quicksand) rasterized at `size`
 * pixels. A font already loaded with the same path and size is shared.
 * Returns the font id, or FNT_ERROR, FNT_ERROR_SLOTS or FNT_ERROR_MEMORY.
 */
int fntLoadFile(const char *path, int size);

/**
 * Same as fntLoadFile() for a font file already in memory. On success the
 * font owns `data` (freed with free()); on failure the caller keeps it.
 */
int fntLoadMemory(const char *path, void *data, int data_size, int size);

/** Whole file in a malloc()ed buffer, or NULL. Thread-safe: no font state is touched. */
void *fntReadFile(const char *path, int *size);

/** Releases a reference to a font; the font is freed with its last one. */
void fntRelease(int id);

/** Rasterization size of a font, in pixels. */
int fntGetSize(int id);

/**
 * Re-reads the video mode (resolution, 4:3 or 16:9, interlaced frame mode)
 * for the glyph aspect ratio. Checked automatically before drawing; a
 * change invalidates every glyph cache.
 */
void fntUpdateAspectRatio();

/**
 * A text laid out once: its glyphs as quads grouped by atlas, to draw again
 * (with outline or shadow) without measuring and decoding the text again.
 */
typedef struct fnt_layout fnt_layout_t;

fnt_layout_t *fntLayoutNew(void);
void fntLayoutFree(fnt_layout_t *layout);

/** Lays out `string` relative to the origin, as fntRenderString() would place it. 0 on failure. */
int fntLayout(fnt_layout_t *layout, int id, short aligned, size_t width, size_t height,
    const char *string, float scale);

/**
 * Whether the layout still holds for the same text with these settings: a
 * new video mode or a flushed glyph cache moves the glyphs.
 */
int fntLayoutMatches(const fnt_layout_t *layout, int id, short aligned, size_t width,
    size_t height, float scale);

/** Draws a layout at (x, y): one packet per atlas holding the outline or shadow copies and the text. */
void fntLayoutDraw(const fnt_layout_t *layout, int x, int y, u64 colour, float outline,
    u64 outline_colour, float dropshadow, u64 dropshadow_colour);

/**
 * Rasterizes the glyphs of `text` into the cache, from byte `*offset`, for
 * up to `budget_ms` (no limit when <= 0). Returns 1 once the end is reached,
 * or 0 with `*offset` at the next character to continue from.
 */
int fntPreload(int id, const char *text, int *offset, float budget_ms);

int fntRenderStringPlus(int id, int x, int y, short aligned, size_t width, size_t height, const char *string, float scale, u64 colour, float outline, u64 outline_colour, float dropshadow, u64 dropshadow_colour);

/** Renders a text with specified window dimensions; `\n` starts a new line. */
int fntRenderString(int id, int x, int y, short aligned, size_t width, size_t height, const char *string, float scale, u64 colour);

/** Width of the widest line of the given text, in pixels. */
int fntCalcDimensions(int id, float scale, const char *str);

/** Distance between two lines of text, in pixels. */
int fntGetLineHeight(int id, float scale);

/** Width of the widest line and height of all lines. */
Coords fntGetTextSize(int id, const char* text, float scale);

/*
 * loadFont() (athena/graphics.h) decodes a bitmap font into CPU memory only;
 * its texture is uploaded when first drawn, so it is safe on a worker thread.
 * athena_bitmap_font_discard() frees one that was never drawn, without the
 * texture manager: safe on any thread.
 */
void athena_bitmap_font_discard(GSFONT *font);

#endif
