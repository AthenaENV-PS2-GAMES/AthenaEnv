#include <math.h>
#include <string.h>
#include <athena/camera3d.h>
#include <athena/float_bits.h>

static int view_matrix(AthenaMatrix4 *out, const AthenaVector4 *p,
    const AthenaVector4 *target, const AthenaVector4 *up) {
    double z[3]={(double)p->x-target->x, (double)p->y-target->y, (double)p->z-target->z};
    double n=sqrt(z[0]*z[0]+z[1]*z[1]+z[2]*z[2]);
    if (!athena_double_isfinite(n) || n <= 1e-12) return 0;
    for (int i=0;i<3;i++) z[i]/=n;
    double x[3]={up->y*z[2]-up->z*z[1], up->z*z[0]-up->x*z[2], up->x*z[1]-up->y*z[0]};
    n=sqrt(x[0]*x[0]+x[1]*x[1]+x[2]*x[2]);
    if (!athena_double_isfinite(n) || n <= 1e-12) return 0;
    for (int i=0;i<3;i++) x[i]/=n;
    double y[3]={z[1]*x[2]-z[2]*x[1], z[2]*x[0]-z[0]*x[2], z[0]*x[1]-z[1]*x[0]};
    AthenaMatrix4 m={{x[0],y[0],z[0],0, x[1],y[1],z[1],0, x[2],y[2],z[2],0,
        -(x[0]*p->x+x[1]*p->y+x[2]*p->z), -(y[0]*p->x+y[1]*p->y+y[2]*p->z),
        -(z[0]*p->x+z[1]*p->y+z[2]*p->z), 1}};
    for(int i=0;i<16;i++) if(!athena_float_isfinite(m.value[i])) return 0;
    *out=m; return 1;
}

void athena_camera3d_init(AthenaCamera3D *c) {
    memset(c,0,sizeof(*c));
    c->position=(AthenaVector4){0,0,5,1}; c->target=(AthenaVector4){0,0,0,1};
    c->up=(AthenaVector4){0,1,0,0};
    athena_camera3d_set_projection(c,60,4.0f/3.0f,0.1f,300);
    athena_camera3d_update(c);
}

int athena_camera3d_set_projection(AthenaCamera3D *c, float fov, float aspect, float near, float far) {
    if (!athena_float_isfinite(fov)||fov<=0||fov>=179||!athena_float_isfinite(aspect)||aspect<=0||
        !athena_float_isfinite(near)||near<=0||!athena_float_isfinite(far)||far<=near) return 0;
    double f=1/tan(fov*(3.14159265358979323846/360));
    double range=(double)far-near;
    AthenaMatrix4 p={{0}};
    p.value[0]=f/aspect; p.value[5]=f;
    p.value[10]=((double)far+near)/range; p.value[11]=-1;
    p.value[14]=(2*(double)far*near)/range;
    for(int i=0;i<16;i++) if(!athena_float_isfinite(p.value[i])) return 0;
    c->projection=p; c->fov_y_degrees=fov; c->aspect=aspect;
    c->near_clip=near; c->far_clip=far; c->dirty=true; return 1;
}

static int set_vector(AthenaCamera3D *c, AthenaVector4 *dst, float x,float y,float z) {
    AthenaVector4 v={x,y,z,dst->w}; AthenaMatrix4 test;
    if(!athena_float_isfinite(x)||!athena_float_isfinite(y)||!athena_float_isfinite(z)) return 0;
    if(!view_matrix(&test,dst==&c->position?&v:&c->position,
        dst==&c->target?&v:&c->target,dst==&c->up?&v:&c->up)) return 0;
    *dst=v; c->dirty=true; return 1;
}
int athena_camera3d_set_position(AthenaCamera3D *c,float x,float y,float z) { return set_vector(c,&c->position,x,y,z); }
int athena_camera3d_look_at(AthenaCamera3D *c,float x,float y,float z) { return set_vector(c,&c->target,x,y,z); }
int athena_camera3d_set_up(AthenaCamera3D *c,float x,float y,float z) { return set_vector(c,&c->up,x,y,z); }
int athena_camera3d_update(AthenaCamera3D *c) {
    if(!c->dirty) return 1;
    AthenaMatrix4 view, vp;
    if(!view_matrix(&view,&c->position,&c->target,&c->up)) return 0;
    ath_matrix4_multiply(&vp,&c->projection,&view);
    for(int i=0;i<16;i++) if(!athena_float_isfinite(vp.value[i])) return 0;
    c->view=view; c->view_projection=vp; c->dirty=false; return 1;
}
int athena_camera3d_box_relation(AthenaCamera3D *c,const AthenaMatrix4 *model,const float min[3],const float max[3]) {
    AthenaMatrix4 m;
    if(!c||!min||!max) return -1;
    for(unsigned i=0;i<3;i++) if(!athena_float_isfinite(min[i])||!athena_float_isfinite(max[i])||min[i]>max[i]) return -1;
    if(!athena_camera3d_update(c)) return -1;
    for(unsigned i=0;i<16;i++) if(!athena_float_isfinite(c->view_projection.value[i])||
        (model && !athena_float_isfinite(model->value[i]))) return -1;
    if(model) ath_matrix4_multiply(&m,&c->view_projection,model); else m=c->view_projection;
    unsigned common=63; int contained=1;
    for(int i=0;i<8;i++) {
        AthenaVector4 in={i&1?max[0]:min[0],i&2?max[1]:min[1],i&4?max[2]:min[2],1},v;
        ath_matrix4_apply(&v,&m,&in);
        if(!athena_float_isfinite(v.x)||!athena_float_isfinite(v.y)||
            !athena_float_isfinite(v.z)||!athena_float_isfinite(v.w)) return -1;
        unsigned flags=(v.x < -v.w?1u:0u)|(v.x>v.w?2u:0u)|(v.y < -v.w?4u:0u)|
            (v.y>v.w?8u:0u)|(v.z < -v.w?16u:0u)|(v.z>v.w?32u:0u);
        common &= flags;
        /* Float: the R5900 emulates double in software. The 1e-5 relative
         * margin stays far above float rounding (~6e-8). */
        float inner=v.w-fabsf(v.w)*1e-5f;
        if(v.w<=0 || fabsf(v.x)>=inner || fabsf(v.y)>=inner || fabsf(v.z)>=inner)
            contained=0;
    }
    return common?ATHENA_FRUSTUM3D_OUTSIDE:contained?ATHENA_FRUSTUM3D_INSIDE:ATHENA_FRUSTUM3D_INTERSECT;
}
int athena_camera3d_box_visible(AthenaCamera3D *c,const AthenaMatrix4 *model,const float min[3],const float max[3]) {
    int relation=athena_camera3d_box_relation(c,model,min,max);
    return relation<0?-1:relation!=ATHENA_FRUSTUM3D_OUTSIDE;
}
