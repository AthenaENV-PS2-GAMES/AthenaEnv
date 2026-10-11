/* Counterpart of bin/3d_scene.js. Launch from bin/ with models/lit_cube.glb.
 * Build with screen,loop,scene3d,draw,usbmass. Seven 180-frame stages:
 * hierarchy, reparenting, visibility, subtree culling, 64 nodes with timing,
 * Loop POST_UPDATE system and scene recreation. Updates are explicit. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <athena.h>
#include <athena/screen.h>
#include <athena/scene3d.h>
#include <athena/loop.h>
#include <athena/draw.h>
#define GRID 8
static const char *names[]={"hierarchy","reparent","visibility","subtree-cull",
    "many-nodes","loop-system","recreate"};
typedef struct {
    AthenaScene3D *scene;
    AthenaNode3D *system,*sun,*planet,*moon,*rows[GRID];
} Demo;
static void release(Demo *d) {
    athena_scene3d_detach_loop(d->scene);
    athena_node3d_release(d->system); athena_node3d_release(d->sun);
    athena_node3d_release(d->planet); athena_node3d_release(d->moon);
    for(unsigned i=0;i<GRID;i++) athena_node3d_release(d->rows[i]);
    athena_scene3d_release(d->scene); memset(d,0,sizeof(*d));
}
static AthenaNode3D *node(AthenaNode3D *parent,AthenaMesh3D *mesh,int *code) {
    AthenaNode3D *n=athena_node3d_create();
    if(!n) { *code=ATHENA_SCENE3D_ENOMEM; return NULL; }
    if(*code==0) *code=athena_node3d_set_mesh(n,mesh);
    if(*code==0) *code=athena_node3d_add_child(parent,n);
    return n;
}
static int create(Demo *d,unsigned stage) {
    AthenaMaterial3D lit={.shading=ATHENA_MATERIAL3D_DIFFUSE,.base_color={1,.85f,.4f,1}};
    AthenaMesh3D *cube=NULL,*marker=NULL;
    int code=athena_mesh3d_load_with_material("models/lit_cube.glb",&lit,&cube);
    if(code<0) { printf("Cannot load models/lit_cube.glb: %s\n",athena_model3d_error(code)); return code; }
    const float positions[]={-.5f,-.5f,0,.5f,-.5f,0,0,.5f,0};
    const float colors[]={1,0,0,1,0,1,0,1,0,0,1,1};
    AthenaGeometry3D geometry={.positions=positions,.vertex_count=3,.colors=colors,.color_count=3};
    code=athena_mesh3d_create(&geometry,&marker);
    d->scene=athena_scene3d_create(); if(!d->scene&&code==0) code=ATHENA_SCENE3D_ENOMEM;
    if(code==0) {
        AthenaNode3D *root=athena_scene3d_root(d->scene);
        d->system=node(root,NULL,&code);
        if(stage==4) {
            for(unsigned r=0;r<GRID&&code==0;r++) {
                d->rows[r]=node(d->system,NULL,&code);
                if(code==0) code=athena_node3d_set_position(d->rows[r],0,(r-3.5f)*.7f,0);
                for(unsigned c=0;c<GRID&&code==0;c++) {
                    AthenaNode3D *n=node(d->rows[r],cube,&code);
                    if(code==0) code=athena_node3d_set_position(n,(c-3.5f)*.9f,0,0);
                    if(code==0) code=athena_node3d_set_scale(n,.25f,.25f,.25f);
                    athena_node3d_release(n);
                }
            }
        } else {
            d->sun=node(d->system,cube,&code);
            d->planet=node(d->system,cube,&code);
            d->moon=node(d->planet,marker,&code);
            if(code==0) code=athena_node3d_set_scale(d->sun,.8f,.8f,.8f);
            if(code==0) code=athena_node3d_set_scale(d->planet,.4f,.4f,.4f);
            if(code==0) code=athena_node3d_set_position(d->moon,0,0,2.2f);
        }
        if(code==0) code=athena_node3d_set_position(d->system,0,0,-7);
    }
    athena_mesh3d_release(cube); athena_mesh3d_release(marker);
    if(code<0) { printf("Scene3D create error: %s\n",athena_scene3d_error(code)); release(d); }
    return code;
}
static void animate(Demo *d,unsigned stage,unsigned local) {
    float angle=local*3.14159265358979323846f/90;
    if(stage==4) {
        athena_node3d_set_euler(d->system,.3f,angle*.25f,0);
        for(unsigned r=0;r<GRID;r++) athena_node3d_set_euler(d->rows[r],angle*(r&1?1:-1),0,0);
        return;
    }
    athena_node3d_set_euler(d->sun,.35f,angle*.5f,0);
    /* The planet orbits through its own position; the moon inherits it. */
    athena_node3d_set_position(d->planet,3*cosf(angle*.5f),0,3*sinf(angle*.5f));
    athena_node3d_set_euler(d->planet,0,angle*2,0);
    athena_node3d_set_euler(d->system,.25f,0,0);
    if(stage==1&&local%60==0) {
        AthenaNode3D *target=local%120==0?d->sun:d->planet;
        athena_node3d_add_child(target,d->moon);
        printf("3D scene: moon parented to %s\n",target==d->sun?"sun":"planet");
    }
    if(stage==2&&local%45==0) athena_node3d_set_visible(d->planet,!athena_node3d_visible(d->planet));
    if(stage==3) athena_node3d_set_position(d->system,9*sinf(angle*.5f),0,-7);
}
int athena_main(int argc,char **argv) {
    (void)argc; (void)argv;
    int result=1; Demo demo={0};
    AthenaLights *lights=athena_lights_create(); if(!lights) return 1;
    athena_lights_set_ambient(lights,.25f,.25f,.25f);
    athena_lights_set_directional(lights,0,.4f,.6f,1,.8f,.8f,.8f);
    graphics_service_init(); AthenaScreenMode mode;
    if(athena_screen_get_mode(&mode)!=ATHENA_SCREEN_OK) goto cleanup;
    int height=mode.height*((mode.interlace==GS_INTERLACED&&mode.field==GS_FRAME)?2:1);
    mode.height=height; mode.zbuffering=true; mode.psmz=GS_ZBUF_16S;
    if(athena_screen_set_mode(&mode,NULL)!=ATHENA_SCREEN_OK) goto cleanup;
    AthenaCamera3D camera; athena_camera3d_init(&camera);
    if(!athena_camera3d_set_projection(&camera,60,(float)mode.width/height,1,30)||
        !athena_camera3d_look_at(&camera,0,0,-1)||!athena_camera3d_set_position(&camera,0,0,0)) goto cleanup;
    for(unsigned stage=0;stage<7;stage++) {
        release(&demo);
        if(create(&demo,stage)<0) goto cleanup;
        if(stage==5&&athena_scene3d_attach_loop(demo.scene,0,NULL)<0) goto cleanup;
        printf("3D scene stage %u: %s\n",stage,names[stage]);
        clock_t update_ticks=0,draw_ticks=0;
        for(unsigned local=0;local<180;local++) {
            if(athena_modules_stop_requested()) { result=0; goto cleanup; }
            if(!demo.scene&&create(&demo,stage)<0) goto cleanup;
            animate(&demo,stage,local);
            AthenaScene3DUpdateStats us={0};
            clock_t start=clock(); int code;
            /* A C game loop runs the systems itself; Loop.run does it in JS. */
            if(stage==5) code=athena_loop_systems_run(ATHENA_LOOP_POST_UPDATE,1/60.0f,1/60.0f,NULL);
            else code=athena_scene3d_update(demo.scene,&us);
            clock_t middle=clock();
            if(code<0) { printf("3D scene update error %d\n",code); goto cleanup; }
            clearScreen(0); AthenaScene3DDrawStats stats; int render=0;
            code=athena_scene3d_draw(demo.scene,&camera,lights,ATHENA_RENDER3D_CULL_NONE,&stats,&render);
            clock_t end=clock();
            if(code<0) { printf("3D scene draw error: %s (render %d)\n",athena_scene3d_error(code),render); goto cleanup; }
            if(local>=60) { update_ticks+=middle-start; draw_ticks+=end-middle; }
            if(local%60==0) printf("3D scene: stage=%s visited=%u worldUpdates=%u queued=%u submitted=%u culled=%u culledSubtrees=%u triangles=%u geometryBytes=%llu\n",
                names[stage],(unsigned)us.visited_nodes,(unsigned)us.world_updates,(unsigned)stats.queued_objects,
                (unsigned)stats.render.submitted_objects,(unsigned)stats.render.culled_objects,(unsigned)stats.culled_subtrees,
                (unsigned)stats.render.triangles,(unsigned long long)stats.render.geometry_bytes);
            if(stage==6&&local%30==29) release(&demo);
            for(unsigned i=0;i<7;i++) draw_sprite(12+i*20,12,14,14,athena_color_new(i==stage?255:64,i==stage?128:64,i==stage?0:64,128));
            draw_sprite(12,height-20,(mode.width-24)*(local+1)/180,8,athena_color_new(255,255,255,128));
            flipScreen();
        }
        printf("3D scene: stage=%s mean over 120 frames update=%.3f ms draw=%.3f ms\n",names[stage],
            update_ticks*1000.0/CLOCKS_PER_SEC/120,draw_ticks*1000.0/CLOCKS_PER_SEC/120);
    }
    puts("3D scene complete; inspect images before recording visual PASS."); result=0;
cleanup:
    release(&demo); athena_lights_destroy(lights);
    return result;
}
