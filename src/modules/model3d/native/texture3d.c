#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <athena/model3d.h>
#include "texture3d_backend.h"
struct AthenaTexture3D { uint64_t refs,stamp; AthenaTexture3DPixels view; void *backend; };
static uint64_t texture_stamp;
uint64_t athena_texture3d_stamp(const AthenaTexture3D *t) { return t?t->stamp:0; }
static int dimension(uint32_t n) { return n&&n<=ATHENA_TEXTURE3D_MAX_SIZE&&!(n&(n-1)); }
int athena_texture3d_create(const AthenaTexture3DPixels *p,AthenaTexture3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    if(!p||!p->pixels||!dimension(p->width)||!dimension(p->height)||p->pixel_count!=p->width*p->height||
        (p->filter!=ATHENA_TEXTURE3D_NEAREST&&p->filter!=ATHENA_TEXTURE3D_LINEAR)||
        (unsigned)p->wrap>ATHENA_TEXTURE3D_REPEAT) return ATHENA_MODEL3D_EINVAL;
    AthenaTexture3D *t=calloc(1,sizeof(*t)); if(!t) return ATHENA_MODEL3D_ENOMEM;
    /* GIF uploads round to QW. Zero padding and 128-byte alignment keep even
     * a 1x1 texture safe for the existing DMA_REF upload implementation. */
    size_t bytes=p->pixel_count*sizeof(uint32_t),allocated=(bytes+15)&~(size_t)15;
    uint32_t *copy=memalign(128,allocated);
    if(!copy) { free(t); return ATHENA_MODEL3D_ENOMEM; }
    memset(copy,0,allocated);
    /* GS alpha: 0x80 is 1.0. The opaque pass ignores it (TEX0.TCC = 0);
     * alpha-tested materials read it. */
    for(uint32_t i=0;i<p->pixel_count;i++)
        copy[i]=(p->pixels[i]&0xffffffu)|(((p->pixels[i]>>24)*128u+127u)/255u)<<24;
    t->refs=1; t->stamp=++texture_stamp; t->view=*p; t->view.pixels=copy; *out=t; return ATHENA_MODEL3D_OK;
}
int athena_texture3d_load(const char *path,AthenaTexture3DFilter filter,AthenaTexture3D **out) {
    return athena_texture3d_load_ex(path,filter,ATHENA_TEXTURE3D_CLAMP,out);
}
int athena_texture3d_load_ex(const char *path,AthenaTexture3DFilter filter,AthenaTexture3DWrap wrap,AthenaTexture3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    if(!path||!*path||(filter!=0&&filter!=1)||(unsigned)wrap>ATHENA_TEXTURE3D_REPEAT) return ATHENA_MODEL3D_EINVAL;
    uint32_t *pixels=NULL,width=0,height=0;
    int result=athena_texture3d_decode(path,&pixels,&width,&height);
    if(result==0) {
        AthenaTexture3DPixels p={width,height,filter,pixels,width*height,wrap};
        result=athena_texture3d_create(&p,out);
    }
    free(pixels); return result;
}
int athena_texture3d_load_memory(const void *data,size_t size,AthenaTexture3DFilter filter,AthenaTexture3DWrap wrap,
    AthenaTexture3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    if(!data||!size||(filter!=0&&filter!=1)||(unsigned)wrap>ATHENA_TEXTURE3D_REPEAT) return ATHENA_MODEL3D_EINVAL;
    uint32_t *pixels=NULL,width=0,height=0;
    int result=athena_texture3d_decode_memory(data,size,&pixels,&width,&height);
    if(result==0) {
        AthenaTexture3DPixels p={width,height,filter,pixels,width*height,wrap};
        result=athena_texture3d_create(&p,out);
    }
    free(pixels); return result;
}
void athena_texture3d_retain(AthenaTexture3D *t) { if(t) t->refs++; }
void athena_texture3d_release(AthenaTexture3D *t) {
    if(t&&--t->refs==0) {
        /* The backend frees VRAM and the CPU pixels once the GS is done
         * with them: deferred to a later frame instead of waiting here. */
        athena_texture3d_backend_destroy(t->backend,(void *)t->view.pixels); free(t);
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
int athena_texture3d_upload(AthenaTexture3D *t) {
    AthenaTexture3DBinding binding;
    return athena_texture3d_bind(t,&binding);
}
