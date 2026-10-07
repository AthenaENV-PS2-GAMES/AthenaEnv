/* Indexed/soup A/B: change only MODEL3D_INDEXED=0|1. Includes GS idle
 * to account for work deferred by DMA/VU1; the extra fence is benchmark-only. */
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <athena.h>
#include <athena/memory.h>
#include <athena/screen.h>
#include <athena/scene3d.h>
#include <athena/draw.h>
#define SIDE 17
#define VERTICES (SIDE*SIDE)
#define CORNERS ((SIDE-1)*(SIDE-1)*6)
static float positions[VERTICES*3],normals[VERTICES*3],weights[VERTICES*4],deltas[VERTICES*3*4];
static uint16_t joints[VERTICES*4];static uint32_t indices[CORNERS];
static const char *names[]={"unlit","diffuse","skin","morph-4"};
int athena_main(int argc,char **argv) {
    (void)argc;(void)argv;
    graphics_service_init();AthenaScreenMode mode;if(athena_screen_get_mode(&mode))return 1;
    mode.height*=mode.interlace==GS_INTERLACED&&mode.field==GS_FRAME?2:1;
    mode.zbuffering=true;mode.psmz=GS_ZBUF_16S;if(athena_screen_set_mode(&mode,NULL))return 1;
    AthenaCamera3D camera;athena_camera3d_init(&camera);
    if(!athena_camera3d_set_projection(&camera,60,(float)mode.width/mode.height,.5f,50)||
       !athena_camera3d_look_at(&camera,0,0,-1)||!athena_camera3d_set_position(&camera,0,0,0))return 1;
    AthenaLights *lights=athena_lights_create();if(!lights||!athena_lights_set_ambient(lights,.6f,.7f,1))return 1;
    for(unsigned y=0;y<SIDE;y++)for(unsigned x=0;x<SIDE;x++) {
        unsigned i=y*SIDE+x;positions[i*3]=(x/16.f-.5f)*3;positions[i*3+1]=(y/16.f-.5f)*3;positions[i*3+2]=-6;
        normals[i*3+2]=1;weights[i*4]=1;joints[i*4]=y>8;
        for(unsigned t=0;t<4;t++)deltas[(t*VERTICES+i)*3+2]=.1f*sinf(x*.4f+t)*cosf(y*.4f+t);
    }
    unsigned at=0;
    for(unsigned y=0;y<SIDE-1;y++)for(unsigned x=0;x<SIDE-1;x++) {
        unsigned a=y*SIDE+x;uint32_t tri[]={a,a+1,a+SIDE+1,a,a+SIDE+1,a+SIDE};
        for(unsigned k=0;k<6;k++)indices[at++]=tri[k];
    }
    printf("3D indexed profile CLOCKS_PER_SEC=%ld\n",(long)CLOCKS_PER_SEC);
    for(unsigned stage=0;stage<4;stage++) {
        AthenaMaterial3D material;athena_material3d_default(&material);material.shading=stage?ATHENA_MATERIAL3D_DIFFUSE:ATHENA_MATERIAL3D_UNLIT;
        AthenaGeometry3D g={.positions=positions,.vertex_count=VERTICES,.indices=indices,.index_count=CORNERS,
            .normals=normals,.normal_count=VERTICES,.material=&material,
            .joints=stage==2?joints:NULL,.weights=stage==2?weights:NULL,.skin_count=stage==2?VERTICES:0,
            .target_positions=stage==3?deltas:NULL,.target_count=stage==3?4:0};
        size_t before=get_allocs_size();AthenaMesh3D *mesh=NULL;if(athena_mesh3d_create(&g,&mesh))return 1;
        size_t model_bytes=get_allocs_size()-before;AthenaMesh3DView view;athena_mesh3d_view(mesh,&view);
        unsigned transformed=0;for(unsigned b=0;b<view.chunk_count;b++)transformed+=view.chunks[b].vertex_count;
        if(!view.indices)transformed=CORNERS;
        AthenaScene3D *scene=athena_scene3d_create();AthenaNode3D *root=athena_scene3d_root(scene),*node=athena_node3d_create();
        if(!scene||!node||athena_node3d_set_mesh(node,mesh)||athena_node3d_add_child(root,node))return 1;
        athena_mesh3d_release(mesh);AthenaNode3D *bones[2]={0};
        if(stage==2) {
            for(unsigned i=0;i<2;i++){bones[i]=athena_node3d_create();if(!bones[i]||athena_node3d_add_child(root,bones[i]))return 1;}
            AthenaSkin3D *skin=athena_skin3d_create(bones,2,NULL);if(!skin||athena_node3d_set_skin(node,skin))return 1;athena_skin3d_release(skin);
        }
        double draw_total=0,idle_total=0;AthenaScene3DDrawStats stats;
        for(unsigned frame=0;frame<180;frame++) {
            if(athena_modules_stop_requested())return 0;
            if(stage==3){float w[]={.2f,.4f,-.3f,.1f};if(athena_node3d_set_weights(node,w,4))return 1;}
            if(stage==2&&athena_node3d_set_position(bones[1],0,.05f*sinf(frame*.03f),0))return 1;
            if(athena_scene3d_update(scene,NULL))return 1;
            clearScreen(0);clock_t start=clock();int error=0;
            int code=athena_scene3d_draw(scene,&camera,lights,ATHENA_RENDER3D_CULL_NONE,&stats,&error);clock_t elapsed=clock()-start;
            graphics_wait_idle();clock_t idle=clock()-start;
            if(code||error||stats.render.triangles!=512||stats.render.near_clip_objects||stats.render.vu_morph_objects!=(stage==3?1u:0u))return 1;
            if(frame>=60){draw_total+=elapsed;idle_total+=idle;}
            for(unsigned i=0;i<4;i++)draw_sprite(12+i*20,12,14,14,athena_color_new(i==stage?255:64,128,0,128));
            flipScreen();
        }
        printf("3D_INDEXED_PROFILE {\"stage\":\"%s\",\"samples\":120,\"drawUs\":%.3f,\"drawIdleUs\":%.3f,\"modelBytes\":%u,\"streamVertices\":%u,\"transformedVertices\":%u,\"chunks\":%u,\"triangles\":%u,\"vuBatches\":%u,\"geometryBytes\":%u}\n",
            names[stage],draw_total/120*1e6/CLOCKS_PER_SEC,idle_total/120*1e6/CLOCKS_PER_SEC,(unsigned)model_bytes,
            (unsigned)athena_mesh3d_stream_count(&view),transformed,(unsigned)view.chunk_count,(unsigned)stats.render.triangles,(unsigned)stats.render.vu_batches,(unsigned)stats.render.geometry_bytes);
        for(unsigned i=0;i<2;i++)if(bones[i])athena_node3d_release(bones[i]);
        athena_node3d_release(node);athena_scene3d_release(scene);
    }
    athena_lights_destroy(lights);puts("3D indexed profile complete");return 0;
}
