#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <athena/debug3d.h>
#include <athena/float_bits.h>
typedef struct { float a[3],b[3]; float remaining; uint32_t color; uint8_t drawn,timed,bounded; } Line;
static Line *lines;
static uint32_t count,dropped;
static int enabled=1;
/* Reference builds can force 0 for differential tests. Both implementations
 * use the same staging/timing, queue and GS emission. */
#ifndef ATHENA_DEBUG3D_OPTIMIZED
#define ATHENA_DEBUG3D_OPTIMIZED 1
#endif
#define PROJECT_BATCH 256u
static AthenaVector4 points[PROJECT_BATCH*2];
static AthenaVector4 projected_points[PROJECT_BATCH*2];
static AthenaDebug3DSegment screen_segments[PROJECT_BATCH];
static AthenaDebug3DStats last_stats;
static int profiling,projecting;
static int vu_operand(float v);
#if defined(_EE)
static uint32_t debug_ticks(void) {
    uint32_t ticks; __asm__ __volatile__("mfc0 %0, $9" : "=r"(ticks)); return ticks;
}
#else
#include <time.h>
static uint32_t debug_ticks(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint32_t)((uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u);
}
#endif
void athena_debug3d_profile(int on) { profiling=on!=0; }
void athena_debug3d_stats(AthenaDebug3DStats *out) { if(out) *out=last_stats; }

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
    l->bounded=1;
    for(int k=0;k<3;k++) if(!vu_operand(a[k])||!vu_operand(b[k])) l->bounded=0;
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
    float normal[3][3];
    if(model) {
        /* Scale before cofactors: avoids overflow/underflow for uniformly huge
         * or tiny matrices. Normalization removes |det|, but not its sign. */
        float scale=0;
        for(int c=0;c<3;c++) for(int r=0;r<3;r++) {
            float value=fabsf(model->value[c*4+r]); if(value>scale) scale=value;
        }
        if(!(scale>0)) return -1;
        float a[3][3];
        for(int r=0;r<3;r++) for(int c=0;c<3;c++) a[r][c]=model->value[c*4+r]/scale;
        for(int r=0;r<3;r++) for(int c=0;c<3;c++) {
            int r1=(r+1)%3,r2=(r+2)%3,c1=(c+1)%3,c2=(c+2)%3;
            normal[r][c]=a[r1][c1]*a[r2][c2]-a[r1][c2]*a[r2][c1];
        }
        float det=a[0][0]*normal[0][0]+a[0][1]*normal[0][1]+a[0][2]*normal[0][2];
        if(!athena_float_isfinite(det)||fabsf(det)<=1e-7f) return -1;
        if(det<0) for(int r=0;r<3;r++) for(int c=0;c<3;c++) normal[r][c]=-normal[r][c];
    }
    int queued=0;
    for(uint32_t i=0;i<athena_mesh3d_stream_count(&view);i++) {
        const AthenaPosition3D *pp=&view.positions[i],*nn=&view.normals[i];
        float p[3]={pp->x,pp->y,pp->z},a[3],n[3]={nn->x,nn->y,nn->z};
        transform(model,p,a);
        if(model) {
            float w[3];
            for(int r=0;r<3;r++) w[r]=normal[r][0]*n[0]+normal[r][1]*n[1]+normal[r][2]*n[2];
            memcpy(n,w,sizeof(n));
        }
        float scale=fmaxf(fabsf(n[0]),fmaxf(fabsf(n[1]),fabsf(n[2])));
        if(!(scale>0)||!athena_float_isfinite(scale)) continue;
        for(int r=0;r<3;r++) n[r]/=scale;
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
static int clip_segment(float a[4],float b[4],float near,int bounded) {
    if(!bounded) for(int i=0;i<4;i++) if(!athena_float_isfinite(a[i])||!athena_float_isfinite(b[i])) return 0;
#if ATHENA_DEBUG3D_OPTIMIZED
    if(a[3]>=near && b[3]>=near && fabsf(a[0])<=a[3] && fabsf(a[1])<=a[3] &&
        fabsf(b[0])<=b[3] && fabsf(b[1])<=b[3]) return 2; /* endpoints unchanged */
    unsigned ca=(a[3]<near?1u:0u)|(a[0]<-a[3]?2u:0u)|(a[0]>a[3]?4u:0u)|
        (a[1]<-a[3]?8u:0u)|(a[1]>a[3]?16u:0u);
    unsigned cb=(b[3]<near?1u:0u)|(b[0]<-b[3]?2u:0u)|(b[0]>b[3]?4u:0u)|
        (b[1]<-b[3]?8u:0u)|(b[1]>b[3]?16u:0u);
    if(!(ca|cb)) return 2;
    if(ca&cb) return 0;
#endif
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
/* VU0 saturates overflow and flushes denormals, unlike IEEE C. Conservative
 * operand bounds keep its products/sums normal and finite; extreme batches
 * use C so overflow rejection remains identical. Zero is allowed. */
static int vu_operand(float v) {
    uint32_t bits; memcpy(&bits,&v,sizeof(bits)); bits&=0x7fffffffu;
    return bits==0||(bits>=0x33800000u&&bits<=0x4b800000u); /* 2^-24..2^24 */
}
static int transform_points(const AthenaMatrix4 *matrix,uint32_t n,int bounded,float width,float height) {
#if defined(__mips__) && ATHENA_DEBUG3D_OPTIMIZED
    if(bounded) {
    AthenaVector4 *at=points;
    AthenaVector4 *screen=projected_points;
    AthenaVector4 scale={width*.5f,-height*.5f,0,0},offset={width*.5f,height*.5f,0,0};
    /* Main-thread macro mode: vf4..vf12/ACC/Q. Wait for each division before
     * consuming Q; no outstanding Q operation on return. No microprogram,
     * VIF0 upload or DMA reference to this scratch. Near/screen clipping stays
     * in C; only unchanged inside endpoints can use these projected pixels. */
    __asm__ __volatile__(
        "lqc2 $vf4, 0x00(%3)\n"
        "lqc2 $vf5, 0x10(%3)\n"
        "lqc2 $vf6, 0x20(%3)\n"
        "lqc2 $vf7, 0x30(%3)\n"
        "lqc2 $vf10, 0x00(%4)\n"
        "lqc2 $vf11, 0x00(%5)\n"
        "1:\n"
        "lqc2 $vf8, 0x00(%0)\n"
        "vmulax.xyzw $ACC, $vf4, $vf8\n"
        "vmadday.xyzw $ACC, $vf5, $vf8\n"
        "vmaddaz.xyzw $ACC, $vf6, $vf8\n"
        "vmaddw.xyzw $vf9, $vf7, $vf8\n"
        "sqc2 $vf9, 0x00(%0)\n"
        "vdiv $Q, $vf0w, $vf9w\n"
        "vwaitq\n"
        "vmulq.xy $vf12, $vf9, $Q\n"
        "vmul.xy $vf12, $vf12, $vf10\n"
        "vadd.xy $vf12, $vf12, $vf11\n"
        "sqc2 $vf12, 0x00(%2)\n"
        "addiu %0, %0, 16\n"
        "addiu %2, %2, 16\n"
        "addiu %1, %1, -1\n"
        "bnez %1, 1b\n"
        "nop\n"
        : "+&r"(at), "+&r"(n), "+&r"(screen) : "r"(matrix), "r"(&scale), "r"(&offset) : "memory");
        last_stats.vu0=1;
        return 1;
    }
    last_stats.fallback_batches++;
#else
    (void)bounded;
    (void)width; (void)height;
#endif
    const float *m=matrix->value;
    for(uint32_t i=0;i<n;i++) {
        AthenaVector4 p=points[i],q;
        q.x=m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12];
        q.y=m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13];
        q.z=m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14];
        q.w=m[3]*p.x+m[7]*p.y+m[11]*p.z+m[15];
        points[i]=q;
    }
    return 0;
}
int athena_debug3d_project_batch(AthenaCamera3D *cam,float width,float height,AthenaDebug3DEmitBatch emit,void *opaque) {
    if(projecting) return -1;
    memset(&last_stats,0,sizeof(last_stats)); last_stats.profiling=profiling;
    uint32_t begin=profiling?debug_ticks():0;
    if(!cam||!emit||!athena_float_isfinite(width)||!athena_float_isfinite(height)||width<=0||height<=0||
        !athena_camera3d_update(cam)) return -1;
    projecting=1;
    last_stats.input=count;
    int matrix_bounded=1;
    for(int i=0;i<16;i++) if(!vu_operand(cam->view_projection.value[i])) matrix_bounded=0;
    int viewport_bounded=vu_operand(width)&&vu_operand(height);
    float near=cam->near_clip*.999f;
    for(uint32_t base=0;base<last_stats.input;base+=PROJECT_BATCH) {
        uint32_t n=last_stats.input-base;
        if(n>PROJECT_BATCH) n=PROJECT_BATCH;
        uint32_t start=profiling?debug_ticks():0;
        int bounded=matrix_bounded;
        for(uint32_t i=0;i<n;i++) {
            Line *l=&lines[base+i]; l->drawn=1;
            if(!l->bounded) bounded=0;
            points[i*2]=(AthenaVector4){l->a[0],l->a[1],l->a[2],1};
            points[i*2+1]=(AthenaVector4){l->b[0],l->b[1],l->b[2],1};
        }
        int projected=transform_points(&cam->view_projection,n*2,bounded,width,height);
        uint32_t end=profiling?debug_ticks():0;
        last_stats.transform_ticks+=end-start; start=end;
        uint32_t visible=0;
        for(uint32_t i=0;i<n;i++) {
            float a[4]={points[i*2].x,points[i*2].y,points[i*2].z,points[i*2].w};
            float b[4]={points[i*2+1].x,points[i*2+1].y,points[i*2+1].z,points[i*2+1].w};
            int clipped=clip_segment(a,b,near,bounded);
            if(!clipped) continue;
            AthenaDebug3DSegment s;
            if(projected&&viewport_bounded&&clipped==2) {
                s=(AthenaDebug3DSegment){projected_points[i*2].x,projected_points[i*2].y,
                    projected_points[i*2+1].x,projected_points[i*2+1].y,lines[base+i].color};
            } else {
#if ATHENA_DEBUG3D_OPTIMIZED
            float ia=1/a[3],ib=a[3]==b[3]?ia:1/b[3];
            s=(AthenaDebug3DSegment){(a[0]*ia+1)*.5f*width,(1-a[1]*ia)*.5f*height,
                (b[0]*ib+1)*.5f*width,(1-b[1]*ib)*.5f*height,lines[base+i].color};
#else
            s=(AthenaDebug3DSegment){(a[0]/a[3]+1)*.5f*width,(1-a[1]/a[3])*.5f*height,
                (b[0]/b[3]+1)*.5f*width,(1-b[1]/b[3])*.5f*height,lines[base+i].color};
#endif
            if(!athena_float_isfinite(s.x0)||!athena_float_isfinite(s.y0)||!athena_float_isfinite(s.x1)||!athena_float_isfinite(s.y1)) continue;
            }
            /* Inside bounded VU0 endpoints have w>0, |xy/w|<=1 and a bounded
             * viewport: their mapped pixels are finite by construction. */
            screen_segments[visible++]=s;
        }
        end=profiling?debug_ticks():0; last_stats.clip_ticks+=end-start; start=end;
        if(visible) { emit(opaque,screen_segments,visible); last_stats.batches++; }
        end=profiling?debug_ticks():0; last_stats.emit_ticks+=end-start;
        last_stats.emitted+=visible;
    }
    last_stats.rejected=last_stats.input-last_stats.emitted;
    last_stats.total_ticks=profiling?debug_ticks()-begin:0;
    projecting=0;
    return (int)last_stats.emitted;
}
typedef struct { AthenaDebug3DEmit emit; void *opaque; } EmitOne;
static void emit_one(void *opaque,const AthenaDebug3DSegment *s,uint32_t n) {
    EmitOne *one=opaque;
    for(uint32_t i=0;i<n;i++) one->emit(one->opaque,&s[i]);
}
int athena_debug3d_project(AthenaCamera3D *cam,float width,float height,AthenaDebug3DEmit emit,void *opaque) {
    EmitOne one={emit,opaque};
    return athena_debug3d_project_batch(cam,width,height,emit?emit_one:NULL,&one);
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
void athena_debug3d_module_shutdown(void) {
    free(lines); lines=NULL; count=0; dropped=0; enabled=1; profiling=projecting=0;
    memset(&last_stats,0,sizeof(last_stats));
}
