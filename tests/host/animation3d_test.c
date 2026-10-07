/* Animation3D clips and players on the host: validation, sampling, looping,
 * ownership and the Loop system. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <athena/animation3d.h>
#include <athena/loop.h>
static void closef(float a,float b) { assert(fabsf(a-b)<0.0001f); }
static void position_of(AthenaNode3D *n,float x,float y,float z) {
    AthenaMatrix4 m; assert(!athena_node3d_local(n,&m));
    closef(m.value[12],x); closef(m.value[13],y); closef(m.value[14],z);
}
int main(void) {
    const float times[]={0,1,2},positions[]={0,0,0, 10,0,0, 10,20,0};
    AthenaTrack3DDesc track={.target=0,.path=ATHENA_ANIM3D_POSITION,.times=times,.values=positions,.key_count=3};
    AthenaClip3D *clip=NULL;
    /* Validation copies nothing on failure. */
    const float bad_times[]={0,1,1};
    AthenaTrack3DDesc bad=track; bad.times=bad_times;
    assert(athena_clip3d_create(&bad,1,&clip)==ATHENA_ANIM3D_EINVAL && !clip);
    bad=track; bad.key_count=0; assert(athena_clip3d_create(&bad,1,&clip)==ATHENA_ANIM3D_EINVAL);
    const float nan_values[]={0,0,0, NAN,0,0, 0,0,0};
    bad=track; bad.values=nan_values; assert(athena_clip3d_create(&bad,1,&clip)==ATHENA_ANIM3D_EINVAL);
    const float zero_quat[]={0,0,0,0};
    bad=(AthenaTrack3DDesc){.path=ATHENA_ANIM3D_ROTATION,.times=times,.values=zero_quat,.key_count=1};
    assert(athena_clip3d_create(&bad,1,&clip)==ATHENA_ANIM3D_EINVAL);
    bad=track; bad.path=(AthenaAnim3DPath)7; assert(athena_clip3d_create(&bad,1,&clip)==ATHENA_ANIM3D_EINVAL);
    assert(athena_clip3d_create(&track,0,&clip)==ATHENA_ANIM3D_EINVAL);
    /* Two targets: position on 0; rotation (with a flipped-sign key) and a
     * step-interpolated scale on 1. */
    const float rtimes[]={0,1},s=sqrtf(.5f);
    const float rotations[]={0,0,0,1, 0,-s,0,-s}; /* identity, then -(90 deg about y) */
    const float stimes[]={0,.5f},scales[]={1,1,1, 2,2,2};
    AthenaTrack3DDesc tracks[]={track,
        {.target=1,.path=ATHENA_ANIM3D_ROTATION,.times=rtimes,.values=rotations,.key_count=2},
        {.target=1,.path=ATHENA_ANIM3D_SCALE,.interpolation=ATHENA_ANIM3D_STEP,.times=stimes,.values=scales,.key_count=2}};
    assert(!athena_clip3d_create(tracks,3,&clip));
    closef(athena_clip3d_duration(clip),2); assert(athena_clip3d_target_count(clip)==2);
    AthenaNode3D *a=athena_node3d_create(),*b=athena_node3d_create();
    AthenaNode3D *nodes[]={a,b};
    assert(!athena_player3d_create(clip,nodes,1)); /* needs two nodes */
    AthenaPlayer3D *p=athena_player3d_create(clip,nodes,2); assert(p);
    athena_clip3d_release(clip); athena_node3d_release(b); /* the player keeps both */
    assert(!athena_player3d_playing(p) && athena_player3d_advance(p,1)==0);
    position_of(a,0,0,0); /* paused players do nothing */
    athena_player3d_play(p);
    assert(athena_player3d_advance(p,.5f)==0); position_of(a,5,0,0);
    /* Slerp at 0.5 between identity and 90 deg about y: 45 deg. The negated
     * key was moved to the same hemisphere, so this is the short arc. */
    AthenaMatrix4 m; assert(!athena_node3d_local(b,&m));
    closef(m.value[0],cosf((float)M_PI/4)*2); closef(m.value[8],sinf((float)M_PI/4)*2); /* step scale is 2 at 0.5 */
    assert(athena_player3d_advance(p,1)==0); position_of(a,10,10,0);
    /* Non-looping: stops at the end, returns 1 once. */
    assert(athena_player3d_advance(p,5)==1 && !athena_player3d_playing(p)); closef(athena_player3d_time(p),2);
    position_of(a,10,20,0);
    /* Seeking backwards uses the binary search, then forward again. */
    assert(!athena_player3d_set_time(p,.25f)); position_of(a,2.5f,0,0);
    assert(!athena_player3d_set_time(p,1.5f)); position_of(a,10,10,0);
    /* Looping wraps. */
    athena_player3d_set_loop(p,1); athena_player3d_play(p);
    assert(athena_player3d_advance(p,1)==0); closef(athena_player3d_time(p),.5f); position_of(a,5,0,0);
    /* Negative speed plays backwards and wraps; non-looping stops at 0. */
    assert(!athena_player3d_set_speed(p,-1)); assert(athena_player3d_advance(p,1)==0);
    closef(athena_player3d_time(p),1.5f);
    athena_player3d_set_loop(p,0); assert(athena_player3d_advance(p,2)==1); closef(athena_player3d_time(p),0);
    athena_player3d_stop(p); closef(athena_player3d_time(p),2); /* rewinds to the end when reversed */
    assert(athena_player3d_set_speed(p,NAN)==ATHENA_ANIM3D_EINVAL && athena_player3d_advance(p,-1)==ATHENA_ANIM3D_EINVAL);
    assert(athena_player3d_set_time(p,INFINITY)==ATHENA_ANIM3D_EINVAL);
    /* advance() covers every live player; the Loop system runs it. */
    assert(!athena_player3d_set_speed(p,1)); athena_player3d_stop(p); athena_player3d_play(p);
    assert(athena_animation3d_advance(1)==0); position_of(a,10,0,0);
    int id=athena_animation3d_attach_loop(ATHENA_ANIM3D_LOOP_PRIORITY,&id); assert(id>0);
    assert(athena_animation3d_attach_loop(0,NULL)==ATHENA_ANIM3D_EINVAL);
    assert(athena_loop_system_get(id)->priority==ATHENA_ANIM3D_LOOP_PRIORITY);
    assert(athena_loop_systems_run(ATHENA_LOOP_POST_UPDATE,1,1,NULL)==0);
    assert(!athena_player3d_playing(p)); position_of(a,10,20,0);
    athena_animation3d_detach_owner(NULL); assert(athena_animation3d_loop_system()==id);
    athena_animation3d_detach_owner(&id); assert(!athena_animation3d_loop_system() && !athena_loop_system_get(id));
    /* Reattach after clear: the Loop release resets the state. */
    assert(athena_animation3d_attach_loop(0,NULL)>0); athena_loop_systems_clear();
    assert(!athena_animation3d_loop_system() && athena_animation3d_attach_loop(0,NULL)>0);
    assert(athena_animation3d_detach_loop()==1 && athena_animation3d_detach_loop()==0);
    athena_player3d_release(p); /* releases the clip and both nodes */
    athena_node3d_release(a);
    assert(athena_animation3d_advance(1)==0);
    puts("Animation3D host tests passed");
    return 0;
}
