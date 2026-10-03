#include <stdlib.h>
#include <string.h>

#include <athena/font.h>

#include "fntsys.h"

Coords athena_font_calc_dimensions(GSFONT *gs_font, float scale, const char *str);

static AthenaFont *font_new(void) {
    AthenaFont *font = calloc(1, sizeof(AthenaFont));
    if (!font)
        return NULL;
    font->color = 0x80808080;
    font->scale = 1.0f;
    font->align = ALIGN_LEFT;
    font->id = -1;
    return font;
}

static int font_is_bitmap(const char *path) {
    const char *extension = path ? strrchr(path, '.') : NULL;
    return extension &&
        (strcasecmp(extension, ".bmp") == 0 ||
         strcasecmp(extension, ".png") == 0 ||
         strcasecmp(extension, ".jpg") == 0 ||
         strcasecmp(extension, ".jpeg") == 0);
}

static void font_set_error(int *error, int code) {
    if (error)
        *error = code;
}

AthenaFont *athena_font_load_ex(const char *path, int size, int *error) {
    AthenaFont *font = font_new();

    font_set_error(error, ATHENA_FONT_OK);
    if (!font) {
        font_set_error(error, ATHENA_FONT_ERR_MEMORY);
        return NULL;
    }
    if (path && strcmp(path, "default") == 0)
        path = NULL;

    font->id = font_is_bitmap(path) ? FNT_ERROR : fntLoadFile(path, size);
    if (font->id >= 0) {
        font->type = ATHENA_FONT_TYPE_TRUETYPE;
        font->size = fntGetSize(font->id);
        return font;
    }

    /* Bitmap fonts, and files FreeType does not recognize. */
    if (font->id == FNT_ERROR && path) {
        font->data = loadFont(path);
        if (font->data) {
            font->type = ATHENA_FONT_TYPE_IMAGE;
            font->id = -1;
            return font;
        }
    }

    font_set_error(error, font->id == FNT_ERROR_SLOTS ? ATHENA_FONT_ERR_SLOTS :
        font->id == FNT_ERROR_MEMORY ? ATHENA_FONT_ERR_MEMORY : ATHENA_FONT_ERR_LOAD);
    free(font);
    return NULL;
}

AthenaFont *athena_font_load(const char *path) {
    return athena_font_load_ex(path, 0, NULL);
}

AthenaFont *athena_font_from_memory(const char *path, void *data, int data_size,
    int size, int *error) {
    AthenaFont *font = font_new();
    int id;

    font_set_error(error, ATHENA_FONT_OK);
    if (!font) {
        font_set_error(error, ATHENA_FONT_ERR_MEMORY);
        return NULL;
    }
    id = fntLoadMemory(path, data, data_size, size);
    if (id < 0) {
        font_set_error(error, id == FNT_ERROR_SLOTS ? ATHENA_FONT_ERR_SLOTS :
            id == FNT_ERROR_MEMORY ? ATHENA_FONT_ERR_MEMORY : ATHENA_FONT_ERR_LOAD);
        free(font);
        return NULL;
    }
    font->id = id;
    font->type = ATHENA_FONT_TYPE_TRUETYPE;
    font->size = fntGetSize(id);
    return font;
}

AthenaFont *athena_font_from_bitmap(GSFONT *data) {
    AthenaFont *font;

    if (!data)
        return NULL;
    font = font_new();
    if (!font)
        return NULL;
    font->type = ATHENA_FONT_TYPE_IMAGE;
    font->data = data;
    return font;
}

int athena_font_preload(AthenaFont *font, const char *text, int *offset, float budget_ms) {
    if (!font || font->type != ATHENA_FONT_TYPE_TRUETYPE)
        return 1;
    return fntPreload(font->id, text, offset, budget_ms);
}

int athena_font_get_line_height(AthenaFont *font) {
    if (!font)
        return 0;
    if (font->type == ATHENA_FONT_TYPE_TRUETYPE)
        return fntGetLineHeight(font->id, font->scale);
    return athena_font_calc_dimensions(font->data, font->scale, "A").height;
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
    render->text = text ? strdup(text) : NULL;
    if (text && !render->text) {
        free(render);
        return NULL;
    }
    render->size = athena_font_get_text_size(font, text);
    return render;
}

void athena_font_render_destroy(AthenaFontRender *render) {
    if (!render)
        return;
    fntLayoutFree(render->layout);
    free(render->text);
    free(render);
}

void athena_font_render_print(AthenaFontRender *render, float x, float y) {
    AthenaFont *font;

    if (!render || !render->font || !render->text)
        return;
    font = render->font;
    if (font->type != ATHENA_FONT_TYPE_TRUETYPE) {
        athena_font_print(font, x, y, render->text);
        return;
    }
    if (!render->layout)
        render->layout = fntLayoutNew();
    if (!render->layout) {
        athena_font_print(font, x, y, render->text);
        return;
    }
    if (!fntLayoutMatches(render->layout, font->id, (short)font->align, 0, 0, font->scale) &&
        !fntLayout(render->layout, font->id, (short)font->align, 0, 0, render->text, font->scale))
        return;
    fntLayoutDraw(render->layout, (int)x, (int)y, font->color,
                  font->outline, font->outline_color,
                  font->dropshadow, font->dropshadow_color);
}

Coords athena_font_render_get_size(const AthenaFontRender *render) {
    if (!render)
        return (Coords){0, 0};
    return render->size;
}
