#ifndef ATHENA_FONT_H
#define ATHENA_FONT_H

#include <stdbool.h>
#include <stdint.h>

#include <athena/graphics.h>

#define ATHENA_FONT_TYPE_IMAGE     0
#define ATHENA_FONT_TYPE_TRUETYPE  1

typedef struct {
    int width;
    int height;
} Coords;

/* Errors of athena_font_load_ex() and athena_font_from_memory(). */
#define ATHENA_FONT_OK          0
#define ATHENA_FONT_ERR_LOAD   (-1)  /* unreadable file or not a font */
#define ATHENA_FONT_ERR_SLOTS  (-2)  /* 16 distinct TrueType fonts are loaded */
#define ATHENA_FONT_ERR_MEMORY (-3)

typedef struct AthenaFont {
    uint32_t type;
    GSFONT *data;
    int id;
    int size;               /* TrueType rasterization size in pixels */
    Color color;
    float scale;
    int align;
    float outline;
    Color outline_color;
    float dropshadow;
    Color dropshadow_color;
} AthenaFont;

typedef struct AthenaFontRender {
    AthenaFont *font;
    char *text;
    Coords size;
    /* TrueType: the glyphs placed once, laid out again only when the font's scale, alignment or glyph cache changes */
    struct fnt_layout *layout;
} AthenaFontRender;

AthenaFont *athena_font_load(const char *path);
/*
 * Loads a TrueType font rasterized at `size` pixels (0: 26), or a bitmap
 * font (.png, .bmp, .jpg with an optional .dat); NULL or "default" is the
 * embedded font. The same file at the same size is loaded once and shared.
 * On failure returns NULL and stores an ATHENA_FONT_ERR_* in `error`.
 */
AthenaFont *athena_font_load_ex(const char *path, int size, int *error);
/*
 * Same, from a TrueType file already read into `data` (malloc'ed). The
 * font takes `data` on success; on failure the caller keeps it.
 */
AthenaFont *athena_font_from_memory(const char *path, void *data, int data_size,
    int size, int *error);
/* Wraps a bitmap font decoded by loadFont() (on any thread); takes it. NULL when out of memory. */
AthenaFont *athena_font_from_bitmap(GSFONT *data);
/*
 * Rasterizes the glyphs of `text` into the cache from byte `*offset`, for up
 * to `budget_ms` (no limit when <= 0). 1 once done, 0 when some remain;
 * bitmap fonts have nothing to rasterize.
 */
int athena_font_preload(AthenaFont *font, const char *text, int *offset, float budget_ms);
/* Distance between two lines of text at the current scale, in pixels. */
int athena_font_get_line_height(AthenaFont *font);
void athena_font_destroy(AthenaFont *font);
void athena_font_print(AthenaFont *font, float x, float y, const char *text);
Coords athena_font_get_text_size(AthenaFont *font, const char *text);

void athena_font_set_scale(AthenaFont *font, float scale);
void athena_font_set_color(AthenaFont *font, Color color);
void athena_font_set_align(AthenaFont *font, int align);
void athena_font_set_outline(AthenaFont *font, float outline, Color color);
void athena_font_set_dropshadow(AthenaFont *font, float dropshadow, Color color);

float athena_font_get_scale(const AthenaFont *font);
Color athena_font_get_color(const AthenaFont *font);
int athena_font_get_align(const AthenaFont *font);

AthenaFontRender *athena_font_render_create(AthenaFont *font, const char *text);
void athena_font_render_destroy(AthenaFontRender *render);
void athena_font_render_print(AthenaFontRender *render, float x, float y);
Coords athena_font_render_get_size(const AthenaFontRender *render);

Coords athena_font_calc_dimensions(GSFONT *gs_font, float scale, const char *str);

#endif /* ATHENA_FONT_H */
