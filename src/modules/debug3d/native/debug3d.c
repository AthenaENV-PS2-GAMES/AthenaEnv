#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <athena/debug3d.h>
#include <athena/float_bits.h>
typedef struct { float a[3],b[3]; float remaining; uint32_t color; uint8_t drawn,timed; } Line;
static Line *lines;
static uint32_t count,dropped;
static int enabled=1;

static int finite3(const float v[3]) {
    return athena_float_isfinite(v[0])&&athena_float_isfinite(v[1])&&athena_float_isfinite(v[2]);
}
static int valid_seconds(float seconds) { return athena_float_isfinite(seconds)&&seconds>=0; }
/* Queues without validation (callers checked); 1 when dropped. */
static int push(const float a[3],const float b[3],uint32_t color,float seconds) {
    if(!lines) { lines=malloc(ATHENA_DEBUG3D_MAX_LINES*sizeof(*lines)); if(!lines) { dropped++; return 1; } }
    if(count>=ATHENA_DEBUG3D_MAX_LINES) { dropped++; return 1; }
    Line *l=&lines[count++];
    memcpy(l->a,a,sizeof(l->a)); memcpy(l->b,b,sizeof(l->b));
    l->color=color; l->remaining=seconds; l->timed=seconds>0; l->drawn=0;
    return 0;
}
int athena_debug3d_line(const float a[3],const float b[3],uint32_t color,float seconds) {
    if(!a||!b||!finite3(a)||!finite3(b)||!valid_seconds(seconds)) return -1;
    if(!enabled) return 0;
    return push(a,b,color,seconds);
}
static void transform(const AthenaMatrix4 *m,const float p[3],float out[3]) {
    if(!m) { memcpy(out,p,3*sizeof(float)); return; }
    const float *v=m->value;
    for(int r=0;r<3;r++) out[r]=v[r]*p[0]+v[4+r]*p[1]+v[8+r]*p[2]+v[12+r];
}
static int finite_matrix(const AthenaMatrix4 *m) {
    if(!m) return 1;
    for(int i=0;i<16;i++) if(!athena_float_isfinite(m->value[i])) return 0;
    return 1;
}
int athena_debug3d_box(const float lo[3],const float hi[3],const AthenaMatrix4 *model,uint32_t color,float seconds) {
    if(!lo||!hi||!finite3(lo)||!finite3(hi)||!finite_matrix(model)||!valid_seconds(seconds)) return -1;
    for(int i=0;i<3;i++) if(lo[i]>hi[i]) return -1;
    if(!enabled) return 0;
    float c[8][3];
    for(int i=0;i<8;i++) {
        float p[3]={i&1?hi[0]:lo[0],i&2?hi[1]:lo[1],i&4?hi[2]:lo[2]};
        transform(model,p,c[i]);
    }
    /* Corners differing in one bit share an edge. */
    static const uint8_t edges[12][2]={{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
    int queued=0;
    for(int e=0;e<12;e++) queued+=!push(c[edges[e][0]],c[edges[e][1]],color,seconds);
    return queued;
}
int athena_debug3d_sphere(const float center[3],float radius,uint32_t segments,uint32_t color,float seconds) {
    if(!center||!finite3(center)||!athena_float_isfinite(radius)||radius<0||segments<3||
        segments>ATHENA_DEBUG3D_MAX_SEGMENTS||!valid_seconds(seconds)) return -1;
    if(!enabled) return 0;
    int queued=0;
    for(int plane=0;plane<3;plane++) {
        /* plane 0: XY, 1: XZ, 2: YZ. */
        int u=plane==2?1:0,v=plane==0?1:2;
        float prev[3];
        for(uint32_t i=0;i<=segments;i++) {
            float angle=6.28318530718f*(float)(i%segments)/(float)segments;
            float p[3]={center[0],center[1],center[2]};
            p[u]+=radius*cosf(angle); p[v]+=radius*sinf(angle);
            if(i) queued+=!push(prev,p,color,seconds);
            memcpy(prev,p,sizeof(prev));
        }
    }
    return queued;
}
int athena_debug3d_axes(const AthenaMatrix4 *m,float size,float seconds) {
    if(!m||!finite_matrix(m)||!athena_float_isfinite(size)||!valid_seconds(seconds)) return -1;
    if(!enabled) return 0;
    static const uint32_t colors[3]={0x800000FFu,0x8000FF00u,0x80FF0000u}; /* ABGR: red, green, blue */
    const float origin[3]={0,0,0};
    float o[3]; transform(m,origin,o);
    int queued=0;
    for(int axis=0;axis<3;axis++) {
        float p[3]={0,0,0},w[3]; p[axis]=size; transform(m,p,w);
        queued+=!push(o,w,colors[axis],seconds);
    }
    return queued;
}
int athena_debug3d_grid(const float center[3],float half,float step,uint32_t color,float seconds) {
    if(!center||!finite3(center)||!athena_float_isfinite(half)||!athena_float_isfinite(step)||
        half<0||step<=0||half/step>128||!valid_seconds(seconds)) return -1;
    if(!enabled) return 0;
    int n=(int)floorf(half/step+1e-4f),queued=0;
    for(int i=-n;i<=n;i++) {
        float t=(float)i*step;
        float a[3]={center[0]+t,center[1],center[2]-half},b[3]={center[0]+t,center[1],center[2]+half};
        float c[3]={center[0]-half,center[1],center[2]+t},d[3]={center[0]+half,center[1],center[2]+t};
        queued+=!push(a,b,color,seconds); queued+=!push(c,d,color,seconds);
    }
    return queued;
}
int athena_debug3d_frustum(AthenaCamera3D *cam,uint32_t color,float seconds) {
    if(!cam||!valid_seconds(seconds)) return -1;
    if(!enabled) return 0;
    const float *v=cam->view.value,*pr=cam->projection.value;
    const float x[3]={v[0],v[4],v[8]},y[3]={v[1],v[5],v[9]},z[3]={v[2],v[6],v[10]};
    const float p[3]={cam->position.x,cam->position.y,cam->position.z};
    float c[8][3];
    for(int i=0;i<8;i++) {
        float d=i&4?cam->far_clip:cam->near_clip;
        float sx=(i&1?1:-1)*d/pr[0],sy=(i&2?1:-1)*d/pr[5];
        for(int k=0;k<3;k++) c[i][k]=p[k]-z[k]*d+x[k]*sx+y[k]*sy;
        if(!finite3(c[i])) return -1;
    }
    static const uint8_t edges[12][2]={{0,1},{2,3},{0,2},{1,3},{4,5},{6,7},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
    int queued=0;
    for(int e=0;e<12;e++) queued+=!push(c[edges[e][0]],c[edges[e][1]],color,seconds);
    return queued;
}
int athena_debug3d_normals(const AthenaMesh3D *mesh,const AthenaMatrix4 *model,float length,uint32_t color,float seconds) {
    if(!mesh||!finite_matrix(model)||!athena_float_isfinite(length)||!valid_seconds(seconds)) return -1;
    if(!enabled) return 0;
    AthenaMesh3DView view; athena_mesh3d_view(mesh,&view);
    if(!view.normals) return 0;
    int queued=0;
    for(uint32_t i=0;i<athena_mesh3d_stream_count(&view);i++) {
        const AthenaPosition3D *pp=&view.positions[i],*nn=&view.normals[i];
        float p[3]={pp->x,pp->y,pp->z},a[3],n[3]={nn->x,nn->y,nn->z};
        transform(model,p,a);
        if(model) {
            const float *m=model->value; float w[3];
            for(int r=0;r<3;r++) w[r]=m[r]*n[0]+m[4+r]*n[1]+m[8+r]*n[2];
            memcpy(n,w,sizeof(n));
        }
        float len=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        if(!(len>0)||!athena_float_isfinite(len)) continue;
        float b[3]={a[0]+n[0]/len*length,a[1]+n[1]/len*length,a[2]+n[2]/len*length};
        if(!finite3(a)||!finite3(b)) continue;
        queued+=!push(a,b,color,seconds);
    }
    return queued;
}
/* Liang-Barsky in clip space against the near plane (w >= near) and the
 * four screen planes (|x| <= w, |y| <= w). */
static int clip_segment(float a[4],float b[4],float near) {
    float t0=0,t1=1,d[4];
    for(int i=0;i<4;i++) d[i]=b[i]-a[i];
    const float p[5][2]={{a[3]-near,d[3]},{a[3]+a[0],d[3]+d[0]},{a[3]-a[0],d[3]-d[0]},{a[3]+a[1],d[3]+d[1]},{a[3]-a[1],d[3]-d[1]}};
    for(int k=0;k<5;k++) {
        float f=p[k][0],df=p[k][1];
        if(df==0) { if(f<0) return 0; continue; }
        float t=-f/df;
        if(df>0) { if(t>t0) t0=t; } else { if(t<t1) t1=t; }
        if(t0>t1) return 0;
    }
    float na[4],nb[4];
    for(int i=0;i<4;i++) { na[i]=a[i]+d[i]*t0; nb[i]=a[i]+d[i]*t1; }
    memcpy(a,na,sizeof(na)); memcpy(b,nb,sizeof(nb));
    return 1;
}
int athena_debug3d_project(AthenaCamera3D *cam,float width,float height,AthenaDebug3DEmit emit,void *opaque) {
    if(!cam||!emit||!athena_float_isfinite(width)||!athena_float_isfinite(height)||width<=0||height<=0||
        !athena_camera3d_update(cam)) return -1;
    const float *m=cam->view_projection.value;
    float near=cam->near_clip*.999f;
    int emitted=0;
    for(uint32_t i=0;i<count;i++) {
        Line *l=&lines[i]; l->drawn=1;
        float a[4],b[4];
        for(int r=0;r<4;r++) {
            a[r]=m[r]*l->a[0]+m[4+r]*l->a[1]+m[8+r]*l->a[2]+m[12+r];
            b[r]=m[r]*l->b[0]+m[4+r]*l->b[1]+m[8+r]*l->b[2]+m[12+r];
        }
        if(!clip_segment(a,b,near)) continue;
        AthenaDebug3DSegment s={(a[0]/a[3]+1)*.5f*width,(1-a[1]/a[3])*.5f*height,
            (b[0]/b[3]+1)*.5f*width,(1-b[1]/b[3])*.5f*height,l->color};
        if(!athena_float_isfinite(s.x0)||!athena_float_isfinite(s.y0)||!athena_float_isfinite(s.x1)||!athena_float_isfinite(s.y1)) continue;
        emit(opaque,&s); emitted++;
    }
    return emitted;
}
void athena_debug3d_age(float dt) {
    if(!athena_float_isfinite(dt)||dt<0) dt=0;
    uint32_t kept=0;
    for(uint32_t i=0;i<count;i++) {
        Line *l=&lines[i];
        if(l->timed&&l->drawn) l->remaining-=dt;
        if(l->drawn&&(!l->timed||l->remaining<=0)) continue;
        if(kept!=i) lines[kept]=*l;
        kept++;
    }
    count=kept;
}
void athena_debug3d_clear(void) { count=0; dropped=0; }
uint32_t athena_debug3d_count(void) { return count; }
uint32_t athena_debug3d_dropped(void) { return dropped; }
void athena_debug3d_set_enabled(int on) { enabled=on!=0; if(!enabled) count=0; }
int athena_debug3d_enabled(void) { return enabled; }
void athena_debug3d_module_shutdown(void) { free(lines); lines=NULL; count=0; dropped=0; enabled=1; }
