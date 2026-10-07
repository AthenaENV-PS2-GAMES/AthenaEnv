/* gltf3d: glTF scene hierarchy, matrices, primitives and animations. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <athena/gltf3d.h>
#include <athena/render3d.h>
extern AthenaMesh3DView host_last_view;
extern unsigned host_vu_skins;
extern unsigned host_vu_morphs; extern int host_morph_refuse;
/* VU1 weights are 8-bit: within 1/255 of the float blend. */
static void closeq(float a,float b) { assert(fabsf(a-b)<0.006f); }
static void closef(float a,float b) { assert(fabsf(a-b)<0.001f); }
static void trs(AthenaNode3D *n,float p[3],AthenaQuaternion *q,float s[3]) { athena_node3d_get_trs(n,p,q,s); }
int main(void) {
    AthenaGltf3D *g=NULL;
    assert(athena_gltf3d_load("bin/models/missing.glb",NULL,&g)==ATHENA_MODEL3D_EIO && !g);
    assert(athena_gltf3d_load(NULL,NULL,&g)==ATHENA_MODEL3D_EINVAL);
    /* A plain mesh file: one node, no clips. */
    assert(!athena_gltf3d_load("bin/models/lit_cube.glb",NULL,&g));
    assert(athena_gltf3d_node_count(g)==1 && athena_gltf3d_clip_count(g)==0 && athena_node3d_mesh(athena_gltf3d_node(g,0)));
    athena_gltf3d_release(g); g=NULL;
    assert(!athena_gltf3d_load("bin/models/arm.glb",NULL,&g));
    assert(athena_gltf3d_node_count(g)==3);
    assert(!strcmp(athena_gltf3d_node_name(g,0),"base") && !strcmp(athena_gltf3d_node_name(g,2),"lamp"));
    AthenaNode3D *base=athena_gltf3d_node(g,0),*arm=athena_gltf3d_node(g,1),*lamp=athena_gltf3d_node(g,2);
    /* Hierarchy: root -> base -> arm (-> second primitive), root -> lamp. */
    assert(athena_node3d_child_count(athena_gltf3d_root(g))==2 && athena_node3d_parent(arm)==base);
    assert(athena_node3d_parent(lamp)==athena_gltf3d_root(g));
    assert(athena_node3d_child_count(arm)==1 && athena_node3d_mesh(athena_node3d_child(arm,0)) && athena_node3d_mesh(arm));
    float p[3],s[3]; AthenaQuaternion q;
    trs(arm,p,&q,s); closef(p[1],1.5f); closef(s[0],.5f); closef(q.w,1);
    /* Matrix decomposed: 90 degrees about Y at (3, 0, 0). */
    trs(lamp,p,&q,s); closef(p[0],3); closef(s[0],1); closef(fabsf(q.y),sqrtf(.5f)); closef(fabsf(q.w),sqrtf(.5f));
    /* Clips. */
    assert(athena_gltf3d_clip_count(g)==2 && !strcmp(athena_gltf3d_clip_name(g,0),"wave"));
    AthenaClip3D *wave=athena_gltf3d_clip(g,0),*bob=athena_gltf3d_clip(g,1);
    closef(athena_clip3d_duration(wave),2); assert(athena_clip3d_target_count(wave)==2);
    closef(athena_clip3d_duration(bob),1);
    AthenaNode3D *nodes[3]={base,arm,lamp};
    AthenaPlayer3D *player=athena_player3d_create(wave,nodes,3); assert(player);
    athena_player3d_play(player); athena_player3d_advance(player,.5f);
    trs(arm,p,&q,s); closef(q.z,sinf((float)M_PI/8)); /* LINEAR: 45 deg about Z */
    trs(base,p,&q,s); closef(p[0],0);                  /* STEP: still the first key */
    athena_player3d_advance(player,.75f); trs(base,p,&q,s); closef(p[0],1);
    athena_player3d_release(player);
    player=athena_player3d_create(bob,nodes,3); assert(player);
    athena_player3d_play(player); athena_player3d_advance(player,.5f);
    trs(base,p,&q,s); closef(s[0],1.5f); /* CUBICSPLINE values, played linearly */
    athena_player3d_release(player);
    /* The scene owns nothing the caller still uses after release... */
    athena_node3d_retain(base);
    athena_gltf3d_release(g);
    /* ...and retained nodes survive with their children. */
    assert(athena_node3d_child_count(base)==1);
    athena_node3d_release(base);
    /* Skinning: bind pose, then tip turned 90 degrees about Z at y = 1. */
    assert(!athena_gltf3d_load("bin/models/bend.glb",NULL,&g));
    AthenaScene3D *scene=athena_scene3d_create();
    assert(!athena_node3d_add_child(athena_scene3d_root(scene),athena_gltf3d_root(g)));
    assert(!athena_scene3d_update(scene,NULL));
    AthenaCamera3D camera; athena_camera3d_init(&camera);
    AthenaScene3DDrawStats ds; int render=0;
    AthenaMesh3DView source; athena_mesh3d_view(athena_node3d_mesh(athena_gltf3d_node(g,0)),&source);
    assert(source.joints && source.joint_count==2 && source.vertex_count==54);
    host_vu_skins=0;
    assert(!athena_scene3d_draw(scene,&camera,NULL,ATHENA_RENDER3D_CULL_NONE,&ds,&render) && ds.queued_objects==1);
    assert(host_vu_skins==1); /* contained: deformed on VU1 */
    for(uint32_t i=0;i<source.vertex_count;i++) {
        closeq(host_last_view.positions[i].x,source.positions[i].x); closeq(host_last_view.positions[i].y,source.positions[i].y);
    }
    AthenaNode3D *bend_nodes[3]={athena_gltf3d_node(g,0),athena_gltf3d_node(g,1),athena_gltf3d_node(g,2)};
    AthenaPlayer3D *bend=athena_player3d_create(athena_gltf3d_clip(g,0),bend_nodes,3); assert(bend);
    assert(!athena_player3d_set_time(bend,1)); /* tip at 90 degrees */
    assert(athena_scene3d_draw(scene,&camera,NULL,ATHENA_RENDER3D_CULL_NONE,&ds,&render)==ATHENA_SCENE3D_ESTALE);
    assert(!athena_scene3d_update(scene,NULL));
    for(int pass=0;pass<2;pass++) {
        /* Pass 0: contained, VU1 (8-bit weights). Pass 1: the camera inside the
         * column makes it cross the frustum: EE skinning with float weights. */
        if(pass) { const float eye[3]={0,1,0},at[3]={0,1,-1}; assert(athena_camera3d_set_view(&camera,eye,at)); }
        host_vu_skins=0;
        assert(!athena_scene3d_draw(scene,&camera,NULL,ATHENA_RENDER3D_CULL_NONE,&ds,&render));
        assert(host_vu_skins==(pass?0u:1u));
        void (*check)(float,float)=pass?closef:closeq;
        for(uint32_t i=0;i<source.vertex_count;i++) {
            const AthenaPosition3D *v=&source.positions[i];
            float wt=source.weights[i*4+1],rx=1-v->y,ry=v->x+1;   /* Rz(90) about (0, 1) */
            check(host_last_view.positions[i].x,(1-wt)*v->x+wt*rx);
            check(host_last_view.positions[i].y,(1-wt)*v->y+wt*ry);
            check(host_last_view.positions[i].z,v->z);
        }
        /* Normals turn with their joints and stay unit length. */
        const AthenaPosition3D *n=&host_last_view.normals[53];
        closeq(n->x*n->x+n->y*n->y+n->z*n->z,1); closeq(n->x,-1); /* top cap: +Y turned to -X */
    }
    athena_player3d_release(bend);
    athena_scene3d_release(scene); athena_gltf3d_release(g);
    /* Morph targets: stretch raises the top face, pinch (sparse accessor)
     * pulls it to the Y axis; the "morph" clip animates the weights. */
    AthenaMaterial3D diffuse; athena_material3d_default(&diffuse); diffuse.shading=ATHENA_MATERIAL3D_DIFFUSE;
    assert(!athena_gltf3d_load("bin/models/morph.glb",&diffuse,&g));
    AthenaNode3D *box=athena_gltf3d_node(g,0);
    AthenaMesh3DView mv; athena_mesh3d_view(athena_node3d_mesh(box),&mv);
    assert(mv.target_count==2 && mv.vertex_count==36 && mv.flat_normals && !mv.target_normals);
    closef(mv.target_maximum[0][1],1); closef(mv.target_minimum[1][0],-1); closef(mv.target_maximum[1][2],1);
    scene=athena_scene3d_create();
    assert(!athena_node3d_add_child(athena_scene3d_root(scene),athena_gltf3d_root(g)));
    athena_camera3d_init(&camera);
    { const float eye[3]={0,3,9},at[3]={0,0,0}; assert(athena_camera3d_set_view(&camera,eye,at)); }
    AthenaPlayer3D *morph=athena_player3d_create(athena_gltf3d_clip(g,0),&box,1); assert(morph);
    float weights[ATHENA_MODEL3D_MAX_TARGETS],lo[3],hi[3];
    for(int step=0;step<8;step++) {
        /* Steps 4..7 repeat 0..3 with VU1 refused: the EE blend must agree. */
        host_morph_refuse=step>=4;
        unsigned morphs_before=host_vu_morphs;
        /* Times 0, 1, 2 and 1.5: weights [0,0], [1,0], [0,1], [.5,.5]. */
        static const float times[4]={0,1,2,1.5f},ws4[4][2]={{0,0},{1,0},{0,1},{.5f,.5f}};
        const float *wt=ws4[step%4];
        assert(!athena_player3d_set_time(morph,times[step%4]));
        athena_node3d_get_weights(box,weights);
        closef(weights[0],wt[0]); closef(weights[1],wt[1]);
        assert(!athena_scene3d_update(scene,NULL));
        assert(athena_node3d_world_bounds(box,lo,hi)==1);
        closef(hi[1],1+wt[0]); closef(lo[1],-1);
        assert(!athena_scene3d_draw(scene,&camera,NULL,ATHENA_RENDER3D_CULL_NONE,&ds,&render) && ds.queued_objects==1);
        for(uint32_t i=0;i<36;i++) {
            const AthenaPosition3D *b=&mv.positions[i],*d=&host_last_view.positions[i];
            int top=b->y>0;
            closef(d->y,b->y+(top?wt[0]:0));
            closef(d->x,b->x*(top?1-wt[1]:1)); closef(d->z,b->z*(top?1-wt[1]:1));
        }
        /* Face 0 (z = -1): its flat normal tilts up and inward under pinch. */
        const AthenaPosition3D *n=&host_last_view.normals[0];
        closef(n->x*n->x+n->y*n->y+n->z*n->z,1);
        if(wt[1]>0) assert(n->y>0.1f&&n->z<0); else { closef(n->z,-1); closef(n->y,0); }
        /* Weights 0: no morph at all; otherwise VU1, or the EE when refused. */
        assert(host_vu_morphs-morphs_before==(unsigned)(step<4&&step%4!=0));
    }
    host_morph_refuse=0;
    athena_player3d_release(morph);
    athena_scene3d_release(scene); athena_gltf3d_release(g);
    puts("glTF3D host tests passed");
    return 0;
}
