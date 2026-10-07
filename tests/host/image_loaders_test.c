/* Exercise production decoders with real libpng/libjpeg and malformed files. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <jpeglib.h>
#include <athena/graphics.h>

static unsigned fail_aligned;
static unsigned fail_calloc, fail_malloc;
void *__real_calloc(size_t, size_t);
void *__real_malloc(size_t);
void *__wrap_calloc(size_t count, size_t size) {
    if (fail_calloc && !--fail_calloc) return NULL;
    return __real_calloc(count, size);
}
void *__wrap_malloc(size_t size) {
    if (fail_malloc && !--fail_malloc) return NULL;
    return __real_malloc(size);
}
void *__real_memalign(size_t, size_t);
void *__wrap_memalign(size_t alignment, size_t size) {
    if (fail_aligned && !--fail_aligned) return NULL;
    return __real_memalign(alignment, size);
}
uint32_t athena_surface_size(int w, int h, int psm) {
    unsigned pixels = (unsigned)w * h;
    return psm == GS_PSM_T4 ? pixels / 2 : psm == GS_PSM_T8 ? pixels :
           psm == GS_PSM_CT16 ? pixels * 2 : pixels * 4;
}
void athena_calculate_tbw(GSSURFACE *s) { s->TBW = (s->Width + 63) / 64; }
static void release(GSSURFACE *s) { free(s->Mem); free(s->Clut); memset(s, 0, sizeof(*s)); }
static FILE *png_fixture(int type, int depth, int interlace, unsigned width) {
    FILE *f = tmpfile(); assert(f);
    png_structp p = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(p); assert(p && info);
    assert(!setjmp(png_jmpbuf(p)));
    png_init_io(p, f);
    png_set_IHDR(p, info, width, 2, depth, type, interlace, 0, 0);
    png_color palette[256]; unsigned char alpha[256];
    for (unsigned i = 0; i < 256; i++) {
        palette[i] = (png_color){i, 255-i, i/2}; alpha[i] = i;
    }
    if (type == PNG_COLOR_TYPE_PALETTE) {
        png_set_PLTE(p, info, palette, depth == 4 ? 16 : 256);
        png_set_tRNS(p, info, alpha, depth == 4 ? 16 : 256, NULL);
    }
    png_write_info(p, info);
    unsigned char pixels[2][4096]; memset(pixels, 0, sizeof(pixels));
    for (unsigned y = 0; y < 2; y++) {
        if (type == PNG_COLOR_TYPE_RGBA) {
            for (unsigned x = 0; x < width; x++) {
                pixels[y][x*4] = 200+y; pixels[y][x*4+1] = 70;
                pixels[y][x*4+2] = 30; pixels[y][x*4+3] = x & 1 ? 255 : 64;
            }
        } else if (type == PNG_COLOR_TYPE_RGB) {
            for (unsigned x = 0; x < width; x++) {
                pixels[y][x*3] = 200+y; pixels[y][x*3+1] = 70; pixels[y][x*3+2] = 30;
            }
        } else if (type == PNG_COLOR_TYPE_GRAY_ALPHA) {
            for (unsigned x = 0; x < width; x++) { pixels[y][x*2] = 90+y; pixels[y][x*2+1] = 255; }
        } else if (type == PNG_COLOR_TYPE_PALETTE) {
            for (unsigned x = 0; x < width; x++) pixels[y][x] = depth == 4 ? 0x18 : 8+x;
        }
    }
    png_bytep rows[2] = {pixels[0], pixels[1]};
    png_write_image(p, rows); png_write_end(p, info);
    png_destroy_write_struct(&p, &info); rewind(f); return f;
}
static void png_tests(void) {
    GSSURFACE s = {0};
    for (int interlace = 0; interlace <= 1; interlace++) {
        assert(!athena_load_png(&s, png_fixture(PNG_COLOR_TYPE_RGBA,8,interlace,1024),true));
        assert(s.PSM == GS_PSM_CT32 && s.Mem[0] == 0x201e46c8u);
        assert(s.Mem[1] == 0x7f1e46c8u && s.Mem[1024] == 0x201e46c9u); release(&s);
        assert(!athena_load_png(&s, png_fixture(PNG_COLOR_TYPE_RGB,8,interlace,3),true));
        assert(s.PSM == GS_PSM_CT24 && ((unsigned char *)s.Mem)[9] == 201); release(&s);
        assert(!athena_load_png(&s, png_fixture(PNG_COLOR_TYPE_GRAY_ALPHA,8,interlace,2),true));
        assert(s.PSM == GS_PSM_CT32 && s.Mem[0] == 0x7f5a5a5au); release(&s);
        assert(!athena_load_png(&s, png_fixture(PNG_COLOR_TYPE_PALETTE,4,interlace,2),true));
        assert(s.PSM == GS_PSM_T4 && *(unsigned char *)s.Mem == 0x81);
        assert(s.Clut[8] == 0x0404f708u); release(&s);
        assert(!athena_load_png(&s, png_fixture(PNG_COLOR_TYPE_PALETTE,8,interlace,2),true));
        assert(s.PSM == GS_PSM_T8 && *(unsigned char *)s.Mem == 8);
        assert(s.Clut[16] == 0x0404f708u); release(&s);
    }
    for (unsigned failure = 1; failure <= 2; failure++) {
        fail_aligned = failure;
        assert(athena_load_png(&s, png_fixture(PNG_COLOR_TYPE_PALETTE,8,0,2),true) < 0);
        assert(!s.Mem && !s.Clut);
    }
    fail_aligned = 0;
    for (unsigned failure = 1; failure <= 2; failure++) {
        fail_calloc = failure;
        assert(athena_load_png(&s, png_fixture(PNG_COLOR_TYPE_RGBA,8,0,2),true) < 0);
        assert(!s.Mem && !s.Clut);
    }
    fail_calloc = 0;
    FILE *valid = png_fixture(PNG_COLOR_TYPE_RGBA,8,0,2);
    unsigned char truncated[48]; assert(fread(truncated,1,sizeof(truncated),valid) == sizeof(truncated)); fclose(valid);
    AthenaImageLoadError error;
    assert(load_image_memory_ex(&s,truncated,sizeof(truncated),true,&error) < 0);
    assert(error == ATHENA_IMAGE_LOAD_DECODE && !s.Mem && !s.Clut);
    /* Fail in png_read_end, after the final pixel buffer has been allocated. */
    valid = png_fixture(PNG_COLOR_TYPE_RGBA,8,0,2);
    assert(!fseek(valid,0,SEEK_END)); long length = ftell(valid); assert(length > 8);
    rewind(valid); unsigned char bytes[512]; assert((size_t)length < sizeof(bytes));
    assert(fread(bytes,1,length,valid) == (size_t)length); fclose(valid);
    assert(load_image_memory_ex(&s,bytes,length-8,true,&error) < 0);
    assert(!s.Mem && !s.Clut);
}
static void put16(unsigned char *p, unsigned v) { p[0]=v; p[1]=v>>8; }
static void put32(unsigned char *p, uint32_t v) { for (unsigned i=0;i<4;i++) p[i]=v>>(i*8); }
static size_t bmp_fixture(unsigned char *b, unsigned width, int height, unsigned bits, unsigned colors) {
    unsigned rows = height < 0 ? -height : height;
    unsigned palette = bits <= 8 ? (colors ? colors : 1u<<bits) : 0;
    unsigned stride = ((width*bits+31)/32)*4, offset = 54+palette*4;
    size_t n = offset+stride*rows; assert(n <= 4096); memset(b,0,n);
    put16(b,0x4d42); put32(b+2,n); put32(b+10,offset); put32(b+14,40);
    put32(b+18,width); put32(b+22,height); put16(b+26,1); put16(b+28,bits); put32(b+46,colors);
    for (unsigned i=0;i<palette;i++) { b[54+i*4]=30; b[55+i*4]=70; b[56+i*4]=i; }
    for (unsigned y=0;y<rows;y++) {
        unsigned char *p=b+offset+y*stride;
        for (unsigned x=0;x<width;x++) {
            if(bits==24) { p[x*3]=30; p[x*3+1]=70; p[x*3+2]=200+y; }
            else if(bits==16) put16(p+x*2,0xfc00);
            else p[x*bits/8]=bits==4 ? 0x18 : 8;
        }
    }
    return n;
}
static void bmp_tests(void) {
    unsigned char b[4096]; GSSURFACE s = {0}; AthenaImageLoadError error;
    for (int direction=-1;direction<=1;direction+=2) {
        size_t n=bmp_fixture(b,3,2*direction,24,0);
        assert(!load_image_memory_ex(&s,b,n,true,&error));
        assert(((unsigned char *)s.Mem)[0] == (direction<0 ? 200 : 201));
        assert(((unsigned char *)s.Mem)[9] == (direction<0 ? 201 : 200)); release(&s);
    }
    size_t n=bmp_fixture(b,3,2,8,0);
    assert(!load_image_memory_ex(&s,b,n,true,&error)); assert(s.Clut[16] == 0x801e4608u); release(&s);
    n=bmp_fixture(b,2,2,4,0);
    assert(!load_image_memory_ex(&s,b,n,true,&error)); assert(*(unsigned char *)s.Mem == 0x81); release(&s);
    n=bmp_fixture(b,3,2,16,0);
    assert(!load_image_memory_ex(&s,b,n,true,&error)); assert(*(uint16_t *)s.Mem == 0x801f); release(&s);
    for (unsigned bad=0;bad<10;bad++) {
        n=bmp_fixture(b,2,2,4,0);
        switch(bad) {
            case 0: put32(b+46,17); break; /* CLUT overflow */
            case 1: put32(b+10,n+1); break;
            case 2: put32(b+10,1); break;
            case 3: put32(b+18,0xffffffffu); break;
            case 4: put32(b+22,0x80000000u); break;
            case 5: put32(b+14,0xffffffffu); break;
            case 6: put32(b+30,1); break; /* RLE */
            case 7: put16(b+28,32); break;
            case 8: n--; break; /* truncated row */
            case 9: put16(b+26,2); break;
        }
        assert(load_image_memory_ex(&s,b,n,true,&error) < 0); assert(!s.Mem && !s.Clut);
    }
    for(unsigned failure=1;failure<=2;failure++) {
        n=bmp_fixture(b,2,2,4,0); fail_aligned=failure;
        assert(load_image_memory_ex(&s,b,n,true,&error)<0); assert(!s.Mem&&!s.Clut);
    }
    fail_aligned=0;
    n=bmp_fixture(b,2,2,4,0); fail_malloc=1;
    assert(load_image_memory_ex(&s,b,n,true,&error)<0); assert(!s.Mem&&!s.Clut);
    fail_malloc=0;
}
static FILE *jpeg_fixture(unsigned width, unsigned height, J_COLOR_SPACE color) {
    FILE *f = tmpfile(); assert(f);
    struct jpeg_compress_struct c = {0}; struct jpeg_error_mgr err;
    c.err = jpeg_std_error(&err); jpeg_create_compress(&c); jpeg_stdio_dest(&c, f);
    c.image_width = width; c.image_height = height;
    c.input_components = color == JCS_GRAYSCALE ? 1 : color == JCS_CMYK ? 4 : 3;
    c.in_color_space = color; jpeg_set_defaults(&c); jpeg_set_quality(&c, 100, TRUE);
    jpeg_start_compress(&c, TRUE);
    unsigned char *pixels = malloc((size_t)width * c.input_components); assert(pixels);
    memset(pixels, 90, (size_t)width * c.input_components);
    while (c.next_scanline < height) {
        JSAMPROW row = pixels; assert(jpeg_write_scanlines(&c, &row, 1) == 1);
    }
    free(pixels); jpeg_finish_compress(&c); jpeg_destroy_compress(&c);
    rewind(f); return f;
}
static void jpeg_tests(void) {
    GSSURFACE s = {0};
    for (unsigned mode = 0; mode < 2; mode++) {
        FILE *f = jpeg_fixture(1, 1, mode ? JCS_GRAYSCALE : JCS_RGB);
        assert(!athena_load_jpeg(&s, f, false, true));
        assert(s.Width == 1 && s.Height == 1 && s.PSM == GS_PSM_CT24);
        unsigned char *p = (unsigned char *)s.Mem;
        assert(p[0] == 90 && p[1] == 90 && p[2] == 90);
        for (unsigned i = 3; i < 16; i++) assert(p[i] == 0);
        release(&s);
    }
    assert(!athena_load_jpeg(&s, jpeg_fixture(1024, 2, JCS_RGB), false, true)); release(&s);
    assert(athena_load_jpeg(&s, jpeg_fixture(1025, 1, JCS_RGB), false, true) < 0);
    assert(!s.Mem && !s.Clut);
    assert(athena_load_jpeg(&s, jpeg_fixture(1, 1025, JCS_RGB), false, true) < 0);
    assert(!athena_load_jpeg(&s, jpeg_fixture(2049, 1, JCS_RGB), true, true));
    assert(s.Width <= 1024 && s.Height == 1); release(&s);
    assert(!athena_load_jpeg(&s, jpeg_fixture(1, 1, JCS_RGB), true, true)); release(&s);
    assert(athena_load_jpeg(&s, jpeg_fixture(8193, 1, JCS_RGB), true, true) < 0);
    assert(athena_load_jpeg(&s, jpeg_fixture(1, 1, JCS_CMYK), false, true) < 0);
    for (unsigned which = 0; which < 2; which++) {
        FILE *f = jpeg_fixture(2, 2, JCS_RGB);
        if (which) fail_aligned = 1; else fail_calloc = 1;
        assert(athena_load_jpeg(&s, f, false, true) < 0);
        assert(!s.Mem && !s.Clut);
    }
    fail_aligned = fail_calloc = 0;
    FILE *f = jpeg_fixture(2, 2, JCS_RGB);
    assert(!fseek(f, 0, SEEK_END)); long length = ftell(f); rewind(f);
    unsigned char bytes[4096]; assert(length > 2 && (size_t)length < sizeof(bytes));
    assert(fread(bytes, 1, length, f) == (size_t)length); fclose(f);
    AthenaImageLoadError error;
    const size_t lengths[] = {2, 32, (size_t)length / 2, (size_t)length - 2};
    for (unsigned i = 0; i < sizeof(lengths)/sizeof(lengths[0]); i++) {
        assert(load_image_memory_ex(&s, bytes, lengths[i], true, &error) < 0);
        assert(error == ATHENA_IMAGE_LOAD_DECODE && !s.Mem && !s.Clut);
    }
    /* Extreme SOF dimensions must be rejected before pixel allocation. */
    for (size_t i = 0; i + 8 < (size_t)length; i++) {
        if (bytes[i] == 0xff && bytes[i+1] == 0xc0) {
            bytes[i+5] = bytes[i+6] = bytes[i+7] = bytes[i+8] = 0xff;
            assert(load_image_memory_ex(&s, bytes, length, true, &error) < 0);
            assert(!s.Mem && !s.Clut); break;
        }
    }
}
int main(void) {
    png_tests(); bmp_tests(); jpeg_tests();
    puts("image_loaders: PNG/Adam7, BMP, JPEG colors/scaling/DMA padding, malformed inputs and OOM passed");
    return 0;
}
