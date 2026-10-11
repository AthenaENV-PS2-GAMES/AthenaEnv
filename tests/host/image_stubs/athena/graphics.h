#ifndef ATHENA_IMAGE_LOADER_TEST_GRAPHICS_H
#define ATHENA_IMAGE_LOADER_TEST_GRAPHICS_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef struct {
    uint32_t Width, Height, PSM, Filter, VramClut, ClutPSM, ClutStorageMode, TBW;
    uint32_t *Mem, *Clut;
    bool Delayed, PageAligned, Macroblock;
} GSSURFACE;
#define GS_PSM_CT32 0
#define GS_PSM_CT24 1
#define GS_PSM_CT16 2
#define GS_PSM_T8 19
#define GS_PSM_T4 20
#define GS_FILTER_NEAREST 0
#define GS_CLUT_STORAGE_CSM1 0
typedef enum {
    ATHENA_IMAGE_LOAD_OK, ATHENA_IMAGE_LOAD_OPEN, ATHENA_IMAGE_LOAD_DECODE,
    ATHENA_IMAGE_LOAD_FORMAT, ATHENA_IMAGE_LOAD_SURFACE
} AthenaImageLoadError;
uint32_t athena_surface_size(int, int, int);
void athena_calculate_tbw(GSSURFACE *);
int load_image_memory_ex(GSSURFACE *, const void *, size_t, bool, AthenaImageLoadError *);
int athena_load_png(GSSURFACE *, FILE *, bool);
int athena_load_bmp(GSSURFACE *, FILE *, bool);
int athena_load_jpeg(GSSURFACE *, FILE *, bool, bool);
#endif
