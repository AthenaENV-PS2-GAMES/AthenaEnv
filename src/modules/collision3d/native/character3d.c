#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <athena/float_bits.h>
#include <athena/loop.h>
#include "collision3d_internal.h"
/* Kinematic characters: an upright ellipsoid moved with Fauerby's
 * collide-and-slide in ellipsoid space. Up is +Y. */
struct AthenaCharacter3D {
    uint64_t refs;
    AthenaCollision3DWorld *world;
    AthenaCharacter3DDesc desc;
    float radius[3],cos_slope;
    float feet[3],velocity[3];
    AthenaCharacter3DState state;
    AthenaNode3D *node;
    uint8_t enabled;
    struct AthenaCharacter3D *prev,*next;
};
/* Every live character, for athena_character3d_step_all(). Main thread only. */
static AthenaCharacter3D *characters;
static int loop_id;
static void *loop_owner;
static uintptr_t loop_generation;
/* Distance kept from surfaces, in ellipsoid space. */
#define VERY_CLOSE 0.005f
#define MAX_SLIDES 5

void athena_character3d_desc_default(AthenaCharacter3DDesc *d) {
    *d=(AthenaCharacter3DDesc){.radius=.4f,.height=1.8f,.step_height=.3f,.max_slope=45,
        .gravity={0,-9.81f,0},.mask=0xffffffffu};
}
static int valid_desc(const AthenaCharacter3DDesc *d) {
    if(!d) return 0;
    float v[7]={d->radius,d->height,d->step_height,d->max_slope,d->gravity[0],d->gravity[1],d->gravity[2]};
    for(int i=0;i<7;i++) if(!athena_float_isfinite(v[i])) return 0;
    return d->radius>0&&d->height>0&&d->step_height>=0&&d->step_height<d->height&&
        d->max_slope>=0&&d->max_slope<90;
}
AthenaCharacter3D *athena_character3d_create(AthenaCollision3DWorld *world,const AthenaCharacter3DDesc *desc) {
    AthenaCharacter3DDesc d; athena_character3d_desc_default(&d);
    if(desc) d=*desc;
    if(!world||!valid_desc(&d)) return NULL;
    AthenaCharacter3D *c=calloc(1,sizeof(*c)); if(!c) return NULL;
    c->refs=1; c->world=world; athena_collision3d_world_retain(world);
    c->desc=d; c->enabled=1;
    c->radius[0]=c->radius[2]=d.radius; c->radius[1]=d.height*.5f;
    c->cos_slope=cosf(d.max_slope*(3.14159265358979323846f/180));
    c->state.ground_normal[1]=1;
    c->next=characters; if(characters) characters->prev=c; characters=c;
    return c;
}
void athena_character3d_retain(AthenaCharacter3D *c) { if(c) c->refs++; }
void athena_character3d_release(AthenaCharacter3D *c) {
    if(!c||--c->refs) return;
    if(c->prev) c->prev->next=c->next; else characters=c->next;
    if(c->next) c->next->prev=c->prev;
    athena_node3d_release(c->node); athena_collision3d_world_release(c->world); free(c);
}
static void sync_node(AthenaCharacter3D *c) {
    if(c->node) athena_node3d_set_position(c->node,c->feet[0],c->feet[1],c->feet[2]);
}
int athena_character3d_set_position(AthenaCharacter3D *c,float x,float y,float z) {
    if(!c||!athena_float_isfinite(x)||!athena_float_isfinite(y)||!athena_float_isfinite(z)) return ATHENA_COLLISION3D_EINVAL;
    c->feet[0]=x; c->feet[1]=y; c->feet[2]=z; c->state.on_ground=0;
    sync_node(c); return 0;
}
void athena_character3d_get_position(const AthenaCharacter3D *c,float out[3]) { memcpy(out,c->feet,sizeof(c->feet)); }
int athena_character3d_set_velocity(AthenaCharacter3D *c,float x,float y,float z) {
    if(!c||!athena_float_isfinite(x)||!athena_float_isfinite(y)||!athena_float_isfinite(z)) return ATHENA_COLLISION3D_EINVAL;
    c->velocity[0]=x; c->velocity[1]=y; c->velocity[2]=z; return 0;
}
void athena_character3d_get_velocity(const AthenaCharacter3D *c,float out[3]) { memcpy(out,c->velocity,sizeof(c->velocity)); }
void athena_character3d_state(const AthenaCharacter3D *c,AthenaCharacter3DState *out) { *out=c->state; }
int athena_character3d_bind(AthenaCharacter3D *c,AthenaNode3D *node) {
    if(!c) return ATHENA_COLLISION3D_EINVAL;
    athena_node3d_retain(node); athena_node3d_release(c->node); c->node=node;
    sync_node(c); return 0;
}
void athena_character3d_set_enabled(AthenaCharacter3D *c,int enabled) { if(c) c->enabled=!!enabled; }
int athena_character3d_enabled(const AthenaCharacter3D *c) { return c&&c->enabled; }

/* What one slide touched, classified by the world normal. */
typedef struct { int ground,wall,ceiling,hit,error; float ground_normal[3]; } Contacts;
enum { SLIDE_HORIZONTAL, SLIDE_VERTICAL, SLIDE_PROBE };
static float length3(const float v[3]) { return sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }
/* Moves the ellipsoid centre pos (ellipsoid space) by vel. HORIZONTAL slides
 * along everything, steep slopes acting as vertical walls; VERTICAL stops on
 * ground (no sliding down walkable slopes) and slides elsewhere; PROBE stops
 * at the first contact. */
static void slide(AthenaCharacter3D *c,float pos[3],float vel[3],int mode,Contacts *k) {
    for(int iteration=0;iteration<MAX_SLIDES;iteration++) {
        float length=length3(vel);
        if(length<1e-6f) return;
        AthenaSweep3D s={.mask=c->desc.mask};
        memcpy(s.radius,c->radius,sizeof(s.radius)); memcpy(s.base,pos,sizeof(s.base)); memcpy(s.velocity,vel,sizeof(s.velocity));
        if(athena_collision3d_sweep(c->world,&s)<0) { k->error=1; return; }
        if(!s.found) { for(int i=0;i<3;i++) pos[i]+=vel[i]; return; }
        float unit[3]={vel[0]/length,vel[1]/length,vel[2]/length},dest[3],point[3];
        for(int i=0;i<3;i++) { dest[i]=pos[i]+vel[i]; point[i]=s.point[i]; }
        if(s.distance>=VERY_CLOSE) {
            float advance=s.distance-VERY_CLOSE;
            for(int i=0;i<3;i++) { pos[i]+=unit[i]*advance; point[i]-=unit[i]*VERY_CLOSE; }
        }
        float n[3]={pos[0]-point[0],pos[1]-point[1],pos[2]-point[2]},nl=length3(n);
        if(!(nl>1e-9f)) return;
        for(int i=0;i<3;i++) n[i]/=nl;
        /* World normal of the surface: n / radius, renormalized. */
        float wn[3]={n[0]/c->radius[0],n[1]/c->radius[1],n[2]/c->radius[2]},wl=length3(wn);
        for(int i=0;i<3;i++) wn[i]/=wl;
        /* Faces are ground up to max_slope. An edge or vertex no higher than
         * step_height above the feet is a ledge to climb: the ellipsoid's
         * round bottom slides up and over it like a ramp. */
        float above=(point[1]-(pos[1]-1))*c->radius[1];
        int walkable=wn[1]>=c->cos_slope||(s.edge&&wn[1]>0&&above<=c->desc.step_height+1e-4f);
        k->hit=1;
        if(walkable) { k->ground=1; memcpy(k->ground_normal,wn,sizeof(wn)); }
        else if(wn[1]<-0.7f) k->ceiling=1;
        else k->wall=1;
        if(mode==SLIDE_PROBE||(mode==SLIDE_VERTICAL&&walkable)) return;
        if(mode==SLIDE_HORIZONTAL&&!walkable&&wn[1]>0) {
            /* A steep slope blocks like a wall instead of being climbed. */
            float h=sqrtf(wn[0]*wn[0]+wn[2]*wn[2]);
            if(!(h>1e-6f)) return;
            float e[3]={wn[0]/h*c->radius[0],0,wn[2]/h*c->radius[2]},el=length3(e);
            for(int i=0;i<3;i++) n[i]=e[i]/el;
        }
        /* The rest of the motion projected onto the slide plane. Equal to
         * Fauerby's newDestination - intersectionPoint for the true normal,
         * and still tangent when a steep slope's normal was made horizontal. */
        float rest[3]={dest[0]-pos[0],dest[1]-pos[1],dest[2]-pos[2]};
        float along=rest[0]*n[0]+rest[1]*n[1]+rest[2]*n[2];
        for(int i=0;i<3;i++) vel[i]=rest[i]-along*n[i];
    }
}
static void to_center(const AthenaCharacter3D *c,const float feet[3],float pos[3]) {
    pos[0]=feet[0]/c->radius[0]; pos[1]=(feet[1]+c->radius[1])/c->radius[1]; pos[2]=feet[2]/c->radius[2];
}
static void to_feet(const AthenaCharacter3D *c,const float pos[3],float feet[3]) {
    feet[0]=pos[0]*c->radius[0]; feet[1]=pos[1]*c->radius[1]-c->radius[1]; feet[2]=pos[2]*c->radius[2];
}
static float planar_sq(const float a[3],const float b[3]) {
    float x=a[0]-b[0],z=a[2]-b[2]; return x*x+z*z;
}
static int move_by(AthenaCharacter3D *c,const float d[3]) {
    int was_ground=c->state.on_ground;
    float pos[3]; to_center(c,c->feet,pos);
    float horizontal[3]={d[0]/c->radius[0],0,d[2]/c->radius[2]};
    float step=c->desc.step_height/c->radius[1];
    Contacts k={0};
    int ground=0; float ground_normal[3]={0,1,0};
    if(length3(horizontal)>1e-9f) {
        float a[3]; memcpy(a,pos,sizeof(a));
        float h[3]; memcpy(h,horizontal,sizeof(h));
        Contacts ka={0}; slide(c,a,h,SLIDE_HORIZONTAL,&ka);
        if(ka.wall&&was_ground&&d[1]<=0&&step>0) {
            /* Step up: rise, move, then come down onto the ledge. */
            float b[3]; memcpy(b,pos,sizeof(b));
            float up[3]={0,step,0}; Contacts ku={0}; slide(c,b,up,SLIDE_PROBE,&ku);
            float risen=b[1]-pos[1];
            memcpy(h,horizontal,sizeof(h)); Contacts kb={0}; slide(c,b,h,SLIDE_HORIZONTAL,&kb);
            float down[3]={0,-(risen+2*VERY_CLOSE),0}; Contacts kd={0}; slide(c,b,down,SLIDE_PROBE,&kd);
            k.error|=ku.error|kb.error|kd.error;
            if(kd.ground&&planar_sq(b,pos)>planar_sq(a,pos)+1e-6f) {
                memcpy(pos,b,sizeof(b)); k.wall=kb.wall; k.ceiling=ku.ceiling|kb.ceiling;
                ground=1; memcpy(ground_normal,kd.ground_normal,sizeof(ground_normal));
            } else { memcpy(pos,a,sizeof(a)); k.wall=ka.wall; k.ceiling=ka.ceiling; k.error|=ka.error; }
        } else {
            memcpy(pos,a,sizeof(a)); k.wall=ka.wall; k.ceiling=ka.ceiling; k.error|=ka.error;
            if(ka.ground&&d[1]<=0) { ground=1; memcpy(ground_normal,ka.ground_normal,sizeof(ground_normal)); }
        }
    }
    if(d[1]!=0) {
        float v[3]={0,d[1]/c->radius[1],0}; Contacts kv={0};
        slide(c,pos,v,SLIDE_VERTICAL,&kv);
        k.error|=kv.error;
        if(d[1]<0&&kv.ground) { ground=1; memcpy(ground_normal,kv.ground_normal,sizeof(ground_normal)); }
        else if(d[1]<0) ground=0;
        if(d[1]>0&&kv.ceiling) k.ceiling=1;
        if(kv.wall) k.wall=1;
    }
    /* Walking down slopes and steps: stay on the ground. */
    if(was_ground&&!ground&&d[1]<=0&&step>0) {
        float p[3]; memcpy(p,pos,sizeof(p));
        float down[3]={0,-step,0}; Contacts ks={0}; slide(c,p,down,SLIDE_PROBE,&ks);
        k.error|=ks.error;
        if(ks.ground) { memcpy(pos,p,sizeof(p)); ground=1; memcpy(ground_normal,ks.ground_normal,sizeof(ground_normal)); }
    }
    if(k.error) return ATHENA_COLLISION3D_ENOMEM; /* the tree could not be built: nothing moved */
    to_feet(c,pos,c->feet);
    c->state.on_ground=ground; c->state.hit_wall=k.wall; c->state.hit_ceiling=k.ceiling;
    memcpy(c->state.ground_normal,ground?ground_normal:(float[3]){0,1,0},sizeof(ground_normal));
    sync_node(c);
    return 0;
}
int athena_character3d_move(AthenaCharacter3D *c,float dx,float dy,float dz) {
    if(!c||!athena_float_isfinite(dx)||!athena_float_isfinite(dy)||!athena_float_isfinite(dz)) return ATHENA_COLLISION3D_EINVAL;
    float d[3]={dx,dy,dz}; return move_by(c,d);
}
int athena_character3d_step(AthenaCharacter3D *c,float dt) {
    if(!c||!athena_float_isfinite(dt)||dt<0) return ATHENA_COLLISION3D_EINVAL;
    for(int i=0;i<3;i++) c->velocity[i]+=c->desc.gravity[i]*dt;
    float d[3]={c->velocity[0]*dt,c->velocity[1]*dt,c->velocity[2]*dt};
    for(int i=0;i<3;i++) if(!athena_float_isfinite(d[i])) return ATHENA_COLLISION3D_EINVAL;
    int code=move_by(c,d);
    if(code<0) return code;
    if(c->state.on_ground&&c->velocity[1]<0) c->velocity[1]=0;
    if(c->state.hit_ceiling&&c->velocity[1]>0) c->velocity[1]=0;
    return 0;
}
int athena_character3d_step_all(float dt) {
    if(!athena_float_isfinite(dt)||dt<0) return ATHENA_COLLISION3D_EINVAL;
    for(AthenaCharacter3D *c=characters;c;c=c->next) {
        if(!c->enabled) continue;
        int code=athena_character3d_step(c,dt);
        if(code<0) return code;
    }
    return 0;
}
static int loop_run(void *opaque,AthenaLoopPhase phase,float value) {
    (void)opaque; (void)phase;
    return athena_character3d_step_all(value)<0?-1:0;
}
static void loop_release(void *opaque) {
    if((uintptr_t)opaque==loop_generation) { loop_id=0; loop_owner=NULL; loop_generation=0; }
}
int athena_character3d_attach_loop(int priority,void *owner) {
    if(loop_id) return ATHENA_COLLISION3D_EINVAL;
    static uintptr_t next_generation;
    uintptr_t generation=++next_generation?next_generation:++next_generation;
    AthenaLoopSystemDesc desc={.priority=priority,.phases=ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_POST_UPDATE),
        .func=loop_run,.release=loop_release,.opaque=(void *)generation};
    int id=athena_loop_system_add(&desc);
    if(id<0) return id==ATHENA_LOOP_SYSTEM_ENOMEM?ATHENA_COLLISION3D_ENOMEM:ATHENA_COLLISION3D_EINVAL;
    loop_id=id; loop_owner=owner; loop_generation=generation; return id;
}
int athena_character3d_detach_loop(void) {
    if(!loop_id) return 0;
    int id=loop_id; loop_id=0; loop_owner=NULL; loop_generation=0;
    athena_loop_system_remove(id); return 1;
}
void athena_character3d_detach_owner(void *owner) {
    if(loop_id&&loop_owner==owner) athena_character3d_detach_loop();
}
int athena_character3d_loop_system(void) { return loop_id; }
