/* Scene3D x Batch cost profile with identical content. Launch from bin/ with
 * models/lit_cube.glb. Build with screen,loop,scene3d,draw,usbmass.
 * First, per-call timings of the math used by every draw/update. Then seven
 * 180-frame stages over the same 8x8 grid of 64 cubes (60 warm-up frames):
 * batch/scene, unlit/diffuse, static/animated. The grid stays fully inside
 * the frustum in stages 0-5, so no object takes the C clipping path; stage 6
 * repeats 3d_scene "many-nodes" for reference, clipping included; stage 7
 * spins the stage 4 cubes natively (set once, athena_scene3d_advance());
 * stage 8 plays an Animation3D rotation clip per cube (athena_animation3d_advance()).
 * Update ticks include the transform setters; draw ticks exclude HUD/flip. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <athena.h>
#include <athena/screen.h>
#include <athena/scene3d.h>
#include <athena/animation3d.h>
#include <athena/render3d.h>
#include <athena/draw.h>

#define GRID 8
#define COUNT (GRID*GRID)
#define FRAMES 180
#define WARMUP 60
#define CALLS 2000
static const char *names[]={"batch-unlit-static","batch-lit-static","scene-lit-static",
    "batch-lit-animated","scene-lit-animated","scene-lit-rows","scene-many-nodes",
    "scene-lit-spin","scene-lit-clip"};
typedef struct {
    AthenaBatch3D *batch;
    AthenaInstance3D *items[COUNT];
    AthenaScene3D *scene;
    AthenaNode3D *system,*rows[GRID],*cubes[COUNT];
    AthenaPlayer3D *players[COUNT];
} Grid;
static void release(Grid *g) {
    athena_batch3d_destroy(g->batch);
    for(unsigned i=0;i<COUNT;i++) {
        athena_instance3d_release(g->items[i]); athena_node3d_release(g->cubes[i]); athena_player3d_release(g->players[i]);
    }
    for(unsigned i=0;i<GRID;i++) athena_node3d_release(g->rows[i]);
    athena_node3d_release(g->system); athena_scene3d_release(g->scene);
    memset(g,0,sizeof(*g));
}
static float cube_x(unsigned i) { return ((int)(i%GRID)-3.5f)*.9f; }
static float row_y(unsigned r) { return ((int)r-3.5f)*.7f; }
static AthenaNode3D *child(AthenaNode3D *parent,AthenaMesh3D *mesh,int *code) {
    AthenaNode3D *n=athena_node3d_create();
    if(!n) { *code=ATHENA_SCENE3D_ENOMEM; return NULL; }
    if(*code==0) *code=athena_node3d_set_mesh(n,mesh);
    if(*code==0) *code=athena_node3d_add_child(parent,n);
    return n;
}
/* Rotation keys every 0.25 s over 3 s, matching the stage 4 Euler rates. */
static AthenaClip3D *rotation_clip(void) {
    enum { KEYS=13 };
    float times[KEYS],values[KEYS*4];
    for(unsigned k=0;k<KEYS;k++) {
        float t=k*.25f,angle=t*2*3.14159265358979323846f/3; AthenaQuaternion q;
        if(!athena_quaternion_euler(&q,angle,angle*.5f,0)) return NULL;
        times[k]=t; values[k*4]=q.x; values[k*4+1]=q.y; values[k*4+2]=q.z; values[k*4+3]=q.w;
    }
    AthenaTrack3DDesc track={.path=ATHENA_ANIM3D_ROTATION,.times=times,.values=values,.key_count=KEYS};
    AthenaClip3D *clip=NULL; return athena_clip3d_create(&track,1,&clip)<0?NULL:clip;
}
static int create(Grid *g,unsigned stage,AthenaMesh3D *unlit,AthenaMesh3D *lit) {
    int code=0;
    if(stage==0||stage==1||stage==3) {
        g->batch=athena_batch3d_create(); if(!g->batch) return -2;
        for(unsigned i=0;i<COUNT&&code==0;i++) {
            AthenaInstance3D *item=athena_instance3d_create(stage==0?unlit:lit);
            if(!item) return -2;
            g->items[i]=item;
            if(!athena_instance3d_set_position(item,cube_x(i),row_y(i/GRID),-7)||
                !athena_instance3d_set_scale(item,.25f,.25f,.25f)) code=-1;
            else code=athena_batch3d_add(g->batch,item);
        }
        return code;
    }
    g->scene=athena_scene3d_create(); if(!g->scene) return ATHENA_SCENE3D_ENOMEM;
    g->system=child(athena_scene3d_root(g->scene),NULL,&code);
    if(code==0) code=athena_node3d_set_position(g->system,0,0,-7);
    for(unsigned r=0;r<GRID&&code==0;r++) {
        g->rows[r]=child(g->system,NULL,&code);
        if(code==0) code=athena_node3d_set_position(g->rows[r],0,row_y(r),0);
        for(unsigned c=0;c<GRID&&code==0;c++) {
            AthenaNode3D *n=g->cubes[r*GRID+c]=child(g->rows[r],lit,&code);
            if(code==0) code=athena_node3d_set_position(n,cube_x(c),0,0);
            if(code==0) code=athena_node3d_set_scale(n,.25f,.25f,.25f);
            /* Comparable to stage 4: 2pi/3 and pi/3 rad/s about local x and y. */
            if(code==0&&stage==7) code=athena_node3d_set_spin(n,2*3.14159265358979323846f/3,3.14159265358979323846f/3,0);
            if(code==0&&stage==8) {
                AthenaClip3D *clip=rotation_clip(); if(!clip) return -2;
                AthenaPlayer3D *p=g->players[r*GRID+c]=athena_player3d_create(clip,&n,1);
                athena_clip3d_release(clip); if(!p) return -2;
                athena_player3d_set_loop(p,1); athena_player3d_play(p);
            }
        }
    }
    return code;
}
/* Same spin for instances and nodes: each cube about its own centre. */
static int animate(Grid *g,unsigned stage,unsigned local) {
    float angle=local*3.14159265358979323846f/90;
    if(stage==3) {
        for(unsigned i=0;i<COUNT;i++) if(!athena_instance3d_set_euler(g->items[i],angle,angle*.5f,0)) return -1;
    } else if(stage==4) {
        for(unsigned i=0;i<COUNT;i++) if(athena_node3d_set_euler(g->cubes[i],angle,angle*.5f,0)<0) return -1;
    } else if(stage==8) {
        int finished=athena_animation3d_advance(1/60.0f); if(finished<0) return finished;
    } else if(stage==7) {
        int moved=athena_scene3d_advance(g->scene,1/60.0f); if(moved<0) return moved;
    } else if(stage==5||stage==6) {
        if(stage==6&&athena_node3d_set_euler(g->system,.3f,angle*.25f,0)<0) return -1;
        for(unsigned r=0;r<GRID;r++) if(athena_node3d_set_euler(g->rows[r],angle*(r&1?1:-1),0,0)<0) return -1;
    }
    return 0;
}
static volatile float sink;
static void micro(AthenaCamera3D *camera) {
    AthenaMatrix4 a,b,out; AthenaQuaternion q;
    AthenaVector4 position={.3f,-.2f,-7,1},scale={.25f,.25f,.25f,0};
    const float minimum[3]={-1,-1,-1},maximum[3]={1,1,1};
    athena_quaternion_axis_angle(&q,.6f,.8f,0,.7f);
    athena_quaternion_trs(&a,&position,&q,&scale);
    b=a; b.value[12]=4.9f; /* Straddles the right frustum plane. */
    clock_t t0=clock();
    for(unsigned i=0;i<CALLS;i++) { ath_matrix4_multiply(&out,&camera->view_projection,&a); sink=out.value[i&15]; }
    clock_t t1=clock();
    for(unsigned i=0;i<CALLS;i++) { position.x=i*1e-4f; athena_quaternion_trs(&out,&position,&q,&scale); sink=out.value[12]; }
    clock_t t2=clock();
    for(unsigned i=0;i<CALLS;i++) sink=athena_camera3d_box_relation(camera,&a,minimum,maximum);
    clock_t t3=clock();
    for(unsigned i=0;i<CALLS;i++) sink=athena_camera3d_box_relation(camera,&b,minimum,maximum);
    clock_t t4=clock();
    for(unsigned i=0;i<CALLS;i++) { volatile double x=i*.37,y=x*1.0001+.5; sink=(float)(x*y-y/x); }
    clock_t t5=clock();
    for(unsigned i=0;i<CALLS;i++) { athena_quaternion_euler(&q,i*1e-3f,i*.5e-3f,0); sink=q.w; }
    clock_t t6=clock();
    AthenaQuaternion from=q,to; athena_quaternion_axis_angle(&to,0,1,0,2);
    for(unsigned i=0;i<CALLS;i++) { athena_quaternion_slerp(&q,&from,&to,(i&255)/255.0f); sink=q.w; }
    clock_t t7=clock();
    /* A follow camera: new position and target, then the view-projection. */
    AthenaCamera3D moving=*camera;
    for(unsigned i=0;i<CALLS;i++) {
        athena_camera3d_set_position(&moving,i*1e-3f,1,2); athena_camera3d_look_at(&moving,0,0,-7);
        athena_camera3d_update(&moving); sink=moving.view_projection.value[0];
    }
    clock_t t8=clock();
    double us=1e6/CLOCKS_PER_SEC/CALLS;
    printf("3D_PROFILE_MICRO {\"calls\":%u,\"matrix4MultiplyUs\":%.3f,\"quaternionTrsUs\":%.3f,"
        "\"boxRelationInsideUs\":%.3f,\"boxRelationIntersectUs\":%.3f,\"doubleMulDivAddUs\":%.3f,"
        "\"quaternionEulerUs\":%.3f,\"quaternionSlerpUs\":%.3f,\"cameraMoveUs\":%.3f}\n",
        CALLS,(t1-t0)*us,(t2-t1)*us,(t3-t2)*us,(t4-t3)*us,(t5-t4)*us,(t6-t5)*us,(t7-t6)*us,(t8-t7)*us);
}
static int compare_ticks(const void *a,const void *b) {
    clock_t x=*(const clock_t *)a,y=*(const clock_t *)b; return (x>y)-(x<y);
}
static double median(clock_t *samples,unsigned n) {
    qsort(samples,n,sizeof(*samples),compare_ticks);
    return n%2?samples[n/2]:(samples[n/2-1]+samples[n/2])/2.0;
}
int athena_main(int argc,char **argv) {
    (void)argc; (void)argv;
    int result=1; Grid grid={0}; AthenaMesh3D *unlit=NULL,*lit=NULL;
    AthenaLights *lights=athena_lights_create(); if(!lights) return 1;
    athena_lights_set_ambient(lights,.25f,.25f,.25f);
    athena_lights_set_directional(lights,0,.4f,.6f,1,.8f,.8f,.8f);
    AthenaMaterial3D unlit_material={.shading=ATHENA_MATERIAL3D_UNLIT,.base_color={1,.85f,.4f,1}};
    AthenaMaterial3D lit_material={.shading=ATHENA_MATERIAL3D_DIFFUSE,.base_color={1,.85f,.4f,1}};
    int code=athena_mesh3d_load_with_material("models/lit_cube.glb",&unlit_material,&unlit);
    if(code==0) code=athena_mesh3d_load_with_material("models/lit_cube.glb",&lit_material,&lit);
    if(code<0) { printf("Cannot load models/lit_cube.glb: %s\n",athena_model3d_error(code)); goto cleanup; }
    graphics_service_init(); AthenaScreenMode mode;
    if(athena_screen_get_mode(&mode)!=ATHENA_SCREEN_OK) goto cleanup;
    int height=mode.height*((mode.interlace==GS_INTERLACED&&mode.field==GS_FRAME)?2:1);
    mode.height=height; mode.zbuffering=true; mode.psmz=GS_ZBUF_16S;
    if(athena_screen_set_mode(&mode,NULL)!=ATHENA_SCREEN_OK) goto cleanup;
    AthenaCamera3D camera; athena_camera3d_init(&camera);
    if(!athena_camera3d_set_projection(&camera,60,(float)mode.width/height,1,30)||
        !athena_camera3d_look_at(&camera,0,0,-1)||!athena_camera3d_set_position(&camera,0,0,0)||
        !athena_camera3d_update(&camera)) goto cleanup;
    printf("3D profile: CLOCKS_PER_SEC=%ld\n",(long)CLOCKS_PER_SEC);
    micro(&camera);
    clock_t update_samples[FRAMES-WARMUP],draw_samples[FRAMES-WARMUP];
    for(unsigned stage=0;stage<sizeof(names)/sizeof(names[0]);stage++) {
        release(&grid);
        if((code=create(&grid,stage,unlit,lit))<0) { printf("3D profile create error %d\n",code); goto cleanup; }
        printf("3D profile stage %u: %s\n",stage,names[stage]);
        AthenaRender3DStats totals={0}; uint64_t world_updates=0;
        for(unsigned local=0;local<FRAMES;local++) {
            if(athena_modules_stop_requested()) { result=0; goto cleanup; }
            AthenaScene3DUpdateStats us={0};
            clock_t start=clock();
            code=animate(&grid,stage,local);
            if(code==0&&grid.scene) code=athena_scene3d_update(grid.scene,&us);
            clock_t middle=clock();
            if(code<0) { printf("3D profile update error %d\n",code); goto cleanup; }
            clearScreen(0); AthenaRender3DStats stats={0}; int render=0;
            if(grid.batch) code=athena_batch3d_draw_lit(grid.batch,&camera,lights,ATHENA_RENDER3D_CULL_NONE,&stats);
            else {
                AthenaScene3DDrawStats scene_stats;
                code=athena_scene3d_draw(grid.scene,&camera,lights,ATHENA_RENDER3D_CULL_NONE,&scene_stats,&render);
                stats=scene_stats.render;
            }
            clock_t end=clock();
            if(code<0) { printf("3D profile draw error %d (render %d)\n",code,render); goto cleanup; }
            if(local>=WARMUP) {
                update_samples[local-WARMUP]=middle-start; draw_samples[local-WARMUP]=end-middle;
                totals.triangles+=stats.triangles; totals.clipped_triangles+=stats.clipped_triangles;
                totals.draw_passes+=stats.draw_passes; totals.pipeline_passes+=stats.pipeline_passes; totals.vu_batches+=stats.vu_batches;
                totals.geometry_bytes+=stats.geometry_bytes; world_updates+=us.world_updates;
            }
            for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++)
                draw_sprite(12+i*20,12,14,14,athena_color_new(i==stage?255:64,i==stage?128:64,i==stage?0:64,128));
            draw_sprite(12,height-20,(mode.width-24)*(local+1)/FRAMES,8,athena_color_new(255,255,255,128));
            flipScreen();
        }
        const unsigned n=FRAMES-WARMUP; double update_mean=0,draw_mean=0;
        for(unsigned i=0;i<n;i++) { update_mean+=update_samples[i]; draw_mean+=draw_samples[i]; }
        update_mean/=n; draw_mean/=n;
        double tick_us=1e6/CLOCKS_PER_SEC;
        printf("3D_PROFILE {\"stage\":\"%s\",\"samples\":%u,\"updateUsMean\":%.1f,\"updateUsMedian\":%.1f,"
            "\"drawUsMean\":%.1f,\"drawUsMedian\":%.1f,\"drawUsPerPass\":%.1f,\"worldUpdatesPerFrame\":%.2f,"
            "\"drawPassesPerFrame\":%.2f,\"pipelinePassesPerFrame\":%.2f,\"trianglesPerFrame\":%.2f,\"clippedTrianglesPerFrame\":%.2f,"
            "\"vuBatchesPerFrame\":%.2f,\"geometryBytesPerFrame\":%.1f}\n",
            names[stage],n,update_mean*tick_us,median(update_samples,n)*tick_us,
            draw_mean*tick_us,median(draw_samples,n)*tick_us,
            totals.draw_passes?draw_mean*tick_us*n/totals.draw_passes:0,world_updates/(double)n,
            totals.draw_passes/(double)n,totals.pipeline_passes/(double)n,totals.triangles/(double)n,totals.clipped_triangles/(double)n,
            totals.vu_batches/(double)n,totals.geometry_bytes/(double)n);
    }
    puts("3D profile complete; stages 0-5 should show the same 8x8 grid of 64 cubes."); result=0;
cleanup:
    release(&grid); athena_mesh3d_release(unlit); athena_mesh3d_release(lit);
    athena_lights_destroy(lights);
    return result;
}
