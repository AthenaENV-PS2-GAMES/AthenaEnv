#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <athena/float_bits.h>
#include <athena/loop.h>
#include <athena/particles2d.h>
#include "particles2d_backend.h"
typedef struct { float x,y,vx,vy,age,inv_life,rotation,spin; } Particle;
struct AthenaEmitter2D {
    uint64_t refs;
    AthenaEmitter2DDesc desc;
    Particle *particles; uint32_t count;
    float x,y,carry;   /* carry: fractional particles owed by rate * dt */
    uint32_t random;
    int active,rotates;
    struct AthenaEmitter2D *prev,*next;
};
/* Every live emitter, for update(). Main thread only. */
static AthenaEmitter2D *emitters;
static int loop_id;
static void *loop_owner;
static uintptr_t loop_generation;

void athena_emitter2d_defaults(AthenaEmitter2DDesc *d) {
    memset(d,0,sizeof(*d));
    d->capacity=256; d->life_min=d->life_max=1; d->size_start=d->size_end=8;
    d->color_start=d->color_end=0x80ffffffu;
}
static int finite_all(const float *v,unsigned n) {
    for(unsigned i=0;i<n;i++) if(!athena_float_isfinite(v[i])) return 0;
    return 1;
}
static int valid(const AthenaEmitter2DDesc *d) {
    const float v[]={d->rate,d->life_min,d->life_max,d->speed_min,d->speed_max,d->angle,d->spread,
        d->gravity_x,d->gravity_y,d->drag,d->size_start,d->size_end,d->rotation_min,d->rotation_max,
        d->spin_min,d->spin_max,d->area_width,d->area_height,d->u1,d->v1,d->u2,d->v2};
    return d->capacity>=1&&d->capacity<=ATHENA_PARTICLES2D_MAX&&finite_all(v,sizeof(v)/sizeof(v[0]))&&
        d->rate>=0&&d->life_min>0&&d->life_max>=d->life_min&&d->speed_max>=d->speed_min&&
        d->drag>=0&&d->size_start>=0&&d->size_end>=0&&d->area_width>=0&&d->area_height>=0&&
        d->rotation_max>=d->rotation_min&&d->spin_max>=d->spin_min;
}
int athena_emitter2d_configure(AthenaEmitter2D *e,const AthenaEmitter2DDesc *d) {
    if(!e||!d||!valid(d)) return ATHENA_PARTICLES2D_EINVAL;
    if(d->capacity!=e->desc.capacity||!e->particles) {
        Particle *p=realloc(e->particles,d->capacity*sizeof(*p)); if(!p) return ATHENA_PARTICLES2D_ENOMEM;
        e->particles=p; if(e->count>d->capacity) e->count=d->capacity;
    }
    if(d->seed!=e->desc.seed||!e->random) e->random=d->seed?d->seed:0x9e3779b9u;
    e->desc=*d;
    e->rotates=d->rotation_min!=0||d->rotation_max!=0||d->spin_min!=0||d->spin_max!=0;
    return 0;
}
int athena_emitter2d_create(const AthenaEmitter2DDesc *d,AthenaEmitter2D **out) {
    if(!out) return ATHENA_PARTICLES2D_EINVAL;
    *out=NULL;
    AthenaEmitter2D *e=calloc(1,sizeof(*e)); if(!e) return ATHENA_PARTICLES2D_ENOMEM;
    int code=athena_emitter2d_configure(e,d);
    if(code<0) { free(e); return code; }
    e->refs=1; e->active=1;
    e->next=emitters; if(emitters) emitters->prev=e; emitters=e;
    *out=e; return 0;
}
const AthenaEmitter2DDesc *athena_emitter2d_desc(const AthenaEmitter2D *e) { return &e->desc; }
void athena_emitter2d_retain(AthenaEmitter2D *e) { if(e) e->refs++; }
void athena_emitter2d_release(AthenaEmitter2D *e) {
    if(!e||--e->refs) return;
    if(e->prev) e->prev->next=e->next; else emitters=e->next;
    if(e->next) e->next->prev=e->prev;
    free(e->particles); free(e);
}
int athena_emitter2d_set_position(AthenaEmitter2D *e,float x,float y) {
    if(!e||!athena_float_isfinite(x)||!athena_float_isfinite(y)) return ATHENA_PARTICLES2D_EINVAL;
    e->x=x; e->y=y; return 0;
}
void athena_emitter2d_set_active(AthenaEmitter2D *e,int active) { if(e) { e->active=!!active; if(!active) e->carry=0; } }
int athena_emitter2d_active(const AthenaEmitter2D *e) { return e&&e->active; }
void athena_emitter2d_clear(AthenaEmitter2D *e) { if(e) e->count=0; }
uint32_t athena_emitter2d_count(const AthenaEmitter2D *e) { return e?e->count:0; }
/* xorshift32 in [0, 1). */
static float random01(AthenaEmitter2D *e) {
    uint32_t x=e->random; x^=x<<13; x^=x>>17; x^=x<<5; e->random=x;
    return (x>>8)*(1.0f/16777216.0f);
}
static float between(AthenaEmitter2D *e,float lo,float hi) { return lo==hi?lo:lo+(hi-lo)*random01(e); }
uint32_t athena_emitter2d_emit(AthenaEmitter2D *e,uint32_t count) {
    if(!e) return 0;
    const AthenaEmitter2DDesc *d=&e->desc;
    uint32_t room=d->capacity-e->count; if(count>room) count=room;
    for(uint32_t i=0;i<count;i++) {
        Particle *p=&e->particles[e->count++];
        float a=d->angle+(random01(e)-.5f)*d->spread,s=between(e,d->speed_min,d->speed_max);
        p->x=e->x+(random01(e)-.5f)*d->area_width; p->y=e->y+(random01(e)-.5f)*d->area_height;
        p->vx=s==0?0:cosf(a)*s; p->vy=s==0?0:sinf(a)*s;
        p->age=0; p->inv_life=1/between(e,d->life_min,d->life_max);
        p->rotation=between(e,d->rotation_min,d->rotation_max); p->spin=between(e,d->spin_min,d->spin_max);
    }
    return count;
}
int athena_emitter2d_update(AthenaEmitter2D *e,float dt) {
    if(!e||!athena_float_isfinite(dt)||dt<0) return ATHENA_PARTICLES2D_EINVAL;
    const AthenaEmitter2DDesc *d=&e->desc;
    float gx=d->gravity_x*dt,gy=d->gravity_y*dt,keep=d->drag>0?1/(1+d->drag*dt):1;
    /* Compaction keeps birth order, so draw goes oldest to newest. */
    uint32_t w=0;
    for(uint32_t i=0;i<e->count;i++) {
        Particle p=e->particles[i];
        p.age+=dt;
        if(p.age*p.inv_life>=1) continue;
        p.vx=(p.vx+gx)*keep; p.vy=(p.vy+gy)*keep;
        p.x+=p.vx*dt; p.y+=p.vy*dt; p.rotation+=p.spin*dt;
        e->particles[w++]=p;
    }
    e->count=w;
    if(e->active&&d->rate>0) {
        e->carry+=d->rate*dt;
        uint32_t n=e->carry>=1?(uint32_t)e->carry:0;
        e->carry-=n;
        athena_emitter2d_emit(e,n);
    }
    return 0;
}
/* sin and cos of any finite angle, |error| < 0.001: a parabola refined
 * once, instead of the software sinf/cosf of the EE's libm. */
static float wrap_pi(float a) {
    const float tau=6.28318530717958647692f;
    float turns=a*(1/tau);
    int whole=(int)(turns+(turns>=0?.5f:-.5f));
    return a-whole*tau;
}
static float fast_sin(float x) {
    const float b=4/3.14159265358979323846f,c=-4/(3.14159265358979323846f*3.14159265358979323846f);
    float y=b*x+c*x*fabsf(x);
    return .225f*(y*fabsf(y)-y)+y;
}
static void fast_sincos(float angle,float *s,float *c) {
    float a=wrap_pi(angle);
    *s=fast_sin(a);
    *c=fast_sin(wrap_pi(a+1.57079632679489661923f));
}
static void lerp_rgba(uint32_t a,uint32_t b,float t,AthenaParticle2DRecord *out) {
    int32_t *channel=&out->r;
    for(int k=0;k<4;k++) {
        float ca=(a>>(k*8))&0xff,cb=(b>>(k*8))&0xff;
        channel[k]=(int32_t)(ca+(cb-ca)*t+.5f);
    }
}
#define CHUNK (ATHENA_PARTICLES2D_BATCH*8)
void athena_emitter2d_draw(AthenaEmitter2D *e,AthenaImage *image) {
    if(!e||!image||!e->count) return;
    const AthenaEmitter2DDesc *d=&e->desc;
    float rect[4]={d->u1,d->v1,d->u2,d->v2};
    if(rect[0]==0&&rect[1]==0&&rect[2]==0&&rect[3]==0) { rect[2]=image->width; rect[3]=image->height; }
    static AthenaParticle2DRecord records[CHUNK];
    uint32_t n=0;
    for(uint32_t i=0;i<e->count;i++) {
        const Particle *p=&e->particles[i];
        float t=p->age*p->inv_life,h=(d->size_start+(d->size_end-d->size_start)*t)*.5f;
        AthenaParticle2DRecord *r=&records[n++];
        r->x=p->x; r->y=p->y;
        if(e->rotates) { float sn,cs; fast_sincos(p->rotation,&sn,&cs); r->hc=h*cs; r->hs=h*sn; }
        else { r->hc=h; r->hs=0; }
        lerp_rgba(d->color_start,d->color_end,t,r);
        if(n==CHUNK) { athena_particles2d_submit(image,rect,records,n); n=0; }
    }
    if(n) athena_particles2d_submit(image,rect,records,n);
}
int athena_particles2d_update(float dt) {
    if(!athena_float_isfinite(dt)||dt<0) return ATHENA_PARTICLES2D_EINVAL;
    for(AthenaEmitter2D *e=emitters;e;e=e->next) athena_emitter2d_update(e,dt);
    return 0;
}
static int loop_run(void *opaque,AthenaLoopPhase phase,float value) {
    (void)opaque; (void)phase;
    return athena_particles2d_update(value)<0?-1:0;
}
static void loop_release(void *opaque) {
    if((uintptr_t)opaque==loop_generation) { loop_id=0; loop_owner=NULL; loop_generation=0; }
}
int athena_particles2d_attach_loop(int priority,void *owner) {
    if(loop_id) return ATHENA_PARTICLES2D_EINVAL;
    static uintptr_t next_generation;
    uintptr_t generation=++next_generation?next_generation:++next_generation;
    AthenaLoopSystemDesc desc={.priority=priority,.phases=ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_POST_UPDATE),
        .func=loop_run,.release=loop_release,.opaque=(void *)generation};
    int id=athena_loop_system_add(&desc);
    if(id<0) return id==ATHENA_LOOP_SYSTEM_ENOMEM?ATHENA_PARTICLES2D_ENOMEM:ATHENA_PARTICLES2D_EINVAL;
    loop_id=id; loop_owner=owner; loop_generation=generation; return id;
}
int athena_particles2d_detach_loop(void) {
    if(!loop_id) return 0;
    int id=loop_id; loop_id=0; loop_owner=NULL; loop_generation=0;
    athena_loop_system_remove(id); return 1;
}
void athena_particles2d_detach_owner(void *owner) { if(loop_id&&loop_owner==owner) athena_particles2d_detach_loop(); }
int athena_particles2d_loop_system(void) { return loop_id; }
