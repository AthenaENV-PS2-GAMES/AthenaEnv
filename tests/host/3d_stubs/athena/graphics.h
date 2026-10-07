#ifndef ATHENA_PACKET_TEST_GRAPHICS_H
#define ATHENA_PACKET_TEST_GRAPHICS_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef struct { int ZBuffering,Height,Width,Interlace,Field,PSMZ,PrimContext; } GSCONTEXT;
#define GS_INTERLACED 1
#define GS_FRAME 1
#define GS_ZBUF_16 2
#define GS_ZBUF_16S 10
#define GS_ZBUF_24 1
#define GS_ZBUF_32 0
#define GS_ST 0x02
#define GS_RGBAQ 0x01
#define VIF_DIRECT 80
#define GIF_AD 0x0e
#define VU_GS_GIFTAG(NLOOP,EOP,DATA,PRE,PRIM,FLG,NREG) (((uint64_t)(NREG)<<60)|((uint64_t)(FLG)<<58)|((uint64_t)(PRIM)<<47)|((uint64_t)(PRE)<<46)|((uint64_t)(EOP)<<15)|((uint64_t)(NLOOP)))
#define GS_PRIM_PRIM_TRIANGLE 3
typedef union { struct { uint64_t PRIM:3,IIP:1,TME:1,FGE:1,ABE:1,AA1:1,FST:1,CTXT:1,FIX:1; }; uint64_t data; } prim_reg_t;
typedef union { struct { uint64_t NLOOP:15,EOP:1,pad:30,PRE:1,PRIM:11,FLG:2,NREG:4; }; uint64_t data; } giftag_t;
void graphics_service_init(void);
GSCONTEXT *getGSGLOBAL(void);
#define GS_CACHE_TEST 26
#define GS_CACHE_TEX0 0
#define GS_CACHE_TEX1 2
#define GS_CACHE_CLAMP 4
#define GS_CACHE_ZBUF 33
#define DEPTH_GEQUAL 2
#define GS_SETREG_TEST(a,b,c,d,e,f,g,h) ((uint64_t)(g)<<16|((uint64_t)(h)<<17))
uint64_t get_register(int);
void set_register(int,uint64_t);
void set_register_force(int,uint64_t);
typedef struct { uint32_t Width,Height,PSM,Filter,Vram,TBW; uint32_t *Mem; bool locked; } GSSURFACE;
#define GS_PSM_CT32 0
#define GS_PSM_CT24 1
#define GS_FILTER_NEAREST 0
#define GS_FILTER_LINEAR 1
#define GRAPHICS_BIND_ERROR (-1)
#define COLOR_MODULATE 0
#define GS_SETREG_TEX0(v,b,p,w,h,c,f,a,d,e,g,k) ((uint64_t)(v)|((uint64_t)(b)<<14)|((uint64_t)(p)<<20)|((uint64_t)(w)<<26)|((uint64_t)(h)<<30)|((uint64_t)(c)<<34))
#define GS_SETREG_TEX1(l,m,mag,min,t,k,b) ((uint64_t)(mag)<<5|((uint64_t)(min)<<6))
#define GS_SETREG_CLAMP(s,t,a,b,c,d) ((uint64_t)(s)|((uint64_t)(t)<<2))
int graphics_surface_init(GSSURFACE *);
void graphics_surface_release(GSSURFACE *);
int graphics_surface_bind_sync(GSSURFACE *);
int graphics_surface_lock(GSSURFACE *);
bool graphics_surface_is_locked(const GSSURFACE *);
void athena_set_tw_th(const GSSURFACE *,int *,int *);
void graphics_wait_idle(void);
void set_finish(void);
int texture_test_csr(void);
void texture_test_clear_csr(void);
#define GS_CSR_FINISH texture_test_csr()
#define GS_SETREG_CSR_FINISH(v) texture_test_clear_csr()
#endif
