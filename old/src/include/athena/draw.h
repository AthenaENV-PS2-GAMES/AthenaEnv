#ifndef ATHENA_DRAW_H
#define ATHENA_DRAW_H

#include <stdbool.h>

#include <graphics.h>

void athena_draw_point(float x, float y, Color color);
void athena_draw_line(float x1, float y1, float x2, float y2, Color color);
void athena_draw_triangle(float x1, float y1, float x2, float y2, float x3, float y3, Color color);
void athena_draw_triangle_gouraud(float x1, float y1, float x2, float y2, float x3, float y3,
                                  Color color1, Color color2, Color color3);
void athena_draw_quad(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4,
                      Color color);
void athena_draw_quad_gouraud(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4,
                              Color color1, Color color2, Color color3, Color color4);
void athena_draw_rect(float x, float y, float w, float h, Color color);
void athena_draw_circle(float x, float y, float r, Color color, bool filled);

#endif /* ATHENA_DRAW_H */
