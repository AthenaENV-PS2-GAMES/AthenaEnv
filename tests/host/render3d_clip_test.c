#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "render3d_clip.h"
static uint32_t seed=12345;
static double random_value(void) { seed=seed*1664525u+1013904223u; return (double)(seed>>8)/16777216.0; }
/* Float clipping: positions may exceed a plane by a few ulps; colors and UVs
 * are clamped exactly into range. */
static void check_polygon(const AthenaClipVertex3D *vertices,int count) {
    assert(count==0 || (count>=3 && count<=9));
    for(int i=0;i<count;i++) {
        float w=vertices[i].position[3]; assert(w>0);
        for(unsigned j=0;j<3;j++) assert(fabsf(vertices[i].position[j])<=w*(1+1e-5f));
        for(unsigned j=0;j<4;j++) assert(vertices[i].color[j]>=0 && vertices[i].color[j]<=255);
        for(unsigned j=0;j<2;j++) assert(vertices[i].texcoord[j]>=0 && vertices[i].texcoord[j]<=1);
    }
}
static double area(const AthenaClipVertex3D *a,const AthenaClipVertex3D *b,const AthenaClipVertex3D *c) {
    double ax=a->position[0]/a->position[3],ay=a->position[1]/a->position[3];
    double bx=b->position[0]/b->position[3],by=b->position[1]/b->position[3];
    double cx=c->position[0]/c->position[3],cy=c->position[1]/c->position[3];
    return (bx-ax)*(cy-ay)-(by-ay)*(cx-ax);
}
static int chunks,vertices_seen;
static int capture(const AthenaVector4 *positions,const AthenaColor3D *colors,uint32_t count,void *unused) {
    (void)unused; (void)colors; chunks++; vertices_seen+=count;
    assert(count && count<=48 && count%3==0);
    for(unsigned i=0;i<count;i++) {
        assert(positions[i].w>0 && isfinite(positions[i].w));
        assert(fabsf(positions[i].x)<=positions[i].w+0.00001f);
        assert(fabsf(positions[i].y)<=positions[i].w+0.00001f);
        assert(fabsf(positions[i].z)<=positions[i].w+0.00001f);
    }
    return 0;
}
static int refuse(const AthenaVector4 *p,const AthenaColor3D *c,uint32_t n,void *unused) {
    (void)p; (void)c; (void)n; (void)unused; return -2;
}
/* box_relation() reference: every corner transformed on its own. */
static int reference_relation(const AthenaMatrix4 *m,const float min[3],const float max[3]) {
    unsigned common=63; int contained=1;
    for(int i=0;i<8;i++) {
        AthenaVector4 in={i&1?max[0]:min[0],i&2?max[1]:min[1],i&4?max[2]:min[2],1},v;
        ath_matrix4_apply(&v,m,&in);
        common&=(v.x < -v.w?1u:0u)|(v.x>v.w?2u:0u)|(v.y < -v.w?4u:0u)|
            (v.y>v.w?8u:0u)|(v.z < -v.w?16u:0u)|(v.z>v.w?32u:0u);
        float inner=v.w-fabsf(v.w)*1e-5f;
        if(v.w<=0 || fabsf(v.x)>=inner || fabsf(v.y)>=inner || fabsf(v.z)>=inner) contained=0;
    }
    return common?ATHENA_FRUSTUM3D_OUTSIDE:contained?ATHENA_FRUSTUM3D_INSIDE:ATHENA_FRUSTUM3D_INTERSECT;
}
/* Corners built from one transformed corner plus scaled columns round
 * differently from eight full transforms: random boxes and models must
 * agree with the reference except within float noise of a plane. */
static void check_box_relation_random(void) {
    AthenaCamera3D camera; athena_camera3d_init(&camera);
    assert(athena_camera3d_set_projection(&camera,60,4.0f/3.0f,.5f,50));
    assert(athena_camera3d_set_position(&camera,1,2,6) && athena_camera3d_look_at(&camera,0,0,-4));
    assert(athena_camera3d_update(&camera));
    unsigned counts[3]={0},mismatches=0;
    for(unsigned n=0;n<20000;n++) {
        AthenaMatrix4 model={{0}},m; float min[3],max[3];
        for(int i=0;i<12;i++) model.value[i]=(float)(random_value()*2-1);
        for(int i=0;i<3;i++) model.value[12+i]=(float)(random_value()*30-15);
        model.value[14]-=10; model.value[15]=1;
        for(int i=0;i<3;i++) { float a=(float)(random_value()*4-2); min[i]=a; max[i]=a+(float)(random_value()*3); }
        int relation=athena_camera3d_box_relation(&camera,&model,min,max);
        ath_matrix4_multiply(&m,&camera.view_projection,&model);
        assert(relation>=0); counts[relation]++;
        if(relation!=reference_relation(&m,min,max)) mismatches++;
    }
    /* All three outcomes are exercised; disagreements are boundary cases. */
    assert(counts[ATHENA_FRUSTUM3D_INSIDE]>500 && counts[ATHENA_FRUSTUM3D_OUTSIDE]>500 &&
        counts[ATHENA_FRUSTUM3D_INTERSECT]>500);
    assert(mismatches<=2);
}
int main(void) {
    check_box_relation_random();
    AthenaClipVertex3D input[3]={
        {.position={-.5,-.5,0,1},.color={255,0,0,255}},
        {.position={.5,-.5,0,1},.color={0,0,255,255}},
        {.position={-.5,.5,0,1},.color={0,255,0,255}}};
    struct { uint64_t before; AthenaClipVertex3D data[12]; uint64_t after; } output={.before=123,.after=456};
    int changed=-1;
    assert(athena_render3d_clip_triangle(input,output.data,&changed)==3 && !changed);
    assert(!memcmp(input,output.data,sizeof(input)));
    input[1].position[0]=2;
    int count=athena_render3d_clip_triangle(input,output.data,&changed);
    assert(count==4 && changed); check_polygon(output.data,count);
    int intersections=0;
    for(int i=0;i<count;i++) if(output.data[i].position[0]==1) {
        assert(fabsf(output.data[i].color[2]-153)<1e-3f);
        assert(fabsf(output.data[i].color[0]+output.data[i].color[1]-102)<1e-3f); intersections++;
    }
    assert(intersections==2);
    const AthenaClipVertex3D base[3]={
        {.position={-.5,-.5,0,1},.color={255,0,0,255}},
        {.position={.5,-.5,0,1},.color={0,0,255,255}},
        {.position={-.5,.5,0,1},.color={0,255,0,255}}};
    for(unsigned axis=0;axis<3;axis++) for(int sign=-1;sign<=1;sign+=2) {
        memcpy(input,base,sizeof(input));
        for(unsigned i=0;i<3;i++) input[i].position[axis]=sign*2;
        assert(athena_render3d_clip_triangle(input,output.data,&changed)==0);
        memcpy(input,base,sizeof(input));
        input[0].position[axis]=sign*2;
        count=athena_render3d_clip_triangle(input,output.data,&changed);
        assert(count>=3 && changed); check_polygon(output.data,count);
    }
    input[0].position[0]=NAN; assert(athena_render3d_clip_triangle(input,output.data,&changed)==-1);
    input[0].position[0]=INFINITY; assert(athena_render3d_clip_triangle(input,output.data,&changed)==-1);
    input[0].position[0]=0; input[0].color[0]=256;
    assert(athena_render3d_clip_triangle(input,output.data,&changed)==-1); input[0].color[0]=255;
    /* Finite but w+x overflows float: rejected rather than clipped to NaN. */
    input[0].position[0]=FLT_MAX; input[0].position[3]=FLT_MAX;
    assert(athena_render3d_clip_triangle(input,output.data,&changed)==-1);
    memcpy(input,base,sizeof(input));
    /* Saturated colors and UVs on a clipped edge stay inside their ranges. */
    for(unsigned i=0;i<3;i++) {
        for(unsigned j=0;j<4;j++) input[i].color[j]=255;
        input[i].texcoord[0]=1; input[i].texcoord[1]=i==1?0:1;
    }
    input[1].position[0]=2.7f; input[2].position[1]=1.3f;
    count=athena_render3d_clip_triangle(input,output.data,&changed);
    assert(count>=4 && changed); check_polygon(output.data,count);
    for(int i=0;i<count;i++) for(unsigned j=0;j<4;j++) assert(output.data[i].color[j]==255);
    memcpy(input,base,sizeof(input));
    for(unsigned run=0;run<20000;run++) {
        for(unsigned i=0;i<3;i++) {
            for(unsigned j=0;j<3;j++) input[i].position[j]=(random_value()-.5)*8;
            input[i].position[3]=.25+random_value()*3;
        }
        count=athena_render3d_clip_triangle(input,output.data,&changed);
        check_polygon(output.data,count);
        /* Orientation is preserved; float rounding allows only sliver noise. */
        double original=area(&input[0],&input[1],&input[2]);
        for(int i=1;i<count-1;i++) assert(original*area(&output.data[0],&output.data[i],&output.data[i+1])>=-1e-7);
        assert(output.before==123 && output.after==456);
    }
    AthenaCamera3D camera; athena_camera3d_init(&camera);
    assert(athena_camera3d_set_projection(&camera,60,1,1,10));
    assert(athena_camera3d_look_at(&camera,0,0,-1));
    assert(athena_camera3d_set_position(&camera,0,0,0));
    assert(athena_camera3d_update(&camera));
    float inside_min[]={-.1,-.1,-3},inside_max[]={.1,.1,-2};
    float outside_min[]={100,100,-3},outside_max[]={101,101,-2};
    assert(athena_camera3d_box_relation(&camera,NULL,inside_min,inside_max)==ATHENA_FRUSTUM3D_INSIDE);
    assert(athena_camera3d_box_relation(&camera,NULL,outside_min,outside_max)==ATHENA_FRUSTUM3D_OUTSIDE);
    /* Guard band: crossing only a side plane is INSIDE a wider frustum;
     * crossing the near plane never is. */
    float side_min[]={.4f,-.1f,-3},side_max[]={1.5f,.1f,-2.5f};
    assert(athena_camera3d_box_relation(&camera,NULL,side_min,side_max)==ATHENA_FRUSTUM3D_INTERSECT);
    assert(athena_camera3d_box_relation_guard(&camera,NULL,side_min,side_max,4)==ATHENA_FRUSTUM3D_INSIDE);
    assert(athena_camera3d_box_relation_guard(&camera,NULL,side_min,side_max,1)==ATHENA_FRUSTUM3D_INTERSECT);
    float near_min[]={-.1f,-.1f,-2},near_max[]={.1f,.1f,.5f};
    assert(athena_camera3d_box_relation_guard(&camera,NULL,near_min,near_max,8)==ATHENA_FRUSTUM3D_INTERSECT);
    float far_side_min[]={30,0,-3},far_side_max[]={31,1,-2.5f};
    assert(athena_camera3d_box_relation_guard(&camera,NULL,far_side_min,far_side_max,4)==ATHENA_FRUSTUM3D_OUTSIDE);
    assert(athena_camera3d_box_relation_guard(&camera,NULL,side_min,side_max,.5f)==-1);
    /* Near guard: crossing only the near plane (camera at the origin
     * looking down -z, near 1, far 10). */
    assert(athena_camera3d_box_near_guard(&camera,NULL,near_min,near_max,8)==1);
    float wide_near_min[]={-50,-.1f,-2},wide_near_max[]={50,.1f,.5f};
    assert(athena_camera3d_box_near_guard(&camera,NULL,wide_near_min,wide_near_max,8)==0);
    float behind_min[]={-.1f,-.1f,1},behind_max[]={.1f,.1f,2};
    assert(athena_camera3d_box_near_guard(&camera,NULL,behind_min,behind_max,8)==0);
    float deep_min[]={-.1f,-.1f,-20},deep_max[]={.1f,.1f,.5f};
    assert(athena_camera3d_box_near_guard(&camera,NULL,deep_min,deep_max,8)==0); /* past far */
    assert(athena_camera3d_box_near_guard(&camera,NULL,inside_min,inside_max,1)==1);
    AthenaCamera3D saved=camera;
    assert(!athena_camera3d_set_projection(&camera,NAN,1,1,10));
    assert(!athena_camera3d_set_position(&camera,INFINITY,0,0));
    assert(!memcmp(&saved,&camera,sizeof(camera)));
    AthenaQuaternion q={0,0,0,1},before=q,invalid={NAN,0,0,1};
    assert(!athena_quaternion_normalize(&q,&invalid)); assert(!memcmp(&q,&before,sizeof(q)));
    assert(!athena_quaternion_axis_angle(&q,0,0,1,INFINITY));
    float xyz[54*3];
    const float crossing[]={-.1,-.1,.5f, .3f,-.1,-2, -.1,.3f,-2};
    for(unsigned i=0;i<18;i++) memcpy(&xyz[i*9],crossing,sizeof(crossing));
    AthenaGeometry3D geometry={.positions=xyz,.vertex_count=54};
    AthenaMesh3D *mesh=NULL; assert(!athena_mesh3d_create(&geometry,&mesh));
    AthenaMesh3DView view; athena_mesh3d_view(mesh,&view);
    assert(athena_camera3d_box_relation(&camera,NULL,view.minimum,view.maximum)==ATHENA_FRUSTUM3D_INTERSECT);
    AthenaRender3DStats stats={0};
    assert(!athena_render3d_clip_mesh(&view,&camera.view_projection,capture,NULL,&stats));
    assert(chunks==3 && vertices_seen==108 && stats.source_triangles==18 && stats.triangles==36 &&
        stats.clipped_triangles==18 && stats.rejected_triangles==0);
    assert(athena_render3d_clip_mesh(&view,&camera.view_projection,refuse,NULL,&stats)==-2);
    AthenaMatrix4 invalid_matrix=camera.view_projection; invalid_matrix.value[0]=INFINITY;
    assert(athena_camera3d_box_relation(&camera,&invalid_matrix,view.minimum,view.maximum)==-1);
    athena_mesh3d_release(mesh);
    puts("3D homogeneous clipping tests passed"); return 0;
}
