#include <math.h>
#include <stdlib.h>
#include <athena/float_bits.h>
#include <athena/loop.h>
#include <athena/camerarig3d.h>
enum { RIG_FOLLOW, RIG_ORBIT };
struct AthenaCameraRig3D {
    uint64_t refs;
    int kind,enabled,local_offset,has_state,snap;
    AthenaCamera3D *camera; AthenaRig3DCameraRelease release_camera;
    AthenaNode3D *target;
    float offset[3],look_offset[3],center[3];
    float yaw,pitch,distance,auto_rotate;
    float pitch_min,pitch_max,distance_min,distance_max;
    float eye_sharpness,look_sharpness;
    float eye[3],look[3]; /* smoothed state written to the camera */
    struct AthenaCameraRig3D *prev,*next;
};
/* Every live rig, for update_all(). Main thread only. */
static AthenaCameraRig3D *rigs;
static int loop_id;
static void *loop_owner;
static uintptr_t loop_generation;
static int finite3(float x,float y,float z) { return athena_float_isfinite(x)&&athena_float_isfinite(y)&&athena_float_isfinite(z); }
static float clampf(float v,float lo,float hi) { return v<lo?lo:v>hi?hi:v; }
static AthenaCameraRig3D *create(int kind,AthenaCamera3D *camera,AthenaRig3DCameraRelease release,AthenaNode3D *target) {
    if(!camera) return NULL;
    AthenaCameraRig3D *r=calloc(1,sizeof(*r)); if(!r) return NULL;
    r->refs=1; r->kind=kind; r->enabled=1; r->local_offset=1;
    r->camera=camera; r->release_camera=release;
    r->target=target; athena_node3d_retain(target);
    r->offset[1]=2; r->offset[2]=6; r->distance=6;
    r->pitch_min=-1.5f; r->pitch_max=1.5f; r->distance_min=.1f; r->distance_max=1e6f;
    r->next=rigs; if(rigs) rigs->prev=r; rigs=r;
    return r;
}
AthenaCameraRig3D *athena_rig3d_follow_create(AthenaCamera3D *c,AthenaRig3DCameraRelease release,AthenaNode3D *t) {
    return create(RIG_FOLLOW,c,release,t);
}
AthenaCameraRig3D *athena_rig3d_orbit_create(AthenaCamera3D *c,AthenaRig3DCameraRelease release,AthenaNode3D *t) {
    return create(RIG_ORBIT,c,release,t);
}
void athena_rig3d_retain(AthenaCameraRig3D *r) { if(r) r->refs++; }
void athena_rig3d_release(AthenaCameraRig3D *r) {
    if(!r||--r->refs) return;
    if(r->prev) r->prev->next=r->next; else rigs=r->next;
    if(r->next) r->next->prev=r->prev;
    athena_node3d_release(r->target);
    if(r->release_camera) r->release_camera(r->camera);
    free(r);
}
int athena_rig3d_set_target(AthenaCameraRig3D *r,AthenaNode3D *t) {
    if(!r) return ATHENA_RIG3D_EINVAL;
    athena_node3d_retain(t); athena_node3d_release(r->target); r->target=t; return 0;
}
int athena_rig3d_set_offset(AthenaCameraRig3D *r,float x,float y,float z,int local) {
    if(!r||!finite3(x,y,z)) return ATHENA_RIG3D_EINVAL;
    r->offset[0]=x; r->offset[1]=y; r->offset[2]=z; r->local_offset=!!local; return 0;
}
int athena_rig3d_set_look_offset(AthenaCameraRig3D *r,float x,float y,float z) {
    if(!r||!finite3(x,y,z)) return ATHENA_RIG3D_EINVAL;
    r->look_offset[0]=x; r->look_offset[1]=y; r->look_offset[2]=z; return 0;
}
int athena_rig3d_set_center(AthenaCameraRig3D *r,float x,float y,float z) {
    if(!r||!finite3(x,y,z)) return ATHENA_RIG3D_EINVAL;
    r->center[0]=x; r->center[1]=y; r->center[2]=z; return 0;
}
int athena_rig3d_set_angles(AthenaCameraRig3D *r,float yaw,float pitch) {
    if(!r||!athena_float_isfinite(yaw)||!athena_float_isfinite(pitch)) return ATHENA_RIG3D_EINVAL;
    r->yaw=remainderf(yaw,6.28318530717958647692f); r->pitch=clampf(pitch,r->pitch_min,r->pitch_max); return 0;
}
int athena_rig3d_rotate(AthenaCameraRig3D *r,float yaw,float pitch) {
    if(!r||!athena_float_isfinite(yaw)||!athena_float_isfinite(pitch)) return ATHENA_RIG3D_EINVAL;
    return athena_rig3d_set_angles(r,r->yaw+yaw,r->pitch+pitch);
}
int athena_rig3d_set_distance(AthenaCameraRig3D *r,float d) {
    if(!r||!athena_float_isfinite(d)) return ATHENA_RIG3D_EINVAL;
    r->distance=clampf(d,r->distance_min,r->distance_max); return 0;
}
int athena_rig3d_zoom(AthenaCameraRig3D *r,float delta) {
    if(!r||!athena_float_isfinite(delta)) return ATHENA_RIG3D_EINVAL;
    return athena_rig3d_set_distance(r,r->distance+delta);
}
int athena_rig3d_set_limits(AthenaCameraRig3D *r,float pmin,float pmax,float dmin,float dmax) {
    /* Pitch stays below +-90 degrees so the up vector never aligns. */
    if(!r||!finite3(pmin,pmax,dmin)||!athena_float_isfinite(dmax)||pmin>pmax||pmin<-1.56f||pmax>1.56f||
        dmin<=0||dmin>dmax) return ATHENA_RIG3D_EINVAL;
    r->pitch_min=pmin; r->pitch_max=pmax; r->distance_min=dmin; r->distance_max=dmax;
    r->pitch=clampf(r->pitch,pmin,pmax); r->distance=clampf(r->distance,dmin,dmax); return 0;
}
int athena_rig3d_set_auto_rotate(AthenaCameraRig3D *r,float speed) {
    if(!r||!athena_float_isfinite(speed)) return ATHENA_RIG3D_EINVAL;
    r->auto_rotate=speed; return 0;
}
void athena_rig3d_get_angles(const AthenaCameraRig3D *r,float *yaw,float *pitch,float *distance) {
    if(!r) return;
    if(yaw) *yaw=r->yaw;
    if(pitch) *pitch=r->pitch;
    if(distance) *distance=r->distance;
}
int athena_rig3d_set_sharpness(AthenaCameraRig3D *r,float eye,float look) {
    if(!r||!athena_float_isfinite(eye)||!athena_float_isfinite(look)||eye<0||look<0) return ATHENA_RIG3D_EINVAL;
    r->eye_sharpness=eye; r->look_sharpness=look; return 0;
}
void athena_rig3d_snap(AthenaCameraRig3D *r) { if(r) r->snap=1; }
void athena_rig3d_set_enabled(AthenaCameraRig3D *r,int enabled) { if(r) r->enabled=!!enabled; }
int athena_rig3d_enabled(const AthenaCameraRig3D *r) { return r&&r->enabled; }
static void apply_point(const float *m,const float p[3],float out[3]) {
    for(int r=0;r<3;r++) out[r]=m[r]*p[0]+m[4+r]*p[1]+m[8+r]*p[2]+m[12+r];
}
/* Exponential smoothing: frame-rate independent, 0 sharpness is rigid. */
static void approach(float state[3],const float goal[3],float sharpness,float dt,int jump) {
    float a=jump||sharpness<=0?1:1-expf(-sharpness*dt);
    for(int i=0;i<3;i++) state[i]+=(goal[i]-state[i])*a;
}
int athena_rig3d_update(AthenaCameraRig3D *r,float dt) {
    if(!r||!athena_float_isfinite(dt)||dt<0) return ATHENA_RIG3D_EINVAL;
    if(!r->enabled) return 0;
    AthenaMatrix4 world; int has_world=0;
    if(r->target) {
        if(athena_node3d_world(r->target,&world)<0) return 0; /* stale: keep last frame */
        has_world=1;
    } else if(r->kind==RIG_FOLLOW) return 0;
    float eye[3],look[3];
    if(r->kind==RIG_FOLLOW) {
        const float *m=world.value;
        if(r->local_offset) apply_point(m,r->offset,eye);
        else for(int i=0;i<3;i++) eye[i]=m[12+i]+r->offset[i];
        apply_point(m,r->look_offset,look);
    } else {
        if(r->auto_rotate!=0) r->yaw=remainderf(r->yaw+r->auto_rotate*dt,6.28318530717958647692f);
        for(int i=0;i<3;i++) look[i]=(has_world?world.value[12+i]:0)+r->center[i];
        float cp=cosf(r->pitch);
        eye[0]=look[0]+r->distance*cp*sinf(r->yaw);
        eye[1]=look[1]+r->distance*sinf(r->pitch);
        eye[2]=look[2]+r->distance*cp*cosf(r->yaw);
    }
    if(!finite3(eye[0],eye[1],eye[2])||!finite3(look[0],look[1],look[2])) return 0;
    int jump=!r->has_state||r->snap;
    approach(r->eye,eye,r->eye_sharpness,dt,jump);
    approach(r->look,look,r->look_sharpness,dt,jump);
    r->has_state=1; r->snap=0;
    return athena_camera3d_set_view(r->camera,r->eye,r->look)?1:0;
}
int athena_rig3d_update_all(float dt) {
    if(!athena_float_isfinite(dt)||dt<0) return ATHENA_RIG3D_EINVAL;
    int moved=0;
    for(AthenaCameraRig3D *r=rigs;r;r=r->next) moved+=athena_rig3d_update(r,dt);
    return moved;
}
static int loop_run(void *opaque,AthenaLoopPhase phase,float value) {
    (void)opaque; (void)phase;
    return athena_rig3d_update_all(value)<0?-1:0;
}
static void loop_release(void *opaque) {
    if((uintptr_t)opaque==loop_generation) { loop_id=0; loop_owner=NULL; loop_generation=0; }
}
int athena_rig3d_attach_loop(int priority,void *owner) {
    if(loop_id) return ATHENA_RIG3D_EINVAL;
    static uintptr_t next_generation;
    uintptr_t generation=++next_generation?next_generation:++next_generation;
    AthenaLoopSystemDesc desc={.priority=priority,.phases=ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_POST_UPDATE),
        .func=loop_run,.release=loop_release,.opaque=(void *)generation};
    int id=athena_loop_system_add(&desc);
    if(id<0) return id==ATHENA_LOOP_SYSTEM_ENOMEM?ATHENA_RIG3D_ENOMEM:ATHENA_RIG3D_EINVAL;
    loop_id=id; loop_owner=owner; loop_generation=generation; return id;
}
int athena_rig3d_detach_loop(void) {
    if(!loop_id) return 0;
    int id=loop_id; loop_id=0; loop_owner=NULL; loop_generation=0;
    athena_loop_system_remove(id); return 1;
}
void athena_rig3d_detach_owner(void *owner) { if(loop_id&&loop_owner==owner) athena_rig3d_detach_loop(); }
int athena_rig3d_loop_system(void) { return loop_id; }
