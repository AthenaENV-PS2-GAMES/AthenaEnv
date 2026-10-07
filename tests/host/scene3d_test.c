/* Scene3D graph, dirty propagation, bounds, culling and Loop integration on
 * the host. Drawing uses the render3d_host.c stand-in: no VU1/GS. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <athena/scene3d.h>
#include <athena/loop.h>
extern unsigned host_contained_draws;
extern unsigned texture3d_host_destroyed;
#include "../../src/modules/model3d/native/texture3d_backend.h"
static void closef(float a,float b) { assert(fabsf(a-b)<0.0001f); }
static AthenaMesh3D *cube(float half) {
    float xyz[]={-half,-half,-half, half,-half,-half, half,half,half};
    AthenaGeometry3D g={.positions=xyz,.vertex_count=3};
    AthenaMesh3D *mesh=NULL; assert(athena_mesh3d_create(&g,&mesh)==0); return mesh;
}
static void point(const AthenaNode3D *n,float x,float y,float z,float ex,float ey,float ez) {
    AthenaMatrix4 world; assert(athena_node3d_world(n,&world)==0);
    AthenaVector4 in={x,y,z,1},out; ath_matrix4_apply(&out,&world,&in);
    closef(out.x,ex); closef(out.y,ey); closef(out.z,ez);
}
extern AthenaMesh3DView host_last_view;
extern int host_morph_refuse;
/* Compare indexed CPU deformation to the same explicit triangle list.
 * Twenty-five joints force the real CPU fallback; morph refusal covers the
 * other fallback. Generated flat normals must never share across faces. */
static void deform_reuse_test(int skin,int morph,int flat) {
    const float positions[]={-.8f,-.8f,-4, .8f,-.8f,-4, .8f,.8f,-4, -.8f,.8f,-3.8f};
    float normals[12],deltas[12],normal_deltas[12],weights[16]; uint16_t joints[16];
    uint32_t indices[]={2,0,1,2,3,0};
    for(unsigned i=0;i<4;i++) {
        normals[i*3]=0;normals[i*3+1]=0;normals[i*3+2]=1;
        deltas[i*3]=.1f*i;deltas[i*3+1]=.05f*i;deltas[i*3+2]=.03f*i;
        normal_deltas[i*3]=.1f*i;normal_deltas[i*3+1]=0;normal_deltas[i*3+2]=0;
        for(unsigned k=0;k<4;k++){joints[i*4+k]=k?0:24;weights[i*4+k]=k?0:1;}
    }
    AthenaMaterial3D material;athena_material3d_default(&material);material.shading=ATHENA_MATERIAL3D_DIFFUSE;
    AthenaGeometry3D g={.positions=positions,.vertex_count=4,.indices=indices,.index_count=6,
        .normals=flat?NULL:normals,.normal_count=flat?0:4,.material=&material,
        .joints=skin?joints:NULL,.weights=skin?weights:NULL,.skin_count=skin?4:0,
        .target_positions=morph?deltas:NULL,.target_normals=morph&&!flat?normal_deltas:NULL,.target_count=morph?1:0};
    AthenaMesh3D *indexed=NULL,*expanded=NULL;assert(!athena_mesh3d_create(&g,&indexed));
    AthenaMesh3DView view;athena_mesh3d_view(indexed,&view);
    const uint16_t expected[]={0,1,2,0,4,1};
    assert(!flat||!view.deform_reuse);
    if(view.indices) {
        assert(athena_mesh3d_stream_count(&view)==4&&view.chunk_count==1);
        for(unsigned i=0;i<6;i++) {
            uint32_t j=athena_mesh3d_corner(&view,i);
            assert(!memcmp(&view.positions[j],positions+indices[i]*3,sizeof(AthenaPosition3D)));
        }
    }else if(!flat)assert(!memcmp(view.deform_reuse,expected,sizeof(expected)));
    float ep[18],en[18],ed[18],end[18],ew[24];uint16_t ej[24];
    for(unsigned i=0;i<6;i++) {
        unsigned j=indices[i];memcpy(ep+i*3,positions+j*3,12);memcpy(en+i*3,normals+j*3,12);
        memcpy(ed+i*3,deltas+j*3,12);memcpy(end+i*3,normal_deltas+j*3,12);
        memcpy(ew+i*4,weights+j*4,16);memcpy(ej+i*4,joints+j*4,8);
    }
    g.positions=ep;g.vertex_count=6;g.indices=NULL;g.index_count=0;
    g.normals=flat?NULL:en;g.normal_count=flat?0:6;
    g.joints=skin?ej:NULL;g.weights=skin?ew:NULL;g.skin_count=skin?6:0;
    g.target_positions=morph?ed:NULL;g.target_normals=morph&&!flat?end:NULL;
    assert(!athena_mesh3d_create(&g,&expanded));
    memset(indices,0xff,sizeof(indices)); /* copied identity, no borrowed indices */
    AthenaPosition3D result_positions[6],result_normals[6];float result_lo[3],result_hi[3];
    AthenaCamera3D camera;athena_camera3d_init(&camera);
    assert(athena_camera3d_look_at(&camera,0,0,-1)&&athena_camera3d_set_position(&camera,0,0,0));
    host_morph_refuse=1;
    for(unsigned pass=0;pass<2;pass++) {
        AthenaScene3D *scene=athena_scene3d_create();AthenaNode3D *root=athena_scene3d_root(scene),*node=athena_node3d_create();
        assert(!athena_node3d_set_mesh(node,pass?expanded:indexed)&&!athena_node3d_add_child(root,node));
        AthenaNode3D *bones[25];
        if(skin) {
            for(unsigned j=0;j<25;j++){bones[j]=athena_node3d_create();assert(!athena_node3d_add_child(root,bones[j]));}
            assert(!athena_node3d_set_position(bones[24],.2f,.1f,0));
            AthenaSkin3D *k=athena_skin3d_create(bones,25,NULL);assert(k&&!athena_node3d_set_skin(node,k));athena_skin3d_release(k);
            for(unsigned j=0;j<25;j++)athena_node3d_release(bones[j]);
        }
        const float w=.6f;if(morph)assert(!athena_node3d_set_weights(node,&w,1));
        assert(!athena_scene3d_update(scene,NULL));AthenaScene3DDrawStats ds;int error;
        assert(!athena_scene3d_draw(scene,&camera,NULL,ATHENA_RENDER3D_CULL_NONE,&ds,&error)&&!error);
        assert(ds.render.source_triangles==2&&host_last_view.vertex_count==6);
        if(!pass){memcpy(result_positions,host_last_view.positions,sizeof(result_positions));memcpy(result_normals,host_last_view.normals,sizeof(result_normals));
            memcpy(result_lo,host_last_view.minimum,sizeof(result_lo));memcpy(result_hi,host_last_view.maximum,sizeof(result_hi));}
        else{assert(!memcmp(result_positions,host_last_view.positions,sizeof(result_positions)));assert(!memcmp(result_normals,host_last_view.normals,sizeof(result_normals)));
            assert(!memcmp(result_lo,host_last_view.minimum,sizeof(result_lo)));assert(!memcmp(result_hi,host_last_view.maximum,sizeof(result_hi)));}
        athena_node3d_release(node);athena_scene3d_release(scene);
    }
    host_morph_refuse=0;athena_mesh3d_release(indexed);athena_mesh3d_release(expanded);
}
int main(void) {
    AthenaScene3D *scene=athena_scene3d_create(); assert(scene);
    AthenaNode3D *root=athena_scene3d_root(scene);
    AthenaNode3D *parent=athena_node3d_create(),*child=athena_node3d_create(),*leaf=athena_node3d_create();
    AthenaMesh3D *mesh=cube(1);
    assert(athena_node3d_set_mesh(child,mesh)==0 && athena_node3d_set_mesh(leaf,mesh)==0);
    athena_mesh3d_release(mesh);
    /* Composition: world = parent * local, with Euler Rz*Ry*Rx and scale. */
    assert(athena_node3d_add_child(root,parent)==0 && athena_node3d_add_child(parent,child)==0);
    assert(athena_node3d_add_child(child,leaf)==0);
    assert(athena_node3d_set_position(parent,10,0,0)==0);
    assert(athena_node3d_set_euler(parent,0,0,(float)M_PI/2)==0);
    assert(athena_node3d_set_scale(parent,2,2,2)==0);
    assert(athena_node3d_set_position(child,1,0,0)==0);
    assert(athena_node3d_set_position(leaf,0,0,-3)==0);
    AthenaMatrix4 m;
    assert(athena_node3d_world(child,&m)==ATHENA_SCENE3D_ESTALE);
    assert(athena_scene3d_stale(scene));
    AthenaScene3DUpdateStats us;
    assert(athena_scene3d_update(scene,&us)==0 && !athena_scene3d_stale(scene));
    assert(us.visited_nodes==4 && us.world_updates==4 && us.bounds_updates==4);
    point(child,0,0,0,10,2,0);
    point(leaf,0,0,0,10,2,-6);
    float lo[3],hi[3];
    assert(athena_node3d_world_bounds(leaf,lo,hi)==1);
    closef(lo[0],8); closef(hi[0],12); closef(lo[2],-8); closef(hi[2],-4);
    assert(athena_node3d_world_bounds(parent,lo,hi)==1);
    closef(lo[2],-8); closef(hi[2],2);
    /* Clean scene: nothing visited. Leaf change updates only its own path. */
    assert(athena_scene3d_update(scene,&us)==0 && us.visited_nodes==0);
    assert(athena_node3d_set_position(leaf,0,0,-4)==0);
    assert(athena_node3d_world(child,&m)==0);
    assert(athena_node3d_world(leaf,&m)==ATHENA_SCENE3D_ESTALE);
    assert(athena_node3d_world_bounds(parent,lo,hi)==ATHENA_SCENE3D_ESTALE);
    assert(athena_scene3d_update(scene,&us)==0);
    assert(us.visited_nodes==4 && us.world_updates==1 && us.bounds_updates==4);
    point(leaf,0,0,0,10,2,-8);
    /* Rejected edits leave the graph unchanged. */
    assert(athena_node3d_add_child(leaf,parent)==ATHENA_SCENE3D_ECYCLE);
    assert(athena_node3d_add_child(leaf,leaf)==ATHENA_SCENE3D_ECYCLE);
    assert(athena_node3d_add_child(leaf,root)==ATHENA_SCENE3D_EROOT);
    assert(athena_node3d_set_position(leaf,NAN,0,0)==ATHENA_SCENE3D_EINVAL);
    assert(athena_node3d_set_rotation(leaf,0,0,0,0)==ATHENA_SCENE3D_EINVAL);
    assert(!athena_scene3d_stale(scene) && athena_node3d_parent(parent)==root);
    /* Depth: root + 63 levels is the limit, whether built down or moved in. */
    AthenaNode3D *chain=athena_node3d_create(),*tail=chain;
    for(uint32_t i=1;i<ATHENA_SCENE3D_MAX_DEPTH-1;i++) {
        AthenaNode3D *next=athena_node3d_create();
        assert(athena_node3d_add_child(tail,next)==0); athena_node3d_release(next); tail=next;
    }
    assert(athena_node3d_add_child(root,chain)==0);
    AthenaNode3D *extra=athena_node3d_create();
    assert(athena_node3d_add_child(tail,extra)==ATHENA_SCENE3D_EDEPTH);
    athena_node3d_detach(chain);
    assert(athena_node3d_add_child(extra,chain)==0); athena_node3d_release(chain);
    assert(athena_node3d_add_child(root,extra)==ATHENA_SCENE3D_EDEPTH);
    athena_node3d_release(extra); /* frees extra and, with it, the whole chain */
    /* Reparenting keeps the local transform and refreshes both old bounds. */
    assert(athena_node3d_add_child(root,leaf)==0 && athena_node3d_parent(leaf)==root);
    assert(athena_node3d_child_count(child)==0 && athena_node3d_child_count(root)==2);
    assert(athena_scene3d_update(scene,&us)==0);
    point(leaf,0,0,0,0,0,-4);
    assert(athena_node3d_world_bounds(child,lo,hi)==1); closef(lo[2],-2); closef(hi[2],2);
    /* Visibility removes a subtree from bounds and draw. */
    AthenaCamera3D camera; athena_camera3d_init(&camera);
    assert(athena_camera3d_set_projection(&camera,60,1,1,100));
    assert(athena_camera3d_set_position(&camera,5,0,40) && athena_camera3d_look_at(&camera,5,0,0));
    AthenaScene3DDrawStats ds; int render=1;
    host_contained_draws=0;
    assert(athena_scene3d_draw(scene,&camera,NULL,ATHENA_RENDER3D_CULL_NONE,&ds,&render)==0 && render==0);
    assert(ds.queued_objects==2 && ds.render.submitted_objects==2 && ds.render.culled_objects==0);
    /* Both meshes share the unlit pipeline, so the draw emits one pass. */
    assert(ds.render.draw_passes==2 && ds.render.pipeline_passes==1);
    /* The root bounds are INSIDE: no mesh repeats the frustum test. */
    assert(host_contained_draws==2);
    assert(athena_node3d_set_visible(parent,0)==0);
    assert(athena_scene3d_draw(scene,&camera,NULL,ATHENA_RENDER3D_CULL_NONE,&ds,&render)==ATHENA_SCENE3D_ESTALE);
    assert(athena_scene3d_update(scene,&us)==0 && us.world_updates==0);
    assert(athena_node3d_world_bounds(parent,lo,hi)==0);
    assert(athena_scene3d_draw(scene,&camera,NULL,ATHENA_RENDER3D_CULL_NONE,&ds,&render)==0);
    assert(ds.queued_objects==1 && ds.render.submitted_objects==1);
    assert(athena_node3d_set_visible(parent,1)==0 && athena_scene3d_update(scene,&us)==0);
    /* A subtree outside the frustum is rejected once, counting its meshes. */
    assert(athena_node3d_set_position(parent,1000,0,0)==0 && athena_scene3d_update(scene,&us)==0);
    host_contained_draws=0;
    assert(athena_scene3d_draw(scene,&camera,NULL,ATHENA_RENDER3D_CULL_NONE,&ds,&render)==0);
    assert(ds.culled_subtrees==1 && ds.render.culled_objects==1 && ds.render.submitted_objects==2);
    assert(ds.queued_objects==1 && ds.render.draw_passes==1);
    /* The root straddles, the leaf alone is INSIDE and skips the render test. */
    assert(host_contained_draws==1);
    /* A mesh straddling the frustum edge keeps the precise render test. */
    assert(athena_node3d_set_position(parent,28,0,0)==0 && athena_scene3d_update(scene,&us)==0);
    host_contained_draws=0;
    assert(athena_scene3d_draw(scene,&camera,NULL,ATHENA_RENDER3D_CULL_NONE,&ds,&render)==0);
    assert(ds.queued_objects==2 && ds.render.culled_objects==0 && ds.culled_subtrees==0);
    assert(host_contained_draws==1);
    assert(athena_node3d_set_position(parent,1000,0,0)==0 && athena_scene3d_update(scene,&us)==0);
    assert(athena_scene3d_draw(scene,&camera,NULL,(AthenaRender3DCull)5,&ds,&render)==ATHENA_SCENE3D_EINVAL);
    /* Overflow fails the update and keeps the scene stale. */
    assert(athena_node3d_set_scale(parent,1e30f,1e30f,1e30f)==0 && athena_node3d_set_scale(child,1e30f,1,1)==0);
    assert(athena_scene3d_update(scene,&us)==ATHENA_SCENE3D_EINVAL && athena_scene3d_stale(scene));
    assert(athena_node3d_set_scale(parent,1,1,1)==0 && athena_node3d_set_scale(child,1,1,1)==0);
    assert(athena_scene3d_update(scene,&us)==0 && !athena_scene3d_stale(scene));
    /* Detached subtrees are outside the scene: world queries are stale. */
    athena_node3d_detach(parent);
    assert(athena_node3d_world(child,&m)==ATHENA_SCENE3D_ESTALE && athena_node3d_parent(parent)==NULL);
    assert(athena_node3d_local(child,&m)==0); closef(m.value[12],1);
    assert(athena_node3d_add_child(root,parent)==0);
    /* Loop: POST_UPDATE refreshes; the system retains the scene. */
    int id=athena_scene3d_attach_loop(scene,5,NULL); assert(id>0);
    assert(athena_scene3d_attach_loop(scene,5,NULL)==ATHENA_SCENE3D_EINVAL);
    assert(athena_loop_system_get(id)->phases==ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_POST_UPDATE));
    assert(athena_scene3d_stale(scene));
    assert(athena_loop_systems_run(ATHENA_LOOP_UPDATE,1,1,NULL)==0 && athena_scene3d_stale(scene));
    assert(athena_loop_systems_run(ATHENA_LOOP_POST_UPDATE,1,1,NULL)==0 && !athena_scene3d_stale(scene));
    assert(athena_scene3d_detach_loop(scene)==1 && !athena_scene3d_loop_system(scene));
    assert(athena_scene3d_detach_loop(scene)==0 && athena_loop_system_get(id)==NULL);
    int owner;
    assert(athena_scene3d_attach_loop(scene,0,&owner)>0);
    athena_scene3d_detach_owner(NULL); assert(athena_scene3d_loop_system(scene));
    athena_scene3d_detach_owner(&owner); assert(!athena_scene3d_loop_system(scene));
    assert(athena_scene3d_attach_loop(scene,0,NULL)>0);
    athena_node3d_release(parent); athena_node3d_release(leaf);
    athena_scene3d_release(scene); /* the Loop still holds it */
    assert(athena_node3d_parent(child)!=NULL);
    athena_loop_systems_clear();   /* last scene reference: frees the graph */
    assert(athena_node3d_parent(child)==NULL);
    athena_node3d_release(child);
    /* Native motion: velocity and local-axis spin integrated by advance(). */
    {
        AthenaScene3D *ms=athena_scene3d_create(); AthenaNode3D *mr=athena_scene3d_root(ms);
        AthenaNode3D *group=athena_node3d_create(),*mover=athena_node3d_create(),*still=athena_node3d_create();
        assert(!athena_node3d_add_child(mr,group) && !athena_node3d_add_child(group,mover) && !athena_node3d_add_child(group,still));
        assert(athena_scene3d_update(ms,NULL)==0);
        assert(athena_scene3d_advance(ms,1)==0 && !athena_scene3d_stale(ms)); /* nothing moves */
        assert(!athena_node3d_set_position(mover,1,2,3) && !athena_node3d_set_velocity(mover,2,0,-1));
        assert(!athena_node3d_set_euler(mover,(float)M_PI/2,0,0));
        assert(!athena_node3d_set_spin(mover,0,(float)M_PI,0)); /* half a turn per second about local y */
        assert(athena_scene3d_update(ms,NULL)==0);
        assert(athena_scene3d_advance(ms,.5f)==1 && athena_scene3d_stale(ms));
        assert(athena_scene3d_update(ms,NULL)==0);
        AthenaNode3D *expected=athena_node3d_create();
        assert(!athena_node3d_set_position(expected,2,2,2.5f));
        /* Local spin composes on the right: Rx(pi/2) then Ry(pi/2) about local y. */
        AthenaQuaternion a,b,q; assert(athena_quaternion_euler(&a,(float)M_PI/2,0,0));
        assert(athena_quaternion_axis_angle(&b,0,1,0,(float)M_PI/2) && athena_quaternion_multiply(&q,&a,&b));
        assert(!athena_node3d_set_rotation(expected,q.x,q.y,q.z,q.w));
        AthenaMatrix4 got,want; assert(!athena_node3d_local(mover,&got) && !athena_node3d_local(expected,&want));
        for(int i=0;i<16;i++) closef(got.value[i],want.value[i]);
        athena_node3d_release(expected);
        /* Detached branches stop counting; reattached ones move again. */
        athena_node3d_retain(mover); athena_node3d_detach(mover);
        assert(athena_scene3d_advance(ms,1)==0);
        assert(!athena_node3d_add_child(still,mover)); athena_node3d_release(mover);
        assert(athena_scene3d_advance(ms,1)==1);
        /* Zero motion stops; a second mover adds up; bad dt and overflow fail. */
        assert(!athena_node3d_set_velocity(still,0,1,0) && athena_scene3d_advance(ms,0)==0);
        assert(athena_scene3d_advance(ms,1)==2);
        assert(!athena_node3d_set_velocity(mover,0,0,0) && !athena_node3d_set_spin(mover,0,0,0));
        assert(athena_scene3d_advance(ms,1)==1);
        assert(athena_scene3d_advance(ms,-1)==ATHENA_SCENE3D_EINVAL && athena_scene3d_advance(ms,NAN)==ATHENA_SCENE3D_EINVAL);
        assert(athena_node3d_set_spin(still,INFINITY,0,0)==ATHENA_SCENE3D_EINVAL);
        assert(!athena_node3d_set_velocity(still,3e38f,0,0) && !athena_node3d_set_position(still,3e38f,0,0));
        assert(athena_scene3d_advance(ms,1)==ATHENA_SCENE3D_EINVAL);
        assert(!athena_node3d_set_velocity(still,0,0,0) && !athena_node3d_set_position(still,0,0,0));
        /* The Loop system advances with its dt before updating. */
        assert(!athena_node3d_set_velocity(group,1,0,0));
        int sys=athena_scene3d_attach_loop(ms,0,NULL); assert(sys>0);
        assert(athena_loop_systems_run(ATHENA_LOOP_POST_UPDATE,.25f,.25f,NULL)==0 && !athena_scene3d_stale(ms));
        assert(!athena_node3d_local(group,&got)); closef(got.value[12],.25f);
        athena_node3d_release(group); athena_node3d_release(mover); athena_node3d_release(still);
        athena_scene3d_release(ms); athena_loop_systems_clear();
    }
    /* A joint that is an ancestor of its skinned node (valid glTF): the skin
     * must not keep it alive, or the subtree would leak. The mesh's texture
     * backend is destroyed only if the whole subtree is freed. */
    {
        uint32_t pixel=0xffffffff; AthenaTexture3DPixels px={.width=1,.height=1,.pixels=&pixel,.pixel_count=1};
        AthenaTexture3D *texture=NULL; assert(!athena_texture3d_create(&px,&texture));
        AthenaTexture3DBinding binding; assert(!athena_texture3d_bind(texture,&binding));
        AthenaMaterial3D material; athena_material3d_default(&material); material.texture=texture;
        float positions[]={0,0,0,1,0,0,0,1,0},uv[]={0,0,1,0,0,1},weights[]={1,0,0,0,1,0,0,0,1,0,0,0};
        uint16_t joints[12]={0};
        AthenaGeometry3D geometry={.positions=positions,.vertex_count=3,.texcoords=uv,.texcoord_count=3,
            .material=&material,.joints=joints,.weights=weights,.skin_count=3};
        AthenaMesh3D *mesh=NULL; assert(!athena_mesh3d_create(&geometry,&mesh)); athena_texture3d_release(texture);
        AthenaNode3D *joint=athena_node3d_create(),*skinned=athena_node3d_create();
        assert(!athena_node3d_add_child(joint,skinned)&&!athena_node3d_set_mesh(skinned,mesh));
        athena_mesh3d_release(mesh);
        AthenaSkin3D *skin=athena_skin3d_create(&joint,1,NULL); assert(skin);
        assert(!athena_node3d_set_skin(skinned,skin)); athena_skin3d_release(skin);
        unsigned destroyed=texture3d_host_destroyed;
        athena_node3d_release(skinned); athena_node3d_release(joint);
        assert(texture3d_host_destroyed==destroyed+1);
        /* A skin outliving its joint: the entry is cleared, not dangling. */
        joint=athena_node3d_create(); skin=athena_skin3d_create(&joint,1,NULL); assert(skin);
        athena_node3d_release(joint); athena_skin3d_release(skin);
    }
    /* Textured meshes are grouped by texture: A(t1) B(t2) C(t1) is two
     * passes, not three. */
    {
        uint32_t pixel=0xffffffff; AthenaTexture3DPixels px={.width=1,.height=1,.pixels=&pixel,.pixel_count=1};
        AthenaTexture3D *t[2]; assert(!athena_texture3d_create(&px,&t[0])&&!athena_texture3d_create(&px,&t[1]));
        float positions[]={-.1f,-.1f,0,.1f,-.1f,0,0,.1f,0},uv[]={0,0,1,0,0,1};
        AthenaScene3D *textured=athena_scene3d_create(); assert(textured);
        for(int i=0;i<3;i++) {
            AthenaMaterial3D material; athena_material3d_default(&material); material.texture=t[i==1];
            AthenaGeometry3D geometry={.positions=positions,.vertex_count=3,.texcoords=uv,.texcoord_count=3,.material=&material};
            AthenaMesh3D *mesh=NULL; assert(!athena_mesh3d_create(&geometry,&mesh));
            AthenaNode3D *node=athena_node3d_create(); assert(!athena_node3d_set_mesh(node,mesh));
            assert(!athena_node3d_add_child(athena_scene3d_root(textured),node));
            athena_node3d_release(node); athena_mesh3d_release(mesh);
        }
        athena_texture3d_release(t[0]); athena_texture3d_release(t[1]);
        AthenaCamera3D view; athena_camera3d_init(&view);
        AthenaScene3DDrawStats drawn;
        assert(!athena_scene3d_update(textured,NULL)&&!athena_scene3d_draw(textured,&view,NULL,ATHENA_RENDER3D_CULL_NONE,&drawn,NULL));
        assert(drawn.queued_objects==3&&drawn.render.draw_passes==3&&drawn.render.pipeline_passes==2);
        athena_scene3d_release(textured);
    }
    /* Morph followers take their parent's weights. */
    {
        AthenaNode3D *parent=athena_node3d_create(),*follower=athena_node3d_create(),*other=athena_node3d_create();
        assert(!athena_node3d_add_child(parent,follower)&&!athena_node3d_add_child(parent,other));
        assert(!athena_node3d_set_morph_follower(follower,1));
        const float w[2]={.25f,.5f}; float got[ATHENA_MODEL3D_MAX_TARGETS];
        assert(!athena_node3d_set_weights(parent,w,2));
        athena_node3d_get_weights(follower,got); assert(got[0]==.25f&&got[1]==.5f&&got[2]==0);
        athena_node3d_get_weights(other,got); assert(got[0]==0&&got[1]==0);
        athena_node3d_release(follower); athena_node3d_release(other); athena_node3d_release(parent);
    }
    deform_reuse_test(1,0,0);deform_reuse_test(0,1,0);deform_reuse_test(1,1,0);
    deform_reuse_test(1,1,1);deform_reuse_test(0,1,1);
    puts("Scene3D host tests passed");
    return 0;
}
