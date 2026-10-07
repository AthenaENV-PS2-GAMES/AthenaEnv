/* Resource stand-in only. Production image decoding, VRAM and GS fences are
 * tested separately; this exercises ownership with the real mesh/bindings. */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <athena/model3d.h>
#include "../../src/modules/model3d/native/texture3d_backend.h"
unsigned texture3d_host_created,texture3d_host_destroyed;
int athena_texture3d_decode(const char *path,uint32_t **p,uint32_t *w,uint32_t *h) {
    /* Only the deterministic fixture: inspect IHDR and supply white test
     * pixels. This deliberately does not claim to implement PNG decoding. */
    size_t length=strlen(path);
    if(length<11||strcmp(path+length-11,"checker.png")) return ATHENA_MODEL3D_EUNSUPPORTED;
    FILE *file=fopen(path,"rb"); if(!file) return ATHENA_MODEL3D_EIO;
    unsigned char header[24]; size_t n=fread(header,1,sizeof(header),file); fclose(file);
    if(n!=24||memcmp(header,"\211PNG\r\n\032\n",8)) return ATHENA_MODEL3D_EFORMAT;
    *w=(uint32_t)header[16]<<24|(uint32_t)header[17]<<16|(uint32_t)header[18]<<8|header[19];
    *h=(uint32_t)header[20]<<24|(uint32_t)header[21]<<16|(uint32_t)header[22]<<8|header[23];
    if(!*w||!*h||*w>512||*h>512) return ATHENA_MODEL3D_EUNSUPPORTED;
    uint32_t count=(*w)*(*h);
    *p=malloc(count*sizeof(**p)); if(!*p) return ATHENA_MODEL3D_ENOMEM;
    for(uint32_t i=0;i<count;i++) (*p)[i]=0x80ffffffu;
    return 0;
}
/* Embedded images: the same fixture, read from the PNG header in memory. */
int athena_texture3d_decode_memory(const void *data,size_t size,uint32_t **p,uint32_t *w,uint32_t *h) {
    const unsigned char *header=data;
    if(size<24||memcmp(header,"\211PNG\r\n\032\n",8)) return ATHENA_MODEL3D_EFORMAT;
    *w=(uint32_t)header[16]<<24|(uint32_t)header[17]<<16|(uint32_t)header[18]<<8|header[19];
    *h=(uint32_t)header[20]<<24|(uint32_t)header[21]<<16|(uint32_t)header[22]<<8|header[23];
    if(!*w||!*h||*w>512||*h>512) return ATHENA_MODEL3D_EUNSUPPORTED;
    *p=malloc((*w)*(*h)*sizeof(**p)); if(!*p) return ATHENA_MODEL3D_ENOMEM;
    for(uint32_t i=0;i<(*w)*(*h);i++) (*p)[i]=0x80ffffffu;
    return 0;
}
void *athena_texture3d_backend_create(const AthenaTexture3DPixels *p) {
    AthenaTexture3DPixels *copy=malloc(sizeof(*copy));
    if(copy) { *copy=*p; texture3d_host_created++; } return copy;
}
int athena_texture3d_backend_bind(void *p,AthenaTexture3DBinding *b) {
    const AthenaTexture3DPixels *view=p;
    b->tex0=0x1000+view->width; b->tex1=view->filter; b->clamp=view->height; return 0;
}
void athena_texture3d_backend_destroy(void *p,void *pixels) { if(p) { texture3d_host_destroyed++; free(p); } free(pixels); }
void athena_texture3d_collect(int force) { (void)force; }
void athena_model3d_module_shutdown(void) {}
