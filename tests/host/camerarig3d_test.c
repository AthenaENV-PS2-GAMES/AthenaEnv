/* CameraRig3D follow/orbit controllers on the host. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <athena/camerarig3d.h>
#include <athena/loop.h>
static void closef(float a,float b) { assert(fabsf(a-b)<0.0005f); }
static void eye_is(const AthenaCamera3D *c,float x,float y,float z) { closef(c->position.x,x); closef(c->position.y,y); closef(c->position.z,z); }
static void look_is(const AthenaCamera3D *c,float x,float y,float z) { closef(c->target.x,x); closef(c->target.y,y); closef(c->target.z,z); }
static int released;
static void on_release(AthenaCamera3D *c) { (void)c; released++; }
int main(void) {
    AthenaCamera3D camera; athena_camera3d_init(&camera);
    /* set_view: atomic, rejects degenerate views and keeps the camera. */
    const float e[3]={1,2,3},same[3]={1,2,3},origin[3]={0,0,0};
    assert(athena_camera3d_set_view(&camera,e,origin)); eye_is(&camera,1,2,3); look_is(&camera,0,0,0);
    assert(!athena_camera3d_set_view(&camera,e,same)); eye_is(&camera,1,2,3);
    AthenaScene3D *scene=athena_scene3d_create(); AthenaNode3D *hero=athena_node3d_create();
    assert(!athena_node3d_add_child(athena_scene3d_root(scene),hero));
    assert(!athena_node3d_set_position(hero,10,0,0) && !athena_node3d_set_euler(hero,0,(float)M_PI/2,0));
    assert(!athena_scene3d_update(scene,NULL));
    /* Follow: local offset rotates with the target. */
    AthenaCameraRig3D *follow=athena_rig3d_follow_create(&camera,on_release,hero); assert(follow);
    assert(!athena_rig3d_set_offset(follow,0,2,6,1) && !athena_rig3d_set_look_offset(follow,0,1,0));
    assert(athena_rig3d_update(follow,1.0f/60)==1);
    eye_is(&camera,16,2,0); look_is(&camera,10,1,0);
    /* World offset ignores the rotation. */
    assert(!athena_rig3d_set_offset(follow,0,2,6,0)); athena_rig3d_snap(follow);
    assert(athena_rig3d_update(follow,0)==1); eye_is(&camera,10,2,6);
    /* A stale target skips the frame. */
    assert(!athena_node3d_set_position(hero,20,0,0));
    assert(athena_rig3d_update(follow,1)==0); eye_is(&camera,10,2,6);
    assert(!athena_scene3d_update(scene,NULL));
    /* Exponential smoothing: one second at sharpness 1 covers 1 - 1/e. */
    assert(!athena_rig3d_set_sharpness(follow,1,0));
    assert(athena_rig3d_update(follow,1)==1);
    closef(camera.position.x,10+10*(1-expf(-1))); look_is(&camera,20,1,0);
    athena_rig3d_snap(follow); assert(athena_rig3d_update(follow,0)==1); eye_is(&camera,20,2,6);
    assert(athena_rig3d_set_sharpness(follow,-1,0)==ATHENA_RIG3D_EINVAL);
    athena_rig3d_set_enabled(follow,0); assert(athena_rig3d_update(follow,1)==0 && !athena_rig3d_enabled(follow));
    athena_rig3d_set_enabled(follow,1);
    assert(athena_rig3d_update(follow,-1)==ATHENA_RIG3D_EINVAL);
    /* Orbit around a fixed centre: yaw 0 sits on +Z. */
    AthenaCamera3D orbit_camera; athena_camera3d_init(&orbit_camera);
    AthenaCameraRig3D *orbit=athena_rig3d_orbit_create(&orbit_camera,NULL,NULL); assert(orbit);
    assert(!athena_rig3d_set_center(orbit,0,1,0) && !athena_rig3d_set_distance(orbit,5));
    assert(athena_rig3d_update(orbit,0)==1); eye_is(&orbit_camera,0,1,5); look_is(&orbit_camera,0,1,0);
    assert(!athena_rig3d_set_angles(orbit,(float)M_PI/2,0)); athena_rig3d_snap(orbit);
    assert(athena_rig3d_update(orbit,0)==1); eye_is(&orbit_camera,5,1,0);
    /* Limits clamp input; auto-rotation adds yaw over time. */
    assert(!athena_rig3d_set_limits(orbit,-.5f,.5f,2,8));
    assert(!athena_rig3d_rotate(orbit,0,3) && !athena_rig3d_zoom(orbit,100));
    float yaw,pitch,distance; athena_rig3d_get_angles(orbit,&yaw,&pitch,&distance);
    closef(pitch,.5f); closef(distance,8);
    assert(athena_rig3d_set_limits(orbit,-2,0,1,1)==ATHENA_RIG3D_EINVAL);
    assert(!athena_rig3d_set_angles(orbit,0,0) && !athena_rig3d_set_auto_rotate(orbit,1));
    athena_rig3d_snap(orbit); assert(athena_rig3d_update(orbit,.5f)==1);
    athena_rig3d_get_angles(orbit,&yaw,NULL,NULL); closef(yaw,.5f);
    eye_is(&orbit_camera,8*sinf(.5f),1,8*cosf(.5f));
    /* A target moves the centre. */
    assert(!athena_rig3d_set_auto_rotate(orbit,0) && !athena_rig3d_set_target(orbit,hero));
    athena_rig3d_snap(orbit); assert(athena_rig3d_update(orbit,0)==1); look_is(&orbit_camera,20,1,0);
    /* update_all and the Loop system. */
    assert(athena_rig3d_update_all(0)==2);
    int id=athena_rig3d_attach_loop(ATHENA_RIG3D_LOOP_PRIORITY,&id); assert(id>0);
    assert(athena_rig3d_attach_loop(0,NULL)==ATHENA_RIG3D_EINVAL);
    assert(!athena_node3d_set_position(hero,30,0,0) && !athena_scene3d_update(scene,NULL));
    athena_rig3d_snap(follow);
    assert(athena_loop_systems_run(ATHENA_LOOP_POST_UPDATE,1.0f/60,1.0f/60,NULL)==0); eye_is(&camera,30,2,6);
    athena_rig3d_detach_owner(&id); assert(!athena_rig3d_loop_system());
    assert(athena_rig3d_attach_loop(0,NULL)>0); athena_loop_systems_clear(); assert(!athena_rig3d_loop_system());
    /* Releases: the camera callback runs once, nodes stay retained until then. */
    athena_node3d_release(hero); athena_scene3d_release(scene);
    athena_rig3d_retain(follow); athena_rig3d_release(follow); assert(!released);
    athena_rig3d_release(follow); assert(released==1);
    athena_rig3d_release(orbit); assert(released==1);
    assert(athena_rig3d_update_all(1)==0);
    puts("CameraRig3D host tests passed");
    return 0;
}
