#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <math.h>
#include <fcntl.h>
#include <athena/debug.h>

#include <jpeglib.h>
#include <png.h>

#include <athena/graphics.h>


struct gsBitMapFileHeader
{
	u16	Type;
	u32	Size;
	u16 Reserved1;
	u16 Reserved2;
	u32 Offset;
} __attribute__ ((packed));
typedef struct gsBitMapFileHeader GSBMFHDR;

struct gsBitMapInfoHeader
{
	u32	Size;
	u32	Width;
	u32	Height;
	u16	Planes;
	u16 BitCount;
	u32 Compression;
	u32 SizeImage;
	u32 XPelsPerMeter;
	u32 YPelsPerMeter;
	u32 ColorUsed;
	u32 ColorImportant;
};
typedef struct gsBitMapInfoHeader GSBMIHDR;
/* This header naturally has the on-disk 40-byte layout. Keep it aligned:
 * packed LWL/LWR loads on R5900 do not provide the register sign extension
 * that GCC assumes when lowering the signed-height normalization. */
_Static_assert(sizeof(GSBMIHDR) == 40, "BMP info header layout must be 40 bytes");

struct gsBitMapClut
{
	u8 Blue;
	u8 Green;
	u8 Red;
	u8 Alpha;
} __attribute__ ((packed));
typedef struct gsBitMapClut GSBMCLUT;

struct gsBitmap
{
	GSBMFHDR FileHeader;
	GSBMIHDR InfoHeader;
	char *Texture;
	GSBMCLUT *Clut;
};
typedef struct gsBitmap GSBITMAP;


/* Decode into the final EE buffer. The heap-owned cleanup state remains
 * defined after libpng longjmp, unlike modified automatic local pointers. */
typedef struct {
    png_structp png;
    png_infop info;
    png_bytep *rows;
} AthenaPngDecode;

int athena_load_png(GSSURFACE *tex, FILE *file, bool delayed)
{
    (void)delayed;
    if (!file) return -1;
    if (!tex) { fclose(file); return -1; }
    tex->Mem = NULL;
    tex->Clut = NULL;
    AthenaPngDecode *state = calloc(1, sizeof(*state));
    if (!state) { fclose(file); return -1; }
    state->png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!state->png) goto fail;
    state->info = png_create_info_struct(state->png);
    if (!state->info) goto fail;
    if (setjmp(png_jmpbuf(state->png))) goto fail;

    png_init_io(state->png, file);
    png_read_info(state->png, state->info);
    png_uint_32 width, height;
    int depth, type, interlace;
    png_get_IHDR(state->png, state->info, &width, &height, &depth, &type,
                 &interlace, NULL, NULL);
    if (!width || !height || width > 1024 || height > 1024) goto fail;
    if (type == PNG_COLOR_TYPE_PALETTE) {
        if ((depth != 4 && depth != 8) || (depth == 4 && (width & 1))) goto fail;
    } else {
        if (depth == 16) png_set_strip_16(state->png);
        if (type == PNG_COLOR_TYPE_GRAY && depth < 8)
            png_set_expand_gray_1_2_4_to_8(state->png);
        if (type == PNG_COLOR_TYPE_GRAY || type == PNG_COLOR_TYPE_GRAY_ALPHA)
            png_set_gray_to_rgb(state->png);
        if (png_get_valid(state->png, state->info, PNG_INFO_tRNS))
            png_set_tRNS_to_alpha(state->png);
    }
    /* png_read_image combines Adam7 passes in the persistent final rows. */
    png_set_interlace_handling(state->png);
    png_read_update_info(state->png, state->info);
    type = png_get_color_type(state->png, state->info);
    size_t stride = png_get_rowbytes(state->png, state->info);
    size_t expected;
    if (type == PNG_COLOR_TYPE_RGB_ALPHA) {
        tex->PSM = GS_PSM_CT32; expected = (size_t)width * 4;
    } else if (type == PNG_COLOR_TYPE_RGB) {
        tex->PSM = GS_PSM_CT24; expected = (size_t)width * 3;
    } else if (type == PNG_COLOR_TYPE_PALETTE) {
        tex->PSM = depth == 4 ? GS_PSM_T4 : GS_PSM_T8;
        expected = depth == 4 ? width / 2 : width;
    } else goto fail;
    if (stride != expected) goto fail;
    tex->Width = width;
    tex->Height = height;
    tex->VramClut = 0;
    tex->ClutStorageMode = GS_CLUT_STORAGE_CSM1;
    size_t size = athena_surface_size(width, height, tex->PSM);
    if (!size || stride * height > size) goto fail;
    /* Transfers may read the complete last DMA QW, including tiny textures. */
    size = (size + 15u) & ~(size_t)15u;
    tex->Mem = memalign(128, size);
    if (!tex->Mem) goto fail;
    memset(tex->Mem, 0, size);
    state->rows = calloc(height, sizeof(*state->rows));
    if (!state->rows) goto fail;
    for (unsigned y = 0; y < height; y++)
        state->rows[y] = (png_bytep)tex->Mem + y * stride;
    png_read_image(state->png, state->rows);

    if (type == PNG_COLOR_TYPE_RGB_ALPHA) {
        unsigned char *pixels = (unsigned char *)tex->Mem;
        for (size_t i = 3; i < stride * height; i += 4) pixels[i] >>= 1;
    } else if (type == PNG_COLOR_TYPE_PALETTE) {
        png_colorp palette = NULL;
        png_bytep alpha = NULL;
        int colors = 0, alphas = 0;
        unsigned limit = depth == 4 ? 16 : 256;
        if (!png_get_PLTE(state->png, state->info, &palette, &colors) ||
            colors <= 0 || (unsigned)colors > limit) goto fail;
        png_get_tRNS(state->png, state->info, &alpha, &alphas, NULL);
        if (alphas < 0 || alphas > colors) goto fail;
        size_t clut_size = athena_surface_size(depth == 4 ? 8 : 16,
                                               depth == 4 ? 2 : 16, GS_PSM_CT32);
        tex->Clut = memalign(128, clut_size);
        if (!tex->Clut) goto fail;
        memset(tex->Clut, 0, clut_size);
        tex->ClutPSM = GS_PSM_CT32;
        for (int i = 0; i < colors; i++) {
            unsigned slot = i;
            if (depth == 8) slot = (slot & ~0x18u) | ((slot & 8u) << 1) | ((slot & 16u) >> 1);
            unsigned a = i < alphas ? alpha[i] >> 1 : 0x80;
            tex->Clut[slot] = palette[i].red | ((uint32_t)palette[i].green << 8) |
                             ((uint32_t)palette[i].blue << 16) | ((uint32_t)a << 24);
        }
        if (depth == 4) {
            unsigned char *pixels = (unsigned char *)tex->Mem;
            for (size_t i = 0; i < stride * height; i++)
                pixels[i] = (pixels[i] << 4) | (pixels[i] >> 4);
        }
    }
    png_read_end(state->png, NULL);
    tex->Filter = GS_FILTER_NEAREST;
    free(state->rows);
    png_destroy_read_struct(&state->png, &state->info, NULL);
    free(state);
    fclose(file);
    athena_calculate_tbw(tex);
    return 0;
fail:
    free(state->rows);
    if (state->png) png_destroy_read_struct(&state->png, &state->info, NULL);
    free(state);
    free(tex->Mem); free(tex->Clut);
    tex->Mem = NULL; tex->Clut = NULL;
    fclose(file);
    return -1;
}

/* BI_RGB Windows BMPs: 4/8-bit indexed, 16-bit RGB555 and 24-bit BGR.
 * Bound all header-derived sizes before allocating. Decode one padded file
 * row at a time, including top-down images, into tightly packed GS pixels. */
int athena_load_bmp(GSSURFACE *tex, FILE *file, bool delayed)
{
    (void)delayed;
    if (!file) return -1;
    if (!tex) { fclose(file); return -1; }
    tex->Mem = NULL; tex->Clut = NULL;
    GSBITMAP bmp = {0};
    const char *stage = "header";
    unsigned char *row = NULL;
    if (fread(&bmp.FileHeader, sizeof(bmp.FileHeader), 1, file) != 1 ||
        fread(&bmp.InfoHeader, sizeof(bmp.InfoHeader), 1, file) != 1) goto fail;
    GSBMIHDR *h = &bmp.InfoHeader;
    /* The DIB stores a signed 32-bit height. Normalize its two's-complement
     * representation with unsigned arithmetic before checking magnitudes. */
    uint32_t raw_height = h->Height;
    bool top_down = (raw_height & 0x80000000u) != 0;
    uint32_t height = top_down ? (uint32_t)(0u - raw_height) : raw_height;
    if (bmp.FileHeader.Type != 0x4d42 || h->Size < 40 || h->Planes != 1 ||
        h->Compression != 0 || !h->Width || h->Width > 1024 ||
        !height || height > 1024) goto fail;
    unsigned width = h->Width;
    unsigned bits = h->BitCount;
    if (bits != 4 && bits != 8 && bits != 16 && bits != 24) goto fail;
    if (bits == 4 && (width & 1)) goto fail;
    stage = "file bounds";
    size_t stride = (((size_t)width * bits + 31) / 32) * 4;
    if (fseek(file, 0, SEEK_END) != 0) goto fail;
    long file_size = ftell(file);
    if (file_size < 0) goto fail;
    if ((size_t)file_size < sizeof(GSBMFHDR) ||
        h->Size > (size_t)file_size - sizeof(GSBMFHDR)) goto fail;
    size_t header_end = sizeof(GSBMFHDR) + (size_t)h->Size;
    size_t offset = bmp.FileHeader.Offset;
    if (header_end > (size_t)file_size || offset < header_end ||
        offset > (size_t)file_size || stride * height > (size_t)file_size - offset) goto fail;
    tex->Width = width; tex->Height = height;
    tex->PSM = bits == 4 ? GS_PSM_T4 : bits == 8 ? GS_PSM_T8 :
               bits == 16 ? GS_PSM_CT16 : GS_PSM_CT24;
    tex->Filter = GS_FILTER_NEAREST;
    tex->VramClut = 0;
    tex->ClutStorageMode = GS_CLUT_STORAGE_CSM1;
    if (bits <= 8) {
        stage = "palette";
        unsigned limit = 1u << bits;
        unsigned colors = h->ColorUsed ? h->ColorUsed : limit;
        if (colors > limit || colors * sizeof(GSBMCLUT) > offset - header_end) goto fail;
        size_t clut_size = athena_surface_size(bits == 4 ? 8 : 16, bits == 4 ? 2 : 16, GS_PSM_CT32);
        tex->Clut = memalign(128, clut_size);
        if (!tex->Clut) goto fail;
        memset(tex->Clut, 0, clut_size);
        tex->ClutPSM = GS_PSM_CT32;
        if (fseek(file, header_end, SEEK_SET) != 0) goto fail;
        for (unsigned i = 0; i < colors; i++) {
            GSBMCLUT color;
            if (fread(&color, sizeof(color), 1, file) != 1) goto fail;
            unsigned slot = i;
            if (bits == 8) slot = (slot & ~0x18u) | ((slot & 8u) << 1) | ((slot & 16u) >> 1);
            tex->Clut[slot] = color.Red | ((uint32_t)color.Green << 8) |
                              ((uint32_t)color.Blue << 16) | 0x80000000u;
        }
    }
    size_t size = (athena_surface_size(width, height, tex->PSM) + 15u) & ~(size_t)15u;
    stage = "allocation";
    tex->Mem = memalign(128, size);
    row = malloc(stride);
    if (!tex->Mem || !row) goto fail;
    memset(tex->Mem, 0, size);
    if (fseek(file, offset, SEEK_SET) != 0) goto fail;
    stage = "pixel rows";
    size_t packed = (size_t)width * bits / 8;
    for (unsigned y = 0; y < height; y++) {
        if (fread(row, 1, stride, file) != stride) goto fail;
        unsigned target = top_down ? y : height - y - 1;
        unsigned char *dst = (unsigned char *)tex->Mem + target * packed;
        if (bits == 24) {
            for (unsigned x = 0; x < width; x++) {
                dst[x*3] = row[x*3+2]; dst[x*3+1] = row[x*3+1]; dst[x*3+2] = row[x*3];
            }
        } else if (bits == 16) {
            for (unsigned x = 0; x < width; x++) {
                uint16_t v = row[x*2] | ((uint16_t)row[x*2+1] << 8);
                v = (v & 0x83e0u) | ((v & 0x1fu) << 10) | ((v & 0x7c00u) >> 10);
                dst[x*2] = v; dst[x*2+1] = v >> 8;
            }
        } else if (bits == 4) {
            for (size_t x = 0; x < packed; x++) dst[x] = (row[x] << 4) | (row[x] >> 4);
        } else memcpy(dst, row, packed);
    }
    free(row);
    fclose(file);
    athena_calculate_tbw(tex);
    return 0;
fail:
    dbgprintf("BMP: decode failed at %s (width=%lu height=%ld bits=%u)\n",
              stage, (unsigned long)bmp.InfoHeader.Width,
              (long)(int32_t)bmp.InfoHeader.Height, (unsigned)bmp.InfoHeader.BitCount);
    (void)stage;
    free(row); free(tex->Mem); free(tex->Clut);
    tex->Mem = NULL; tex->Clut = NULL;
    fclose(file);
    return -1;
}

/* Keep all libjpeg state on the heap: fields changed after setjmp must
 * remain defined when a decoder error jumps back into cleanup. */
typedef struct {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr error;
    jmp_buf jump;
} AthenaJpegDecode;

static void athena_jpeg_error(j_common_ptr cinfo)
{
    AthenaJpegDecode *state = (AthenaJpegDecode *)cinfo;
    (*cinfo->err->output_message)(cinfo);
    longjmp(state->jump, 1);
}

/* libjpeg normally repairs truncated streams and returns partial pixels.
 * Reject warnings as well so missing EOI/data never becomes a valid asset. */
static void athena_jpeg_message(j_common_ptr cinfo, int level)
{
    if (level < 0) athena_jpeg_error(cinfo);
}

int athena_load_jpeg(GSSURFACE *tex, FILE *fp, bool scale_down, bool delayed)
{
    (void)delayed;
    if (!fp) return -1;
    if (!tex) { fclose(fp); return -1; }
    tex->Mem = NULL;
    tex->Clut = NULL;
    AthenaJpegDecode *state = calloc(1, sizeof(*state));
    if (!state) { fclose(fp); return -1; }
    struct jpeg_decompress_struct *cinfo = &state->cinfo;
    cinfo->err = jpeg_std_error(&state->error);
    state->error.error_exit = athena_jpeg_error;
    state->error.emit_message = athena_jpeg_message;
    if (setjmp(state->jump)) goto fail;
    jpeg_create_decompress(cinfo);
    jpeg_stdio_src(cinfo, fp);
    if (jpeg_read_header(cinfo, TRUE) != JPEG_HEADER_OK) goto fail;
    if (!cinfo->image_width || !cinfo->image_height) goto fail;
    /* CMYK/YCCK need an explicit conversion policy; never interpret their
     * four color channels as RGBA. Grayscale is expanded by libjpeg. */
    if (cinfo->jpeg_color_space != JCS_GRAYSCALE &&
        cinfo->jpeg_color_space != JCS_RGB &&
        cinfo->jpeg_color_space != JCS_YCbCr) goto fail;
    cinfo->out_color_space = JCS_RGB;
    if (scale_down) {
        unsigned longest = cinfo->image_width > cinfo->image_height ?
                           cinfo->image_width : cinfo->image_height;
        /* Standard libjpeg supports 1/1, 1/2, 1/4 and 1/8 scaling. */
        cinfo->scale_num = 1;
        cinfo->scale_denom = 1;
        while (cinfo->scale_denom < 8 &&
               (longest + cinfo->scale_denom - 1) / cinfo->scale_denom > 1024)
            cinfo->scale_denom *= 2;
    }
    jpeg_calc_output_dimensions(cinfo);
    if (!cinfo->output_width || !cinfo->output_height ||
        cinfo->output_width > 1024 || cinfo->output_height > 1024) goto fail;
    if (!jpeg_start_decompress(cinfo) || cinfo->output_components != 3) goto fail;
    size_t stride = (size_t)cinfo->output_width * 3;
    size_t bytes = stride * cinfo->output_height;
    size_t padded = (bytes + 15u) & ~(size_t)15u;
    tex->Mem = memalign(128, padded);
    if (!tex->Mem) goto fail;
    memset(tex->Mem, 0, padded);
    while (cinfo->output_scanline < cinfo->output_height) {
        JSAMPROW row = (unsigned char *)tex->Mem + cinfo->output_scanline * stride;
        if (jpeg_read_scanlines(cinfo, &row, 1) != 1) goto fail;
    }
    if (!jpeg_finish_decompress(cinfo)) goto fail;
    tex->Width = cinfo->output_width;
    tex->Height = cinfo->output_height;
    tex->PSM = GS_PSM_CT24;
    tex->Filter = GS_FILTER_NEAREST;
    tex->VramClut = 0;
    tex->ClutStorageMode = GS_CLUT_STORAGE_CSM1;
    jpeg_destroy_decompress(cinfo);
    free(state);
    fclose(fp);
    athena_calculate_tbw(tex);
    return 0;
fail:
    /* Safe even if jpeg_create_decompress failed before creating its pool. */
    jpeg_destroy_decompress(cinfo);
    free(state);
    free(tex->Mem);
    tex->Mem = NULL;
    tex->Clut = NULL;
    fclose(fp);
    return -1;
}

/* Decodes from an open stream by magic number; the decoder closes it. */
static int load_image_stream(GSSURFACE* image, FILE* file, bool delayed,
	AthenaImageLoadError *error) {
	uint16_t magic;
	int result = -1;

	if (fread(&magic, sizeof(magic), 1, file) != 1) {
		fclose(file);
		if (error)
			*error = ATHENA_IMAGE_LOAD_DECODE;
		return -1;
	}
	fseek(file, 0, SEEK_SET);
	if (magic == 0x4D42)
		result = athena_load_bmp(image, file, delayed);
	else if (magic == 0xD8FF)
		result = athena_load_jpeg(image, file, false, delayed);
	else if (magic == 0x5089)
		result = athena_load_png(image, file, delayed);
	else {
		fclose(file);
		if (error)
			*error = ATHENA_IMAGE_LOAD_FORMAT;
	}

	if (result == 0 && error)
		*error = ATHENA_IMAGE_LOAD_OK;
	return result;
}

static int load_image_prepare(GSSURFACE* image, bool delayed,
	AthenaImageLoadError *error) {
	if (error)
		*error = ATHENA_IMAGE_LOAD_DECODE;
	if (!image) {
		if (error)
			*error = ATHENA_IMAGE_LOAD_SURFACE;
		return -1;
	}
	image->Delayed = delayed;
	image->PageAligned = false;
	image->Macroblock = false;
	return 0;
}

int load_image_ex(GSSURFACE* image, const char* path, bool delayed,
	AthenaImageLoadError *error) {
	FILE* file;

	if (!path) {
		if (error)
			*error = ATHENA_IMAGE_LOAD_SURFACE;
		return -1;
	}
	if (load_image_prepare(image, delayed, error) < 0)
		return -1;
	file = fopen(path, "rb");
	if (!file) {
		if (error)
			*error = ATHENA_IMAGE_LOAD_OPEN;
		return -1;
	}
	return load_image_stream(image, file, delayed, error);
}

/* Same formats from a borrowed buffer (e.g. an image embedded in a .glb),
 * read through fmemopen. */
int load_image_memory_ex(GSSURFACE* image, const void* data, size_t size,
	bool delayed, AthenaImageLoadError *error) {
	FILE* file;

	if (!data || size < 2) {
		if (error)
			*error = ATHENA_IMAGE_LOAD_DECODE;
		return -1;
	}
	if (load_image_prepare(image, delayed, error) < 0)
		return -1;
	file = fmemopen((void *)data, size, "rb");
	if (!file) {
		if (error)
			*error = ATHENA_IMAGE_LOAD_OPEN;
		return -1;
	}
	return load_image_stream(image, file, delayed, error);
}

int load_image(GSSURFACE* image, const char* path, bool delayed) {
	return load_image_ex(image, path, delayed, NULL);
}
