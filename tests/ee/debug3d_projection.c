/* Shared host/EE differential test. Oracle clips endpoints successively
 * against each plane, independently of Debug3D's interval clipper. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <athena/debug3d.h>
#include <athena/float_bits.h>
#ifdef _EE
#include <kernel.h>
#else
/* Projection does not use meshes; real mesh normals are covered in JS. */
void athena_mesh3d_view(const AthenaMesh3D *mesh,AthenaMesh3DView *out) {
    (void)mesh;
    static const AthenaPosition3D positions[]={{0,0,-5}},normals[]={{1,1,0}};
    memset(out,0,sizeof(*out)); out->positions=positions;out->normals=normals;out->vertex_count=1;
}
static int measuring;
static unsigned allocations;
void *__real_malloc(size_t);
void *__wrap_malloc(size_t n) { if(measuring) allocations++; return __real_malloc(n); }
#endif
static AthenaDebug3DSegment expected[ATHENA_DEBUG3D_MAX_LINES];
static unsigned expected_count,observed,cases;
static float max_error;
static float distance(const float p[4],int plane,float near) {
    switch(plane) {
        case 0:return p[3]-near;
        case 1:return p[3]+p[0];
        case 2:return p[3]-p[0];
        case 3:return p[3]+p[1];
        default:return p[3]-p[1];
    }
}
static void reference(AthenaCamera3D *cam,const float *pa,const float *pb,uint32_t color,float w,float h) {
    const float *m=cam->view_projection.value; float p[2][4];
    for(int v=0;v<2;v++) {
        const float *s=v?pb:pa;
        for(int r=0;r<4;r++) {
            p[v][r]=m[r]*s[0]+m[r+4]*s[1]+m[r+8]*s[2]+m[r+12];
            if(!athena_float_isfinite(p[v][r])) return;
        }
    }
    for(int plane=0;plane<5;plane++) {
        float da=distance(p[0],plane,cam->near_clip*.999f),db=distance(p[1],plane,cam->near_clip*.999f);
        if(da<0&&db<0) return;
        if((da<0)!=(db<0)) {
            float t=da/(da-db),q[4];
            for(int r=0;r<4;r++) q[r]=p[0][r]+t*(p[1][r]-p[0][r]);
            memcpy(p[da<0?0:1],q,sizeof(q));
        }
    }
    AthenaDebug3DSegment s={(p[0][0]/p[0][3]+1)*w*.5f,(1-p[0][1]/p[0][3])*h*.5f,
        (p[1][0]/p[1][3]+1)*w*.5f,(1-p[1][1]/p[1][3])*h*.5f,color};
    if(!athena_float_isfinite(s.x0)||!athena_float_isfinite(s.y0)||!athena_float_isfinite(s.x1)||!athena_float_isfinite(s.y1)) return;
    expected[expected_count++]=s;
}
static void check(void *opaque,const AthenaDebug3DSegment *s) {
    (void)opaque; assert(observed<expected_count);
    AthenaDebug3DSegment *e=&expected[observed++];
    if(s->color!=e->color) printf("Debug3D color mismatch at %u/%u: got %u expected %u\n",observed,expected_count,s->color,e->color);
    assert(s->color==e->color);
    const float got[4]={s->x0,s->y0,s->x1,s->y1},want[4]={e->x0,e->y0,e->x1,e->y1};
    for(int i=0;i<4;i++) {
        float error=fabsf(got[i]-want[i]); if(error>max_error) max_error=error;
        if(!(athena_float_isfinite(got[i])&&error<.03f)) printf("Debug3D mismatch %u coord %d: %g vs %g\n",observed,i,(double)got[i],(double)want[i]);
        assert(athena_float_isfinite(got[i])&&error<.03f);
    }
}
static void check_batch(void *opaque,const AthenaDebug3DSegment *s,uint32_t n) {
    assert(n>0&&n<=256);
    for(uint32_t i=0;i<n;i++) check(opaque,s+i);
}
static uint32_t seed=1234567;
static float random_coord(void) { seed=seed*1664525u+1013904223u; return (float)(seed>>8)/16777216.0f; }
static void scene(AthenaCamera3D *cam,unsigned n,float width,float height) {
    athena_debug3d_clear(); expected_count=observed=0;
    assert(athena_camera3d_update(cam));
    for(unsigned i=0;i<n;i++) {
        float a[3],b[3];
        for(int k=0;k<3;k++) { a[k]=(random_coord()-.5f)*80; b[k]=(random_coord()-.5f)*80; }
        uint32_t color=0x80000000u+i*531;
        assert(athena_debug3d_line(a,b,color,0)==0); reference(cam,a,b,color,width,height);
    }
    athena_debug3d_profile(1);
#ifndef _EE
    measuring=1;
#endif
    int result=athena_debug3d_project_batch(cam,width,height,check_batch,NULL);
#ifndef _EE
    measuring=0; assert(allocations==0);
#endif
    assert(result==(int)expected_count&&observed==expected_count);
    AthenaDebug3DStats stats; athena_debug3d_stats(&stats);
    assert(stats.input==n&&stats.emitted==expected_count&&stats.rejected==n-expected_count&&stats.profiling);
    assert(stats.total_ticks>=stats.transform_ticks+stats.clip_ticks+stats.emit_ticks);
    observed=0; athena_debug3d_profile(0);
    assert(athena_debug3d_project(cam,width,height,check,NULL)==result&&observed==expected_count);
    athena_debug3d_stats(&stats); assert(!stats.profiling&&stats.total_ticks==0&&stats.transform_ticks==0);
    athena_debug3d_age(0); assert(athena_debug3d_count()==0); cases++;
}
static void run(void) {
    AthenaCamera3D cam; athena_camera3d_init(&cam);
    assert(athena_camera3d_set_view(&cam,(float[]){0,0,0},(float[]){0,0,-1}));
    assert(athena_camera3d_set_projection(&cam,90,1,1,10));
    /* Far remains visible; near crossing survives; behind and shared screen
     * outcodes are rejected, all drawn lines still expire. */
    const float edge[][6]={{0,0,-20,1,0,-20},{0,0,-2,0,0,2},{-100,0,-2,-90,0,-2},
        {0,0,2,1,0,3},{-3,0,-2,3,0,-2},{0,-3,-2,0,3,-2},{0,0,-.999f,0,0,-2}};
    expected_count=observed=0; athena_camera3d_update(&cam);
    for(unsigned i=0;i<sizeof(edge)/sizeof(edge[0]);i++) {
        athena_debug3d_line(edge[i],edge[i]+3,i+1,0); reference(&cam,edge[i],edge[i]+3,i+1,320,240);
    }
    assert(athena_debug3d_project(&cam,320,240,check,NULL)==(int)expected_count&&observed==expected_count&&expected_count==5);
    athena_debug3d_age(0); assert(athena_debug3d_count()==0);
    for(unsigned n=1;n<=8192;n=n==1?255:n==255?256:n==256?257:n==257?2000:8192) {
        scene(&cam,n,640,448); if(n==8192) break;
    }
    athena_camera3d_set_view(&cam,(float[]){5,3,9},(float[]){-1,-2,-5});
    athena_camera3d_set_projection(&cam,43,1.7f,.13f,150);
    scene(&cam,4000,720,480);
    /* Extreme operand batches must fall back to IEEE C, including overflow. */
    athena_camera3d_set_view(&cam,(float[]){0,0,0},(float[]){0,0,-1});
    athena_camera3d_set_projection(&cam,43,1,.5f,150); athena_camera3d_update(&cam);
    const float huge[3]={FLT_MAX,FLT_MAX,-2},tiny[3]={1e-30f,0,-5},other[3]={0,0,-6};
    athena_debug3d_clear(); expected_count=observed=0;
    /* Huge X/Y at depth 2 is entirely outside the screen. Do not feed
     * overflowing arithmetic into the oracle: EE FPU overflow is not IEEE. */
    athena_debug3d_line(huge,huge,99,0);
    athena_debug3d_line(tiny,other,100,0); reference(&cam,tiny,other,100,640,448);
    assert(athena_debug3d_project(&cam,640,448,check,NULL)==1&&observed==expected_count);
#ifdef _EE
    AthenaDebug3DStats stats; athena_debug3d_stats(&stats); assert(stats.fallback_batches==1&&!stats.vu0);
#endif
    athena_debug3d_clear(); assert(athena_debug3d_project(NULL,1,1,check,NULL)==-1);
    assert(athena_debug3d_project(&cam,-1,1,check,NULL)==-1);
    assert(athena_debug3d_project(&cam,1,1,NULL,NULL)==-1);
    assert(athena_debug3d_project(&cam,1,1,check,NULL)==0);
#ifndef _EE
    int token=0; AthenaMesh3D *mesh=(AthenaMesh3D *)&token;
    const float target[][3]={{.5f,1,0},{-.5f,1,0},{1,-1,0}};
    for(int variant=0;variant<3;variant++) {
        AthenaMatrix4 model;ath_matrix4_identity(&model);
        if(variant<2) model.value[0]=variant?-2:2;
        else model.value[4]=2;
        float len=sqrtf(target[variant][0]*target[variant][0]+target[variant][1]*target[variant][1]);
        float a[3]={0,0,-5},b[3]={target[variant][0]/len,target[variant][1]/len,-5};
        expected_count=observed=0;reference(&cam,a,b,123,640,448);
        assert(athena_debug3d_normals(mesh,&model,1,123,0)==1);
        assert(athena_debug3d_project(&cam,640,448,check,NULL)==1&&observed==1);
        athena_debug3d_clear();
    }
    AthenaMatrix4 singular;ath_matrix4_identity(&singular);singular.value[0]=0;
    assert(athena_debug3d_normals(mesh,&singular,1,123,0)==-1&&athena_debug3d_count()==0);
#endif
    athena_debug3d_module_shutdown();
    printf("Debug3D differential projection passed: %u scenes; max error %.6f px; near/screen/far, colors, expiry, bounded batches and extreme fallback\n",cases,(double)max_error);
}
#ifdef _EE
int athena_main(int argc,char **argv) { (void)argc;(void)argv;run();puts("Debug3D native EE test passed");SleepThread();return 0; }
#else
int main(void) { run();return 0; }
#endif
