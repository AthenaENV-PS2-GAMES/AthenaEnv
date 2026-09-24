#ifndef ATHENA_COLOR_H
#define ATHENA_COLOR_H

#include <stdint.h>

/*
 * Packed 0xAABBGGRR colors, as used by <athena/graphics.h> (Color, R(), G(),
 * B(), A()). Alpha 0x80 is the GS "opaque" default.
 */

#define ATHENA_COLOR_DEFAULT_ALPHA 0x80

static inline uint32_t athena_color_new(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    return (r & 0xff) | ((g & 0xff) << 8) | ((b & 0xff) << 16) | ((a & 0xff) << 24);
}

/* component: 0 = red, 1 = green, 2 = blue, 3 = alpha. */
static inline uint32_t athena_color_get(uint32_t color, unsigned int component) {
    return (color >> ((component & 3) * 8)) & 0xff;
}

static inline uint32_t athena_color_set(uint32_t color, unsigned int component, uint32_t value) {
    unsigned int shift = (component & 3) * 8;
    return (color & ~(0xffU << shift)) | ((value & 0xff) << shift);
}

#endif /* ATHENA_COLOR_H */
