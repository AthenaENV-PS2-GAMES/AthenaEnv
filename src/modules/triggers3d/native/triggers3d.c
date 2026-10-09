#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <athena/triggers3d.h>
#include <athena/float_bits.h>
typedef struct {
    uint8_t used,enabled,shape; uint32_t mask;
    float a[3],b[3];          /* as given (relative to the node when following) */
    AthenaNode3D *node; float offset[3];
} Zone;
typedef struct {
    uint8_t used; uint32_t layers;
    float p[3],radius;
    AthenaNode3D *node; float offset[3];
} Body;
struct AthenaTriggers3D {
    Zone *zones; Body *bodies;
    uint32_t zone_cap,body_cap,zone_end,body_end;  /* end: one past the highest used id */
    uint8_t *inside;                                /* zone_cap * body_cap bits as bytes */
    AthenaTrigger3DEvent *events; uint32_t event_count,event_cap;
};
AthenaTriggers3D *athena_triggers3d_create(void) { return calloc(1,sizeof(AthenaTriggers3D)); }
void athena_triggers3d_destroy(AthenaTriggers3D *t) {
    if(!t) return;
    for(uint32_t i=0;i<t->zone_end;i++) athena_node3d_release(t->zones[i].node);
    for(uint32_t i=0;i<t->body_end;i++) athena_node3d_release(t->bodies[i].node);
    free(t->zones); free(t->bodies); free(t->inside); free(t->events); free(t);
}
static int finite3(const float v[3]) { return athena_float_isfinite(v[0])&&athena_float_isfinite(v[1])&&athena_float_isfinite(v[2]); }
static int shape_ok(AthenaTrigger3DShape s,const float a[3],const float b[3]) {
    if(!a||!b||!finite3(a)) return 0;
    if(s==ATHENA_TRIGGER3D_BOX) return finite3(b)&&a[0]<=b[0]&&a[1]<=b[1]&&a[2]<=b[2];
    return s==ATHENA_TRIGGER3D_SPHERE&&athena_float_isfinite(b[0])&&b[0]>=0;
}
/* Grows the pair matrix keeping the bytes of existing pairs. */
static int resize(AthenaTriggers3D *t,uint32_t zc,uint32_t bc) {
    uint8_t *m=calloc((size_t)zc*bc,1); if(!m) return 0;
    for(uint32_t z=0;z<t->zone_cap&&z<zc;z++) for(uint32_t b=0;b<t->body_cap&&b<bc;b++) m[z*bc+b]=t->inside[z*t->body_cap+b];
    free(t->inside); t->inside=m; return 1;
}
static int push_event(AthenaTriggers3D *t,uint8_t type,uint32_t z,uint32_t b) {
    if(t->event_count==t->event_cap) {
        uint32_t next=t->event_cap?t->event_cap*2:32;
        AthenaTrigger3DEvent *e=realloc(t->events,next*sizeof(*e)); if(!e) return 0;
        t->events=e; t->event_cap=next;
    }
    t->events[t->event_count++]=(AthenaTrigger3DEvent){type,(uint16_t)z,(uint16_t)b};
    return 1;
}
int athena_triggers3d_add_zone(AthenaTriggers3D *t,AthenaTrigger3DShape shape,const float a[3],const float b[3],uint32_t mask) {
    if(!t||!shape_ok(shape,a,b)) return -1;
    uint32_t id=0; while(id<t->zone_end&&t->zones[id].used) id++;
    if(id>=ATHENA_TRIGGERS3D_MAX) return -1;
    if(id>=t->zone_cap) {
        uint32_t next=t->zone_cap?t->zone_cap*2:16;
        Zone *z=realloc(t->zones,next*sizeof(*z)); if(!z) return -1;
        t->zones=z;
        if(!resize(t,next,t->body_cap)) return -1;
        memset(t->zones+t->zone_cap,0,(next-t->zone_cap)*sizeof(*z)); t->zone_cap=next;
    }
    Zone *z=&t->zones[id]; memset(z,0,sizeof(*z));
    z->used=z->enabled=1; z->shape=(uint8_t)shape; z->mask=mask;
    memcpy(z->a,a,12); if(shape==ATHENA_TRIGGER3D_BOX) memcpy(z->b,b,12); else z->b[0]=b[0];
    memset(&t->inside[id*t->body_cap],0,t->body_cap);
    if(id>=t->zone_end) t->zone_end=id+1;
    return (int)id;
}
static Zone *zone_at(const AthenaTriggers3D *t,int id) { return t&&id>=0&&(uint32_t)id<t->zone_end&&t->zones[id].used?&t->zones[id]:NULL; }
static Body *body_at(const AthenaTriggers3D *t,int id) { return t&&id>=0&&(uint32_t)id<t->body_end&&t->bodies[id].used?&t->bodies[id]:NULL; }
int athena_triggers3d_set_zone(AthenaTriggers3D *t,int id,AthenaTrigger3DShape shape,const float a[3],const float b[3]) {
    Zone *z=zone_at(t,id); if(!z||!shape_ok(shape,a,b)) return -1;
    z->shape=(uint8_t)shape; memcpy(z->a,a,12); if(shape==ATHENA_TRIGGER3D_BOX) memcpy(z->b,b,12); else z->b[0]=b[0];
    return 0;
}
int athena_triggers3d_add_body(AthenaTriggers3D *t,const float p[3],float radius,uint32_t layers) {
    if(!t||!p||!finite3(p)||!athena_float_isfinite(radius)||radius<0) return -1;
    uint32_t id=0; while(id<t->body_end&&t->bodies[id].used) id++;
    if(id>=ATHENA_TRIGGERS3D_MAX) return -1;
    if(id>=t->body_cap) {
        uint32_t next=t->body_cap?t->body_cap*2:16;
        Body *b=realloc(t->bodies,next*sizeof(*b)); if(!b) return -1;
        t->bodies=b;
        if(!resize(t,t->zone_cap,next)) return -1;
        memset(t->bodies+t->body_cap,0,(next-t->body_cap)*sizeof(*b)); t->body_cap=next;
    }
    Body *b=&t->bodies[id]; memset(b,0,sizeof(*b));
    b->used=1; b->layers=layers; memcpy(b->p,p,12); b->radius=radius;
    for(uint32_t z=0;z<t->zone_cap;z++) t->inside[z*t->body_cap+id]=0;
    if(id>=t->body_end) t->body_end=id+1;
    return (int)id;
}
int athena_triggers3d_set_body(AthenaTriggers3D *t,int id,const float p[3],float radius) {
    Body *b=body_at(t,id); if(!b||!p||!finite3(p)||!athena_float_isfinite(radius)||radius<0) return -1;
    memcpy(b->p,p,12); b->radius=radius; return 0;
}
int athena_triggers3d_remove_zone(AthenaTriggers3D *t,int id) {
    Zone *z=zone_at(t,id); if(!z) return -1;
    memset(&t->inside[(uint32_t)id*t->body_cap],0,t->body_cap);
    athena_node3d_release(z->node); memset(z,0,sizeof(*z)); return 0;
}
int athena_triggers3d_remove_body(AthenaTriggers3D *t,int id) {
    Body *b=body_at(t,id); if(!b) return -1;
    for(uint32_t z=0;z<t->zone_cap;z++) t->inside[z*t->body_cap+(uint32_t)id]=0;
    athena_node3d_release(b->node); memset(b,0,sizeof(*b)); return 0;
}
int athena_triggers3d_set_zone_enabled(AthenaTriggers3D *t,int id,int enabled) { Zone *z=zone_at(t,id); if(!z) return -1; z->enabled=enabled!=0; return 0; }
int athena_triggers3d_set_mask(AthenaTriggers3D *t,int id,uint32_t mask) { Zone *z=zone_at(t,id); if(!z) return -1; z->mask=mask; return 0; }
int athena_triggers3d_set_layers(AthenaTriggers3D *t,int id,uint32_t layers) { Body *b=body_at(t,id); if(!b) return -1; b->layers=layers; return 0; }
static int follow(AthenaNode3D **slot,float off[3],AthenaNode3D *node,const float offset[3]) {
    if(offset&&!finite3(offset)) return -1;
    athena_node3d_retain(node); athena_node3d_release(*slot); *slot=node;
    if(offset) memcpy(off,offset,12); else off[0]=off[1]=off[2]=0;
    return 0;
}
int athena_triggers3d_zone_follow(AthenaTriggers3D *t,int id,AthenaNode3D *node,const float offset[3]) {
    Zone *z=zone_at(t,id); return z?follow(&z->node,z->offset,node,offset):-1;
}
int athena_triggers3d_body_follow(AthenaTriggers3D *t,int id,AthenaNode3D *node,const float offset[3]) {
    Body *b=body_at(t,id); return b?follow(&b->node,b->offset,node,offset):-1;
}
static int overlaps(const Zone *z,const float zo[3],const float p[3],float r) {
    if(z->shape==ATHENA_TRIGGER3D_SPHERE) {
        float dx=p[0]-(z->a[0]+zo[0]),dy=p[1]-(z->a[1]+zo[1]),dz=p[2]-(z->a[2]+zo[2]),rr=z->b[0]+r;
        return dx*dx+dy*dy+dz*dz<=rr*rr;
    }
    float d2=0;
    for(int i=0;i<3;i++) {
        float lo=z->a[i]+zo[i],hi=z->b[i]+zo[i],v=p[i]<lo?lo-p[i]:p[i]>hi?p[i]-hi:0;
        d2+=v*v;
    }
    return d2<=r*r;
}
int athena_triggers3d_update(AthenaTriggers3D *t) {
    if(!t) return -1;
    t->event_count=0;
    for(uint32_t zi=0;zi<t->zone_end;zi++) {
        Zone *z=&t->zones[zi];
        if(!z->used) continue;
        float zo[3]={0,0,0};
        if(z->node) { athena_node3d_last_world_position(z->node,zo); for(int i=0;i<3;i++) zo[i]+=z->offset[i]; }
        uint8_t *row=&t->inside[zi*t->body_cap];
        for(uint32_t bi=0;bi<t->body_end;bi++) {
            Body *b=&t->bodies[bi];
            if(!b->used) continue;
            float p[3]={b->p[0],b->p[1],b->p[2]};
            if(b->node) { athena_node3d_last_world_position(b->node,p); for(int i=0;i<3;i++) p[i]+=b->offset[i]; }
            int now=z->enabled&&(z->mask&b->layers)&&overlaps(z,zo,p,b->radius);
            if(now!=row[bi]) {
                row[bi]=(uint8_t)now;
                if(!push_event(t,now?ATHENA_TRIGGER3D_ENTER:ATHENA_TRIGGER3D_EXIT,zi,bi)) return -1;
            }
        }
    }
    return (int)t->event_count;
}
const AthenaTrigger3DEvent *athena_triggers3d_events(const AthenaTriggers3D *t,uint32_t *count) {
    if(count) *count=t?t->event_count:0;
    return t?t->events:NULL;
}
int athena_triggers3d_inside(const AthenaTriggers3D *t,int zone,int body) {
    return zone_at(t,zone)&&body_at(t,body)&&t->inside[(uint32_t)zone*t->body_cap+(uint32_t)body];
}
int athena_triggers3d_occupants(const AthenaTriggers3D *t,int zone,uint16_t *out,uint32_t max) {
    if(!zone_at(t,zone)) return -1;
    uint32_t n=0;
    for(uint32_t b=0;b<t->body_end;b++) if(t->bodies[b].used&&t->inside[(uint32_t)zone*t->body_cap+b]) { if(n<max&&out) out[n]=(uint16_t)b; n++; }
    return (int)n;
}
