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
static GSCONTEXT gs;
static GSSURFACE *resident;
static unsigned queued,completed,flag,waits,uploads,releases;
static int fail_upload,decode_psm=GS_PSM_CT32;
static owl_qword storage[16]; static owl_packet packet;
GSCONTEXT *getGSGLOBAL(void) { return &gs; }
owl_packet *owl_query_packet(owl_channel c,size_t n) { assert(c==CHANNEL_VIF1&&n==1); packet.ptr=storage; return &packet; }
uint64_t owl_flush_generation(void) { return queued; }
void owl_wait_generation(uint64_t g) { assert(g==queued); completed=queued; flag=1; waits++; }
int texture_test_csr(void) { return flag; }
void texture_test_clear_csr(void) { assert(completed==queued); flag=0; }
void set_finish(void) { graphics_finish_begin(); queued++; }
int graphics_surface_init(GSSURFACE *s) { memset(s,0,sizeof(*s)); return 0; }
int graphics_surface_bind_sync(GSSURFACE *s) {
    assert(completed==queued&&!flag); assert(s->Mem[0]==0x800000ff);
    /* 1x1 upload still reads a complete aligned QW; padding must be live. */
    assert(!((uintptr_t)s->Mem&127)); assert(s->Mem[1]==0&&s->Mem[3]==0);
    if(fail_upload) return GRAPHICS_BIND_ERROR;
    s->Vram=8192; s->TBW=1; resident=s; uploads++; return 0;
}
int graphics_surface_lock(GSSURFACE *s) { s->locked=true; return 1; }
bool graphics_surface_is_locked(const GSSURFACE *s) { return s->locked; }
void graphics_surface_release(GSSURFACE *s) {
    assert(completed==queued&&!flag); assert(s->Mem[0]==0x800000ff);
    releases++; if(resident==s) resident=NULL;
}
void athena_set_tw_th(const GSSURFACE *s,int *w,int *h) { (void)s; *w=0; *h=0; }
int athena_image_decode(const char *path,AthenaImageBuffer *b) {
    memset(b,0,sizeof(*b)); if(!strcmp(path,"missing")) { b->error=ATHENA_IMAGE_LOAD_OPEN; return -1; }
    b->width=b->height=1; b->psm=decode_psm; b->mem=calloc(4,4);
    ((unsigned char *)b->mem)[0]=255; return 0;
}
void athena_image_buffer_release(AthenaImageBuffer *b) { free(b->mem); b->mem=NULL; }
int main(void) {
    set_finish(); assert(queued==1&&completed==0);
    /* A fresh FINISH must consume the old one before clearing CSR. */
    set_finish(); assert(completed==1&&queued==2&&!flag);
    graphics_finish_wait(); assert(completed==2&&!flag);
    unsigned old_waits=waits; graphics_finish_wait(); assert(waits==old_waits);
    uint32_t pixel=255; AthenaTexture3DPixels p={.width=1,.height=1,.pixels=&pixel,.pixel_count=1};
    AthenaTexture3D *texture=NULL; AthenaTexture3DBinding binding;
    assert(!athena_texture3d_create(&p,&texture)); assert(!athena_texture3d_bind(texture,&binding));
    assert(uploads==1&&resident&&resident->locked&&!(binding.tex0&(1ull<<34)));
    old_waits=waits; assert(!athena_texture3d_bind(texture,&binding)); assert(waits==old_waits&&uploads==1);
    /* Retained CPU pixels permit re-upload after the texture manager reset. */
    graphics_wait_idle(); resident->locked=false; resident->Vram=0;
    assert(!athena_texture3d_bind(texture,&binding)); assert(uploads==2);
    set_finish(); athena_texture3d_release(texture); assert(completed==queued&&releases==1&&!resident);
    fail_upload=1; assert(!athena_texture3d_create(&p,&texture));
    assert(athena_texture3d_bind(texture,&binding)==ATHENA_MODEL3D_EVRAM); athena_texture3d_release(texture);
    fail_upload=0;
    decode_psm=GS_PSM_CT24; assert(!athena_texture3d_load("rgb",0,&texture));
    athena_texture3d_view(texture,&p); assert(p.pixels[0]==0x800000ff); athena_texture3d_release(texture);
    decode_psm=19; assert(athena_texture3d_load("palette",0,&texture)==ATHENA_MODEL3D_EUNSUPPORTED&&!texture);
    assert(athena_texture3d_load("missing",0,&texture)==ATHENA_MODEL3D_EIO&&!texture);
    puts("3D GS texture upload, FINISH ordering, reset and release tests passed"); return 0;
}
