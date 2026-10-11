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
    /* A failing player (the lerp of huge opposite keys overflows) is paused
     * and reported; a healthy one still advances in the same call. */
    {
        const float t2[]={0,1},huge[]={3e38f,0,0,-3e38f,0,0},calm[]={0,0,0,2,0,0};
        AthenaTrack3DDesc ht={.target=0,.path=ATHENA_ANIM3D_POSITION,.times=t2,.values=huge,.key_count=2};
        AthenaTrack3DDesc ct=ht; ct.values=calm;
        AthenaClip3D *hc=NULL,*cc=NULL; assert(!athena_clip3d_create(&ht,1,&hc)&&!athena_clip3d_create(&ct,1,&cc));
        AthenaNode3D *hn=athena_node3d_create(),*cn=athena_node3d_create();
        /* Players run newest first: the failing one runs before the healthy one. */
        AthenaPlayer3D *cp=athena_player3d_create(cc,&cn,1),*hp=athena_player3d_create(hc,&hn,1);
        athena_clip3d_release(hc); athena_clip3d_release(cc);
        athena_player3d_play(hp); athena_player3d_play(cp);
        /* Single precision (EE, SSE) overflows in b - a; x87 excess precision
         * (i386) may not, and then nothing fails. Either way the healthy
         * player advances. */
        int code=athena_animation3d_advance(.5f);
        assert(code==ATHENA_ANIM3D_EINVAL||code==0);
        if(code) assert(!athena_player3d_playing(hp));
        assert(athena_player3d_playing(cp)); position_of(cn,1,0,0);
        if(code) { assert(athena_animation3d_advance(.25f)==0); position_of(cn,1.5f,0,0); } /* no repeated error */
        athena_player3d_release(hp); athena_player3d_release(cp); athena_node3d_release(hn); athena_node3d_release(cn);
    }
    /* CUBIC (glTF CUBICSPLINE): Hermite with per-second tangents. Keys 0 ->
     * 10 over 2 s, out tangent 3 at t=0, in tangent -1 at t=2: at u=0.25
     * (t=0.5) p = h00*0 + h10*2*3 + h01*10 + h11*2*(-1). */
    {
        const float times[]={0,2},values[]={0,0,0, 10,0,0},in[]={0,0,0, -1,0,0},out[]={3,0,0, 0,0,0};
        AthenaTrack3DDesc cubic={.target=0,.path=ATHENA_ANIM3D_POSITION,.interpolation=ATHENA_ANIM3D_CUBIC,
            .times=times,.values=values,.key_count=2,.in_tangents=in,.out_tangents=out};
        AthenaClip3D *clip=NULL; assert(!athena_clip3d_create(&cubic,1,&clip));
        AthenaNode3D *node=athena_node3d_create(); AthenaPlayer3D *player=athena_player3d_create(clip,&node,1);
        athena_clip3d_release(clip);
        float u=.25f,u2=u*u,u3=u2*u;
        float expected=(u3-2*u2+u)*2*3+(-2*u3+3*u2)*10+(u3-u2)*2*-1;
        assert(!athena_player3d_set_time(player,.5f)); position_of(node,expected,0,0);
        assert(!athena_player3d_set_time(player,2)); position_of(node,10,0,0);
        athena_player3d_release(player); athena_node3d_release(node);
        /* Missing tangents or non-finite ones are rejected. */
        cubic.in_tangents=NULL; assert(athena_clip3d_create(&cubic,1,&clip)==ATHENA_ANIM3D_EINVAL);
        const float bad[]={0,0,0, NAN,0,0}; cubic.in_tangents=bad;
        assert(athena_clip3d_create(&cubic,1,&clip)==ATHENA_ANIM3D_EINVAL);
        /* Rotations: keys keep their authored signs, the result is normalized. */
        const float qt[]={0,1},q[]={0,0,0,1, 0,0,.7071068f,.7071068f},zero[8]={0};
        AthenaTrack3DDesc rot={.target=0,.path=ATHENA_ANIM3D_ROTATION,.interpolation=ATHENA_ANIM3D_CUBIC,
            .times=qt,.values=q,.key_count=2,.in_tangents=zero,.out_tangents=zero};
        assert(!athena_clip3d_create(&rot,1,&clip));
        node=athena_node3d_create(); player=athena_player3d_create(clip,&node,1); athena_clip3d_release(clip);
        assert(!athena_player3d_set_time(player,.5f));
        float p[3],sc[3]; AthenaQuaternion r; athena_node3d_get_trs(node,p,&r,sc);
        closef(r.x*r.x+r.y*r.y+r.z*r.z+r.w*r.w,1); assert(r.z>0&&r.w>0);
        athena_player3d_release(player); athena_node3d_release(node);
    }
    puts("Animation3D host tests passed");
    return 0;
}
