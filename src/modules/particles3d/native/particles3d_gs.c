#include <string.h>
#include <athena/graphics.h>
#include <athena/graphics/owl_packet.h>
#include <athena/graphics/mpg_manager.h>
#include "particles3d_backend.h"
/*
 * VU1 program from vu1/draw_3D_billboards.vcl, compiled to the .vsm by
 * OpenVCL (the Makefile's %.vsm rule): edit the .vcl, not the .vsm.
 */
register_vu_program(VU1Draw3D_Billboards);
static vu_mpg *program;
/*
 * VU1 data memory: quadwords 0..8 hold the view-projection, screen scale,
 * camera axes, texture rectangle and guard scale. Each batch buffer,
 * relative to TOP, holds the GIF tag at +0, 88 input quadwords, the output
 * tag at +90 and 396 output quadwords: 487, two buffers from BASE 9.
 */
#define BILLBOARD_VU1_BASE 9
#define BILLBOARD_VU1_BUFFER_QWC 487
_Static_assert(BILLBOARD_VU1_BASE + 2 * BILLBOARD_VU1_BUFFER_QWC <= 1024,
    "Particles3D VU1 buffers exceed VU1 data memory");
#define BILLBOARD_GIF_REGS (((u64)GS_RGBAQ) << 0 | \
    ((u64)GS_UV) << 4 | ((u64)GS_XYZ3) << 8 | \
    ((u64)GS_UV) << 12 | ((u64)GS_XYZ3) << 16 | \
    ((u64)GS_UV) << 20 | ((u64)GS_XYZ2) << 24 | \
    ((u64)GS_UV) << 28 | ((u64)GS_XYZ2) << 32)

static uint64_t billboard_giftag(void) {
    prim_reg_t prim={.PRIM=GS_PRIM_PRIM_TRISTRIP,.IIP=0,.TME=1,.FGE=gsGlobal->PrimFogEnable,
        .ABE=gsGlobal->PrimAlphaEnable,.AA1=gsGlobal->PrimAAEnable,.FST=1,.CTXT=gsGlobal->PrimContext,.FIX=0};
    giftag_t tag={.NLOOP=0,.EOP=1,.PRE=1,.PRIM=prim.data,.FLG=0,.NREG=9};
    return tag.data;
}
static void upload_tags(owl_packet *packet,int texture_id) {
    owl_add_cnt_tag(packet,4,0);
    owl_add_uint(packet,VIF_CODE(0,0,VIF_NOP,0));
    owl_add_uint(packet,VIF_CODE(0,0,VIF_NOP,0));
    owl_add_uint(packet,VIF_CODE(0,0,VIF_FLUSH,0));
    owl_add_uint(packet,VIF_CODE(2,0,VIF_DIRECT,0));
    owl_add_tag(packet,GIF_AD,GIFTAG(1,1,0,0,0,1));
    owl_add_tag(packet,GIF_NOP,0);
    owl_add_uint(packet,VIF_CODE(0,0,VIF_FLUSHA,0));
    owl_add_uint(packet,VIF_CODE(0,0,VIF_NOP,0));
    owl_add_uint(packet,VIF_CODE(texture_id,0,VIF_MARK,0));
    owl_add_uint(packet,VIF_CODE(0,0,VIF_NOP,1));
}
/* TEX0/TEX1 after earlier draws end: also the barrier before this draw
 * overwrites the static VU1 data and the double-buffer settings. */
static void texture_tags(owl_packet *packet,GSSURFACE *tex) {
    int tw,th;
    owl_add_cnt_tag(packet,4,owl_vif_code_double(VIF_CODE(0,0,VIF_NOP,0),VIF_CODE(0,0,VIF_NOP,0)));
    owl_add_uint(packet,VIF_CODE(0,0,VIF_FLUSHA,0));
    owl_add_uint(packet,VIF_CODE(0,0,VIF_NOP,0));
    owl_add_uint(packet,VIF_CODE(0,0,VIF_NOP,0));
    owl_add_uint(packet,VIF_CODE(3,0,VIF_DIRECT,0));
    owl_add_tag(packet,GIF_AD,GIFTAG(2,1,0,0,0,1));
    athena_set_tw_th(tex,&tw,&th);
    owl_add_tag(packet,GS_TEX0_1+gsGlobal->PrimContext,
        GS_SETREG_TEX0((tex->Vram&~GRAPHICS_TRANSFER_REQUEST_MASK)/256,tex->TBW,tex->PSM,tw,th,
            gsGlobal->PrimAlphaEnable,COLOR_MODULATE,(tex->VramClut&~GRAPHICS_TRANSFER_REQUEST_MASK)/256,
            tex->ClutPSM,0,0,tex->VramClut?GS_CLUT_STOREMODE_LOAD:GS_CLUT_STOREMODE_NOLOAD));
    owl_add_tag(packet,GS_TEX1_1+gsGlobal->PrimContext,GS_SETREG_TEX1(1,0,tex->Filter,tex->Filter,0,0,0));
}
int athena_particles3d_submit(AthenaCamera3D *camera,AthenaImage *image,const float rect[4],
    const AthenaParticle3DRecord *records,uint32_t count) {
    if(!count||!athena_image_is_loaded(image)) return 0;
    graphics_service_init();
    GSCONTEXT *gs=getGSGLOBAL();
    if(!gs||!gs->ZBuffering) return ATHENA_PARTICLES3D_NO_ZBUFFER;
    if(image->delayed&&image->status==ATHENA_IMAGE_STATUS_DECODED)
        image->status=ATHENA_IMAGE_STATUS_UPLOAD_PENDING;
    GSSURFACE *tex=image->surface;
    int texture_id=graphics_surface_bind(tex,true);
    if(texture_id==GRAPHICS_BIND_ERROR) return 0;
    if(!program) {
        program=vu_mpg_load_buffer(embed_vu_code_ptr(VU1Draw3D_Billboards),
            embed_vu_code_size(VU1Draw3D_Billboards),VECTOR_UNIT_1,false);
        if(!program) return ATHENA_PARTICLES3D_ENOMEM;
    }
    owl_packet *packet=owl_query_packet(CHANNEL_VIF1,10);
    if(texture_id>=0) upload_tags(packet,texture_id);
    texture_tags(packet,tex);
    vu1_set_double_buffer_settings(BILLBOARD_VU1_BASE,BILLBOARD_VU1_BUFFER_QWC);
    int address=vu_mpg_preload(program,true);
    if(address<0) return ATHENA_PARTICLES3D_ENOMEM;
    vu1_invalidate_static_data();
    /* As Render3D: Y flipped for the GS, reversed depth over the z-buffer range. */
    AthenaMatrix4 vp=camera->view_projection;
    for(int i=1;i<16;i+=4) vp.value[i]=-vp.value[i];
    int height=gs->Height*((gs->Interlace==GS_INTERLACED&&gs->Field==GS_FRAME)?2:1);
    uint32_t max_z=(gs->PSMZ==GS_ZBUF_16||gs->PSMZ==GS_ZBUF_16S)?0xffffu:gs->PSMZ==GS_ZBUF_24?0xffffffu:0x7fffff00u;
    const float *v=camera->view.value;
    float statics[20] __attribute__((aligned(16)))={gs->Width/2.0f,height/2.0f,max_z/2.0f,0,
        v[0],v[4],v[8],0, v[1],v[5],v[9],0, rect[0],rect[1],rect[2],rect[3], .25f,.25f,0,0};
    packet=owl_query_packet(CHANNEL_VIF1,10);
    owl_add_unpack_data_cnt(packet,0,9,0);
    for(int i=0;i<4;i++) owl_add_uquad_ptr(packet,(__uint128_t *)&vp.value[i*4]);
    for(int i=0;i<5;i++) owl_add_uquad_ptr(packet,(__uint128_t *)&statics[i*4]);
    /* Depth tested against the scene, not written: transparent particles do
     * not hide each other. The caller's cached TEST/ZBUF are restored. */
    int test_reg=GS_CACHE_TEST+gs->PrimContext,zbuf_reg=GS_CACHE_ZBUF+gs->PrimContext;
    uint64_t saved_test=get_register(test_reg),saved_zbuf=get_register(zbuf_reg);
    set_register(test_reg,GS_SETREG_TEST(0,1,0x80,0,0,0,1,DEPTH_GEQUAL));
    set_register(zbuf_reg,saved_zbuf|(1ull<<32));
    uint64_t giftag=billboard_giftag();
    int started=0;
    for(uint32_t first=0;first<count;first+=ATHENA_PARTICLES3D_BATCH) {
        uint32_t n=count-first; if(n>ATHENA_PARTICLES3D_BATCH) n=ATHENA_PARTICLES3D_BATCH;
        packet=owl_query_packet(CHANNEL_VIF1,4+n*2);
        owl_add_unpack_data_cnt(packet,0,1,1);
        owl_add_ulong(packet,giftag); owl_add_ulong(packet,BILLBOARD_GIF_REGS);
        owl_add_unpack_data_cnt(packet,1,n*2,1);
        memcpy(packet->ptr,&records[first],n*sizeof(*records)); packet->ptr+=n*2;
        owl_add_cnt_tag(packet,0,owl_vif_code_double(VIF_CODE(address,0,started?VIF_MSCNT:VIF_MSCALF,0),
            VIF_CODE(n,0,VIF_ITOP,0)));
        started=1;
    }
    /* Wait for PATH1 before restoring TEST/ZBUF through PATH2. */
    packet=owl_query_packet(CHANNEL_VIF1,1);
    owl_add_cnt_tag(packet,0,owl_vif_code_double(VIF_CODE(0,0,VIF_FLUSHA,0),VIF_CODE(0,0,VIF_NOP,0)));
    set_register(test_reg,saved_test); set_register(zbuf_reg,saved_zbuf);
    return 0;
}
