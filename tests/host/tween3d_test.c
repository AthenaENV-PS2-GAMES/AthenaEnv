/* Tween3D and the native easing curves on the host. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <athena/tween3d.h>
#include <athena/loop.h>
static void closef(float a,float b) { assert(fabsf(a-b)<0.0005f); }
static void position_is(AthenaNode3D *n,float x,float y,float z) {
    float p[3],s[3]; AthenaQuaternion q; athena_node3d_get_trs(n,p,&q,s);
    closef(p[0],x); closef(p[1],y); closef(p[2],z);
}
static int ends,completes;
static AthenaTween3D *to_kill;
static void on_end(void *opaque,int completed) {
    (void)opaque; ends++; completes+=completed;
    if(to_kill) { AthenaTween3D *k=to_kill; to_kill=NULL; athena_tween3d_kill(k,0); } /* reentrant kill */
}
static AthenaTween3DDesc move(float x,float y,float z,float duration) {
    AthenaTween3DDesc d={.channels=ATHENA_TWEEN3D_POSITION,.position={x,y,z},.duration=duration};
    return d;
}
/* Reference formulas of ease.js in double. */
static double ref_out_back(double t) { const double c=1.70158; t-=1; return 1+t*t*((c+1)*t+c); }
static double ref_bounce_out(double t) {
    const double n=7.5625,d=2.75;
    if(t<1/d) return n*t*t;
    if(t<2/d) { t-=1.5/d; return n*t*t+.75; }
    if(t<2.5/d) { t-=2.25/d; return n*t*t+.9375; }
    t-=2.625/d; return n*t*t+.984375;
}
int main(void) {
    /* Curves: endpoints, clamping, references and names. */
    for(int c=0;c<ATHENA_EASE_COUNT;c++) {
        closef(athena_ease(c,0),0); closef(athena_ease(c,1),1);
        closef(athena_ease(c,-5),0); closef(athena_ease(c,7),1);
        assert(athena_ease_find(athena_ease_name(c))==c);
    }
    for(double t=0;t<=1;t+=.05) {
        closef(athena_ease(ATHENA_EASE_OUT_BACK,(float)t),(float)ref_out_back(t));
        closef(athena_ease(ATHENA_EASE_OUT_BOUNCE,(float)t),(float)ref_bounce_out(t));
        closef(athena_ease(ATHENA_EASE_IN_OUT_QUAD,(float)t),(float)(t<.5?2*t*t:1-pow(-2*t+2,2)/2));
        closef(athena_ease(ATHENA_EASE_OUT_SINE,(float)t),(float)sin(t*M_PI/2));
    }
    assert(athena_ease(ATHENA_EASE_OUT_BACK,.8f)>1); /* overshoots */
    assert(athena_ease_find("easeOutBack")==ATHENA_EASE_OUT_BACK && athena_ease_find("outBack")==ATHENA_EASE_OUT_BACK);
    assert(athena_ease_find("easeInOutElastic")==ATHENA_EASE_IN_OUT_ELASTIC && athena_ease_find("linear")==0);
    assert(athena_ease_find("outback")==-1 && athena_ease_find("ease")==-1 && athena_ease_find("")==-1);
    assert(!strcmp(athena_ease_name(ATHENA_EASE_IN_OUT_CIRC),"inOutCirc"));

    AthenaNode3D *node=athena_node3d_create();
    /* Linear position over 1 s; the last frame is exact. */
    AthenaTween3DDesc d=move(10,0,-4,1); AthenaTween3D *t=NULL;
    assert(!athena_tween3d_create(ATHENA_TWEEN3D_NODE,node,NULL,&d,&t));
    athena_tween3d_set_end(t,on_end,NULL);
    assert(athena_tween3d_advance(.5f)==0); position_is(node,5,0,-2); closef(athena_tween3d_progress(t),.5f);
    assert(athena_tween3d_advance(.75f)==1 && ends==1 && completes==1); position_is(node,10,0,-4);
    assert(!athena_tween3d_active(t)); closef(athena_tween3d_progress(t),1); athena_tween3d_release(t);
    /* Validation. */
    d=move(0,0,0,1); d.channels|=ATHENA_TWEEN3D_LOOK;
    assert(athena_tween3d_create(ATHENA_TWEEN3D_NODE,node,NULL,&d,&t)==ATHENA_TWEEN3D_EINVAL && !t);
    d=move(NAN,0,0,1); assert(athena_tween3d_create(ATHENA_TWEEN3D_NODE,node,NULL,&d,&t)==ATHENA_TWEEN3D_EINVAL);
    d=move(0,0,0,-1); assert(athena_tween3d_create(ATHENA_TWEEN3D_NODE,node,NULL,&d,&t)==ATHENA_TWEEN3D_EINVAL);
    /* Delay: start values are read when the delay ends. */
    d=move(0,0,0,1); d.delay=.5f;
    assert(!athena_tween3d_create(ATHENA_TWEEN3D_NODE,node,NULL,&d,&t));
    assert(athena_tween3d_advance(.25f)==0); closef(athena_tween3d_progress(t),0);
    assert(!athena_node3d_set_position(node,4,0,0));
    assert(athena_tween3d_advance(.75f)==0); position_is(node,2,0,0); /* 0.5 s into the tween */
    athena_tween3d_kill(t,1); position_is(node,0,0,0); athena_tween3d_release(t);
    /* Yoyo with one repeat returns to the start; progress runs backwards. */
    d=move(8,0,0,1); d.yoyo=1; d.repeat=1;
    assert(!athena_tween3d_create(ATHENA_TWEEN3D_NODE,node,NULL,&d,&t));
    athena_tween3d_advance(1.25f); position_is(node,6,0,0);
    assert(athena_tween3d_advance(1)==1); position_is(node,0,0,0); athena_tween3d_release(t);
    /* Infinite repeat stays active through large steps; kill(complete). */
    d=move(1,0,0,.5f); d.repeat=-1;
    assert(!athena_tween3d_create(ATHENA_TWEEN3D_NODE,node,NULL,&d,&t));
    assert(athena_tween3d_advance(1000.25f)==0 && athena_tween3d_active(t)); position_is(node,.5f,0,0);
    athena_tween3d_pause(t,1); athena_tween3d_advance(.1f); position_is(node,.5f,0,0); athena_tween3d_pause(t,0);
    athena_tween3d_kill(t,1); position_is(node,1,0,0); assert(!athena_tween3d_active(t)); athena_tween3d_release(t);
    /* Overwrite ends the other tweens of the target, notified as killed. */
    ends=completes=0;
    d=move(5,0,0,1); AthenaTween3D *first=NULL,*second=NULL;
    assert(!athena_tween3d_create(ATHENA_TWEEN3D_NODE,node,NULL,&d,&first)); athena_tween3d_set_end(first,on_end,NULL);
    d=move(-5,0,0,1); d.overwrite=1;
    assert(!athena_tween3d_create(ATHENA_TWEEN3D_NODE,node,NULL,&d,&second));
    assert(athena_tween3d_count_target(node)==2);
    assert(athena_tween3d_advance(.5f)==1 && ends==1 && completes==0 && !athena_tween3d_active(first));
    assert(athena_tween3d_count_target(node)==1);
    /* Reentrant end callback: completing one kills another from inside. */
    athena_tween3d_set_end(second,on_end,NULL);
    AthenaNode3D *other=athena_node3d_create(); d=move(1,1,1,10);
    AthenaTween3D *victim=NULL; assert(!athena_tween3d_create(ATHENA_TWEEN3D_NODE,other,NULL,&d,&victim));
    athena_tween3d_set_end(victim,on_end,NULL); to_kill=victim;
    assert(athena_tween3d_advance(1)==1 && ends==3 && !athena_tween3d_active(victim));
    athena_tween3d_release(first); athena_tween3d_release(second); athena_tween3d_release(victim);
    /* Rotation: identity -> 90 deg about y, half way is 45 deg. */
    assert(!athena_node3d_set_rotation(node,0,0,0,1));
    d=(AthenaTween3DDesc){.channels=ATHENA_TWEEN3D_ROTATION,.rotation={0,(float)M_PI/2,0},.duration=1};
    assert(!athena_tween3d_create(ATHENA_TWEEN3D_NODE,node,NULL,&d,&t));
    athena_tween3d_advance(.5f);
    float p[3],s[3]; AthenaQuaternion q; athena_node3d_get_trs(node,p,&q,s);
    closef(q.y,sinf((float)M_PI/8)); closef(q.w,cosf((float)M_PI/8));
    athena_tween3d_advance(1); athena_node3d_get_trs(node,p,&q,s); closef(q.y,sinf((float)M_PI/4));
    athena_tween3d_release(t);
    /* Instance scale with an eased curve. */
    float xyz[]={0,0,0, 1,0,0, 0,1,0}; AthenaGeometry3D g={.positions=xyz,.vertex_count=3};
    AthenaMesh3D *mesh=NULL; assert(!athena_mesh3d_create(&g,&mesh));
    AthenaInstance3D *instance=athena_instance3d_create(mesh); athena_mesh3d_release(mesh);
    d=(AthenaTween3DDesc){.channels=ATHENA_TWEEN3D_SCALE,.scale={3,3,3},.duration=1,.ease=ATHENA_EASE_IN_QUAD};
    assert(!athena_tween3d_create(ATHENA_TWEEN3D_INSTANCE,instance,NULL,&d,&t));
    athena_instance3d_release(instance); /* the tween keeps it */
    athena_tween3d_advance(.5f);
    AthenaQuaternion iq; athena_instance3d_get_trs(instance,p,&iq,s); closef(s[0],1+2*.25f);
    athena_tween3d_advance(1); athena_instance3d_get_trs(instance,p,&iq,s); closef(s[2],3);
    athena_tween3d_release(t);
    /* Camera position; the look target is kept. */
    AthenaCamera3D camera; athena_camera3d_init(&camera);
    d=(AthenaTween3DDesc){.channels=ATHENA_TWEEN3D_POSITION,.position={0,0,15},.duration=1};
    assert(!athena_tween3d_create(ATHENA_TWEEN3D_CAMERA,&camera,NULL,&d,&t));
    athena_tween3d_advance(.5f); closef(camera.position.z,10); closef(camera.target.z,0);
    athena_tween3d_kill_target(&camera,1); closef(camera.position.z,15); athena_tween3d_release(t);
    /* Owner kill and the Loop system (PRE_UPDATE). */
    d=move(9,9,9,1); assert(!athena_tween3d_create(ATHENA_TWEEN3D_NODE,node,NULL,&d,&t));
    int owner; athena_tween3d_set_owner(t,&owner);
    athena_tween3d_kill_owner(&owner); assert(!athena_tween3d_active(t)); athena_tween3d_release(t);
    d=move(2,0,0,1); assert(!athena_tween3d_create(ATHENA_TWEEN3D_NODE,node,NULL,&d,&t));
    assert(!athena_node3d_set_position(node,0,0,0));
    int id=athena_tween3d_attach_loop(ATHENA_TWEEN3D_LOOP_PRIORITY,&owner); assert(id>0);
    assert(athena_loop_system_get(id)->phases==ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_PRE_UPDATE));
    assert(athena_loop_systems_run(ATHENA_LOOP_PRE_UPDATE,.5f,.5f,NULL)==0); position_is(node,1,0,0);
    athena_tween3d_detach_owner(&owner); assert(!athena_tween3d_loop_system());
    athena_tween3d_kill(t,0); athena_tween3d_release(t);
    athena_node3d_release(node); athena_node3d_release(other);
    assert(athena_tween3d_advance(1)==0);
    puts("Tween3D host tests passed");
    return 0;
}
