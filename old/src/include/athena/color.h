#ifndef ATHENA_COLOR_H
#define ATHENA_COLOR_H

#include <stdint.h>

#include <graphics.h>

uint32_t athena_color_rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a);
uint32_t athena_color_get_r(Color color);
uint32_t athena_color_get_g(Color color);
uint32_t athena_color_get_b(Color color);
uint32_t athena_color_get_a(Color color);
uint32_t athena_color_set_r(Color color, uint32_t r);
uint32_t athena_color_set_g(Color color, uint32_t g);
uint32_t athena_color_set_b(Color color, uint32_t b);
uint32_t athena_color_set_a(Color color, uint32_t a);

#endif /* ATHENA_COLOR_H */
