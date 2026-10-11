#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <athena/model3d.h>
#include "texture3d_backend.h"
struct AthenaTexture3D { uint64_t refs,stamp; AthenaTexture3DPixels view; void *backend; };
static uint64_t texture_stamp;
uint64_t athena_texture3d_stamp(const AthenaTexture3D *t) { return t?t->stamp:0; }
static int dimension(uint32_t n) { return n&&n<=ATHENA_TEXTURE3D_MAX_SIZE&&!(n&(n-1)); }
/* The retained CPU copy: GIF uploads round to QW, so zero padding and 128-byte alignment keep even
 * a 1x1 texture safe for the existing DMA_REF upload implementation. GS alpha: 0x80 is 1.0. The
 * opaque pass ignores it (TEX0.TCC = 0); alpha-tested materials read it. Thread-safe. */
static uint32_t *prepare_pixels(const uint32_t *src,uint32_t count) {
    size_t bytes=count*sizeof(uint32_t),allocated=(bytes+15)&~(size_t)15;
    uint32_t *copy=memalign(128,allocated);
    if(!copy) return NULL;
    memset(copy,0,allocated);
    for(uint32_t i=0;i<count;i++) copy[i]=(src[i]&0xffffffu)|(((src[i]>>24)*128u+127u)/255u)<<24;
    return copy;
}
int athena_texture3d_decode_prepared(const char *path,uint32_t **prepared,uint32_t *width,uint32_t *height) {
    if(!prepared) return ATHENA_MODEL3D_EINVAL;
    *prepared=NULL;
    uint32_t *pixels=NULL,w=0,h=0;
    int result=athena_texture3d_decode_quiet(path,&pixels,&w,&h);
    if(result<0) return result;
    if(!dimension(w)||!dimension(h)) { free(pixels); return ATHENA_MODEL3D_EUNSUPPORTED; }
    uint32_t *copy=prepare_pixels(pixels,w*h);
    free(pixels);
    if(!copy) return ATHENA_MODEL3D_ENOMEM;
    *prepared=copy; *width=w; *height=h; return 0;
}
int athena_texture3d_adopt(uint32_t *prepared,uint32_t width,uint32_t height,AthenaTexture3DFilter filter,AthenaTexture3DWrap wrap,
    AthenaTexture3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    if(!prepared||!dimension(width)||!dimension(height)||(filter!=0&&filter!=1)||(unsigned)wrap>ATHENA_TEXTURE3D_REPEAT||
        ((uintptr_t)prepared&127)) return ATHENA_MODEL3D_EINVAL;
    AthenaTexture3D *t=calloc(1,sizeof(*t)); if(!t) return ATHENA_MODEL3D_ENOMEM;
    t->refs=1; t->stamp=++texture_stamp;
    t->view=(AthenaTexture3DPixels){width,height,filter,prepared,width*height,wrap};
    *out=t; return ATHENA_MODEL3D_OK;
}
int athena_texture3d_create(const AthenaTexture3DPixels *p,AthenaTexture3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    if(!p||!p->pixels||!dimension(p->width)||!dimension(p->height)||p->pixel_count!=p->width*p->height||
        (p->filter!=ATHENA_TEXTURE3D_NEAREST&&p->filter!=ATHENA_TEXTURE3D_LINEAR)||
        (unsigned)p->wrap>ATHENA_TEXTURE3D_REPEAT) return ATHENA_MODEL3D_EINVAL;
    AthenaTexture3D *t=calloc(1,sizeof(*t)); if(!t) return ATHENA_MODEL3D_ENOMEM;
    uint32_t *copy=prepare_pixels(p->pixels,p->pixel_count);
    if(!copy) { free(t); return ATHENA_MODEL3D_ENOMEM; }
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
