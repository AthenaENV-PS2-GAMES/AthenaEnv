#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <athena/float_bits.h>
#include <athena/quaternion.h>
#include <athena/loop.h>
#include <athena/animation3d.h>
typedef struct {
    uint32_t target,key_count,width;
    AthenaAnim3DPath path; AthenaAnim3DInterpolation interpolation;
    float *times,*values;
    /* Rotation tracks: per segment k..k+1, the arc angle and 1/sin(angle),
     * so sampling is two sinf and no acos or normalization. */
    float *arcs;
} Track;
struct AthenaClip3D {
    uint64_t refs;
    Track *tracks; uint32_t track_count,target_count;
    float duration;
};
struct AthenaPlayer3D {
    uint64_t refs;
    AthenaClip3D *clip;
    AthenaNode3D **nodes; uint32_t node_count;
    uint32_t *cursors; /* last key index per track: forward playback is O(1) */
    float time,speed;
    uint8_t playing,loop;
    struct AthenaPlayer3D *prev,*next;
};
/* Every live player, for athena_animation3d_advance(). Main thread only. */
static AthenaPlayer3D *players;
static int loop_id;
static void *loop_owner;
/* Tags each attach: a deferred release of an older system must not clear a
 * newer one. */
static uintptr_t loop_generation;

static void clip_free(AthenaClip3D *c) {
    for(uint32_t i=0;i<c->track_count;i++) { free(c->tracks[i].times); free(c->tracks[i].values); free(c->tracks[i].arcs); }
    free(c->tracks); free(c);
}
static int copy_track(Track *t,const AthenaTrack3DDesc *d) {
    if(d->path>ATHENA_ANIM3D_WEIGHTS||d->interpolation>ATHENA_ANIM3D_STEP||!d->times||!d->values||
        !d->key_count||d->key_count>ATHENA_ANIM3D_MAX_KEYS||
        (d->path==ATHENA_ANIM3D_WEIGHTS&&(!d->weight_count||d->weight_count>ATHENA_MODEL3D_MAX_TARGETS)))
        return ATHENA_ANIM3D_EINVAL;
    uint32_t width=d->path==ATHENA_ANIM3D_WEIGHTS?d->weight_count:d->path==ATHENA_ANIM3D_ROTATION?4:3;
    for(uint32_t k=0;k<d->key_count;k++) {
        float time=d->times[k];
        if(!athena_float_isfinite(time)||time<0||(k&&time<=d->times[k-1])) return ATHENA_ANIM3D_EINVAL;
        for(uint32_t j=0;j<width;j++) if(!athena_float_isfinite(d->values[k*width+j])) return ATHENA_ANIM3D_EINVAL;
    }
    t->times=malloc(d->key_count*sizeof(float)); t->values=malloc(d->key_count*width*sizeof(float));
    if(!t->times||!t->values) return ATHENA_ANIM3D_ENOMEM;
    memcpy(t->times,d->times,d->key_count*sizeof(float));
    memcpy(t->values,d->values,d->key_count*width*sizeof(float));
    t->target=d->target; t->path=d->path; t->interpolation=d->interpolation;
    t->key_count=d->key_count; t->width=width;
    if(d->path==ATHENA_ANIM3D_ROTATION) for(uint32_t k=0;k<d->key_count;k++) {
        float *q=&t->values[k*4];
        AthenaQuaternion in={q[0],q[1],q[2],q[3]},n;
        if(!athena_quaternion_normalize(&n,&in)) return ATHENA_ANIM3D_EINVAL;
        /* Same hemisphere as the previous key: slerp takes the short arc. */
        if(k&&q[-4]*n.x+q[-3]*n.y+q[-2]*n.z+q[-1]*n.w<0) { n.x=-n.x; n.y=-n.y; n.z=-n.z; n.w=-n.w; }
        q[0]=n.x; q[1]=n.y; q[2]=n.z; q[3]=n.w;
    }
    if(d->path==ATHENA_ANIM3D_ROTATION&&d->key_count>1) {
        t->arcs=malloc((d->key_count-1)*2*sizeof(float)); if(!t->arcs) return ATHENA_ANIM3D_ENOMEM;
        for(uint32_t k=0;k+1<d->key_count;k++) {
            const float *a=&t->values[k*4],*b=a+4;
            float dot=a[0]*b[0]+a[1]*b[1]+a[2]*b[2]+a[3]*b[3]; if(dot>1) dot=1;
            float angle=acosf(dot),sine=sinf(angle);
            /* Nearly equal keys: plain lerp (arc 0), renormalized when applied. */
            t->arcs[k*2]=sine>1e-4f?angle:0; t->arcs[k*2+1]=sine>1e-4f?1/sine:0;
        }
    }
    return 0;
}
int athena_clip3d_create(const AthenaTrack3DDesc *tracks,uint32_t count,AthenaClip3D **out) {
    if(!out) return ATHENA_ANIM3D_EINVAL;
    *out=NULL;
    if(!tracks||!count||count>ATHENA_ANIM3D_MAX_TRACKS) return ATHENA_ANIM3D_EINVAL;
    AthenaClip3D *c=calloc(1,sizeof(*c)); if(!c) return ATHENA_ANIM3D_ENOMEM;
    c->tracks=calloc(count,sizeof(*c->tracks)); if(!c->tracks) { free(c); return ATHENA_ANIM3D_ENOMEM; }
    c->track_count=count; c->refs=1;
    for(uint32_t i=0;i<count;i++) {
        if(tracks[i].target>=ATHENA_ANIM3D_MAX_TRACKS) { clip_free(c); return ATHENA_ANIM3D_EINVAL; }
        int code=copy_track(&c->tracks[i],&tracks[i]);
        if(code<0) { clip_free(c); return code; }
        Track *t=&c->tracks[i];
        if(t->target+1>c->target_count) c->target_count=t->target+1;
        if(t->times[t->key_count-1]>c->duration) c->duration=t->times[t->key_count-1];
    }
    *out=c; return 0;
}
void athena_clip3d_retain(AthenaClip3D *c) { if(c) c->refs++; }
void athena_clip3d_release(AthenaClip3D *c) { if(c&&!--c->refs) clip_free(c); }
float athena_clip3d_duration(const AthenaClip3D *c) { return c?c->duration:0; }
uint32_t athena_clip3d_target_count(const AthenaClip3D *c) { return c?c->target_count:0; }

AthenaPlayer3D *athena_player3d_create(AthenaClip3D *clip,AthenaNode3D *const *nodes,uint32_t count) {
    if(!clip||!nodes||count<clip->target_count) return NULL;
    for(uint32_t i=0;i<count;i++) if(!nodes[i]) return NULL;
    AthenaPlayer3D *p=calloc(1,sizeof(*p)); if(!p) return NULL;
    p->nodes=malloc((count?count:1)*sizeof(*p->nodes)); p->cursors=calloc(clip->track_count,sizeof(*p->cursors));
    if(!p->nodes||!p->cursors) { free(p->nodes); free(p->cursors); free(p); return NULL; }
    memcpy(p->nodes,nodes,count*sizeof(*nodes)); p->node_count=count;
    for(uint32_t i=0;i<count;i++) athena_node3d_retain(nodes[i]);
    athena_clip3d_retain(clip); p->clip=clip; p->refs=1; p->speed=1;
    p->next=players; if(players) players->prev=p; players=p;
    return p;
}
void athena_player3d_retain(AthenaPlayer3D *p) { if(p) p->refs++; }
void athena_player3d_release(AthenaPlayer3D *p) {
    if(!p||--p->refs) return;
    if(p->prev) p->prev->next=p->next; else players=p->next;
    if(p->next) p->next->prev=p->prev;
    for(uint32_t i=0;i<p->node_count;i++) athena_node3d_release(p->nodes[i]);
    athena_clip3d_release(p->clip); free(p->nodes); free(p->cursors); free(p);
}
void athena_player3d_play(AthenaPlayer3D *p) { if(p) p->playing=1; }
void athena_player3d_pause(AthenaPlayer3D *p) { if(p) p->playing=0; }
void athena_player3d_stop(AthenaPlayer3D *p) {
    if(!p) return;
    p->playing=0; p->time=p->speed<0?p->clip->duration:0;
}
int athena_player3d_playing(const AthenaPlayer3D *p) { return p&&p->playing; }
float athena_player3d_time(const AthenaPlayer3D *p) { return p?p->time:0; }
float athena_player3d_speed(const AthenaPlayer3D *p) { return p?p->speed:0; }
int athena_player3d_set_speed(AthenaPlayer3D *p,float speed) {
    if(!p||!athena_float_isfinite(speed)) return ATHENA_ANIM3D_EINVAL;
    p->speed=speed; return 0;
}
void athena_player3d_set_loop(AthenaPlayer3D *p,int loop) { if(p) p->loop=!!loop; }
int athena_player3d_loop(const AthenaPlayer3D *p) { return p&&p->loop; }

/* Key k with times[k] <= time < times[k+1], from the cached cursor. */
static uint32_t find_key(const Track *t,uint32_t *cursor,float time) {
    uint32_t k=*cursor<t->key_count?*cursor:0;
    if(t->times[k]>time) {
        /* Backwards (seek, loop wrap, negative speed): binary search. */
        uint32_t lo=0,hi=k;
        while(lo<hi) { uint32_t mid=(lo+hi+1)/2; if(t->times[mid]<=time) lo=mid; else hi=mid-1; }
        k=lo;
    } else while(k+1<t->key_count&&t->times[k+1]<=time) k++;
    *cursor=k; return k;
}
static int apply(AthenaPlayer3D *p) {
    const AthenaClip3D *c=p->clip;
    for(uint32_t i=0;i<c->track_count;i++) {
        const Track *t=&c->tracks[i];
        uint32_t k=find_key(t,&p->cursors[i],p->time);
        const float *a=&t->values[k*t->width];
        float v[ATHENA_MODEL3D_MAX_TARGETS]; memcpy(v,a,t->width*sizeof(float));
        if(t->interpolation==ATHENA_ANIM3D_LINEAR&&k+1<t->key_count&&p->time>t->times[k]) {
            const float *b=a+t->width;
            float u=(p->time-t->times[k])/(t->times[k+1]-t->times[k]);
            if(t->path==ATHENA_ANIM3D_ROTATION) {
                float angle=t->arcs[k*2],wa=1-u,wb=u;
                if(angle>0) { wa=sinf(wa*angle)*t->arcs[k*2+1]; wb=sinf(wb*angle)*t->arcs[k*2+1]; }
                for(uint32_t j=0;j<4;j++) v[j]=a[j]*wa+b[j]*wb;
            } else for(uint32_t j=0;j<t->width;j++) v[j]=a[j]+(b[j]-a[j])*u;
        }
        AthenaNode3D *n=p->nodes[t->target];
        int code=t->path==ATHENA_ANIM3D_POSITION?athena_node3d_set_position(n,v[0],v[1],v[2]):
            t->path==ATHENA_ANIM3D_SCALE?athena_node3d_set_scale(n,v[0],v[1],v[2]):
            t->path==ATHENA_ANIM3D_WEIGHTS?athena_node3d_set_weights(n,v,t->width):
            athena_node3d_set_rotation(n,v[0],v[1],v[2],v[3]);
        if(code<0) return ATHENA_ANIM3D_EINVAL;
    }
    return 0;
}
/* Wraps or clamps time into [0, duration]; returns 1 when a non-looping
 * player reached its end. */
static int settle(AthenaPlayer3D *p) {
    float d=p->clip->duration;
    if(p->loop) {
        if(d<=0) { p->time=0; return 0; }
        if(p->time<0||p->time>d) { p->time=fmodf(p->time,d); if(p->time<0) p->time+=d; }
        return 0;
    }
    if(p->time>=d&&p->speed>=0) { p->time=d; return 1; }
    if(p->time<=0&&p->speed<0) { p->time=0; return 1; }
    if(p->time<0) p->time=0;
    if(p->time>d) p->time=d;
    return 0;
}
int athena_player3d_set_time(AthenaPlayer3D *p,float time) {
    if(!p||!athena_float_isfinite(time)) return ATHENA_ANIM3D_EINVAL;
    p->time=time; settle(p); return apply(p);
}
int athena_player3d_advance(AthenaPlayer3D *p,float dt) {
    if(!p||!athena_float_isfinite(dt)||dt<0) return ATHENA_ANIM3D_EINVAL;
    if(!p->playing) return 0;
    float time=p->time+dt*p->speed;
    if(!athena_float_isfinite(time)) return ATHENA_ANIM3D_EINVAL;
    p->time=time;
    int finished=settle(p);
    int code=apply(p);
    if(code<0) return code;
    if(finished) p->playing=0;
    return finished;
}
int athena_animation3d_advance(float dt) {
    if(!athena_float_isfinite(dt)||dt<0) return ATHENA_ANIM3D_EINVAL;
    int finished=0;
    for(AthenaPlayer3D *p=players;p;p=p->next) {
        int code=athena_player3d_advance(p,dt);
        if(code<0) return code;
        finished+=code;
    }
    return finished;
}
static int loop_run(void *opaque,AthenaLoopPhase phase,float value) {
    (void)opaque; (void)phase;
    return athena_animation3d_advance(value)<0?-1:0;
}
static void loop_release(void *opaque) {
    if((uintptr_t)opaque==loop_generation) { loop_id=0; loop_owner=NULL; loop_generation=0; }
}
int athena_animation3d_attach_loop(int priority,void *owner) {
    if(loop_id) return ATHENA_ANIM3D_EINVAL;
    static uintptr_t next_generation;
    uintptr_t generation=++next_generation?next_generation:++next_generation;
    AthenaLoopSystemDesc desc={.priority=priority,.phases=ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_POST_UPDATE),
        .func=loop_run,.release=loop_release,.opaque=(void *)generation};
    int id=athena_loop_system_add(&desc);
    if(id<0) return id==ATHENA_LOOP_SYSTEM_ENOMEM?ATHENA_ANIM3D_ENOMEM:ATHENA_ANIM3D_EINVAL;
    loop_id=id; loop_owner=owner; loop_generation=generation; return id;
}
int athena_animation3d_detach_loop(void) {
    if(!loop_id) return 0;
    int id=loop_id; loop_id=0; loop_owner=NULL; loop_generation=0;
    athena_loop_system_remove(id); return 1;
}
void athena_animation3d_detach_owner(void *owner) {
    if(loop_id&&loop_owner==owner) athena_animation3d_detach_loop();
}
int athena_animation3d_loop_system(void) { return loop_id; }
