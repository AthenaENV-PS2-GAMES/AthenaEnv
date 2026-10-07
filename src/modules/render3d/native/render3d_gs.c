#include <string.h>
#include <stddef.h>
#include <athena/float_bits.h>
#include <athena/render3d.h>
#include <athena/graphics.h>
#include <athena/graphics/owl_packet.h>
#include <athena/graphics/mpg_manager.h>
#include "render3d_clip.h"
#include "../../model3d/native/texture3d_backend.h"
register_vu_program(VU1Draw3DCS);
register_vu_program(VU1Draw3DDiffuse);
register_vu_program(VU1Draw3DTexture);
register_vu_program(VU1Draw3DSkinned);
register_vu_program(VU1Draw3DNear);
register_vu_program(VU1Draw3DMorph);
static vu_mpg *program,*diffuse_program,*texture_program,*skinned_program,*near_program,*morph_program;
/* vu1/draw_3D_morph.vcl: 33 vertices (11 triangles) per batch; streams at
 * 2 (positions), 35 (colours), 68 (normals), 101 (UVs), 134 + 33 t (deltas). */
#define R3D_MORPH_BATCH 33u
/* vu1/draw_3D_near.vcl: the static layout, at most 24 vertices per batch
 * (8 triangles; each may become two). */
#define R3D_NEAR_BATCH 24u
/* vu1/draw_3D_skinned.vcl: room for 32 vertices per batch, palette from
 * quadword 36. Batches hold whole triangles (30 vertices): the kick cycle and
 * the culling state restart with every program call. */
#define R3D_SKIN_BATCH 30u
#define R3D_SKIN_PALETTE 36u
/* Same static triangle layout as the legacy color microprogram. */
#define R3D_BATCH ATHENA_RENDER3D_CHUNK
#define R3D_BASE 141u
static float guard_of(const GSCONTEXT *gs);
#define R3D_OFFSET 369u

static void inline_packed(owl_packet *packet,uint32_t dest,const void *data,uint32_t count,
    uint32_t format,uint32_t bytes,uint32_t unsigned_values) {
    uint32_t padded=(count+3)&~3u, qwc=padded*bytes/16;
    owl_add_ulong(packet,DMA_TAG(qwc,0,DMA_CNT,0,0,0));
    owl_add_uint(packet,VIF_CODE(0x0101,0,VIF_STCYCL,0));
    owl_add_uint(packet,VIF_CODE(dest|(unsigned_values<<14)|(1u<<15),padded,format|0x60,0));
    memcpy(packet->ptr,data,qwc*16); packet->ptr+=qwc;
}
static void inline_matrix(owl_packet *p,uint32_t destination,const AthenaMatrix4 *m) {
    owl_add_unpack_data_cnt(p,destination,4,0);
    for(int i=0;i<4;i++) owl_add_uquad_ptr(p,(void *)&m->value[i*4]);
}
/* color_scale: CLIPFAN.x, read by the near program (255 untextured, 128 for
 * MODULATE); the other programs ignore it. */
/* owl_add_uquad_ptr() copies with 128-bit loads, which ignore the low four
 * address bits: every light array sent that way must be 16-byte aligned. */
_Static_assert(offsetof(AthenaLightsView,ambient)%16==0&&offsetof(AthenaLightsView,direction)%16==0&&
    offsetof(AthenaLightsView,diffuse)%16==0&&offsetof(AthenaLightsView,point_position)%16==0&&
    offsetof(AthenaLightsView,point_color)%16==0,"light arrays must be quadword aligned");
_Static_assert(_Alignof(AthenaLightsView)>=16,"AthenaLightsView must be quadword aligned");
/* Point lights for vu1/include/point_lights.i: positions (w = 1 / range^2)
 * at 132, colours at 136; their count goes to 15.z. 9 quadwords. */
static void inline_point_lights(owl_packet *p,const AthenaLightsView *lights) {
    owl_add_unpack_data_cnt(p,132,8,0);
    for(unsigned i=0;i<ATHENA_LIGHTS_MAX_POINT;i++) owl_add_uquad_ptr(p,(void *)lights->point_position[i]);
    for(unsigned i=0;i<ATHENA_LIGHTS_MAX_POINT;i++) owl_add_uquad_ptr(p,(void *)lights->point_color[i]);
}
typedef struct { int address,context,textured; AthenaRender3DCull cull; AthenaRender3DStats *stats; float color_scale; int fog; } Submission;
#ifndef GS_XYZF2
#define GS_XYZF2 0x04
#endif
#ifndef GS_FOGCOL
#define GS_FOGCOL 0x3d
#endif
/* GIF register lists: ST, RGBAQ and XYZ2, or XYZF2 with fog. */
static uint64_t register_list(int fog) {
    return fog?((uint64_t)GS_ST|(uint64_t)GS_RGBAQ<<4|(uint64_t)GS_XYZF2<<8):(uint64_t)(DRAW_STQ2_REGLIST);
}
static void submit_chunk(const void *positions,const AthenaColor3D *colors,uint32_t count,
    int pretransformed,const AthenaPosition3D *normals,const AthenaTexcoord3D *texcoords,Submission *submission) {
    uint32_t padded=(count+3)&~3u,position_bytes=pretransformed?16u:12u;
    uint32_t vertex_bytes=position_bytes+4+(normals?12:0)+(texcoords?8:0);
    size_t qwc=8+(normals?1:0)+(texcoords?1:0)+padded*vertex_bytes/16;
    owl_packet *p=owl_query_packet(CHANNEL_VIF1,qwc);
    prim_reg_t prim={.PRIM=GS_PRIM_PRIM_TRIANGLE,.IIP=1,.TME=submission->textured,.FGE=submission->fog?1:0,.CTXT=submission->context};
    giftag_t tag={.EOP=1,.PRE=1,.PRIM=prim.data,.NREG=3};
    owl_add_unpack_data_cnt(p,26,1,0);
    owl_add_float(p,submission->color_scale); owl_add_uint(p,pretransformed); owl_add_uint(p,normals!=NULL); owl_add_float(p,submission->cull);
    owl_add_unpack_data_cnt(p,0,1,1); owl_add_ulong(p,tag.data); owl_add_ulong(p,register_list(submission->fog));
    inline_packed(p,2,positions,count,pretransformed?UNPACK_V4_32:UNPACK_V3_32,position_bytes,1);
    inline_packed(p,98,colors,count,UNPACK_V4_8,4,1);
    if(normals) inline_packed(p,50,normals,count,UNPACK_V3_32,12,1);
    if(texcoords) inline_packed(p,146,texcoords,count,UNPACK_V2_32,8,1);
    owl_add_cnt_tag(p,1,0);
    owl_add_uint(p,VIF_CODE(0,0,VIF_FLUSH,0));
    owl_add_uint(p,VIF_CODE(0,0,VIF_NOP,0));
    owl_add_uint(p,VIF_CODE(count,0,VIF_ITOP,0));
    owl_add_uint(p,VIF_CODE(submission->address,0,VIF_MSCALF,0));
    submission->stats->vu_batches++;
    submission->stats->geometry_bytes+=padded*vertex_bytes;
}
/* Active targets: indices and weights of the nonzero ones. */
typedef struct { uint32_t count,index[ATHENA_RENDER3D_MORPH_TARGETS]; float weight[ATHENA_RENDER3D_MORPH_TARGETS]; int flat; } MorphSet;
static void submit_morph_chunk(const AthenaMesh3DView *m,uint32_t first,uint32_t count,const MorphSet *morph,
    int lit,Submission *submission) {
    uint32_t padded=(count+3)&~3u;
    size_t qwc=8+5+padded*(12+4+(lit?12:0)+(submission->textured?8:0)+morph->count*12)/16;
    owl_packet *p=owl_query_packet(CHANNEL_VIF1,qwc);
    prim_reg_t prim={.PRIM=GS_PRIM_PRIM_TRIANGLE,.IIP=1,.TME=submission->textured,.FGE=submission->fog?1:0,.CTXT=submission->context};
    giftag_t tag={.EOP=1,.PRE=1,.PRIM=prim.data,.NREG=3};
    owl_add_unpack_data_cnt(p,26,1,0);
    owl_add_float(p,submission->color_scale); owl_add_uint(p,0); owl_add_uint(p,lit); owl_add_float(p,submission->cull);
    owl_add_unpack_data_cnt(p,0,1,1); owl_add_ulong(p,tag.data); owl_add_ulong(p,register_list(submission->fog));
    inline_packed(p,2,&m->positions[first],count,UNPACK_V3_32,12,1);
    inline_packed(p,35,&m->colors[first],count,UNPACK_V4_8,4,1);
    if(lit) inline_packed(p,68,&m->normals[first],count,UNPACK_V3_32,12,1);
    if(submission->textured) inline_packed(p,101,&m->texcoords[first],count,UNPACK_V2_32,8,1);
    for(uint32_t t=0;t<morph->count;t++)
        inline_packed(p,134+R3D_MORPH_BATCH*t,&m->target_positions[morph->index[t]*m->vertex_count+first],count,UNPACK_V3_32,12,1);
    owl_add_cnt_tag(p,1,0);
    owl_add_uint(p,VIF_CODE(0,0,VIF_FLUSH,0));
    owl_add_uint(p,VIF_CODE(0,0,VIF_NOP,0));
    owl_add_uint(p,VIF_CODE(count,0,VIF_ITOP,0));
    owl_add_uint(p,VIF_CODE(submission->address,0,VIF_MSCALF,0));
    submission->stats->vu_batches++;
    submission->stats->geometry_bytes+=padded*(12+4+(lit?12:0)+(submission->textured?8:0)+morph->count*12);
}
static int submit_clipped(const AthenaVector4 *positions,const AthenaColor3D *colors,
    const AthenaTexcoord3D *texcoords,uint32_t count,void *opaque) {
    Submission *s=opaque;
    submit_chunk(positions,colors,count,1,NULL,s->textured?texcoords:NULL,s); return 0;
}
int athena_render3d_draw_lit(AthenaInstance3D *instance,AthenaCamera3D *camera,
    const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *stats) {
    if(!instance) return -1;
    const AthenaMatrix4 *model=athena_instance3d_transform(instance);
    if(!model) return -1;
    return athena_render3d_draw_mesh(athena_instance3d_mesh(instance),model,camera,lights,cull,stats);
}
/* The open pass: its key (program, texture, camera, GS context) and the GS
 * registers it overrides. Outside a group it closes after every draw. */
static struct {
    int open,grouped,address,context;
    vu_mpg **program; const AthenaTexture3D *texture; const AthenaCamera3D *camera;
    int test_reg,zbuf_reg,tex0_reg,tex1_reg,clamp_reg;
    uint64_t saved_test,saved_zbuf,saved_tex0,saved_tex1,saved_clamp;
    /* Fog parameters in VU memory (vu1/include/fog.i) and the GS FOGCOL
     * last written; fog_valid is cleared when a pass opens. */
    float fog[4] __attribute__((aligned(16))); int fog_valid,fog_on; uint64_t fogcol; int fogcol_valid;
} pass;
/* Fog of a draw: from its lights, never with a 32-bit Z buffer (XYZF2 has
 * 24 bits of depth). Uploads the parameters and FOGCOL when they change. */
static int fog_prepare(const AthenaLightsView *lights,const GSCONTEXT *gs) {
    int on=lights&&lights->fog_enabled&&gs->PSMZ!=GS_ZBUF_32;
    /* Most draws: no fog, already uploaded as off. (An integer flag: the
     * enabled word read as a float is a denormal, which the FPU flushes.) */
    if(!on&&pass.fog_valid&&!pass.fog_on) return 0;
    union { float f[4]; uint32_t u[4]; } fog __attribute__((aligned(16)))={{0}};
    if(on) {
        fog.u[0]=1; fog.f[1]=lights->fog_end; fog.f[2]=255/(lights->fog_end-lights->fog_start);
        uint64_t color=0;
        for(int i=0;i<3;i++) {
            float c=lights->fog_color[i]*255+.5f; color|=(uint64_t)(c<0?0:c>255?255:(uint32_t)c)<<(i*8);
        }
        if(!pass.fogcol_valid||pass.fogcol!=color) {
            owl_packet *p=owl_query_packet(CHANNEL_VIF1,4);
            owl_add_cnt_tag(p,3,0);
            owl_add_uint(p,VIF_CODE(0,0,VIF_FLUSHA,0)); owl_add_uint(p,VIF_CODE(0,0,VIF_NOP,0));
            owl_add_uint(p,VIF_CODE(0,0,VIF_NOP,0)); owl_add_uint(p,VIF_CODE(2,0,VIF_DIRECT,0));
            owl_add_tag(p,GIF_AD,VU_GS_GIFTAG(1,1,NULL,0,0,0,1));
            owl_add_tag(p,GS_FOGCOL,color);
            pass.fogcol=color; pass.fogcol_valid=1;
        }
    }
    if(!pass.fog_valid||memcmp(pass.fog,fog.f,sizeof(pass.fog))) {
        owl_packet *p=owl_query_packet(CHANNEL_VIF1,2);
        owl_add_unpack_data_cnt(p,9,1,0); owl_add_uquad_ptr(p,(void *)fog.f);
        memcpy(pass.fog,fog.f,sizeof(pass.fog)); pass.fog_valid=1; pass.fog_on=on;
    }
    return on;
}
static void pass_close(void) {
    if(!pass.open) return;
    /* Wait for PATH1 before restoring TEST through PATH2 (VIF DIRECT). */
    owl_packet *p=owl_query_packet(CHANNEL_VIF1,1);
    owl_add_cnt_tag(p,0,owl_vif_code_double(VIF_CODE(0,0,VIF_FLUSHA,0),VIF_CODE(0,0,VIF_NOP,0)));
    set_register(pass.test_reg,pass.saved_test); set_register(pass.zbuf_reg,pass.saved_zbuf);
    if(pass.texture) {
        set_register(pass.tex0_reg,pass.saved_tex0); set_register(pass.tex1_reg,pass.saved_tex1);
        set_register(pass.clamp_reg,pass.saved_clamp);
    }
    pass.open=0;
}
/* Also uploads the first object's model matrix with the camera constants. */
static int pass_open(vu_mpg **program,const AthenaTexture3D *texture,const AthenaTexture3DBinding *binding,
    const AthenaCamera3D *camera,const AthenaMatrix4 *model,GSCONTEXT *gs,AthenaRender3DStats *stats) {
    /* Barrier before overwriting BASE/OFFSET and shared VU1 constants. */
    owl_packet *p=owl_query_packet(CHANNEL_VIF1,1);
    owl_add_cnt_tag(p,0,owl_vif_code_double(VIF_CODE(0,0,VIF_FLUSHA,0),VIF_CODE(0,0,VIF_NOP,0)));
    vu1_set_double_buffer_settings(R3D_BASE,R3D_OFFSET);
    int address=vu_mpg_preload(*program,true);
    if(address<0) return -2;
    vu1_invalidate_static_data();
    AthenaMatrix4 gpu_view=camera->view_projection;
    /* Public camera coordinates are Y-up; the GS viewport is Y-down. */
    for(int i=1;i<16;i+=4) gpu_view.value[i]=-gpu_view.value[i];
    int height=gs->Height*((gs->Interlace==GS_INTERLACED&&gs->Field==GS_FRAME)?2:1);
    uint32_t max_z=(gs->PSMZ==GS_ZBUF_16||gs->PSMZ==GS_ZBUF_16S)?0xffffu:
        gs->PSMZ==GS_ZBUF_24?0xffffffu:0x7fffff00u;
    /* w: 1 / guard band, the VU programs' triangle rejection scale. */
    float scale[4] __attribute__((aligned(16)))={gs->Width/2.0f,height/2.0f,max_z/2.0f,1/guard_of(gs)};
    p=owl_query_packet(CHANNEL_VIF1,12);
    owl_add_unpack_data_cnt(p,0,1,0); owl_add_uquad_ptr(p,(void *)scale);
    inline_matrix(p,1,&gpu_view); inline_matrix(p,5,model);
    /* A 3D pass owns reversed-depth testing and depth writes, then restores
     * the caller's cached state. The default 2D screen uses DEPTH_ALWAYS. */
    pass.test_reg=GS_CACHE_TEST+gs->PrimContext; pass.zbuf_reg=GS_CACHE_ZBUF+gs->PrimContext;
    pass.saved_test=get_register(pass.test_reg); pass.saved_zbuf=get_register(pass.zbuf_reg);
    if(texture) {
        pass.tex0_reg=GS_CACHE_TEX0+gs->PrimContext; pass.tex1_reg=GS_CACHE_TEX1+gs->PrimContext;
        pass.clamp_reg=GS_CACHE_CLAMP+gs->PrimContext;
        pass.saved_tex0=get_register(pass.tex0_reg); pass.saved_tex1=get_register(pass.tex1_reg);
        pass.saved_clamp=get_register(pass.clamp_reg);
        /* Force the pass state, then restore through the cache. TileMap may
         * write texture registers directly without updating the shared cache. */
        set_register_force(pass.tex0_reg,binding->tex0);
        set_register_force(pass.tex1_reg,binding->tex1);
        set_register_force(pass.clamp_reg,binding->clamp);
    }
    set_register(pass.test_reg,GS_SETREG_TEST(0,1,0x80,0,0,0,1,DEPTH_GEQUAL));
    set_register(pass.zbuf_reg,pass.saved_zbuf&~(1ull<<32));
    pass.open=1; pass.address=address; pass.context=gs->PrimContext; pass.fog_valid=0;
    pass.program=program; pass.texture=texture; pass.camera=camera;
    stats->pipeline_passes++; return 0;
}
int athena_render3d_group_begin(void) {
    if(pass.grouped) return -1;
    pass.grouped=1; return 0;
}
void athena_render3d_group_end(void) {
    pass_close(); pass.grouped=0;
}
/* GS primitive coordinates span 0..4096 with the screen centred at 2048
 * (the VU programs add 2048), so a vertex up to 2048 pixels from the centre
 * still fits; 90% of that, in units of the half screen. */
/* Cached per screen mode: draws ask for it for every object. */
static float guard_of(const GSCONTEXT *gs) {
    static int width,height; static float guard=1;
    if(!gs||!gs->Width||!gs->Height) return 1;
    int h=gs->Height*((gs->Interlace==GS_INTERLACED&&gs->Field==GS_FRAME)?2:1);
    if(gs->Width!=width||h!=height) {
        float x=2048.0f/(gs->Width*.5f),y=2048.0f/(h*.5f),g=(x<y?x:y)*.9f;
        width=gs->Width; height=h; guard=g>1?g:1;
    }
    return guard;
}
float athena_render3d_guard_band(void) {
    graphics_service_init();
    return guard_of(getGSGLOBAL());
}
/* relation < 0: test the view bounds here; otherwise the caller's result. */
/* morph: NULL, or the active targets of a VU1 morph draw (then 1 is
 * returned, with nothing drawn or counted, when the mesh is not contained). */
static int draw_view(const AthenaMesh3DView *view,const AthenaMatrix4 *model,
    AthenaCamera3D *camera,const AthenaLights *lights,AthenaRender3DCull cull,
    AthenaRender3DStats *stats,int relation,const MorphSet *morph) {
    if(!view||!model||!camera||!stats||(cull!=0&&cull!=1&&cull!=-1)) return -1;
    if(!athena_camera3d_update(camera)) return -1;
    AthenaMesh3DView mesh=*view;
    stats->submitted_objects++;
    graphics_service_init();
    GSCONTEXT *gs=getGSGLOBAL();
    /* -1: not tested yet; the caller's INTERSECT is tested again below. */
    int guard_inside=-1;
    float guard=guard_of(gs);
    if(relation<0) relation=athena_camera3d_box_relation_ex(camera,model,mesh.minimum,mesh.maximum,guard,&guard_inside);
    else for(int i=0;i<16;i++) if(!athena_float_isfinite(model->value[i])) return -1;
    if(relation<0) return -1;
    if(relation==ATHENA_FRUSTUM3D_OUTSIDE) {
        stats->culled_objects++; return 0;
    }
    if(morph&&relation==ATHENA_FRUSTUM3D_INTERSECT&&!(guard>1&&(guard_inside>=0?guard_inside:
        athena_camera3d_box_relation_guard(camera,model,mesh.minimum,mesh.maximum,guard)==ATHENA_FRUSTUM3D_INSIDE))) {
        stats->submitted_objects--; return 1;
    }
    /* Crossing only the screen edges, within the guard band: VU1 draws it
     * and the GS scissor trims it, instead of clipping in C. */
    /* Crossing the near plane only: VU1 clips it (draw_3D_near). */
    int near=0;
    if(relation==ATHENA_FRUSTUM3D_INTERSECT&&guard>1) {
        if(guard_inside<0) guard_inside=athena_camera3d_box_relation_guard(camera,model,mesh.minimum,mesh.maximum,guard)==ATHENA_FRUSTUM3D_INSIDE;
        if(guard_inside) { relation=ATHENA_FRUSTUM3D_INSIDE; stats->guard_band_objects++; }
        else if(athena_camera3d_box_near_guard(camera,model,mesh.minimum,mesh.maximum,guard)==1) {
            near=1; stats->near_clip_objects++;
        }
    }
    AthenaShade3D shade;
    if(!athena_render3d_shade_prepare(&shade,&mesh,model,lights)) return -1;
    int vu_diffuse=shade.enabled&&(relation==ATHENA_FRUSTUM3D_INSIDE||near);
    if(morph) { near=0; relation=ATHENA_FRUSTUM3D_INSIDE; vu_diffuse=shade.enabled; }
    if(!gs||!gs->ZBuffering) return -3;
    if(owl_get_controller()->size<=10+R3D_BATCH*36/16+1) return -2;
    const AthenaTexture3D *texture=mesh.material.texture;
    vu_mpg **selected=morph?&morph_program:near?&near_program:texture?&texture_program:vu_diffuse?&diffuse_program:&program;
    int same=pass.open&&pass.program==selected&&pass.texture==texture&&pass.camera==camera&&
        pass.context==gs->PrimContext;
    /* Close before binding: a first bind may wait for idle and upload. */
    if(pass.open&&!same) pass_close();
    if(!same) {
        AthenaTexture3DBinding binding={0};
        if(texture) {
            int code=athena_texture3d_bind(mesh.material.texture,&binding);
            if(code<0) return code;
        }
        if(!*selected) {
            *selected=morph?vu_mpg_load_buffer(embed_vu_code_ptr(VU1Draw3DMorph),embed_vu_code_size(VU1Draw3DMorph),VECTOR_UNIT_1,false):
                near?vu_mpg_load_buffer(embed_vu_code_ptr(VU1Draw3DNear),embed_vu_code_size(VU1Draw3DNear),VECTOR_UNIT_1,false):
                vu_mpg_load_buffer(texture?embed_vu_code_ptr(VU1Draw3DTexture):vu_diffuse?embed_vu_code_ptr(VU1Draw3DDiffuse):embed_vu_code_ptr(VU1Draw3DCS),
                texture?embed_vu_code_size(VU1Draw3DTexture):vu_diffuse?embed_vu_code_size(VU1Draw3DDiffuse):embed_vu_code_size(VU1Draw3DCS),VECTOR_UNIT_1,false);
            if(!*selected) return -2;
        }
        int code=pass_open(selected,texture,&binding,camera,model,gs,stats);
        if(code<0) return code;
    } else {
        /* The microprogram reads the object constants when it starts. Within
         * a shared pass, wait for the previous object's last chunk to end. */
        owl_packet *p=owl_query_packet(CHANNEL_VIF1,6);
        owl_add_cnt_tag(p,0,owl_vif_code_double(VIF_CODE(0,0,VIF_FLUSHE,0),VIF_CODE(0,0,VIF_NOP,0)));
        inline_matrix(p,5,model);
    }
    if(vu_diffuse) {
        owl_packet *p=owl_query_packet(CHANNEL_VIF1,17+(shade.lights.point_count?9:0));
        inline_matrix(p,27,&shade.normal_matrix);
        owl_add_unpack_data_cnt(p,10,6,0);
        for(unsigned i=0;i<ATHENA_LIGHTS_MAX_DIRECTIONAL;i++) owl_add_uquad_ptr(p,(void *)shade.lights.direction[i]);
        owl_add_uquad_ptr(p,(void *)shade.lights.ambient);
        owl_add_uint(p,0); owl_add_uint(p,0); owl_add_uint(p,shade.lights.point_count); owl_add_uint(p,shade.lights.count);
        if(shade.lights.point_count) inline_point_lights(p,&shade.lights);
        owl_add_unpack_data_cnt(p,18,4,0);
        for(unsigned i=0;i<ATHENA_LIGHTS_MAX_DIRECTIONAL;i++) owl_add_uquad_ptr(p,(void *)shade.lights.diffuse[i]);
    }
    int fog=fog_prepare(athena_lights_peek(lights),gs);
    Submission submission={pass.address,gs->PrimContext,texture!=NULL,cull,stats,near||morph?(texture?128.0f:255.0f):1.0f,fog};
    int result=0;
    if(morph) {
        /* Active target count and flat normals at 35, weights from 36. */
        owl_packet *p=owl_query_packet(CHANNEL_VIF1,6);
        owl_add_unpack_data_cnt(p,35,5,0);
        owl_add_uint(p,morph->count); owl_add_uint(p,vu_diffuse&&morph->flat); owl_add_uint(p,0); owl_add_uint(p,0);
        for(uint32_t t=0;t<ATHENA_RENDER3D_MORPH_TARGETS;t++) {
            float w=t<morph->count?morph->weight[t]:0;
            owl_add_float(p,w); owl_add_float(p,w); owl_add_float(p,w); owl_add_float(p,0);
        }
        for(uint32_t first=0;first<mesh.vertex_count;first+=R3D_MORPH_BATCH) {
            uint32_t count=mesh.vertex_count-first; if(count>R3D_MORPH_BATCH) count=R3D_MORPH_BATCH;
            submit_morph_chunk(&mesh,first,count,morph,vu_diffuse,&submission);
        }
        stats->vu_morph_objects++;
        stats->source_triangles+=mesh.vertex_count/3;
        stats->triangles+=mesh.vertex_count/3;
    } else if(near) {
        for(uint32_t first=0;first<mesh.vertex_count;first+=R3D_NEAR_BATCH) {
            uint32_t count=mesh.vertex_count-first; if(count>R3D_NEAR_BATCH) count=R3D_NEAR_BATCH;
            submit_chunk(&mesh.positions[first],&mesh.colors[first],count,0,vu_diffuse?&mesh.normals[first]:NULL,
                texture?&mesh.texcoords[first]:NULL,&submission);
        }
        stats->source_triangles+=mesh.vertex_count/3;
        stats->triangles+=mesh.vertex_count/3;
    } else if(relation==ATHENA_FRUSTUM3D_INSIDE) {
        for(uint32_t first=0;first<mesh.vertex_count;first+=R3D_BATCH) {
            uint32_t count=mesh.vertex_count-first; if(count>R3D_BATCH) count=R3D_BATCH;
            submit_chunk(&mesh.positions[first],&mesh.colors[first],count,0,vu_diffuse?&mesh.normals[first]:NULL,
                texture?&mesh.texcoords[first]:NULL,&submission);
        }
        stats->source_triangles+=mesh.vertex_count/3;
        stats->triangles+=mesh.vertex_count/3;
    } else {
        AthenaMatrix4 gpu_view=camera->view_projection,clip_matrix;
        for(int i=1;i<16;i+=4) gpu_view.value[i]=-gpu_view.value[i];
        ath_matrix4_multiply(&clip_matrix,&gpu_view,model);
        result=athena_render3d_clip_mesh_textured(&mesh,&clip_matrix,&shade,submit_clipped,&submission,stats);
    }
    if(!pass.grouped) pass_close();
    stats->draw_passes++; return result;
}
static int draw_mesh(const AthenaMesh3D *source,const AthenaMatrix4 *model,
    AthenaCamera3D *camera,const AthenaLights *lights,AthenaRender3DCull cull,
    AthenaRender3DStats *stats,int relation) {
    if(!source) return -1;
    AthenaMesh3DView view; athena_mesh3d_view(source,&view);
    return draw_view(&view,model,camera,lights,cull,stats,relation,NULL);
}
int athena_render3d_draw_mesh(const AthenaMesh3D *mesh,const AthenaMatrix4 *model,
    AthenaCamera3D *camera,const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *stats) {
    return draw_mesh(mesh,model,camera,lights,cull,stats,-1);
}
static void submit_skinned_chunk(const AthenaMesh3DView *v,uint32_t first,uint32_t count,Submission *submission) {
    uint32_t padded=(count+3)&~3u;
    /* Flags 2, GIF tag 2, five stream headers and the kick 2, plus the data. */
    size_t qwc=11+padded*(12+12+4+4+4)/16;
    owl_packet *p=owl_query_packet(CHANNEL_VIF1,qwc);
    prim_reg_t prim={.PRIM=GS_PRIM_PRIM_TRIANGLE,.IIP=1,.TME=0,.FGE=submission->fog?1:0,.CTXT=submission->context};
    giftag_t tag={.EOP=1,.PRE=1,.PRIM=prim.data,.NREG=3};
    owl_add_unpack_data_cnt(p,26,1,0);
    owl_add_float(p,1); owl_add_uint(p,0); owl_add_uint(p,1); owl_add_float(p,submission->cull);
    owl_add_unpack_data_cnt(p,0,1,1); owl_add_ulong(p,tag.data); owl_add_ulong(p,register_list(submission->fog));
    inline_packed(p,2,&v->positions[first],count,UNPACK_V3_32,12,1);
    inline_packed(p,34,&v->normals[first],count,UNPACK_V3_32,12,1);
    inline_packed(p,66,&v->colors[first],count,UNPACK_V4_8,4,1);
    inline_packed(p,98,&v->joints[first*4],count,UNPACK_V4_8,4,1);
    inline_packed(p,130,&v->weights8[first*4],count,UNPACK_V4_8,4,1);
    owl_add_cnt_tag(p,1,0);
    owl_add_uint(p,VIF_CODE(0,0,VIF_FLUSH,0));
    owl_add_uint(p,VIF_CODE(0,0,VIF_NOP,0));
    owl_add_uint(p,VIF_CODE(count,0,VIF_ITOP,0));
    owl_add_uint(p,VIF_CODE(submission->address,0,VIF_MSCALF,0));
    submission->stats->vu_batches++;
    submission->stats->geometry_bytes+=padded*36;
}
int athena_render3d_draw_skinned_contained(const AthenaMesh3DView *v,const AthenaMatrix4 *palette,
    uint32_t joint_count,AthenaCamera3D *camera,const AthenaLights *lights,AthenaRender3DCull cull,
    AthenaRender3DStats *stats) {
    if(!v||!palette||!camera||!stats||(cull!=0&&cull!=1&&cull!=-1)||!v->joints||!v->weights8||!v->normals||
        v->material.texture||!joint_count||joint_count>ATHENA_RENDER3D_SKIN_JOINTS||v->joint_count>joint_count) return -1;
    if(!athena_camera3d_update(camera)) return -1;
    for(uint32_t j=0;j<joint_count;j++) for(int k=0;k<16;k++) if(!athena_float_isfinite(palette[j].value[k])) return -1;
    stats->submitted_objects++;
    AthenaMatrix4 identity; ath_matrix4_identity(&identity);
    AthenaShade3D shade;
    if(!athena_render3d_shade_prepare(&shade,v,&identity,lights)) return -1;
    if(!shade.enabled) {
        /* Unlit: white ambient and no lights leave the vertex color as is. */
        ath_matrix4_identity(&shade.normal_matrix); memset(&shade.lights,0,sizeof(shade.lights));
        shade.lights.ambient[0]=shade.lights.ambient[1]=shade.lights.ambient[2]=1;
    }
    graphics_service_init();
    GSCONTEXT *gs=getGSGLOBAL();
    if(!gs||!gs->ZBuffering) return -3;
    if(owl_get_controller()->size<=10+R3D_SKIN_BATCH*36/16+1) return -2;
    vu_mpg **selected=&skinned_program;
    int same=pass.open&&pass.program==selected&&pass.texture==NULL&&pass.camera==camera&&pass.context==gs->PrimContext;
    if(pass.open&&!same) pass_close();
    if(!same) {
        if(!skinned_program) {
            skinned_program=vu_mpg_load_buffer(embed_vu_code_ptr(VU1Draw3DSkinned),embed_vu_code_size(VU1Draw3DSkinned),
                VECTOR_UNIT_1,false);
            if(!skinned_program) return -2;
        }
        AthenaTexture3DBinding binding={0};
        int code=pass_open(selected,NULL,&binding,camera,&identity,gs,stats);
        if(code<0) return code;
    } else {
        owl_packet *p=owl_query_packet(CHANNEL_VIF1,6);
        owl_add_cnt_tag(p,0,owl_vif_code_double(VIF_CODE(0,0,VIF_FLUSHE,0),VIF_CODE(0,0,VIF_NOP,0)));
        inline_matrix(p,5,&identity);
    }
    /* Normal matrix, lights and the palette (four columns per joint). */
    owl_packet *p=owl_query_packet(CHANNEL_VIF1,17+1+joint_count*4+(shade.lights.point_count?9:0));
    inline_matrix(p,27,&shade.normal_matrix);
    owl_add_unpack_data_cnt(p,10,6,0);
    for(unsigned i=0;i<ATHENA_LIGHTS_MAX_DIRECTIONAL;i++) owl_add_uquad_ptr(p,(void *)shade.lights.direction[i]);
    owl_add_uquad_ptr(p,(void *)shade.lights.ambient);
    owl_add_uint(p,0); owl_add_uint(p,0); owl_add_uint(p,shade.lights.point_count); owl_add_uint(p,shade.lights.count);
    if(shade.lights.point_count) inline_point_lights(p,&shade.lights);
    owl_add_unpack_data_cnt(p,18,4,0);
    for(unsigned i=0;i<ATHENA_LIGHTS_MAX_DIRECTIONAL;i++) owl_add_uquad_ptr(p,(void *)shade.lights.diffuse[i]);
    owl_add_unpack_data_cnt(p,R3D_SKIN_PALETTE,joint_count*4,0);
    for(uint32_t j=0;j<joint_count;j++) for(int c=0;c<4;c++) owl_add_uquad_ptr(p,(void *)&palette[j].value[c*4]);
    int fog=fog_prepare(athena_lights_peek(lights),gs);
    Submission submission={pass.address,gs->PrimContext,0,cull,stats,1,fog};
    for(uint32_t first=0;first<v->vertex_count;first+=R3D_SKIN_BATCH) {
        uint32_t count=v->vertex_count-first; if(count>R3D_SKIN_BATCH) count=R3D_SKIN_BATCH;
        submit_skinned_chunk(v,first,count,&submission);
    }
    stats->source_triangles+=v->vertex_count/3; stats->triangles+=v->vertex_count/3;
    if(!pass.grouped) pass_close();
    stats->draw_passes++; return 0;
}
int athena_render3d_draw_view(const AthenaMesh3DView *view,const AthenaMatrix4 *model,
    AthenaCamera3D *camera,const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *stats) {
    return draw_view(view,model,camera,lights,cull,stats,-1,NULL);
}
int athena_render3d_draw_morph(const AthenaMesh3DView *view,const AthenaMatrix4 *model,const float *weights,
    AthenaCamera3D *camera,const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *stats) {
    if(!view||!weights||!view->target_count||!view->target_positions) return -1;
    if(view->target_normals) return 1;
    MorphSet morph={0};
    for(uint32_t t=0;t<view->target_count;t++) {
        if(!athena_float_isfinite(weights[t])) return -1;
        if(weights[t]==0) continue;
        if(morph.count==ATHENA_RENDER3D_MORPH_TARGETS) return 1;
        morph.index[morph.count]=t; morph.weight[morph.count++]=weights[t];
    }
    morph.flat=view->flat_normals;
    return draw_view(view,model,camera,lights,cull,stats,-1,&morph);
}
int athena_render3d_draw_mesh_contained(const AthenaMesh3D *mesh,const AthenaMatrix4 *model,
    AthenaCamera3D *camera,const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *stats) {
    return draw_mesh(mesh,model,camera,lights,cull,stats,ATHENA_FRUSTUM3D_INSIDE);
}
int athena_render3d_draw(AthenaInstance3D *instance,AthenaCamera3D *camera,
    AthenaRender3DCull cull,AthenaRender3DStats *stats) {
    return athena_render3d_draw_lit(instance,camera,NULL,cull,stats);
}
void athena_render3d_module_shutdown(void) {
    athena_render3d_group_end();
    owl_wait_generation(owl_flush_generation());
    if(program) { vu_mpg_unload(program); program=NULL; }
    if(diffuse_program) { vu_mpg_unload(diffuse_program); diffuse_program=NULL; }
    if(texture_program) { vu_mpg_unload(texture_program); texture_program=NULL; }
    if(skinned_program) { vu_mpg_unload(skinned_program); skinned_program=NULL; }
    if(near_program) { vu_mpg_unload(near_program); near_program=NULL; }
    if(morph_program) { vu_mpg_unload(morph_program); morph_program=NULL; }
}
