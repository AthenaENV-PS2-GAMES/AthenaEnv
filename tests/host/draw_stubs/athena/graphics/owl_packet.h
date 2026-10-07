#ifndef ATHENA_DRAW_TEST_PACKET_H
#define ATHENA_DRAW_TEST_PACKET_H
#include <athena/graphics.h>
#include "../../../font_stubs/athena/graphics/owl_packet.h"
#define owl_add_cnt_tag_fill(p,n) owl_add_cnt_tag(p,n,0)
static inline void owl_add_direct(owl_packet *p,int n) {
    owl_add_uint(p,0); owl_add_uint(p,0); owl_add_uint(p,0); owl_add_uint(p,VIF_CODE(n,0,VIF_DIRECT,0));
}
static inline uint64_t test_xy(int x,int y) {
    x=(int16_t)x+2048; y=(int16_t)y+2048;
    if(x<0)x=0;
    if(x>4095)x=4095;
    if(y<0)y=0;
    if(y>4095)y=4095;
    return (uint64_t)(x*16)|((uint64_t)(y*16)<<16);
}
static inline void owl_add_xy(owl_packet *p,int x,int y) { owl_add_ulong(p,test_xy(x,y)); }
static inline void owl_add_xy_2x(owl_packet *p,int x,int y,int a,int b) {
    owl_add_ulong(p,test_xy(x,y)); owl_add_ulong(p,test_xy(a,b));
}
static inline void owl_add_rgba_xy(owl_packet *p,uint64_t color,int x,int y) {
    owl_add_tag(p,test_xy(x,y),color);
}
static inline void owl_add_uv(owl_packet *p,int u,int v) {
    owl_add_ulong(p,GS_SETREG_UV(u*16,v*16));
}
static inline void owl_add_xy_uv(owl_packet *p,int x,int y,int u,int v) {
    owl_add_tag(p,test_xy(x,y),GS_SETREG_UV(u*16,v*16));
}
static inline void owl_add_xy_uv_2x(owl_packet *p,int x,int y,int u,int v,
    int a,int b,int c,int d) {
    owl_add_xy_uv(p,x,y,u,v); owl_add_xy_uv(p,a,b,c,d);
}
static inline void owl_align_packet(owl_packet *p) {
    uintptr_t end=((uintptr_t)p->ptr+15)&~(uintptr_t)15;
    memset(p->ptr,0,end-(uintptr_t)p->ptr); p->ptr=(owl_qword *)end;
}
#endif
