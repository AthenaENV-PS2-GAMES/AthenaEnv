#include <string.h>
#include <athena/graphics.h>
#include <athena/graphics/owl_packet.h>
#include <athena/graphics/mpg_manager.h>
#include <athena/graphics/view.h>
#include "particles2d_backend.h"
/*
 * VU1 program from vu1/draw_2D_particles.vcl, compiled to the .vsm by
 * OpenVCL (the Makefile's %.vsm rule): edit the .vcl, not the .vsm.
 */
register_vu_program(VU1Draw2D_Particles);
static vu_mpg *program;
/*
 * VU1 data memory: quadwords 0..3 hold the view's columns, its origin and the
 * texture rectangle. Each batch buffer, relative to TOP, holds the GIF tag
 * at +0, 88 input quadwords at +1..+88, the output GIF tag at +90 and 396
 * output quadwords at +91..+486: 487 quadwords, two buffers from BASE 4.
 */
#define PARTICLES_VU1_BASE 4
#define PARTICLES_VU1_BUFFER_QWC 487
_Static_assert(PARTICLES_VU1_BASE + 2 * PARTICLES_VU1_BUFFER_QWC <= 1024,
    "Particles2D VU1 buffers exceed VU1 data memory");
/* GS drawing offset of the 2D view, as TileMap. */
#define PARTICLES_GS_ORIGIN 2047.35f
#define PARTICLES_GIF_REGS (((u64)GS_RGBAQ) << 0 | \
    ((u64)GS_UV) << 4 | ((u64)GS_XYZ3) << 8 | \
    ((u64)GS_UV) << 12 | ((u64)GS_XYZ3) << 16 | \
    ((u64)GS_UV) << 20 | ((u64)GS_XYZ2) << 24 | \
    ((u64)GS_UV) << 28 | ((u64)GS_XYZ2) << 32)

static uint64_t particles_giftag(void) {
    prim_reg_t prim={.PRIM=GS_PRIM_PRIM_TRISTRIP,.IIP=0,.TME=1,.FGE=gsGlobal->PrimFogEnable,
        .ABE=gsGlobal->PrimAlphaEnable,.AA1=gsGlobal->PrimAAEnable,.FST=1,.CTXT=gsGlobal->PrimContext,.FIX=0};
    giftag_t tag={.NLOOP=0,.EOP=1,.PRE=1,.PRIM=prim.data,.FLG=0,.NREG=9};
    return tag.data;
}
/* VIF MARK that makes the texture manager upload `texture_id` (as TileMap). */
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
void athena_particles2d_submit(AthenaImage *image,const float rect[4],
    const AthenaParticle2DRecord *records,uint32_t count) {
    if(!count||!athena_image_is_loaded(image)) return;
    if(image->delayed&&image->status==ATHENA_IMAGE_STATUS_DECODED)
        image->status=ATHENA_IMAGE_STATUS_UPLOAD_PENDING;
    graphics_service_init();
    GSSURFACE *tex=image->surface;
    int texture_id=graphics_surface_bind(tex,true);
    if(texture_id==GRAPHICS_BIND_ERROR) return;
    if(!program) {
        program=vu_mpg_load_buffer(embed_vu_code_ptr(VU1Draw2D_Particles),
            embed_vu_code_size(VU1Draw2D_Particles),VECTOR_UNIT_1,false);
        if(!program) return;
    }
    owl_packet *packet=owl_query_packet(CHANNEL_VIF1,10);
    if(texture_id>=0) upload_tags(packet,texture_id);
    texture_tags(packet,tex);
    vu1_set_double_buffer_settings(PARTICLES_VU1_BASE,PARTICLES_VU1_BUFFER_QWC);
    int address=vu_mpg_preload(program,true);
    /* VU1 static addresses 0..3 now hold the view: others re-upload theirs. */
    vu1_invalidate_static_data();
    AthenaAffine2D view; athena_view_get(&view);
    float statics[16] __attribute__((aligned(16)))={view.xx,view.yx,0,0, view.xy,view.yy,0,0,
        view.tx+PARTICLES_GS_ORIGIN,view.ty+PARTICLES_GS_ORIGIN,0,0, rect[0],rect[1],rect[2],rect[3]};
    packet=owl_query_packet(CHANNEL_VIF1,5);
    owl_add_unpack_data_cnt(packet,0,4,0);
    for(int i=0;i<4;i++) owl_add_uquad_ptr(packet,(__uint128_t *)&statics[i*4]);
    uint64_t giftag=particles_giftag();
    int started=0;
    for(uint32_t first=0;first<count;first+=ATHENA_PARTICLES2D_BATCH) {
        uint32_t n=count-first; if(n>ATHENA_PARTICLES2D_BATCH) n=ATHENA_PARTICLES2D_BATCH;
        packet=owl_query_packet(CHANNEL_VIF1,4+n*2);
        owl_add_unpack_data_cnt(packet,0,1,1);
        owl_add_ulong(packet,giftag); owl_add_ulong(packet,PARTICLES_GIF_REGS);
        owl_add_unpack_data_cnt(packet,1,n*2,1);
        memcpy(packet->ptr,&records[first],n*sizeof(*records)); packet->ptr+=n*2;
        /* As TileMap: the VIF waits for the previous program before MSCNT
         * and the double-buffered layout keeps this unpack clear of it. */
        owl_add_cnt_tag(packet,0,owl_vif_code_double(VIF_CODE(address,0,started?VIF_MSCNT:VIF_MSCALF,0),
            VIF_CODE(n,0,VIF_ITOP,0)));
        started=1;
    }
    packet=owl_query_packet(CHANNEL_VIF1,1);
    owl_add_cnt_tag(packet,0,owl_vif_code_double(VIF_CODE(0,0,VIF_FLUSH,0),VIF_CODE(0,0,VIF_FLUSH,0)));
}
