#ifndef ATHENA_SCREEN_LINES_H
#define ATHENA_SCREEN_LINES_H
#include <stdint.h>
/* Portable producer/GS-consumer format: projected pixels and GS ABGR color.
 * The packet writer copies these values; never retains a DMA reference. */
typedef struct { float x0,y0,x1,y1; uint32_t color; } AthenaScreenLine;
#endif
