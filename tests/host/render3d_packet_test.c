#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <athena/render3d.h>
#include <athena/graphics.h>
#include <athena/graphics/owl_packet.h>
#include <athena/graphics/mpg_manager.h>
extern unsigned texture3d_host_destroyed;
static owl_qword memory[8192];
static int active[CHANNEL_SIZE],waits[CHANNEL_SIZE],sends[CHANNEL_SIZE];
static owl_qword *reading[CHANNEL_SIZE];
static int parse_pending[CHANNEL_SIZE];
static const void *dma_addresses[32768];
static uint32_t dma_address_count,ref_bytes,ref_tags;
uint32_t athena_test_dma_address(uintptr_t address) {
    assert(address&&!(address&15));
    assert(dma_address_count<32768);
    dma_addresses[dma_address_count++]=(const void *)address;
    return dma_address_count;
}
static int flusha_tags,flushe_tags,model_uploads;
static int parse_chains,position_chunks,color_chunks,clipped_chunks,clip_mode,normal_chunks,light_uploads,uv_chunks,textured_tags;
static uint32_t geometry_bytes;
static unsigned indexed_mode,indexed_unique,indexed_chunks,indexed_config_uploads;
static owl_qword indexed_config;
static unsigned skin_normals,skin_weights,skin_uvs;
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
static void parse_chain(owl_qword *p);
void dmaKit_wait(owl_channel c,int flag) {
    (void)flag; assert(c<CHANNEL_SIZE);
    /* Emulate reading after caller disposal, not when the chain is authored. */
    if(active[c]&&parse_pending[c]) parse_chain(reading[c]);
    active[c]=0; waits[c]++;
}
void dmaKit_send_chain_ucab(owl_channel c,void *data) {
    assert(!active[c]); active[c]=1; reading[c]=data; sends[c]++;
    parse_pending[c]=parse_chains;
}
static void parse_chain(owl_qword *p) {
    for(;;) {
        unsigned count=p->sword[0]&0xffffu, tag=(p->sword[0]>>28)&7u;
        assert(tag==DMA_CNT||tag==DMA_END||tag==DMA_REF);
        const owl_qword *payload=p+1;
        if(tag==DMA_REF) {
            assert(ATHENA_RENDER3D_DMA_REF);
            uint32_t token=p->sword[1]; assert(token&&token<=dma_address_count);
            payload=dma_addresses[token-1];
            /* Read the whole external payload under ASan, including padding. */
            volatile unsigned checksum=0;
            for(unsigned i=0;i<count*16;i++) checksum+=((const uint8_t *)payload)[i];
            (void)checksum; ref_bytes+=count*16; ref_tags++;
        }
        unsigned code=p->sword[3], cmd=code>>24, dest=code&0x3ffu, vertices=(code>>16)&255u;
        if(cmd==0x13) flusha_tags++;
        if(cmd==0x10) flushe_tags++;
        if(cmd==0x6c && dest==5 && !(code&(1u<<15))) { assert(count==4); model_uploads++; }
        if(cmd==0x6c && dest==26) { assert(count==1); clip_mode=payload[0].sword[1]; indexed_mode=0; indexed_config_uploads++; indexed_config=payload[0]; }
        if(cmd==0x6c&&dest==1&&(code&(1u<<15))) {
            assert(count==1);indexed_unique=payload[0].sword[0];
            assert(indexed_unique&&indexed_unique<=24);indexed_mode=1;
        }
        if(indexed_mode&&(code&(1u<<15))&&cmd!=0x6c) {
            assert(vertices&&vertices%4==0);
            if(cmd==0x62&&dest==98) {
                assert(vertices==48&&count==3);indexed_chunks++;
                const uint8_t *indices=(const uint8_t *)payload;
                for(unsigned i=0;i<48;i++)assert(indices[i]<indexed_unique);
            } else {
                assert(vertices<=24);
                assert((cmd==0x68&&(dest==2||dest==26||(dest>=146&&dest<=194)))||
                    (cmd==0x6e&&(dest==50||dest==146||dest==170))||
                    (cmd==0x64&&dest==74));
                unsigned bytes=cmd==0x68?12:cmd==0x64?8:4;assert(count*16==vertices*bytes);
                if(cmd==0x68||cmd==0x64)for(unsigned i=0;i<count*4;i++)assert(isfinite(payload[i/4].f[i%4]));
                if(dest==2)position_chunks++;
                if(cmd==0x6e&&dest==50)color_chunks++;
            }
            geometry_bytes+=count*16;
            p+=1+(tag==DMA_REF?0:count);assert(p<=memory+owl_get_controller()->size*2);continue;
        }
        if(cmd==0x68 && dest==2) {
            assert(!clip_mode);
            assert(vertices && vertices<=48 && vertices%4==0 && count==vertices*12/16);
            position_chunks++;
            geometry_bytes+=count*16;
        }
        if(cmd==0x6c && dest==2) {
            assert(clip_mode==1 && vertices && vertices<=48 && vertices%4==0 && count==vertices);
            for(unsigned i=0;i<vertices;i++) {
                const float *v=payload[i].f;
                assert(isfinite(v[3]) && v[3]>=0);
                if(v[3]>0) for(unsigned j=0;j<3;j++) assert(fabsf(v[j])<=v[3]+0.00001f);
            }
            clipped_chunks++;
            geometry_bytes+=count*16;
        }
        if(cmd==0x6e && (dest==98||dest==66)) {
            assert(vertices && vertices<=48 && vertices%4==0 && count==vertices/4);
            color_chunks++;
            geometry_bytes+=count*16;
        }
        if(cmd==0x68 && dest==50) {
            assert(!clip_mode&&vertices&&vertices<=48&&vertices%4==0&&count==vertices*12/16);
            const float *normals=(const float *)payload;
            for(unsigned i=0;i<vertices;i++) {
                double length=0;
                for(unsigned j=0;j<3;j++) { assert(isfinite(normals[i*3+j])); length+=normals[i*3+j]*normals[i*3+j]; }
                assert(length==0||fabs(length-1)<1e-5); /* padding is zero */
            }
            normal_chunks++; geometry_bytes+=count*16;
        }
        if(cmd==0x64&&dest==146) {
            assert(vertices&&vertices<=48&&vertices%4==0&&count==vertices/2);
            const float *uv=(const float *)payload;
            for(unsigned i=0;i<vertices*2;i++) assert(isfinite(uv[i])&&uv[i]>=0&&uv[i]<=1);
            uv_chunks++; geometry_bytes+=count*16;
        }
        if(cmd==0x68&&dest==34) {
            assert(vertices&&vertices<=32&&vertices%4==0&&count==vertices*12/16);
            const float *n=(const float *)payload;
            for(unsigned i=0;i<vertices*3;i++) assert(isfinite(n[i]));
            skin_normals++; geometry_bytes+=count*16;
        }
        if(cmd==0x6e&&dest==130) {
            assert(vertices&&vertices<=32&&vertices%4==0&&count==vertices/4);
            const uint8_t *weights=(const uint8_t *)payload;
            for(unsigned i=0;i<vertices;i++) {
                unsigned sum=0; for(unsigned j=0;j<4;j++) sum+=weights[i*4+j];
                assert(sum==255||sum==0); /* Padding is zero. */
            }
            skin_weights++; geometry_bytes+=count*16;
        }
        if(cmd==0x64&&dest==162) {
            assert(vertices&&vertices<=32&&vertices%4==0&&count==vertices/2);
            const float *uv=(const float *)payload;
            for(unsigned i=0;i<vertices*2;i++) assert(isfinite(uv[i]));
            skin_uvs++; geometry_bytes+=count*16;
        }
        if(cmd==0x6c&&dest==0&&(code&(1u<<15))) {
            assert(count==1);
            giftag_t giftag={.data=payload[0].dword[0]}; prim_reg_t prim={.data=giftag.PRIM};
            if(prim.TME) { assert(!prim.FST); textured_tags++; }
        }
        if(cmd==0x6c&&dest==10) {
            assert(count==6&&payload[5].sword[3]<=4);
            for(unsigned i=0;i<4;i++) {
                const float *direction=payload[i].f;
                float length=direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2];
                assert(length==0||fabsf(length-1)<1e-5);
            }
            light_uploads++;
        }
        p+=1+(tag==DMA_REF?0:count);
        assert(p<=memory+owl_get_controller()->size*2);
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
    owl_packet_stats transport; owl_packet_stats_read(&transport);
    assert(transport.queries==2&&transport.flushes==2&&transport.submitted_qwords==4);
    assert(transport.channel_flushes==0&&transport.capacity_flushes==0);
    assert(transport.reuse_waits==1&&transport.fence_waits==1&&!transport.submit_waits);
    assert(transport.peak_half_qwords==2);
    owl_packet_stats_reset(); owl_packet_stats_read(&transport);
    assert(!transport.queries&&!transport.flushes&&!transport.submitted_qwords&&!transport.peak_half_qwords);
    write_packet(CHANNEL_GIF); uint64_t current=owl_flush_generation();
    owl_wait_generation(current); assert(owl_generation_read(current));
    int old_waits=waits[CHANNEL_GIF]; owl_wait_generation(current); assert(waits[CHANNEL_GIF]==old_waits);
    current=owl_flush_generation(); owl_wait_generation(current);
    assert(owl_generation_read(current)); /* Empty generations also close on wait. */
    write_packet(CHANNEL_VIF1); owl_flush_packet();
    write_packet(CHANNEL_VIF1); owl_flush_packet();
    assert(active[CHANNEL_VIF1]); /* Same-channel double buffering retains CPU/DMA overlap. */
    owl_wait_generation(owl_flush_generation());
    owl_packet_stats_reset();
    write_packet(CHANNEL_GIF); write_packet(CHANNEL_VIF1);
    owl_wait_generation(owl_flush_generation());
    owl_packet_stats_read(&transport);
    assert(transport.channel_flushes==1&&!transport.capacity_flushes);
    assert(transport.queries==2&&transport.flushes==2&&transport.submitted_qwords==4);
    write_packet(CHANNEL_VIF1); owl_packet_stats_reset();
    owl_packet_stats_read(&transport);
    assert(!transport.queries&&transport.peak_half_qwords==2); /* Reset keeps authored data/ticket. */
    owl_wait_generation(owl_flush_generation());
    current=owl_flush_generation();
    owl_init(memory,512);
    assert(owl_flush_generation()>current);
    assert(owl_generation_read(current)); /* Reset closes the drained stream without reusing tickets. */
    parse_chains=1;
    registers[GS_CACHE_TEST]=123; registers[GS_CACHE_ZBUF]=1ull<<32;
    /* 51 vertices crosses a 48-vertex chunk boundary and exercises padding. */
    float positions[51*3]={0};
    for(unsigned i=0;i<51;i++) positions[i*3]=(i%3)*0.1f;
    AthenaGeometry3D geometry={.positions=positions,.vertex_count=51};
    AthenaMesh3D *mesh=NULL; assert(!athena_mesh3d_create(&geometry,&mesh));
    current=owl_flush_generation();
    assert(athena_mesh3d_dma_claim(mesh,current));
    assert(!athena_mesh3d_dma_claim(mesh,current));
    owl_wait_generation(current);
    owl_init(memory,512);
    assert(owl_flush_generation()>current);
    assert(athena_mesh3d_dma_claim(mesh,owl_flush_generation()));
    owl_wait_generation(owl_flush_generation()); /* Leave a new ticket for the first draw. */
    AthenaInstance3D *instance=athena_instance3d_create(mesh); athena_mesh3d_release(mesh);
    AthenaCamera3D camera; athena_camera3d_init(&camera);
    AthenaRender3DStats stats={0};
    assert(!athena_render3d_draw(instance,&camera,ATHENA_RENDER3D_CULL_NONE,&stats));
    owl_controller *control=owl_get_controller();
    extern owl_packet internal_packet;
    assert((size_t)(internal_packet.ptr-control->base-(control->context?control->size:0))==control->alloc);
    assert(stats.triangles==17 && stats.vu_batches==2 && stats.source_triangles==17 && !stats.clipped_triangles);
    assert(registers[GS_CACHE_TEST]==123 && registers[GS_CACHE_ZBUF]==(1ull<<32));
    /* Dispose before flush: both inline and referenced streams must remain readable. */
    athena_instance3d_release(instance);
    owl_wait_generation(owl_flush_generation());
    assert(position_chunks==2 && color_chunks==2);
    assert(stats.geometry_bytes==52*16 && geometry_bytes==stats.geometry_bytes);
    assert(ref_tags==(ATHENA_RENDER3D_DMA_REF?4u:0u));
    assert(ref_bytes==(ATHENA_RENDER3D_DMA_REF?52u*16:0u));
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
    /* A 4096-pixel screen leaves no guard band (factor 1): the C clipper. */
    gs.Width=4096;
    stats=(AthenaRender3DStats){0};
    assert(!athena_render3d_draw(instance,&camera,ATHENA_RENDER3D_CULL_NONE,&stats));
    control=owl_get_controller();
    assert((size_t)(internal_packet.ptr-control->base-(control->context?control->size:0))==control->alloc);
    assert(stats.source_triangles==18 && stats.clipped_triangles==18 && stats.triangles==36 && stats.vu_batches==3);
    assert(!stats.near_clip_objects && !stats.guard_band_objects);
    assert(registers[GS_CACHE_TEST]==123 && registers[GS_CACHE_ZBUF]==(1ull<<32));
    owl_wait_generation(owl_flush_generation());
    assert(clipped_chunks==3 && position_chunks==2 && color_chunks==5);
    assert(stats.geometry_bytes==108*20 && geometry_bytes==stats.geometry_bytes);
    /* With the guard band, only the near plane cuts it: VU1 clips it, in
     * batches of at most 24 untransformed vertices (24 + 24 + 6). */
    gs.Width=640; geometry_bytes=0;
    stats=(AthenaRender3DStats){0};
    assert(!athena_render3d_draw(instance,&camera,ATHENA_RENDER3D_CULL_NONE,&stats));
    assert(stats.near_clip_objects==1 && !stats.clipped_triangles && stats.vu_batches==3);
    assert(stats.source_triangles==18 && stats.triangles==18);
    athena_instance3d_release(instance); /* Inline geometry must have been copied into DMA. */
    owl_wait_generation(owl_flush_generation());
    assert(stats.geometry_bytes==(24+24+8)*16 && geometry_bytes==stats.geometry_bytes);
    /* VU1 morph: 66 untextured unlit vertices with 5 targets, 2 active:
     * batches of 33, positions 12 + colours 4 + 2 deltas 24 bytes each. */
    {
        float mp[66*3],deltas[5*66*3];
        for(unsigned i=0;i<66;i++) { mp[i*3]=(i%3)*.1f; mp[i*3+1]=(i%3==2)*.1f; mp[i*3+2]=-3; }
        for(unsigned i=0;i<5*66*3;i++) deltas[i]=.01f;
        AthenaGeometry3D mg={.positions=mp,.vertex_count=66,.target_positions=deltas,.target_count=5};
        AthenaMesh3D *morph_mesh=NULL; assert(!athena_mesh3d_create(&mg,&morph_mesh));
        AthenaMesh3DView mv; athena_mesh3d_view(morph_mesh,&mv);
        AthenaMatrix4 identity; ath_matrix4_identity(&identity);
        const float two[5]={.5f,0,0,.25f,0},five[5]={.1f,.1f,.1f,.1f,.1f};
        geometry_bytes=0; stats=(AthenaRender3DStats){0};
        assert(athena_render3d_draw_morph(&mv,&identity,two,&camera,NULL,ATHENA_RENDER3D_CULL_NONE,&stats)==0);
        assert(stats.vu_morph_objects==1&&stats.vu_batches==2&&stats.submitted_objects==1&&stats.triangles==22);
        assert(stats.geometry_bytes==(36+36)*(12+4+24));
        stats=(AthenaRender3DStats){0};
        assert(athena_render3d_draw_morph(&mv,&identity,five,&camera,NULL,ATHENA_RENDER3D_CULL_NONE,&stats)==1);
        assert(!stats.submitted_objects&&!stats.vu_batches); /* the caller blends on the EE */
        athena_mesh3d_release(morph_mesh);
        owl_wait_generation(owl_flush_generation());
    }
    /* The checks below exercise the C clipper: no guard band from here. */
    gs.Width=4096;
    athena_render3d_module_shutdown();
    assert(program_loads==3 && program_unloads==3); /* the colour, near and morph programs */
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
        assert(program_loads==cycle+4 && program_unloads==cycle+4);
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
    assert(program_loads==17&&program_unloads==17); /* 15 plus the near and morph programs once */
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
    assert(program_loads==18&&program_unloads==18);
    /* Grouping: a Batch shares one pass among consecutive objects of the same
     * pipeline. Contained and clipped unlit objects both use the color
     * program; a diffuse object between them needs its own pass. */
    gs.PrimContext=0;
    AthenaMaterial3D unlit; athena_material3d_default(&unlit);
    athena_material3d_default(&material); material.shading=ATHENA_MATERIAL3D_DIFFUSE;
    AthenaMesh3D *inside_mesh=NULL,*crossing_mesh=NULL,*lit_mesh=NULL;
    geometry=(AthenaGeometry3D){.positions=small,.vertex_count=3,.material=&unlit};
    assert(!athena_mesh3d_create(&geometry,&inside_mesh));
    geometry=(AthenaGeometry3D){.positions=crossing,.vertex_count=3,.material=&unlit};
    assert(!athena_mesh3d_create(&geometry,&crossing_mesh));
    geometry=(AthenaGeometry3D){.positions=small,.vertex_count=3,.material=&material};
    assert(!athena_mesh3d_create(&geometry,&lit_mesh));
    AthenaBatch3D *batch=athena_batch3d_create(); assert(batch);
    AthenaMesh3D *order[]={inside_mesh,crossing_mesh,inside_mesh,inside_mesh};
    for(unsigned i=0;i<4;i++) {
        instance=athena_instance3d_create(order[i]); assert(instance);
        assert(athena_instance3d_set_position(instance,i*.01f,0,0));
        assert(!athena_batch3d_add(batch,instance)); athena_instance3d_release(instance);
    }
    owl_wait_generation(owl_flush_generation());
    flusha_tags=flushe_tags=model_uploads=0; clipped_chunks=position_chunks=0;
    assert(!athena_batch3d_draw(batch,&camera,ATHENA_RENDER3D_CULL_NONE,&stats));
    owl_wait_generation(owl_flush_generation());
    assert(stats.draw_passes==4&&stats.pipeline_passes==1&&stats.clipped_triangles==1);
    assert(flusha_tags==2&&flushe_tags==3&&model_uploads==4);
    assert(position_chunks==3&&clipped_chunks==1);
    assert(registers[GS_CACHE_TEST]==123&&registers[GS_CACHE_ZBUF]==(1ull<<32));
    /* Individual draws keep one pass each. */
    flusha_tags=flushe_tags=0; stats=(AthenaRender3DStats){0};
    instance=athena_instance3d_create(inside_mesh);
    assert(!athena_render3d_draw(instance,&camera,ATHENA_RENDER3D_CULL_NONE,&stats));
    assert(!athena_render3d_draw(instance,&camera,ATHENA_RENDER3D_CULL_NONE,&stats));
    athena_instance3d_release(instance);
    owl_wait_generation(owl_flush_generation());
    assert(stats.draw_passes==2&&stats.pipeline_passes==2&&flusha_tags==4&&!flushe_tags);
    /* Camera mutation and reinitialization at the same address must upload
     * new constants within a group. Unchanged and copied state can reuse. */
    {
        AthenaCamera3D changing=camera;
        AthenaMatrix4 identity; ath_matrix4_identity(&identity);
        stats=(AthenaRender3DStats){0};
        assert(!athena_render3d_group_begin());
        assert(!athena_render3d_draw_mesh(inside_mesh,&identity,&changing,NULL,ATHENA_RENDER3D_CULL_NONE,&stats));
        uint64_t stamp=changing.stamp;
        assert(!athena_camera3d_set_projection(&changing,0,1,1,10));
        assert(changing.stamp==stamp);
        assert(!athena_render3d_draw_mesh(inside_mesh,&identity,&changing,NULL,ATHENA_RENDER3D_CULL_NONE,&stats));
        assert(stats.pipeline_passes==1);
        assert(athena_camera3d_set_position(&changing,.1f,0,0));
        assert(!athena_render3d_draw_mesh(inside_mesh,&identity,&changing,NULL,ATHENA_RENDER3D_CULL_NONE,&stats));
        assert(changing.stamp!=stamp&&stats.pipeline_passes==2);
        stamp=changing.stamp;
        athena_camera3d_init(&changing);
        assert(changing.stamp!=stamp);
        assert(!athena_render3d_draw_mesh(inside_mesh,&identity,&changing,NULL,ATHENA_RENDER3D_CULL_NONE,&stats));
        assert(stats.pipeline_passes==3);
        AthenaCamera3D copy=changing;
        assert(!athena_render3d_draw_mesh(inside_mesh,&identity,&copy,NULL,ATHENA_RENDER3D_CULL_NONE,&stats));
        assert(stats.pipeline_passes==3);
        athena_render3d_group_end();
        owl_wait_generation(owl_flush_generation());
    }
    /* A diffuse object splits the run: unlit, diffuse, unlit is three passes. */
    athena_batch3d_clear(batch);
    AthenaMesh3D *mixed[]={inside_mesh,lit_mesh,inside_mesh};
    for(unsigned i=0;i<3;i++) {
        instance=athena_instance3d_create(mixed[i]); assert(!athena_batch3d_add(batch,instance));
        athena_instance3d_release(instance);
    }
    lights=athena_lights_create(); assert(lights&&athena_lights_set_ambient(lights,.5f,.5f,.5f));
    flusha_tags=flushe_tags=model_uploads=0; light_uploads=0;
    assert(!athena_batch3d_draw_lit(batch,&camera,lights,ATHENA_RENDER3D_CULL_NONE,&stats));
    owl_wait_generation(owl_flush_generation());
    assert(stats.draw_passes==3&&stats.pipeline_passes==3&&flusha_tags==6&&!flushe_tags&&model_uploads==3&&light_uploads==1);
    /* Alpha mask: the pass enables the GS alpha test (ATE, GEQUAL, AREF =
     * cutoff * 128) and TEX0.TCC; another cutoff opens another pass. */
    {
        uint32_t texel=0xffffffff; AthenaTexture3DPixels px={.width=1,.height=1,.pixels=&texel,.pixel_count=1};
        AthenaTexture3D *texture=NULL; assert(!athena_texture3d_create(&px,&texture));
        float tri[]={-.1f,-.1f,-3,.1f,-.1f,-3,0,.1f,-3},uv[]={0,0,1,0,0,1};
        AthenaMesh3D *masked[2]={NULL,NULL};
        for(int i=0;i<2;i++) {
            AthenaMaterial3D material; athena_material3d_default(&material);
            material.texture=texture; material.alpha_mask=1; material.alpha_cutoff=i?.25f:.5f;
            AthenaGeometry3D g={.positions=tri,.vertex_count=3,.texcoords=uv,.texcoord_count=3,.material=&material};
            assert(!athena_mesh3d_create(&g,&masked[i]));
        }
        athena_texture3d_release(texture);
        AthenaMatrix4 identity; ath_matrix4_identity(&identity);
        stats=(AthenaRender3DStats){0};
        assert(!athena_render3d_group_begin());
        assert(!athena_render3d_draw_mesh(masked[0],&identity,&camera,NULL,ATHENA_RENDER3D_CULL_NONE,&stats));
        uint64_t test=registers[GS_CACHE_TEST];
        assert((test&1)&&((test>>1)&7)==5&&((test>>4)&0xff)==64&&!((test>>12)&3));
        assert(registers[GS_CACHE_TEX0]&(1ull<<34));
        assert(!athena_render3d_draw_mesh(masked[0],&identity,&camera,NULL,ATHENA_RENDER3D_CULL_NONE,&stats));
        assert(!athena_render3d_draw_mesh(masked[1],&identity,&camera,NULL,ATHENA_RENDER3D_CULL_NONE,&stats));
        assert(((registers[GS_CACHE_TEST]>>4)&0xff)==32);
        athena_render3d_group_end();
        owl_wait_generation(owl_flush_generation());
        assert(stats.pipeline_passes==2&&stats.draw_passes==3);
        assert(registers[GS_CACHE_TEST]==123&&!(registers[GS_CACHE_TEX0]&(1ull<<34)));
        athena_mesh3d_release(masked[0]); athena_mesh3d_release(masked[1]);
        /* Invalid cutoffs are rejected by material validation. */
        AthenaMaterial3D bad; athena_material3d_default(&bad); bad.alpha_mask=1; bad.alpha_cutoff=1.5f;
        assert(!athena_material3d_validate(&bad));
    }
    /* DMA in place: a mesh disposed right after its draw stays alive (its
     * streams may be read by DMA) until that generation is read, and is
     * released by a later draw. Seen through the texture it retains. */
    athena_render3d_module_shutdown(); /* Drain earlier meshes before counting releases. */
    for(int release_path=0;release_path<2;release_path++) {
        uint32_t texel=0xffffffff; AthenaTexture3DPixels px={.width=1,.height=1,.pixels=&texel,.pixel_count=1};
        AthenaTexture3D *texture=NULL; assert(!athena_texture3d_create(&px,&texture));
        assert(!athena_texture3d_upload(texture)); /* backend created: destroy is counted */
        AthenaMaterial3D material; athena_material3d_default(&material); material.texture=texture;
        float tri[]={-.1f,-.1f,-3,.1f,-.1f,-3,0,.1f,-3},uv[]={0,0,1,0,0,1};
        AthenaGeometry3D g={.positions=tri,.vertex_count=3,.texcoords=uv,.texcoord_count=3,.material=&material};
        AthenaMesh3D *kept=NULL; assert(!athena_mesh3d_create(&g,&kept)); athena_texture3d_release(texture);
        AthenaMatrix4 identity; ath_matrix4_identity(&identity);
        unsigned destroyed=texture3d_host_destroyed;
        stats=(AthenaRender3DStats){0};
        assert(!athena_render3d_draw_mesh(kept,&identity,&camera,NULL,ATHENA_RENDER3D_CULL_NONE,&stats));
        assert(!athena_render3d_draw_mesh(kept,&identity,&camera,NULL,ATHENA_RENDER3D_CULL_NONE,&stats));
        athena_mesh3d_release(kept);
        assert(texture3d_host_destroyed==destroyed+(ATHENA_RENDER3D_DMA_REF?0u:1u));
        owl_wait_generation(owl_flush_generation());
        if(!release_path) {
            assert(!athena_render3d_group_begin());
            athena_render3d_group_end(); /* Even an empty scene releases completed DMA references. */
        } else {
            identity.value[12]=10000;
            stats=(AthenaRender3DStats){0};
            assert(!athena_render3d_draw_mesh(inside_mesh,&identity,&camera,NULL,ATHENA_RENDER3D_CULL_NONE,&stats));
            assert(stats.culled_objects==1&&!stats.draw_passes);
        }
        assert(texture3d_host_destroyed==destroyed+1); /* released once, after the read */
        owl_wait_generation(owl_flush_generation());
    }
    /* Scale 0 hides a DIFFUSE object: culled, not an error. A singular but
     * nonzero transform still fails, with a reason. */
    {
        AthenaInstance3D *hidden=athena_instance3d_create(lit_mesh); assert(hidden);
        assert(athena_instance3d_set_scale(hidden,0,0,0));
        stats=(AthenaRender3DStats){0};
        assert(!athena_render3d_draw_lit(hidden,&camera,lights,ATHENA_RENDER3D_CULL_NONE,&stats));
        assert(stats.culled_objects==1&&!stats.draw_passes);
        assert(athena_instance3d_set_scale(hidden,1,0,1));
        athena_render3d_set_error_detail(NULL);
        assert(athena_render3d_draw_lit(hidden,&camera,lights,ATHENA_RENDER3D_CULL_NONE,&stats)==-1);
        assert(strstr(athena_render3d_error_detail(),"singular"));
        athena_instance3d_release(hidden);
    }
    /* Diffuse objects of one pass share the lights already in VU memory:
     * uploaded once per stamp, again after an effective change. */
    athena_batch3d_clear(batch);
    for(unsigned i=0;i<3;i++) {
        instance=athena_instance3d_create(lit_mesh); assert(!athena_batch3d_add(batch,instance));
        athena_instance3d_release(instance);
    }
    light_uploads=0;
    assert(!athena_batch3d_draw_lit(batch,&camera,lights,ATHENA_RENDER3D_CULL_NONE,&stats));
    owl_wait_generation(owl_flush_generation());
    assert(stats.pipeline_passes==1&&light_uploads==1);
    AthenaLights *other=athena_lights_create(); assert(other&&athena_lights_set_ambient(other,.5f,.5f,.5f));
    light_uploads=0; assert(!athena_render3d_group_begin());
    assert(!athena_batch3d_draw_lit(batch,&camera,lights,ATHENA_RENDER3D_CULL_NONE,&stats));
    assert(athena_lights_set_ambient(lights,.25f,.5f,.5f));
    assert(!athena_batch3d_draw_lit(batch,&camera,lights,ATHENA_RENDER3D_CULL_NONE,&stats));
    /* Same contents and revision, other object: a distinct stamp. */
    assert(!athena_batch3d_draw_lit(batch,&camera,other,ATHENA_RENDER3D_CULL_NONE,&stats));
    athena_render3d_group_end(); athena_lights_destroy(other);
    owl_wait_generation(owl_flush_generation());
    assert(light_uploads==3);
    /* A caller's group spans several Batch draws and is not closed by them;
     * a nested begin fails. A failed draw inside a group still restores. */
    assert(!athena_render3d_group_begin()); assert(athena_render3d_group_begin()==-1);
    athena_batch3d_clear(batch);
    instance=athena_instance3d_create(inside_mesh); assert(!athena_batch3d_add(batch,instance));
    flusha_tags=flushe_tags=0;
    AthenaRender3DStats first_draw,second_draw;
    assert(!athena_batch3d_draw(batch,&camera,ATHENA_RENDER3D_CULL_NONE,&first_draw));
    assert(!athena_batch3d_draw(batch,&camera,ATHENA_RENDER3D_CULL_NONE,&second_draw));
    assert(first_draw.pipeline_passes==1&&second_draw.pipeline_passes==0);
    assert(registers[GS_CACHE_TEST]!=123); /* Pass still open. */
    stats=(AthenaRender3DStats){0};
    assert(athena_render3d_draw(instance,&camera,(AthenaRender3DCull)3,&stats)==-1);
    athena_render3d_group_end();
    owl_wait_generation(owl_flush_generation());
    assert(flusha_tags==2&&flushe_tags==1);
    assert(registers[GS_CACHE_TEST]==123&&registers[GS_CACHE_ZBUF]==(1ull<<32));
    assert(!athena_render3d_group_begin()); athena_render3d_group_end();
    athena_instance3d_release(instance); athena_lights_destroy(lights); athena_batch3d_destroy(batch);
    athena_mesh3d_release(inside_mesh); athena_mesh3d_release(crossing_mesh); athena_mesh3d_release(lit_mesh);
    athena_render3d_module_shutdown();
    /* Textured VU1 skinning: two chunks, a shared texture pass, then an
     * untextured draw. Dispose and overwrite input streams before the flush. */
    {
        float pos[33*3],uvs[33*2],weights[33*4]={0}; uint16_t joints[33*4]={0};
        for(unsigned i=0;i<33;i++) {
            memcpy(&pos[i*3],&small[(i%3)*3],3*sizeof(float));
            uvs[i*2]=(i%3)/2.0f; uvs[i*2+1]=1;
            joints[i*4]=1; weights[i*4]=1;
        }
        uint32_t pixel=0xffffffff;
        AthenaTexture3DPixels px={.width=1,.height=1,.pixels=&pixel,.pixel_count=1};
        AthenaTexture3D *tex=NULL; assert(!athena_texture3d_create(&px,&tex));
        AthenaMaterial3D mat; athena_material3d_default(&mat); mat.texture=tex;
        mat.shading=ATHENA_MATERIAL3D_DIFFUSE;
        mat.alpha_mask=1; mat.alpha_cutoff=.5f;
        AthenaGeometry3D g={.positions=pos,.vertex_count=33,.texcoords=uvs,.texcoord_count=33,
            .joints=joints,.weights=weights,.skin_count=33,.material=&mat};
        AthenaMesh3D *m=NULL; assert(!athena_mesh3d_create(&g,&m)); athena_texture3d_release(tex);
        AthenaMesh3DView v; athena_mesh3d_view(m,&v);
        AthenaMatrix4 palette[2]; ath_matrix4_identity(&palette[0]); ath_matrix4_identity(&palette[1]);
        geometry_bytes=0; skin_normals=skin_weights=skin_uvs=0; textured_tags=0;
        stats=(AthenaRender3DStats){0};
        assert(!athena_render3d_group_begin());
        for(unsigned i=0;i<2;i++) assert(!athena_render3d_draw_skinned_contained(&v,palette,2,&camera,NULL,0,&stats));
        assert(((registers[GS_CACHE_TEST]>>4)&255)==64);
        AthenaMesh3DView invalid=v; invalid.texcoords=NULL;
        assert(athena_render3d_draw_skinned_contained(&invalid,palette,2,&camera,NULL,0,&stats)==-1);
        v.material.texture=NULL; v.material.alpha_mask=0;
        assert(!athena_render3d_draw_skinned_contained(&v,palette,2,&camera,NULL,0,&stats));
        athena_render3d_group_end();
        assert(stats.pipeline_passes==2&&stats.draw_passes==3&&stats.vu_batches==6&&stats.triangles==33);
        assert(stats.geometry_bytes==36*(44*2+36));
        assert(registers[GS_CACHE_TEST]==123&&!(registers[GS_CACHE_TEX0]&(1ull<<34)));
        assert((size_t)(internal_packet.ptr-control->base-(control->context?control->size:0))==control->alloc);
        athena_mesh3d_release(m); memset(pos,0,sizeof(pos)); memset(uvs,0,sizeof(uvs));
        owl_wait_generation(owl_flush_generation());
        assert(skin_normals==6&&skin_weights==6&&skin_uvs==4&&textured_tags==4);
        assert(geometry_bytes==stats.geometry_bytes);
        athena_render3d_module_shutdown();
    }
    /* Grow the retention queue past its initial 64 entries, then force ring
     * flushes with multiple generations. No external mesh owner survives. */
    owl_init(memory,8192);
    stats=(AthenaRender3DStats){0};
    assert(!athena_render3d_group_begin());
    unsigned refs_before=ref_tags;
    AthenaMatrix4 identity; ath_matrix4_identity(&identity);
    for(unsigned i=0;i<512;i++) {
        AthenaGeometry3D g={.positions=small,.vertex_count=3};
        AthenaMesh3D *m=NULL; assert(!athena_mesh3d_create(&g,&m));
        assert(!athena_render3d_draw_mesh(m,&identity,&camera,NULL,0,&stats));
        athena_mesh3d_release(m);
    }
    athena_render3d_group_end();
    owl_wait_generation(owl_flush_generation());
    owl_packet_stats_read(&transport);
    assert(transport.capacity_flushes>0&&transport.peak_half_qwords<4096);
    assert(transport.flushes>1&&transport.submitted_qwords>4096);
    assert(stats.triangles==512&&stats.draw_passes==512&&stats.pipeline_passes==1);
    assert(ref_tags-refs_before==(ATHENA_RENDER3D_DMA_REF?1024u:0u));
    athena_render3d_module_shutdown();
    /* Compact batches preserve corner order and all streams, including
     * targets. Read REF payload after mesh disposal, under ASan. */
    for(unsigned variant=0;variant<4;variant++) {
        enum { N=81,C=384 };
        float p[N*3],n[N*3]={0},uv[N*2],w[N*4]={0},delta[N*3*4]={0};
        uint16_t j[N*4]={0};uint32_t indices[C];unsigned at=0;
        for(unsigned y=0;y<9;y++)for(unsigned x=0;x<9;x++) {
            unsigned i=y*9+x;p[i*3]=(x/8.f-.5f)*2;p[i*3+1]=(y/8.f-.5f)*2;p[i*3+2]=-6;
            n[i*3+2]=1;uv[i*2]=x/8.f;uv[i*2+1]=y/8.f;w[i*4]=1;
            for(unsigned t=0;t<4;t++)delta[(t*N+i)*3+2]=.01f*(t+1)*x;
        }
        for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++) {
            unsigned a=y*9+x;uint32_t tri[]={a,a+1,a+10,a,a+10,a+9};
            memcpy(indices+at,tri,sizeof(tri));at+=6;
        }
        uint32_t pixel=0xffffffff;AthenaTexture3DPixels px={.width=1,.height=1,.pixels=&pixel,.pixel_count=1};
        AthenaTexture3D *tex=NULL;assert(!athena_texture3d_create(&px,&tex));
        AthenaMaterial3D mat;athena_material3d_default(&mat);mat.shading=variant?ATHENA_MATERIAL3D_DIFFUSE:ATHENA_MATERIAL3D_UNLIT;
        if(variant==1||variant==2)mat.texture=tex;
        AthenaGeometry3D g={.positions=p,.vertex_count=N,.normals=n,.normal_count=N,.texcoords=uv,.texcoord_count=N,
            .indices=indices,.index_count=C,.material=&mat,.joints=variant==2?j:NULL,.weights=variant==2?w:NULL,.skin_count=variant==2?N:0,
            .target_positions=variant==3?delta:NULL,.target_count=variant==3?4:0};
        AthenaMesh3D *m=NULL;assert(!athena_mesh3d_create(&g,&m));athena_texture3d_release(tex);
        AthenaMesh3DView v;athena_mesh3d_view(m,&v);assert(v.indices&&v.chunk_count&&v.stream_vertex_count<C);
        unsigned transformed=0,padded=0,corners=0;
        for(unsigned b=0;b<v.chunk_count;b++) {
            const AthenaMesh3DChunk *chunk=v.chunks+b;assert(chunk->first==corners&&chunk->index_count%3==0);
            assert(!(((uintptr_t)&v.positions[chunk->stream_first])&15));
            for(unsigned k=0;k<chunk->index_count;k++) {
                unsigned corner=chunk->first+k,id=chunk->stream_first+v.chunk_indices[b*48+k];
                assert(v.indices[corner]==id);unsigned source=indices[corner];
                assert(!memcmp(&v.positions[id],p+source*3,12));assert(!memcmp(&v.texcoords[id],uv+source*2,8));
                for(unsigned t=0;t<g.target_count;t++)assert(!memcmp(&v.target_positions[t*v.stream_vertex_count+id],delta+(t*N+source)*3,12));
            }
            transformed+=chunk->vertex_count;padded+=(chunk->vertex_count+3)&~3u;corners+=chunk->index_count;
        }
        assert(corners==C&&transformed<C&&padded==v.stream_vertex_count);
        if(variant==0) {
            /* Same indexed program, different lighting/culling in one group.
             * No flags from an earlier object may leak into the next draw. */
            assert(!athena_render3d_group_begin());
            for(unsigned draw=0;draw<3;draw++) {
                AthenaMesh3DView changed=v;
                changed.material.shading=draw==1?ATHENA_MATERIAL3D_DIFFUSE:ATHENA_MATERIAL3D_UNLIT;
                AthenaRender3DStats grouped={0};unsigned uploaded=indexed_config_uploads;
                int cull=draw==0?ATHENA_RENDER3D_CULL_NONE:draw==1?ATHENA_RENDER3D_CULL_BACK:ATHENA_RENDER3D_CULL_FRONT;
                assert(!athena_render3d_draw_view(&changed,&identity,&camera,NULL,cull,&grouped));
                owl_wait_generation(owl_flush_generation());
                assert(indexed_config_uploads-uploaded==1);
                assert(indexed_config.f[0]==255&&indexed_config.sword[1]==0);
                assert(indexed_config.sword[2]==(draw==1)&&indexed_config.f[3]==cull);
            }
            athena_render3d_group_end();owl_wait_generation(owl_flush_generation());
        }
        stats=(AthenaRender3DStats){0};geometry_bytes=0;unsigned before=indexed_chunks,config_before=indexed_config_uploads;
        if(variant==2) {
            AthenaMatrix4 palette;ath_matrix4_identity(&palette);
            assert(!athena_render3d_draw_skinned_contained(&v,&palette,1,&camera,NULL,0,&stats));
        } else if(variant==3) {
            float weights[]={.2f,.4f,-.3f,.1f};assert(!athena_render3d_draw_morph(&v,&identity,weights,&camera,NULL,0,&stats));
        } else assert(!athena_render3d_draw_mesh(m,&identity,&camera,NULL,0,&stats));
        unsigned batches=v.chunk_count;
        athena_mesh3d_release(m);memset(p,0,sizeof(p));memset(indices,0,sizeof(indices));
        owl_wait_generation(owl_flush_generation());
        assert(indexed_chunks-before==batches&&stats.vu_batches==batches&&stats.triangles==C/3);
        assert(indexed_config_uploads-config_before==1); /* Constants once per object, not per batch. */
        unsigned stride=16+(variant?12:0)+(variant==1||variant==2?8:0)+(variant==2?8:0)+(variant==3?48:0);
        assert(stats.geometry_bytes==padded*stride+batches*48&&geometry_bytes==stats.geometry_bytes);
        athena_render3d_module_shutdown();
    }
    memset(dma_addresses,0,sizeof(dma_addresses)); /* Do not hide stream leaks from LSan. */
    printf("3D packets and cross-channel DMA lifetime tests passed (DMA_REF=%d)\n",ATHENA_RENDER3D_DMA_REF);
    return 0;
}
