#ifndef ATHENA_DRAW_TEST_GRAPHICS_H
#define ATHENA_DRAW_TEST_GRAPHICS_H
#include "../../font_stubs/athena/graphics.h"
typedef struct {
	float x;
	float y;

	Color rgba;
} prim_point;

typedef struct {
	float x;
	float y;

	float x2;
	float y2;

	Color rgba;
} prim_line;

typedef struct {
	float x;
	float y;
    Color rgba;

	float x2;
	float y2;
    Color rgba2;
} prim_gouraud_line;

typedef struct {
	float x;
	float y;

	float x2;
	float y2;

	float x3;
	float y3;

	Color rgba;
} prim_triangle;

typedef struct {
	float x;
	float y;
    Color rgba;

	float x2;
	float y2;
    Color rgba2;

	float x3;
	float y3;
	Color rgba3;
} prim_gouraud_triangle;

typedef struct {
	float x;
	float y;
	float u;
	float v;

	float x2;
	float y2;
	float u2;
	float v2;

	float x3;
	float y3;
	float u3;
	float v3;

	Color rgba;
} prim_tex_triangle;

typedef struct {
	float x;
	float y;
	float u;
	float v;
    Color rgba;

	float x2;
	float y2;
	float u2;
	float v2;
    Color rgba2;

	float x3;
	float y3;
	float u3;
	float v3;
	Color rgba3;
} prim_tex_gouraud_triangle;

typedef struct {
	float x;
	float y;
	float u1;
	float v1;

	float w;
	float h;
	float u2;
	float v2;

	Color rgba;
} prim_tex_sprite;

void draw_point_list(float x, float y, prim_point *list, int list_size);

void draw_line_list(float x, float y, prim_line *list, int list_size);

void draw_line_gouraud_list(float x, float y, prim_gouraud_line *list, int list_size);

void draw_triangle_list(float x, float y, prim_triangle *list, int list_size);

void draw_triangle_gouraud_list(float x, float y, prim_gouraud_triangle *list, int list_size);

void draw_tex_triangle_list(GSSURFACE* source, float x, float y, prim_tex_triangle *list, int list_size);

void draw_tex_triangle_gouraud_list(GSSURFACE* source, float x, float y, prim_tex_gouraud_triangle *list, int list_size);

void draw_image_list(GSSURFACE* source, float x, float y, prim_tex_sprite *list, int list_size);



#define GS_PRIM_PRIM_POINT 0
#define GS_PRIM_PRIM_LINE 1
#define GS_PRIM_PRIM_LINESTRIP 2
#define GS_PRIM_PRIM_TRISTRIP 4
#define GS_PRIM_PRIM_TRIFAN 5
#define GS_SETREG_STQ(s,t) ((uint64_t)(s)|((uint64_t)(t)<<32))
#define GS_SETREG_RGBA(r,g,b,a) ((uint64_t)(r)|((uint64_t)(g)<<8)|((uint64_t)(b)<<16)|((uint64_t)(a)<<24))
#define GS_MODE_DTV_720P 0x52
#define GS_MODE_DTV_1080I 0x53
typedef GSSURFACE GSCLUT;
extern GSSURFACE *cur_screen_buffer[3];
int graphics_surface_bind(GSSURFACE *,bool);
int texture_manager_bind(GSCONTEXT *,GSSURFACE *,bool);
void flush_gs_texcache(void);
#define qw_aligned __attribute__((aligned(64)))
#define ftoi4(type,val) ((type)((val)*16))
#endif
