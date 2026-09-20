#include <athena/draw.h>

void athena_draw_point(float x, float y, Color color) {
    draw_point(x, y, color);
}

void athena_draw_line(float x1, float y1, float x2, float y2, Color color) {
    draw_line(x1, y1, x2, y2, color);
}

void athena_draw_triangle(float x1, float y1, float x2, float y2, float x3, float y3, Color color) {
    draw_triangle(x1, y1, x2, y2, x3, y3, color);
}

void athena_draw_triangle_gouraud(float x1, float y1, float x2, float y2, float x3, float y3,
                                  Color color1, Color color2, Color color3) {
    draw_triangle_gouraud(x1, y1, x2, y2, x3, y3, color1, color2, color3);
}

void athena_draw_quad(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4,
                      Color color) {
    draw_quad(x1, y1, x2, y2, x3, y3, x4, y4, color);
}

void athena_draw_quad_gouraud(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4,
                              Color color1, Color color2, Color color3, Color color4) {
    draw_quad_gouraud(x1, y1, x2, y2, x3, y3, x4, y4, color1, color2, color3, color4);
}

void athena_draw_rect(float x, float y, float w, float h, Color color) {
    draw_sprite(x, y, w, h, color);
}

void athena_draw_circle(float x, float y, float r, Color color, bool filled) {
    draw_circle(x, y, r, color, filled);
}
