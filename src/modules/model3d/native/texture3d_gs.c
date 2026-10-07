#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <athena/image.h>
#include <athena/model3d.h>
#include <athena/graphics/sync.h>
#include "texture3d_backend.h"
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
static int expand_buffer(AthenaImageBuffer *buffer,uint32_t **pixels,uint32_t *width,uint32_t *height);
int athena_texture3d_decode(const char *path,uint32_t **pixels,uint32_t *width,uint32_t *height) {
    AthenaImageBuffer buffer;
    if(athena_image_decode(path,&buffer)<0)
        return buffer.error==ATHENA_IMAGE_LOAD_OPEN?ATHENA_MODEL3D_EIO:ATHENA_MODEL3D_EFORMAT;
    return expand_buffer(&buffer,pixels,width,height);
}
int athena_texture3d_decode_memory(const void *data,size_t size,uint32_t **pixels,uint32_t *width,uint32_t *height) {
    AthenaImageBuffer buffer;
    if(athena_image_decode_memory(data,size,&buffer)<0) {
        athena_model3d_set_detail("embedded image is not a decodable PNG/JPEG/BMP");
        return ATHENA_MODEL3D_EFORMAT;
    }
    return expand_buffer(&buffer,pixels,width,height);
}
/* Consumes buffer: 32-bit pixels from any decoded format. */
static int expand_buffer(AthenaImageBuffer *b,uint32_t **pixels,uint32_t *width,uint32_t *height) {
    AthenaImageBuffer buffer=*b;
    int result=ATHENA_MODEL3D_EUNSUPPORTED;
    int paletted=buffer.psm==GS_PSM_T8||buffer.psm==GS_PSM_T4;
    if(buffer.width>ATHENA_TEXTURE3D_MAX_SIZE||buffer.height>ATHENA_TEXTURE3D_MAX_SIZE) {
        athena_model3d_set_detail("texture %ux%u exceeds %u",(unsigned)buffer.width,(unsigned)buffer.height,(unsigned)ATHENA_TEXTURE3D_MAX_SIZE);
        goto done;
    }
    if((buffer.psm!=GS_PSM_CT32&&buffer.psm!=GS_PSM_CT24&&buffer.psm!=GS_PSM_CT16&&!paletted)||
        (paletted&&(!buffer.clut||buffer.clut_psm!=GS_PSM_CT32))) {
        athena_model3d_set_detail("texture pixel format 0x%x is not supported",(unsigned)buffer.psm);
        goto done;
    }
    uint32_t count=buffer.width*buffer.height;
    uint32_t *copy=malloc(count*sizeof(*copy));
    if(!copy) { result=ATHENA_MODEL3D_ENOMEM; goto done; }
    const uint8_t *bytes=(const uint8_t *)buffer.mem;
    for(uint32_t i=0;i<count;i++) {
        /* The loaders store GS alpha (0..0x80); Texture3D pixels use 0..255. */
        if(buffer.psm==GS_PSM_CT32) copy[i]=gs_to_alpha(buffer.mem[i]);
        else if(buffer.psm==GS_PSM_CT24) {
            const uint8_t *p=bytes+i*3;
            copy[i]=p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|0xff000000u;
        } else if(buffer.psm==GS_PSM_CT16) copy[i]=expand16((uint16_t)(bytes[i*2]|bytes[i*2+1]<<8));
        else if(buffer.psm==GS_PSM_T8) copy[i]=gs_to_alpha(buffer.clut[clut_slot(bytes[i])]);
        else copy[i]=gs_to_alpha(buffer.clut[(bytes[i/2]>>((i&1)*4))&15]); /* T4: low nibble first */
    }
    *pixels=copy; *width=buffer.width; *height=buffer.height; result=0;
done:
    athena_image_buffer_release(&buffer); return result;
}
/* The surface first: the texture manager only ever sees &backend->surface. */
typedef struct { GSSURFACE surface; AthenaTexture3DWrap wrap; void *indexed; } Backend;
/* Keep the canonical CT32 view, but upload an exact indexed copy when the
 * combined texture/CLUT allocation is smaller. Alpha participates in identity.
 * Allocation failure is harmless: the borrowed CT32 surface remains usable. */
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
    int psm=count<=16?GS_PSM_T4:GS_PSM_T8;
    int cw=psm==GS_PSM_T4?8:16,ch=psm==GS_PSM_T4?2:16;
    uint32_t cost=athena_vram_surface_size(p->width,p->height,psm)+athena_vram_surface_size(cw,ch,GS_PSM_CT32);
    if(cost>=athena_vram_surface_size(p->width,p->height,GS_PSM_CT32)) return;
    size_t bytes=psm==GS_PSM_T4?(p->pixel_count+1)/2:p->pixel_count;
    bytes=(bytes+15)&~(size_t)15;
    /* The manager flushes the whole CLUT VRAM allocation, including padding. */
    size_t clut_bytes=athena_vram_surface_size(cw,ch,GS_PSM_CT32);
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
void *athena_texture3d_backend_create(const AthenaTexture3DPixels *p) {
    Backend *b=malloc(sizeof(*b)); if(!b) return NULL;
    GSSURFACE *s=&b->surface;
    if(graphics_surface_init(s)<0) { free(b); return NULL; }
    s->Width=p->width; s->Height=p->height; s->PSM=GS_PSM_CT32;
    s->Filter=p->filter==ATHENA_TEXTURE3D_LINEAR?GS_FILTER_LINEAR:GS_FILTER_NEAREST;
    s->Mem=(uint32_t *)p->pixels; /* Borrowed from the owning Texture3D. */
    b->wrap=p->wrap; b->indexed=NULL;
    index_surface(b,p);
    return b;
}
int athena_texture3d_backend_bind(void *backend,AthenaTexture3DBinding *out) {
    GSSURFACE *s=&((Backend *)backend)->surface; AthenaTexture3DWrap wrap=((Backend *)backend)->wrap;
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
void athena_texture3d_collect(int force) {
    if(!pending) return;
    if(force) graphics_wait_idle();
    uint32_t frames=graphics_finished_frames(),idle=graphics_idle_count();
    for(Pending **p=&pending;*p;) {
        Pending *e=*p;
        if(frames-e->frame>=2||idle!=e->idle) { *p=e->next; destroy_now(e->backend,e->pixels); free(e); }
        else p=&e->next;
    }
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
