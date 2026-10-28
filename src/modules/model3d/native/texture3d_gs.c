#include <athena/memory_retirement.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <athena/image.h>
#include <athena/model3d.h>
#include <athena/graphics/sync.h>
#include "texture3d_backend.h"

#if defined(ATHENA_MEMORY_DIAGNOSTICS) && ATHENA_MEMORY_DIAGNOSTICS
#define ATHENA_MEMORY_CATEGORY ATHENA_MEM_TEXTURE
#include <athena/memory_tags.h>
#endif
/* CSM1 stores each 32-entry block of a 256-entry CLUT with entries 8..15
 * and 16..23 swapped (the GS addressing order): the slot of index i. */
static uint32_t clut_slot(uint32_t i) { return (i&0xe7u)|((i&0x08u)<<1)|((i&0x10u)>>1); }
/* 16-bit images are opaque: the STP bit is not a coverage alpha. */
static uint32_t expand16(uint16_t v) {
    uint32_t r=v&31,g=(v>>5)&31,b=(v>>10)&31;
    return (r<<3|r>>2)|(g<<3|g>>2)<<8|(b<<3|b>>2)<<16|0xff000000u;
}
static uint32_t gs_to_alpha(uint32_t pixel) {
    uint32_t a=pixel>>24; a=a>=0x80?255:a*2+(a>>6);
    return (pixel&0xffffffu)|a<<24;
}
/* Texture3D retained-copy alpha: standard 0..255 to GS 0..0x80 (the former prepare_pixels() formula). */
static uint32_t alpha_to_gs(uint32_t pixel) {
    return (pixel&0xffffffu)|(((pixel>>24)*128u+127u)/255u)<<24;
}
static int expand_buffer(AthenaImageBuffer *buffer,uint32_t **pixels,AthenaTexture3DIndexed **indexed,uint32_t *width,uint32_t *height,int quiet,int canonical);
static int decode_file(const char *path,uint32_t **pixels,AthenaTexture3DIndexed **indexed,uint32_t *width,uint32_t *height,int quiet,int canonical) {
    AthenaImageBuffer buffer;
    if(athena_image_decode(path,&buffer)<0)
        return buffer.error==ATHENA_IMAGE_LOAD_OPEN?ATHENA_MODEL3D_EIO:ATHENA_MODEL3D_EFORMAT;
    return expand_buffer(&buffer,pixels,indexed,width,height,quiet,canonical);
}
int athena_texture3d_decode(const char *path,uint32_t **pixels,uint32_t *width,uint32_t *height) {
    return decode_file(path,pixels,NULL,width,height,0,0);
}
/* Thread-safe: no detail string, no other shared state (the worker half of a texture load). */
int athena_texture3d_decode_quiet(const char *path,uint32_t **pixels,uint32_t *width,uint32_t *height) {
    return decode_file(path,pixels,NULL,width,height,1,0);
}
int athena_texture3d_decode_memory(const void *data,size_t size,uint32_t **pixels,uint32_t *width,uint32_t *height) {
    AthenaImageBuffer buffer;
    if(athena_image_decode_memory(data,size,&buffer)<0) {
        athena_model3d_set_detail("embedded image is not a decodable PNG/JPEG/BMP");
        return ATHENA_MODEL3D_EFORMAT;
    }
    return expand_buffer(&buffer,pixels,NULL,width,height,0,0);
}
/* The retained-copy form: the expansion buffer is itself the canonical allocation (128-byte aligned,
 * zero-padded to 16 bytes, GS alpha), so no second 4WH copy exists. The caller frees *pixels with
 * free() or hands it to athena_texture3d_adopt(). Dimensions are not policy-checked here. */
int athena_texture3d_decode_canonical(const char *path,int quiet,uint32_t **pixels,uint32_t *width,uint32_t *height) {
    return decode_file(path,pixels,NULL,width,height,quiet,1);
}
int athena_texture3d_decode_canonical_memory(const void *data,size_t size,uint32_t **pixels,uint32_t *width,uint32_t *height) {
    return athena_texture3d_decode_canonical_memory_indexed(data,size,pixels,NULL,width,height);
}
/* Phase 5C.4: the canonical buffer plus, when the source is a T4/T8 image whose exact indexed backend is
 * strictly smaller in GS memory, the ready-made index/CLUT streams. *indexed is NULL otherwise. */
int athena_texture3d_decode_canonical_indexed(const char *path,int quiet,uint32_t **pixels,AthenaTexture3DIndexed **indexed,
    uint32_t *width,uint32_t *height) {
    if(indexed) *indexed=NULL;
    return decode_file(path,pixels,indexed,width,height,quiet,1);
}
int athena_texture3d_decode_canonical_memory_indexed(const void *data,size_t size,uint32_t **pixels,AthenaTexture3DIndexed **indexed,
    uint32_t *width,uint32_t *height) {
    if(indexed) *indexed=NULL;
    AthenaImageBuffer buffer;
    if(athena_image_decode_memory(data,size,&buffer)<0) {
        athena_model3d_set_detail("embedded image is not a decodable PNG/JPEG/BMP");
        return ATHENA_MODEL3D_EFORMAT;
    }
    return expand_buffer(&buffer,pixels,indexed,width,height,0,1);
}
/* Consumes buffer: 32-bit pixels from any decoded format; canonical selects the retained-copy layout. */
static AthenaTexture3DIndexed *build_indexed(const AthenaImageBuffer *b,uint32_t width,uint32_t height);
static int expand_buffer(AthenaImageBuffer *b,uint32_t **pixels,AthenaTexture3DIndexed **indexed,uint32_t *width,uint32_t *height,int quiet,int canonical) {
    AthenaImageBuffer buffer=*b;
    int result=ATHENA_MODEL3D_EUNSUPPORTED;
    int paletted=buffer.psm==GS_PSM_T8||buffer.psm==GS_PSM_T4;
    if(buffer.width>ATHENA_TEXTURE3D_MAX_SIZE||buffer.height>ATHENA_TEXTURE3D_MAX_SIZE) {
        if(!quiet) athena_model3d_set_detail("texture %ux%u exceeds %u",(unsigned)buffer.width,(unsigned)buffer.height,(unsigned)ATHENA_TEXTURE3D_MAX_SIZE);
        goto done;
    }
    if((buffer.psm!=GS_PSM_CT32&&buffer.psm!=GS_PSM_CT24&&buffer.psm!=GS_PSM_CT16&&!paletted)||
        (paletted&&(!buffer.clut||buffer.clut_psm!=GS_PSM_CT32))) {
        if(!quiet) athena_model3d_set_detail("texture pixel format 0x%x is not supported",(unsigned)buffer.psm);
        goto done;
    }
    uint32_t count=buffer.width*buffer.height;
    size_t allocated=((size_t)count*sizeof(uint32_t)+15)&~(size_t)15;
    uint32_t *copy=canonical?memalign(128,allocated):malloc(count*sizeof(*copy));
    if(!copy) { result=ATHENA_MODEL3D_ENOMEM; goto done; }
    if(canonical) memset(copy+count,0,allocated-(size_t)count*sizeof(uint32_t));
    const uint8_t *bytes=(const uint8_t *)buffer.mem;
    if(paletted) {
        /* Palette-level conversion: the same gs_to_alpha()/alpha_to_gs() composition, applied once per logical index
         * instead of once per pixel (entries beyond the PLTE are the loader's zero padding and convert to zero, exactly
         * as the per-pixel form read them). */
        uint32_t pal[256]; const int t8=buffer.psm==GS_PSM_T8; const unsigned entries=t8?256:16;
        for(unsigned k=0;k<entries;k++) {
            uint32_t c=gs_to_alpha(buffer.clut[t8?clut_slot(k):k]);
            pal[k]=canonical?alpha_to_gs(c):c;
        }
        if(t8) for(uint32_t i=0;i<count;i++) copy[i]=pal[bytes[i]];
        else { /* T4: low nibble first */
            uint32_t i=0;
            for(;i+1<count;i+=2) { unsigned b=bytes[i/2]; copy[i]=pal[b&15]; copy[i+1]=pal[b>>4]; }
            if(i<count) copy[i]=pal[bytes[i/2]&15];
        }
    } else for(uint32_t i=0;i<count;i++) {
        /* The loaders store GS alpha (0..0x80); Texture3D pixels use 0..255. */
        if(buffer.psm==GS_PSM_CT32) copy[i]=gs_to_alpha(buffer.mem[i]);
        else if(buffer.psm==GS_PSM_CT24) {
            const uint8_t *p=bytes+i*3;
            copy[i]=p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|0xff000000u;
        } else copy[i]=expand16((uint16_t)(bytes[i*2]|bytes[i*2+1]<<8));
        if(canonical) copy[i]=alpha_to_gs(copy[i]);
    }
    /* Optional, best effort: failure leaves NULL and the backend falls back to scanning the canonical pixels. */
    if(canonical&&paletted&&indexed) *indexed=build_indexed(&buffer,buffer.width,buffer.height);
    *pixels=copy; *width=buffer.width; *height=buffer.height; result=0;
done:
    athena_image_buffer_release(&buffer); return result;
}
/* The surface first: the texture manager only ever sees &backend->surface. */
typedef struct { GSSURFACE surface; AthenaTexture3DWrap wrap; void *indexed; int target,valid; } Backend;
/* Keep the canonical CT32 view, but upload an exact indexed copy when the
 * combined texture/CLUT allocation is smaller. Alpha participates in identity.
 * Allocation failure is harmless: the borrowed CT32 surface remains usable. */
/* The exact format rule: T4 for at most 16 colors else T8, only when texture+CLUT GS memory is strictly smaller than CT32. */
static int indexed_plan(unsigned count,uint32_t w,uint32_t h,int *psm,size_t *index_bytes,size_t *clut_bytes) {
    *psm=count<=16?GS_PSM_T4:GS_PSM_T8;
    int cw=*psm==GS_PSM_T4?8:16,ch=*psm==GS_PSM_T4?2:16;
    uint32_t cost=athena_vram_surface_size(w,h,*psm)+athena_vram_surface_size(cw,ch,GS_PSM_CT32);
    if(cost>=athena_vram_surface_size(w,h,GS_PSM_CT32)) return 0;
    size_t bytes=*psm==GS_PSM_T4?((size_t)w*h+1)/2:(size_t)w*h;
    *index_bytes=(bytes+15)&~(size_t)15;
    /* The manager flushes the whole CLUT VRAM allocation, including padding. */
    *clut_bytes=athena_vram_surface_size(cw,ch,GS_PSM_CT32);
    return 1;
}
static void index_surface(Backend *b,const AthenaTexture3DPixels *p) {
    uint16_t slots[512]={0}; uint32_t colors[256]; unsigned count=0;
    for(uint32_t i=0;i<p->pixel_count;i++) {
        uint32_t color=p->pixels[i]; unsigned slot=(color*2654435761u)>>23;
        while(slots[slot]&&colors[slots[slot]-1]!=color) slot=(slot+1)&511;
        if(!slots[slot]) {
            if(count==256) return;
            colors[count]=color; slots[slot]=++count;
        }
    }
    int psm; size_t bytes,clut_bytes;
    if(!indexed_plan(count,p->width,p->height,&psm,&bytes,&clut_bytes)) return;
    uint8_t *indices=memalign(128,bytes); uint32_t *clut=memalign(128,clut_bytes);
    if(!indices||!clut) { free(indices); free(clut); return; }
    memset(indices,0,bytes); memset(clut,0,clut_bytes);
    for(unsigned i=0;i<count;i++) clut[psm==GS_PSM_T8?clut_slot(i):i]=colors[i];
    for(uint32_t i=0;i<p->pixel_count;i++) {
        uint32_t color=p->pixels[i]; unsigned slot=(color*2654435761u)>>23;
        while(colors[slots[slot]-1]!=color) slot=(slot+1)&511;
        unsigned index=slots[slot]-1;
        if(psm==GS_PSM_T8) indices[i]=index;
        else indices[i/2]|=index<<((i&1)*4);
    }
    b->indexed=indices; b->surface.Mem=(uint32_t *)indices;
    b->surface.Clut=clut; b->surface.ClutPSM=GS_PSM_CT32; b->surface.PSM=psm;
}
/* Phase 5C.4: the result index_surface() would compute, derived from the decoder's own indices.
 * Source indices are renumbered by first appearance after mapping each source palette entry through the
 * same canonical alpha pipeline as expand_buffer() (gs_to_alpha then alpha_to_gs), merging entries that
 * become identical. Only a 256-entry table is searched; the pixel stream is never hashed. Output is
 * byte-identical to index_surface() on the canonical buffer. Never retains pointers into the decoder. */
struct AthenaTexture3DIndexed { uint8_t *indices; uint32_t *clut; uint32_t width,height; int psm; };
static AthenaTexture3DIndexed *build_indexed(const AthenaImageBuffer *b,uint32_t w,uint32_t h) {
    if(!w||!h||w>ATHENA_TEXTURE3D_MAX_SIZE||h>ATHENA_TEXTURE3D_MAX_SIZE||(w&(w-1))||(h&(h-1))) return NULL;
    const uint8_t *bytes=(const uint8_t *)b->mem;
    const int t8=b->psm==GS_PSM_T8;
    const uint32_t count=w*h;
    const unsigned entries=t8?256:16;
    uint8_t lut[256]={0},known[256]={0}; uint32_t colors[256]; unsigned n=0,used=0;
#define NUMBER(src) do { unsigned s_=(src); if(!known[s_]) { known[s_]=1; used++; \
        uint32_t color=alpha_to_gs(gs_to_alpha(b->clut[t8?clut_slot(s_):s_])); \
        unsigned c=0; while(c<n&&colors[c]!=color) c++; \
        if(c==n) colors[n++]=color; \
        lut[s_]=(uint8_t)c; } } while(0)
    /* Pass 1: first-appearance numbering. Stops once every possible source index has been seen: no pixel after that
     * can introduce a new color. */
    if(t8) for(uint32_t i=0;i<count&&used<entries;i++) NUMBER(bytes[i]);
    else for(uint32_t j=0;j<(count+1)/2&&used<entries;j++) {
        unsigned v=bytes[j]; NUMBER(v&15);
        if(2*j+1<count) NUMBER(v>>4);
    }
#undef NUMBER
    int psm; size_t index_bytes,clut_bytes;
    if(!indexed_plan(n,w,h,&psm,&index_bytes,&clut_bytes)) return NULL;
    AthenaTexture3DIndexed *x=malloc(sizeof(*x)); if(!x) return NULL;
    x->indices=memalign(128,index_bytes); x->clut=memalign(128,clut_bytes);
    if(!x->indices||!x->clut) { free(x->indices); free(x->clut); free(x); return NULL; }
    memset(x->indices,0,index_bytes); memset(x->clut,0,clut_bytes);
    for(unsigned i=0;i<n;i++) x->clut[psm==GS_PSM_T8?clut_slot(i):i]=colors[i];
    /* Pass 2: table-driven rewrite of the index stream (no per-pixel branches). Every index that occurs was numbered above. */
    uint8_t *d=x->indices;
    if(!t8) { /* T4 source -> T4 output: one table lookup per byte (two pixels) */
        uint8_t lut2[256]; for(unsigned v=0;v<256;v++) lut2[v]=(uint8_t)(lut[v&15]|lut[v>>4]<<4);
        const uint32_t pairs=count/2;
        for(uint32_t j=0;j<pairs;j++) d[j]=lut2[bytes[j]];
        if(count&1) d[pairs]=lut[bytes[pairs]&15]; /* the unused high nibble of an odd last byte stays zero */
    } else if(psm==GS_PSM_T8) for(uint32_t i=0;i<count;i++) d[i]=lut[bytes[i]];
    else { /* T8 source -> T4 output */
        const uint32_t pairs=count/2;
        for(uint32_t j=0;j<pairs;j++) d[j]=(uint8_t)(lut[bytes[2*j]]|lut[bytes[2*j+1]]<<4);
        if(count&1) d[pairs]=lut[bytes[count-1]];
    }
    x->width=w; x->height=h; x->psm=psm;
    return x;
}
void athena_texture3d_indexed_free(AthenaTexture3DIndexed *x) {
    if(x) { free(x->indices); free(x->clut); free(x); }
}
/* On success *xp is consumed (adopted by the backend or freed) and set to NULL; on failure it stays with the caller. */
void *athena_texture3d_backend_create_adopt(const AthenaTexture3DPixels *p,AthenaTexture3DIndexed **xp) {
    AthenaTexture3DIndexed *x=xp?*xp:NULL;
    Backend *b=calloc(1,sizeof(*b)); if(!b) return NULL;
    GSSURFACE *s=&b->surface;
    if(graphics_surface_init(s)<0) { free(b); return NULL; }
    s->Width=p->width; s->Height=p->height; s->PSM=GS_PSM_CT32;
    s->Filter=p->filter==ATHENA_TEXTURE3D_LINEAR?GS_FILTER_LINEAR:GS_FILTER_NEAREST;
    s->Mem=(uint32_t *)p->pixels; /* Borrowed from the owning Texture3D. */
    b->wrap=p->wrap; b->indexed=NULL;
    if(x&&x->width==p->width&&x->height==p->height) {
        b->indexed=x->indices; s->Mem=(uint32_t *)x->indices;
        s->Clut=x->clut; s->ClutPSM=GS_PSM_CT32; s->PSM=x->psm;
        free(x); /* the streams now belong to the backend (destroy_now frees them) */
    } else {
        athena_texture3d_indexed_free(x);
        index_surface(b,p);
    }
    if(xp) *xp=NULL;
    return b;
}
/* Always consumes x (adopted into the backend on success, freed otherwise). */
void *athena_texture3d_backend_create_indexed(const AthenaTexture3DPixels *p,AthenaTexture3DIndexed *x) {
    void *b=athena_texture3d_backend_create_adopt(p,&x);
    athena_texture3d_indexed_free(x);
    return b;
}
int athena_texture3d_backend_indexed_resident(const void *backend) {
    const Backend *b=backend;
    return b&&!b->target&&b->indexed&&b->surface.Clut&&(b->surface.PSM==GS_PSM_T4||b->surface.PSM==GS_PSM_T8);
}
/* Exact inverse of the index/CLUT encoding: the CLUT already holds canonical GS-alpha colors, so no alpha conversion. */
int athena_texture3d_backend_materialize(const void *backend,uint32_t **out) {
    if(out) *out=NULL;
    if(!out||!athena_texture3d_backend_indexed_resident(backend)) return ATHENA_MODEL3D_EINVAL;
    const Backend *b=backend; const uint8_t *idx=(const uint8_t *)b->indexed; const uint32_t *clut=b->surface.Clut;
    const uint32_t w=b->surface.Width,h=b->surface.Height;
    if(!w||!h||w>ATHENA_TEXTURE3D_MAX_SIZE||h>ATHENA_TEXTURE3D_MAX_SIZE) return ATHENA_MODEL3D_EINVAL;
    const uint32_t count=w*h; const size_t bytes=(size_t)count*sizeof(uint32_t),allocated=(bytes+15)&~(size_t)15;
    uint32_t *px=memalign(128,allocated); if(!px) return ATHENA_MODEL3D_ENOMEM;
    memset(px+count,0,allocated-bytes);
    uint32_t pal[256];
    if(b->surface.PSM==GS_PSM_T8) {
        for(unsigned k=0;k<256;k++) pal[k]=clut[clut_slot(k)];
        for(uint32_t i=0;i<count;i++) px[i]=pal[idx[i]];
    } else { /* T4: low nibble first */
        for(unsigned k=0;k<16;k++) pal[k]=clut[k];
        uint32_t i=0;
        for(;i+1<count;i+=2) { unsigned v=idx[i/2]; px[i]=pal[v&15]; px[i+1]=pal[v>>4]; }
        if(i<count) px[i]=pal[idx[i/2]&15];
    }
    *out=px; return 0;
}
void *athena_texture3d_backend_create(const AthenaTexture3DPixels *p) {
    return athena_texture3d_backend_create_indexed(p,NULL);
}
void *athena_texture3d_backend_create_target(const AthenaTexture3DPixels *p) {
    Backend *b=calloc(1,sizeof(*b)); if(!b) return NULL;
    GSSURFACE *s=&b->surface;
    if(graphics_surface_init(s)<0) { free(b); return NULL; }
    s->Width=p->width; s->Height=p->height; s->PSM=GS_PSM_CT32;
    s->Filter=p->filter==ATHENA_TEXTURE3D_LINEAR?GS_FILTER_LINEAR:GS_FILTER_NEAREST;
    s->PageAligned=1; b->target=1; b->wrap=ATHENA_TEXTURE3D_CLAMP;
    return b;
}
int athena_texture3d_backend_target_begin(void *backend,AthenaTexture3DTarget *out) {
    Backend *b=backend;
    if(!b||!b->target||!out) return ATHENA_MODEL3D_EINVAL;
    GSSURFACE *s=&b->surface;
    graphics_wait_idle(); athena_texture3d_collect(0);
    b->valid=0;
    if(!graphics_surface_is_locked(s)&&
        (graphics_surface_bind_sync(s)==GRAPHICS_BIND_ERROR||!graphics_surface_lock(s))) return ATHENA_MODEL3D_EVRAM;
    if(s->Vram%8192) return ATHENA_MODEL3D_EVRAM;
    *out=(AthenaTexture3DTarget){s->Vram,s->Width,s->Height,s->TBW}; return 0;
}
void athena_texture3d_backend_target_end(void *backend,int commit) {
    Backend *b=backend; if(b&&b->target) b->valid=!!commit;
}
int athena_texture3d_backend_bind(void *backend,AthenaTexture3DBinding *out) {
    Backend *b=backend;
    GSSURFACE *s=&b->surface; AthenaTexture3DWrap wrap=b->wrap;
    /* A video mode reset drops residency; never upload stale/absent CPU pixels. */
    if(b->target&&(!b->valid||!graphics_surface_is_locked(s))) {
        b->valid=0; athena_model3d_set_detail("render target needs a successful silhouette capture after creation, abort or video-mode reset");
        return ATHENA_MODEL3D_EINVAL;
    }
    athena_texture3d_collect(0); /* reclaim idle releases; nothing to do when none */
    if(!graphics_surface_is_locked(s)) {
        /* Do not reuse/evict a VRAM block while queued draws still sample it.
         * Synchronous upload plus a persistent lock avoids async markers.
         * The wait also makes every deferred release idle: reclaim them
         * first, so their VRAM is available to this upload. */
        graphics_wait_idle();
        athena_texture3d_collect(0);
        if(graphics_surface_bind_sync(s)==GRAPHICS_BIND_ERROR||!graphics_surface_lock(s))
            return ATHENA_MODEL3D_EVRAM;
    }
    int tw,th; athena_set_tw_th(s,&tw,&th);
    out->tex0=GS_SETREG_TEX0(s->Vram/256,s->TBW,s->PSM,tw,th,0,COLOR_MODULATE,s->VramClut/256,GS_PSM_CT32,0,0,s->Clut?1:0);
    out->tex1=GS_SETREG_TEX1(1,0,s->Filter,s->Filter,0,0,0);
    /* WMS/WMT: 0 repeats (power-of-two sizes), 1 clamps to the edge. */
    out->clamp=GS_SETREG_CLAMP(!(wrap&ATHENA_TEXTURE3D_REPEAT_U),!(wrap&ATHENA_TEXTURE3D_REPEAT_V),0,s->Width-1,0,s->Height-1);
    return 0;
}
/* Releases whose GS work may still be pending: the surface keeps its VRAM
 * and the CPU pixels stay alive (an upload DMA may read them) until the
 * fence in graphics/sync.h proves the GS idle. Main thread only. */
typedef struct Pending { Backend *backend; void *pixels; uint32_t frame,idle; struct Pending *next; } Pending;
static Pending *pending;
static void destroy_now(Backend *b,void *pixels) {
    if(b) { graphics_surface_release(&b->surface); free(b->indexed); free(b->surface.Clut); free(b); }
    free(pixels);
}
static void collect_ready(size_t budget) {
    uint32_t frames=graphics_finished_frames(),idle=graphics_idle_count();
    Pending **p=&pending;
    for(;*p&&budget;budget--) {
        Pending *e=*p;
        if(frames-e->frame>=2||idle!=e->idle) { *p=e->next; destroy_now(e->backend,e->pixels); free(e); }
        else p=&e->next;
    }
}
void athena_texture3d_collect(int force) {
    if(!pending) return;
    if(force) graphics_wait_idle();
    collect_ready(SIZE_MAX);
}
/* Existing completed-frame hook: no new fence, and at most eight entries
 * visited/freed per frame. Insertion still performs the existing full scan,
 * so young entries cannot starve older ready entries behind this budget.
 * Keep the existing predicate and surface release's DMA-read protection. */
void athena_texture3d_collect_frame(void) {
    if(pending) collect_ready(8);
}
void athena_texture3d_backend_destroy(void *backend,void *pixels) {
    Backend *b=backend;
    /* Never resident (or dropped by a mode reset): nothing samples it. */
    if(!b||!b->surface.Vram) { destroy_now(b,pixels); return; }
    Pending *e=malloc(sizeof(*e));
    if(!e) { graphics_wait_idle(); destroy_now(b,pixels); return; }
    *e=(Pending){b,pixels,graphics_finished_frames(),graphics_idle_count(),pending};
    pending=e;
    athena_texture3d_collect(0);
}
/* Waits for the GS and frees every deferred texture release. */
void athena_model3d_module_shutdown(void) { athena_texture3d_collect(1); }

void athena_texture3d_retirement_stats(AthenaTextureRetirementStats *out) {
    if(!out) return;
    memset(out,0,sizeof(*out));
    uint32_t frames=graphics_finished_frames(),idle=graphics_idle_count();
    for(Pending *e=pending;e;e=e->next) {
        out->resources++;
        if(frames-e->frame>=2||idle!=e->idle) out->ready_resources++;
        Backend *b=e->backend;
        if(e->pixels && b) out->cpu_payload+=(size_t)b->surface.Width*b->surface.Height*4;
        if(b && b->indexed) {
            size_t pixels=(size_t)b->surface.Width*b->surface.Height;
            out->cpu_payload+=b->surface.PSM==GS_PSM_T4?(pixels+1)/2:pixels;
        }
        if(b && b->surface.Clut) out->cpu_payload+=b->surface.PSM==GS_PSM_T8?1024:64;
    }
}
