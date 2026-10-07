/* Compile the production texture backend and FINISH serializer with a GS/DMA
 * stand-in. This checks ordering and ownership, not actual rasterization. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <athena/image.h>
#include <athena/model3d.h>
#include <athena/graphics/owl_packet.h>
#include <athena/graphics/sync.h>
#include "../../src/modules/model3d/native/texture3d_backend.h"
static unsigned fail_memalign_at;
void *__real_memalign(size_t,size_t);
void *__wrap_memalign(size_t alignment,size_t size) {
    if(fail_memalign_at&&!--fail_memalign_at) return NULL;
    return __real_memalign(alignment,size);
}
static GSCONTEXT gs;
static GSSURFACE *resident;
static unsigned queued,completed,flag,waits,uploads,releases;
static int fail_upload,decode_psm=GS_PSM_CT32,decode_clut;
static owl_qword storage[16]; static owl_packet packet;
GSCONTEXT *getGSGLOBAL(void) { return &gs; }
owl_packet *owl_query_packet(owl_channel c,size_t n) { assert(c==CHANNEL_VIF1&&n==1); packet.ptr=storage; return &packet; }
uint64_t owl_flush_generation(void) { return queued; }
void owl_wait_generation(uint64_t g) { assert(g==queued); completed=queued; flag=1; waits++; }
int texture_test_csr(void) { return flag; }
void texture_test_clear_csr(void) { assert(completed==queued); flag=0; }
void set_finish(void) { graphics_finish_begin(); queued++; }
/* Exact GS allocations for the fixture dimensions (not the encoder logic). */
uint32_t athena_vram_surface_size(int w,int h,int psm) {
    if(w==1&&h==1) return 256;
    if(w==8&&h==2&&psm==GS_PSM_CT32) return 256;
    if(w==16&&h==16&&psm==GS_PSM_CT32) return 1024;
    assert(w==64&&h==64);
    return psm==GS_PSM_CT32?16384:psm==GS_PSM_T8?4096:2048;
}
int graphics_surface_init(GSSURFACE *s) { memset(s,0,sizeof(*s)); return 0; }
int graphics_surface_bind_sync(GSSURFACE *s) {
    assert(completed==queued&&!flag);
    if(s->Width==1) assert(s->Mem[0]==0x800000ff);
    /* 1x1 upload still reads a complete aligned QW; padding must be live. */
    assert(!((uintptr_t)s->Mem&127));
    if(s->Width==1) assert(s->Mem[1]==0&&s->Mem[3]==0);
    if(s->Clut) assert(!((uintptr_t)s->Clut&127)&&s->ClutPSM==GS_PSM_CT32);
    if(fail_upload) return GRAPHICS_BIND_ERROR;
    s->Vram=8192; s->VramClut=s->Clut?8192+athena_vram_surface_size(s->Width,s->Height,s->PSM):0; s->TBW=1; resident=s; uploads++; return 0;
}
int graphics_surface_lock(GSSURFACE *s) { s->locked=true; return 1; }
bool graphics_surface_is_locked(const GSSURFACE *s) { return s->locked; }
void graphics_surface_release(GSSURFACE *s) {
    assert(completed==queued&&!flag);
    if(s->Width==1) assert(s->Mem[0]==0x800000ff);
    releases++; if(resident==s) resident=NULL;
}
void athena_set_tw_th(const GSSURFACE *s,int *w,int *h) { (void)s; *w=0; *h=0; }
int athena_image_decode(const char *path,AthenaImageBuffer *b) {
    memset(b,0,sizeof(*b)); if(!strcmp(path,"missing")) { b->error=ATHENA_IMAGE_LOAD_OPEN; return -1; }
    b->width=b->height=1; b->psm=decode_psm; b->mem=calloc(4,4);
    b->mem[0]=0x800000ff; /* loaders store GS alpha: 0x80 is opaque */
    if(decode_clut) {
        /* CSM1 order: logical entries 8..15 live in slots 16..23. */
        b->clut=calloc(256,4); b->clut_psm=GS_PSM_CT32;
        ((unsigned char *)b->mem)[0]=decode_psm==GS_PSM_T8?8:0xf5;
        b->clut[16]=0x80123456; b->clut[5]=0x80abcdef; b->clut[8]=0x80ffffff;
    }
    return 0;
}
int athena_image_decode_memory(const void *d,size_t n,AthenaImageBuffer *b) {
    (void)d; (void)n; memset(b,0,sizeof(*b)); return -1;
}
void athena_image_buffer_release(AthenaImageBuffer *b) { free(b->mem); free(b->clut); b->mem=NULL; b->clut=NULL; }
static void indexed_test(unsigned colors,int expected_psm) {
    uint32_t pixels[4096];
    /* Distinct RGB plus varied coverage, including transparent/opaque colors. */
    for(unsigned i=0;i<4096;i++) {
        unsigned c=i%colors; pixels[i]=(colors==2?0xabcdefu:c)|((c%3==0?0u:c%3==1?128u:255u)<<24);
    }
    AthenaTexture3DPixels input={.width=64,.height=64,.pixels=pixels,.pixel_count=4096};
    AthenaTexture3D *t=NULL; AthenaTexture3DBinding binding; AthenaTexture3DPixels view;
    assert(!athena_texture3d_create(&input,&t)&&!athena_texture3d_bind(t,&binding));
    athena_texture3d_view(t,&view);
    assert(resident->PSM==(unsigned)expected_psm&&((binding.tex0>>20)&63)==(unsigned)expected_psm);
    assert(((binding.tex0>>61)&7)==(expected_psm==GS_PSM_CT32?0:1));
    assert(((binding.tex0>>37)&16383)==resident->VramClut/256);
    uint8_t *indices=(uint8_t *)resident->Mem;
    for(unsigned i=0;i<4096;i++) {
        uint32_t actual;
        if(expected_psm==GS_PSM_CT32) actual=resident->Mem[i];
        else {
            unsigned idx=expected_psm==GS_PSM_T8?indices[i]:(indices[i/2]>>((i&1)*4))&15;
            assert(idx==i%colors);
            unsigned slot=expected_psm==GS_PSM_T8?(idx&0xe7)|((idx&8)<<1)|((idx&16)>>1):idx;
            actual=resident->Clut[slot];
        }
        assert(actual==view.pixels[i]);
        assert((actual&0xffffff)==(pixels[i]&0xffffff));
    }
    if(expected_psm==GS_PSM_T4) for(unsigned i=16;i<64;i++) assert(!resident->Clut[i]);
    /* Re-upload retains both streams, then deferred release retains the CLUT. */
    uint32_t *mem=resident->Mem,*clut=resident->Clut;
    graphics_wait_idle(); resident->locked=false; resident->Vram=resident->VramClut=0;
    assert(!athena_texture3d_bind(t,&binding)&&resident->Mem==mem&&resident->Clut==clut);
    set_finish(); athena_texture3d_release(t); assert(resident&&resident->Mem==mem);
    if(clut) assert(resident->Clut[0]==view.pixels[0]);
    graphics_wait_idle(); athena_texture3d_collect(0); assert(!resident);
}
static void indexed_oom_test(unsigned allocation) {
    uint32_t pixels[4096]; for(unsigned i=0;i<4096;i++) pixels[i]=0xff0000ff;
    AthenaTexture3DPixels input={.width=64,.height=64,.pixels=pixels,.pixel_count=4096};
    AthenaTexture3D *t=NULL; AthenaTexture3DBinding binding;
    assert(!athena_texture3d_create(&input,&t));
    fail_memalign_at=allocation;
    assert(!athena_texture3d_bind(t,&binding)&&!fail_memalign_at);
    assert(resident->PSM==GS_PSM_CT32&&!resident->Clut&&resident->Mem[0]==0x800000ff);
    athena_texture3d_release(t); graphics_wait_idle(); athena_texture3d_collect(0);
}
int main(void) {
    set_finish(); assert(queued==1&&completed==0);
    /* A fresh FINISH must consume the old one before clearing CSR. */
    set_finish(); assert(completed==1&&queued==2&&!flag);
    graphics_finish_wait(); assert(completed==2&&!flag);
    unsigned old_waits=waits; graphics_finish_wait(); assert(waits==old_waits);
    uint32_t pixel=0xff0000ff; AthenaTexture3DPixels p={.width=1,.height=1,.pixels=&pixel,.pixel_count=1};
    AthenaTexture3D *texture=NULL; AthenaTexture3DBinding binding;
    assert(!athena_texture3d_create(&p,&texture)); assert(!athena_texture3d_bind(texture,&binding));
    assert(uploads==1&&resident&&resident->locked&&!(binding.tex0&(1ull<<34)));
    old_waits=waits; assert(!athena_texture3d_bind(texture,&binding)); assert(waits==old_waits&&uploads==1);
    /* Retained CPU pixels permit re-upload after the texture manager reset. */
    graphics_wait_idle(); resident->locked=false; resident->Vram=0;
    assert(!athena_texture3d_bind(texture,&binding)); assert(uploads==2);
    /* Release is deferred, never a GS wait in the caller (finalizers run at
     * arbitrary times): VRAM and pixels stay until two flips that waited
     * for FINISH. */
    set_finish(); old_waits=waits; athena_texture3d_release(texture);
    assert(waits==old_waits&&releases==0&&resident);
    for(int flip=0;flip<2;flip++) { set_finish(); graphics_finish_wait(); graphics_frame_finished(); }
    athena_texture3d_collect(0); assert(releases==1&&!resident);
    /* graphics_wait_idle() also proves it idle; collect(1) waits itself. */
    assert(!athena_texture3d_create(&p,&texture)&&!athena_texture3d_bind(texture,&binding));
    athena_texture3d_release(texture); assert(releases==1);
    graphics_wait_idle(); athena_texture3d_collect(0); assert(releases==2);
    assert(!athena_texture3d_create(&p,&texture)&&!athena_texture3d_bind(texture,&binding));
    athena_texture3d_release(texture); assert(releases==2);
    athena_model3d_module_shutdown(); assert(releases==3&&completed==queued);
    fail_upload=1; assert(!athena_texture3d_create(&p,&texture));
    assert(athena_texture3d_bind(texture,&binding)==ATHENA_MODEL3D_EVRAM); athena_texture3d_release(texture);
    fail_upload=0;
    decode_psm=GS_PSM_CT24; assert(!athena_texture3d_load("rgb",0,&texture));
    athena_texture3d_view(texture,&p); assert(p.pixels[0]==0x800000ff); athena_texture3d_release(texture);
    decode_psm=19; assert(athena_texture3d_load("palette",0,&texture)==ATHENA_MODEL3D_EUNSUPPORTED&&!texture);
    assert(athena_texture3d_load("missing",0,&texture)==ATHENA_MODEL3D_EIO&&!texture);
    /* Paletted and 16-bit images expand to 32-bit pixels. */
    decode_clut=1;
    decode_psm=GS_PSM_T8; assert(!athena_texture3d_load("t8",0,&texture));
    athena_texture3d_view(texture,&p); assert(p.pixels[0]==0x80123456); athena_texture3d_release(texture);
    decode_psm=GS_PSM_T4; assert(!athena_texture3d_load("t4",0,&texture)); /* low nibble first */
    athena_texture3d_view(texture,&p); assert(p.pixels[0]==0x80abcdef); athena_texture3d_release(texture);
    decode_clut=0;
    decode_psm=GS_PSM_CT16; assert(!athena_texture3d_load("ct16",0,&texture)); /* 0x00ff: r 31, g 7 */
    athena_texture3d_view(texture,&p); assert(p.pixels[0]==0x800039ff); athena_texture3d_release(texture);
    /* Addressing: WMS/WMT 1 clamp, 0 repeat, per axis. */
    decode_psm=GS_PSM_CT32;
    const AthenaTexture3DWrap wraps[]={ATHENA_TEXTURE3D_CLAMP,ATHENA_TEXTURE3D_REPEAT,ATHENA_TEXTURE3D_REPEAT_U,ATHENA_TEXTURE3D_REPEAT_V};
    const uint64_t clamps[]={5,0,4,1};
    for(int i=0;i<4;i++) {
        assert(!athena_texture3d_load_ex("rgb",0,wraps[i],&texture));
        assert(!athena_texture3d_bind(texture,&binding)&&binding.clamp==clamps[i]);
        set_finish(); athena_texture3d_release(texture);
    }
    assert(athena_texture3d_load_ex("rgb",0,(AthenaTexture3DWrap)4,&texture)==ATHENA_MODEL3D_EINVAL&&!texture);
    athena_model3d_module_shutdown();
    indexed_test(1,GS_PSM_T4); indexed_test(2,GS_PSM_T4); indexed_test(16,GS_PSM_T4);
    indexed_test(17,GS_PSM_T8); indexed_test(256,GS_PSM_T8);
    indexed_test(257,GS_PSM_CT32);
    indexed_oom_test(1); indexed_oom_test(2);
    puts("3D GS texture upload, FINISH ordering, reset and release tests passed"); return 0;
}
