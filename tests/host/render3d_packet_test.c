#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <athena/render3d.h>
#include <athena/graphics.h>
#include <athena/graphics/owl_packet.h>
#include <athena/graphics/mpg_manager.h>
static owl_qword memory[512];
static int active[CHANNEL_SIZE],waits[CHANNEL_SIZE],sends[CHANNEL_SIZE];
static owl_qword *reading[CHANNEL_SIZE];
static int parse_chains,position_chunks,color_chunks,clipped_chunks,clip_mode,normal_chunks,light_uploads,uv_chunks,textured_tags;
static uint32_t geometry_bytes;
static GSCONTEXT gs={.ZBuffering=1,.Width=640,.Height=448,.PSMZ=GS_ZBUF_16S};
static vu_mpg mpg;
static unsigned program_loads,program_unloads;
static uint64_t registers[35];
uint64_t get_register(int id) { return registers[id]; }
void set_register(int id,uint64_t value) {
    if(registers[id]==value) return;
    owl_packet *p=owl_query_packet(CHANNEL_VIF1,1);
    owl_add_cnt_tag(p,0,0); registers[id]=value;
}
void set_register_force(int id,uint64_t value) {
    owl_packet *p=owl_query_packet(CHANNEL_VIF1,1);
    owl_add_cnt_tag(p,0,0); registers[id]=value;
}
void graphics_service_init(void) {}
GSCONTEXT *getGSGLOBAL(void) { return &gs; }
void SyncDCache(void *a,void *b) { (void)a; (void)b; }
void dmaKit_wait(owl_channel c,int flag) { (void)flag; assert(c<CHANNEL_SIZE); active[c]=0; waits[c]++; }
void dmaKit_send_chain_ucab(owl_channel c,void *data) {
    assert(!active[c]); active[c]=1; reading[c]=data; sends[c]++;
    if(!parse_chains) return;
    owl_qword *p=data;
    for(;;) {
        unsigned count=p->sword[0]&0xffffu, tag=(p->sword[0]>>28)&7u;
        assert(tag==DMA_CNT||tag==DMA_END); /* Geometry must never be DMA_REF. */
        unsigned code=p->sword[3], cmd=code>>24, dest=code&0x3ffu, vertices=(code>>16)&255u;
        if(cmd==0x6c && dest==26) { assert(count==1); clip_mode=p[1].sword[1]; }
        if(cmd==0x68 && dest==2) {
            assert(!clip_mode);
            assert(vertices && vertices<=48 && vertices%4==0 && count==vertices*12/16);
            position_chunks++;
            geometry_bytes+=count*16;
        }
        if(cmd==0x6c && dest==2) {
            assert(clip_mode==1 && vertices && vertices<=48 && vertices%4==0 && count==vertices);
            for(unsigned i=0;i<vertices;i++) {
                const float *v=p[1+i].f;
                assert(isfinite(v[3]) && v[3]>=0);
                if(v[3]>0) for(unsigned j=0;j<3;j++) assert(fabsf(v[j])<=v[3]+0.00001f);
            }
            clipped_chunks++;
            geometry_bytes+=count*16;
        }
        if(cmd==0x6e && dest==98) {
            assert(vertices && vertices<=48 && vertices%4==0 && count==vertices/4);
            color_chunks++;
            geometry_bytes+=count*16;
        }
        if(cmd==0x68 && dest==50) {
            assert(!clip_mode&&vertices&&vertices<=48&&vertices%4==0&&count==vertices*12/16);
            const float *normals=(const float *)(p+1);
            for(unsigned i=0;i<vertices;i++) {
                double length=0;
                for(unsigned j=0;j<3;j++) { assert(isfinite(normals[i*3+j])); length+=normals[i*3+j]*normals[i*3+j]; }
                assert(length==0||fabs(length-1)<1e-5); /* padding is zero */
            }
            normal_chunks++; geometry_bytes+=count*16;
        }
        if(cmd==0x64&&dest==146) {
            assert(vertices&&vertices<=48&&vertices%4==0&&count==vertices/2);
            const float *uv=(const float *)(p+1);
            for(unsigned i=0;i<vertices*2;i++) assert(isfinite(uv[i])&&uv[i]>=0&&uv[i]<=1);
            uv_chunks++; geometry_bytes+=count*16;
        }
        if(cmd==0x6c&&dest==0&&(code&(1u<<15))) {
            assert(count==1);
            giftag_t giftag={.data=p[1].dword[0]}; prim_reg_t prim={.data=giftag.PRIM};
            if(prim.TME) { assert(!prim.FST); textured_tags++; }
        }
        if(cmd==0x6c&&dest==10) {
            assert(count==6&&p[6].sword[3]<=4);
            for(unsigned i=0;i<4;i++) {
                const float *direction=p[1+i].f;
                float length=direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2];
                assert(length==0||fabsf(length-1)<1e-5);
            }
            light_uploads++;
        }
        p+=1+count; assert(p<=memory+512);
        if(tag==DMA_END) break;
    }
}
vu_mpg *vu_mpg_load_buffer(void *p,uint32_t s,int unit,bool owned) { (void)p;(void)s;(void)unit;(void)owned; program_loads++; return &mpg; }
int vu_mpg_preload(vu_mpg *p,bool dma) { (void)p;(void)dma; return 0; }
void vu_mpg_unload(vu_mpg *p) { (void)p; program_unloads++; }
void vu1_invalidate_static_data(void) {}
static void write_packet(owl_channel channel) {
    owl_packet *p=owl_query_packet(channel,1);
    for(int c=0;c<CHANNEL_SIZE;c++)
        assert(!active[c] || p->ptr<reading[c] || p->ptr>=reading[c]+256);
    owl_add_cnt_tag(p,0,0);
}
int main(void) {
    owl_init(memory,512);
    write_packet(CHANNEL_GIF); uint64_t first=owl_flush_generation(); owl_flush_packet();
    assert(!owl_generation_read(first));
    write_packet(CHANNEL_VIF1); uint64_t second=owl_flush_generation(); owl_flush_packet();
    assert(waits[CHANNEL_GIF]>=2); /* Previous half drained before reuse via another channel. */
    assert(owl_generation_read(first));
    assert(!owl_generation_read(second));
    owl_wait_generation(second); assert(owl_generation_read(second));
    write_packet(CHANNEL_GIF); uint64_t current=owl_flush_generation();
    owl_wait_generation(current); assert(owl_generation_read(current));
    int old_waits=waits[CHANNEL_GIF]; owl_wait_generation(current); assert(waits[CHANNEL_GIF]==old_waits);
    current=owl_flush_generation(); owl_wait_generation(current);
    assert(owl_generation_read(current)); /* Empty generations also close on wait. */
    write_packet(CHANNEL_VIF1); owl_flush_packet();
    write_packet(CHANNEL_VIF1); owl_flush_packet();
    assert(active[CHANNEL_VIF1]); /* Same-channel double buffering retains CPU/DMA overlap. */
    owl_wait_generation(owl_flush_generation());
    owl_init(memory,512); parse_chains=1;
    registers[GS_CACHE_TEST]=123; registers[GS_CACHE_ZBUF]=1ull<<32;
    /* 51 vertices crosses a 48-vertex chunk boundary and exercises padding. */
    float positions[51*3]={0};
    for(unsigned i=0;i<51;i++) positions[i*3]=(i%3)*0.1f;
    AthenaGeometry3D geometry={.positions=positions,.vertex_count=51};
    AthenaMesh3D *mesh=NULL; assert(!athena_mesh3d_create(&geometry,&mesh));
    AthenaInstance3D *instance=athena_instance3d_create(mesh); athena_mesh3d_release(mesh);
    AthenaCamera3D camera; athena_camera3d_init(&camera);
    AthenaRender3DStats stats={0};
    assert(!athena_render3d_draw(instance,&camera,ATHENA_RENDER3D_CULL_NONE,&stats));
    owl_controller *control=owl_get_controller();
    extern owl_packet internal_packet;
    assert((size_t)(internal_packet.ptr-control->base-(control->context?control->size:0))==control->alloc);
    assert(stats.triangles==17 && stats.vu_batches==2 && stats.source_triangles==17 && !stats.clipped_triangles);
    assert(registers[GS_CACHE_TEST]==123 && registers[GS_CACHE_ZBUF]==(1ull<<32));
    /* Dispose before flush: inline geometry must remain readable in the packet. */
    athena_instance3d_release(instance);
    owl_wait_generation(owl_flush_generation());
    assert(position_chunks==2 && color_chunks==2);
    assert(stats.geometry_bytes==52*16 && geometry_bytes==stats.geometry_bytes);
    geometry_bytes=0;
    const float crossing[]={-.1,-.1,.5f, .3f,-.1,-2, -.1,.3f,-2};
    float crossing_positions[54*3];
    for(unsigned i=0;i<18;i++) memcpy(&crossing_positions[i*9],crossing,sizeof(crossing));
    geometry=(AthenaGeometry3D){.positions=crossing_positions,.vertex_count=54};
    assert(!athena_mesh3d_create(&geometry,&mesh));
    instance=athena_instance3d_create(mesh); athena_mesh3d_release(mesh);
    assert(athena_camera3d_set_projection(&camera,60,1,1,10));
    assert(athena_camera3d_look_at(&camera,0,0,-1));
    assert(athena_camera3d_set_position(&camera,0,0,0));
    stats=(AthenaRender3DStats){0};
    assert(!athena_render3d_draw(instance,&camera,ATHENA_RENDER3D_CULL_NONE,&stats));
    control=owl_get_controller();
    assert((size_t)(internal_packet.ptr-control->base-(control->context?control->size:0))==control->alloc);
    assert(stats.source_triangles==18 && stats.clipped_triangles==18 && stats.triangles==36 && stats.vu_batches==3);
    assert(registers[GS_CACHE_TEST]==123 && registers[GS_CACHE_ZBUF]==(1ull<<32));
    athena_instance3d_release(instance); /* Clipped scratch must have been copied into DMA. */
    owl_wait_generation(owl_flush_generation());
    assert(clipped_chunks==3 && position_chunks==2 && color_chunks==5);
    assert(stats.geometry_bytes==108*20 && geometry_bytes==stats.geometry_bytes);
    athena_render3d_module_shutdown();
    assert(program_loads==1 && program_unloads==1);
    /* Recreate resources/programs repeatedly, comparing Batch and individual
     * draws through the production packet path and a second camera/context. */
    const float small[]={-.1,-.1,-3, .1,-.1,-3, 0,.1,-3};
    geometry=(AthenaGeometry3D){.positions=small,.vertex_count=3};
    AthenaCamera3D camera_b=camera;
    assert(athena_camera3d_set_position(&camera_b,.1,0,0));
    assert(athena_camera3d_look_at(&camera_b,.1,0,-1));
    registers[GS_CACHE_TEST+1]=456; registers[GS_CACHE_ZBUF+1]=1ull<<32;
    for(unsigned cycle=0;cycle<12;cycle++) {
        geometry_bytes=0;
        assert(!athena_mesh3d_create(&geometry,&mesh));
        instance=athena_instance3d_create(mesh);
        AthenaInstance3D *outside=athena_instance3d_create(mesh);
        assert(instance && outside && athena_instance3d_set_position(outside,1000,0,0));
        AthenaBatch3D *batch=athena_batch3d_create(); assert(batch);
        assert(!athena_batch3d_add(batch,instance) && !athena_batch3d_add(batch,outside));
        athena_mesh3d_release(mesh);
        AthenaRender3DStats individual={0},batched;
        assert(!athena_render3d_draw(instance,&camera,ATHENA_RENDER3D_CULL_BACK,&individual));
        assert(!athena_render3d_draw(outside,&camera,ATHENA_RENDER3D_CULL_BACK,&individual));
        athena_instance3d_release(instance); athena_instance3d_release(outside);
        /* Batch owns the only remaining references. Changing GS context and
         * camera must preserve the caller's independent depth states. */
        gs.PrimContext=1;
        assert(!athena_batch3d_draw(batch,&camera_b,ATHENA_RENDER3D_CULL_FRONT,&batched));
        assert(individual.submitted_objects==2 && individual.culled_objects==1 && individual.geometry_bytes==64);
        assert(batched.submitted_objects==individual.submitted_objects && batched.culled_objects==individual.culled_objects);
        assert(batched.triangles==individual.triangles && batched.geometry_bytes==individual.geometry_bytes);
        assert(registers[GS_CACHE_TEST]==123 && registers[GS_CACHE_ZBUF]==(1ull<<32));
        assert(registers[GS_CACHE_TEST+1]==456 && registers[GS_CACHE_ZBUF+1]==(1ull<<32));
        athena_batch3d_clear(batch);
        assert(!athena_batch3d_draw(batch,&camera_b,ATHENA_RENDER3D_CULL_NONE,&batched));
        assert(!batched.submitted_objects && !batched.geometry_bytes);
        athena_batch3d_destroy(batch); /* Release before pending DMA is flushed. */
        athena_render3d_module_shutdown();
        assert(geometry_bytes==128);
        athena_render3d_module_shutdown(); /* Idempotent, then reload next cycle. */
        assert(program_loads==cycle+2 && program_unloads==cycle+2);
        gs.PrimContext=0;
    }
    /* Alternate the new diffuse program with the existing clipping/color
     * program. Disposing lights and geometry before DMA is safe: all streams
     * and uniforms must already belong to the packet. */
    geometry_bytes=0;
    AthenaMaterial3D material; athena_material3d_default(&material); material.shading=ATHENA_MATERIAL3D_DIFFUSE;
    geometry=(AthenaGeometry3D){.positions=small,.vertex_count=3,.material=&material};
    assert(!athena_mesh3d_create(&geometry,&mesh)); instance=athena_instance3d_create(mesh); athena_mesh3d_release(mesh);
    AthenaLights *lights=athena_lights_create(); assert(lights);
    assert(athena_lights_set_ambient(lights,.25f,.25f,.25f));
    assert(athena_lights_set_directional(lights,3,0,0,4,.75f,.75f,.75f));
    stats=(AthenaRender3DStats){0};
    assert(!athena_render3d_draw_lit(instance,&camera,lights,ATHENA_RENDER3D_CULL_NONE,&stats));
    assert(stats.geometry_bytes==112);
    assert((size_t)(internal_packet.ptr-control->base-(control->context?control->size:0))==control->alloc);
    athena_instance3d_release(instance);
    geometry=(AthenaGeometry3D){.positions=crossing,.vertex_count=3,.material=&material};
    assert(!athena_mesh3d_create(&geometry,&mesh)); instance=athena_instance3d_create(mesh); athena_mesh3d_release(mesh);
    stats=(AthenaRender3DStats){0};
    assert(!athena_render3d_draw_lit(instance,&camera,lights,ATHENA_RENDER3D_CULL_NONE,&stats));
    assert(stats.clipped_triangles==1&&stats.triangles==2&&stats.geometry_bytes==160);
    athena_instance3d_release(instance);
    geometry=(AthenaGeometry3D){.positions=small,.vertex_count=3,.material=&material};
    assert(!athena_mesh3d_create(&geometry,&mesh)); instance=athena_instance3d_create(mesh); athena_mesh3d_release(mesh);
    athena_lights_clear(lights);
    stats=(AthenaRender3DStats){0};
    assert(!athena_render3d_draw_lit(instance,&camera,lights,ATHENA_RENDER3D_CULL_NONE,&stats));
    athena_lights_destroy(lights); athena_instance3d_release(instance);
    assert(registers[GS_CACHE_TEST]==123&&registers[GS_CACHE_ZBUF]==(1ull<<32));
    athena_render3d_module_shutdown();
    assert(normal_chunks==2&&light_uploads==2&&geometry_bytes==384);
    assert(program_loads==15&&program_unloads==15);
    /* Texture UV streams remain inline after releasing every mesh/instance.
     * Switch contexts and programs while preserving all cached GS registers. */
    uint32_t pixel=255; AthenaTexture3DPixels pixels={.width=1,.height=1,.pixels=&pixel,.pixel_count=1};
    AthenaTexture3D *texture=NULL; assert(!athena_texture3d_create(&pixels,&texture));
    material.texture=texture; float uv[6]={0,1,1,1,.5,0};
    geometry_bytes=0;
    for(unsigned draw=0;draw<4;draw++) {
        gs.PrimContext=draw%2;
        registers[GS_CACHE_TEX0+gs.PrimContext]=42; registers[GS_CACHE_TEX1+gs.PrimContext]=99;
        registers[GS_CACHE_CLAMP+gs.PrimContext]=1234;
        material.shading=draw/2;
        geometry=(AthenaGeometry3D){.positions=draw%2?crossing:small,.vertex_count=3,
            .texcoords=uv,.texcoord_count=3,.material=&material};
        assert(!athena_mesh3d_create(&geometry,&mesh)); instance=athena_instance3d_create(mesh); athena_mesh3d_release(mesh);
        stats=(AthenaRender3DStats){0};
        assert(!athena_render3d_draw_lit(instance,&camera,NULL,ATHENA_RENDER3D_CULL_NONE,&stats));
        assert(stats.geometry_bytes==(draw%2?224:draw/2?144:96));
        assert(registers[GS_CACHE_TEX0+gs.PrimContext]==42&&registers[GS_CACHE_TEX1+gs.PrimContext]==99&&registers[GS_CACHE_CLAMP+gs.PrimContext]==1234);
        athena_instance3d_release(instance);
    }
    athena_texture3d_release(texture); athena_render3d_module_shutdown();
    assert(uv_chunks==4&&textured_tags==4&&geometry_bytes==688);
    assert(program_loads==16&&program_unloads==16);
    puts("3D packets and cross-channel DMA lifetime tests passed"); return 0;
}
