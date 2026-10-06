#include <string.h>
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
static vu_mpg *program,*diffuse_program,*texture_program;
/* Same static triangle layout as the legacy color microprogram. */
#define R3D_BATCH ATHENA_RENDER3D_CHUNK
#define R3D_BASE 141u
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
typedef struct { int address,context,textured; AthenaRender3DCull cull; AthenaRender3DStats *stats; } Submission;
static void submit_chunk(const void *positions,const AthenaColor3D *colors,uint32_t count,
    int pretransformed,const AthenaPosition3D *normals,const AthenaTexcoord3D *texcoords,Submission *submission) {
    uint32_t padded=(count+3)&~3u,position_bytes=pretransformed?16u:12u;
    uint32_t vertex_bytes=position_bytes+4+(normals?12:0)+(texcoords?8:0);
    size_t qwc=8+(normals?1:0)+(texcoords?1:0)+padded*vertex_bytes/16;
    owl_packet *p=owl_query_packet(CHANNEL_VIF1,qwc);
    prim_reg_t prim={.PRIM=GS_PRIM_PRIM_TRIANGLE,.IIP=1,.TME=submission->textured,.CTXT=submission->context};
    giftag_t tag={.EOP=1,.PRE=1,.PRIM=prim.data,.NREG=3};
    owl_add_unpack_data_cnt(p,26,1,0);
    owl_add_float(p,1); owl_add_uint(p,pretransformed); owl_add_uint(p,normals!=NULL); owl_add_float(p,submission->cull);
    owl_add_unpack_data_cnt(p,0,1,1); owl_add_ulong(p,tag.data); owl_add_ulong(p,DRAW_STQ2_REGLIST);
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
/* relation < 0: test the mesh bounds here; otherwise the caller's result. */
static int draw_mesh(const AthenaMesh3D *source,const AthenaMatrix4 *model,
    AthenaCamera3D *camera,const AthenaLights *lights,AthenaRender3DCull cull,
    AthenaRender3DStats *stats,int relation) {
    if(!source||!model||!camera||!stats||(cull!=0&&cull!=1&&cull!=-1)) return -1;
    if(!athena_camera3d_update(camera)) return -1;
    AthenaMesh3DView mesh; athena_mesh3d_view(source,&mesh);
    stats->submitted_objects++;
    if(relation<0) relation=athena_camera3d_box_relation(camera,model,mesh.minimum,mesh.maximum);
    else for(int i=0;i<16;i++) if(!athena_float_isfinite(model->value[i])) return -1;
    if(relation<0) return -1;
    if(relation==ATHENA_FRUSTUM3D_OUTSIDE) {
        stats->culled_objects++; return 0;
    }
    AthenaShade3D shade;
    if(!athena_render3d_shade_prepare(&shade,&mesh,model,lights)) return -1;
    int vu_diffuse=shade.enabled&&relation==ATHENA_FRUSTUM3D_INSIDE;
    graphics_service_init();
    GSCONTEXT *gs=getGSGLOBAL();
    if(!gs||!gs->ZBuffering) return -3;
    if(owl_get_controller()->size<=10+R3D_BATCH*36/16+1) return -2;
    AthenaTexture3DBinding texture;
    int textured=mesh.material.texture!=NULL;
    if(textured) {
        int code=athena_texture3d_bind(mesh.material.texture,&texture);
        if(code<0) return code;
    }
    vu_mpg **selected=textured?&texture_program:vu_diffuse?&diffuse_program:&program;
    if(!*selected) {
        *selected=vu_mpg_load_buffer(textured?embed_vu_code_ptr(VU1Draw3DTexture):vu_diffuse?embed_vu_code_ptr(VU1Draw3DDiffuse):embed_vu_code_ptr(VU1Draw3DCS),
            textured?embed_vu_code_size(VU1Draw3DTexture):vu_diffuse?embed_vu_code_size(VU1Draw3DDiffuse):embed_vu_code_size(VU1Draw3DCS),VECTOR_UNIT_1,false);
        if(!*selected) return -2;
    }
    /* Barrier before overwriting BASE/OFFSET and shared VU1 constants. */
    owl_packet *p=owl_query_packet(CHANNEL_VIF1,1);
    owl_add_cnt_tag(p,0,owl_vif_code_double(VIF_CODE(0,0,VIF_FLUSHA,0),VIF_CODE(0,0,VIF_NOP,0)));
    vu1_set_double_buffer_settings(R3D_BASE,R3D_OFFSET);
    int address=vu_mpg_preload(*selected,true);
    if(address<0) return -2;
    vu1_invalidate_static_data();
    AthenaMatrix4 gpu_view=camera->view_projection;
    /* Public camera coordinates are Y-up; the GS viewport is Y-down. */
    for(int i=1;i<16;i+=4) gpu_view.value[i]=-gpu_view.value[i];
    int height=gs->Height*((gs->Interlace==GS_INTERLACED&&gs->Field==GS_FRAME)?2:1);
    uint32_t max_z=(gs->PSMZ==GS_ZBUF_16||gs->PSMZ==GS_ZBUF_16S)?0xffffu:
        gs->PSMZ==GS_ZBUF_24?0xffffffu:0x7fffff00u;
    float scale[4] __attribute__((aligned(16)))={gs->Width/2.0f,height/2.0f,max_z/2.0f,0};
    p=owl_query_packet(CHANNEL_VIF1,12);
    owl_add_unpack_data_cnt(p,0,1,0); owl_add_uquad_ptr(p,(void *)scale);
    inline_matrix(p,1,&gpu_view); inline_matrix(p,5,model);
    if(vu_diffuse) {
        p=owl_query_packet(CHANNEL_VIF1,17);
        inline_matrix(p,27,&shade.normal_matrix);
        owl_add_unpack_data_cnt(p,10,6,0);
        for(unsigned i=0;i<ATHENA_LIGHTS_MAX_DIRECTIONAL;i++) owl_add_uquad_ptr(p,(void *)shade.lights.direction[i]);
        owl_add_uquad_ptr(p,(void *)shade.lights.ambient);
        owl_add_uint(p,0); owl_add_uint(p,0); owl_add_uint(p,0); owl_add_uint(p,shade.lights.count);
        owl_add_unpack_data_cnt(p,18,4,0);
        for(unsigned i=0;i<ATHENA_LIGHTS_MAX_DIRECTIONAL;i++) owl_add_uquad_ptr(p,(void *)shade.lights.diffuse[i]);
    }
    /* A 3D pass owns reversed-depth testing and depth writes, then restores
     * the caller's cached state. The default 2D screen uses DEPTH_ALWAYS. */
    int test_reg=GS_CACHE_TEST+gs->PrimContext,zbuf_reg=GS_CACHE_ZBUF+gs->PrimContext;
    uint64_t saved_test=get_register(test_reg),saved_zbuf=get_register(zbuf_reg);
    int tex0_reg=GS_CACHE_TEX0+gs->PrimContext,tex1_reg=GS_CACHE_TEX1+gs->PrimContext,clamp_reg=GS_CACHE_CLAMP+gs->PrimContext;
    uint64_t saved_tex0=0,saved_tex1=0,saved_clamp=0;
    if(textured) {
        saved_tex0=get_register(tex0_reg); saved_tex1=get_register(tex1_reg); saved_clamp=get_register(clamp_reg);
        /* Force the pass state, then restore through the cache. TileMap may
         * write texture registers directly without updating the shared cache. */
        set_register_force(tex0_reg,texture.tex0);
        set_register_force(tex1_reg,texture.tex1);
        set_register_force(clamp_reg,texture.clamp);
    }
    set_register(test_reg,GS_SETREG_TEST(0,1,0x80,0,0,0,1,DEPTH_GEQUAL));
    set_register(zbuf_reg,saved_zbuf&~(1ull<<32));
    Submission submission={address,gs->PrimContext,textured,cull,stats};
    int result=0;
    if(relation==ATHENA_FRUSTUM3D_INSIDE) {
        for(uint32_t first=0;first<mesh.vertex_count;first+=R3D_BATCH) {
            uint32_t count=mesh.vertex_count-first; if(count>R3D_BATCH) count=R3D_BATCH;
            submit_chunk(&mesh.positions[first],&mesh.colors[first],count,0,vu_diffuse?&mesh.normals[first]:NULL,
                textured?&mesh.texcoords[first]:NULL,&submission);
        }
        stats->source_triangles+=mesh.vertex_count/3;
        stats->triangles+=mesh.vertex_count/3;
    } else {
        AthenaMatrix4 clip_matrix;
        ath_matrix4_multiply(&clip_matrix,&gpu_view,model);
        result=athena_render3d_clip_mesh_textured(&mesh,&clip_matrix,&shade,submit_clipped,&submission,stats);
    }
    /* Wait for PATH1 before restoring TEST through PATH2 (VIF DIRECT). */
    p=owl_query_packet(CHANNEL_VIF1,1);
    owl_add_cnt_tag(p,0,owl_vif_code_double(VIF_CODE(0,0,VIF_FLUSHA,0),VIF_CODE(0,0,VIF_NOP,0)));
    set_register(test_reg,saved_test); set_register(zbuf_reg,saved_zbuf);
    if(textured) { set_register(tex0_reg,saved_tex0); set_register(tex1_reg,saved_tex1); set_register(clamp_reg,saved_clamp); }
    stats->draw_passes++; return result;
}
int athena_render3d_draw_mesh(const AthenaMesh3D *mesh,const AthenaMatrix4 *model,
    AthenaCamera3D *camera,const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *stats) {
    return draw_mesh(mesh,model,camera,lights,cull,stats,-1);
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
    owl_wait_generation(owl_flush_generation());
    if(program) { vu_mpg_unload(program); program=NULL; }
    if(diffuse_program) { vu_mpg_unload(diffuse_program); diffuse_program=NULL; }
    if(texture_program) { vu_mpg_unload(texture_program); texture_program=NULL; }
}
