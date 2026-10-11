/* Same clipping scene as bin/3d_clip.js; build with screen,loop,render3d,usbmass.
 * make RUNTIME=native APP_SRCS=samples/native/3d_clipping/main.c EE_BIN_PREF=athena_3d_clip_native
 */
#include <math.h>
#include <stdio.h>
#include <athena.h>
#include <athena/screen.h>
#include <athena/loop.h>
#include <athena/render3d.h>
int athena_main(int argc,char **argv) {
    (void)argc; (void)argv;
    int result=1;
    AthenaMesh3D *near_mesh=NULL,*edge_mesh=NULL;
    AthenaInstance3D *near=NULL,*horizontal=NULL,*vertical=NULL;
    AthenaBatch3D *batch=NULL;
    graphics_service_init();
    AthenaScreenMode mode;
    if(athena_screen_get_mode(&mode)!=ATHENA_SCREEN_OK) return 1;
    int height=mode.height*((mode.interlace==GS_INTERLACED&&mode.field==GS_FRAME)?2:1);
    mode.height=height; mode.zbuffering=true; mode.psmz=GS_ZBUF_16S;
    if(athena_screen_set_mode(&mode,NULL)!=ATHENA_SCREEN_OK) return 1;
    float aspect=(float)mode.width/height;
    AthenaCamera3D camera; athena_camera3d_init(&camera);
    if(!athena_camera3d_set_projection(&camera,60,aspect,1,20)||
        !athena_camera3d_look_at(&camera,0,0,-1)||!athena_camera3d_set_position(&camera,0,0,0)) return 1;
    const float colors[]={1,0,0,1,0,1,0,1,0,0,1,1};
    const float near_positions[]={-.18f,-.18f,-.4f,.5f,-.18f,-2.5f,-.18f,.5f,-2.5f};
    const float edge_positions[]={-.6f,-.6f,-4,.6f,-.6f,-4,0,.6f,-4};
    AthenaGeometry3D geometry={.positions=near_positions,.vertex_count=3,.colors=colors,.color_count=3};
    if(athena_mesh3d_create(&geometry,&near_mesh)<0) goto cleanup;
    geometry.positions=edge_positions;
    if(athena_mesh3d_create(&geometry,&edge_mesh)<0) goto cleanup;
    near=athena_instance3d_create(near_mesh);
    horizontal=athena_instance3d_create(edge_mesh); vertical=athena_instance3d_create(edge_mesh);
    batch=athena_batch3d_create();
    if(!near||!horizontal||!vertical||!batch) goto cleanup;
    if(athena_batch3d_add(batch,horizontal)<0||athena_batch3d_add(batch,vertical)<0||
        athena_batch3d_add(batch,near)<0) goto cleanup;
    AthenaLoopClock clock; athena_loop_clock_reset(&clock,ATHENA_LOOP_DEFAULT_MAX_DELTA);
    float time=0,report=0,edge=4*tanf(3.14159265358979323846f/6)+.8f;
    while(!athena_modules_stop_requested()) {
        float dt=athena_loop_clock_tick(&clock); time+=dt; report+=dt;
        athena_instance3d_set_position(near,0,0,sinf(time)*.8f);
        athena_instance3d_set_position(horizontal,sinf(time*.7f)*((edge-.8f)*aspect+.8f),0,0);
        athena_instance3d_set_position(vertical,0,cosf(time*.9f)*edge,0);
        clearScreen(0);
        AthenaRender3DStats stats;
        int code=athena_batch3d_draw(batch,&camera,ATHENA_RENDER3D_CULL_NONE,&stats);
        if(code<0) { printf("3D clip draw error %d\n",code); goto cleanup; }
        if(report>=5) {
            report=0;
            printf("3D clip: input=%u output=%u clipped=%u rejected=%u culledObjects=%u VU batches=%u\n",
                (unsigned)stats.source_triangles,(unsigned)stats.triangles,(unsigned)stats.clipped_triangles,
                (unsigned)stats.rejected_triangles,(unsigned)stats.culled_objects,(unsigned)stats.vu_batches);
        }
        flipScreen();
    }
    result=0;
cleanup:
    athena_batch3d_destroy(batch);
    athena_instance3d_release(near); athena_instance3d_release(horizontal); athena_instance3d_release(vertical);
    athena_mesh3d_release(near_mesh); athena_mesh3d_release(edge_mesh);
    return result;
}
