#include <athena/color.h>

uint32_t athena_color_rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    return r | (g << 8) | (b << 16) | (a << 24);
}

uint32_t athena_color_get_r(Color color) {
    return R(color);
}

uint32_t athena_color_get_g(Color color) {
    return G(color);
}

uint32_t athena_color_get_b(Color color) {
    return B(color);
}

uint32_t athena_color_get_a(Color color) {
    return A(color);
}

uint32_t athena_color_set_r(Color color, uint32_t r) {
    return r | (G(color) << 8) | (B(color) << 16) | (A(color) << 24);
}

uint32_t athena_color_set_g(Color color, uint32_t g) {
    return R(color) | (g << 8) | (B(color) << 16) | (A(color) << 24);
}

uint32_t athena_color_set_b(Color color, uint32_t b) {
    return R(color) | (G(color) << 8) | (b << 16) | (A(color) << 24);
}

uint32_t athena_color_set_a(Color color, uint32_t a) {
    return R(color) | (G(color) << 8) | (B(color) << 16) | (a << 24);
}
