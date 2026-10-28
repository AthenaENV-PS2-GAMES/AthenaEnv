#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <athena/model3d.h>
#include "texture3d_backend.h"

#if defined(ATHENA_MEMORY_DIAGNOSTICS) && ATHENA_MEMORY_DIAGNOSTICS
#define ATHENA_MEMORY_CATEGORY ATHENA_MEM_TEXTURE
#include <athena/memory_tags.h>
#endif
struct AthenaTexture3D { uint64_t refs,stamp; AthenaTexture3DPixels view; void *backend; AthenaTexture3DIndexed *indexed; int target,writing;
    /* Phase 5C.5B: compact_ok = owner declared it does not need the infallible legacy view(); pinned = a pixel pointer
     * was handed out (never released before the final release). "Canonical absent" is derived: !target && !view.pixels. */
    uint8_t compact_ok,pinned; };
static AthenaTexture3DCompactStats compact_stats;
static size_t canonical_bytes(const AthenaTexture3D *t) { return ((size_t)t->view.pixel_count*sizeof(uint32_t)+15)&~(size_t)15; }
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
int athena_texture3d_decode_prepared_indexed(const char *path,uint32_t **prepared,AthenaTexture3DIndexed **indexed,
    uint32_t *width,uint32_t *height) {
    if(indexed) *indexed=NULL;
    if(!prepared) return ATHENA_MODEL3D_EINVAL;
    *prepared=NULL;
    uint32_t *pixels=NULL,w=0,h=0; AthenaTexture3DIndexed *x=NULL;
    int result=athena_texture3d_decode_canonical_indexed(path,1,&pixels,indexed?&x:NULL,&w,&h);
    if(result<0) return result;
    if(!dimension(w)||!dimension(h)) { free(pixels); athena_texture3d_indexed_free(x); return ATHENA_MODEL3D_EUNSUPPORTED; }
    *prepared=pixels; *width=w; *height=h; if(indexed) *indexed=x; return 0;
}
int athena_texture3d_decode_prepared(const char *path,uint32_t **prepared,uint32_t *width,uint32_t *height) {
    return athena_texture3d_decode_prepared_indexed(path,prepared,NULL,width,height);
}
int athena_texture3d_adopt_indexed(uint32_t *prepared,AthenaTexture3DIndexed *indexed,uint32_t width,uint32_t height,
    AthenaTexture3DFilter filter,AthenaTexture3DWrap wrap,AthenaTexture3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    if(!prepared||!dimension(width)||!dimension(height)||(filter!=0&&filter!=1)||(unsigned)wrap>ATHENA_TEXTURE3D_REPEAT||
        ((uintptr_t)prepared&127)) return ATHENA_MODEL3D_EINVAL;
    AthenaTexture3D *t=calloc(1,sizeof(*t)); if(!t) return ATHENA_MODEL3D_ENOMEM;
    t->refs=1; t->stamp=++texture_stamp; t->indexed=indexed;
    t->view=(AthenaTexture3DPixels){width,height,filter,prepared,width*height,wrap};
    *out=t; return ATHENA_MODEL3D_OK;
}
int athena_texture3d_adopt(uint32_t *prepared,uint32_t width,uint32_t height,AthenaTexture3DFilter filter,AthenaTexture3DWrap wrap,
    AthenaTexture3D **out) {
    return athena_texture3d_adopt_indexed(prepared,NULL,width,height,filter,wrap,out);
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
int athena_texture3d_create_target(uint32_t width,uint32_t height,AthenaTexture3DFilter filter,AthenaTexture3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    if(width<64||height<64||!dimension(width)||!dimension(height)||(filter!=0&&filter!=1)) return ATHENA_MODEL3D_EINVAL;
    AthenaTexture3D *t=calloc(1,sizeof(*t)); if(!t) return ATHENA_MODEL3D_ENOMEM;
    t->refs=1; t->stamp=++texture_stamp; t->target=1;
    t->view=(AthenaTexture3DPixels){.width=width,.height=height,.filter=filter,.wrap=ATHENA_TEXTURE3D_CLAMP};
    *out=t; return 0;
}
int athena_texture3d_target_begin(AthenaTexture3D *t,AthenaTexture3DTarget *out) {
    if(!t||!out||!t->target||t->writing) return ATHENA_MODEL3D_EINVAL;
    if(!t->backend) t->backend=athena_texture3d_backend_create_target(&t->view);
    if(!t->backend) return ATHENA_MODEL3D_ENOMEM;
    int code=athena_texture3d_backend_target_begin(t->backend,out);
    if(code<0) return code;
    t->writing=1; athena_texture3d_retain(t); return 0;
}
void athena_texture3d_target_end(AthenaTexture3D *t,int commit) {
    if(!t||!t->writing) return;
    athena_texture3d_backend_target_end(t->backend,commit); t->writing=0;
    /* Bump the binding revision: a recapture must never reuse a stale pass. */
    t->stamp=++texture_stamp; athena_texture3d_release(t);
}
int athena_texture3d_load(const char *path,AthenaTexture3DFilter filter,AthenaTexture3D **out) {
    return athena_texture3d_load_ex(path,filter,ATHENA_TEXTURE3D_CLAMP,out);
}
/* Takes a canonical buffer from decode_canonical(): adopted on success, freed on failure. Non-power-of-two
 * sizes return EINVAL, as the former create() path did. */
static int adopt_decoded(int result,uint32_t *pixels,AthenaTexture3DIndexed *indexed,uint32_t width,uint32_t height,
    AthenaTexture3DFilter filter,AthenaTexture3DWrap wrap,AthenaTexture3D **out) {
    if(result<0) { athena_texture3d_indexed_free(indexed); return result; }
    if(!dimension(width)||!dimension(height)) result=ATHENA_MODEL3D_EINVAL;
    else result=athena_texture3d_adopt_indexed(pixels,indexed,width,height,filter,wrap,out);
    if(result<0) { free(pixels); athena_texture3d_indexed_free(indexed); }
    return result;
}
int athena_texture3d_load_ex(const char *path,AthenaTexture3DFilter filter,AthenaTexture3DWrap wrap,AthenaTexture3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    if(!path||!*path||(filter!=0&&filter!=1)||(unsigned)wrap>ATHENA_TEXTURE3D_REPEAT) return ATHENA_MODEL3D_EINVAL;
    uint32_t *pixels=NULL,width=0,height=0; AthenaTexture3DIndexed *indexed=NULL;
    int result=athena_texture3d_decode_canonical_indexed(path,0,&pixels,&indexed,&width,&height);
    return adopt_decoded(result,pixels,indexed,width,height,filter,wrap,out);
}
int athena_texture3d_load_memory(const void *data,size_t size,AthenaTexture3DFilter filter,AthenaTexture3DWrap wrap,
    AthenaTexture3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    if(!data||!size||(filter!=0&&filter!=1)||(unsigned)wrap>ATHENA_TEXTURE3D_REPEAT) return ATHENA_MODEL3D_EINVAL;
    uint32_t *pixels=NULL,width=0,height=0; AthenaTexture3DIndexed *indexed=NULL;
    int result=athena_texture3d_decode_canonical_memory_indexed(data,size,&pixels,&indexed,&width,&height);
    return adopt_decoded(result,pixels,indexed,width,height,filter,wrap,out);
}
void athena_texture3d_retain(AthenaTexture3D *t) { if(t) t->refs++; }
void athena_texture3d_release(AthenaTexture3D *t) {
    if(t&&--t->refs==0) {
        /* The backend frees VRAM and the CPU pixels once the GS is done
         * with them: deferred to a later frame instead of waiting here. */
        if(t->pinned&&t->compact_ok) { compact_stats.pinned_buffers--; compact_stats.pinned_bytes-=canonical_bytes(t); }
        athena_texture3d_backend_destroy(t->backend,(void *)t->view.pixels); athena_texture3d_indexed_free(t->indexed); free(t);
    }
}
int athena_texture3d_info(const AthenaTexture3D *t,AthenaTexture3DInfo *out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=(AthenaTexture3DInfo){0};
    if(!t) return ATHENA_MODEL3D_EINVAL;
    *out=(AthenaTexture3DInfo){t->view.width,t->view.height,t->view.filter,t->view.wrap,t->target};
    return 0;
}
/* A pixel pointer is about to be handed out: from now on the canonical buffer lives until the final release. */
static void pin(AthenaTexture3D *t) {
    if(t->pinned) return;
    t->pinned=1;
    if(t->compact_ok) { compact_stats.pinned_buffers++; compact_stats.pinned_bytes+=canonical_bytes(t); }
}
int athena_texture3d_try_view(AthenaTexture3D *t,AthenaTexture3DPixels *out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=(AthenaTexture3DPixels){0};
    if(!t) return ATHENA_MODEL3D_EINVAL;
    *out=t->view; out->pixels=NULL; out->pixel_count=0;
    if(t->target) return 0; /* GPU-owned: no CPU pixels by design, not an error */
    if(!t->view.pixels) {
        uint32_t *rebuilt=NULL;
        compact_stats.materialize_attempts++;
        int code=t->backend?athena_texture3d_backend_materialize(t->backend,&rebuilt):ATHENA_MODEL3D_EINVAL;
        if(code<0) { compact_stats.materialize_failures++; return code; }
        t->view.pixels=rebuilt; compact_stats.materialize_ok++;
    }
    pin(t);
    *out=t->view; return 0;
}
int athena_texture3d_pin_canonical(AthenaTexture3D *t) {
    AthenaTexture3DPixels ignored;
    return athena_texture3d_try_view(t,&ignored);
}
void athena_texture3d_view(const AthenaTexture3D *t,AthenaTexture3DPixels *out) {
    /* Cannot fail unless the owner opted in to compaction and the canonical buffer was released. */
    if(athena_texture3d_try_view((AthenaTexture3D *)t,out)<0)
        athena_model3d_set_detail("texture3d view: cannot rebuild canonical pixels (use athena_texture3d_try_view)");
}
/* Frees the canonical RGBA once the indexed backend exists and owns the upload source. */
static void release_canonical(AthenaTexture3D *t) {
    if(!ATHENA_TEXTURE3D_COMPACT_CANONICAL||!t->compact_ok||t->pinned||t->target||!t->view.pixels||!t->backend||
        !athena_texture3d_backend_indexed_resident(t->backend)) return;
    void *canonical=(void *)t->view.pixels;
    compact_stats.released_buffers++; compact_stats.released_bytes+=canonical_bytes(t);
    t->view.pixels=NULL; free(canonical);
}
void athena_texture3d_allow_compaction(AthenaTexture3D *t) {
    if(!t||t->target) return;
    t->compact_ok=1;
    if(t->pinned) { compact_stats.pinned_buffers++; compact_stats.pinned_bytes+=canonical_bytes(t); } /* pinned before opting in */
    release_canonical(t);
}
void athena_texture3d_compact_stats(AthenaTexture3DCompactStats *out) { if(out) *out=compact_stats; }
int athena_texture3d_bind(AthenaTexture3D *t,AthenaTexture3DBinding *out) {
    if(!t||!out) return ATHENA_MODEL3D_EINVAL;
    if(t->writing) { athena_model3d_set_detail("cannot sample a render target during its capture"); return ATHENA_MODEL3D_EINVAL; }
    if(!t->backend) {
        /* The sidecar stays with the texture until the backend exists (create_adopt consumes it only on success). */
        t->backend=t->target?athena_texture3d_backend_create_target(&t->view):athena_texture3d_backend_create_adopt(&t->view,&t->indexed);
        if(!t->backend) return ATHENA_MODEL3D_ENOMEM;
        release_canonical(t);
    }
    return athena_texture3d_backend_bind(t->backend,out);
}
int athena_texture3d_upload(AthenaTexture3D *t) {
    AthenaTexture3DBinding binding;
    return athena_texture3d_bind(t,&binding);
}
