#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <athena/model3d.h>
#include "texture3d_backend.h"
struct AthenaTexture3D { uint64_t refs; AthenaTexture3DPixels view; void *backend; };
static int dimension(uint32_t n) { return n&&n<=ATHENA_TEXTURE3D_MAX_SIZE&&!(n&(n-1)); }
int athena_texture3d_create(const AthenaTexture3DPixels *p,AthenaTexture3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    if(!p||!p->pixels||!dimension(p->width)||!dimension(p->height)||p->pixel_count!=p->width*p->height||
        (p->filter!=ATHENA_TEXTURE3D_NEAREST&&p->filter!=ATHENA_TEXTURE3D_LINEAR)) return ATHENA_MODEL3D_EINVAL;
    AthenaTexture3D *t=calloc(1,sizeof(*t)); if(!t) return ATHENA_MODEL3D_ENOMEM;
    /* GIF uploads round to QW. Zero padding and 128-byte alignment keep even
     * a 1x1 texture safe for the existing DMA_REF upload implementation. */
    size_t bytes=p->pixel_count*sizeof(uint32_t),allocated=(bytes+15)&~(size_t)15;
    uint32_t *copy=memalign(128,allocated);
    if(!copy) { free(t); return ATHENA_MODEL3D_ENOMEM; }
    memset(copy,0,allocated);
    for(uint32_t i=0;i<p->pixel_count;i++) copy[i]=(p->pixels[i]&0xffffffu)|0x80000000u;
    t->refs=1; t->view=*p; t->view.pixels=copy; *out=t; return ATHENA_MODEL3D_OK;
}
int athena_texture3d_load(const char *path,AthenaTexture3DFilter filter,AthenaTexture3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    if(!path||!*path||(filter!=0&&filter!=1)) return ATHENA_MODEL3D_EINVAL;
    uint32_t *pixels=NULL,width=0,height=0;
    int result=athena_texture3d_decode(path,&pixels,&width,&height);
    if(result==0) {
        AthenaTexture3DPixels p={width,height,filter,pixels,width*height};
        result=athena_texture3d_create(&p,out);
    }
    free(pixels); return result;
}
void athena_texture3d_retain(AthenaTexture3D *t) { if(t) t->refs++; }
void athena_texture3d_release(AthenaTexture3D *t) {
    if(t&&--t->refs==0) {
        /* Platform destroy waits for GS completion before releasing VRAM.
         * The CPU pixels must stay alive through that wait and upload DMA. */
        athena_texture3d_backend_destroy(t->backend); free((void *)t->view.pixels); free(t);
    }
}
void athena_texture3d_view(const AthenaTexture3D *t,AthenaTexture3DPixels *out) { *out=t->view; }
int athena_texture3d_bind(AthenaTexture3D *t,AthenaTexture3DBinding *out) {
    if(!t||!out) return ATHENA_MODEL3D_EINVAL;
    if(!t->backend) {
        t->backend=athena_texture3d_backend_create(&t->view);
        if(!t->backend) return ATHENA_MODEL3D_ENOMEM;
    }
    return athena_texture3d_backend_bind(t->backend,out);
}
