/* Native version of bin/3d.js, without QuickJS.
 * node tools/modules.js configure --modules=screen,loop,render3d
 * make RUNTIME=native APP_SRCS=samples/native/3d/main.c EE_BIN_PREF=athena_3d_native
 */
#include <athena.h>
#include <athena/screen.h>
#include <athena/loop.h>
#include <athena/render3d.h>
static const float positions[]={
    -1,-1,-1,1,-1,-1,1,1,-1,-1,1,-1,
    -1,-1,1,1,-1,1,1,1,1,-1,1,1};
static const float colors[]={
    1,0,0,1,0,1,0,1,0,0,1,1,1,1,0,1,
    1,0,1,1,0,1,1,1,1,1,1,1,0.4f,0.6f,1,1};
static const uint32_t indices[]={
    0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,
    3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5};
int athena_main(int argc,char **argv) {
    (void)argc; (void)argv;
    graphics_service_init();
    AthenaScreenMode mode;
    if(athena_screen_get_mode(&mode)!=ATHENA_SCREEN_OK) return 1;
    int height=mode.height*((mode.interlace==GS_INTERLACED&&mode.field==GS_FRAME)?2:1);
    mode.height=height; /* set_mode expects the complete frame height. */
    mode.zbuffering=true; mode.psmz=GS_ZBUF_16S;
    if(athena_screen_set_mode(&mode,NULL)!=ATHENA_SCREEN_OK) return 1;
    AthenaCamera3D camera; athena_camera3d_init(&camera);
    if(!athena_camera3d_set_projection(&camera,60,(float)mode.width/height,0.1f,300)) return 1;
    AthenaGeometry3D geometry={.positions=positions,.vertex_count=8,.colors=colors,.color_count=8,.indices=indices,.index_count=36};
    AthenaMesh3D *mesh=NULL;
    if(athena_mesh3d_create(&geometry,&mesh)<0) return 1;
    AthenaInstance3D *left=athena_instance3d_create(mesh),*right=athena_instance3d_create(mesh);
    athena_mesh3d_release(mesh);
    AthenaBatch3D *batch=athena_batch3d_create();
    if(!left||!right||!batch) goto fail;
    if(athena_batch3d_add(batch,left)<0||athena_batch3d_add(batch,right)<0) goto fail;
    athena_instance3d_set_position(left,-1.25f,0,0); athena_instance3d_set_position(right,1.25f,0,0);
    athena_instance3d_set_scale(left,0.7f,0.7f,0.7f); athena_instance3d_set_scale(right,0.7f,0.7f,0.7f);
    AthenaLoopClock clock; athena_loop_clock_reset(&clock,ATHENA_LOOP_DEFAULT_MAX_DELTA);
    float angle=0;
    while(!athena_modules_stop_requested()) {
        angle+=athena_loop_clock_tick(&clock);
        athena_instance3d_set_euler(left,angle*0.3f,angle,0);
        athena_instance3d_set_euler(right,0,-angle,angle*0.2f);
        clearScreen(0);
        AthenaRender3DStats stats;
        if(athena_batch3d_draw(batch,&camera,ATHENA_RENDER3D_CULL_NONE,&stats)<0) goto fail;
        flipScreen();
    }
    athena_batch3d_destroy(batch);
    athena_instance3d_release(left); athena_instance3d_release(right); return 0;
fail:
    athena_batch3d_destroy(batch);
    athena_instance3d_release(left); athena_instance3d_release(right); return 1;
}
