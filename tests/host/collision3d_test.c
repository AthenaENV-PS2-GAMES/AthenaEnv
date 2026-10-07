/* collision3d: BVH queries and the character controller. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <athena/collision3d.h>
#include <athena/loop.h>
static void close_at(int line,float a,float b,float e) {
    if(fabsf(a-b)>e) { fprintf(stderr,"line %d: expected %f, got %f\n",line,b,a); assert(0); }
}
#define close_to(a,b,e) close_at(__LINE__,a,b,e)
/* A floor quad at y = 0 facing up, x and z in [-s, s]. */
static int add_floor(AthenaCollision3DWorld *w,float s,float y,uint32_t layer) {
    const float p[18]={-s,y,-s, -s,y,s, s,y,s,  -s,y,-s, s,y,s, s,y,-s};
    return athena_collision3d_add_triangles(w,p,2,NULL,layer);
}
/* A ramp rising along +x from x0 by angle degrees over length, z in [-5, 5]. */
static float add_ramp(AthenaCollision3DWorld *w,float x0,float length,float degrees) {
    float h=length*tanf(degrees*3.14159265f/180),x1=x0+length;
    const float p[18]={x0,0,-5, x0,0,5, x1,h,5,  x0,0,-5, x1,h,5, x1,h,-5};
    assert(athena_collision3d_add_triangles(w,p,2,NULL,1)>0);
    return h;
}
static AthenaCharacter3D *character_at(AthenaCollision3DWorld *w,float x,float y,float z) {
    AthenaCharacter3D *c=athena_character3d_create(w,NULL); assert(c);
    assert(!athena_character3d_set_position(c,x,y,z));
    return c;
}
static void run(AthenaCharacter3D *c,float vx,float vz,int frames) {
    for(int i=0;i<frames;i++) {
        float v[3]; athena_character3d_get_velocity(c,v);
        assert(!athena_character3d_set_velocity(c,vx,v[1],vz));
        assert(!athena_character3d_step(c,1.0f/60));
    }
}
int main(void) {
    AthenaCollision3DWorld *w=athena_collision3d_world_create(); assert(w);
    const float lo[3]={-20,-1,-20},hi[3]={20,0,20};
    int floor=athena_collision3d_add_box(w,lo,hi,1); assert(floor>0);
    assert(athena_collision3d_triangle_count(w)==12);
    /* Rays hit both sides, normals face the ray. */
    AthenaCollision3DHit hit;
    const float down[3]={0,-1,0},up[3]={0,2,0};
    float origin[3]={1,5,2};
    assert(athena_collision3d_raycast(w,origin,down,10,-1u,&hit)==1);
    close_to(hit.distance,5,1e-4f); close_to(hit.normal[1],1,1e-6f); close_to(hit.point[0],1,1e-5f);
    assert(hit.shape==floor);
    assert(athena_collision3d_raycast(w,origin,down,4,-1u,&hit)==0);       /* too short */
    assert(athena_collision3d_raycast(w,origin,down,10,2,&hit)==0);        /* masked out */
    origin[1]=-0.5f;
    assert(athena_collision3d_raycast(w,origin,up,10,-1u,&hit)==1);        /* inside: the top's back side */
    close_to(hit.distance,0.5f,1e-4f); close_to(hit.normal[1],-1,1e-6f);
    /* Sphere casts stop a radius above the surface. */
    const float center[3]={0,3,0};
    assert(athena_collision3d_sphere_cast(w,center,.5f,down,10,-1u,&hit)==1);
    close_to(hit.distance,2.5f,1e-3f); close_to(hit.normal[1],1,1e-3f); close_to(hit.point[1],0,1e-3f);
    /* Overlaps: shapes within the radius, once each. */
    int shapes[4];
    const float near[3]={0,.3f,0},far[3]={0,3,0};
    assert(athena_collision3d_overlap_sphere(w,near,.5f,-1u,shapes,4)==1 && shapes[0]==floor);
    assert(athena_collision3d_overlap_sphere(w,far,.5f,-1u,shapes,4)==0);
    assert(athena_collision3d_overlap_sphere(w,near,.5f,-1u,NULL,0)==1);
    assert(!athena_collision3d_set_layer(w,floor,4));
    assert(athena_collision3d_overlap_sphere(w,near,.5f,1,shapes,4)==0);
    assert(!athena_collision3d_set_layer(w,floor,1));
    /* Invalid input. */
    const float zero[3]={0,0,0},nan3[3]={NAN,0,0};
    assert(athena_collision3d_raycast(w,origin,zero,1,-1u,&hit)==ATHENA_COLLISION3D_EINVAL);
    assert(athena_collision3d_raycast(w,nan3,down,1,-1u,&hit)==ATHENA_COLLISION3D_EINVAL);
    assert(athena_collision3d_add_box(w,hi,lo,1)==ATHENA_COLLISION3D_EINVAL);
    assert(athena_collision3d_remove(w,999)==ATHENA_COLLISION3D_EINVAL);

    /* Falling: lands with the feet on the floor and stops. */
    AthenaCharacter3D *c=character_at(w,0,2,0);
    AthenaCharacter3DState state; float feet[3],v[3];
    run(c,0,0,120);
    athena_character3d_get_position(c,feet); athena_character3d_state(c,&state);
    close_to(feet[1],0,0.02f); assert(state.on_ground); close_to(state.ground_normal[1],1,1e-3f);
    athena_character3d_get_velocity(c,v); close_to(v[1],0,1e-6f);
    /* Standing still: stays put frame after frame. */
    run(c,0,0,60); athena_character3d_get_position(c,feet);
    close_to(feet[0],0,1e-4f); close_to(feet[1],0,0.02f);
    /* Walls: a box from x = 3 blocks at x = 3 - radius and the walk slides
     * along it in z. */
    const float wall_lo[3]={3,0,-20},wall_hi[3]={4,3,20};
    int wall=athena_collision3d_add_box(w,wall_lo,wall_hi,1); assert(wall>0);
    run(c,3,1,90);
    athena_character3d_get_position(c,feet); athena_character3d_state(c,&state);
    close_to(feet[0],3-.4f,0.02f); assert(feet[2]>1.4f); assert(state.hit_wall&&state.on_ground);
    assert(!athena_collision3d_remove(w,wall));
    /* Jumping: leaves the ground and lands again. */
    assert(!athena_character3d_set_velocity(c,0,4,0));
    assert(!athena_character3d_step(c,1.0f/60));
    athena_character3d_state(c,&state); assert(!state.on_ground);
    float top=0;
    for(int i=0;i<120;i++) { assert(!athena_character3d_step(c,1.0f/60)); athena_character3d_get_position(c,feet); if(feet[1]>top) top=feet[1]; }
    close_to(top,4*4/(2*9.81f),0.08f); athena_character3d_state(c,&state); assert(state.on_ground);
    /* Ceilings stop the jump. */
    const float roof_lo[3]={-2,2.4f,-2},roof_hi[3]={2,3,2};
    int roof=athena_collision3d_add_box(w,roof_lo,roof_hi,1);
    assert(!athena_character3d_set_position(c,0,0,0)); run(c,0,0,10);
    assert(!athena_character3d_set_velocity(c,0,6,0));
    int ceiling=0;
    for(int i=0;i<30;i++) {
        assert(!athena_character3d_step(c,1.0f/60)); athena_character3d_state(c,&state); ceiling|=state.hit_ceiling;
        athena_character3d_get_position(c,feet); assert(feet[1]+1.8f<=2.4f+0.02f);
    }
    assert(ceiling); athena_character3d_get_velocity(c,v); assert(v[1]<=0);
    assert(!athena_collision3d_remove(w,roof));
    athena_character3d_release(c);

    /* Steps: 0.2 is climbed, 0.5 blocks (step height 0.3). */
    const float step_lo[3]={-6,0,-5},step_hi[3]={-2,.2f,5};
    int low=athena_collision3d_add_box(w,step_lo,step_hi,1);
    c=character_at(w,0,0,0); run(c,0,0,10); run(c,-2,0,90);
    athena_character3d_get_position(c,feet); athena_character3d_state(c,&state);
    assert(feet[0]<-2.5f); close_to(feet[1],.2f,0.02f); assert(state.on_ground);
    /* Off the far edge: down the 0.2 step, still on the ground. */
    run(c,-2,0,120); athena_character3d_get_position(c,feet); athena_character3d_state(c,&state);
    assert(feet[0]<-6.5f); close_to(feet[1],0,0.02f); assert(state.on_ground);
    assert(!athena_collision3d_remove(w,low));
    const float high_hi[3]={-2,.5f,5};
    int high=athena_collision3d_add_box(w,step_lo,high_hi,1);
    assert(!athena_character3d_set_position(c,0,0,0)); run(c,0,0,10); run(c,-2,0,90);
    athena_character3d_get_position(c,feet); athena_character3d_state(c,&state);
    /* The round bottom meets the 0.5 edge 0.358 from the axis. */
    close_to(feet[0],-2+.358f,0.02f); close_to(feet[1],0,0.02f); assert(state.hit_wall);
    assert(!athena_collision3d_remove(w,high));
    athena_character3d_release(c);

    /* Slopes: 30 degrees is walked up (and down without leaving the ground),
     * 60 degrees blocks like a wall. */
    float h=add_ramp(w,1,4,30);
    c=character_at(w,0,0,0); run(c,0,0,10);
    for(int i=0;i<90;i++) { run(c,2,0,1); athena_character3d_state(c,&state); assert(state.on_ground); }
    athena_character3d_get_position(c,feet);
    assert(feet[0]>2.5f&&feet[1]>0.5f&&feet[1]<h);
    close_to(feet[1],(feet[0]-1)*tanf(30*3.14159265f/180),0.08f);
    for(int i=0;i<40;i++) { run(c,-2,0,1); athena_character3d_state(c,&state); assert(state.on_ground); }
    athena_character3d_release(c);
    AthenaCollision3DWorld *steep=athena_collision3d_world_create();
    add_floor(steep,20,0,1); add_ramp(steep,1,4,60);
    c=character_at(steep,0,0,0); run(c,0,0,10); run(c,2,0,120);
    athena_character3d_get_position(c,feet); athena_character3d_state(c,&state);
    assert(feet[0]<1.6f&&feet[1]<0.3f); assert(state.hit_wall);
    /* Standing on a walkable slope does not slide down it. */
    athena_character3d_release(c);
    c=character_at(w,3,3,0); run(c,0,0,120);
    athena_character3d_get_position(c,feet); float x=feet[0];
    run(c,0,0,120); athena_character3d_get_position(c,feet);
    close_to(feet[0],x,0.01f); athena_character3d_state(c,&state); assert(state.on_ground);
    athena_character3d_release(c);
    /* No tunneling: a fast fall onto a one-sided floor quad. */
    c=character_at(steep,-10,30,0);
    assert(!athena_character3d_set_velocity(c,0,-200,0));
    for(int i=0;i<60;i++) assert(!athena_character3d_step(c,1.0f/60));
    athena_character3d_get_position(c,feet); close_to(feet[1],0,0.02f);
    /* Nodes follow the feet; move() collides without gravity. */
    AthenaNode3D *node=athena_node3d_create();
    assert(!athena_character3d_bind(c,node));
    assert(!athena_character3d_move(c,1,-1,0));
    float p[3],s[3]; AthenaQuaternion q; athena_node3d_get_trs(node,p,&q,s);
    athena_character3d_get_position(c,feet);
    close_to(p[0],-9,1e-4f); close_to(p[1],feet[1],1e-6f); close_to(feet[1],0,0.02f);
    /* The Loop system steps every enabled character. */
    athena_character3d_set_enabled(c,0);
    assert(!athena_character3d_step_all(1));
    athena_character3d_get_position(c,feet); close_to(feet[0],-9,1e-4f);
    assert(athena_character3d_attach_loop(ATHENA_CHARACTER3D_LOOP_PRIORITY,w)>0);
    assert(athena_character3d_attach_loop(0,w)==ATHENA_COLLISION3D_EINVAL);
    athena_character3d_detach_owner(w); assert(!athena_character3d_loop_system());
    athena_node3d_release(node);
    athena_character3d_release(c);
    athena_collision3d_world_release(steep);

    /* Meshes and node subtrees, with composed transforms. */
    const float quad[18]={-1,0,-1, -1,0,1, 1,0,1,  -1,0,-1, 1,0,1, 1,0,-1};
    AthenaGeometry3D g={.positions=quad,.vertex_count=6};
    AthenaMesh3D *mesh=NULL; assert(!athena_mesh3d_create(&g,&mesh));
    AthenaNode3D *parent=athena_node3d_create(),*child=athena_node3d_create();
    assert(!athena_node3d_set_position(parent,10,5,0) && !athena_node3d_set_scale(child,2,1,2));
    assert(!athena_node3d_set_mesh(child,mesh) && !athena_node3d_add_child(parent,child));
    AthenaCollision3DWorld *level=athena_collision3d_world_create();
    int platform=athena_collision3d_add_node(level,child,8); assert(platform>0);
    assert(athena_collision3d_triangle_count(level)==2);
    const float above[3]={11.5f,9,1.5f};
    assert(athena_collision3d_raycast(level,above,down,10,8,&hit)==1);
    close_to(hit.distance,4,1e-4f); assert(hit.shape==platform);
    const float outside[3]={12.5f,9,0};
    assert(athena_collision3d_raycast(level,outside,down,10,8,&hit)==0);
    AthenaMatrix4 shift; ath_matrix4_identity(&shift); shift.value[13]=-3;
    assert(athena_collision3d_add_mesh(level,mesh,&shift,8)>0);
    const float sky[3]={0,1,0};
    assert(athena_collision3d_raycast(level,sky,down,10,8,&hit)==1); close_to(hit.distance,4,1e-4f);
    athena_node3d_release(child); athena_node3d_release(parent); athena_mesh3d_release(mesh);
    /* Compact indexed storage must give the same collision triangles for
     * mesh and subtree insertion; preserve source triangle IDs across batches. */
    {
        const float vertices[]={-1,0,-1,-1,0,1,1,0,1,1,0,-1};
        uint32_t indices[96];for(unsigned i=0;i<96;i++)indices[i]=(uint32_t[]){0,1,2,0,2,3}[i%6];
        AthenaGeometry3D indexed={.positions=vertices,.vertex_count=4,.indices=indices,.index_count=96};
        AthenaMesh3D *m=NULL;assert(!athena_mesh3d_create(&indexed,&m));
        AthenaMesh3DView v;athena_mesh3d_view(m,&v);assert(v.indices&&v.stream_vertex_count<v.vertex_count);
        AthenaCollision3DWorld *cw=athena_collision3d_world_create();
        AthenaMatrix4 tr;ath_matrix4_identity(&tr);tr.value[13]=2;
        int id=athena_collision3d_add_mesh(cw,m,&tr,1);assert(id>0);
        assert(athena_collision3d_triangle_count(cw)==32);
        float origin[]={.75f,5,-.75f};assert(athena_collision3d_raycast(cw,origin,down,10,1,&hit)==1);
        close_to(hit.distance,3,1e-4f);assert(hit.shape==id&&hit.triangle%2==1);
        AthenaNode3D *nd=athena_node3d_create();assert(!athena_node3d_set_mesh(nd,m));
        assert(!athena_node3d_set_position(nd,0,2,0));
        assert(athena_collision3d_add_node(cw,nd,2)>0);assert(athena_collision3d_triangle_count(cw)==64);
        athena_node3d_release(nd);athena_mesh3d_release(m);
        assert(athena_collision3d_raycast(cw,origin,down,10,2,&hit)==1);close_to(hit.distance,3,1e-4f);
        athena_collision3d_world_release(cw);
    }
    /* Many triangles: the tree answers the same as brute force. */
    for(int i=0;i<200;i++) {
        float y=(float)(i%10),x=(float)(i/10)*3;
        const float t[9]={x,y,0, x+1,y,1, x+1,y,0};
        athena_collision3d_add_triangles(level,t,1,NULL,16);
    }
    const float probe_origin[3]={30.8f,20,0.2f};
    assert(athena_collision3d_raycast(level,probe_origin,down,30,16,&hit)==1);
    close_to(hit.distance,11,1e-4f);
    athena_collision3d_world_release(level);
    athena_collision3d_world_release(w);
    puts("Collision3D host tests passed");
    return 0;
}
