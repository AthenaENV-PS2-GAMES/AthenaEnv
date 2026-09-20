#ifndef ATHENA_FONT_H
#define ATHENA_FONT_H

#include <stdbool.h>
#include <stdint.h>

#include <graphics.h>
#include <fntsys.h>

#define ATHENA_FONT_TYPE_IMAGE     0
#define ATHENA_FONT_TYPE_TRUETYPE  1

typedef struct AthenaFont {
    uint32_t type;
    GSFONT *data;
    int id;
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
    const char *text;
    Coords size;
} AthenaFontRender;

AthenaFont *athena_font_load(const char *path);
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
