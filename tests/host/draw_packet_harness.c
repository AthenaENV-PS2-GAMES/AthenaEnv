/* Production DMA allocator plus a VIF DIRECT/GIF parser, independent of the
 * writers' header sizes and batching logic. Only EE pack instructions are stubs. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "draw_packet_harness.h"
static owl_qword memory[8194];
static unsigned ring_size;
static GSCONTEXT context;
GSCONTEXT *gsGlobal=&context;
GSSURFACE *cur_screen_buffer[3];
AthenaAffine2D athena_view_matrix;
AthenaViewKind athena_view_current_kind;
uint32_t athena_view_culled;
uint64_t host_bus_ticks;
int test_bind_result;
bool test_upload_once;
unsigned test_upload_bind_calls;
unsigned test_packets,test_queries,test_marks,test_bind_calls,test_vertices;
static TestVertex on_vertex;
static owl_packet *last_packet;
static unsigned char *last_start;
static size_t last_size;
static uint64_t color,uv;
static unsigned prim;
void test_packet_checkpoint(void) {
    if(last_packet) assert((unsigned char *)last_packet->ptr==last_start+16*last_size);
    last_packet=NULL;
}
owl_packet *__real_owl_query_packet(owl_channel,size_t);
owl_packet *__wrap_owl_query_packet(owl_channel c,size_t n) {
    test_packet_checkpoint();
    assert(c==CHANNEL_VIF1 && n<owl_get_controller()->size);
    owl_packet *p=__real_owl_query_packet(c,n);
    last_packet=p; last_start=(unsigned char *)p->ptr; last_size=n; test_queries++;
    return p;
}
int graphics_surface_bind(GSSURFACE *s,bool async) {
    (void)s;(void)async; test_bind_calls++;
    int result=test_bind_result;
    if(result>=0) {test_upload_bind_calls++;if(test_upload_once)test_bind_result=-2;}
    return result;
}
int texture_manager_bind(GSCONTEXT *c,GSSURFACE *s,bool async) {
    (void)c; return graphics_surface_bind(s,async);
}
void athena_set_tw_th(const GSSURFACE *s,int *w,int *h) { (void)s; *w=*h=8; }
void flush_gs_texcache(void) {}
float athena_sinf(float x) { return sinf(x); }
float athena_cosf(float x) { return cosf(x); }
int GetInterlacedFrameMode(void) { return 0; }
uint32_t athena_test_dma_address(uintptr_t p) { (void)p; return 0; }
void SyncDCache(void *a,void *b) { (void)a;(void)b; }
void dmaKit_wait(owl_channel c,int n) { (void)c;(void)n; }
bool athena_view_screen_box_visible(float x0,float y0,float x1,float y1) {
    return x1>=0 && y1>=0 && x0<1200 && y0<900;
}
void athena_affine_bounds(const AthenaAffine2D *m,const AthenaRect2D *r,AthenaRect2D *out) {
    float x[4],y[4];
    athena_affine_apply(m,r->x0,r->y0,&x[0],&y[0]);
    athena_affine_apply(m,r->x1,r->y0,&x[1],&y[1]);
    athena_affine_apply(m,r->x0,r->y1,&x[2],&y[2]);
    athena_affine_apply(m,r->x1,r->y1,&x[3],&y[3]);
    *out=(AthenaRect2D){x[0],y[0],x[0],y[0]};
    for(int i=1;i<4;i++) {
        out->x0=fminf(out->x0,x[i]);out->x1=fmaxf(out->x1,x[i]);
        out->y0=fminf(out->y0,y[i]);out->y1=fmaxf(out->y1,y[i]);
    }
}
bool athena_view_culler_init(AthenaViewCuller *c) {
    if(athena_view_kind()==ATHENA_VIEW_IDENTITY) return false;
    *c=(AthenaViewCuller){.m=athena_view_matrix,.clip={0,0,1200,900},
        .rotated=athena_view_kind()==ATHENA_VIEW_ROTATED}; return true;
}
/* Binary-exact transforms avoid x87 excess-precision differences at a
 * 12.4 rounding boundary: these tests target packet serialization. */
void test_view(AthenaViewKind kind) {
    athena_view_current_kind=kind;
    athena_view_matrix=kind==ATHENA_VIEW_IDENTITY?(AthenaAffine2D){1,0,0,1,0,0}:
        kind==ATHENA_VIEW_AXIS?(AthenaAffine2D){1.5f,0,0,0.75f,20,30}:
        (AthenaAffine2D){0.75f,-0.5f,0.5f,0.75f,300,20};
}
static void reg(unsigned reg,uint64_t value) {
    if(reg==GS_PRIM) prim=value&7;
    else if(reg==GS_RGBAQ) color=value;
    else if(reg==GS_UV) uv=value;
    else if(reg==GS_XYZ2) { on_vertex(value,uv,color,prim);test_vertices++; }
}
static void gif(const unsigned char *data,size_t qwords) {
    size_t pos=0;
    while(pos<qwords) {
        uint64_t tag,regs; memcpy(&tag,data+pos*16,8);memcpy(&regs,data+pos*16+8,8);pos++;
        unsigned loops=tag&0x7fff,format=tag>>58&3,nreg=tag>>60;
        if(!nreg)nreg=16;
        if(tag>>46&1) prim=tag>>47&7;
        size_t values=(size_t)loops*nreg;
        if(format==0) {
            assert(pos+values<=qwords);
            for(size_t i=0;i<values;i++) {
                uint64_t value,address; memcpy(&value,data+(pos+i)*16,8);
                memcpy(&address,data+(pos+i)*16+8,8);
                unsigned r=regs>>(4*(i%nreg))&15;
                assert(r==GIF_AD || r==GIF_NOP);
                if(r==GIF_AD) reg(address&255,value);
            }
            pos+=values;
        } else {
            assert(format==1 && pos+(values+1)/2<=qwords);
            for(size_t i=0;i<values;i++) {
                uint64_t value;memcpy(&value,data+pos*16+i*8,8);
                reg(regs>>(4*(i%nreg))&15,value);
            }
            pos+=(values+1)/2;
        }
    }
    assert(pos==qwords);
}
void dmaKit_send_chain_ucab(owl_channel c,void *base) {
    assert(c==CHANNEL_VIF1);
    owl_qword *p=base; size_t total=0;
    while((p->dword[0]>>28&7)!=DMA_END) {
        assert((p->dword[0]>>28&7)==DMA_CNT);
        size_t n=p->dword[0]&0xffff;
        assert(total+n+1<owl_get_controller()->size);
        const unsigned char *body=(const unsigned char *)(p+1);
        for(size_t i=0;i<n*4;) {
            uint32_t code;memcpy(&code,body+i*4,4);i++;
            unsigned cmd=code>>24&127;
            if(cmd==VIF_DIRECT) {
                size_t q=code&0xffff;assert(!(i%4)&&i+q*4<=n*4);
                gif(body+i*4,q);i+=q*4;
            } else if(cmd==VIF_MARK) test_marks++;
            else assert(cmd==VIF_NOP || cmd==VIF_FLUSH || cmd==VIF_FLUSHA);
        }
        total+=n+1;p+=n+1;test_packets++;
    }
    assert(total==owl_get_controller()->alloc);
}
void test_packet_begin(unsigned ring,TestVertex vertex,bool prefill) {
    assert(ring<=8192); memset(memory,0xa5,sizeof(memory));
    last_packet=NULL;owl_init(memory+1,ring);ring_size=ring;on_vertex=vertex;
    test_packets=test_queries=test_marks=test_bind_calls=test_vertices=test_upload_bind_calls=0;
    athena_view_culled=0;color=uv=prim=0;
    context=(GSCONTEXT){.Width=1200,.Height=900,.OffsetX=32768,.OffsetY=32768};
    if(prefill) {
        owl_packet *p=owl_query_packet(CHANNEL_VIF1,ring/2-20);
        for(unsigned i=0;i<ring/2-20;i++) owl_add_cnt_tag(p,0,0);
        test_packet_checkpoint();
        test_queries=0;
    }
}
void test_packet_end(void) {
    test_packet_checkpoint();owl_flush_packet();
    for(unsigned i=0;i<4;i++) {
        assert(memory[0].sword[i]==0xa5a5a5a5);
        assert(memory[ring_size+1].sword[i]==0xa5a5a5a5);
    }
}
