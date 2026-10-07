/* P1 CPU fallback A/B. One indexed 16x16 grid: 289 unique vertices,
 * 1536 expanded corners. 25 joints force CPU skinning; target normals force
 * CPU morphing. Warm-up 60 frames, then 120 samples, no external assets. */
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <athena.h>
#include <athena/screen.h>
#include <athena/scene3d.h>
#include <athena/draw.h>
#define SIDE 17
#define VERTICES (SIDE*SIDE)
#define CORNERS ((SIDE-1)*(SIDE-1)*6)
static float positions[VERTICES*3],normals[VERTICES*3],weights[VERTICES*4],deltas[VERTICES*3],normal_deltas[VERTICES*3];
static uint16_t joints[VERTICES*4];static uint32_t indices[CORNERS];
static const char *names[]={"cpu-skin","cpu-morph","cpu-skin-morph"};
int athena_main(int argc,char **argv) {
    (void)argc;(void)argv;int result=1;
    graphics_service_init();AthenaScreenMode mode;if(athena_screen_get_mode(&mode))return 1;
    mode.height*=mode.interlace==GS_INTERLACED&&mode.field==GS_FRAME?2:1;
    mode.zbuffering=true;mode.psmz=GS_ZBUF_16S;if(athena_screen_set_mode(&mode,NULL))return 1;
    AthenaCamera3D camera;athena_camera3d_init(&camera);
    if(!athena_camera3d_set_projection(&camera,60,(float)mode.width/mode.height,.5f,50)||
       !athena_camera3d_look_at(&camera,0,0,-1)||!athena_camera3d_set_position(&camera,0,0,0))return 1;
    for(unsigned y=0;y<SIDE;y++)for(unsigned x=0;x<SIDE;x++){
        unsigned i=y*SIDE+x;positions[i*3]=(x/16.f-.5f)*3;positions[i*3+1]=(y/16.f-.5f)*3;positions[i*3+2]=-5;
        normals[i*3+2]=1;weights[i*4]=1;joints[i*4]=i%25;
        deltas[i*3+2]=.15f*sinf(x*.4f)*cosf(y*.4f);normal_deltas[i*3]=.03f*x;
    }
    unsigned offset=0;
    for(unsigned y=0;y<SIDE-1;y++)for(unsigned x=0;x<SIDE-1;x++){
        unsigned a=y*SIDE+x,b=a+1,c=a+SIDE,d=c+1;
        indices[offset++]=a;indices[offset++]=b;indices[offset++]=d;
        indices[offset++]=a;indices[offset++]=d;indices[offset++]=c;
    }
    for(unsigned stage=0;stage<3;stage++){
        AthenaGeometry3D g={.positions=positions,.vertex_count=VERTICES,.indices=indices,.index_count=CORNERS,
            .normals=normals,.normal_count=VERTICES,.joints=stage!=1?joints:NULL,.weights=stage!=1?weights:NULL,.skin_count=stage!=1?VERTICES:0,
            .target_positions=stage?deltas:NULL,.target_normals=stage?normal_deltas:NULL,.target_count=stage?1:0};
        AthenaMesh3D *mesh=NULL;if(athena_mesh3d_create(&g,&mesh))return 1;
        AthenaMesh3DView view;athena_mesh3d_view(mesh,&view);unsigned computations=0;
        for(unsigned i=0;i<athena_mesh3d_stream_count(&view);i++)if(!view.deform_reuse||view.deform_reuse[i]==i)computations++;
        AthenaScene3D *scene=athena_scene3d_create();AthenaNode3D *root=athena_scene3d_root(scene),*node=athena_node3d_create();
        if(athena_node3d_set_mesh(node,mesh)||athena_node3d_add_child(root,node))return 1;
        athena_mesh3d_release(mesh);
        AthenaNode3D *bones[25]={0};
        if(stage!=1){
            for(unsigned i=0;i<25;i++){bones[i]=athena_node3d_create();if(athena_node3d_add_child(root,bones[i]))return 1;}
            AthenaSkin3D *skin=athena_skin3d_create(bones,25,NULL);if(!skin||athena_node3d_set_skin(node,skin))return 1;athena_skin3d_release(skin);
        }
        double total=0;AthenaScene3DDrawStats stats;
        for(unsigned frame=0;frame<180;frame++){
            if(athena_modules_stop_requested())return 0;
            if(stage){float w=.5f+.5f*sinf(frame*.03f);if(athena_node3d_set_weights(node,&w,1))return 1;}
            if(stage!=1)for(unsigned i=0;i<25;i++)if(athena_node3d_set_position(bones[i],0,.05f*sinf(frame*.03f+i),0))return 1;
            if(athena_scene3d_update(scene,NULL))return 1;
            clearScreen(0);clock_t start=clock();int error=0;
            int code=athena_scene3d_draw(scene,&camera,NULL,ATHENA_RENDER3D_CULL_NONE,&stats,&error);
            clock_t elapsed=clock()-start;if(code||error||stats.render.triangles!=512||stats.render.vu_morph_objects)return 1;
            if(frame>=60)total+=elapsed;
            for(unsigned i=0;i<3;i++)draw_sprite(12+i*20,12,14,14,athena_color_new(i==stage?255:64,128,0,128));
            flipScreen();
        }
        printf("3D_DEFORM {\"stage\":\"%s\",\"samples\":120,\"drawTicksMean\":%.3f,\"vertices\":%u,\"computations\":%u,\"triangles\":%u,\"geometryBytes\":%u}\n",
            names[stage],total/120,CORNERS,computations,stats.render.triangles,stats.render.geometry_bytes);
        for(unsigned i=0;i<25;i++)if(bones[i])athena_node3d_release(bones[i]);
        athena_node3d_release(node);athena_scene3d_release(scene);
    }
    puts("3D deform complete");result=0;return result;
}
