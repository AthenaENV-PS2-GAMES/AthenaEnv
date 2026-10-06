#include <stdlib.h>
#include <string.h>
#include <athena/image.h>
#include <athena/model3d.h>
#include "texture3d_backend.h"
int athena_texture3d_decode(const char *path,uint32_t **pixels,uint32_t *width,uint32_t *height) {
    AthenaImageBuffer buffer;
    if(athena_image_decode(path,&buffer)<0)
        return buffer.error==ATHENA_IMAGE_LOAD_OPEN?ATHENA_MODEL3D_EIO:ATHENA_MODEL3D_EFORMAT;
    int result=ATHENA_MODEL3D_EUNSUPPORTED;
    if(buffer.width>ATHENA_TEXTURE3D_MAX_SIZE||buffer.height>ATHENA_TEXTURE3D_MAX_SIZE||
        (buffer.psm!=GS_PSM_CT32&&buffer.psm!=GS_PSM_CT24)) goto done;
    uint32_t count=buffer.width*buffer.height;
    uint32_t *copy=malloc(count*sizeof(*copy));
    if(!copy) { result=ATHENA_MODEL3D_ENOMEM; goto done; }
    for(uint32_t i=0;i<count;i++) {
        if(buffer.psm==GS_PSM_CT32) copy[i]=buffer.mem[i];
        else {
            const uint8_t *p=(const uint8_t *)buffer.mem+i*3;
            copy[i]=p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|0x80000000u;
        }
    }
    *pixels=copy; *width=buffer.width; *height=buffer.height; result=0;
done:
    athena_image_buffer_release(&buffer); return result;
}
void *athena_texture3d_backend_create(const AthenaTexture3DPixels *p) {
    GSSURFACE *s=malloc(sizeof(*s)); if(!s) return NULL;
    if(graphics_surface_init(s)<0) { free(s); return NULL; }
    s->Width=p->width; s->Height=p->height; s->PSM=GS_PSM_CT32;
    s->Filter=p->filter==ATHENA_TEXTURE3D_LINEAR?GS_FILTER_LINEAR:GS_FILTER_NEAREST;
    s->Mem=(uint32_t *)p->pixels; /* Borrowed from the owning Texture3D. */
    return s;
}
int athena_texture3d_backend_bind(void *backend,AthenaTexture3DBinding *out) {
    GSSURFACE *s=backend;
    if(!graphics_surface_is_locked(s)) {
        /* Do not reuse/evict a VRAM block while queued draws still sample it.
         * Synchronous upload plus a persistent lock avoids async markers. */
        graphics_wait_idle();
        if(graphics_surface_bind_sync(s)==GRAPHICS_BIND_ERROR||!graphics_surface_lock(s))
            return ATHENA_MODEL3D_EVRAM;
    }
    int tw,th; athena_set_tw_th(s,&tw,&th);
    out->tex0=GS_SETREG_TEX0(s->Vram/256,s->TBW,s->PSM,tw,th,0,COLOR_MODULATE,0,GS_PSM_CT32,0,0,0);
    out->tex1=GS_SETREG_TEX1(1,0,s->Filter,s->Filter,0,0,0);
    out->clamp=GS_SETREG_CLAMP(1,1,0,s->Width-1,0,s->Height-1);
    return 0;
}
void athena_texture3d_backend_destroy(void *backend) {
    GSSURFACE *s=backend; if(!s) return;
    if(s->Vram) graphics_wait_idle();
    graphics_surface_release(s); free(s);
}
