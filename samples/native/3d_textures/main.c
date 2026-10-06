/* Counterpart of bin/3d_textures.js. Launch from bin/ with models/. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <athena.h>
#include <athena/screen.h>
#include <athena/render3d.h>
#include <athena/image.h>
#include <athena/draw.h>
#include <athena/tilemap.h>
static const char *names[]={"unlit","diffuse","perspective","clip-sweep","filters",
    "camera-tile-camera","retained-texture","recreate","video-reset"};
typedef struct { AthenaInstance3D *objects[3]; AthenaBatch3D *batch; AthenaTexture3D *texture; } Scene;
static void release(Scene *s) {
    athena_batch3d_destroy(s->batch);
    for(unsigned i=0;i<3;i++) athena_instance3d_release(s->objects[i]);
    athena_texture3d_release(s->texture); memset(s,0,sizeof(*s));
}
static int create(Scene *s,unsigned stage) {
    AthenaMaterial3D material; athena_material3d_default(&material);
    material.shading=stage==1?ATHENA_MATERIAL3D_DIFFUSE:ATHENA_MATERIAL3D_UNLIT;
    AthenaMesh3D *left=NULL,*right=NULL,*quad=NULL;
    int code=athena_mesh3d_load_with_material("models/textured_cube.glb",&material,&left);
    if(code<0) goto done;
    if(stage==6) {
        uint32_t pixels[64*64],palette[4][3]={{255,64,64},{64,255,64},{64,64,255},{255,255,64}};
        for(unsigned y=0;y<64;y++) for(unsigned x=0;x<64;x++) {
            unsigned index=(y>=32?2:0)+(x>=32?1:0); float factor=((x>>3)^(y>>3))&1?1:.35f;
            pixels[y*64+x]=0xff000000u|((uint32_t)lroundf(palette[index][0]*factor))|
                ((uint32_t)lroundf(palette[index][1]*factor)<<8)|((uint32_t)lroundf(palette[index][2]*factor)<<16);
        }
        AthenaTexture3DPixels p={.width=64,.height=64,.pixels=pixels,.pixel_count=4096};
        code=athena_texture3d_create(&p,&s->texture);
    } else code=athena_texture3d_load("models/checker.png",stage==4?ATHENA_TEXTURE3D_LINEAR:ATHENA_TEXTURE3D_NEAREST,&s->texture);
    if(code<0) goto done;
    material.texture=s->texture;
    code=athena_mesh3d_load_with_material("models/textured_cube.glb",&material,&right); if(code<0) goto done;
    /* Planar tilted quad: differing W values expose affine UV distortion. */
    const float positions[]={-.9f,-.6f,-2,.9f,-.6f,-5,.9f,.6f,-5, -.9f,-.6f,-2,.9f,.6f,-5,-.9f,.6f,-2};
    const float uv[]={0,1,1,1,1,0, 0,1,1,0,0,0};
    AthenaGeometry3D geometry={.positions=positions,.vertex_count=6,.texcoords=uv,.texcoord_count=6,.material=&material};
    code=athena_mesh3d_create(&geometry,&quad); if(code<0) goto done;
    s->objects[0]=athena_instance3d_create(left); s->objects[1]=athena_instance3d_create(right);
    s->objects[2]=athena_instance3d_create(quad); s->batch=athena_batch3d_create();
    if(!s->objects[0]||!s->objects[1]||!s->objects[2]||!s->batch) { code=ATHENA_MODEL3D_ENOMEM; goto done; }
    athena_instance3d_set_position(s->objects[0],-1.7f,.5f,-6); athena_instance3d_set_scale(s->objects[0],.65f,.65f,.65f);
    athena_instance3d_set_position(s->objects[1],1.7f,.5f,-6); athena_instance3d_set_scale(s->objects[1],.65f,.65f,.65f);
    athena_instance3d_set_position(s->objects[2],0,-.7f,-1);
    for(unsigned i=0;i<3&&code==0;i++) code=athena_batch3d_add(s->batch,s->objects[i]);
done:
    athena_mesh3d_release(left); athena_mesh3d_release(right); athena_mesh3d_release(quad);
    if(code<0) printf("3D texture creation error: %s (%d)\n",athena_model3d_error(code),code);
    return code;
}
int athena_main(int argc,char **argv) {
    (void)argc; (void)argv;
    int result=1; Scene scene={0}; AthenaTileSprite *tile=NULL; AthenaImage *image=NULL;
    AthenaLights *lights=athena_lights_create(); if(!lights) goto cleanup;
    athena_lights_set_ambient(lights,.2f,.2f,.2f); athena_lights_set_directional(lights,0,0,0,1,.8f,.8f,.8f);
    graphics_service_init(); AthenaScreenMode mode;
    if(athena_screen_get_mode(&mode)!=ATHENA_SCREEN_OK) goto cleanup;
    int height=mode.height*((mode.interlace==GS_INTERLACED&&mode.field==GS_FRAME)?2:1);
    mode.height=height; mode.zbuffering=true; mode.psmz=GS_ZBUF_16S;
    if(athena_screen_set_mode(&mode,NULL)!=ATHENA_SCREEN_OK) goto cleanup;
    AthenaCamera3D camera,camera_b; athena_camera3d_init(&camera); athena_camera3d_init(&camera_b);
    athena_camera3d_set_projection(&camera,60,(float)mode.width/height,1,20);
    athena_camera3d_look_at(&camera,0,0,-1); athena_camera3d_set_position(&camera,0,0,0);
    athena_camera3d_set_projection(&camera_b,60,(float)mode.width/height,1,20);
    athena_camera3d_look_at(&camera_b,.4f,0,-1); athena_camera3d_set_position(&camera_b,.4f,0,0);
    image=athena_image_create("models/checker.png",false); if(!image||!athena_image_is_loaded(image)) goto cleanup;
    tile=athena_tilemap_buffer_alloc(1); if(!tile) goto cleanup;
    tile->w=48; tile->h=48; tile->u2=64; tile->v2=64; tile->r=tile->g=tile->b=tile->a=128;
    AthenaTileMaterial tile_material={.texture_index=0,.end=0}; GSSURFACE *tile_textures[]={image->surface};
    for(unsigned stage=0;stage<9;stage++) {
        release(&scene); if(create(&scene,stage)<0) goto cleanup;
        printf("3D textures stage %u: %s\n",stage,names[stage]);
        for(unsigned local=0;local<180;local++) {
            if(athena_modules_stop_requested()) { result=0; goto cleanup; }
            if(!scene.batch&&create(&scene,stage)<0) goto cleanup;
            float angle=local*3.14159265358979323846f/90;
            athena_instance3d_set_euler(scene.objects[0],.3f,angle*.25f,0);
            athena_instance3d_set_euler(scene.objects[1],.3f,-angle*.25f,0);
            if(stage==2) athena_instance3d_set_euler(scene.objects[2],0,.35f*sinf(angle),0);
            if(stage==3) athena_instance3d_set_position(scene.objects[2],.8f*sinf(angle),-.7f,1.5f*sinf(angle));
            clearScreen(0); AthenaRender3DStats stats={0}; int code;
            if(stage==5) {
                code=athena_render3d_draw_lit(scene.objects[0],&camera,lights,ATHENA_RENDER3D_CULL_NONE,&stats);
                if(code==0&&athena_tilemap_render(&tile_material,1,tile_textures,1,tile,1,NULL,0,mode.width/2.0f-24,36)!=1) code=-1;
                AthenaRender3DStats extra={0};
                if(code==0) code=athena_render3d_draw_lit(scene.objects[1],&camera_b,lights,ATHENA_RENDER3D_CULL_NONE,&extra);
                if(code==0) code=athena_render3d_draw_lit(scene.objects[2],&camera,lights,ATHENA_RENDER3D_CULL_NONE,&extra);
            } else code=athena_batch3d_draw_lit(scene.batch,&camera,lights,ATHENA_RENDER3D_CULL_NONE,&stats);
            if(code<0) { printf("3D textures draw error %d\n",code); goto cleanup; }
            if(local%60==0) printf("3D textures: stage=%s submitted=%u source=%u output=%u clipped=%u VU batches=%u geometryBytes=%llu\n",
                names[stage],(unsigned)stats.submitted_objects,(unsigned)stats.source_triangles,(unsigned)stats.triangles,
                (unsigned)stats.clipped_triangles,(unsigned)stats.vu_batches,(unsigned long long)stats.geometry_bytes);
            if(stage==6&&local==90) { athena_texture3d_release(scene.texture); scene.texture=NULL; puts("3D textures: texture handle released; meshes retain it"); }
            if(stage==7&&local%30==29) release(&scene);
            if(stage==8&&local==90) {
                if(athena_screen_set_mode(&mode,NULL)!=ATHENA_SCREEN_OK) goto cleanup;
                puts("3D textures: video reset; retained pixels will re-upload");
            }
            for(unsigned i=0;i<9;i++) draw_sprite(12+i*20,12,14,14,athena_color_new(i==stage?255:64,i==stage?128:64,0,128));
            draw_sprite(12,height-20,(mode.width-24)*(local+1)/180,8,athena_color_new(255,255,255,128)); flipScreen();
        }
    }
    puts("3D textures complete; inspect images before recording visual PASS."); result=0;
cleanup:
    release(&scene); athena_lights_destroy(lights);
    if(tile) { athena_tilemap_sync(); free(tile); }
    if(image) { graphics_wait_idle(); athena_image_destroy(image); }
    return result;
}
