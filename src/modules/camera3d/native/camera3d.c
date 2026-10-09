#include <math.h>
#include <string.h>
#include <athena/camera3d.h>
#include <athena/float_bits.h>

/* Main-thread camera updates share a revision sequence, including init and
 * copies changed through setters: an address can never identify GPU state. */
static uint64_t camera_stamp;

/* Float: the R5900 emulates double in software (~1.4 us per operation) and
 * moving cameras rebuild the view every frame. Scaling by the largest
 * component keeps the sum of squares in [1,3]; lengths at or below 1e-12
 * are rejected as before. */
static int normalize3(float v[3]) {
    float m=fabsf(v[0]);
    if(fabsf(v[1])>m) m=fabsf(v[1]);
    if(fabsf(v[2])>m) m=fabsf(v[2]);
    if(!athena_float_isfinite(m) || m<=1e-12f) return 0;
    float x=v[0]/m, y=v[1]/m, z=v[2]/m, n=sqrtf(x*x+y*y+z*z);
    if(m*n<=1e-12f) return 0;
    v[0]=x/n; v[1]=y/n; v[2]=z/n; return 1;
}
static int finite_point(const float p[3]) {
    return athena_float_isfinite(p[0])&&athena_float_isfinite(p[1])&&athena_float_isfinite(p[2]);
}
static int view_matrix(AthenaMatrix4 *out, const AthenaVector4 *p,
    const AthenaVector4 *target, const AthenaVector4 *up) {
    float z[3]={p->x-target->x, p->y-target->y, p->z-target->z};
    if (!normalize3(z)) return 0;
    float x[3]={up->y*z[2]-up->z*z[1], up->z*z[0]-up->x*z[2], up->x*z[1]-up->y*z[0]};
    if (!normalize3(x)) return 0;
    float y[3]={z[1]*x[2]-z[2]*x[1], z[2]*x[0]-z[0]*x[2], z[0]*x[1]-z[1]*x[0]};
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
    view_matrix(&c->view,&c->position,&c->target,&c->up);
    athena_camera3d_set_projection(c,60,4.0f/3.0f,0.1f,300);
    athena_camera3d_update(c);
}

int athena_camera3d_set_projection(AthenaCamera3D *c, float fov, float aspect, float near, float far) {
    if (!athena_float_isfinite(fov)||fov<=0||fov>=179||!athena_float_isfinite(aspect)||aspect<=0||
        !athena_float_isfinite(near)||near<=0||!athena_float_isfinite(far)||far<=near) return 0;
    float f=1/tanf(fov*(3.14159265358979323846f/360)), range=far-near;
    AthenaMatrix4 p={{0}};
    p.value[0]=f/aspect; p.value[5]=f;
    p.value[10]=(far+near)/range; p.value[11]=-1;
    p.value[14]=2*far*(near/range);
    for(int i=0;i<16;i++) if(!athena_float_isfinite(p.value[i])) return 0;
    c->projection=p; c->fov_y_degrees=fov; c->aspect=aspect;
    c->near_clip=near; c->far_clip=far; c->dirty=true; return 1;
}

/* The view that validates the change is kept: update() only multiplies. */
static int set_vector(AthenaCamera3D *c, AthenaVector4 *dst, float x,float y,float z) {
    AthenaVector4 v={x,y,z,dst->w}; AthenaMatrix4 view;
    if(!athena_float_isfinite(x)||!athena_float_isfinite(y)||!athena_float_isfinite(z)) return 0;
    if(!view_matrix(&view,dst==&c->position?&v:&c->position,
        dst==&c->target?&v:&c->target,dst==&c->up?&v:&c->up)) return 0;
    *dst=v; c->view=view; c->dirty=true; return 1;
}
int athena_camera3d_set_position(AthenaCamera3D *c,float x,float y,float z) { return set_vector(c,&c->position,x,y,z); }
int athena_camera3d_look_at(AthenaCamera3D *c,float x,float y,float z) { return set_vector(c,&c->target,x,y,z); }
int athena_camera3d_set_up(AthenaCamera3D *c,float x,float y,float z) { return set_vector(c,&c->up,x,y,z); }
int athena_camera3d_set_view(AthenaCamera3D *c,const float eye[3],const float target[3]) {
    AthenaVector4 p={eye[0],eye[1],eye[2],1},t={target[0],target[1],target[2],1}; AthenaMatrix4 view;
    for(int i=0;i<3;i++) if(!athena_float_isfinite(eye[i])||!athena_float_isfinite(target[i])) return 0;
    if(!view_matrix(&view,&p,&t,&c->up)) return 0;
    c->position=p; c->target=t; c->view=view; c->dirty=true; return 1;
}
int athena_camera3d_update(AthenaCamera3D *c) {
    if(!c->dirty) return 1;
    AthenaMatrix4 vp;
    ath_matrix4_multiply(&vp,&c->projection,&c->view);
    for(int i=0;i<16;i++) if(!athena_float_isfinite(vp.value[i])) return 0;
    c->view_projection=vp; c->stamp=++camera_stamp; c->dirty=false; return 1;
}
/* The eight corners of the box in clip space: the min corner transformed,
 * then the other seven by adding the matrix columns scaled by the box size
 * (corner i has bit 0 = max x, bit 1 = max y, bit 2 = max z). On the EE the
 * whole set is one VU0 macro-mode sequence; the host does the same sums. */
static void box_corners(AthenaVector4 corner[8], const AthenaMatrix4 *m,
    const float min[3], const float max[3]) {
    AthenaVector4 lo={min[0],min[1],min[2],1};
    AthenaVector4 size={max[0]-min[0],max[1]-min[1],max[2]-min[2],0};
#if defined(__mips__)
    __asm__ __volatile__(
        "lqc2 $vf4, 0x00(%1)\n"
        "lqc2 $vf5, 0x10(%1)\n"
        "lqc2 $vf6, 0x20(%1)\n"
        "lqc2 $vf7, 0x30(%1)\n"
        "lqc2 $vf8, 0x00(%2)\n"
        "lqc2 $vf9, 0x00(%3)\n"
        "vmulax.xyzw $ACC, $vf4, $vf8\n"
        "vmadday.xyzw $ACC, $vf5, $vf8\n"
        "vmaddaz.xyzw $ACC, $vf6, $vf8\n"
        "vmaddw.xyzw $vf10, $vf7, $vf8\n"
        "vmulx.xyzw $vf11, $vf4, $vf9\n"
        "vmuly.xyzw $vf12, $vf5, $vf9\n"
        "vmulz.xyzw $vf13, $vf6, $vf9\n"
        "vadd.xyzw $vf14, $vf10, $vf11\n"
        "vadd.xyzw $vf15, $vf10, $vf12\n"
        "vadd.xyzw $vf16, $vf14, $vf12\n"
        "vadd.xyzw $vf17, $vf10, $vf13\n"
        "vadd.xyzw $vf18, $vf14, $vf13\n"
        "vadd.xyzw $vf19, $vf15, $vf13\n"
        "vadd.xyzw $vf20, $vf16, $vf13\n"
        "sqc2 $vf10, 0x00(%0)\n"
        "sqc2 $vf14, 0x10(%0)\n"
        "sqc2 $vf15, 0x20(%0)\n"
        "sqc2 $vf16, 0x30(%0)\n"
        "sqc2 $vf17, 0x40(%0)\n"
        "sqc2 $vf18, 0x50(%0)\n"
        "sqc2 $vf19, 0x60(%0)\n"
        "sqc2 $vf20, 0x70(%0)\n"
        : : "r"(corner), "r"(m), "r"(&lo), "r"(&size) : "memory");
#else
    const float *c=m->value; float base[4], step[3][4];
    for(int r=0;r<4;r++) {
        base[r]=c[r]*lo.x+c[4+r]*lo.y+c[8+r]*lo.z+c[12+r];
        for(int a=0;a<3;a++) step[a][r]=c[a*4+r]*(&size.x)[a];
    }
    for(int i=0;i<8;i++) {
        float v[4];
        for(int r=0;r<4;r++) {
            v[r]=base[r];
            if(i&1) v[r]+=step[0][r];
            if(i&2) v[r]+=step[1][r];
            if(i&4) v[r]+=step[2][r];
        }
        corner[i]=(AthenaVector4){v[0],v[1],v[2],v[3]};
    }
#endif
}
/* athena_camera3d_update() only accepts a finite view_projection, so only the
 * model matrix and the corners are checked here. The relation is against
 * the frustum; with guard_inside, also whether the box is inside the frustum
 * widened in x and y by guard, from the same corners. */
static int relation(AthenaCamera3D *c,const AthenaMatrix4 *model,const float min[3],const float max[3],
    float guard,int *guard_inside) {
    AthenaMatrix4 m; AthenaVector4 corner[8];
    if(!c||!min||!max) return -1;
    for(unsigned i=0;i<3;i++) if(!athena_float_isfinite(min[i])||!athena_float_isfinite(max[i])||min[i]>max[i]) return -1;
    if(!athena_camera3d_update(c)) return -1;
    if(model) {
        for(unsigned i=0;i<16;i++) if(!athena_float_isfinite(model->value[i])) return -1;
        ath_matrix4_multiply(&m,&c->view_projection,model);
    }
    box_corners(corner,model?&m:&c->view_projection,min,max);
    unsigned common=63; int contained=1;
    for(int i=0;i<8;i++) {
        const AthenaVector4 v=corner[i];
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
    if(guard_inside) {
        /* Only an intersecting box needs the wider test: same corners. */
        int inside=!common;
        if(inside&&!contained) for(int i=0;i<8&&inside;i++) {
            const AthenaVector4 v=corner[i];
            float inner=v.w-fabsf(v.w)*1e-5f,guarded=inner*guard;
            if(v.w<=0||fabsf(v.z)>=inner||fabsf(v.x)>=guarded||fabsf(v.y)>=guarded) inside=0;
        }
        *guard_inside=inside;
    }
    return common?ATHENA_FRUSTUM3D_OUTSIDE:contained?ATHENA_FRUSTUM3D_INSIDE:ATHENA_FRUSTUM3D_INTERSECT;
}
int athena_camera3d_box_relation(AthenaCamera3D *c,const AthenaMatrix4 *model,const float min[3],const float max[3]) {
    return relation(c,model,min,max,1,NULL);
}
int athena_camera3d_box_relation_ex(AthenaCamera3D *c,const AthenaMatrix4 *model,const float min[3],const float max[3],
    float guard,int *guard_inside) {
    if(!athena_float_isfinite(guard)||guard<1||!guard_inside) return -1;
    return relation(c,model,min,max,guard,guard_inside);
}
/* Clip space: in front of the near plane when z <= w (the projection maps
 * the near plane to z = w and the far plane to z = -w). */
static int near_point_ok(float x,float y,float z,float w,float guard) {
    if(!(w>0)) return 0;
    float inner=w-fabsf(w)*1e-5f,guarded=inner*guard;
    return fabsf(x)<guarded&&fabsf(y)<guarded&&z>-inner;
}
int athena_camera3d_box_near_guard(AthenaCamera3D *c,const AthenaMatrix4 *model,const float min[3],const float max[3],float guard) {
    AthenaMatrix4 m; AthenaVector4 corner[8];
    if(!c||!min||!max||!athena_float_isfinite(guard)||guard<1) return -1;
    for(unsigned i=0;i<3;i++) if(!athena_float_isfinite(min[i])||!athena_float_isfinite(max[i])||min[i]>max[i]) return -1;
    if(!athena_camera3d_update(c)) return -1;
    if(model) {
        for(unsigned i=0;i<16;i++) if(!athena_float_isfinite(model->value[i])) return -1;
        ath_matrix4_multiply(&m,&c->view_projection,model);
    }
    box_corners(corner,model?&m:&c->view_projection,min,max);
    int front=0;
    for(int i=0;i<8;i++) {
        const AthenaVector4 *v=&corner[i];
        if(!athena_float_isfinite(v->x)||!athena_float_isfinite(v->y)||!athena_float_isfinite(v->z)||!athena_float_isfinite(v->w)) return -1;
        if(v->w-v->z<0) continue;
        if(!near_point_ok(v->x,v->y,v->z,v->w,guard)) return 0;
        front++;
    }
    if(!front) return 0;
    /* Edges (corners differing in one bit) crossing the near plane. */
    for(int i=0;i<8;i++) for(int bit=1;bit<8;bit<<=1) {
        if(i&bit) continue;
        const AthenaVector4 *a=&corner[i],*b=&corner[i|bit];
        float da=a->w-a->z,db=b->w-b->z;
        if((da<0)==(db<0)) continue;
        float t=da/(da-db);
        float x=a->x+(b->x-a->x)*t,y=a->y+(b->y-a->y)*t,w=a->w+(b->w-a->w)*t;
        if(!near_point_ok(x,y,w,w,guard)) return 0;
    }
    return 1;
}
int athena_camera3d_box_relation_guard(AthenaCamera3D *c,const AthenaMatrix4 *model,const float min[3],const float max[3],float guard) {
    int inside; int r=athena_camera3d_box_relation_ex(c,model,min,max,guard,&inside);
    return r<0?r:r==ATHENA_FRUSTUM3D_OUTSIDE?r:inside?ATHENA_FRUSTUM3D_INSIDE:ATHENA_FRUSTUM3D_INTERSECT;
}
int athena_camera3d_box_visible(AthenaCamera3D *c,const AthenaMatrix4 *model,const float min[3],const float max[3]) {
    int relation=athena_camera3d_box_relation(c,model,min,max);
    return relation<0?-1:relation!=ATHENA_FRUSTUM3D_OUTSIDE;
}

static int viewport_valid(float width,float height) {
    return athena_float_isfinite(width)&&athena_float_isfinite(height)&&width>0&&height>0;
}
int athena_camera3d_world_to_screen(AthenaCamera3D *c,const float p[3],float width,float height,float out[3]) {
    if(!c||!p||!out||!viewport_valid(width,height)||!finite_point(p)||!athena_camera3d_update(c)) return -1;
    const float *m=c->view_projection.value;
    float x=m[0]*p[0]+m[4]*p[1]+m[8]*p[2]+m[12];
    float y=m[1]*p[0]+m[5]*p[1]+m[9]*p[2]+m[13];
    float w=m[3]*p[0]+m[7]*p[1]+m[11]*p[2]+m[15];
    /* The projection's w row is (0,0,-1,0): w is -z in view space, the
     * distance in front of the camera along its axis. */
    if(!athena_float_isfinite(w)||!(w>0)) return 0;
    float sx=(x/w+1)*.5f*width,sy=(1-y/w)*.5f*height;
    if(!athena_float_isfinite(sx)||!athena_float_isfinite(sy)) return -1;
    out[0]=sx; out[1]=sy; out[2]=w; return 1;
}
int athena_camera3d_screen_to_ray(AthenaCamera3D *c,float sx,float sy,float width,float height,
    float origin[3],float direction[3]) {
    if(!c||!origin||!direction||!viewport_valid(width,height)||
        !athena_float_isfinite(sx)||!athena_float_isfinite(sy)) return -1;
    /* Without inverting a matrix: the view direction from the projection
     * scale factors, then the camera basis (the rows of the view rotation). */
    const float *pr=c->projection.value,*v=c->view.value;
    float nx=2*sx/width-1,ny=1-2*sy/height;
    float vx=nx/pr[0],vy=ny/pr[5];
    float d[3]={vx*v[0]+vy*v[1]-v[2], vx*v[4]+vy*v[5]-v[6], vx*v[8]+vy*v[9]-v[10]};
    if(!normalize3(d)) return -1;
    origin[0]=c->position.x; origin[1]=c->position.y; origin[2]=c->position.z;
    direction[0]=d[0]; direction[1]=d[1]; direction[2]=d[2]; return 1;
}
