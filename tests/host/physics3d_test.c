/* physics3d: rigid spheres and boxes against the level and each other. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <athena/physics3d.h>
#include <athena/loop.h>
static void close_at(int line,float a,float b,float e) {
    if(fabsf(a-b)>e) { fprintf(stderr,"line %d: expected %f, got %f\n",line,b,a); assert(0); }
}
#define close_to(a,b,e) close_at(__LINE__,a,b,e)
static AthenaBody3D *body(AthenaPhysics3DWorld *w,AthenaShape3DType shape,float x,float y,float z) {
    AthenaBody3DDesc d; athena_body3d_desc_default(&d);
    d.shape=shape; d.position[0]=x; d.position[1]=y; d.position[2]=z;
    AthenaBody3D *b=athena_body3d_create(w,&d); assert(b); return b;
}
static void run(AthenaPhysics3DWorld *w,float seconds) {
    for(int i=0;i<(int)(seconds*60+.5f);i++) assert(athena_physics3d_step(w,1.0f/60)>=0);
}
int main(void) {
    AthenaCollision3DWorld *level=athena_collision3d_world_create();
    const float lo[3]={-20,-1,-20},hi[3]={20,0,20};
    assert(athena_collision3d_add_box(level,lo,hi,1)>0);
    AthenaPhysics3DWorld *w=athena_physics3d_world_create(level); assert(w);
    float p[3],v[3],q[4];
    /* Free fall: y = y0 - g t^2 / 2 (no contact yet). */
    AthenaBody3D *ball=body(w,ATHENA_SHAPE3D_SPHERE,0,20,0);
    run(w,1);
    athena_body3d_get_position(ball,p); close_to(p[1],20-9.81f/2,0.25f);
    /* Rests on the level, then sleeps. */
    run(w,4);
    athena_body3d_get_position(ball,p); close_to(p[1],.5f,0.02f);
    athena_body3d_get_velocity(ball,v); close_to(v[1],0,1e-6f);
    assert(athena_body3d_sleeping(ball));
    /* A box lands flat and settles without spinning or drifting. */
    AthenaBody3D *box=body(w,ATHENA_SHAPE3D_BOX,3,2,0);
    run(w,3);
    athena_body3d_get_position(box,p); athena_body3d_get_rotation(box,q);
    close_to(p[1],.5f,0.02f); close_to(p[0],3,0.02f); close_to(fabsf(q[3]),1,1e-3f);
    /* A tilted box falls onto an edge and ends up on a face. */
    AthenaBody3D *tilted=body(w,ATHENA_SHAPE3D_BOX,-3,2,0);
    assert(!athena_body3d_set_rotation(tilted,0,0,sinf(.3f),cosf(.3f)));
    run(w,4);
    athena_body3d_get_position(tilted,p); close_to(p[1],.5f,0.03f);
    float up[3]; athena_body3d_get_rotation(tilted,q);
    /* Rotated +Y: one of the box axes is vertical again. */
    float x=q[0],y=q[1],z=q[2],s=q[3];
    up[0]=2*(x*y-s*z); up[1]=1-2*(x*x+z*z); up[2]=2*(y*z+s*x);
    float r0=2*(x*y+s*z); /* y component of the rotated X axis */
    assert(fabsf(up[1])>0.99f||fabsf(r0)>0.99f);
    /* A stack of three boxes stays standing. */
    AthenaBody3D *stack[3];
    for(int i=0;i<3;i++) stack[i]=body(w,ATHENA_SHAPE3D_BOX,8,.5f+i*1.01f,0);
    run(w,5);
    for(int i=0;i<3;i++) {
        athena_body3d_get_position(stack[i],p);
        close_to(p[0],8,0.05f); close_to(p[1],.5f+i,0.05f);
    }
    /* Spheres on boxes and on spheres. */
    AthenaBody3D *on_box=body(w,ATHENA_SHAPE3D_SPHERE,3,4,0);
    run(w,3); athena_body3d_get_position(on_box,p); close_to(p[1],1.5f,0.03f);
    /* Restitution: a bouncy ball comes back up. */
    AthenaBody3DDesc d; athena_body3d_desc_default(&d);
    d.restitution=.8f; d.position[0]=-8; d.position[1]=5;
    AthenaBody3D *bouncy=athena_body3d_create(w,&d); assert(bouncy);
    float lowest=10,after=0; int bounced=0;
    for(int i=0;i<120;i++) {
        athena_physics3d_step(w,1.0f/60); athena_body3d_get_position(bouncy,p); athena_body3d_get_velocity(bouncy,v);
        if(p[1]<lowest) lowest=p[1];
        if(v[1]>0) { bounced=1; if(p[1]>after) after=p[1]; }
    }
    assert(bounced&&after>2.5f);
    /* Friction: a box pushed along the floor stops; a ball rolls. */
    float push[3]={5,0,0};
    athena_body3d_get_position(box,p); float start=p[0];
    assert(!athena_body3d_apply_impulse(box,push,NULL));
    assert(!athena_body3d_sleeping(box));
    run(w,3);
    athena_body3d_get_position(box,p); athena_body3d_get_velocity(box,v);
    assert(p[0]>start+1&&p[0]<start+5); close_to(v[0],0,0.01f);
    /* Kinematic bodies push dynamic ones and are not pushed back. */
    d.type=ATHENA_BODY3D_KINEMATIC; d.shape=ATHENA_SHAPE3D_BOX; d.restitution=0;
    d.position[0]=-14; d.position[1]=.6f; d.position[2]=6; d.velocity[0]=2;
    AthenaBody3D *pusher=athena_body3d_create(w,&d); assert(pusher);
    AthenaBody3D *pushed=body(w,ATHENA_SHAPE3D_SPHERE,-12,.5f,6);
    run(w,2);
    athena_body3d_get_position(pushed,p); assert(p[0]>-10.5f);
    athena_body3d_get_velocity(pusher,v); close_to(v[0],2,1e-6f);
    /* Nodes follow position and rotation. */
    AthenaNode3D *node=athena_node3d_create();
    assert(!athena_body3d_bind(tilted,node));
    float np[3],ns[3]; AthenaQuaternion nq; athena_node3d_get_trs(node,np,&nq,ns);
    athena_body3d_get_position(tilted,p); athena_body3d_get_rotation(tilted,q);
    close_to(np[0],p[0],1e-6f); close_to(nq.w,q[3],1e-5f);
    /* Masks and removal. */
    assert(athena_physics3d_body_count(w)==10);
    athena_body3d_remove(on_box); assert(!athena_body3d_alive(on_box));
    athena_body3d_remove(on_box);
    assert(athena_physics3d_body_count(w)==9);
    /* Invalid descriptions. */
    athena_body3d_desc_default(&d); d.mass=0; assert(!athena_body3d_create(w,&d));
    athena_body3d_desc_default(&d); d.radius=-1; assert(!athena_body3d_create(w,&d));
    athena_body3d_desc_default(&d); d.restitution=2; assert(!athena_body3d_create(w,&d));
    assert(athena_physics3d_step(w,-1)==ATHENA_PHYSICS3D_EINVAL);
    /* No tunnelling through the floor at a high (but bounded) speed. */
    AthenaBody3D *fast=body(w,ATHENA_SHAPE3D_SPHERE,15,10,15);
    assert(!athena_body3d_set_velocity(fast,0,-25,0));
    run(w,2); athena_body3d_get_position(fast,p); assert(p[1]>0.3f);
    /* The Loop system steps the world. */
    assert(athena_physics3d_attach_loop(w,ATHENA_PHYSICS3D_LOOP_PRIORITY,level)>0);
    assert(athena_physics3d_attach_loop(w,0,level)==ATHENA_PHYSICS3D_EINVAL);
    athena_physics3d_detach_owner(level); assert(!athena_physics3d_loop_system(w));
    athena_node3d_release(node);
    AthenaBody3D *all[]={ball,box,tilted,stack[0],stack[1],stack[2],on_box,bouncy,pusher,pushed,fast};
    for(unsigned i=0;i<sizeof(all)/sizeof(all[0]);i++) athena_body3d_release(all[i]);
    athena_physics3d_world_release(w);
    /* Capsules (radius .3, half height .5), on a fresh level. */
    AthenaPhysics3DWorld *cw=athena_physics3d_world_create(level); assert(cw);
    AthenaBody3DDesc cd; athena_body3d_desc_default(&cd);
    cd.shape=ATHENA_SHAPE3D_CAPSULE; cd.radius=.3f; cd.half_height=.5f;
    cd.position[1]=3; AthenaBody3D *upright=athena_body3d_create(cw,&cd); assert(upright);
    cd.position[0]=4; cd.position[1]=2; cd.rotation[2]=sinf(.7853982f); cd.rotation[3]=cosf(.7853982f);
    AthenaBody3D *lying=athena_body3d_create(cw,&cd); assert(lying);
    run(cw,4);
    athena_body3d_get_position(upright,p); close_to(p[1],.8f,0.02f); close_to(p[0],0,0.02f);
    athena_body3d_get_position(lying,p); close_to(p[1],.3f,0.02f); close_to(p[0],4,0.05f);
    athena_body3d_get_rotation(lying,q); close_to(fabsf(q[2]),sinf(.7853982f),0.02f); /* still lying */
    assert(athena_body3d_sleeping(upright)&&athena_body3d_sleeping(lying));
    /* Capsule on capsule (crossed), on a box, a sphere on a capsule. */
    cd.position[0]=4; cd.position[1]=1.5f; cd.position[2]=0;
    cd.rotation[0]=sinf(.7853982f); cd.rotation[2]=0; cd.rotation[3]=cosf(.7853982f); /* along z */
    AthenaBody3D *crossed=athena_body3d_create(cw,&cd); assert(crossed);
    AthenaBody3DDesc bd; athena_body3d_desc_default(&bd);
    bd.shape=ATHENA_SHAPE3D_BOX; bd.type=ATHENA_BODY3D_STATIC; bd.position[0]=-4; bd.position[1]=.5f;
    AthenaBody3D *table=athena_body3d_create(cw,&bd); assert(table);
    cd.position[0]=-4; cd.position[1]=2; cd.rotation[0]=0; cd.rotation[2]=sinf(.7853982f);
    AthenaBody3D *on_table=athena_body3d_create(cw,&cd); assert(on_table);
    AthenaBody3D *ball_on_top=body(cw,ATHENA_SHAPE3D_SPHERE,0,3,0);
    run(cw,4);
    athena_body3d_get_position(crossed,p); close_to(p[1],.9f,0.03f); close_to(p[0],4,0.05f);
    athena_body3d_get_position(on_table,p); close_to(p[1],1.3f,0.03f);
    athena_body3d_get_position(ball_on_top,p); close_to(p[1],1.6f+.5f,0.03f); /* capsule top 1.6 + radius */
    /* Parallel capsules: the upper one rests on two points and stays parallel. */
    cd.position[0]=8; cd.position[1]=.3f; cd.rotation[0]=0; cd.rotation[2]=sinf(.7853982f); cd.rotation[3]=cosf(.7853982f);
    AthenaBody3D *bottom=athena_body3d_create(cw,&cd);
    cd.position[1]=1.2f; AthenaBody3D *top=athena_body3d_create(cw,&cd);
    assert(bottom&&top);
    run(cw,2);
    athena_body3d_get_position(top,p); athena_body3d_get_rotation(top,q);
    assert(p[1]<.31f||fabsf(p[1]-.9f)<.03f); /* rolled off to the floor, or resting on top */
    close_to(fabsf(q[2]),sinf(.7853982f),0.03f);
    /* Inertia: about the axis smaller than across it. */
    cd.mass=2; cd.position[0]=20; cd.position[1]=10; cd.rotation[0]=cd.rotation[2]=0; cd.rotation[3]=1;
    AthenaBody3D *spinner=athena_body3d_create(cw,&cd); assert(spinner);
    const float twist[3]={.1f,0,0},twist_point[3]={20,10.8f,0};
    assert(!athena_body3d_apply_impulse(spinner,twist,twist_point));
    athena_body3d_get_angular_velocity(spinner,v); assert(fabsf(v[2])>0);
    cd.radius=0; assert(!athena_body3d_create(cw,&cd));
    cd.radius=.3f; cd.half_height=0; assert(!athena_body3d_create(cw,&cd));
    AthenaBody3D *capsules[]={upright,lying,crossed,table,on_table,ball_on_top,bottom,top,spinner};
    for(unsigned i=0;i<9;i++) athena_body3d_release(capsules[i]);
    athena_physics3d_world_release(cw);
    /* Joints, in a world without a level. */
    AthenaPhysics3DWorld *jw=athena_physics3d_world_create(NULL); assert(jw);
    /* Pendulum: a ball joint to the world keeps the length and swings down. */
    AthenaBody3D *bob=body(jw,ATHENA_SHAPE3D_SPHERE,1,5,0);
    const float pivot[3]={0,5,0};
    AthenaJoint3D *pin=athena_joint3d_ball(jw,bob,NULL,pivot); assert(pin);
    float lowest_y=5;
    for(int i=0;i<120;i++) {
        assert(athena_physics3d_step(jw,1.0f/60)>=0);
        athena_body3d_get_position(bob,p);
        float d=sqrtf((p[0]-pivot[0])*(p[0]-pivot[0])+(p[1]-pivot[1])*(p[1]-pivot[1])+(p[2]-pivot[2])*(p[2]-pivot[2]));
        close_to(d,1,0.03f);
        if(p[1]<lowest_y) lowest_y=p[1];
    }
    assert(lowest_y<4.1f);
    /* Hinge: a door about +y at its edge turns, does not fall, and keeps
     * its axis; limits and the motor act on the angle. */
    AthenaBody3DDesc dd; athena_body3d_desc_default(&dd);
    dd.shape=ATHENA_SHAPE3D_BOX; dd.half[0]=.5f; dd.half[1]=1; dd.half[2]=.05f;
    dd.position[0]=10.5f; dd.position[1]=1; dd.linear_damping=dd.angular_damping=0;
    AthenaBody3D *door=athena_body3d_create(jw,&dd); assert(door);
    const float hinge_point[3]={10,1,0},hinge_up[3]={0,1,0};
    AthenaJoint3D *hinge=athena_joint3d_hinge(jw,door,NULL,hinge_point,hinge_up); assert(hinge);
    const float kick[3]={0,0,-.5f},edge[3]={11,1,0};
    assert(!athena_body3d_apply_impulse(door,kick,edge));
    run(jw,1);
    athena_body3d_get_position(door,p); athena_body3d_get_rotation(door,q);
    close_to(p[1],1,0.02f); close_to(q[0],0,0.01f); close_to(q[2],0,0.01f);
    float angle=athena_joint3d_angle(hinge); assert(fabsf(angle)>.2f);
    /* The anchor stays on the hinge line: x, z of the edge stay at distance .5. */
    close_to(sqrtf((p[0]-10)*(p[0]-10)+p[2]*p[2]),.5f,0.02f);
    assert(!athena_joint3d_set_limits(hinge,-.3f,.3f));
    assert(athena_joint3d_set_limits(hinge,1,0)==ATHENA_PHYSICS3D_EINVAL);
    assert(!athena_body3d_set_angular_velocity(door,0,3,0));
    run(jw,2);
    angle=athena_joint3d_angle(hinge); assert(angle<.33f&&angle>-.33f);
    athena_joint3d_disable_limits(hinge);
    assert(!athena_joint3d_set_motor(hinge,1,100));
    run(jw,1);
    athena_body3d_get_angular_velocity(door,v); close_to(v[1],1,0.05f);
    assert(!athena_joint3d_set_motor(hinge,0,0));
    /* Rope: falls until taut at 3, slack above. Rigid distance keeps 2. */
    AthenaBody3D *weight=body(jw,ATHENA_SHAPE3D_SPHERE,20,4,0);
    const float hook[3]={20,5,0},weight_at[3]={20,4,0};
    AthenaJoint3D *rope=athena_joint3d_distance(jw,weight,NULL,weight_at,hook,3,1); assert(rope);
    athena_body3d_get_position(weight,p); assert(p[1]<4.01f); /* slack: no pull yet */
    run(jw,2);
    athena_body3d_get_position(weight,p); close_to(p[1],2,0.05f); close_to(p[0],20,0.05f);
    AthenaBody3D *rod_end=body(jw,ATHENA_SHAPE3D_SPHERE,30,3,0);
    const float rod_anchor[3]={30,3,0},rod_hook[3]={30,5,0};
    AthenaJoint3D *rod=athena_joint3d_distance(jw,rod_end,NULL,rod_anchor,rod_hook,-1,0); assert(rod);
    assert(!athena_body3d_set_velocity(rod_end,0,5,0)); /* pushed toward the hook */
    for(int i=0;i<60;i++) {
        assert(athena_physics3d_step(jw,1.0f/60)>=0);
        athena_body3d_get_position(rod_end,p);
        close_to(sqrtf((p[0]-30)*(p[0]-30)+(p[1]-5)*(p[1]-5)+p[2]*p[2]),2,0.05f);
    }
    /* Weld: two boxes stay rigidly together while they fall and spin. */
    AthenaBody3D *left=body(jw,ATHENA_SHAPE3D_BOX,40,5,0),*right=body(jw,ATHENA_SHAPE3D_BOX,41,5,0);
    const float seam[3]={40.5f,5,0};
    AthenaJoint3D *weld=athena_joint3d_weld(jw,left,right,seam); assert(weld);
    assert(!athena_body3d_set_angular_velocity(left,0,0,2));
    run(jw,1);
    float pl[3],pr[3],ql[4],qr[4];
    athena_body3d_get_position(left,pl); athena_body3d_get_position(right,pr);
    athena_body3d_get_rotation(left,ql); athena_body3d_get_rotation(right,qr);
    close_to(sqrtf((pl[0]-pr[0])*(pl[0]-pr[0])+(pl[1]-pr[1])*(pl[1]-pr[1])+(pl[2]-pr[2])*(pl[2]-pr[2])),1,0.03f);
    close_to(fabsf(ql[0]*qr[0]+ql[1]*qr[1]+ql[2]*qr[2]+ql[3]*qr[3]),1,0.003f);
    assert(pl[1]<4); /* falling together */
    /* Weld with a rest rotation (b turned 90 degrees about z), spinning as
     * a whole: the relative rotation must stay 90 degrees. */
    AthenaBody3D *hub=body(jw,ATHENA_SHAPE3D_BOX,50,5,0),*arm=body(jw,ATHENA_SHAPE3D_BOX,50,5,0);
    assert(!athena_body3d_set_rotation(arm,0,0,sinf(.7853982f),cosf(.7853982f)));
    const float hub_center[3]={50,5,0};
    AthenaJoint3D *cross=athena_joint3d_weld(jw,arm,hub,hub_center); assert(cross);
    assert(!athena_body3d_set_angular_velocity(hub,3,0,0)&&!athena_body3d_set_angular_velocity(arm,3,0,0)); /* about x, not the rest axis */
    run(jw,2);
    athena_body3d_get_rotation(hub,ql); athena_body3d_get_rotation(arm,qr);
    {   /* conj(hub) * arm, in the hub's frame, should stay 90 degrees about z. */
        float x=ql[3]*qr[0]-ql[0]*qr[3]-ql[1]*qr[2]+ql[2]*qr[1],y=ql[3]*qr[1]+ql[0]*qr[2]-ql[1]*qr[3]-ql[2]*qr[0];
        float z=ql[3]*qr[2]-ql[0]*qr[1]+ql[1]*qr[0]-ql[2]*qr[3],w2=ql[3]*qr[3]+ql[0]*qr[0]+ql[1]*qr[1]+ql[2]*qr[2];
        close_to(fabsf(z),sinf(.7853982f),0.02f); close_to(fabsf(w2),cosf(.7853982f),0.02f);
        close_to(x,0,0.02f); close_to(y,0,0.02f);
    }
    assert(!athena_physics3d_contact_count(jw)); /* overlapping, but joined: no contacts */
    athena_joint3d_release(cross); athena_body3d_release(hub); athena_body3d_release(arm);
    /* Validation and removal with a body. */
    assert(!athena_joint3d_ball(jw,bob,bob,pivot));
    assert(!athena_joint3d_hinge(jw,door,NULL,hinge_point,(float[3]){0,0,0}));
    assert(athena_physics3d_joint_count(jw)==6);
    athena_body3d_remove(right); assert(!athena_joint3d_alive(weld));
    assert(athena_physics3d_joint_count(jw)==5);
    athena_joint3d_remove(rope); assert(athena_physics3d_joint_count(jw)==4);
    AthenaJoint3D *joints[]={pin,hinge,rope,rod,weld};
    for(unsigned i=0;i<5;i++) athena_joint3d_release(joints[i]);
    AthenaBody3D *joint_bodies[]={bob,door,weight,rod_end,left,right};
    for(unsigned i=0;i<6;i++) athena_body3d_release(joint_bodies[i]);
    athena_physics3d_world_release(jw);
    athena_collision3d_world_release(level);
    puts("Physics3D host tests passed");
    return 0;
}
