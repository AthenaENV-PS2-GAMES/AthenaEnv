#include <math.h>
#include <stdlib.h>
#include <athena/float_bits.h>
#include <athena/loop.h>
#include <athena/tween3d.h>
/* DELAY and RUNNING advance; ENDED waits for its end notification in the
 * second phase of advance(); DONE is unlinked. */
enum { DELAY, RUNNING, ENDED, DONE };
struct AthenaTween3D {
    uint64_t refs;
    AthenaTween3DTarget kind; void *target; void (*release_target)(void *);
    AthenaTween3DDesc desc;
    int state,paused,completed,cycle;
    int32_t repeat_left;
    float delay_left,elapsed;
    float from_position[3],from_scale[3],from_look[3];
    AthenaQuaternion from_rotation,to_rotation;
    float arc,inv_sin;      /* rotation arc from -> to, extrapolated by overshooting curves */
    AthenaTween3DEnd end; void *end_opaque; void *owner;
    struct AthenaTween3D *prev,*next;
};
/* Active tweens, each holding one reference. Main thread only. */
static AthenaTween3D *tweens;
static int loop_id;
static void *loop_owner;
static uintptr_t loop_generation;

static int finite_desc(const AthenaTween3DDesc *d) {
    const float *v[]={d->position,d->scale,d->rotation,d->look};
    for(int k=0;k<4;k++) for(int i=0;i<3;i++) if(!athena_float_isfinite(v[k][i])) return 0;
    return athena_float_isfinite(d->duration)&&d->duration>=0&&athena_float_isfinite(d->delay)&&d->delay>=0&&
        d->repeat>=-1&&d->ease>=ATHENA_EASE_LINEAR&&d->ease<ATHENA_EASE_COUNT;
}
int athena_tween3d_create(AthenaTween3DTarget kind,void *target,void (*release_target)(void *),
    const AthenaTween3DDesc *desc,AthenaTween3D **out) {
    if(!out) return ATHENA_TWEEN3D_EINVAL;
    *out=NULL;
    uint32_t allowed=kind==ATHENA_TWEEN3D_CAMERA?(ATHENA_TWEEN3D_POSITION|ATHENA_TWEEN3D_LOOK):
        (ATHENA_TWEEN3D_POSITION|ATHENA_TWEEN3D_SCALE|ATHENA_TWEEN3D_ROTATION);
    if(!target||!desc||kind>ATHENA_TWEEN3D_CAMERA||(desc->channels&~allowed)||!finite_desc(desc)) return ATHENA_TWEEN3D_EINVAL;
    AthenaTween3D *t=calloc(1,sizeof(*t)); if(!t) return ATHENA_TWEEN3D_ENOMEM;
    if(desc->channels&ATHENA_TWEEN3D_ROTATION&&
        !athena_quaternion_euler(&t->to_rotation,desc->rotation[0],desc->rotation[1],desc->rotation[2])) {
        free(t); return ATHENA_TWEEN3D_EINVAL;
    }
    t->refs=2; /* caller and active list */
    t->kind=kind; t->target=target; t->release_target=release_target; t->desc=*desc;
    t->state=DELAY; t->delay_left=desc->delay; t->repeat_left=desc->repeat;
    if(kind==ATHENA_TWEEN3D_NODE) athena_node3d_retain(target);
    else if(kind==ATHENA_TWEEN3D_INSTANCE) athena_instance3d_retain(target);
    t->next=tweens; if(tweens) tweens->prev=t; tweens=t;
    *out=t; return 0;
}
void athena_tween3d_retain(AthenaTween3D *t) { if(t) t->refs++; }
void athena_tween3d_release(AthenaTween3D *t) {
    if(!t||--t->refs) return;
    if(t->kind==ATHENA_TWEEN3D_NODE) athena_node3d_release(t->target);
    else if(t->kind==ATHENA_TWEEN3D_INSTANCE) athena_instance3d_release(t->target);
    if(t->release_target) t->release_target(t->target);
    free(t);
}
void athena_tween3d_set_end(AthenaTween3D *t,AthenaTween3DEnd end,void *opaque) { if(t) { t->end=end; t->end_opaque=opaque; } }
void athena_tween3d_set_owner(AthenaTween3D *t,void *owner) { if(t) t->owner=owner; }
void athena_tween3d_pause(AthenaTween3D *t,int paused) { if(t) t->paused=!!paused; }
int athena_tween3d_paused(const AthenaTween3D *t) { return t&&t->paused; }
int athena_tween3d_active(const AthenaTween3D *t) { return t&&(t->state==DELAY||t->state==RUNNING); }
float athena_tween3d_progress(const AthenaTween3D *t) {
    if(!t||t->state==DELAY) return 0;
    if(t->state!=RUNNING) return t->completed?1:0;
    return t->desc.duration>0?t->elapsed/t->desc.duration:1;
}

static void start(AthenaTween3D *t) {
    if(t->kind==ATHENA_TWEEN3D_CAMERA) {
        const AthenaCamera3D *c=t->target;
        t->from_position[0]=c->position.x; t->from_position[1]=c->position.y; t->from_position[2]=c->position.z;
        t->from_look[0]=c->target.x; t->from_look[1]=c->target.y; t->from_look[2]=c->target.z;
    } else {
        if(t->kind==ATHENA_TWEEN3D_NODE) athena_node3d_get_trs(t->target,t->from_position,&t->from_rotation,t->from_scale);
        else athena_instance3d_get_trs(t->target,t->from_position,&t->from_rotation,t->from_scale);
        if(t->desc.channels&ATHENA_TWEEN3D_ROTATION) {
            AthenaQuaternion *a=&t->from_rotation,*b=&t->to_rotation;
            float dot=a->x*b->x+a->y*b->y+a->z*b->z+a->w*b->w;
            if(dot<0) { b->x=-b->x; b->y=-b->y; b->z=-b->z; b->w=-b->w; dot=-dot; }
            if(dot>1) dot=1;
            float angle=acosf(dot),sine=sinf(angle);
            t->arc=sine>1e-4f?angle:0; t->inv_sin=sine>1e-4f?1/sine:0;
        }
    }
    if(t->desc.overwrite) for(AthenaTween3D *o=tweens;o;o=o->next)
        if(o!=t&&o->target==t->target&&(o->state==DELAY||o->state==RUNNING)) { o->state=ENDED; o->completed=0; }
    t->state=RUNNING; t->elapsed=0; t->cycle=0;
}
static void lerp3(float out[3],const float a[3],const float b[3],float e) {
    for(int i=0;i<3;i++) out[i]=e==1?b[i]:e==0?a[i]:a[i]+(b[i]-a[i])*e;
}
/* Applies the value at eased progress e of the from -> to path. */
static void apply(AthenaTween3D *t,float e) {
    const AthenaTween3DDesc *d=&t->desc;
    float v[3];
    if(t->kind==ATHENA_TWEEN3D_CAMERA) {
        AthenaCamera3D *c=t->target;
        float eye[3]={c->position.x,c->position.y,c->position.z},look[3]={c->target.x,c->target.y,c->target.z};
        if(d->channels&ATHENA_TWEEN3D_POSITION) lerp3(eye,t->from_position,d->position,e);
        if(d->channels&ATHENA_TWEEN3D_LOOK) lerp3(look,t->from_look,d->look,e);
        athena_camera3d_set_view(c,eye,look); /* a degenerate frame keeps the camera */
        return;
    }
    int node=t->kind==ATHENA_TWEEN3D_NODE;
    if(d->channels&ATHENA_TWEEN3D_POSITION) {
        lerp3(v,t->from_position,d->position,e);
        if(node) athena_node3d_set_position(t->target,v[0],v[1],v[2]); else athena_instance3d_set_position(t->target,v[0],v[1],v[2]);
    }
    if(d->channels&ATHENA_TWEEN3D_SCALE) {
        lerp3(v,t->from_scale,d->scale,e);
        if(node) athena_node3d_set_scale(t->target,v[0],v[1],v[2]); else athena_instance3d_set_scale(t->target,v[0],v[1],v[2]);
    }
    if(d->channels&ATHENA_TWEEN3D_ROTATION) {
        const AthenaQuaternion *a=&t->from_rotation,*b=&t->to_rotation;
        float wa=1-e,wb=e; /* exact ends; nlerp for nearly equal keys */
        if(e!=0&&e!=1&&t->arc>0) { wa=sinf((1-e)*t->arc)*t->inv_sin; wb=sinf(e*t->arc)*t->inv_sin; }
        float q[4]={a->x*wa+b->x*wb,a->y*wa+b->y*wb,a->z*wa+b->z*wb,a->w*wa+b->w*wb};
        if(node) athena_node3d_set_rotation(t->target,q[0],q[1],q[2],q[3]);
        else athena_instance3d_set_rotation(t->target,q[0],q[1],q[2],q[3]);
    }
}
static float eased(const AthenaTween3D *t,float progress) {
    /* Yoyo: odd cycles run backwards. */
    if(t->desc.yoyo&&(t->cycle&1)) progress=1-progress;
    return athena_ease(t->desc.ease,progress);
}
/* Final values of the last cycle: the end, or the start after a yoyo return. */
static void finish(AthenaTween3D *t,int completed) {
    if(completed) {
        if(t->state==DELAY) start(t);
        apply(t,t->desc.yoyo&&(t->cycle&1)?0:1);
    }
    t->state=ENDED; t->completed=completed;
}
static void step(AthenaTween3D *t,float dt) {
    if(t->paused) return;
    if(t->state==DELAY) {
        t->delay_left-=dt;
        if(t->delay_left>0) return;
        dt=-t->delay_left; start(t);
    }
    if(t->state!=RUNNING) return;
    float duration=t->desc.duration;
    if(duration<=0) {
        /* Every cycle is instantaneous: finite repeats end at once. */
        if(t->repeat_left>=0) { t->cycle+=t->repeat_left; finish(t,1); }
        return;
    }
    t->elapsed+=dt;
    if(t->elapsed>=duration) {
        float cycles=floorf(t->elapsed/duration);
        if(t->repeat_left>=0&&cycles>t->repeat_left) {
            t->cycle+=t->repeat_left; finish(t,1); return;
        }
        t->elapsed-=cycles*duration; t->cycle+=(int)cycles;
        if(t->repeat_left>0) t->repeat_left-=(int32_t)cycles;
    }
    apply(t,eased(t,t->elapsed/duration));
}
/* Unlinks an ENDED tween, notifies it and drops the list reference. */
static void notify(AthenaTween3D *t) {
    if(t->prev) t->prev->next=t->next; else tweens=t->next;
    if(t->next) t->next->prev=t->prev;
    t->prev=t->next=NULL; t->state=DONE;
    AthenaTween3DEnd end=t->end; t->end=NULL;
    if(end) end(t->end_opaque,t->completed);
    athena_tween3d_release(t);
}
static int notify_ended(void) {
    int ended=0;
    for(;;) {
        AthenaTween3D *t=tweens;
        while(t&&t->state!=ENDED) t=t->next;
        if(!t) return ended;
        notify(t); ended++; /* the callback may change the list: rescan */
    }
}
void athena_tween3d_kill(AthenaTween3D *t,int complete) {
    if(!t||(t->state!=DELAY&&t->state!=RUNNING)) return;
    finish(t,complete); notify(t);
}
int athena_tween3d_kill_target(const void *target,int complete) {
    int killed=0;
    for(AthenaTween3D *t=tweens;t;t=t->next)
        if(t->target==target&&(t->state==DELAY||t->state==RUNNING)) { finish(t,complete); killed++; }
    notify_ended();
    return killed;
}
int athena_tween3d_count_target(const void *target) {
    int count=0;
    for(AthenaTween3D *t=tweens;t;t=t->next) count+=t->target==target&&(t->state==DELAY||t->state==RUNNING);
    return count;
}
void athena_tween3d_kill_owner(void *owner) {
    for(AthenaTween3D *t=tweens;t;t=t->next)
        if(t->owner==owner&&(t->state==DELAY||t->state==RUNNING)) { t->state=ENDED; t->completed=0; }
    notify_ended();
}
int athena_tween3d_advance(float dt) {
    if(!athena_float_isfinite(dt)||dt<0) return ATHENA_TWEEN3D_EINVAL;
    for(AthenaTween3D *t=tweens;t;t=t->next) if(t->state==DELAY||t->state==RUNNING) step(t,dt);
    return notify_ended();
}
static int loop_run(void *opaque,AthenaLoopPhase phase,float value) {
    (void)opaque; (void)phase;
    return athena_tween3d_advance(value)<0?-1:0;
}
static void loop_release(void *opaque) {
    if((uintptr_t)opaque==loop_generation) { loop_id=0; loop_owner=NULL; loop_generation=0; }
}
int athena_tween3d_attach_loop(int priority,void *owner) {
    if(loop_id) return ATHENA_TWEEN3D_EINVAL;
    static uintptr_t next_generation;
    uintptr_t generation=++next_generation?next_generation:++next_generation;
    AthenaLoopSystemDesc desc={.priority=priority,.phases=ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_PRE_UPDATE),
        .func=loop_run,.release=loop_release,.opaque=(void *)generation};
    int id=athena_loop_system_add(&desc);
    if(id<0) return id==ATHENA_LOOP_SYSTEM_ENOMEM?ATHENA_TWEEN3D_ENOMEM:ATHENA_TWEEN3D_EINVAL;
    loop_id=id; loop_owner=owner; loop_generation=generation; return id;
}
int athena_tween3d_detach_loop(void) {
    if(!loop_id) return 0;
    int id=loop_id; loop_id=0; loop_owner=NULL; loop_generation=0;
    athena_loop_system_remove(id); return 1;
}
void athena_tween3d_detach_owner(void *owner) { if(loop_id&&loop_owner==owner) athena_tween3d_detach_loop(); }
int athena_tween3d_loop_system(void) { return loop_id; }
