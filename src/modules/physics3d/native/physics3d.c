#include <math.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <athena/float_bits.h>
#include <athena/loop.h>
#include <time.h>
#include "physics3d_internal.h"
/* Sequential impulses (Catto) over contacts rebuilt every substep: velocities
 * are integrated, contacts generated and solved, then positions integrated.
 * Contact normals point from body b (or the level) to body a. */
static Attachment *attached;

/* Rotation matrix and world inverse inertia from the orientation. */
static void update_frame(AthenaBody3D *b) {
    float x=b->q[0],y=b->q[1],z=b->q[2],w=b->q[3];
    float *r=b->rotation;
    r[0]=1-2*(y*y+z*z); r[1]=2*(x*y+w*z);   r[2]=2*(x*z-w*y);
    r[3]=2*(x*y-w*z);   r[4]=1-2*(x*x+z*z); r[5]=2*(y*z+w*x);
    r[6]=2*(x*z+w*y);   r[7]=2*(y*z-w*x);   r[8]=1-2*(x*x+y*y);
    for(int i=0;i<3;i++) for(int j=0;j<3;j++) {
        float s=0;
        for(int k=0;k<3;k++) s+=r[k*3+i]*b->inverse_inertia[k]*r[k*3+j];
        b->world_inverse_inertia[i*3+j]=s;
    }
    float e[3];
    if(b->shape==ATHENA_SHAPE3D_SPHERE) e[0]=e[1]=e[2]=b->radius;
    else if(b->shape==ATHENA_SHAPE3D_CAPSULE) for(int i=0;i<3;i++) e[i]=fabsf(r[3+i])*b->half[1]+b->radius;
    else for(int i=0;i<3;i++) e[i]=fabsf(r[i])*b->half[0]+fabsf(r[3+i])*b->half[1]+fabsf(r[6+i])*b->half[2];
    for(int i=0;i<3;i++) { b->lo[i]=b->p[i]-e[i]; b->hi[i]=b->p[i]+e[i]; }
}
static void sync_node(AthenaBody3D *b) {
    if(!b->node) return;
    athena_node3d_set_position(b->node,b->p[0],b->p[1],b->p[2]);
    athena_node3d_set_rotation(b->node,b->q[0],b->q[1],b->q[2],b->q[3]);
}

void athena_body3d_desc_default(AthenaBody3DDesc *d) {
    *d=(AthenaBody3DDesc){.type=ATHENA_BODY3D_DYNAMIC,.shape=ATHENA_SHAPE3D_SPHERE,.radius=.5f,
        .half={.5f,.5f,.5f},.mass=1,.rotation={0,0,0,1},.friction=.5f,.restitution=0,
        .linear_damping=.05f,.angular_damping=.1f,.rolling_friction=.02f,.layer=1,.mask=0xffffffffu};
}
AthenaPhysics3DWorld *athena_physics3d_world_create(AthenaCollision3DWorld *statics) {
    AthenaPhysics3DWorld *w=calloc(1,sizeof(*w)); if(!w) return NULL;
    w->refs=1; w->statics=statics; athena_collision3d_world_retain(statics);
    w->static_mask=0xffffffffu; w->gravity[1]=-9.81f; w->iterations=8;
    return w;
}
void athena_physics3d_world_retain(AthenaPhysics3DWorld *w) { if(w) w->refs++; }
void athena_physics3d_world_release(AthenaPhysics3DWorld *w) {
    if(!w||--w->refs) return;
    athena_joint3d_release_all(w);
    for(uint32_t i=0;i<w->body_count;i++) { w->bodies[i]->world=NULL; athena_body3d_release(w->bodies[i]); }
    athena_collision3d_world_release(w->statics);
    free(w->solver);
    free(w->bodies); free(w->order); free(w->contacts); free(w->previous); free(w->table); free(w);
}
int athena_physics3d_set_gravity(AthenaPhysics3DWorld *w,float x,float y,float z) {
    float g[3]={x,y,z}; if(!w||!finite_n(g,3)) return ATHENA_PHYSICS3D_EINVAL;
    memcpy(w->gravity,g,sizeof(g));
    for(uint32_t i=0;i<w->body_count;i++) athena_body3d_wake(w->bodies[i]);
    return 0;
}
void athena_physics3d_set_static_mask(AthenaPhysics3DWorld *w,uint32_t mask) { if(w) w->static_mask=mask; }
int athena_physics3d_set_iterations(AthenaPhysics3DWorld *w,int iterations) {
    if(!w||iterations<1||iterations>64) return ATHENA_PHYSICS3D_EINVAL;
    w->iterations=iterations; return 0;
}
uint32_t athena_physics3d_body_count(const AthenaPhysics3DWorld *w) { return w?w->body_count:0; }
uint32_t athena_physics3d_contact_count(const AthenaPhysics3DWorld *w) { return w?w->solved_contacts:0; }

AthenaBody3D *athena_body3d_create(AthenaPhysics3DWorld *w,const AthenaBody3DDesc *d) {
    if(!w||!d||w->body_count>=ATHENA_PHYSICS3D_MAX_BODIES) return NULL;
    if(d->type>ATHENA_BODY3D_STATIC||d->shape>ATHENA_SHAPE3D_CAPSULE) return NULL;
    if(d->shape==ATHENA_SHAPE3D_CAPSULE&&(!athena_float_isfinite(d->half_height)||!(d->half_height>0))) return NULL;
    float numbers[]={d->radius,d->half[0],d->half[1],d->half[2],d->mass,d->friction,d->restitution,
        d->linear_damping,d->angular_damping,d->rolling_friction};
    if(!finite_n(numbers,10)||!finite_n(d->position,3)||!finite_n(d->rotation,4)||
        !finite_n(d->velocity,3)||!finite_n(d->angular_velocity,3)) return NULL;
    if(d->shape!=ATHENA_SHAPE3D_BOX?!(d->radius>0):!(d->half[0]>0&&d->half[1]>0&&d->half[2]>0)) return NULL;
    if((d->type==ATHENA_BODY3D_DYNAMIC&&!(d->mass>0))||d->friction<0||d->restitution<0||d->restitution>1||
        d->linear_damping<0||d->angular_damping<0||d->rolling_friction<0) return NULL;
    float ql=sqrtf(d->rotation[0]*d->rotation[0]+d->rotation[1]*d->rotation[1]+d->rotation[2]*d->rotation[2]+d->rotation[3]*d->rotation[3]);
    if(!(ql>1e-6f)) return NULL;
    if(w->body_count==w->body_capacity) {
        uint32_t capacity=w->body_capacity?w->body_capacity*2:16;
        AthenaBody3D **bodies=realloc(w->bodies,capacity*sizeof(*bodies)); if(!bodies) return NULL;
        w->bodies=bodies;
        uint32_t *order=realloc(w->order,capacity*sizeof(*order)); if(!order) return NULL;
        w->order=order; w->body_capacity=capacity;
    }
    AthenaBody3D *b=memalign(16,sizeof(*b)); if(!b) return NULL;
    memset(b,0,sizeof(*b));
    b->refs=2; /* the world's and the caller's */
    b->world=w; b->type=d->type; b->shape=d->shape; b->radius=d->radius; memcpy(b->half,d->half,sizeof(b->half));
    /* Capsules keep their half height in half[1]. */
    if(b->shape==ATHENA_SHAPE3D_CAPSULE) { b->half[0]=b->half[2]=0; b->half[1]=d->half_height; }
    memcpy(b->p,d->position,sizeof(b->p));
    for(int i=0;i<4;i++) b->q[i]=d->rotation[i]/ql;
    b->friction=d->friction; b->restitution=d->restitution;
    b->linear_damping=d->linear_damping; b->angular_damping=d->angular_damping; b->rolling_friction=d->rolling_friction;
    b->layer=d->layer; b->mask=d->mask;
    if(b->type!=ATHENA_BODY3D_STATIC) { memcpy(b->v,d->velocity,12); memcpy(b->w,d->angular_velocity,12); }
    if(b->type==ATHENA_BODY3D_DYNAMIC) {
        b->inverse_mass=1/d->mass;
        if(b->shape==ATHENA_SHAPE3D_SPHERE) {
            float i=.4f*d->mass*d->radius*d->radius; b->inverse_inertia[0]=b->inverse_inertia[1]=b->inverse_inertia[2]=1/i;
        } else if(b->shape==ATHENA_SHAPE3D_CAPSULE) {
            /* A cylinder of length 2 h and two hemispheres, mass by volume. */
            float r=d->radius,length=2*d->half_height,cylinder=r*r*length,caps=4.0f/3*r*r*r;
            float mc=d->mass*cylinder/(cylinder+caps),ms=d->mass-mc;
            float along=mc*r*r*.5f+ms*.4f*r*r;
            float across=mc*(length*length/12+r*r*.25f)+ms*(.4f*r*r+length*length*.25f+.375f*length*r);
            b->inverse_inertia[0]=b->inverse_inertia[2]=1/across; b->inverse_inertia[1]=1/along;
        } else {
            float x=2*d->half[0],y=2*d->half[1],z=2*d->half[2];
            b->inverse_inertia[0]=12/(d->mass*(y*y+z*z));
            b->inverse_inertia[1]=12/(d->mass*(x*x+z*z));
            b->inverse_inertia[2]=12/(d->mass*(x*x+y*y));
        }
    }
    update_frame(b);
    w->order[w->body_count]=w->body_count;
    b->slot=w->body_count; w->bodies[w->body_count++]=b;
    return b;
}
void athena_body3d_retain(AthenaBody3D *b) { if(b) b->refs++; }
void athena_body3d_release(AthenaBody3D *b) {
    if(!b||--b->refs) return;
    athena_node3d_release(b->node); free(b);
}
void athena_body3d_remove(AthenaBody3D *b) {
    if(!b||!b->world) return;
    AthenaPhysics3DWorld *w=b->world;
    athena_joint3d_body_removed(w,b);
    w->bodies[b->slot]=w->bodies[--w->body_count];
    w->bodies[b->slot]->slot=b->slot;
    /* The sort order is rebuilt: indices may have moved. */
    for(uint32_t i=0;i<w->body_count;i++) w->order[i]=i;
    w->contact_count=0; w->previous_count=0; /* contacts may point at b */
    b->world=NULL; athena_body3d_release(b);
}
int athena_body3d_alive(const AthenaBody3D *b) { return b&&b->world; }
void athena_body3d_wake(AthenaBody3D *b) { if(b&&b->type==ATHENA_BODY3D_DYNAMIC) { b->sleeping=0; b->sleep_time=0; } }
int athena_body3d_sleeping(const AthenaBody3D *b) { return b&&b->sleeping; }
AthenaBody3DType athena_body3d_type(const AthenaBody3D *b) { return b?b->type:ATHENA_BODY3D_STATIC; }
int athena_body3d_set_position(AthenaBody3D *b,float x,float y,float z) {
    float p[3]={x,y,z}; if(!b||!finite_n(p,3)) return ATHENA_PHYSICS3D_EINVAL;
    memcpy(b->p,p,sizeof(p)); update_frame(b); athena_body3d_wake(b); sync_node(b); return 0;
}
int athena_body3d_set_rotation(AthenaBody3D *b,float x,float y,float z,float w) {
    float q[4]={x,y,z,w}; if(!b||!finite_n(q,4)) return ATHENA_PHYSICS3D_EINVAL;
    float l=sqrtf(x*x+y*y+z*z+w*w); if(!(l>1e-6f)) return ATHENA_PHYSICS3D_EINVAL;
    for(int i=0;i<4;i++) b->q[i]=q[i]/l;
    update_frame(b); athena_body3d_wake(b); sync_node(b); return 0;
}
int athena_body3d_set_velocity(AthenaBody3D *b,float x,float y,float z) {
    float v[3]={x,y,z}; if(!b||!finite_n(v,3)) return ATHENA_PHYSICS3D_EINVAL;
    if(b->type==ATHENA_BODY3D_STATIC) return 0;
    memcpy(b->v,v,sizeof(v)); athena_body3d_wake(b); return 0;
}
int athena_body3d_set_angular_velocity(AthenaBody3D *b,float x,float y,float z) {
    float v[3]={x,y,z}; if(!b||!finite_n(v,3)) return ATHENA_PHYSICS3D_EINVAL;
    if(b->type==ATHENA_BODY3D_STATIC) return 0;
    memcpy(b->w,v,sizeof(v)); athena_body3d_wake(b); return 0;
}
void athena_body3d_get_position(const AthenaBody3D *b,float o[3]) { memcpy(o,b->p,sizeof(b->p)); }
void athena_body3d_get_rotation(const AthenaBody3D *b,float o[4]) { memcpy(o,b->q,sizeof(b->q)); }
void athena_body3d_get_velocity(const AthenaBody3D *b,float o[3]) { memcpy(o,b->v,12); }
void athena_body3d_get_angular_velocity(const AthenaBody3D *b,float o[3]) { memcpy(o,b->w,12); }
static void apply_impulse(AthenaBody3D *b,const float j[3],const float r[3],float sign) {
    madd3(b->v,b->v,sign*b->inverse_mass,j);
    float t[3],dw[3]; cross3(t,r,j); mul33(dw,b->world_inverse_inertia,t);
    madd3(b->w,b->w,sign,dw);
}
int athena_body3d_apply_impulse(AthenaBody3D *b,const float impulse[3],const float point[3]) {
    if(!b||!impulse||!finite_n(impulse,3)||(point&&!finite_n(point,3))) return ATHENA_PHYSICS3D_EINVAL;
    if(b->type!=ATHENA_BODY3D_DYNAMIC) return 0;
    float r[3]={0,0,0}; if(point) sub3(r,point,b->p);
    athena_body3d_wake(b); apply_impulse(b,impulse,r,1); return 0;
}
int athena_body3d_apply_force(AthenaBody3D *b,const float force[3]) {
    if(!b||!force||!finite_n(force,3)) return ATHENA_PHYSICS3D_EINVAL;
    if(b->type!=ATHENA_BODY3D_DYNAMIC) return 0;
    athena_body3d_wake(b);
    for(int i=0;i<3;i++) b->force[i]+=force[i];
    return 0;
}
int athena_body3d_bind(AthenaBody3D *b,AthenaNode3D *node) {
    if(!b) return ATHENA_PHYSICS3D_EINVAL;
    athena_node3d_retain(node); athena_node3d_release(b->node); b->node=node;
    sync_node(b); return 0;
}

/* Contacts. */
static int add_contact(AthenaPhysics3DWorld *w,AthenaBody3D *a,AthenaBody3D *b,const float n[3],const float point[3],float depth) {
    if(w->contact_count==w->contact_capacity) {
        uint32_t capacity=w->contact_capacity?w->contact_capacity*2:64;
        Contact *c=realloc(w->contacts,capacity*sizeof(*c)); if(!c) return ATHENA_PHYSICS3D_ENOMEM;
        w->contacts=c; w->contact_capacity=capacity;
    }
    Contact *c=&w->contacts[w->contact_count++];
    memset(c,0,sizeof(*c));
    c->a=a; c->b=b; memcpy(c->n,n,sizeof(c->n)); c->depth=depth;
    sub3(c->ra,point,a->p);
    if(b) sub3(c->rb,point,b->p);
    return 0;
}
static void closest_on_triangle(float out[3],const float p[3],const float a[3],const float b[3],const float c[3]) {
    float ab[3],ac[3],ap[3]; sub3(ab,b,a); sub3(ac,c,a); sub3(ap,p,a);
    float d1=dot3(ab,ap),d2=dot3(ac,ap);
    if(d1<=0&&d2<=0) { memcpy(out,a,12); return; }
    float bp[3]; sub3(bp,p,b); float d3=dot3(ab,bp),d4=dot3(ac,bp);
    if(d3>=0&&d4<=d3) { memcpy(out,b,12); return; }
    float vc=d1*d4-d3*d2;
    if(vc<=0&&d1>=0&&d3<=0) { madd3(out,a,d1/(d1-d3),ab); return; }
    float cp[3]; sub3(cp,p,c); float d5=dot3(ab,cp),d6=dot3(ac,cp);
    if(d6>=0&&d5<=d6) { memcpy(out,c,12); return; }
    float vb=d5*d2-d1*d6;
    if(vb<=0&&d2>=0&&d6<=0) { madd3(out,a,d2/(d2-d6),ac); return; }
    float va=d3*d6-d5*d4;
    if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0) {
        float bc[3]; sub3(bc,c,b); madd3(out,b,(d4-d3)/((d4-d3)+(d5-d6)),bc); return;
    }
    float inverse=1/(va+vb+vc);
    for(int k=0;k<3;k++) out[k]=a[k]+ab[k]*vb*inverse+ac[k]*vc*inverse;
}
static int inside_triangle(const float p[3],const float a[3],const float b[3],const float c[3],const float n[3]) {
    float e[3],q[3],x[3];
    sub3(e,b,a); sub3(q,p,a); cross3(x,e,q); if(dot3(x,n)<0) return 0;
    sub3(e,c,b); sub3(q,p,b); cross3(x,e,q); if(dot3(x,n)<0) return 0;
    sub3(e,a,c); sub3(q,p,c); cross3(x,e,q); if(dot3(x,n)<0) return 0;
    return 1;
}
static void box_corner(float out[3],const AthenaBody3D *b,int k) {
    for(int i=0;i<3;i++) out[i]=b->p[i];
    for(int j=0;j<3;j++) madd3(out,out,(k>>j&1)?b->half[j]:-b->half[j],axis(b,j));
}
/* Level triangles: one-sided (the body centre must be in front). */
/* corners: a box's corners, computed once per query. */
/* corners: a box's corners, computed once per query; used: corners that
 * already have a contact (one per corner, also across triangles sharing an
 * edge). */
typedef struct { AthenaPhysics3DWorld *w; AthenaBody3D *b; int error,count; float corners[8][3]; unsigned used; } StaticContext;
static void capsule_ends(const AthenaBody3D *b,float p0[3],float p1[3]);
static void segment_point(float out[3],const float a[3],const float b[3],float t);
static void segment_segment(const float p1[3],const float q1[3],const float p2[3],const float q2[3],float *s,float *t);
/* A sphere of the body (its own, or a point of a capsule) against a level
 * triangle. */
static void sphere_triangle_at(StaticContext *s,const float centre[3],const float a[3],const float b[3],const float c[3],const float n[3]) {
    AthenaBody3D *body=s->b;
    if(s->error||s->count>=MAX_STATIC_CONTACTS) return;
    float q[3],d[3]; closest_on_triangle(q,centre,a,b,c); sub3(d,centre,q);
    float distance=length3(d);
    if(distance>=body->radius+MARGIN) return;
    float normal[3];
    if(distance>1e-6f) for(int k=0;k<3;k++) normal[k]=d[k]/distance; else memcpy(normal,n,12);
    if(add_contact(s->w,body,NULL,normal,q,body->radius-distance)<0) s->error=1; else s->count++;
}
static void static_triangle(void *context,const float a[3],const float b[3],const float c[3],const float n[3],int shape) {
    (void)shape;
    StaticContext *s=context; AthenaBody3D *body=s->b;
    if(s->error||s->count>=MAX_STATIC_CONTACTS) return;
    float to[3]; sub3(to,body->p,a);
    if(dot3(to,n)<=0) return;
    if(body->shape==ATHENA_SHAPE3D_SPHERE) { sphere_triangle_at(s,body->p,a,b,c,n); return; }
    if(body->shape==ATHENA_SHAPE3D_CAPSULE) {
        /* Both ends, and the closest point to the triangle's edges when it is
         * away from the ends (a capsule lying across a ledge). */
        float p0[3],p1[3]; capsule_ends(body,p0,p1);
        sphere_triangle_at(s,p0,a,b,c,n); sphere_triangle_at(s,p1,a,b,c,n);
        const float *corners[3]={a,b,c}; float best=INFINITY,best_t=0;
        for(int e=0;e<3;e++) {
            float u,v,x[3],y[3],d[3]; segment_segment(p0,p1,corners[e],corners[(e+1)%3],&u,&v);
            segment_point(x,p0,p1,u); segment_point(y,corners[e],corners[(e+1)%3],v); sub3(d,x,y);
            if(dot3(d,d)<best) { best=dot3(d,d); best_t=u; }
        }
        if(best_t>.1f&&best_t<.9f) { float x[3]; segment_point(x,p0,p1,best_t); sphere_triangle_at(s,x,a,b,c,n); }
        return;
    }
    /* Box corners behind the plane whose projection falls inside. */
    float limit=2*fmaxf(body->half[0],fmaxf(body->half[1],body->half[2]));
    for(int k=0;k<8&&s->count<MAX_STATIC_CONTACTS;k++) {
        if(s->used>>k&1) continue;
        const float *v=s->corners[k]; float d[3]; sub3(d,v,a);
        float distance=dot3(d,n);
        if(distance>=MARGIN||distance<-limit) continue;
        float projected[3]; madd3(projected,v,-distance,n);
        if(!inside_triangle(projected,a,b,c,n)) continue;
        if(add_contact(s->w,body,NULL,n,v,-distance)<0) { s->error=1; return; }
        s->count++; s->used|=1u<<k;
    }
    /* Triangle corners inside the box: pushed out along the nearest face. */
    const float *corners[3]={a,b,c};
    for(int k=0;k<3&&s->count<MAX_STATIC_CONTACTS;k++) {
        float d[3],local[3]; sub3(d,corners[k],body->p);
        for(int i=0;i<3;i++) local[i]=dot3(d,axis(body,i));
        int best=-1; float depth=INFINITY;
        for(int i=0;i<3;i++) {
            float inside=body->half[i]-fabsf(local[i]);
            if(inside<=0) { best=-1; break; }
            if(inside<depth) { depth=inside; best=i; }
        }
        if(best<0) continue;
        float normal[3]; float sign=local[best]>0?-1.0f:1.0f;
        for(int i=0;i<3;i++) normal[i]=axis(body,best)[i]*sign;
        if(add_contact(s->w,body,NULL,normal,corners[k],depth)<0) { s->error=1; return; }
        s->count++;
    }
}
static int sphere_sphere(AthenaPhysics3DWorld *w,AthenaBody3D *a,AthenaBody3D *b) {
    float d[3]; sub3(d,a->p,b->p);
    float distance=length3(d),r=a->radius+b->radius;
    if(distance>=r+MARGIN) return 0;
    float n[3]={0,1,0};
    if(distance>1e-6f) for(int k=0;k<3;k++) n[k]=d[k]/distance;
    float point[3]; madd3(point,b->p,b->radius,n);
    return add_contact(w,a,b,n,point,r-distance);
}
/* A sphere of body a (centre, radius: a sphere or a point of a capsule)
 * against box b. */
static int sphere_box_at(AthenaPhysics3DWorld *w,AthenaBody3D *a,const float centre[3],float radius,AthenaBody3D *b) {
    float d[3],local[3],clamped[3]; sub3(d,centre,b->p);
    int inside=1;
    for(int i=0;i<3;i++) {
        local[i]=dot3(d,axis(b,i));
        clamped[i]=fmaxf(-b->half[i],fminf(b->half[i],local[i]));
        if(clamped[i]!=local[i]) inside=0;
    }
    float n[3],point[3],depth;
    if(inside) {
        /* Centre inside: out through the nearest face. */
        int best=0; float least=INFINITY;
        for(int i=0;i<3;i++) { float gap=b->half[i]-fabsf(local[i]); if(gap<least) { least=gap; best=i; } }
        float sign=local[best]>=0?1.0f:-1.0f;
        for(int i=0;i<3;i++) n[i]=axis(b,best)[i]*sign;
        madd3(point,centre,-radius,n);
        depth=least+radius;
    } else {
        for(int i=0;i<3;i++) point[i]=b->p[i];
        for(int i=0;i<3;i++) madd3(point,point,clamped[i],axis(b,i));
        float e[3]; sub3(e,centre,point);
        float distance=length3(e);
        if(distance>=radius+MARGIN||!(distance>1e-6f)) return 0;
        for(int i=0;i<3;i++) n[i]=e[i]/distance;
        depth=radius-distance;
    }
    return add_contact(w,a,b,n,point,depth);
}
static int sphere_box(AthenaPhysics3DWorld *w,AthenaBody3D *a,AthenaBody3D *b) { return sphere_box_at(w,a,a->p,a->radius,b); }
/* Spheres at two points (of a and of b): normal from b's to a's. */
static int spheres_at(AthenaPhysics3DWorld *w,AthenaBody3D *a,const float ca[3],float ra,AthenaBody3D *b,const float cb[3],float rb) {
    float d[3]; sub3(d,ca,cb);
    float distance=length3(d),r=ra+rb;
    if(distance>=r+MARGIN) return 0;
    float n[3]={0,1,0};
    if(distance>1e-6f) for(int k=0;k<3;k++) n[k]=d[k]/distance;
    float point[3]; madd3(point,cb,rb,n);
    return add_contact(w,a,b,n,point,r-distance);
}
/* Capsule segment ends: centre -/+ half height along local y. */
static void capsule_ends(const AthenaBody3D *b,float p0[3],float p1[3]) {
    madd3(p0,b->p,-b->half[1],axis(b,1)); madd3(p1,b->p,b->half[1],axis(b,1));
}
static float closest_on_segment(const float p[3],const float a[3],const float b[3]) {
    float d[3],e[3]; sub3(d,b,a); sub3(e,p,a);
    float dd=dot3(d,d); if(!(dd>1e-12f)) return 0;
    return fmaxf(0,fminf(1,dot3(e,d)/dd));
}
static void segment_point(float out[3],const float a[3],const float b[3],float t) {
    for(int k=0;k<3;k++) out[k]=a[k]+(b[k]-a[k])*t;
}
/* Closest points of segments p1q1 and p2q2 (Ericson 5.1.9): parameters s, t. */
static void segment_segment(const float p1[3],const float q1[3],const float p2[3],const float q2[3],float *s,float *t) {
    float d1[3],d2[3],r[3]; sub3(d1,q1,p1); sub3(d2,q2,p2); sub3(r,p1,p2);
    float a=dot3(d1,d1),e=dot3(d2,d2),f=dot3(d2,r);
    if(a<=1e-12f&&e<=1e-12f) { *s=*t=0; return; }
    if(a<=1e-12f) { *s=0; *t=fmaxf(0,fminf(1,f/e)); return; }
    float c=dot3(d1,r);
    if(e<=1e-12f) { *t=0; *s=fmaxf(0,fminf(1,-c/a)); return; }
    float b=dot3(d1,d2),denominator=a*e-b*b;
    *s=denominator>1e-12f?fmaxf(0,fminf(1,(b*f-c*e)/denominator)):0;
    *t=(b**s+f)/e;
    if(*t<0) { *t=0; *s=fmaxf(0,fminf(1,-c/a)); }
    else if(*t>1) { *t=1; *s=fmaxf(0,fminf(1,(b-c)/a)); }
}
/* a: capsule, b: sphere. */
static int capsule_sphere(AthenaPhysics3DWorld *w,AthenaBody3D *a,AthenaBody3D *b) {
    float p0[3],p1[3],c[3]; capsule_ends(a,p0,p1);
    segment_point(c,p0,p1,closest_on_segment(b->p,p0,p1));
    return spheres_at(w,a,c,a->radius,b,b->p,b->radius);
}
static int sphere_capsule(AthenaPhysics3DWorld *w,AthenaBody3D *a,AthenaBody3D *b) {
    float p0[3],p1[3],c[3]; capsule_ends(b,p0,p1);
    segment_point(c,p0,p1,closest_on_segment(a->p,p0,p1));
    return spheres_at(w,a,a->p,a->radius,b,c,b->radius);
}
/* Two capsules: the closest points, and for near-parallel segments also
 * each end against the other segment (so lying capsules rest on two points). */
static int capsule_capsule(AthenaPhysics3DWorld *w,AthenaBody3D *a,AthenaBody3D *b) {
    float a0[3],a1[3],b0[3],b1[3]; capsule_ends(a,a0,a1); capsule_ends(b,b0,b1);
    float da[3],db[3]; sub3(da,a1,a0); sub3(db,b1,b0);
    float la=length3(da),lb=length3(db);
    if(la>1e-6f&&lb>1e-6f&&fabsf(dot3(da,db))>.98f*la*lb) {
        const float *ends[2][2]={{a0,a1},{b0,b1}};
        for(int e=0;e<2;e++) {
            float c[3]; segment_point(c,b0,b1,closest_on_segment(ends[0][e],b0,b1));
            int code=spheres_at(w,a,ends[0][e],a->radius,b,c,b->radius); if(code<0) return code;
            segment_point(c,a0,a1,closest_on_segment(ends[1][e],a0,a1));
            if((code=spheres_at(w,a,c,a->radius,b,ends[1][e],b->radius))<0) return code;
        }
        return 0;
    }
    float s,t,ca[3],cb[3]; segment_segment(a0,a1,b0,b1,&s,&t);
    segment_point(ca,a0,a1,s); segment_point(cb,b0,b1,t);
    return spheres_at(w,a,ca,a->radius,b,cb,b->radius);
}
/* Point of the segment closest to the box: a few alternating projections. */
static float segment_box_parameter(const AthenaBody3D *box,const float p0[3],const float p1[3]) {
    float t=.5f;
    for(int it=0;it<4;it++) {
        float x[3],d[3],q[3]; segment_point(x,p0,p1,t); sub3(d,x,box->p);
        memcpy(q,box->p,12);
        for(int i=0;i<3;i++) madd3(q,q,fmaxf(-box->half[i],fminf(box->half[i],dot3(d,axis(box,i)))),axis(box,i));
        t=closest_on_segment(q,p0,p1);
    }
    return t;
}
/* a: capsule, b: box: both end spheres, and the closest point when it is
 * away from the ends (a capsule across a box edge). */
static int capsule_box(AthenaPhysics3DWorld *w,AthenaBody3D *a,AthenaBody3D *b) {
    float p0[3],p1[3],c[3]; capsule_ends(a,p0,p1);
    int code=sphere_box_at(w,a,p0,a->radius,b); if(code<0) return code;
    if((code=sphere_box_at(w,a,p1,a->radius,b))<0) return code;
    float t=segment_box_parameter(b,p0,p1);
    if(t>.1f&&t<.9f) { segment_point(c,p0,p1,t); return sphere_box_at(w,a,c,a->radius,b); }
    return 0;
}
static int inside_box(const AthenaBody3D *b,const float v[3],float tolerance) {
    float d[3]; sub3(d,v,b->p);
    for(int i=0;i<3;i++) if(fabsf(dot3(d,axis(b,i)))>b->half[i]+tolerance) return 0;
    return 1;
}
static float project_box(const AthenaBody3D *b,const float l[3]) {
    return b->half[0]*fabsf(dot3(axis(b,0),l))+b->half[1]*fabsf(dot3(axis(b,1),l))+b->half[2]*fabsf(dot3(axis(b,2),l));
}
/* Separating axis test over 15 axes; contacts from the least penetrating:
 * corners inside the other box for face axes, closest edge points for edge
 * axes. */
static int box_box(AthenaPhysics3DWorld *w,AthenaBody3D *a,AthenaBody3D *b) {
    float t[3]; sub3(t,a->p,b->p);
    float best_overlap=INFINITY,best_axis[3]={0,1,0}; int best=-1;
    for(int k=0;k<15;k++) {
        float l[3];
        if(k<3) memcpy(l,axis(a,k),12);
        else if(k<6) memcpy(l,axis(b,k-3),12);
        else {
            cross3(l,axis(a,(k-6)/3),axis(b,(k-6)%3));
            float length=length3(l); if(length<1e-4f) continue;
            for(int i=0;i<3;i++) l[i]/=length;
        }
        float distance=dot3(t,l),overlap=project_box(a,l)+project_box(b,l)-fabsf(distance);
        if(overlap<-MARGIN) return 0;
        /* Edge axes must be clearly better: face contacts are more stable. */
        float score=k<6?overlap:overlap*1.05f+0.001f;
        if(score<best_overlap) {
            best_overlap=score; best=k;
            float sign=distance<0?-1.0f:1.0f;
            for(int i=0;i<3;i++) best_axis[i]=l[i]*sign;
        }
    }
    float *n=best_axis;
    float overlap=project_box(a,n)+project_box(b,n)-fabsf(dot3(t,n));
    int added=0;
    if(best<6) {
        /* Corners of the incident box inside the reference box. */
        AthenaBody3D *reference=best<3?a:b,*incident=best<3?b:a;
        for(int k=0;k<8;k++) {
            float v[3]; box_corner(v,incident,k);
            if(!inside_box(reference,v,MARGIN)) continue;
            /* Depth to the reference face facing the incident box (negative
             * for a corner within the margin). */
            float d[3]; sub3(d,v,reference->p);
            int i=best%3;
            float depth=reference->half[i]-fabsf(dot3(d,axis(reference,i)));
            if(depth>overlap+0.01f) continue;
            if(add_contact(w,a,b,n,v,depth)<0) return ATHENA_PHYSICS3D_ENOMEM;
            added++;
        }
        if(added) return 0;
    } else {
        /* Closest points between the two edges meeting along the axis. */
        int i=(best-6)/3,j=(best-6)%3;
        float pa[3],pb[3];
        memcpy(pa,a->p,12); memcpy(pb,b->p,12);
        for(int k=0;k<3;k++) {
            if(k!=i) madd3(pa,pa,dot3(axis(a,k),n)>0?-a->half[k]:a->half[k],axis(a,k));
            if(k!=j) madd3(pb,pb,dot3(axis(b,k),n)>0?b->half[k]:-b->half[k],axis(b,k));
        }
        const float *da=axis(a,i),*db=axis(b,j);
        float r[3]; sub3(r,pa,pb);
        float e=dot3(da,db),f=dot3(db,r),c=dot3(da,r),denominator=1-e*e;
        float s=denominator>1e-6f?(e*f-c)/denominator:0;
        s=fmaxf(-a->half[i],fminf(a->half[i],s));
        float u=fmaxf(-b->half[j],fminf(b->half[j],e*s+f));
        float qa[3],qb[3],point[3]; madd3(qa,pa,s,da); madd3(qb,pb,u,db);
        for(int k=0;k<3;k++) point[k]=(qa[k]+qb[k])*.5f;
        return add_contact(w,a,b,n,point,overlap);
    }
    /* No corner inside: one contact between the centres. */
    float point[3]; madd3(point,a->p,-(project_box(a,n)-overlap*.5f),n);
    return add_contact(w,a,b,n,point,overlap);
}
static int collide_pair(AthenaPhysics3DWorld *w,AthenaBody3D *a,AthenaBody3D *b) {
    AthenaShape3DType x=a->shape,y=b->shape;
    if(x==ATHENA_SHAPE3D_CAPSULE||y==ATHENA_SHAPE3D_CAPSULE) {
        if(x==ATHENA_SHAPE3D_CAPSULE&&y==ATHENA_SHAPE3D_CAPSULE) return capsule_capsule(w,a,b);
        if(x==ATHENA_SHAPE3D_CAPSULE) return y==ATHENA_SHAPE3D_SPHERE?capsule_sphere(w,a,b):capsule_box(w,a,b);
        return x==ATHENA_SHAPE3D_SPHERE?sphere_capsule(w,a,b):capsule_box(w,b,a);
    }
    if(x==ATHENA_SHAPE3D_SPHERE&&y==ATHENA_SHAPE3D_SPHERE) return sphere_sphere(w,a,b);
    if(x==ATHENA_SHAPE3D_SPHERE) return sphere_box(w,a,b);
    if(y==ATHENA_SHAPE3D_SPHERE) return sphere_box(w,b,a);
    return box_box(w,a,b);
}
static int moving(const AthenaBody3D *b) {
    return b->type==ATHENA_BODY3D_KINEMATIC||(b->type==ATHENA_BODY3D_DYNAMIC&&!b->sleeping);
}
static float speed_sq(const AthenaBody3D *b) { return dot3(b->v,b->v)+dot3(b->w,b->w); }
static int wakes(const AthenaBody3D *b) {
    return (b->type==ATHENA_BODY3D_DYNAMIC&&!b->sleeping)||(b->type==ATHENA_BODY3D_KINEMATIC&&speed_sq(b)>0);
}
static int collide(AthenaPhysics3DWorld *w) {
    w->contact_count=0;
    /* Sort and sweep along x; the order is coherent between steps. */
    uint32_t *o=w->order;
    for(uint32_t i=1;i<w->body_count;i++) {
        uint32_t x=o[i]; float key=w->bodies[x]->lo[0]; uint32_t j=i;
        while(j>0&&w->bodies[o[j-1]]->lo[0]>key) { o[j]=o[j-1]; j--; }
        o[j]=x;
    }
    for(uint32_t i=0;i<w->body_count;i++) {
        AthenaBody3D *a=w->bodies[o[i]];
        for(uint32_t k=i+1;k<w->body_count;k++) {
            AthenaBody3D *b=w->bodies[o[k]];
            if(b->lo[0]>a->hi[0]) break;
            if(b->lo[1]>a->hi[1]||b->hi[1]<a->lo[1]||b->lo[2]>a->hi[2]||b->hi[2]<a->lo[2]) continue;
            if(!(a->mask&b->layer)||!(b->mask&a->layer)) continue;
            if(a->type!=ATHENA_BODY3D_DYNAMIC&&b->type!=ATHENA_BODY3D_DYNAMIC) continue;
            if(!moving(a)&&!moving(b)) continue;
            if(athena_joint3d_joined(w,a,b)) continue;
            /* A sleeping body touched by an awake dynamic body, or by a
             * kinematic one in motion, joins its island again. The sleep
             * timer is kept: a quiet island falls asleep again together. */
            if(a->sleeping&&wakes(b)) a->sleeping=0;
            if(b->sleeping&&wakes(a)) b->sleeping=0;
            /* The dynamic body first, so static/kinematic ones are b. */
            AthenaBody3D *x=a,*y=b;
            if(x->type!=ATHENA_BODY3D_DYNAMIC) { x=b; y=a; }
            if(collide_pair(w,x,y)<0) return ATHENA_PHYSICS3D_ENOMEM;
        }
    }
    if(w->statics) for(uint32_t i=0;i<w->body_count;i++) {
        AthenaBody3D *b=w->bodies[i];
        if(b->type!=ATHENA_BODY3D_DYNAMIC||b->sleeping) continue;
        StaticContext s={.w=w,.b=b};
        if(b->shape==ATHENA_SHAPE3D_BOX) for(int k=0;k<8;k++) box_corner(s.corners[k],b,k);
        if(athena_collision3d_query_triangles(w->statics,b->lo,b->hi,w->static_mask,static_triangle,&s)<0||s.error)
            return ATHENA_PHYSICS3D_ENOMEM;
    }
    return 0;
}
/* Mass and inertia seen by the solver: sleeping and non-dynamic bodies do
 * not move. */
static float inverse_mass(const AthenaBody3D *b) { return b&&b->type==ATHENA_BODY3D_DYNAMIC&&!b->sleeping?b->inverse_mass:0; }
static void relative_velocity(const Contact *c,float dv[3]) {
    float t[3];
    cross3(t,c->a->w,c->ra);
    for(int i=0;i<3;i++) dv[i]=c->a->v[i]+t[i];
    if(c->b) { cross3(t,c->b->w,c->rb); for(int i=0;i<3;i++) dv[i]-=c->b->v[i]+t[i]; }
}
static void set_row(Contact *c,Row *r,const float d[3]) {
    float k=c->ma+c->mb;
    cross3(r->ra,c->ra,d);
    if(c->ma>0) { mul33(r->ia,c->a->world_inverse_inertia,r->ra); k+=dot3(r->ra,r->ia); } else memset(r->ia,0,12);
    if(c->b) cross3(r->rb,c->rb,d); else memset(r->rb,0,12);
    if(c->mb>0) { mul33(r->ib,c->b->world_inverse_inertia,r->rb); k+=dot3(r->rb,r->ib); } else memset(r->ib,0,12);
    r->mass=k>0?1/k:0;
}
/* Relative velocity along a row's direction d. */
/* Inlined: called for every row of every contact in every iteration (GCC
 * kept them out of line, and the calls cost more than their arithmetic). */
static inline __attribute__((always_inline)) float row_velocity(const Contact *c,const Row *r,const float d[3]) {
    float v=dot3(c->a->v,d)+dot3(c->a->w,r->ra);
    if(c->b) v-=dot3(c->b->v,d)+dot3(c->b->w,r->rb);
    return v;
}
static inline __attribute__((always_inline)) void row_push(Contact *c,const Row *r,const float d[3],float lambda) {
    if(c->ma>0) { madd3(c->a->v,c->a->v,c->ma*lambda,d); madd3(c->a->w,c->a->w,lambda,r->ia); }
    if(c->mb>0) { madd3(c->b->v,c->b->v,-c->mb*lambda,d); madd3(c->b->w,c->b->w,-lambda,r->ib); }
}
/* Friction directions: the sliding direction, or a fixed perpendicular
 * (stable from step to step, for warm starting at rest). */
static void set_tangents(Contact *c) {
    float dv[3]; relative_velocity(c,dv);
    float vn=dot3(dv,c->n),tangent[3]; madd3(tangent,dv,-vn,c->n);
    float tl=length3(tangent);
    if(tl>1e-2f) for(int i=0;i<3;i++) c->t1[i]=tangent[i]/tl;
    else {
        float helper[3]={fabsf(c->n[0])<.6f?1.0f:0.0f,fabsf(c->n[0])<.6f?0.0f:1.0f,0};
        cross3(c->t1,c->n,helper); float l=length3(c->t1); for(int i=0;i<3;i++) c->t1[i]/=l;
    }
    cross3(c->t2,c->n,c->t1);
}
static void prepare(Contact *c,float h) {
    c->ma=inverse_mass(c->a); c->mb=inverse_mass(c->b);
    set_row(c,&c->rows[0],c->n); set_row(c,&c->rows[1],c->t1); set_row(c,&c->rows[2],c->t2);
    float vn=row_velocity(c,&c->rows[0],c->n);
    /* Rolling resistance for spheres: an angular impulse budget of
     * coefficient * radius per unit of normal impulse. */
    /* Spheres and capsules roll. */
    float ra=c->a->shape!=ATHENA_SHAPE3D_BOX?c->a->rolling_friction*c->a->radius:0;
    float rb=c->b&&c->b->shape!=ATHENA_SHAPE3D_BOX?c->b->rolling_friction*c->b->radius:0;
    c->rolling=fmaxf(ra,rb);
    float restitution=c->b?fmaxf(c->a->restitution,c->b->restitution):c->a->restitution;
    c->friction=c->b?sqrtf(c->a->friction*c->b->friction):c->a->friction;
    /* Separated (speculative) contacts allow closing the gap this step. */
    float drift=c->depth<0?c->depth/h:BAUMGARTE/h*fmaxf(c->depth-SLOP,0);
    float bounce=vn<-RESTITUTION_THRESHOLD&&c->depth>-SLOP?-restitution*vn:0;
    c->bias=fmaxf(drift,bounce);
    /* Warm start with the impulses carried from the last step. */
    row_push(c,&c->rows[0],c->n,c->pn);
    row_push(c,&c->rows[1],c->t1,c->pt[0]); row_push(c,&c->rows[2],c->t2,c->pt[1]);
}
static uint32_t pair_hash(const AthenaBody3D *a,const AthenaBody3D *b,uint32_t size) {
    uintptr_t x=(uintptr_t)a*2654435761u^(uintptr_t)b*40503u;
    return (uint32_t)(x^(x>>15))&(size-1);
}
/* Contacts of one pair are contiguous: the table maps a pair to its first
 * contact in the previous step (open addressing, index + 1, 0 = empty). */
static int index_previous(AthenaPhysics3DWorld *w) {
    uint32_t size=16; while(size<w->previous_count*2) size*=2;
    if(size>w->table_size) {
        uint32_t *t=realloc(w->table,size*sizeof(*t)); if(!t) return ATHENA_PHYSICS3D_ENOMEM;
        w->table=t; w->table_size=size;
    }
    size=w->table_size; memset(w->table,0,size*sizeof(*w->table));
    for(uint32_t i=0;i<w->previous_count;i++) {
        const Contact *c=&w->previous[i];
        if(i&&w->previous[i-1].a==c->a&&w->previous[i-1].b==c->b) continue;
        uint32_t h=pair_hash(c->a,c->b,size);
        while(w->table[h]) h=(h+1)&(size-1);
        w->table[h]=i+1;
    }
    return 0;
}
static void match_previous(AthenaPhysics3DWorld *w,Contact *c) {
    if(!w->previous_count) return;
    uint32_t size=w->table_size,h=pair_hash(c->a,c->b,size);
    for(;w->table[h];h=(h+1)&(size-1)) {
        uint32_t first=w->table[h]-1;
        if(w->previous[first].a!=c->a||w->previous[first].b!=c->b) continue;
        float best=MATCH_DISTANCE_SQ; const Contact *found=NULL;
        for(uint32_t i=first;i<w->previous_count&&w->previous[i].a==c->a&&w->previous[i].b==c->b;i++) {
            float d[3]; sub3(d,w->previous[i].ra,c->ra);
            float e=dot3(d,d); if(e<best) { best=e; found=&w->previous[i]; }
        }
        if(found&&dot3(found->n,c->n)>0.9f) {
            /* Friction impulses re-expressed in the new tangent frame. */
            float f[3]; for(int i=0;i<3;i++) f[i]=found->t1[i]*found->pt[0]+found->t2[i]*found->pt[1];
            c->pn=found->pn; c->pt[0]=dot3(f,c->t1); c->pt[1]=dot3(f,c->t2);
        }
        return;
    }
}
/* Islands: dynamic bodies joined by contacts (union-find over the solved
 * contacts) fall asleep together, once all of them have been slow for
 * SLEEP_TIME; so a stack never has a sleeping (immovable) body on an awake
 * one. */
static uint32_t find(AthenaPhysics3DWorld *w,uint32_t i) {
    while(w->bodies[i]->island!=i) {
        uint32_t parent=w->bodies[i]->island;
        w->bodies[i]->island=w->bodies[parent]->island; i=parent;
    }
    return i;
}
static void sleep_islands(AthenaPhysics3DWorld *w) {
    for(uint32_t i=0;i<w->body_count;i++) w->bodies[i]->island=i;
    for(uint32_t i=0;i<w->previous_count;i++) {
        const Contact *c=&w->previous[i];
        if(!c->b||c->a->type!=ATHENA_BODY3D_DYNAMIC||c->b->type!=ATHENA_BODY3D_DYNAMIC) continue;
        if(i&&w->previous[i-1].a==c->a&&w->previous[i-1].b==c->b) continue;
        uint32_t x=find(w,c->a->slot),y=find(w,c->b->slot);
        if(x!=y) w->bodies[x]->island=y;
    }
    for(uint32_t i=0;i<w->joint_count;i++) {
        const AthenaJoint3D *j=w->joints[i];
        if(!j->b||j->a->type!=ATHENA_BODY3D_DYNAMIC||j->b->type!=ATHENA_BODY3D_DYNAMIC) continue;
        uint32_t x=find(w,j->a->slot),y=find(w,j->b->slot);
        if(x!=y) w->bodies[x]->island=y;
    }
    /* The least sleep time of each island, kept at its root. */
    for(uint32_t i=0;i<w->body_count;i++) {
        AthenaBody3D *b=w->bodies[i];
        if(b->type!=ATHENA_BODY3D_DYNAMIC||b->sleeping) continue;
        AthenaBody3D *root=w->bodies[find(w,i)];
        if(root!=b&&b->sleep_time<root->sleep_time) root->sleep_time=b->sleep_time;
    }
    for(uint32_t i=0;i<w->body_count;i++) {
        AthenaBody3D *b=w->bodies[i];
        if(b->type!=ATHENA_BODY3D_DYNAMIC||b->sleeping) continue;
        if(w->bodies[find(w,i)]->sleep_time>=SLEEP_TIME) { b->sleeping=1; memset(b->v,0,12); memset(b->w,0,12); }
    }
}
static float microseconds(clock_t ticks) { return (float)ticks*(1e6f/CLOCKS_PER_SEC); }
/* A body that does not move in the solver: its rows are zero (mb = 0, ib = 0),
 * so the packed solver may read and write here freely. */
static float still[2][4] __attribute__((aligned(16)));
static int pack_contacts(AthenaPhysics3DWorld *w) {
    if(w->contact_count>w->solver_capacity) {
        uint32_t capacity=w->solver_capacity?w->solver_capacity:64;
        while(capacity<w->contact_count) capacity*=2;
        SolverContact *s=memalign(16,capacity*sizeof(*s)); if(!s) return ATHENA_PHYSICS3D_ENOMEM;
        free(w->solver); w->solver=s; w->solver_capacity=capacity;
    }
    for(uint32_t i=0;i<w->contact_count;i++) {
        Contact *c=&w->contacts[i]; SolverContact *s=&w->solver[i];
        const float *dirs[3]={c->n,c->t1,c->t2};
        for(int r=0;r<3;r++) {
            memcpy(s->d[r],dirs[r],12); memcpy(s->ra[r],c->rows[r].ra,12); memcpy(s->rb[r],c->rows[r].rb,12);
            memcpy(s->ia[r],c->rows[r].ia,12); memcpy(s->ib[r],c->rows[r].ib,12);
            s->d[r][3]=s->ra[r][3]=s->rb[r][3]=s->ia[r][3]=s->ib[r][3]=0;
        }
        s->k[0]=c->rows[0].mass; s->k[1]=c->rows[1].mass; s->k[2]=c->rows[2].mass; s->k[3]=c->bias;
        s->impulses[0]=c->pn; s->impulses[1]=c->pt[0]; s->impulses[2]=c->pt[1]; s->impulses[3]=c->friction;
        s->inverse[0]=c->ma; s->inverse[1]=c->mb; s->inverse[2]=s->inverse[3]=0;
        s->va=c->a->v; s->wa=c->a->w;
        s->vb=c->b&&c->mb>0?c->b->v:c->b?c->b->v:still[0]; s->wb=c->b?c->b->w:still[1];
        s->source=c;
    }
    return 0;
}
static void unpack_contacts(AthenaPhysics3DWorld *w) {
    for(uint32_t i=0;i<w->contact_count;i++) {
        const SolverContact *s=&w->solver[i]; Contact *c=s->source;
        c->pn=s->impulses[0]; c->pt[0]=s->impulses[1]; c->pt[1]=s->impulses[2];
    }
}
/* One contact: friction rows (clamped by friction * pn), then the normal
 * row (pn >= 0). On the EE the whole contact is one VU0 macro-mode block:
 * the four velocities stay in VU registers across its three rows. */
#if defined(__mips__)
#define SOLVE_FRICTION_ROW(R,LANE) \
    "lqc2 $vf8, " #R "*16(%4)\n"      /* d */ \
    "lqc2 $vf9, " #R "*16+48(%4)\n"   /* ra */ \
    "lqc2 $vf10, " #R "*16+96(%4)\n"  /* rb */ \
    "lqc2 $vf11, " #R "*16+144(%4)\n" /* ia */ \
    "lqc2 $vf12, " #R "*16+192(%4)\n" /* ib */ \
    "vmula.xyz $ACC, $vf1, $vf8\n" \
    "vmadda.xyz $ACC, $vf2, $vf9\n" \
    "vmsuba.xyz $ACC, $vf3, $vf8\n" \
    "vmsub.xyz $vf13, $vf4, $vf10\n" \
    "vaddy.x $vf13, $vf13, $vf13\n" \
    "vaddz.x $vf13, $vf13, $vf13\n" \
    "vmul" LANE ".x $vf14, $vf13, $vf5\n"   /* v * mass */ \
    "vsub.x $vf14, $vf0, $vf14\n"           /* lambda */ \
    "vmulw.x $vf15, $vf6, $vf6\n"           /* limit = pn * friction */ \
    "vadd" LANE ".x $vf16, $vf14, $vf6\n"   /* total = lambda + pt */ \
    "vmini.x $vf16, $vf16, $vf15\n" \
    "vsub.x $vf17, $vf0, $vf15\n" \
    "vmax.x $vf16, $vf16, $vf17\n" \
    "vsub" LANE ".x $vf14, $vf16, $vf6\n"   /* lambda = total - pt */ \
    "vaddx." LANE " $vf6, $vf0, $vf16\n"    /* pt = total */ \
    "vmulx.x $vf17, $vf7, $vf14\n"          /* ma * lambda */ \
    "vmulx.y $vf17, $vf7, $vf14\n"          /* mb * lambda */ \
    "vmulx.xyz $vf18, $vf8, $vf17\n" "vadd.xyz $vf1, $vf1, $vf18\n" \
    "vmuly.xyz $vf18, $vf8, $vf17\n" "vsub.xyz $vf3, $vf3, $vf18\n" \
    "vmulx.xyz $vf18, $vf11, $vf14\n" "vadd.xyz $vf2, $vf2, $vf18\n" \
    "vmulx.xyz $vf18, $vf12, $vf14\n" "vsub.xyz $vf4, $vf4, $vf18\n"
#endif
static void solve_packed(SolverContact *s) {
#if defined(__mips__)
    __asm__ __volatile__(
        "lqc2 $vf1, 0(%0)\n" "lqc2 $vf2, 0(%1)\n" "lqc2 $vf3, 0(%2)\n" "lqc2 $vf4, 0(%3)\n"
        "lqc2 $vf5, 240(%4)\n"   /* k */
        "lqc2 $vf6, 256(%4)\n"   /* impulses */
        "lqc2 $vf7, 272(%4)\n"   /* inverse masses */
        SOLVE_FRICTION_ROW(1,"y")
        SOLVE_FRICTION_ROW(2,"z")
        /* Normal row: lambda = (bias - v) * mass, pn >= 0. */
        "lqc2 $vf8, 0(%4)\n" "lqc2 $vf9, 48(%4)\n" "lqc2 $vf10, 96(%4)\n"
        "lqc2 $vf11, 144(%4)\n" "lqc2 $vf12, 192(%4)\n"
        "vmula.xyz $ACC, $vf1, $vf8\n"
        "vmadda.xyz $ACC, $vf2, $vf9\n"
        "vmsuba.xyz $ACC, $vf3, $vf8\n"
        "vmsub.xyz $vf13, $vf4, $vf10\n"
        "vaddy.x $vf13, $vf13, $vf13\n"
        "vaddz.x $vf13, $vf13, $vf13\n"
        "vaddw.x $vf14, $vf0, $vf5\n"
        "vsub.x $vf14, $vf14, $vf13\n"
        "vmul.x $vf14, $vf14, $vf5\n"
        "vadd.x $vf16, $vf14, $vf6\n"
        "vmax.x $vf16, $vf16, $vf0\n"
        "vsub.x $vf14, $vf16, $vf6\n"
        "vaddx.x $vf6, $vf0, $vf16\n"
        "vmulx.x $vf17, $vf7, $vf14\n"
        "vmulx.y $vf17, $vf7, $vf14\n"
        "vmulx.xyz $vf18, $vf8, $vf17\n" "vadd.xyz $vf1, $vf1, $vf18\n"
        "vmuly.xyz $vf18, $vf8, $vf17\n" "vsub.xyz $vf3, $vf3, $vf18\n"
        "vmulx.xyz $vf18, $vf11, $vf14\n" "vadd.xyz $vf2, $vf2, $vf18\n"
        "vmulx.xyz $vf18, $vf12, $vf14\n" "vsub.xyz $vf4, $vf4, $vf18\n"
        "sqc2 $vf1, 0(%0)\n" "sqc2 $vf2, 0(%1)\n" "sqc2 $vf3, 0(%2)\n" "sqc2 $vf4, 0(%3)\n"
        "sqc2 $vf6, 256(%4)\n"
        : : "r"(s->va), "r"(s->wa), "r"(s->vb), "r"(s->wb), "r"(s) : "memory");
#else
    float *va=s->va,*wa=s->wa,*vb=s->vb,*wb=s->wb,*imp=s->impulses;
    for(int r=0;r<3;r++) {
        /* Friction rows 1 and 2 first, then the normal row 0. */
        int row=r<2?r+1:0;
        const float *d=s->d[row];
        float v=dot3(va,d)+dot3(wa,s->ra[row])-dot3(vb,d)-dot3(wb,s->rb[row]),lambda,total;
        if(row) {
            float limit=imp[0]*imp[3];
            lambda=-v*s->k[row];
            total=fmaxf(-limit,fminf(limit,imp[row]+lambda));
        } else {
            lambda=(s->k[3]-v)*s->k[0];
            total=fmaxf(imp[0]+lambda,0);
        }
        lambda=total-imp[row]; imp[row]=total;
        madd3(va,va,s->inverse[0]*lambda,d); madd3(vb,vb,-s->inverse[1]*lambda,d);
        madd3(wa,wa,lambda,s->ia[row]); madd3(wb,wb,-lambda,s->ib[row]);
    }
#endif
}
/* Rolling resistance, once per substep after the iterations: a dissipative
 * angular impulse against the relative spin, at most rolling * normal
 * impulse (in the iterations it cost a square root and two inertia products
 * per contact and iteration). */
static void roll(Contact *c) {
    if(c->rolling>0) {
        /* Oppose the relative spin, within this substep's budget. */
        float spin[3]; memcpy(spin,c->a->w,12);
        if(c->b) sub3(spin,spin,c->b->w);
        float speed=length3(spin),budget=c->rolling*c->pn-c->rolling_used;
        if(speed<1e-5f||budget<=0) return;
        float d[3]={spin[0]/speed,spin[1]/speed,spin[2]/speed},ia[3]={0,0,0},ib[3]={0,0,0};
        float k=0;
        if(c->ma>0) { mul33(ia,c->a->world_inverse_inertia,d); k+=dot3(d,ia); }
        if(c->mb>0) { mul33(ib,c->b->world_inverse_inertia,d); k+=dot3(d,ib); }
        if(!(k>0)) return;
        float impulse=fminf(speed/k,budget); c->rolling_used+=impulse;
        madd3(c->a->w,c->a->w,-impulse,ia); if(c->mb>0) madd3(c->b->w,c->b->w,impulse,ib);
    }
}
static int substep(AthenaPhysics3DWorld *w,float h) {
    clock_t t0=clock();
    for(uint32_t i=0;i<w->body_count;i++) {
        AthenaBody3D *b=w->bodies[i];
        if(b->type!=ATHENA_BODY3D_DYNAMIC||b->sleeping) continue;
        for(int k=0;k<3;k++) b->v[k]+=(w->gravity[k]+b->force[k]*b->inverse_mass)*h;
        float linear=1/(1+h*b->linear_damping),angular=1/(1+h*b->angular_damping);
        for(int k=0;k<3;k++) { b->v[k]*=linear; b->w[k]*=angular; }
    }
    int code=collide(w); if(code<0) return code;
    clock_t t1=clock();
    if((code=index_previous(w))<0) return code;
    athena_joint3d_prepare_all(w,h);
    for(uint32_t i=0;i<w->contact_count;i++) { set_tangents(&w->contacts[i]); match_previous(w,&w->contacts[i]); prepare(&w->contacts[i],h); }
    clock_t t2=clock();
    if((code=pack_contacts(w))<0) return code;
    for(int it=0;it<w->iterations;it++) {
        athena_joint3d_solve_all(w);
        for(uint32_t i=0;i<w->contact_count;i++) solve_packed(&w->solver[i]);
    }
    unpack_contacts(w);
    for(uint32_t i=0;i<w->contact_count;i++) roll(&w->contacts[i]);
    clock_t t3=clock();
    w->solved_contacts=w->contact_count;
    /* These contacts warm start the next substep. */
    Contact *swap=w->previous; uint32_t capacity=w->previous_capacity;
    w->previous=w->contacts; w->previous_capacity=w->contact_capacity; w->previous_count=w->contact_count;
    w->contacts=swap; w->contact_capacity=capacity; w->contact_count=0;
    for(uint32_t i=0;i<w->body_count;i++) {
        AthenaBody3D *b=w->bodies[i];
        if(!moving(b)) continue;
        madd3(b->p,b->p,h,b->v);
        /* q += h/2 * (w, 0) * q, renormalized. */
        float *q=b->q,x=b->w[0],y=b->w[1],z=b->w[2];
        float dq[4]={x*q[3]+y*q[2]-z*q[1],y*q[3]+z*q[0]-x*q[2],z*q[3]+x*q[1]-y*q[0],-x*q[0]-y*q[1]-z*q[2]};
        for(int k=0;k<4;k++) q[k]+=.5f*h*dq[k];
        float l=sqrtf(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
        if(!(l>1e-6f)||!finite_n(b->p,3)) return ATHENA_PHYSICS3D_EINVAL;
        for(int k=0;k<4;k++) q[k]/=l;
        update_frame(b);
        if(b->type==ATHENA_BODY3D_DYNAMIC) {
            if(dot3(b->v,b->v)<SLEEP_LINEAR&&dot3(b->w,b->w)<SLEEP_ANGULAR) b->sleep_time+=h;
            else b->sleep_time=0;
        }
    }
    sleep_islands(w);
    for(uint32_t i=0;i<w->body_count;i++) memset(w->bodies[i]->force,0,12);
    clock_t t4=clock();
    w->running.collide+=microseconds(t1-t0); w->running.prepare+=microseconds(t2-t1);
    w->running.solve+=microseconds(t3-t2); w->running.integrate+=microseconds(t4-t3); w->running.substeps++;
    return 0;
}
void athena_physics3d_profile(const AthenaPhysics3DWorld *w,AthenaPhysics3DProfile *out) {
    if(w) *out=w->profile; else memset(out,0,sizeof(*out));
}
int athena_physics3d_step(AthenaPhysics3DWorld *w,float dt) {
    if(!w||!athena_float_isfinite(dt)||dt<0) return ATHENA_PHYSICS3D_EINVAL;
    w->accumulator+=dt;
    int steps=0;
    memset(&w->running,0,sizeof(w->running));
    while(w->accumulator>=SUBSTEP*0.999f&&steps<MAX_SUBSTEPS) {
        int code=substep(w,SUBSTEP); if(code<0) return code;
        w->accumulator-=SUBSTEP; steps++;
    }
    /* Too slow to keep up: drop the backlog instead of spiralling. */
    if(w->accumulator>SUBSTEP*MAX_SUBSTEPS) w->accumulator=0;
    if(w->accumulator<0) w->accumulator=0;
    w->profile=w->running;
    /* Bodies that fell asleep in this step moved in it too: sync every
     * non-static body. */
    if(steps) for(uint32_t i=0;i<w->body_count;i++) if(w->bodies[i]->type!=ATHENA_BODY3D_STATIC) sync_node(w->bodies[i]);
    return steps;
}

/* Loop systems, one per world (retained while attached). */
static int loop_update(void *opaque,AthenaLoopPhase phase,float value) {
    (void)phase; Attachment *a=opaque;
    return athena_physics3d_step(a->world,value)<0?-1:0;
}
static void unlink_attachment(Attachment *a) {
    for(Attachment **p=&attached;*p;p=&(*p)->next) if(*p==a) { *p=a->next; break; }
    if(a->world->attachment==a) a->world->attachment=NULL;
}
static void loop_release(void *opaque) {
    Attachment *a=opaque; unlink_attachment(a);
    athena_physics3d_world_release(a->world); free(a);
}
int athena_physics3d_attach_loop(AthenaPhysics3DWorld *w,int priority,void *owner) {
    if(!w||w->attachment) return ATHENA_PHYSICS3D_EINVAL;
    Attachment *a=calloc(1,sizeof(*a)); if(!a) return ATHENA_PHYSICS3D_ENOMEM;
    a->world=w; a->owner=owner;
    AthenaLoopSystemDesc desc={.priority=priority,.phases=ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_POST_UPDATE),
        .func=loop_update,.release=loop_release,.opaque=a};
    int id=athena_loop_system_add(&desc);
    if(id<0) { free(a); return id==ATHENA_LOOP_SYSTEM_ENOMEM?ATHENA_PHYSICS3D_ENOMEM:ATHENA_PHYSICS3D_EINVAL; }
    athena_physics3d_world_retain(w);
    a->id=id; a->next=attached; attached=a; w->attachment=a;
    return id;
}
int athena_physics3d_detach_loop(AthenaPhysics3DWorld *w) {
    if(!w||!w->attachment) return 0;
    Attachment *a=w->attachment;
    unlink_attachment(a);
    athena_loop_system_remove(a->id); return 1;
}
void athena_physics3d_detach_owner(void *owner) {
    for(Attachment *a=attached,*next;a;a=next) {
        next=a->next;
        if(a->owner==owner) athena_physics3d_detach_loop(a->world);
    }
}
int athena_physics3d_loop_system(const AthenaPhysics3DWorld *w) { return w&&w->attachment?w->attachment->id:0; }
