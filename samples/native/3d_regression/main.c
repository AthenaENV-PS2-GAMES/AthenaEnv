/* Deterministic counterpart of bin/3d_regression.js. No external assets.
 * Build with screen,loop,render3d,draw,tilemap,system,timer,usbmass.
 * 11 stages x 180 frames; first 60 frames of each stage are warm-up. Stage 9
 * spins a closed cube with one colour per face under CULL_BACK: every face
 * must show whole, in one colour (a broken cull shows inner triangles).
 * Stage 10 puts 64 triangles across the left and right screen edges (not
 * the near plane): drawn by VU1 inside the GS guard band and trimmed by the
 * scissor, so the edges must look cut straight, with nothing clipped in C. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <athena.h>
#include <athena/screen.h>
#include <athena/render3d.h>
#include <athena/draw.h>
#include <athena/tilemap.h>

#define FRAMES 180
#define WARMUP 60
#define COUNT 64
static const char *names[]={"depth","cull-back","cull-front","camera-tile-camera","recreate",
    "inside-batch","clip-batch","inside-individual","clip-individual","closed-cull","edge-batch"};
typedef struct { AthenaInstance3D *items[COUNT]; unsigned count; AthenaBatch3D *batch; } Scene;
static void release(Scene *s) {
    athena_batch3d_destroy(s->batch);
    for(unsigned i=0;i<s->count;i++) athena_instance3d_release(s->items[i]);
    memset(s,0,sizeof(*s));
}
static int make_mesh(const float *positions,const float color[4],AthenaMesh3D **out) {
    float colors[12]; for(int i=0;i<3;i++) memcpy(colors+i*4,color,4*sizeof(float));
    AthenaGeometry3D g={.positions=positions,.vertex_count=3,.colors=colors,.color_count=3};
    return athena_mesh3d_create(&g,out);
}
static int pair(Scene *s,int depth) {
    const float front[]={-.5,-.5,-2, .5,-.5,-2, 0,.5,-2};
    const float far[]={-1,-1,-4, 1,-1,-4, 0,1,-4};
    const float back[]={-.5,-.5,-2, 0,.5,-2, .5,-.5,-2};
    const float green[]={0,1,0,1},red[]={1,0,0,1},yellow[]={1,1,0,1},cyan[]={0,1,1,1};
    AthenaMesh3D *a=NULL,*b=NULL;
    int code=make_mesh(front,depth?green:yellow,&a);
    if(code==0) code=make_mesh(depth?far:back,depth?red:cyan,&b);
    if(code==0) {
        s->items[0]=athena_instance3d_create(a); s->items[1]=athena_instance3d_create(b); s->count=2;
        if(!s->items[0]||!s->items[1]) code=-2;
        else if(!depth&&(!athena_instance3d_set_position(s->items[0],-.8,0,-1)||
            !athena_instance3d_set_position(s->items[1],.8,0,-1))) code=-1;
    }
    athena_mesh3d_release(a); athena_mesh3d_release(b); return code;
}
/* Counter-clockwise from outside, as tools/make_3d_lighting_asset.js. */
static int cube(Scene *s) {
    static const float p[8][3]={{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
    static const unsigned char faces[6][6]={{0,2,1,0,3,2},{4,5,6,4,6,7},{0,1,5,0,5,4},
        {3,7,6,3,6,2},{0,4,7,0,7,3},{1,2,6,1,6,5}};
    static const float tint[6][4]={{1,0,0,1},{0,1,0,1},{0,0,1,1},{1,1,0,1},{0,1,1,1},{1,0,1,1}};
    float positions[36*3],colors[36*4];
    for(unsigned f=0;f<6;f++) for(unsigned k=0;k<6;k++) {
        memcpy(positions+(f*6+k)*3,p[faces[f][k]],sizeof(p[0])); memcpy(colors+(f*6+k)*4,tint[f],sizeof(tint[0]));
    }
    AthenaGeometry3D g={.positions=positions,.vertex_count=36,.colors=colors,.color_count=36};
    AthenaMesh3D *mesh=NULL; int code=athena_mesh3d_create(&g,&mesh);
    if(code<0) return code;
    s->items[0]=athena_instance3d_create(mesh); athena_mesh3d_release(mesh);
    if(!s->items[0]) return -2;
    s->count=1;
    return athena_instance3d_set_position(s->items[0],0,0,-3)&&athena_instance3d_set_scale(s->items[0],.6f,.6f,.6f)?0:-1;
}
/* crossing: 0 inside, 1 through the near plane, 2 across the screen edges. */
static int collection(Scene *s,int crossing,float edge) {
    const float inside[]={-.06,-.06,-2.5, .06,-.06,-2.5, 0,.06,-2.5};
    const float clip[]={-.06,-.06,-.4, .06,-.06,-2.5, 0,.06,-2.5},color[]={0,.8,1,1};
    const float wide[]={-.3,-.06,-2.5, .3,-.06,-2.5, 0,.06,-2.5},orange[]={1,.5,0,1};
    AthenaMesh3D *mesh=NULL;
    int code=make_mesh(crossing==2?wide:crossing?clip:inside,crossing==2?orange:color,&mesh);
    if(code<0) return code;
    s->batch=athena_batch3d_create();
    if(!s->batch) code=-2;
    for(unsigned i=0;code==0&&i<COUNT;i++) {
        AthenaInstance3D *item=athena_instance3d_create(mesh);
        if(!item) { code=-2; break; }
        s->items[s->count++]=item;
        /* Edges: alternately on the left and right edge at z = -2.5. */
        float x=crossing==2?(i&1?edge:-edge):((int)(i%8)-3.5f)*.12f,y=crossing==2?((int)(i/2)-15.5f)*.04f:((int)(i/8)-3.5f)*.12f;
        if(!athena_instance3d_set_position(item,x,y,0)) code=-1;
        else code=athena_batch3d_add(s->batch,item);
    }
    athena_mesh3d_release(mesh); return code;
}
static void add_stats(AthenaRender3DStats *a,const AthenaRender3DStats *b) {
    a->submitted_objects+=b->submitted_objects; a->culled_objects+=b->culled_objects;
    a->draw_passes+=b->draw_passes; a->pipeline_passes+=b->pipeline_passes; a->triangles+=b->triangles; a->vu_batches+=b->vu_batches;
    a->source_triangles+=b->source_triangles; a->clipped_triangles+=b->clipped_triangles;
    a->rejected_triangles+=b->rejected_triangles; a->geometry_bytes+=b->geometry_bytes;
    a->guard_band_objects+=b->guard_band_objects;
}
static int compare_ticks(const void *a,const void *b) {
    clock_t x=*(const clock_t *)a,y=*(const clock_t *)b; return (x>y)-(x<y);
}
static void report(unsigned stage,clock_t samples[FRAMES-WARMUP],const AthenaRender3DStats *totals) {
    const unsigned n=FRAMES-WARMUP; double mean=0;
    for(unsigned i=0;i<n;i++) mean+=samples[i];
    qsort(samples,n,sizeof(*samples),compare_ticks);
    printf("3D_BASELINE {\"stage\":\"%s\",\"samples\":%u,\"drawTicksMean\":%.3f,"
        "\"drawTicksP95\":%ld,\"drawTicksP99\":%ld,\"allocs\":%u,"
        "\"submittedObjectsPerFrame\":%.3f,\"culledObjectsPerFrame\":%.3f,\"drawPassesPerFrame\":%.3f,\"pipelinePassesPerFrame\":%.3f,"
        "\"sourceTrianglesPerFrame\":%.3f,\"trianglesPerFrame\":%.3f,\"clippedTrianglesPerFrame\":%.3f,"
        "\"rejectedTrianglesPerFrame\":%.3f,\"vuBatchesPerFrame\":%.3f,\"geometryBytesPerFrame\":%.3f,"
        "\"guardBandObjectsPerFrame\":%.3f}\n",
        names[stage],n,mean/n,(long)samples[(unsigned)ceil(n*.95)-1],(long)samples[(unsigned)ceil(n*.99)-1],
        (unsigned)get_allocs_size(),totals->submitted_objects/(double)n,totals->culled_objects/(double)n,
        totals->draw_passes/(double)n,totals->pipeline_passes/(double)n,totals->source_triangles/(double)n,totals->triangles/(double)n,
        totals->clipped_triangles/(double)n,totals->rejected_triangles/(double)n,
        totals->vu_batches/(double)n,totals->geometry_bytes/(double)n,totals->guard_band_objects/(double)n);
}
int athena_main(int argc,char **argv) {
    (void)argc; (void)argv;
    Scene scene={0}; int result=1; AthenaTileSprite *tile=NULL;
    graphics_service_init(); AthenaScreenMode mode;
    if(athena_screen_get_mode(&mode)!=ATHENA_SCREEN_OK) goto cleanup;
    int height=mode.height*((mode.interlace==GS_INTERLACED&&mode.field==GS_FRAME)?2:1);
    mode.height=height; mode.zbuffering=true; mode.psmz=GS_ZBUF_16S;
    if(athena_screen_set_mode(&mode,NULL)!=ATHENA_SCREEN_OK) goto cleanup;
    AthenaCamera3D camera,camera_b; athena_camera3d_init(&camera); athena_camera3d_init(&camera_b);
    if(!athena_camera3d_set_projection(&camera,60,(float)mode.width/height,1,20)||
       !athena_camera3d_look_at(&camera,0,0,-1)||!athena_camera3d_set_position(&camera,0,0,0)||
       !athena_camera3d_set_projection(&camera_b,60,(float)mode.width/height,1,20)||
       !athena_camera3d_look_at(&camera_b,.5,0,-1)||!athena_camera3d_set_position(&camera_b,.5,0,0)) goto cleanup;
    tile=athena_tilemap_buffer_alloc(1); if(!tile) goto cleanup;
    tile->w=32; tile->h=16; tile->r=255; tile->g=128; tile->a=128;
    AthenaTileMaterial material={.texture_index=ATHENA_TILEMAP_NO_TEXTURE,.end=0};
    clock_t samples[FRAMES-WARMUP];
    printf("3D regression: CLOCKS_PER_SEC=%ld; drawTicks exclude HUD, flip and reporting.\n",(long)CLOCKS_PER_SEC);
    for(unsigned stage=0;stage<sizeof(names)/sizeof(names[0]);stage++) {
        release(&scene);
        /* The screen edge at z = -2.5: tan(30 deg) * aspect * 2.5. */
        float edge=2.5f*tanf(30*3.14159265f/180)*(float)mode.width/height;
        int code=stage==9?cube(&scene):stage==10?collection(&scene,2,edge):
            stage>=5?collection(&scene,stage==6||stage==8,edge):pair(&scene,stage==0||stage==4);
        if(code<0) { printf("3D regression create error %d\n",code); goto cleanup; }
        printf("3D regression stage %u: %s\n",stage,names[stage]);
        AthenaRender3DStats totals={0};
        for(unsigned local=0;local<FRAMES;local++) {
            if(athena_modules_stop_requested()) { result=0; goto cleanup; }
            if(stage==4&&!scene.count&&pair(&scene,1)<0) goto cleanup;
            clearScreen(0); AthenaRender3DStats stats={0}; clock_t start=clock();
            AthenaRender3DCull cull=stage==1||stage==9?ATHENA_RENDER3D_CULL_BACK:stage==2?ATHENA_RENDER3D_CULL_FRONT:ATHENA_RENDER3D_CULL_NONE;
            if(stage==9&&!athena_instance3d_set_euler(scene.items[0],local*.03f,local*.02f,0)) goto cleanup;
            if(stage==5||stage==6||stage==10) code=athena_batch3d_draw(scene.batch,&camera,cull,&stats);
            else for(unsigned n=0;n<scene.count;n++) {
                unsigned index=(stage==0||stage==4)&&(local/30)%2?scene.count-1-n:n;
                code=athena_render3d_draw(scene.items[index],stage==3&&n==1?&camera_b:&camera,cull,&stats);
                if(code<0) break;
                if(stage==3&&n==0&&athena_tilemap_render(&material,1,NULL,0,tile,1,NULL,0,mode.width/2.0f-16,36)!=1) { code=-1; break; }
            }
            clock_t ticks=clock()-start;
            if(code<0) { printf("3D regression draw error %d\n",code); goto cleanup; }
            if(local>=WARMUP) { samples[local-WARMUP]=ticks; add_stats(&totals,&stats); }
            if(stage==4&&local%30==29) release(&scene); /* Pending inline DMA outlives these handles. */
            for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++)
                draw_sprite(12+i*20,12,14,14,athena_color_new(i==stage?255:64,i==stage?128:64,i==stage?0:64,128));
            draw_sprite(12,height-20,(mode.width-24)*(local+1)/FRAMES,8,athena_color_new(255,255,255,128));
            if(local==FRAMES-1) report(stage,samples,&totals);
            flipScreen();
        }
    }
    puts("3D regression complete; inspect images before recording visual PASS."); result=0;
cleanup:
    release(&scene);
    if(tile) { athena_tilemap_sync(); free(tile); }
    return result;
}
