#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <athena/float_bits.h>
#include <athena/loop.h>
#include <athena/particles3d.h>
#include "particles3d_backend.h"
typedef struct { float x,y,z,vx,vy,vz,age,inv_life; } Particle;
struct AthenaEmitter3D {
    uint64_t refs;
    AthenaEmitter3DDesc desc;
    float basis[2][3];  /* unit vectors orthogonal to the cone axis */
    Particle *particles; uint32_t count;
    float x,y,z,carry;
    uint32_t random;
    int active;
    struct AthenaEmitter3D *prev,*next;
};
static AthenaEmitter3D *emitters;
static int loop_id;
static void *loop_owner;
static uintptr_t loop_generation;

void athena_emitter3d_defaults(AthenaEmitter3DDesc *d) {
    memset(d,0,sizeof(*d));
    d->capacity=256; d->life_min=d->life_max=1; d->size_start=d->size_end=.5f;
    d->direction[1]=1; d->color_start=d->color_end=0x80ffffffu;
}
static int finite_all(const float *v,unsigned n) {
    for(unsigned i=0;i<n;i++) if(!athena_float_isfinite(v[i])) return 0;
    return 1;
}
static int valid(const AthenaEmitter3DDesc *d) {
    const float v[]={d->rate,d->life_min,d->life_max,d->speed_min,d->speed_max,d->direction[0],d->direction[1],
        d->direction[2],d->spread,d->gravity[0],d->gravity[1],d->gravity[2],d->drag,d->size_start,d->size_end,
        d->area[0],d->area[1],d->area[2],d->u1,d->v1,d->u2,d->v2};
    return d->capacity>=1&&d->capacity<=ATHENA_PARTICLES3D_MAX&&finite_all(v,sizeof(v)/sizeof(v[0]))&&
        d->rate>=0&&d->life_min>0&&d->life_max>=d->life_min&&d->speed_max>=d->speed_min&&d->drag>=0&&
        d->size_start>=0&&d->size_end>=0&&d->area[0]>=0&&d->area[1]>=0&&d->area[2]>=0&&
        d->spread>=0&&d->spread<=3.14159265358979323846f;
}
int athena_emitter3d_configure(AthenaEmitter3D *e,const AthenaEmitter3DDesc *src) {
    if(!e||!src||!valid(src)) return ATHENA_PARTICLES3D_EINVAL;
    AthenaEmitter3DDesc d=*src;
    float *a=d.direction,len=sqrtf(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);
    if(!(len>1e-6f)) return ATHENA_PARTICLES3D_EINVAL;
    for(int i=0;i<3;i++) a[i]/=len;
    if(d.capacity!=e->desc.capacity||!e->particles) {
        Particle *p=realloc(e->particles,d.capacity*sizeof(*p)); if(!p) return ATHENA_PARTICLES3D_ENOMEM;
        e->particles=p; if(e->count>d.capacity) e->count=d.capacity;
    }
    if(d.seed!=e->desc.seed||!e->random) e->random=d.seed?d.seed:0x9e3779b9u;
    /* Basis of the cone: any vector not parallel to the axis, made orthogonal. */
    float helper[3]={fabsf(a[0])<.9f?1.0f:0.0f,fabsf(a[0])<.9f?0.0f:1.0f,0};
    float *b0=e->basis[0],*b1=e->basis[1];
    b0[0]=a[1]*helper[2]-a[2]*helper[1]; b0[1]=a[2]*helper[0]-a[0]*helper[2]; b0[2]=a[0]*helper[1]-a[1]*helper[0];
    float n=sqrtf(b0[0]*b0[0]+b0[1]*b0[1]+b0[2]*b0[2]); for(int i=0;i<3;i++) b0[i]/=n;
    b1[0]=a[1]*b0[2]-a[2]*b0[1]; b1[1]=a[2]*b0[0]-a[0]*b0[2]; b1[2]=a[0]*b0[1]-a[1]*b0[0];
    e->desc=d; return 0;
}
int athena_emitter3d_create(const AthenaEmitter3DDesc *d,AthenaEmitter3D **out) {
    if(!out) return ATHENA_PARTICLES3D_EINVAL;
    *out=NULL;
    AthenaEmitter3D *e=calloc(1,sizeof(*e)); if(!e) return ATHENA_PARTICLES3D_ENOMEM;
    int code=athena_emitter3d_configure(e,d);
    if(code<0) { free(e->particles); free(e); return code; }
    e->refs=1; e->active=1;
    e->next=emitters; if(emitters) emitters->prev=e; emitters=e;
    *out=e; return 0;
}
const AthenaEmitter3DDesc *athena_emitter3d_desc(const AthenaEmitter3D *e) { return &e->desc; }
void athena_emitter3d_retain(AthenaEmitter3D *e) { if(e) e->refs++; }
void athena_emitter3d_release(AthenaEmitter3D *e) {
    if(!e||--e->refs) return;
    if(e->prev) e->prev->next=e->next; else emitters=e->next;
    if(e->next) e->next->prev=e->prev;
    free(e->particles); free(e);
}
int athena_emitter3d_set_position(AthenaEmitter3D *e,float x,float y,float z) {
    if(!e||!athena_float_isfinite(x)||!athena_float_isfinite(y)||!athena_float_isfinite(z)) return ATHENA_PARTICLES3D_EINVAL;
    e->x=x; e->y=y; e->z=z; return 0;
}
void athena_emitter3d_set_active(AthenaEmitter3D *e,int active) { if(e) { e->active=!!active; if(!active) e->carry=0; } }
int athena_emitter3d_active(const AthenaEmitter3D *e) { return e&&e->active; }
void athena_emitter3d_clear(AthenaEmitter3D *e) { if(e) e->count=0; }
uint32_t athena_emitter3d_count(const AthenaEmitter3D *e) { return e?e->count:0; }
static float random01(AthenaEmitter3D *e) {
    uint32_t x=e->random; x^=x<<13; x^=x>>17; x^=x<<5; e->random=x;
    return (x>>8)*(1.0f/16777216.0f);
}
static float between(AthenaEmitter3D *e,float lo,float hi) { return lo==hi?lo:lo+(hi-lo)*random01(e); }
uint32_t athena_emitter3d_emit(AthenaEmitter3D *e,uint32_t count) {
    if(!e) return 0;
    const AthenaEmitter3DDesc *d=&e->desc;
    uint32_t room=d->capacity-e->count; if(count>room) count=room;
    for(uint32_t i=0;i<count;i++) {
        Particle *p=&e->particles[e->count++];
        p->x=e->x+(random01(e)-.5f)*d->area[0]; p->y=e->y+(random01(e)-.5f)*d->area[1];
        p->z=e->z+(random01(e)-.5f)*d->area[2];
        float s=between(e,d->speed_min,d->speed_max),dir[3];
        if(d->spread>0) {
            /* Uniform over the cone's cap: cos(theta) uniform in [cos(spread), 1]. */
            float ct=1-random01(e)*(1-cosf(d->spread)),st=sqrtf(1-ct*ct),phi=random01(e)*6.28318530717958647692f;
            float cp=cosf(phi)*st,sp=sinf(phi)*st;
            for(int k=0;k<3;k++) dir[k]=d->direction[k]*ct+e->basis[0][k]*cp+e->basis[1][k]*sp;
        } else for(int k=0;k<3;k++) dir[k]=d->direction[k];
        p->vx=dir[0]*s; p->vy=dir[1]*s; p->vz=dir[2]*s;
        p->age=0; p->inv_life=1/between(e,d->life_min,d->life_max);
    }
    return count;
}
int athena_emitter3d_update(AthenaEmitter3D *e,float dt) {
    if(!e||!athena_float_isfinite(dt)||dt<0) return ATHENA_PARTICLES3D_EINVAL;
    const AthenaEmitter3DDesc *d=&e->desc;
    float g[3]={d->gravity[0]*dt,d->gravity[1]*dt,d->gravity[2]*dt},keep=d->drag>0?1/(1+d->drag*dt):1;
    uint32_t w=0;
    for(uint32_t i=0;i<e->count;i++) {
        Particle p=e->particles[i];
        p.age+=dt;
        if(p.age*p.inv_life>=1) continue;
        p.vx=(p.vx+g[0])*keep; p.vy=(p.vy+g[1])*keep; p.vz=(p.vz+g[2])*keep;
        p.x+=p.vx*dt; p.y+=p.vy*dt; p.z+=p.vz*dt;
        e->particles[w++]=p;
    }
    e->count=w;
    if(e->active&&d->rate>0) {
        e->carry+=d->rate*dt;
        uint32_t n=e->carry>=1?(uint32_t)e->carry:0;
        e->carry-=n;
        athena_emitter3d_emit(e,n);
    }
    return 0;
}
#define CHUNK (ATHENA_PARTICLES3D_BATCH*8)
int athena_emitter3d_draw(AthenaEmitter3D *e,AthenaCamera3D *camera,AthenaImage *image) {
    if(!e||!camera) return ATHENA_PARTICLES3D_EINVAL;
    if(!athena_camera3d_update(camera)) return ATHENA_PARTICLES3D_EINVAL;
    if(!image||!e->count) return 0;
    const AthenaEmitter3DDesc *d=&e->desc;
    float rect[4]={d->u1,d->v1,d->u2,d->v2};
    if(rect[0]==0&&rect[1]==0&&rect[2]==0&&rect[3]==0) { rect[2]=image->width; rect[3]=image->height; }
    /* View depth along the camera's forward axis (-Z of the view): every
     * corner of a billboard shares it, so this test is exact. */
    const float *v=camera->view.value;
    float fx=-v[2],fy=-v[6],fz=-v[10],ex=camera->position.x,ey=camera->position.y,ez=camera->position.z;
    float near=camera->near_clip,far=camera->far_clip;
    static AthenaParticle3DRecord records[CHUNK];
    uint32_t n=0,sent=0;
    for(uint32_t i=0;i<e->count;i++) {
        const Particle *p=&e->particles[i];
        float depth=(p->x-ex)*fx+(p->y-ey)*fy+(p->z-ez)*fz;
        if(depth<=near||depth>=far) continue;
        float t=p->age*p->inv_life;
        AthenaParticle3DRecord *r=&records[n++];
        r->x=p->x; r->y=p->y; r->z=p->z; r->half=(d->size_start+(d->size_end-d->size_start)*t)*.5f;
        int32_t *channel=&r->r;
        for(int k=0;k<4;k++) {
            float a=(d->color_start>>(k*8))&0xff,b=(d->color_end>>(k*8))&0xff;
            channel[k]=(int32_t)(a+(b-a)*t+.5f);
        }
        if(n==CHUNK) {
            int code=athena_particles3d_submit(camera,image,rect,records,n); if(code<0) return code;
            sent+=n; n=0;
        }
    }
    if(n) { int code=athena_particles3d_submit(camera,image,rect,records,n); if(code<0) return code; sent+=n; }
    return (int)sent;
}
int athena_particles3d_update(float dt) {
    if(!athena_float_isfinite(dt)||dt<0) return ATHENA_PARTICLES3D_EINVAL;
    for(AthenaEmitter3D *e=emitters;e;e=e->next) athena_emitter3d_update(e,dt);
    return 0;
}
static int loop_run(void *opaque,AthenaLoopPhase phase,float value) {
    (void)opaque; (void)phase;
    return athena_particles3d_update(value)<0?-1:0;
}
static void loop_release(void *opaque) {
    if((uintptr_t)opaque==loop_generation) { loop_id=0; loop_owner=NULL; loop_generation=0; }
}
int athena_particles3d_attach_loop(int priority,void *owner) {
    if(loop_id) return ATHENA_PARTICLES3D_EINVAL;
    static uintptr_t next_generation;
    uintptr_t generation=++next_generation?next_generation:++next_generation;
    AthenaLoopSystemDesc desc={.priority=priority,.phases=ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_POST_UPDATE),
        .func=loop_run,.release=loop_release,.opaque=(void *)generation};
    int id=athena_loop_system_add(&desc);
    if(id<0) return id==ATHENA_LOOP_SYSTEM_ENOMEM?ATHENA_PARTICLES3D_ENOMEM:ATHENA_PARTICLES3D_EINVAL;
    loop_id=id; loop_owner=owner; loop_generation=generation; return id;
}
int athena_particles3d_detach_loop(void) {
    if(!loop_id) return 0;
    int id=loop_id; loop_id=0; loop_owner=NULL; loop_generation=0;
    athena_loop_system_remove(id); return 1;
}
void athena_particles3d_detach_owner(void *owner) { if(loop_id&&loop_owner==owner) athena_particles3d_detach_loop(); }
int athena_particles3d_loop_system(void) { return loop_id; }
