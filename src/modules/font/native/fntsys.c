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
#include "fntsys.h"
#include <athena/utf8.h>
#include "atlas.h"
#include <athena/graphics.h>
#include <athena/graphics/owl_packet.h>

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

static fnt_glyph_cache_entry_t *fntCacheGlyph(font_t *font, uint32_t gid);

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

void fntRenderGlyph(fnt_glyph_cache_entry_t *glyph, owl_packet *packet, int pen_x, int pen_y, float scale)
{
    float x1, y1, x2, y2;
    float u1, v1, u2, v2;

    x1 = (float)pen_x + ((float)glyph->ox * scale)-0.5f;

    if (GetInterlacedFrameMode()) {
        y1 = ((float)pen_y + ((float)glyph->oy / 2.0f) * scale)-0.5f;
        y2 = (y1 + ((float)glyph->height / 2.0f) * scale)-0.5f;
    } else {
        y1 = (float)pen_y + ((float)glyph->oy * scale)-0.5f;
        y2 = y1 + ((float)glyph->height * scale)-0.5f;
    }

    x2 = x1 + ((float)glyph->width * scale)-0.5f;

    u1 = glyph->allocation->x;
    v1 = glyph->allocation->y;
    u2 = glyph->allocation->x + glyph->width + 0.5f;
    v2 = glyph->allocation->y + glyph->height + 0.5f;

	owl_add_tag(packet, (uint64_t)(owl_coord_transform(x1, gsGlobal->OffsetX)) | ((uint64_t)(owl_coord_transform(y1, gsGlobal->OffsetY)) << 16), GS_SETREG_UV( owl_uv_transform(u1, 1024), owl_uv_transform(v1, 1024)));
	owl_add_tag(packet, (uint64_t)(owl_coord_transform(x2, gsGlobal->OffsetX)) | ((uint64_t)(owl_coord_transform(y2, gsGlobal->OffsetY)) << 16), GS_SETREG_UV( owl_uv_transform(u2, 1024), owl_uv_transform(v2, 1024)));
}

/*
 * Glyphs of one atlas are sent as one GIF packet whose sizes were reserved
 * for every glyph left in the string. Writes the sizes actually used once
 * the run ends: glyphs without a bitmap (spaces, tabs, missing characters)
 * reserved room they did not fill.
 */
static void fntFinishRun(owl_qword *cnt, owl_qword *direct, owl_qword *reglist,
    owl_qword *first, owl_qword *end, int texture_id)
{
    int size = (int)(((uint32_t)end - (uint32_t)first) / 16);

    cnt->dword[0] = DMA_TAG((texture_id != -1 ? 11 : 7) + size, 0, DMA_CNT, 0, 0, 0);
    direct->sword[3] = VIF_CODE(6 + size, 0, VIF_DIRECT, 0);
    reglist->dword[0] = VU_GS_GIFTAG(size, 1, NO_CUSTOM_DATA, 0, 0, 1, 2);
}

int fntRenderString(int id, int x, int y, short aligned, size_t width, size_t height, const char *string, float scale, u64 colour)
{
    font_t *font = fntGet(id);

    if (!font || !string)
        return x;
    fntCheckVideoMode();

    int line_height = fntLineHeight(font, scale);
    int text_height = (int)(font->size * scale) + (fntCountLines(string) - 1) * line_height;

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

    owl_packet *packet = NULL;
    GSSURFACE *tex = NULL;
    const char *text_to_render = string;
    owl_qword *last_cnt = NULL, *last_direct = NULL, *last_prim = NULL,
        *before_first_draw = NULL, *after_draw = NULL;
    int text_size = 0, texture_id = -1;
    bool started_rendering = false;

    for (; *text_to_render; ++text_to_render) {
        if (*text_to_render == '\n') {
            y += line_height;
            pen_x = fntAlignLine(font, scale, text_to_render + 1, x, aligned);
            previous = 0;
            state = UTF8_ACCEPT;
            continue;
        }
        if (utf8Decode(&state, &codepoint, *text_to_render)) // accumulate the codepoint value
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

        if (glyph->allocation) {
            if (tex != &glyph->atlas->surface || !glyph->atlas->surface.Vram) {
                tex = &glyph->atlas->surface;

                if (started_rendering)
                    fntFinishRun(last_cnt, last_direct, last_prim, before_first_draw, after_draw, texture_id);

                // Room for every glyph left: spaces, line breaks and UTF-8 continuation bytes excluded
                text_size = strlen(text_to_render)-count_spaces(text_to_render, " \n")-count_nonascii(text_to_render);
                int text_vert_size = (text_size*2);

                texture_id = texture_manager_bind(gsGlobal, tex, true);

	            packet = owl_query_packet(CHANNEL_VIF1, (texture_id != -1? 12 : 8)+text_vert_size);

                last_cnt = packet->ptr;
	            owl_add_cnt_tag(packet, (texture_id != -1? 11 : 7)+text_vert_size, 0); // 4 quadwords for vif

	            if (texture_id != -1) {
	            	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	            	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	            	owl_add_uint(packet, VIF_CODE(0, 0, VIF_FLUSH, 0));
	            	owl_add_uint(packet, VIF_CODE(2, 0, VIF_DIRECT, 0));

	            	owl_add_tag(packet, GIF_AD, GIFTAG(1, 1, 0, 0, 0, 1));
	            	owl_add_tag(packet, GIF_NOP, 0);

	            	owl_add_uint(packet, VIF_CODE(0, 0, VIF_FLUSHA, 0));
	            	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	            	owl_add_uint(packet, VIF_CODE(texture_id, 0, VIF_MARK, 0));
	            	owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 1));
	            }

                last_direct = packet->ptr;
	            owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	            owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
	            owl_add_uint(packet, VIF_CODE(0, 0, VIF_FLUSHA, 0));
	            owl_add_uint(packet, VIF_CODE(6+(text_size*2), 0, VIF_DIRECT, 0)); // 3 giftags

	            owl_add_tag(packet, GIF_AD, GIFTAG(4, 1, 0, 0, 0, 1));

	            int tw, th;
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

                owl_add_tag(packet, GS_RGBAQ, colour);

                last_prim = packet->ptr;
	            owl_add_tag(packet,
					   ((uint64_t)(GS_UV) << 0 | (uint64_t)(GS_XYZ2) << 4),
					   	VU_GS_GIFTAG(text_vert_size,
							1, NO_CUSTOM_DATA, 0,
							0,
    						1, 2)
						);

                before_first_draw = packet->ptr;
            }

            fntRenderGlyph(glyph, packet, pen_x, y, scale);

            after_draw = packet->ptr;

            started_rendering = true;
        }

        pen_x += ((int)(glyph->shx*scale) >> 6);
    }

    if (started_rendering)
        fntFinishRun(last_cnt, last_direct, last_prim, before_first_draw, after_draw, texture_id);

    return pen_x;
}

int fntRenderStringPlus(int id, int x, int y, short aligned, size_t width, size_t height, const char *string, float scale, u64 colour, float outline, u64 outline_colour, float dropshadow, u64 dropshadow_colour) {
    if (outline > 0.0f) {
        float offsets[][2] = { {outline, outline}, {outline, -outline}, {-outline, outline}, {-outline, -outline} };

	    for(int i = 0; i < 4; i++){
            fntRenderString(id, x+offsets[i][0], y+offsets[i][1], aligned, width, height, string, scale, outline_colour);
	    }
    } else if (dropshadow > 0.0f) {
        fntRenderString(id, x+dropshadow, y+dropshadow, aligned, width, height, string, scale, dropshadow_colour);
    }

    fntRenderString(id, x, y, aligned, width, height, string, scale, colour);
    return 0;
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
