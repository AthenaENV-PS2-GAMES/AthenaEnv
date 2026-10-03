#ifndef MPG_MANAGER_H
#define MPG_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

/*
 * VU microprogram manager. Programs are uploaded on demand into VU code
 * memory and kept in a small per-unit cache, so renderers that share a
 * vector unit do not re-upload each other's code every draw.
 */

typedef enum {
    VECTOR_UNIT_0,
    VECTOR_UNIT_1,

    VECTOR_UNIT_SIZE
} eVectorUnits;

#define MPG_CACHE_SIZE 16

#define VU0_QWC 256
#define VU1_QWC 1024

#define VU0_SIZE 4096
#define VU1_SIZE 16384

#define VU0_CODE 0x11000000 //  4 KB
#define VU0_DATA 0x11004000 //  4 KB
#define VU1_CODE 0x11008000 //  16 KB
#define VU1_DATA 0x1100C000 //  16 KB

typedef struct {
    void *code;

    uint32_t qwc;
    uint32_t size;
    bool free;

    int dst;
} vu_mpg;

#define vu_mpg_count_instr(size) (((size) / 2) + (((size) / 2) & 1))

vu_mpg *vu_mpg_load_buffer(void *ptr, uint32_t size, int dst, bool autofree);
vu_mpg *vu_mpg_load_file(const char *path, int dst);

void vu_mpg_unload(vu_mpg *mpg);
/* Makes `mpg` resident and returns its VU code address in doublewords. */
int vu_mpg_preload(vu_mpg *mpg, bool dma_transfer);

/*
 * Renderers may cache per-draw constants in VU1 data memory across draws.
 * Code that overwrites that memory calls vu1_invalidate_static_data(); a
 * renderer compares vu1_static_data_version() with the version it uploaded
 * and re-uploads its constants when they differ.
 */
void vu1_invalidate_static_data(void);
uint32_t vu1_static_data_version(void);

#endif
