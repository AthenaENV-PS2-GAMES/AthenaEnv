#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <athena/texture3d.h>
#include <athena/texture_decode_job.h>
typedef struct {
    char *path; size_t max;
    /* Under athena_job_lock(): written by the worker, taken by the script thread. */
    uint32_t *pixels; AthenaTexture3DIndexed *indexed; uint32_t width,height;
} TextureDecode;
static int decode_run(AthenaJob *job,void *data) {
    TextureDecode *d=data;
    FILE *file=fopen(d->path,"rb");
    if(!file) return -3; /* ATHENA_MODEL3D_EIO */
    long size=fseek(file,0,SEEK_END)==0?ftell(file):-1;
    fclose(file);
    if(size<0) return -3;
    if((unsigned long)size>d->max) return ATHENA_TEXTURE_DECODE_TOO_LARGE;
    if(athena_job_should_stop(job)) return ATHENA_TEXTURE_DECODE_CANCELLED;
    uint32_t *pixels=NULL,w=0,h=0; AthenaTexture3DIndexed *indexed=NULL;
    int code=athena_texture3d_decode_prepared_indexed(d->path,&pixels,&indexed,&w,&h);
    if(code<0) return code;
    if(athena_job_should_stop(job)) { free(pixels); athena_texture3d_indexed_free(indexed); return ATHENA_TEXTURE_DECODE_CANCELLED; }
    athena_job_lock(job); d->pixels=pixels; d->indexed=indexed; d->width=w; d->height=h; athena_job_unlock(job);
    return 0;
}
static void decode_release(void *data) { TextureDecode *d=data; free(d->path); free(d->pixels); athena_texture3d_indexed_free(d->indexed); free(d); }
static const AthenaJobType decode_type={"Texture decode",decode_run,decode_release,ATHENA_TEXTURE_DECODE_CANCELLED,ATHENA_JOB_PRIORITY_CPU};
AthenaJob *athena_texture_decode_submit(const char *path,size_t max_bytes) {
    if(!path||!*path) return NULL;
    TextureDecode *d=calloc(1,sizeof(*d));
    if(!d) return NULL;
    d->path=malloc(strlen(path)+1);
    if(!d->path) { free(d); return NULL; }
    strcpy(d->path,path); d->max=max_bytes?max_bytes:16u*1024u*1024u;
    AthenaJob *job=athena_job_submit(&decode_type,d);
    return job;
}
uint32_t *athena_texture_decode_take_indexed(AthenaJob *job,AthenaTexture3DIndexed **indexed,uint32_t *width,uint32_t *height) {
    TextureDecode *d=athena_job_data(job);
    athena_job_lock(job);
    uint32_t *pixels=d->pixels; *width=d->width; *height=d->height; d->pixels=NULL;
    if(indexed) { *indexed=pixels?d->indexed:NULL; if(pixels) d->indexed=NULL; }
    athena_job_unlock(job);
    return pixels;
}
/* Pixel-only take: any derived index streams stay with the job and are freed at its release. */
uint32_t *athena_texture_decode_take(AthenaJob *job,uint32_t *width,uint32_t *height) {
    return athena_texture_decode_take_indexed(job,NULL,width,height);
}
