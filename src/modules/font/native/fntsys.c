/*
 Copyright 2010, Volca
 Licenced under Academic Free License version 3.0
 Review OpenUsbLd README & LICENSE files for further details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <timer.h>
#include "fntsys.h"
#include <athena/utf8.h>
#include "atlas.h"
#include <athena/graphics.h>
#include <athena/graphics/owl_packet.h>
#include <athena/graphics/texture_manager.h>

extern unsigned char quicksand_regular[] __attribute__((aligned(16)));
extern int size_quicksand_regular;

#include <sys/types.h>
#include <ft2build.h>

#include <athena/str_utils.h>

#include <athena/macros.h>

#include FT_FREETYPE_H

// freetype vars
static FT_Library font_library;
static int font_library_ready;

static GSCLUT fontClut;

static const float fDPI = 72.0f;

/** Single entry in the glyph cache */
typedef struct
{
    int isValid;
    // size in pixels of the glyph
    int width, height;
    // offsetting of the glyph
    int ox, oy;
    // advancements in pixels after rendering this glyph
    int shx, shy;
    // FreeType glyph index, for kerning
    FT_UInt index;

    // atlas for which the allocation was done
    atlas_t *atlas;

    // atlas allocation position
    struct atlas_allocation_t *allocation;

} fnt_glyph_cache_entry_t;

/** A whole font definition */
typedef struct
{
    /** GLYPH CACHE. Every glyph is cached when first used, so no additional
     * memory aside from the one needed to render the used characters is used.
     */
    fnt_glyph_cache_entry_t **glyphCache;

    /// Maximal font cache page index
    int cacheMaxPageID;

    /// Font face
    FT_Face face;

    /// Nonzero if font is used
    int isValid;

    /// Texture atlases (default to NULL)
    atlas_t *atlases[ATLAS_MAX];

    /// Font file contents, freed with the font (NULL for the embedded font)
    void *dataPtr;

    /// Path the font was loaded from (NULL for the embedded font), for sharing
    char *path;

    /// Rasterization size in pixels, side of its atlases, and references
    int size;
    int atlasSize;
    int refs;

    /// Changes whenever the glyphs move (cache flushed), to invalidate text layouts
    unsigned generation;

    FT_Bool kerning;
} font_t;

/// Array of font definitions
static font_t fonts[FNT_MAX_COUNT];

/// Video mode the glyphs are rasterized for (fntUpdateAspectRatio)
static struct {
    int width, height, mode, frame;
    float yscale;
} fntVideo = { 0, 0, -1, 0, 1.0f };

#define GLYPH_CACHE_PAGE_SIZE 256

/// Source of font_t::generation values, never reused while the ELF runs
static unsigned fntGenerations;

static fnt_glyph_cache_entry_t *fntCacheGlyph(font_t *font, uint32_t gid);
static void fntScratchFree(void);

static font_t *fntGet(int id)
{
    if (id < 0 || id >= FNT_MAX_COUNT || !fonts[id].isValid)
        return NULL;
    return &fonts[id];
}

/* Whole file in a malloc'ed buffer, or NULL. */
void *fntReadFile(const char *path, int *size)
{
    void *buffer;
    int fd = open(path, O_RDONLY, 0666);
    int length, done = 0;

    if (fd < 0)
        return NULL;
    length = lseek(fd, 0, SEEK_END);
    if (length <= 0 || lseek(fd, 0, SEEK_SET) < 0) {
        close(fd);
        return NULL;
    }
    buffer = malloc(length);
    if (!buffer) {
        close(fd);
        return NULL;
    }
    while (done < length) {
        int got = read(fd, (char *)buffer + done, length - done);
        if (got <= 0)
            break;
        done += got;
    }
    close(fd);
    if (done != length) {
        free(buffer);
        return NULL;
    }
    *size = length;
    return buffer;
}

static void fntCacheFlushPage(fnt_glyph_cache_entry_t *page)
{
    int i;

    for (i = 0; i < GLYPH_CACHE_PAGE_SIZE; ++i, ++page) {
        page->isValid = 0;
        // we're not doing any atlasFree or such - atlas has to be rebuild
        page->allocation = NULL;
        page->atlas = NULL;
    }
}

static void fntCacheFlush(font_t *font)
{
    // Release all the glyphs from the cache
    int i;
    for (i = 0; i <= font->cacheMaxPageID; ++i) {
        if (font->glyphCache[i]) {
            fntCacheFlushPage(font->glyphCache[i]);
            free(font->glyphCache[i]);
            font->glyphCache[i] = NULL;
        }
    }

    free(font->glyphCache);
    font->glyphCache = NULL;
    font->cacheMaxPageID = -1;
    font->generation = ++fntGenerations;

    // free all atlasses too, they're invalid now anyway
    int aid;
    for (aid = 0; aid < ATLAS_MAX; ++aid) {
        atlasFree(font->atlases[aid]);
        font->atlases[aid] = NULL;
    }
}

static int fntPrepareGlyphCachePage(font_t *font, int pageid)
{
    if (pageid > font->cacheMaxPageID) {
        fnt_glyph_cache_entry_t **np = (fnt_glyph_cache_entry_t**)realloc(font->glyphCache, (pageid + 1) * sizeof(fnt_glyph_cache_entry_t *));

        if (!np)
            return 0;

        font->glyphCache = np;

        int page;
        for (page = font->cacheMaxPageID + 1; page <= pageid; ++page)
            font->glyphCache[page] = NULL;

        font->cacheMaxPageID = pageid;
    }

    // if it already was allocated, skip this
    if (font->glyphCache[pageid])
        return 1;

    // allocate the page
    font->glyphCache[pageid] = (fnt_glyph_cache_entry_t*)calloc(GLYPH_CACHE_PAGE_SIZE, sizeof(fnt_glyph_cache_entry_t));
    return font->glyphCache[pageid] != NULL;
}

static void fntPrepareCLUT()
{
    fontClut.PSM = GS_PSM_T8;
    fontClut.ClutPSM = GS_PSM_CT32;
    fontClut.Clut = (u32*)memalign(128, 256 * 4);
    fontClut.VramClut = 0;
    if (!fontClut.Clut)
        return;

    // generate the clut table
    size_t i;
    u32 *clut = fontClut.Clut;
    for (i = 0; i < 256; ++i) {
        u8 alpha = (i * 128) / 255;

        *clut = GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, alpha);
        clut++;
    }
}

static void fntDestroyCLUT()
{
    free(fontClut.Clut);
    fontClut.Clut = NULL;
}

static void fntInitSlot(font_t *font)
{
    memset(font, 0, sizeof(*font));
    font->cacheMaxPageID = -1;
}

static void fntDeleteSlot(font_t *font)
{
    // free the glyph cache, atlases, unload the font
    fntCacheFlush(font);

    if (font->face) {
        FT_Done_Face(font->face);
        font->face = NULL;
    }

    free(font->dataPtr);
    free(font->path);
    fntInitSlot(font);
}

/* Vertical scale that makes glyphs square on the TV: pixels are not square in NTSC (taller) and PAL (shorter). */
static float fntVerticalScale(void)
{
    float aspect, yscale;

    if (!gsGlobal || gsGlobal->Width <= 0 || gsGlobal->Height <= 0)
        return 448.0f / 480.0f;
    aspect = (gsGlobal->Mode == GS_MODE_DTV_720P || gsGlobal->Mode == GS_MODE_DTV_1080I) ?
        16.0f / 9.0f : 4.0f / 3.0f;
    yscale = ((float)gsGlobal->Height / (float)gsGlobal->Width) * aspect;
    // Supersample height*2 when using interlaced frame mode; glyphs are drawn at half height
    if (GetInterlacedFrameMode() == 1)
        yscale *= 2.0f;
    return yscale;
}

static void fntApplySize(font_t *font)
{
    FT_Set_Char_Size(font->face, font->size * 64, font->size * 64, fDPI, fDPI * fntVideo.yscale);
}

void fntUpdateAspectRatio()
{
    int i;

    if (gsGlobal) {
        fntVideo.width = gsGlobal->Width;
        fntVideo.height = gsGlobal->Height;
        fntVideo.mode = gsGlobal->Mode;
    }
    fntVideo.frame = GetInterlacedFrameMode();
    fntVideo.yscale = fntVerticalScale();

    // flush cache - it will be invalid after the setting
    for (i = 0; i < FNT_MAX_COUNT; i++) {
        if (fonts[i].isValid) {
            fntCacheFlush(&fonts[i]);
            fntApplySize(&fonts[i]);
        }
    }
}

/* Screen.setMode() changes the pixel aspect ratio: follow it. */
static void fntCheckVideoMode(void)
{
    if (gsGlobal && (gsGlobal->Width != fntVideo.width || gsGlobal->Height != fntVideo.height ||
        gsGlobal->Mode != fntVideo.mode || GetInterlacedFrameMode() != fntVideo.frame))
        fntUpdateAspectRatio();
}

void fntInit()
{
    int i;

    if (FT_Init_FreeType(&font_library)) {
        // just report over the ps2link
        return;
    }
    font_library_ready = 1;

    fntPrepareCLUT();

    for (i = 0; i < FNT_MAX_COUNT; ++i)
        fntInitSlot(&fonts[i]);

    fntUpdateAspectRatio();
}

void fntEnd()
{
    // release all the fonts
    int id;
    for (id = 0; id < FNT_MAX_COUNT; ++id)
        if (fonts[id].isValid)
            fntDeleteSlot(&fonts[id]);

    // deinit freetype system
    if (font_library_ready)
        FT_Done_FreeType(font_library);
    font_library_ready = 0;

    fntDestroyCLUT();

    fntScratchFree();
}

static int fntClampSize(int size)
{
    if (size <= 0)
        return FNTSYS_CHAR_SIZE;
    return size < FNTSYS_MIN_SIZE ? FNTSYS_MIN_SIZE : size > FNTSYS_MAX_SIZE ? FNTSYS_MAX_SIZE : size;
}

/* A loaded font with the same file and size, shared instead of loaded twice. */
static int fntFindShared(const char *path, int size)
{
    int i;
    for (i = 0; i < FNT_MAX_COUNT; i++) {
        font_t *font = &fonts[i];
        if (font->isValid && font->size == size &&
            ((!path && !font->path) || (path && font->path && strcmp(path, font->path) == 0)))
            return i;
    }
    return -1;
}

int fntLoadMemory(const char *path, void *data, int data_size, int size)
{
    font_t *font = NULL;
    int id;

    if (!font_library_ready)
        return FNT_ERROR;
    size = fntClampSize(size);

    id = fntFindShared(path, size);
    if (id >= 0) {
        free(data);
        fonts[id].refs++;
        return id;
    }

    for (id = 0; id < FNT_MAX_COUNT; id++) {
        if (!fonts[id].isValid) {
            font = &fonts[id];
            break;
        }
    }
    if (!font)
        return FNT_ERROR_SLOTS;

    fntInitSlot(font);
    if (path) {
        font->path = strdup(path);
        if (!font->path)
            return FNT_ERROR_MEMORY;
    }

    // load the font via memory handle
    if (FT_New_Memory_Face(font_library, (FT_Byte *)(data ? data : quicksand_regular),
        data ? data_size : size_quicksand_regular, 0, &font->face)) {
        free(font->path);
        fntInitSlot(font);
        return FNT_ERROR;
    }

    font->dataPtr = data;
    font->size = size;
    font->atlasSize = size <= 32 ? 256 : 512;
    font->refs = 1;
    font->kerning = FT_HAS_KERNING(font->face);
    font->generation = ++fntGenerations;
    font->isValid = 1;

    fntCheckVideoMode();
    fntApplySize(font);
    return id;
}

int fntLoadFile(const char *path, int size)
{
    void *data;
    int data_size = 0, id;

    id = fntFindShared(path, fntClampSize(size));
    if (id >= 0) {
        fonts[id].refs++;
        return id;
    }
    if (!path)
        return fntLoadMemory(NULL, NULL, 0, size);

    data = fntReadFile(path, &data_size);
    if (!data)
        return FNT_ERROR;
    id = fntLoadMemory(path, data, data_size, size);
    if (id < 0)
        free(data);
    return id;
}

void fntRelease(int id)
{
    font_t *font = fntGet(id);

    if (font && --font->refs <= 0)
        fntDeleteSlot(font);
}

int fntGetSize(int id)
{
    font_t *font = fntGet(id);
    return font ? font->size : 0;
}

static atlas_t *fntNewAtlas(font_t *font)
{
    atlas_t *atl = atlasNew(font->atlasSize, font->atlasSize, GS_PSM_T8);

    if (!atl)
        return NULL;
    atl->surface.ClutPSM = GS_PSM_CT32;
    atl->surface.Clut = (uint32_t *)fontClut.Clut;

    return atl;
}

static int fntGlyphAtlasPlace(font_t *font, fnt_glyph_cache_entry_t *glyph)
{
    FT_GlyphSlot slot = font->face->glyph;

    if (slot->bitmap.width == 0 || slot->bitmap.rows == 0) {
        // no bitmap glyph, just skip
        return 1;
    }

    int aid = 0;
    for (; aid < ATLAS_MAX; aid++) {
        atlas_t **atl = &font->atlases[aid];
        if (!*atl) { // atlas slot not yet used
            *atl = fntNewAtlas(font);
            if (!*atl)
                return 0;
        }

        glyph->allocation = atlasPlace(*atl, slot->bitmap.width, slot->bitmap.rows, slot->bitmap.buffer);
        if (glyph->allocation) {
            glyph->atlas = *atl;

            return 1;
        }
    }

    return 0;
}

/** Internal method. Makes sure the bitmap data for particular character are pre-rendered to the glyph cache */
static fnt_glyph_cache_entry_t *fntCacheGlyph(font_t *font, uint32_t gid)
{
    // calc page id and in-page index from glyph id
    int pageid = gid / GLYPH_CACHE_PAGE_SIZE;
    int idx = gid % GLYPH_CACHE_PAGE_SIZE;

    // do not call on every char of every font rendering call
    if (pageid > font->cacheMaxPageID || !font->glyphCache[pageid])
        if (!fntPrepareGlyphCachePage(font, pageid)) // failed to prepare the page...
            return NULL;

    fnt_glyph_cache_entry_t *glyph = &font->glyphCache[pageid][idx];
    if (glyph->isValid)
        return glyph;

    // not cached but valid. Cache
    if (!font->face)
        return NULL;

    if (FT_Load_Char(font->face, gid, FT_LOAD_RENDER))
        return NULL;

    // find atlas placement for the glyph
    if (!fntGlyphAtlasPlace(font, glyph))
        return NULL;

    FT_GlyphSlot slot = font->face->glyph;
    glyph->width = slot->bitmap.width;
    glyph->height = slot->bitmap.rows;
    glyph->shx = slot->advance.x;
    glyph->shy = slot->advance.y;
    glyph->ox = slot->bitmap_left;
    glyph->oy = -slot->bitmap_top;
    glyph->index = slot->glyph_index;

    glyph->isValid = 1;

    return glyph;
}

/* Horizontal adjustment between two glyphs, in pixels at `scale`. */
static int fntKerning(font_t *font, FT_UInt previous, FT_UInt index, float scale)
{
    FT_Vector delta;

    if (!font->kerning || !previous || !index ||
        FT_Get_Kerning(font->face, previous, index, FT_KERNING_DEFAULT, &delta))
        return 0;
    return (int)(delta.x * scale) >> 6;
}

/* Width of the line starting at `text`, up to a '\n' or the end, which is stored in `end`. */
static int fntLineWidth(font_t *font, float scale, const char *text, const char **end)
{
    uint32_t codepoint, state = UTF8_ACCEPT;
    FT_UInt previous = 0;
    int width = 0;

    for (; *text && *text != '\n'; ++text) {
        if (utf8Decode(&state, &codepoint, *text)) // accumulate the codepoint value
            continue;

        // Could just as well only get the glyph dimensions
        // but it is probable the glyphs will be needed anyway
        fnt_glyph_cache_entry_t *glyph = fntCacheGlyph(font, codepoint);
        if (!glyph)
            continue;

        width += fntKerning(font, previous, glyph->index, scale);
        previous = glyph->index;
        width += (int)(glyph->shx * scale) >> 6;
    }
    if (end)
        *end = text;
    return width;
}

/* Start of a line at `x` for the horizontal alignment. */
static int fntAlignLine(font_t *font, float scale, const char *line, int x, short aligned)
{
    if (aligned & ALIGN_HCENTER)
        return x - (fntLineWidth(font, scale, line, NULL) >> 1);
    if (aligned & ALIGN_RIGHT)
        return x - fntLineWidth(font, scale, line, NULL);
    return x;
}

static int fntCountLines(const char *text)
{
    int lines = 1;
    for (; *text; text++)
        if (*text == '\n')
            lines++;
    return lines;
}

static int fntLineHeight(font_t *font, float scale)
{
    int height = (int)(font->face->size->metrics.height >> 6);

    // glyphs are rasterized at twice the height in interlaced frame mode
    if (fntVideo.frame == 1)
        height /= 2;
    return (int)(height * scale);
}

int fntGetLineHeight(int id, float scale)
{
    font_t *font = fntGet(id);

    if (!font)
        return 0;
    fntCheckVideoMode();
    return fntLineHeight(font, scale);
}

/*
 * Text layout: the glyphs of a string placed once, as quads relative to the
 * text origin and grouped by atlas. Drawing a layout only translates the
 * quads and emits one packet per atlas, so an outline (4 extra copies) or a
 * FontRender printed every frame never measures or decodes the text again.
 */

/** A glyph placed by fntLayout(), relative to the text origin */
typedef struct
{
    float x1, y1, x2, y2;
    u64 uv1, uv2;
    // index of the glyph's atlas in font_t::atlases, to group the quads
    int atlas;
} fnt_quad_t;

struct fnt_layout
{
    // what the quads were laid out for (fntLayoutMatches)
    int id;
    unsigned generation;
    short aligned;
    size_t width, height;
    float scale;
    int valid;

    fnt_quad_t *quads;
    int count, capacity;

    // quads[start, start + count) use atlas
    struct {
        atlas_t *atlas;
        int start, count;
    } runs[ATLAS_MAX];
    int runCount;

    // pen position after the last glyph, relative to the origin
    int endX;

    // box of every quad, relative to the origin: culls the whole text
    float x0, y0, x1, y1;
};

/** One copy of the text: an outline or shadow offset, and its colour */
typedef struct
{
    float dx, dy;
    u64 colour;
} fnt_pass_t;

/// Layout of the texts printed directly (fntRenderString*), reused by every call
static fnt_layout_t fntScratch;

static void fntScratchFree(void)
{
    free(fntScratch.quads);
    memset(&fntScratch, 0, sizeof(fntScratch));
}

fnt_layout_t *fntLayoutNew(void)
{
    return calloc(1, sizeof(fnt_layout_t));
}

void fntLayoutFree(fnt_layout_t *layout)
{
    if (!layout)
        return;
    free(layout->quads);
    free(layout);
}

static int fntLayoutReserve(fnt_layout_t *layout, int count)
{
    fnt_quad_t *quads;

    if (count <= layout->capacity)
        return 1;
    quads = realloc(layout->quads, count * sizeof(fnt_quad_t));
    if (!quads)
        return 0;
    layout->quads = quads;
    layout->capacity = count;
    return 1;
}

static void fntLayoutAddGlyph(fnt_layout_t *layout, font_t *font, fnt_glyph_cache_entry_t *glyph,
    int pen_x, int pen_y, float scale)
{
    fnt_quad_t *quad;
    int aid;

    for (aid = 0; aid < ATLAS_MAX && font->atlases[aid] != glyph->atlas; aid++)
        ;
    if (aid == ATLAS_MAX || layout->count >= layout->capacity)
        return;

    quad = &layout->quads[layout->count++];
    quad->atlas = aid;
    quad->x1 = (float)pen_x + ((float)glyph->ox * scale) - 0.5f;
    if (fntVideo.frame == 1) {
        quad->y1 = ((float)pen_y + ((float)glyph->oy / 2.0f) * scale) - 0.5f;
        quad->y2 = (quad->y1 + ((float)glyph->height / 2.0f) * scale) - 0.5f;
    } else {
        quad->y1 = (float)pen_y + ((float)glyph->oy * scale) - 0.5f;
        quad->y2 = quad->y1 + ((float)glyph->height * scale) - 0.5f;
    }
    quad->x2 = quad->x1 + ((float)glyph->width * scale) - 0.5f;

    quad->uv1 = GS_SETREG_UV(owl_uv_transform(glyph->allocation->x, 1024),
        owl_uv_transform(glyph->allocation->y, 1024));
    quad->uv2 = GS_SETREG_UV(owl_uv_transform(glyph->allocation->x + glyph->width + 0.5f, 1024),
        owl_uv_transform(glyph->allocation->y + glyph->height + 0.5f, 1024));
}

static int fntCompareQuads(const void *a, const void *b)
{
    return ((const fnt_quad_t *)a)->atlas - ((const fnt_quad_t *)b)->atlas;
}

/* Groups the quads by atlas: the order within the text does not matter to the GS. */
static void fntLayoutGroup(fnt_layout_t *layout, font_t *font)
{
    int i, sorted = 1;

    for (i = 1; i < layout->count; i++) {
        if (layout->quads[i].atlas < layout->quads[i - 1].atlas) {
            sorted = 0;
            break;
        }
    }
    if (!sorted)
        qsort(layout->quads, layout->count, sizeof(fnt_quad_t), fntCompareQuads);

    layout->runCount = 0;
    for (i = 0; i < layout->count; i++) {
        if (!layout->runCount || layout->quads[i].atlas != layout->quads[i - 1].atlas) {
            layout->runs[layout->runCount].atlas = font->atlases[layout->quads[i].atlas];
            layout->runs[layout->runCount].start = i;
            layout->runs[layout->runCount].count = 0;
            layout->runCount++;
        }
        layout->runs[layout->runCount - 1].count++;
    }
}

static int fntLayoutBuild(fnt_layout_t *layout, font_t *font, int id, short aligned,
    size_t width, size_t height, const char *string, float scale)
{
    int line_height = fntLineHeight(font, scale);
    int text_height = (int)(font->size * scale) + (fntCountLines(string) - 1) * line_height;
    int x = 0, y = 0;

    layout->valid = 0;
    layout->count = 0;
    layout->runCount = 0;
    layout->endX = 0;
    // one quad per byte at most: spaces and UTF-8 continuation bytes use none
    if (!fntLayoutReserve(layout, strlen(string)))
        return 0;

    if (aligned & ALIGN_VCENTER)
        y += ((int)height - text_height) >> 1;
    else if (aligned & ALIGN_BOTTOM)
        y += (int)height - text_height;
    else
        y += ((int)(font->size * scale) - 2);

    int pen_x = fntAlignLine(font, scale, string, x, aligned);
    int xmax = x + width;

    uint32_t codepoint, state = UTF8_ACCEPT;
    FT_UInt previous = 0;
    const char *text = string;

    for (; *text; ++text) {
        if (*text == '\n') {
            y += line_height;
            pen_x = fntAlignLine(font, scale, text + 1, x, aligned);
            previous = 0;
            state = UTF8_ACCEPT;
            continue;
        }
        if (utf8Decode(&state, &codepoint, *text)) // accumulate the codepoint value
            continue;

        fnt_glyph_cache_entry_t *glyph = fntCacheGlyph(font, codepoint);
        if (!glyph)
            continue;

        pen_x += fntKerning(font, previous, glyph->index, scale);
        previous = glyph->index;

        if (width && pen_x + (int)(glyph->width * scale) > xmax) {
            pen_x = x;
            y += line_height;
        }

        if (glyph->allocation)
            fntLayoutAddGlyph(layout, font, glyph, pen_x, y, scale);

        pen_x += ((int)(glyph->shx * scale) >> 6);
    }

    fntLayoutGroup(layout, font);
    layout->x0 = layout->y0 = layout->x1 = layout->y1 = 0.0f;
    for (int i = 0; i < layout->count; i++) {
        const fnt_quad_t *quad = &layout->quads[i];

        if (i == 0 || quad->x1 < layout->x0) layout->x0 = quad->x1;
        if (i == 0 || quad->y1 < layout->y0) layout->y0 = quad->y1;
        if (i == 0 || quad->x2 > layout->x1) layout->x1 = quad->x2;
        if (i == 0 || quad->y2 > layout->y1) layout->y1 = quad->y2;
    }
    layout->id = id;
    layout->generation = font->generation;
    layout->aligned = aligned;
    layout->width = width;
    layout->height = height;
    layout->scale = scale;
    layout->endX = pen_x;
    layout->valid = 1;
    return 1;
}

int fntLayout(fnt_layout_t *layout, int id, short aligned, size_t width, size_t height,
    const char *string, float scale)
{
    font_t *font = fntGet(id);

    if (!layout)
        return 0;
    layout->valid = 0;
    if (!font || !string)
        return 0;
    fntCheckVideoMode();
    return fntLayoutBuild(layout, font, id, aligned, width, height, string, scale);
}

int fntLayoutMatches(const fnt_layout_t *layout, int id, short aligned, size_t width,
    size_t height, float scale)
{
    font_t *font = fntGet(id);

    if (!layout || !layout->valid || !font)
        return 0;
    // a new video mode rasterizes the glyphs again, in new atlas places
    fntCheckVideoMode();
    return layout->id == id && layout->generation == font->generation &&
        layout->aligned == aligned && layout->width == width &&
        layout->height == height && layout->scale == scale;
}

/* Glyphs converted per draw_tex_rect_list() call under a rotating view. */
#define FNT_ROTATED_BATCH 128

/*
 * Under a rotating view (athena/graphics/view.h) glyphs cannot stay GS
 * sprites: they go out as triangles, one packet per batch and pass.
 */
static void fntEmitQuadsRotated(atlas_t *atlas, const fnt_quad_t *quads, int count,
    const fnt_pass_t *passes, int pass_count, int x, int y)
{
    static prim_tex_rect rects[FNT_ROTATED_BATCH];
    int pass, i, n;

    for (pass = 0; pass < pass_count; pass++) {
        float ox = (float)x + passes[pass].dx;
        float oy = (float)y + passes[pass].dy;

        n = 0;
        for (i = 0; i < count; i++) {
            const fnt_quad_t *quad = &quads[i];
            prim_tex_rect *r = &rects[n++];

            r->x1 = quad->x1 + ox;
            r->y1 = quad->y1 + oy;
            r->x2 = quad->x2 + ox;
            r->y2 = quad->y2 + oy;
            /* GS_SETREG_UV: 12.4 fixed U in bits 0..13, V in bits 16..29. */
            r->u1 = (float)(quad->uv1 & 0x3FFF) / 16.0f;
            r->v1 = (float)((quad->uv1 >> 16) & 0x3FFF) / 16.0f;
            r->u2 = (float)(quad->uv2 & 0x3FFF) / 16.0f;
            r->v2 = (float)((quad->uv2 >> 16) & 0x3FFF) / 16.0f;
            if (n == FNT_ROTATED_BATCH) {
                draw_tex_rect_list(&atlas->surface, rects, n, passes[pass].colour);
                n = 0;
            }
        }
        if (n)
            draw_tex_rect_list(&atlas->surface, rects, n, passes[pass].colour);
    }
}

/*
 * One packet for `count` quads of an atlas, drawn once per pass: the texture
 * state is sent once, then each pass is its colour and the quads moved by
 * its offset.
 */
static void fntEmitQuads(atlas_t *atlas, const fnt_quad_t *quads, int count,
    const fnt_pass_t *passes, int pass_count, int x, int y)
{
    GSSURFACE *tex = &atlas->surface;
    int texture_id, upload, gif_size, body_size, tw, th, pass, i;
    owl_packet *packet;
    bool view = athena_view_kind() != ATHENA_VIEW_IDENTITY;

    if (athena_view_kind() == ATHENA_VIEW_ROTATED) {
        fntEmitQuadsRotated(atlas, quads, count, passes, pass_count, x, y);
        return;
    }

    texture_id = texture_manager_bind(gsGlobal, tex, true);
    if (texture_id == GRAPHICS_BIND_ERROR)
        return;
    upload = texture_id >= 0;

    // TEX0, TEX1 and PRIM; per pass, RGBAQ and the UV/XYZ2 pairs
    gif_size = 4 + pass_count * (3 + 2 * count);
    body_size = (upload ? 4 : 0) + 1 + gif_size;

    packet = owl_query_packet(CHANNEL_VIF1, body_size + 1);
    owl_add_cnt_tag(packet, body_size, 0);

    if (upload) {
        texture_manager_add_mark(packet, texture_id);
    }

    owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
    owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
    owl_add_uint(packet, VIF_CODE(0, 0, VIF_FLUSHA, 0));
    owl_add_uint(packet, VIF_CODE(gif_size, 0, VIF_DIRECT, 0));

    owl_add_tag(packet, GIF_AD, GIFTAG(3, 1, 0, 0, 0, 1));

    athena_set_tw_th(tex, &tw, &th);
    owl_add_tag(packet,
        GS_TEX0_1,
        GS_SETREG_TEX0((tex->Vram & ~GRAPHICS_TRANSFER_REQUEST_MASK)/256,
                      tex->TBW,
                      tex->PSM,
                      tw, th,
                      gsGlobal->PrimAlphaEnable,
                      COLOR_MODULATE,
                      (tex->VramClut & ~GRAPHICS_TRANSFER_REQUEST_MASK)/256,
                      tex->ClutPSM,
                      0, 0,
                      tex->VramClut? GS_CLUT_STOREMODE_LOAD : GS_CLUT_STOREMODE_NOLOAD)
    );
    owl_add_tag(packet, GS_TEX1_1, GS_SETREG_TEX1(1, 0, tex->Filter, tex->Filter, 0, 0, 0));
    owl_add_tag(packet, GS_PRIM,
        VU_GS_PRIM(
            GS_PRIM_PRIM_SPRITE,
            0,
            1,
            gsGlobal->PrimFogEnable,
            gsGlobal->PrimAlphaEnable,
            gsGlobal->PrimAAEnable,
            1,
            gsGlobal->PrimContext,
            0
        )
    );

    for (pass = 0; pass < pass_count; pass++) {
        float ox = (float)x + passes[pass].dx;
        float oy = (float)y + passes[pass].dy;

        owl_add_tag(packet, GIF_AD, GIFTAG(1, 1, 0, 0, 0, 1));
        owl_add_tag(packet, GS_RGBAQ, passes[pass].colour);
        owl_add_tag(packet,
            ((uint64_t)(GS_UV) << 0 | (uint64_t)(GS_XYZ2) << 4),
            VU_GS_GIFTAG(2 * count, 1, NO_CUSTOM_DATA, 0, 0, 1, 2));

        for (i = 0; i < count; i++) {
            const fnt_quad_t *quad = &quads[i];
            float x1 = quad->x1 + ox, y1 = quad->y1 + oy;
            float x2 = quad->x2 + ox, y2 = quad->y2 + oy;

            /* An axis-aligned view keeps glyphs as sprites. */
            if (view) {
                athena_view_apply(x1, y1, &x1, &y1);
                athena_view_apply(x2, y2, &x2, &y2);
            }
            owl_add_tag(packet,
                (uint64_t)(owl_coord_transform(x1, gsGlobal->OffsetX)) |
                ((uint64_t)(owl_coord_transform(y1, gsGlobal->OffsetY)) << 16),
                quad->uv1);
            owl_add_tag(packet,
                (uint64_t)(owl_coord_transform(x2, gsGlobal->OffsetX)) |
                ((uint64_t)(owl_coord_transform(y2, gsGlobal->OffsetY)) << 16),
                quad->uv2);
        }
    }
}

/* Quads of `pass_count` passes that fit in one packet of the packet buffer. */
static int fntQuadsPerPacket(int pass_count)
{
    owl_controller *controller = owl_get_controller();
    // cnt tag, texture upload, DIRECT, TEX0/TEX1/PRIM, a spare quadword for the end tag
    int fixed = 1 + 4 + 1 + 4 + 1 + pass_count * 3;

    if (!controller || (int)controller->size <= fixed)
        return 0;
    return ((int)controller->size - fixed) / (2 * pass_count);
}

static void fntEmitPasses(const fnt_layout_t *layout, const fnt_pass_t *passes, int pass_count,
    int x, int y)
{
    int capacity = fntQuadsPerPacket(pass_count);
    int r, offset;

    if (capacity <= 0)
        return;
    for (r = 0; r < layout->runCount; r++) {
        for (offset = 0; offset < layout->runs[r].count; offset += capacity) {
            int count = layout->runs[r].count - offset;
            if (count > capacity)
                count = capacity;
            fntEmitQuads(layout->runs[r].atlas, layout->quads + layout->runs[r].start + offset,
                count, passes, pass_count, x, y);
        }
    }
}

void fntLayoutDraw(const fnt_layout_t *layout, int x, int y, u64 colour, float outline,
    u64 outline_colour, float dropshadow, u64 dropshadow_colour)
{
    fnt_pass_t passes[5];
    int pass_count = 0, p;
    AthenaViewCuller culler;

    if (!layout || !layout->valid || !layout->count)
        return;

    /*
     * Under a camera, text outside its viewport is skipped whole: its box,
     * grown by the outline or shadow offset, is tested once.
     */
    if (athena_view_culler_init(&culler)) {
        float margin = outline > dropshadow ? outline : dropshadow;

        if (margin < 0.0f)
            margin = 0.0f;
        if (!athena_view_culler_visible(&culler, (float)x + layout->x0 - margin,
                (float)y + layout->y0 - margin,
                layout->x1 - layout->x0 + 2.0f * margin,
                layout->y1 - layout->y0 + 2.0f * margin)) {
            athena_view_count_culled(1);
            return;
        }
    }

    if (outline > 0.0f) {
        const float offsets[4][2] = { {outline, outline}, {outline, -outline}, {-outline, outline}, {-outline, -outline} };
        for (p = 0; p < 4; p++)
            passes[pass_count++] = (fnt_pass_t){ offsets[p][0], offsets[p][1], outline_colour };
    } else if (dropshadow > 0.0f) {
        passes[pass_count++] = (fnt_pass_t){ dropshadow, dropshadow, dropshadow_colour };
    }
    passes[pass_count++] = (fnt_pass_t){ 0.0f, 0.0f, colour };

    /*
     * Glyphs of one atlas do not overlap each other's copies in a way that
     * matters, but an outline of a second atlas must not cover the text of
     * the first: with several atlases, every pass is drawn over all of them
     * before the next one.
     */
    if (layout->runCount == 1) {
        fntEmitPasses(layout, passes, pass_count, x, y);
    } else {
        for (p = 0; p < pass_count; p++)
            fntEmitPasses(layout, &passes[p], 1, x, y);
    }
}

int fntRenderString(int id, int x, int y, short aligned, size_t width, size_t height, const char *string, float scale, u64 colour)
{
    if (!fntLayout(&fntScratch, id, aligned, width, height, string, scale))
        return x;
    fntLayoutDraw(&fntScratch, x, y, colour, 0.0f, 0, 0.0f, 0);
    return x + fntScratch.endX;
}

int fntRenderStringPlus(int id, int x, int y, short aligned, size_t width, size_t height, const char *string, float scale, u64 colour, float outline, u64 outline_colour, float dropshadow, u64 dropshadow_colour) {
    if (fntLayout(&fntScratch, id, aligned, width, height, string, scale))
        fntLayoutDraw(&fntScratch, x, y, colour, outline, outline_colour, dropshadow, dropshadow_colour);
    return 0;
}

int fntPreload(int id, const char *text, int *offset, float budget_ms)
{
    font_t *font = fntGet(id);
    uint64_t start = GetTimerSystemTime();
    uint64_t budget = budget_ms > 0.0f ? (uint64_t)(budget_ms * (kBUSCLK / 1000)) : 0;
    uint32_t codepoint, state = UTF8_ACCEPT;
    int position = *offset;

    if (!font || !text)
        return 1;
    fntCheckVideoMode();
    while (text[position]) {
        if (utf8Decode(&state, &codepoint, text[position++]))
            continue;
        *offset = position;
        if (codepoint != '\n')
            fntCacheGlyph(font, codepoint);
        if (budget && GetTimerSystemTime() - start >= budget)
            return !text[position];
    }
    *offset = position;
    return 1;
}

int fntCalcDimensions(int id, float scale, const char *str)
{
    font_t *font = fntGet(id);
    int width = 0;

    if (!font || !str)
        return 0;
    fntCheckVideoMode();
    for (;;) {
        const char *end;
        int line = fntLineWidth(font, scale, str, &end);
        if (line > width)
            width = line;
        if (!*end)
            break;
        str = end + 1;
    }
    return width;
}

Coords fntGetTextSize(int id, const char* text, float scale) {
    font_t *font = fntGet(id);
    Coords size = { 0, 0 };

    if (!font || !text)
        return size;
    size.width = fntCalcDimensions(id, scale, text);
    size.height = (int)(font->size * scale) + (fntCountLines(text) - 1) * fntLineHeight(font, scale);
	return size;
}
