#include <stdlib.h>
#include <string.h>

#include <athena/font.h>
#include <dbgprintf.h>

static bool athena_truetype_font_loaded = false;

Coords athena_font_calc_dimensions(GSFONT *gs_font, float scale, const char *str);

AthenaFont *athena_font_load(const char *path) {
    AthenaFont *font = calloc(1, sizeof(AthenaFont));
    if (!font)
        return NULL;

    font->color = 0x80808080;
    font->scale = 1.0f;
    font->align = ALIGN_LEFT;

    if (!path)
        return font;

    if (strcmp(path, "default") == 0) {
        font->type = ATHENA_FONT_TYPE_TRUETYPE;
        if (!athena_truetype_font_loaded) {
            font->id = fntLoadFile(NULL);
            fntSetCharSize(font->id, FNTSYS_CHAR_SIZE * 64, FNTSYS_CHAR_SIZE * 64);
            athena_truetype_font_loaded = true;
        }
        return font;
    }

    dbgprintf("%s\n", path);
    font->id = fntLoadFile(path);
    font->type = ATHENA_FONT_TYPE_TRUETYPE;

    if (font->id == -1) {
        font->data = loadFont(path);
        if (!font->data) {
            free(font);
            return NULL;
        }
        font->type = ATHENA_FONT_TYPE_IMAGE;
    } else {
        fntSetCharSize(font->id, FNTSYS_CHAR_SIZE * 64, FNTSYS_CHAR_SIZE * 64);
    }

    return font;
}

void athena_font_destroy(AthenaFont *font) {
    if (!font)
        return;

    if (font->type == ATHENA_FONT_TYPE_IMAGE)
        unloadFont(font->data);
    else if (font->id >= 0)
        fntRelease(font->id);

    free(font);
}

void athena_font_print(AthenaFont *font, float x, float y, const char *text) {
    if (!font || !text)
        return;

    if (font->type == ATHENA_FONT_TYPE_IMAGE) {
        printFontTextPlus(font->data, text, x, y, font->scale, font->color,
                          font->align, 0, 0,
                          font->outline, font->outline_color,
                          font->dropshadow, font->dropshadow_color);
    } else {
        fntRenderStringPlus(font->id, (int)x, (int)y, (short)font->align, 0, 0,
                            text, font->scale, font->color,
                            font->outline, font->outline_color,
                            font->dropshadow, font->dropshadow_color);
    }
}

Coords athena_font_get_text_size(AthenaFont *font, const char *text) {
    Coords size = {0, 0};
    if (!font || !text)
        return size;

    if (font->type == ATHENA_FONT_TYPE_TRUETYPE)
        return fntGetTextSize(font->id, text, font->scale);

    return athena_font_calc_dimensions(font->data, font->scale, text);
}

void athena_font_set_scale(AthenaFont *font, float scale) {
    if (font) font->scale = scale;
}

void athena_font_set_color(AthenaFont *font, Color color) {
    if (font) font->color = color;
}

void athena_font_set_align(AthenaFont *font, int align) {
    if (font) font->align = align;
}

void athena_font_set_outline(AthenaFont *font, float outline, Color color) {
    if (!font) return;
    font->outline = outline;
    font->outline_color = color;
}

void athena_font_set_dropshadow(AthenaFont *font, float dropshadow, Color color) {
    if (!font) return;
    font->dropshadow = dropshadow;
    font->dropshadow_color = color;
}

float athena_font_get_scale(const AthenaFont *font) {
    return font ? font->scale : 0.0f;
}

Color athena_font_get_color(const AthenaFont *font) {
    return font ? font->color : 0;
}

int athena_font_get_align(const AthenaFont *font) {
    return font ? font->align : ALIGN_LEFT;
}

AthenaFontRender *athena_font_render_create(AthenaFont *font, const char *text) {
    AthenaFontRender *render = calloc(1, sizeof(AthenaFontRender));
    if (!render)
        return NULL;

    render->font = font;
    render->text = text;
    render->size = athena_font_get_text_size(font, text);
    return render;
}

void athena_font_render_destroy(AthenaFontRender *render) {
    free(render);
}

void athena_font_render_print(AthenaFontRender *render, float x, float y) {
    if (!render || !render->font || !render->text)
        return;
    athena_font_print(render->font, x, y, render->text);
}

Coords athena_font_render_get_size(const AthenaFontRender *render) {
    if (!render)
        return (Coords){0, 0};
    return render->size;
}
