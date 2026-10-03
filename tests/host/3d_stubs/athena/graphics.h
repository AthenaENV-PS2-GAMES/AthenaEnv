#ifndef ATHENA_PACKET_TEST_GRAPHICS_H
#define ATHENA_PACKET_TEST_GRAPHICS_H
#include <stdint.h>
typedef struct { int ZBuffering,Height,Width,Interlace,Field,PSMZ,PrimContext; } GSCONTEXT;
#define GS_INTERLACED 1
#define GS_FRAME 1
#define GS_ZBUF_16 2
#define GS_ZBUF_16S 10
#define GS_ZBUF_24 1
#define GS_PRIM_PRIM_TRIANGLE 3
typedef union { struct { uint64_t PRIM:3,IIP:1,TME:1,FGE:1,ABE:1,AA1:1,FST:1,CTXT:1,FIX:1; }; uint64_t data; } prim_reg_t;
typedef union { struct { uint64_t NLOOP:15,EOP:1,pad:30,PRE:1,PRIM:11,FLG:2,NREG:4; }; uint64_t data; } giftag_t;
void graphics_service_init(void);
GSCONTEXT *getGSGLOBAL(void);
#define GS_CACHE_TEST 26
#define GS_CACHE_ZBUF 33
#define DEPTH_GEQUAL 2
#define GS_SETREG_TEST(a,b,c,d,e,f,g,h) ((uint64_t)(g)<<16|((uint64_t)(h)<<17))
uint64_t get_register(int);
void set_register(int,uint64_t);
#endif
