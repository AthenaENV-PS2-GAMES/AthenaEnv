#ifndef ATHENA_BITMAP_FONT_TEST_GRAPHICS_H
#define ATHENA_BITMAP_FONT_TEST_GRAPHICS_H
#include <tamtypes.h>
#include "../../3d_stubs/athena/graphics.h"
#include <athena/graphics/view.h>
typedef uint64_t Color;
typedef enum { FONT_TYPE_FNT, FONT_TYPE_BMP_DAT, FONT_TYPE_PNG_DAT, FONT_TYPE_JPEG_DAT } eTextureFontTypes;
typedef struct {
    char *Path, *Path_DAT;
    u8 Type, *RawData;
    int RawSize;
    GSSURFACE *Texture;
    u32 CharWidth, CharHeight, HChars, VChars;
    short *Additional;
    int pgcount;
} GSFONT;
typedef struct { float x1,y1,x2,y2,u1,v1,u2,v2; } prim_tex_rect;
extern GSCONTEXT *gsGlobal;
#define GRAPHICS_TRANSFER_REQUEST_MASK 0x80000000u
#define GS_CLUT_STOREMODE_LOAD 1
#define GS_CLUT_STOREMODE_NOLOAD 0
#define GS_PRIM 0
#define GS_TEX0_1 6
#define GS_TEX1_1 20
#define GS_UV 3
#define GS_XYZ2 5
#define GS_PRIM_PRIM_SPRITE 6
#define GIF_NOP 15
#define NO_CUSTOM_DATA 0
#define GIFTAG(n,e,p,prim,f,r) VU_GS_GIFTAG(n,e,0,p,prim,f,r)
#define VU_GS_PRIM(p,i,t,f,a,aa,s,c,x) ((p)|((i)<<3)|((t)<<4)|((f)<<5)|((a)<<6)|((aa)<<7)|((s)<<8)|((c)<<9)|((x)<<10))
#define GS_SETREG_UV(u,v) ((uint64_t)(u)|((uint64_t)(v)<<16))
int GetInterlacedFrameMode(void);
int load_image(GSSURFACE *,const char *,bool);
void unloadFont(GSFONT *);
void draw_tex_rect_list(GSSURFACE *,const prim_tex_rect *,int,Color);
void athena_font_print_scaled(GSCONTEXT *,GSFONT *,float,float,int,float,unsigned long,const char *);
#endif
