#include "physics3d_internal.h"
/* Joints as velocity constraints in the same sequential-impulse iterations as
 * the contacts, with Baumgarte position correction:
 * - point (ball, hinge, weld): a 3x3 block keeping the anchors together;
 * - angular rows (hinge: 2 perpendicular to the axis, weld: 3);
 * - hinge limit and motor rows about the axis; distance: one row.
 * Relative velocities are a's minus b's; impulses go +P to a, -P to b. */
#define JOINT_BAUMGARTE 0.2f

/* A missing b is the world: at rest, at the origin, unrotated. */
static void to_world(float out[3],const AthenaBody3D *b,const float local[3]) {
    if(!b) { memcpy(out,local,12); return; }
    for(int i=0;i<3;i++) out[i]=b->rotation[i]*local[0]+b->rotation[3+i]*local[1]+b->rotation[6+i]*local[2];
}
static void to_local(float out[3],const AthenaBody3D *b,const float world[3]) {
    if(!b) { memcpy(out,world,12); return; }
    for(int i=0;i<3;i++) out[i]=dot3(axis(b,i),world);
}
static void point_to_local(float out[3],const AthenaBody3D *b,const float point[3]) {
    float d[3]; if(b) sub3(d,point,b->p); else memcpy(d,point,12);
    to_local(out,b,d);
}
static float joint_inverse_mass(const AthenaBody3D *b) {
    return b&&b->type==ATHENA_BODY3D_DYNAMIC&&!b->sleeping?b->inverse_mass:0;
}
static void quat_mul(float o[4],const float a[4],const float b[4]) {
    float x=a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],y=a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0];
    float z=a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],w=a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2];
    o[0]=x; o[1]=y; o[2]=z; o[3]=w;
}
/* conj(qb) * qa: a's rotation in b's frame, unchanged when both turn
 * together (a world-frame qa * conj(qb) would be conjugated by that turn). */
static void relative_rotation(float o[4],const AthenaBody3D *a,const AthenaBody3D *b) {
    float qb[4]={0,0,0,1};
    if(b) { qb[0]=-b->q[0]; qb[1]=-b->q[1]; qb[2]=-b->q[2]; qb[3]=b->q[3]; }
    quat_mul(o,qb,a->q);
}
static void perpendicular(float out[3],const float n[3]) {
    float helper[3]={fabsf(n[0])<.6f?1.0f:0.0f,fabsf(n[0])<.6f?0.0f:1.0f,0};
    cross3(out,n,helper); float l=length3(out); for(int i=0;i<3;i++) out[i]/=l;
}

static AthenaJoint3D *create(AthenaPhysics3DWorld *w,AthenaJoint3DType type,AthenaBody3D *a,AthenaBody3D *b) {
    if(!w||!a||a==b||a->world!=w||(b&&b->world!=w)) return NULL;
    if(a->type!=ATHENA_BODY3D_DYNAMIC&&(!b||b->type!=ATHENA_BODY3D_DYNAMIC)) return NULL;
    if(w->joint_count>=ATHENA_PHYSICS3D_MAX_JOINTS) return NULL;
    if(w->joint_count==w->joint_capacity) {
        uint32_t capacity=w->joint_capacity?w->joint_capacity*2:8;
        AthenaJoint3D **joints=realloc(w->joints,capacity*sizeof(*joints)); if(!joints) return NULL;
        w->joints=joints; w->joint_capacity=capacity;
    }
    AthenaJoint3D *j=calloc(1,sizeof(*j)); if(!j) return NULL;
    j->refs=2; j->world=w; j->type=type; j->a=a; j->b=b;
    athena_body3d_retain(a); athena_body3d_retain(b);
    a->joints++; if(b) b->joints++;
    j->slot=w->joint_count; w->joints[w->joint_count++]=j;
    athena_body3d_wake(a); athena_body3d_wake(b);
    return j;
}
AthenaJoint3D *athena_joint3d_ball(AthenaPhysics3DWorld *w,AthenaBody3D *a,AthenaBody3D *b,const float anchor[3]) {
    if(!anchor||!finite_n(anchor,3)) return NULL;
    AthenaJoint3D *j=create(w,ATHENA_JOINT3D_BALL,a,b); if(!j) return NULL;
    point_to_local(j->local_a,a,anchor); point_to_local(j->local_b,b,anchor);
    return j;
}
AthenaJoint3D *athena_joint3d_hinge(AthenaPhysics3DWorld *w,AthenaBody3D *a,AthenaBody3D *b,
    const float anchor[3],const float hinge_axis[3]) {
    if(!anchor||!hinge_axis||!finite_n(anchor,3)||!finite_n(hinge_axis,3)) return NULL;
    float n[3]; float l=length3(hinge_axis); if(!(l>1e-6f)) return NULL;
    for(int i=0;i<3;i++) n[i]=hinge_axis[i]/l;
    AthenaJoint3D *j=create(w,ATHENA_JOINT3D_HINGE,a,b); if(!j) return NULL;
    point_to_local(j->local_a,a,anchor); point_to_local(j->local_b,b,anchor);
    to_local(j->axis_a,a,n); to_local(j->axis_b,b,n);
    float ref[3]; perpendicular(ref,n);
    to_local(j->ref_a,a,ref); to_local(j->ref_b,b,ref);
    return j;
}
AthenaJoint3D *athena_joint3d_distance(AthenaPhysics3DWorld *w,AthenaBody3D *a,AthenaBody3D *b,
    const float anchor_a[3],const float anchor_b[3],float length,int rope) {
    if(!anchor_a||!anchor_b||!finite_n(anchor_a,3)||!finite_n(anchor_b,3)||!athena_float_isfinite(length)) return NULL;
    float d[3]; sub3(d,anchor_a,anchor_b);
    if(length<0) length=length3(d);
    AthenaJoint3D *j=create(w,ATHENA_JOINT3D_DISTANCE,a,b); if(!j) return NULL;
    point_to_local(j->local_a,a,anchor_a); point_to_local(j->local_b,b,anchor_b);
    j->length=length; j->rope=!!rope;
    return j;
}
AthenaJoint3D *athena_joint3d_weld(AthenaPhysics3DWorld *w,AthenaBody3D *a,AthenaBody3D *b,const float anchor[3]) {
    if(!anchor||!finite_n(anchor,3)) return NULL;
    AthenaJoint3D *j=create(w,ATHENA_JOINT3D_WELD,a,b); if(!j) return NULL;
    point_to_local(j->local_a,a,anchor); point_to_local(j->local_b,b,anchor);
    relative_rotation(j->rest,a,b);
    return j;
}
void athena_joint3d_retain(AthenaJoint3D *j) { if(j) j->refs++; }
void athena_joint3d_release(AthenaJoint3D *j) {
    if(!j||--j->refs) return;
    athena_body3d_release(j->a); athena_body3d_release(j->b); free(j);
}
void athena_joint3d_remove(AthenaJoint3D *j) {
    if(!j||!j->world) return;
    AthenaPhysics3DWorld *w=j->world;
    w->joints[j->slot]=w->joints[--w->joint_count];
    w->joints[j->slot]->slot=j->slot;
    j->a->joints--; if(j->b) j->b->joints--;
    athena_body3d_wake(j->a); athena_body3d_wake(j->b);
    j->world=NULL; athena_joint3d_release(j);
}
int athena_joint3d_alive(const AthenaJoint3D *j) { return j&&j->world; }
AthenaJoint3DType athena_joint3d_type(const AthenaJoint3D *j) { return j?j->type:ATHENA_JOINT3D_BALL; }
int athena_joint3d_set_limits(AthenaJoint3D *j,float lower,float upper) {
    if(!j||j->type!=ATHENA_JOINT3D_HINGE||!athena_float_isfinite(lower)||!athena_float_isfinite(upper)||lower>upper)
        return ATHENA_PHYSICS3D_EINVAL;
    j->limits=1; j->lower=lower; j->upper=upper; j->limit_impulse=0;
    athena_body3d_wake(j->a); athena_body3d_wake(j->b); return 0;
}
void athena_joint3d_disable_limits(AthenaJoint3D *j) { if(j) { j->limits=0; j->limit_impulse=0; } }
int athena_joint3d_set_motor(AthenaJoint3D *j,float speed,float max_torque) {
    if(!j||j->type!=ATHENA_JOINT3D_HINGE||!athena_float_isfinite(speed)||!athena_float_isfinite(max_torque)||max_torque<0)
        return ATHENA_PHYSICS3D_EINVAL;
    j->motor=max_torque>0; j->motor_speed=speed; j->motor_max=max_torque;
    if(!j->motor) j->motor_impulse=0;
    athena_body3d_wake(j->a); athena_body3d_wake(j->b); return 0;
}
float athena_joint3d_angle(const AthenaJoint3D *j) { return j&&j->type==ATHENA_JOINT3D_HINGE?j->angle:0; }
/* Bodies joined by a joint do not collide with each other (a hinge's parts
 * overlap at the axis). Only bodies with joints pay for the scan. */
int athena_joint3d_joined(const AthenaPhysics3DWorld *w,const AthenaBody3D *a,const AthenaBody3D *b) {
    if(!a->joints||!b->joints) return 0;
    for(uint32_t i=0;i<w->joint_count;i++) {
        const AthenaJoint3D *j=w->joints[i];
        if((j->a==a&&j->b==b)||(j->a==b&&j->b==a)) return 1;
    }
    return 0;
}
uint32_t athena_physics3d_joint_count(const AthenaPhysics3DWorld *w) { return w?w->joint_count:0; }

void athena_joint3d_body_removed(AthenaPhysics3DWorld *w,AthenaBody3D *body) {
    for(uint32_t i=w->joint_count;i-->0;) {
        AthenaJoint3D *j=w->joints[i];
        if(j->a==body||j->b==body) athena_joint3d_remove(j);
    }
}
void athena_joint3d_release_all(AthenaPhysics3DWorld *w) {
    for(uint32_t i=0;i<w->joint_count;i++) { w->joints[i]->world=NULL; athena_joint3d_release(w->joints[i]); }
    free(w->joints); w->joints=NULL; w->joint_count=w->joint_capacity=0;
}

/* Velocity of a's anchor relative to b's. */
static void anchor_velocity(const AthenaJoint3D *j,float dv[3]) {
    float t[3]; cross3(t,j->a->w,j->ra);
    for(int i=0;i<3;i++) dv[i]=j->a->v[i]+t[i];
    if(j->b) { cross3(t,j->b->w,j->rb); for(int i=0;i<3;i++) dv[i]-=j->b->v[i]+t[i]; }
}
static void push_linear(AthenaJoint3D *j,const float p[3]) {
    float t[3],u[3];
    if(j->ma>0) {
        madd3(j->a->v,j->a->v,j->ma,p); cross3(t,j->ra,p); mul33(u,j->a->world_inverse_inertia,t); madd3(j->a->w,j->a->w,1,u);
    }
    if(j->mb>0) {
        madd3(j->b->v,j->b->v,-j->mb,p); cross3(t,j->rb,p); mul33(u,j->b->world_inverse_inertia,t); madd3(j->b->w,j->b->w,-1,u);
    }
}
static void push_angular(AthenaJoint3D *j,const float d[3],float lambda) {
    float u[3];
    if(j->ma>0) { mul33(u,j->a->world_inverse_inertia,d); madd3(j->a->w,j->a->w,lambda,u); }
    if(j->mb>0) { mul33(u,j->b->world_inverse_inertia,d); madd3(j->b->w,j->b->w,-lambda,u); }
}
static float angular_mass(const AthenaJoint3D *j,const float d[3]) {
    float u[3],k=0;
    if(j->ma>0) { mul33(u,j->a->world_inverse_inertia,d); k+=dot3(d,u); }
    if(j->mb>0) { mul33(u,j->b->world_inverse_inertia,d); k+=dot3(d,u); }
    return k>1e-12f?1/k:0;
}
static float relative_spin(const AthenaJoint3D *j,const float d[3]) {
    float s=dot3(j->a->w,d);
    if(j->b) s-=dot3(j->b->w,d);
    return s;
}
/* K = (ma + mb) I + sum over bodies of (I^-1 (r x e)) x r, by columns. */
static void point_mass(AthenaJoint3D *j) {
    float k[9];
    for(int c=0;c<3;c++) {
        float e[3]={c==0,c==1,c==2},col[3]={(j->ma+j->mb)*e[0],(j->ma+j->mb)*e[1],(j->ma+j->mb)*e[2]},t[3],u[3],v[3];
        if(j->ma>0) { cross3(t,j->ra,e); mul33(u,j->a->world_inverse_inertia,t); cross3(v,u,j->ra); madd3(col,col,1,v); }
        if(j->mb>0) { cross3(t,j->rb,e); mul33(u,j->b->world_inverse_inertia,t); cross3(v,u,j->rb); madd3(col,col,1,v); }
        for(int r=0;r<3;r++) k[r*3+c]=col[r];
    }
    float c00=k[4]*k[8]-k[5]*k[7],c01=k[5]*k[6]-k[3]*k[8],c02=k[3]*k[7]-k[4]*k[6];
    float det=k[0]*c00+k[1]*c01+k[2]*c02;
    if(!(fabsf(det)>1e-12f)) { memset(j->k_inverse,0,sizeof(j->k_inverse)); return; }
    float i=1/det,*o=j->k_inverse;
    o[0]=c00*i; o[1]=(k[2]*k[7]-k[1]*k[8])*i; o[2]=(k[1]*k[5]-k[2]*k[4])*i;
    o[3]=c01*i; o[4]=(k[0]*k[8]-k[2]*k[6])*i; o[5]=(k[2]*k[3]-k[0]*k[5])*i;
    o[6]=c02*i; o[7]=(k[1]*k[6]-k[0]*k[7])*i; o[8]=(k[0]*k[4]-k[1]*k[3])*i;
}
static void prepare(AthenaJoint3D *j,float h) {
    float beta=JOINT_BAUMGARTE/h;
    j->ma=joint_inverse_mass(j->a); j->mb=joint_inverse_mass(j->b);
    to_world(j->ra,j->a,j->local_a);
    if(j->b) to_world(j->rb,j->b,j->local_b); else memset(j->rb,0,12);
    float pa[3],pb[3];
    for(int i=0;i<3;i++) { pa[i]=j->a->p[i]+j->ra[i]; pb[i]=(j->b?j->b->p[i]:0)+(j->b?j->rb[i]:j->local_b[i]); }
    j->angular_rows=0; j->limit_state=0;
    if(j->type==ATHENA_JOINT3D_DISTANCE) {
        float d[3]; sub3(d,pa,pb);
        float l=length3(d);
        if(l>1e-6f) for(int i=0;i<3;i++) j->n[i]=d[i]/l;
        else { j->n[0]=0; j->n[1]=1; j->n[2]=0; }
        float c=l-j->length,t[3],u[3],v[3],k=j->ma+j->mb;
        if(j->ma>0) { cross3(t,j->ra,j->n); mul33(u,j->a->world_inverse_inertia,t); cross3(v,u,j->ra); k+=dot3(v,j->n); }
        if(j->mb>0) { cross3(t,j->rb,j->n); mul33(u,j->b->world_inverse_inertia,t); cross3(v,u,j->rb); k+=dot3(v,j->n); }
        j->distance_mass=k>1e-12f?1/k:0;
        /* A slack rope may close the gap within the step, not pull. */
        j->distance_bias=j->rope&&c<0?c/h:beta*c;
        if(j->rope&&c<0&&j->distance_impulse<0) j->distance_impulse=0;
        float p[3]; for(int i=0;i<3;i++) p[i]=j->n[i]*j->distance_impulse;
        push_linear(j,p);
        return;
    }
    point_mass(j);
    for(int i=0;i<3;i++) j->bias[i]=beta*(pa[i]-pb[i]);
    push_linear(j,j->impulse);
    if(j->type==ATHENA_JOINT3D_HINGE) {
        float axis_a[3],axis_b[3],error[3];
        to_world(axis_a,j->a,j->axis_a); to_world(axis_b,j->b,j->axis_b);
        memcpy(j->hinge_axis,axis_a,12);
        /* b's axis to a's: a small rotation phi about t gives about phi t. */
        cross3(error,axis_b,axis_a);
        perpendicular(j->angular_axes[0],axis_a); cross3(j->angular_axes[1],axis_a,j->angular_axes[0]);
        j->angular_rows=2;
        for(int r=0;r<2;r++) {
            j->angular_mass[r]=angular_mass(j,j->angular_axes[r]);
            j->angular_bias[r]=beta*dot3(error,j->angular_axes[r]);
        }
        float ref_a[3],ref_b[3],x[3];
        to_world(ref_a,j->a,j->ref_a); to_world(ref_b,j->b,j->ref_b);
        cross3(x,ref_b,ref_a);
        j->angle=atan2f(dot3(x,axis_a),dot3(ref_b,ref_a));
        j->axis_mass=angular_mass(j,axis_a);
        if(j->limits) {
            if(j->angle<=j->lower+.01f) { j->limit_state=-1; float c=j->angle-j->lower; j->limit_bias=c<0?beta*c:c/h; }
            else if(j->angle>=j->upper-.01f) { j->limit_state=1; float c=j->upper-j->angle; j->limit_bias=c<0?beta*c:c/h; }
        }
        if(!j->limit_state) j->limit_impulse=0;
        if(!j->motor) j->motor_impulse=0;
        push_angular(j,axis_a,j->motor_impulse+(j->limit_state>0?-j->limit_impulse:j->limit_impulse));
    } else if(j->type==ATHENA_JOINT3D_WELD) {
        /* conj(qb) qa = rest * delta: delta is a's deviation in its own
         * frame; 2 vec(delta), turned to world by a, is the error. */
        float q[4],e[4],rest_inverse[4]={-j->rest[0],-j->rest[1],-j->rest[2],j->rest[3]};
        relative_rotation(q,j->a,j->b); quat_mul(e,rest_inverse,q);
        float sign=e[3]<0?-1.0f:1.0f,local[3]={2*sign*e[0],2*sign*e[1],2*sign*e[2]},error[3];
        to_world(error,j->a,local);
        j->angular_rows=3;
        for(int r=0;r<3;r++) {
            float d[3]={r==0,r==1,r==2};
            memcpy(j->angular_axes[r],d,12);
            j->angular_mass[r]=angular_mass(j,d);
            j->angular_bias[r]=beta*error[r];
        }
    }
    for(int r=0;r<j->angular_rows;r++) push_angular(j,j->angular_axes[r],j->angular_impulse[r]);
}
static void solve(AthenaJoint3D *j) {
    float dv[3];
    if(j->type==ATHENA_JOINT3D_DISTANCE) {
        anchor_velocity(j,dv);
        float lambda=-(dot3(dv,j->n)+j->distance_bias)*j->distance_mass;
        if(j->rope) {
            /* A rope only pulls (negative impulse along a - b). */
            float total=fminf(j->distance_impulse+lambda,0);
            lambda=total-j->distance_impulse; j->distance_impulse=total;
        } else j->distance_impulse+=lambda;
        float p[3]={j->n[0]*lambda,j->n[1]*lambda,j->n[2]*lambda};
        push_linear(j,p);
        return;
    }
    if(j->type==ATHENA_JOINT3D_HINGE) {
        if(j->motor) {
            float lambda=-(relative_spin(j,j->hinge_axis)-j->motor_speed)*j->axis_mass;
            float limit=j->motor_max*SUBSTEP,total=fmaxf(-limit,fminf(limit,j->motor_impulse+lambda));
            lambda=total-j->motor_impulse; j->motor_impulse=total;
            push_angular(j,j->hinge_axis,lambda);
        }
        if(j->limit_state) {
            /* Lower: the angle may only grow; upper: only shrink. */
            float s=(float)-j->limit_state;
            float lambda=-(s*relative_spin(j,j->hinge_axis)+j->limit_bias)*j->axis_mass;
            float total=fmaxf(j->limit_impulse+lambda,0);
            lambda=total-j->limit_impulse; j->limit_impulse=total;
            push_angular(j,j->hinge_axis,s*lambda);
        }
    }
    for(int r=0;r<j->angular_rows;r++) {
        float lambda=-(relative_spin(j,j->angular_axes[r])+j->angular_bias[r])*j->angular_mass[r];
        j->angular_impulse[r]+=lambda;
        push_angular(j,j->angular_axes[r],lambda);
    }
    anchor_velocity(j,dv);
    float rhs[3]={-(dv[0]+j->bias[0]),-(dv[1]+j->bias[1]),-(dv[2]+j->bias[2])},p[3];
    mul33(p,j->k_inverse,rhs);
    for(int i=0;i<3;i++) j->impulse[i]+=p[i];
    push_linear(j,p);
}
void athena_joint3d_prepare_all(AthenaPhysics3DWorld *w,float h) {
    for(uint32_t i=0;i<w->joint_count;i++) {
        AthenaJoint3D *j=w->joints[i];
        /* A joint ties its bodies' sleep: an awake one wakes the other. */
        int awake_a=j->a->type==ATHENA_BODY3D_DYNAMIC&&!j->a->sleeping;
        int awake_b=j->b&&j->b->type==ATHENA_BODY3D_DYNAMIC&&!j->b->sleeping;
        if(awake_a&&j->b&&j->b->sleeping) j->b->sleeping=0;
        if(awake_b&&j->a->sleeping) j->a->sleeping=0;
        prepare(j,h);
    }
}
void athena_joint3d_solve_all(AthenaPhysics3DWorld *w) {
    for(uint32_t i=0;i<w->joint_count;i++) {
        AthenaJoint3D *j=w->joints[i];
        if(j->ma>0||j->mb>0) solve(j);
    }
}
