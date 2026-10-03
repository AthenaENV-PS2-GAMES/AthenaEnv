/*
 * Host stand-in for the Font module's native side (FreeType rasterizer and
 * GS drawing), so the real binding (quickjs/ath_font.c) runs in the JS runner:
 * fonts are plain allocations, glyphs 8x16 pixels, and preload() rasterizes
 * a few characters per call, so it spans several setTimeout slices as on the
 * console. Nothing is drawn.
 */
#include <stdlib.h>
#include <string.h>

#include <athena/font.h>
#include "../../src/modules/font/native/fntsys.h"

#define PRELOAD_CHARS_PER_SLICE 10

void fntInit(void) {}
void graphics_service_init(void) {}
GSFONT *loadFont(const char *path) { return NULL; }
void *fntReadFile(const char *path, int *size) { *size = 0; return NULL; }
void athena_bitmap_font_discard(GSFONT *font) {}

static AthenaFont *font_new(int size) {
    AthenaFont *font = calloc(1, sizeof(*font));
    if (!font)
        return NULL;
    font->type = ATHENA_FONT_TYPE_TRUETYPE;
    font->size = size ? size : FNTSYS_CHAR_SIZE;
    font->color = 0x80FFFFFF;
    font->scale = 1.0f;
    return font;
}

AthenaFont *athena_font_load_ex(const char *path, int size, int *error) {
    AthenaFont *font = font_new(size);
    *error = font ? ATHENA_FONT_OK : ATHENA_FONT_ERR_MEMORY;
    return font;
}

AthenaFont *athena_font_from_memory(const char *path, void *data, int data_size,
    int size, int *error) {
    AthenaFont *font = font_new(size);
    if (font)
        free(data);
    *error = font ? ATHENA_FONT_OK : ATHENA_FONT_ERR_MEMORY;
    return font;
}

AthenaFont *athena_font_from_bitmap(GSFONT *data) { return font_new(0); }
void athena_font_destroy(AthenaFont *font) { free(font); }

int athena_font_preload(AthenaFont *font, const char *text, int *offset, float budget_ms) {
    int length = (int)strlen(text);
    *offset += PRELOAD_CHARS_PER_SLICE;
    if (budget_ms <= 0 || *offset >= length) {
        *offset = length;
        return 1;
    }
    return 0;
}

int athena_font_get_line_height(AthenaFont *font) { return (int)(16 * font->scale); }
void athena_font_print(AthenaFont *font, float x, float y, const char *text) {}

Coords athena_font_get_text_size(AthenaFont *font, const char *text) {
    Coords size = { 0, 16 };
    int line = 0;
    for (const char *c = text; *c; c++) {
        if (*c == '\n') {
            size.height += 16;
            line = 0;
        } else if (++line * 8 > size.width) {
            size.width = line * 8;
        }
    }
    return size;
}

AthenaFontRender *athena_font_render_create(AthenaFont *font, const char *text) {
    AthenaFontRender *render = calloc(1, sizeof(*render));
    if (!render)
        return NULL;
    render->font = font;
    render->text = strdup(text);
    if (!render->text) {
        free(render);
        return NULL;
    }
    return render;
}

void athena_font_render_destroy(AthenaFontRender *render) {
    if (render)
        free(render->text);
    free(render);
}

void athena_font_render_print(AthenaFontRender *render, float x, float y) {}
