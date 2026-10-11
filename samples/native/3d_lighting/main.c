/* Counterpart of bin/3d_lighting.js. Launch from bin/ with models/lit_cube.glb.
 * Nine 180-frame stages; shared GLB, independent lights, clipping and 2D HUD. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <athena.h>
#include <athena/screen.h>
#include <athena/render3d.h>
#include <athena/draw.h>
#include <athena/tilemap.h>
static const char *names[]={"unlit","ambient","directional","rotating-light","four-lights",
    "nonuniform","clip-sweep","camera-tile-camera","recreate"};
typedef struct { AthenaInstance3D *objects[3]; AthenaBatch3D *batch; } Scene;
static void release(Scene *s) {
    athena_batch3d_destroy(s->batch);
    for(unsigned i=0;i<3;i++) athena_instance3d_release(s->objects[i]);
    memset(s,0,sizeof(*s));
}
static int create(Scene *s,unsigned stage) {
    AthenaMaterial3D material={.shading=stage==0?ATHENA_MATERIAL3D_UNLIT:ATHENA_MATERIAL3D_DIFFUSE,.base_color={.75f,.9f,1,1}};
    AthenaMesh3D *mesh=NULL,*triangle=NULL;
    int code=athena_mesh3d_load_with_material("models/lit_cube.glb",&material,&mesh);
    if(code<0) { printf("Cannot load models/lit_cube.glb: %s\n",athena_model3d_error(code)); return code; }
    const float positions[]={-.18f,-.18f,-.4f,.5f,-.18f,-2.5f,-.18f,.5f,-2.5f};
    const float colors[]={1,0,0,1,0,1,0,1,0,0,1,1};
    AthenaGeometry3D geometry={.positions=positions,.vertex_count=3,.colors=colors,.color_count=3,.material=&material};
    code=athena_mesh3d_create(&geometry,&triangle);
    if(code==0) {
        s->objects[0]=athena_instance3d_create(mesh); s->objects[1]=athena_instance3d_create(mesh);
        s->objects[2]=athena_instance3d_create(triangle); s->batch=athena_batch3d_create();
        if(!s->objects[0]||!s->objects[1]||!s->objects[2]||!s->batch) code=-2;
        else {
            athena_instance3d_set_position(s->objects[0],-1.7f,0,-6); athena_instance3d_set_scale(s->objects[0],.75f,.75f,.75f);
            athena_instance3d_set_position(s->objects[1],1.7f,0,-6); athena_instance3d_set_scale(s->objects[1],.75f,.75f,.75f);
            athena_instance3d_set_position(s->objects[2],0,-.6f,-2);
            for(unsigned i=0;i<3&&code==0;i++) code=athena_batch3d_add(s->batch,s->objects[i]);
        }
    }
    athena_mesh3d_release(mesh); athena_mesh3d_release(triangle); return code;
}
int athena_main(int argc,char **argv) {
    (void)argc; (void)argv;
    int result=1; Scene scene={0}; AthenaTileSprite *tile=NULL;
    AthenaLights *lights=athena_lights_create(),*other_lights=athena_lights_create();
    if(!lights||!other_lights) goto cleanup;
    athena_lights_set_ambient(other_lights,.1f,.1f,.3f); athena_lights_set_directional(other_lights,0,0,0,1,.2f,.3f,.7f);
    graphics_service_init(); AthenaScreenMode mode;
    if(athena_screen_get_mode(&mode)!=ATHENA_SCREEN_OK) goto cleanup;
    int height=mode.height*((mode.interlace==GS_INTERLACED&&mode.field==GS_FRAME)?2:1);
    mode.height=height; mode.zbuffering=true; mode.psmz=GS_ZBUF_16S;
    if(athena_screen_set_mode(&mode,NULL)!=ATHENA_SCREEN_OK) goto cleanup;
    AthenaCamera3D camera,camera_b; athena_camera3d_init(&camera); athena_camera3d_init(&camera_b);
    if(!athena_camera3d_set_projection(&camera,60,(float)mode.width/height,1,20)||
        !athena_camera3d_look_at(&camera,0,0,-1)||!athena_camera3d_set_position(&camera,0,0,0)||
        !athena_camera3d_set_projection(&camera_b,60,(float)mode.width/height,1,20)||
        !athena_camera3d_look_at(&camera_b,.4f,0,-1)||!athena_camera3d_set_position(&camera_b,.4f,0,0)) goto cleanup;
    tile=athena_tilemap_buffer_alloc(1); if(!tile) goto cleanup;
    tile->w=32; tile->h=16; tile->r=255; tile->g=128; tile->a=128;
    AthenaTileMaterial tile_material={.texture_index=ATHENA_TILEMAP_NO_TEXTURE,.end=0};
    for(unsigned stage=0;stage<9;stage++) {
        release(&scene); athena_lights_clear(lights); athena_lights_set_ambient(lights,.2f,.2f,.2f);
        if(stage>=2) athena_lights_set_directional(lights,0,0,0,1,.8f,.8f,.8f);
        if(stage==4) {
            athena_lights_set_directional(lights,0,0,0,1,.6f,.1f,.1f);
            athena_lights_set_directional(lights,1,1,0,0,.1f,.6f,.1f);
            athena_lights_set_directional(lights,2,0,1,0,.1f,.1f,.6f);
            athena_lights_set_directional(lights,3,-1,0,1,.3f,.3f,.3f);
        }
        if(create(&scene,stage)<0) goto cleanup;
        printf("3D lighting stage %u: %s\n",stage,names[stage]);
        for(unsigned local=0;local<180;local++) {
            if(athena_modules_stop_requested()) { result=0; goto cleanup; }
            if(!scene.batch&&create(&scene,stage)<0) goto cleanup;
            float angle=local*3.14159265358979323846f/90;
            athena_instance3d_set_euler(scene.objects[0],.35f,angle*.25f,0);
            athena_instance3d_set_euler(scene.objects[1],.35f,-angle*.25f,0);
            if(stage==3) athena_lights_set_directional(lights,0,sinf(angle),.4f,cosf(angle),.8f,.8f,.8f);
            if(stage==4&&local==90) { athena_lights_disable(lights,3); puts("3D lighting: slot 3 disabled"); }
            if(stage==5) athena_instance3d_set_scale(scene.objects[1],.35f,1.2f,.6f);
            if(stage==6) athena_instance3d_set_position(scene.objects[2],0,-.6f,-1.6f+1.6f*sinf(angle));
            clearScreen(0); AthenaRender3DStats stats={0}; int code;
            if(stage==7) {
                code=athena_render3d_draw_lit(scene.objects[0],&camera,lights,ATHENA_RENDER3D_CULL_BACK,&stats);
                if(code==0&&athena_tilemap_render(&tile_material,1,NULL,0,tile,1,NULL,0,mode.width/2.0f-16,36)!=1) code=-1;
                AthenaRender3DStats extra={0};
                if(code==0) code=athena_render3d_draw_lit(scene.objects[1],&camera_b,other_lights,ATHENA_RENDER3D_CULL_BACK,&extra);
                if(code==0) code=athena_render3d_draw_lit(scene.objects[2],&camera,lights,ATHENA_RENDER3D_CULL_NONE,&extra);
            } else code=athena_batch3d_draw_lit(scene.batch,&camera,lights,ATHENA_RENDER3D_CULL_NONE,&stats);
            if(code<0) { printf("3D lighting draw error %d\n",code); goto cleanup; }
            if(local%60==0) printf("3D lighting: stage=%s submitted=%u source=%u output=%u clipped=%u VU batches=%u geometryBytes=%llu\n",
                names[stage],(unsigned)stats.submitted_objects,(unsigned)stats.source_triangles,(unsigned)stats.triangles,
                (unsigned)stats.clipped_triangles,(unsigned)stats.vu_batches,(unsigned long long)stats.geometry_bytes);
            if(stage==8&&local%30==29) release(&scene);
            for(unsigned i=0;i<9;i++) draw_sprite(12+i*20,12,14,14,athena_color_new(i==stage?255:64,i==stage?128:64,i==stage?0:64,128));
            draw_sprite(12,height-20,(mode.width-24)*(local+1)/180,8,athena_color_new(255,255,255,128));
            flipScreen();
        }
    }
    puts("3D lighting complete; inspect images before recording visual PASS."); result=0;
cleanup:
    release(&scene); athena_lights_destroy(lights); athena_lights_destroy(other_lights);
    if(tile) { athena_tilemap_sync(); free(tile); }
    return result;
}
