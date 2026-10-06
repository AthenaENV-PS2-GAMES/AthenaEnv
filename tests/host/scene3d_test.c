/* Scene3D graph, dirty propagation, bounds, culling and Loop integration on
 * the host. Drawing uses the render3d_host.c stand-in: no VU1/GS. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <athena/scene3d.h>
#include <athena/loop.h>
extern unsigned host_contained_draws;
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
    puts("Scene3D host tests passed");
    return 0;
}
