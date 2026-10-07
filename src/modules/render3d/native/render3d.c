#include <stdlib.h>
#include <string.h>
#include <athena/render3d.h>
struct AthenaBatch3D { AthenaInstance3D **items; uint32_t count,capacity; };
/* Why the last draw failed. Static strings only: no formatting per draw. */
static const char *error_detail="";
const char *athena_render3d_error_detail(void) { return error_detail; }
void athena_render3d_set_error_detail(const char *detail) { error_detail=detail?detail:""; }
AthenaBatch3D *athena_batch3d_create(void) { return calloc(1,sizeof(AthenaBatch3D)); }
void athena_batch3d_clear(AthenaBatch3D *b) {
    if(!b) return;
    for(uint32_t i=0;i<b->count;i++) athena_instance3d_release(b->items[i]);
    b->count=0;
}
void athena_batch3d_destroy(AthenaBatch3D *b) {
    if(!b) return;
    athena_batch3d_clear(b); free(b->items); free(b);
}
int athena_batch3d_add(AthenaBatch3D *b,AthenaInstance3D *i) {
    if(!b||!i||b->count>=ATHENA_MODEL3D_MAX_VERTICES) return -1;
    if(b->count==b->capacity) {
        uint32_t next=b->capacity?b->capacity*2:16;
        void *items=realloc(b->items,next*sizeof(*b->items)); if(!items) return -2;
        b->items=items; b->capacity=next;
    }
    b->items[b->count++]=i; athena_instance3d_retain(i); return 0;
}
uint32_t athena_batch3d_size(const AthenaBatch3D *b) { return b?b->count:0; }
int athena_batch3d_draw_lit(AthenaBatch3D *b,AthenaCamera3D *c,const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *s) {
    if(!b||!c||!s||(cull!=0&&cull!=1&&cull!=-1)) { athena_render3d_set_error_detail("invalid batch, camera or cull mode"); return -1; }
    memset(s,0,sizeof(*s));
    int owned=athena_render3d_group_begin()==0,result=0;
    for(uint32_t n=0;n<b->count&&result>=0;n++) result=athena_render3d_draw_lit(b->items[n],c,lights,cull,s);
    if(owned) athena_render3d_group_end();
    return result<0?result:0;
}
int athena_batch3d_draw(AthenaBatch3D *b,AthenaCamera3D *c,AthenaRender3DCull cull,AthenaRender3DStats *s) {
    return athena_batch3d_draw_lit(b,c,NULL,cull,s);
}
