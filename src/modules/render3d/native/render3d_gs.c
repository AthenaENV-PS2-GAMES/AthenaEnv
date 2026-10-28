#include <athena/memory_retirement.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <malloc.h>
#include <athena/float_bits.h>
#include <athena/render3d.h>
#include <athena/graphics.h>
#include <athena/graphics/sync.h>
#include <athena/graphics/owl_packet.h>
#include <athena/graphics/mpg_manager.h>
#include "render3d_clip.h"
#include "../../model3d/native/texture3d_backend.h"

#if defined(ATHENA_MEMORY_DIAGNOSTICS) && ATHENA_MEMORY_DIAGNOSTICS
#define ATHENA_MEMORY_CATEGORY ATHENA_MEM_GEOMETRY
#include <athena/memory_tags.h>
#endif
register_vu_program(VU1Draw3DCS);
register_vu_program(VU1Draw3DDiffuse);
register_vu_program(VU1Draw3DTexture);
register_vu_program(VU1Draw3DSkinned);
register_vu_program(VU1Draw3DNear);
register_vu_program(VU1Draw3DMorph);
register_vu_program(VU1Draw3DIndexed);
register_vu_program(VU1Draw3DIndexedSkinned);
register_vu_program(VU1Draw3DIndexedMorph);
static vu_mpg *indexed_program,*indexed_skin_program,*indexed_morph_program;
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
#define R3D_OFFSET 420u
/* The host packet test supplies an address mapper for REF tags. The build
 * switch also permits a controlled inline/REF comparison on the EE. */
#ifndef ATHENA_RENDER3D_DMA_REF
#if defined(__mips__)
#define ATHENA_RENDER3D_DMA_REF 1
#else
#define ATHENA_RENDER3D_DMA_REF 0
#endif
#endif

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
/* dma: the mesh whose immutable streams the chunks reference in place (REF
 * tags), or NULL to copy them into the packet. */
static int blend_mode=-1;
static struct { AthenaTexture3D *texture; GSCONTEXT saved; int blend;
    uint64_t frame,xyoffset,scissor,test,zbuf,fba; } capture;
static AthenaColor3D *silhouette_colors;
static uint32_t silhouette_capacity;
static int silhouette_view(const AthenaMesh3DView *source,AthenaMesh3DView *out) {
    uint32_t count=athena_mesh3d_stream_count(source)+4;
    if(count>silhouette_capacity) {
        AthenaColor3D *colors=memalign(16,count*sizeof(*colors));
        if(!colors) return -2;
        memset(colors,255,count*sizeof(*colors));
        free(silhouette_colors); silhouette_colors=colors; silhouette_capacity=count;
    }
    *out=*source; out->colors=silhouette_colors;
    athena_material3d_default(&out->material); return 0;
}
typedef struct { int address,context,textured; AthenaRender3DCull cull; AthenaRender3DStats *stats; float color_scale; int fog;
    AthenaMesh3D *dma; } Submission;
/* Meshes referenced by DMA chains not yet read, oldest first: each is
 * retained once per generation (athena_mesh3d_dma_claim) and released when
 * owl_generation_read() proves that chain consumed. */
static struct { AthenaMesh3D *mesh; uint64_t generation; } *retained;
static uint32_t retained_head,retained_count,retained_capacity;
/* Releases consumed heads in FIFO order, visiting at most `budget` entries
 * (UINT32_MAX: unbounded). An unready head stops the walk and still counts as
 * a visit; later entries are never examined past it. */
static void dma_collect(uint32_t budget) {
    while(budget&&retained_count) {
        budget--;
        if(!owl_generation_read(retained[retained_head].generation)) break;
        athena_mesh3d_release(retained[retained_head].mesh);
        retained_head++; retained_count--;
    }
    if(!retained_count) retained_head=0;
}
static void dma_drain(int all) {
    if(all&&retained_count) owl_wait_generation(retained[retained_head+retained_count-1].generation);
    dma_collect(UINT32_MAX);
}
/* Completed-frame hook (graphics_frame_finished, after the existing FINISH
 * wait; no new fence). Same owl_generation_read predicate as dma_drain, at
 * most eight entries visited per frame, so a long queue drains over several
 * frames and an idle scene still releases consumed meshes. Releasing a mesh
 * may hand its texture to Texture3D Pending; that collector runs after this. */
#define RENDER3D_FRAME_COLLECT_BUDGET 8u
void athena_render3d_collect_frame(void) {
    if(retained_count) dma_collect(RENDER3D_FRAME_COLLECT_BUDGET);
}
/* Room for one more entry, before a packet query: 0 (no memory) means the
 * chunk is copied instead. Afterwards dma_retain() cannot fail. */
static int dma_reserve(void) {
    if(retained_head+retained_count<retained_capacity) return 1;
    if(retained_head) {
        memmove(retained,&retained[retained_head],retained_count*sizeof(*retained)); retained_head=0; return 1;
    }
    uint32_t next=retained_capacity?retained_capacity*2:64;
    void *grown=realloc(retained,next*sizeof(*retained)); if(!grown) return 0;
    retained=grown; retained_capacity=next; return 1;
}
/* Retains mesh for the chain being authored (once per generation). */
static void dma_retain(AthenaMesh3D *mesh) {
    uint64_t generation=owl_flush_generation();
    if(!athena_mesh3d_dma_claim(mesh,generation)) return;
    athena_mesh3d_retain(mesh);
    retained[retained_head+retained_count].mesh=mesh; retained[retained_head+retained_count].generation=generation;
    retained_count++;
}
/* One stream: a REF tag to data in place (ref), or a CNT copy. VIF unpacks
 * whole groups of four vertices: the source has that padding. */
static void stream(owl_packet *packet,uint32_t dest,const void *data,uint32_t count,
    uint32_t format,uint32_t bytes,int ref) {
    if(ref) {
        uint32_t padded=(count+3)&~3u,qwc=padded*bytes/16;
        owl_add_ulong(packet,DMA_TAG(qwc,0,DMA_REF,0,(uintptr_t)data,0));
        owl_add_uint(packet,VIF_CODE(0x0101,0,VIF_STCYCL,0));
        owl_add_uint(packet,VIF_CODE(dest|(1u<<14)|(1u<<15),padded,format|0x60,0));
        return;
    }
    inline_packed(packet,dest,data,count,format,bytes,1);
}
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
    int keep=ATHENA_RENDER3D_DMA_REF&&submission->dma&&!pretransformed&&dma_reserve();
    int ref=keep;
    size_t qwc=8+(normals?1:0)+(texcoords?1:0)+(ref?0:padded*vertex_bytes/16);
    owl_packet *p=owl_query_packet(CHANNEL_VIF1,qwc);
    /* After the query, which may have flushed: this chunk's generation. */
    if(keep) dma_retain(submission->dma);
    prim_reg_t prim={.PRIM=GS_PRIM_PRIM_TRIANGLE,.IIP=1,.TME=submission->textured,.ABE=blend_mode>=0,.FGE=submission->fog?1:0,.CTXT=submission->context};
    giftag_t tag={.EOP=1,.PRE=1,.PRIM=prim.data,.NREG=3};
    owl_add_unpack_data_cnt(p,26,1,0);
    owl_add_float(p,submission->color_scale); owl_add_uint(p,pretransformed); owl_add_uint(p,normals!=NULL); owl_add_float(p,submission->cull);
    owl_add_unpack_data_cnt(p,0,1,1); owl_add_ulong(p,tag.data); owl_add_ulong(p,register_list(submission->fog));
    stream(p,2,positions,count,pretransformed?UNPACK_V4_32:UNPACK_V3_32,position_bytes,ref);
    stream(p,98,colors,count,UNPACK_V4_8,4,ref);
    if(normals) stream(p,50,normals,count,UNPACK_V3_32,12,ref);
    if(texcoords) stream(p,146,texcoords,count,UNPACK_V2_32,8,ref);
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
    prim_reg_t prim={.PRIM=GS_PRIM_PRIM_TRIANGLE,.IIP=1,.TME=submission->textured,.ABE=blend_mode>=0,.FGE=submission->fog?1:0,.CTXT=submission->context};
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
/* Batches and all streams were prepared at mesh creation. Only indices for
 * the original corner order are uploaded; projection/shading happen once per
 * local vertex. Temporary deformed views are copied, immutable meshes use REF. */
/* Indexed flags are constant across all batches of this object. The object
 * setup has already fenced the preceding VU1 invocation before these writes. */
static void prepare_indexed(const Submission *submission,int lit) {
    owl_packet *p=owl_query_packet(CHANNEL_VIF1,2);
    owl_add_unpack_data_cnt(p,26,1,0);
    owl_add_float(p,submission->textured?128:255);
    owl_add_uint(p,submission->textured);
    owl_add_uint(p,lit);
    owl_add_float(p,submission->cull);
}
static void submit_indexed(const AthenaMesh3DView *v,uint32_t batch,int lit,int skin,
    const MorphSet *morph,Submission *submission) {
    const AthenaMesh3DChunk *chunk=&v->chunks[batch];uint32_t first=chunk->stream_first;
    uint32_t count=chunk->vertex_count,padded=(count+3)&~3u;
    uint32_t targets=morph?morph->count:0;
    uint32_t stride=16+(lit?12:0)+(submission->textured?8:0)+(skin?8:0)+targets*12;
    int keep=ATHENA_RENDER3D_DMA_REF&&submission->dma&&dma_reserve();
    size_t qwc=9+(lit?1:0)+(submission->textured?1:0)+(skin?2:0)+targets+(keep?0:padded*stride/16+3);
    owl_packet *p=owl_query_packet(CHANNEL_VIF1,qwc);
    if(keep)dma_retain(submission->dma);
    prim_reg_t prim={.PRIM=GS_PRIM_PRIM_TRIANGLE,.IIP=1,.TME=submission->textured,.ABE=blend_mode>=0,.FGE=submission->fog?1:0,.CTXT=submission->context};
    giftag_t tag={.EOP=1,.PRE=1,.PRIM=prim.data,.NREG=3};
    owl_add_unpack_data_cnt(p,0,1,1);owl_add_ulong(p,tag.data);owl_add_ulong(p,register_list(submission->fog));
    owl_add_unpack_data_cnt(p,1,1,1);owl_add_uint(p,count);owl_add_uint(p,0);owl_add_uint(p,0);owl_add_uint(p,0);
    stream(p,2,&v->positions[first],count,UNPACK_V3_32,12,keep);
    stream(p,50,&v->colors[first],count,UNPACK_V4_8,4,keep);
    if(lit)stream(p,26,&v->normals[first],count,UNPACK_V3_32,12,keep);
    if(submission->textured)stream(p,74,&v->texcoords[first],count,UNPACK_V2_32,8,keep);
    /* S_8 expands one index into each qword. 48 bytes/48 outputs: padding
     * lands in the not-yet-authored vertex cache, never in live indices. */
    stream(p,98,v->chunk_indices+batch*ATHENA_MESH3D_INDEXED_INDEX_BYTES,48,2,1,keep);
    if(skin) {
        stream(p,146,&v->joints[first*4],count,UNPACK_V4_8,4,keep);
        stream(p,170,&v->weights8[first*4],count,UNPACK_V4_8,4,keep);
    }
    for(uint32_t t=0;t<targets;t++)
        stream(p,146+16*t,&v->target_positions[morph->index[t]*athena_mesh3d_stream_count(v)+first],count,UNPACK_V3_32,12,keep);
    owl_add_cnt_tag(p,1,0);
    owl_add_uint(p,VIF_CODE(0,0,VIF_FLUSH,0));owl_add_uint(p,VIF_CODE(0,0,VIF_NOP,0));
    owl_add_uint(p,VIF_CODE(chunk->index_count,0,VIF_ITOP,0));owl_add_uint(p,VIF_CODE(submission->address,0,VIF_MSCALF,0));
    submission->stats->vu_batches++;submission->stats->geometry_bytes+=padded*stride+48;
}
/* Near clipping still consumes triangle lists: gather a bounded temporary
 * chunk. Other indexed paths keep the immutable compact streams in place. */
static void submit_corners(const AthenaMesh3DView *v,uint32_t first,uint32_t count,int lit,Submission *s) {
    if(!v->indices) {submit_chunk(&v->positions[first],&v->colors[first],count,0,lit?&v->normals[first]:NULL,s->textured?&v->texcoords[first]:NULL,s);return;}
    AthenaPosition3D positions[R3D_BATCH+4] __attribute__((aligned(16)))={{0}},normals[R3D_BATCH+4] __attribute__((aligned(16)))={{0}};
    AthenaColor3D colors[R3D_BATCH+4] __attribute__((aligned(16)))={{0}};
    AthenaTexcoord3D uv[R3D_BATCH+4] __attribute__((aligned(16)))={{0}};
    for(uint32_t i=0;i<count;i++) {
        uint32_t j=athena_mesh3d_corner(v,first+i);positions[i]=v->positions[j];colors[i]=v->colors[j];
        if(lit)normals[i]=v->normals[j];
        if(s->textured)uv[i]=v->texcoords[j];
    }
    Submission temporary=*s;temporary.dma=NULL;
    submit_chunk(positions,colors,count,0,lit?normals:NULL,s->textured?uv:NULL,&temporary);
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
    if(!model) { athena_render3d_set_error_detail("instance transform overflows"); return -1; }
    return athena_render3d_draw_mesh(athena_instance3d_mesh(instance),model,camera,lights,cull,stats);
}
/* The open pass: its key (program, texture/camera revisions, GS context) and the GS
 * registers it overrides. Outside a group it closes after every draw. */
static struct {
    int open,grouped,address,context;
    vu_mpg **program; uint64_t texture_stamp,camera_stamp;
    /* Alpha test: 0 off, else 1 + AREF (0..128) of an alpha-mask material. */
    int alpha,blend;
    int blend_reg; uint64_t saved_blend;
    int test_reg,zbuf_reg,tex0_reg,tex1_reg,clamp_reg;
    uint64_t saved_test,saved_zbuf,saved_tex0,saved_tex1,saved_clamp;
    /* Fog parameters in VU memory (vu1/include/fog.i) and the GS FOGCOL
     * last written; fog_valid is cleared when a pass opens. */
    float fog[4] __attribute__((aligned(16))); int fog_valid,fog_on; uint64_t fogcol; int fogcol_valid;
    /* Lights in VU memory (10..21, 132..139): their stamp, valid since the
     * pass opened. Objects of one pass usually share them. */
    uint64_t lights_stamp; int lights_valid;
    /* Per-object RGB multiplier in VU memory (quad 16): tint times the program's color
     * scale (255, or 128 for GS MODULATE). Valid since the pass opened. */
    float rgb[4] __attribute__((aligned(16))); int rgb_valid;
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
/* The draw's tint (athena_render3d_set_tint), uploaded when it differs from the one already in
 * VU memory for this pass: draws of different tints never inherit each other's. Every program
 * multiplies the final vertex RGB (after lighting) by quad 16. */
static void upload_tint(int textured) {
    float tint[3]={1,1,1}; if(!capture.texture) athena_render3d_get_tint(tint);
    float scale=textured?128.0f:255.0f;
    float rgb[4] __attribute__((aligned(16)))={tint[0]*scale,tint[1]*scale,tint[2]*scale,0};
    if(pass.rgb_valid&&!memcmp(pass.rgb,rgb,sizeof(rgb))) return;
    owl_packet *p=owl_query_packet(CHANNEL_VIF1,2);
    owl_add_unpack_data_cnt(p,16,1,0); owl_add_uquad_ptr(p,(void *)rgb);
    memcpy(pass.rgb,rgb,sizeof(rgb)); pass.rgb_valid=1;
}
static void pass_close(void) {
    if(!pass.open) return;
    /* Wait for PATH1 before restoring TEST through PATH2 (VIF DIRECT). */
    owl_packet *p=owl_query_packet(CHANNEL_VIF1,1);
    owl_add_cnt_tag(p,0,owl_vif_code_double(VIF_CODE(0,0,VIF_FLUSHA,0),VIF_CODE(0,0,VIF_NOP,0)));
    set_register(pass.test_reg,pass.saved_test); set_register(pass.zbuf_reg,pass.saved_zbuf);
    if(pass.blend>=0) set_register(pass.blend_reg,pass.saved_blend);
    if(pass.texture_stamp) {
        set_register(pass.tex0_reg,pass.saved_tex0); set_register(pass.tex1_reg,pass.saved_tex1);
        set_register(pass.clamp_reg,pass.saved_clamp);
    }
    pass.open=0;
}
/* The per-object normal matrix, and the lights when they differ from those
 * already in VU memory for this pass (same stamp: same contents). */
static void upload_shade(const AthenaShade3D *shade) {
    owl_packet *p=owl_query_packet(CHANNEL_VIF1,5);
    inline_matrix(p,27,&shade->normal_matrix);
    if(pass.lights_valid&&pass.lights_stamp==shade->lights.stamp) return;
    p=owl_query_packet(CHANNEL_VIF1,12+(shade->lights.point_count?9:0));
    owl_add_unpack_data_cnt(p,10,6,0);
    for(unsigned i=0;i<ATHENA_LIGHTS_MAX_DIRECTIONAL;i++) owl_add_uquad_ptr(p,(void *)shade->lights.direction[i]);
    owl_add_uquad_ptr(p,(void *)shade->lights.ambient);
    owl_add_uint(p,0); owl_add_uint(p,0); owl_add_uint(p,shade->lights.point_count); owl_add_uint(p,shade->lights.count);
    if(shade->lights.point_count) inline_point_lights(p,&shade->lights);
    owl_add_unpack_data_cnt(p,18,4,0);
    for(unsigned i=0;i<ATHENA_LIGHTS_MAX_DIRECTIONAL;i++) owl_add_uquad_ptr(p,(void *)shade->lights.diffuse[i]);
    pass.lights_stamp=shade->lights.stamp; pass.lights_valid=1;
}
/* Also uploads the first object's model matrix with the camera constants. */
static int pass_open(vu_mpg **program,const AthenaTexture3D *texture,const AthenaTexture3DBinding *binding,
    const AthenaCamera3D *camera,const AthenaMatrix4 *model,GSCONTEXT *gs,int alpha,AthenaRender3DStats *stats) {
    /* Barrier before overwriting BASE/OFFSET and shared VU1 constants. */
    owl_packet *p=owl_query_packet(CHANNEL_VIF1,1);
    owl_add_cnt_tag(p,0,owl_vif_code_double(VIF_CODE(0,0,VIF_FLUSHA,0),VIF_CODE(0,0,VIF_NOP,0)));
    vu1_set_double_buffer_settings(R3D_BASE,R3D_OFFSET);
    int address=vu_mpg_preload(*program,true);
    if(address<0) { athena_render3d_set_error_detail("VU1 microprogram cannot be loaded"); return -2; }
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
        /* TCC: alpha-mask passes take the texture alpha (MODULATE by the
         * vertex alpha, 0x80 neutral); blended passes need it too.
         * Opaque passes keep RGB only. */
        set_register_force(pass.tex0_reg,binding->tex0|((alpha||blend_mode>=0)?1ull<<34:0));
        set_register_force(pass.tex1_reg,binding->tex1);
        set_register_force(pass.clamp_reg,binding->clamp);
    }
    /* Alpha mask: ATST GEQUAL (5) against AREF, AFAIL KEEP (neither colour
     * nor depth written for discarded pixels). */
    set_register(pass.test_reg,alpha?GS_SETREG_TEST(1,5,alpha-1,0,0,0,1,DEPTH_GEQUAL):
        GS_SETREG_TEST(0,1,0x80,0,0,0,1,capture.texture?1:DEPTH_GEQUAL));
    set_register(pass.zbuf_reg,(blend_mode>=0||capture.texture)?pass.saved_zbuf|(1ull<<32):pass.saved_zbuf&~(1ull<<32));
    pass.blend=blend_mode;
    if(blend_mode>=0) {
        pass.blend_reg=GS_CACHE_ALPHA+gs->PrimContext;
        pass.saved_blend=get_register(pass.blend_reg);
        /* (Cs-Cd)*As+Cd for darken/alpha; Cs*As+Cd for add.
         * ALPHA fields A,B,C,D; D selects RGB, not an alpha source. */
        set_register(pass.blend_reg,(uint64_t)(blend_mode==2?2:1)<<2 | 1ull<<6);
    }
    pass.open=1; pass.address=address; pass.context=gs->PrimContext; pass.fog_valid=0; pass.lights_valid=0; pass.rgb_valid=0;
    pass.program=program; pass.camera_stamp=camera->stamp;
    pass.texture_stamp=athena_texture3d_stamp(texture); pass.alpha=alpha;
    stats->pipeline_passes++; return 0;
}
int athena_render3d_capture_begin(AthenaTexture3D *texture) {
    if(capture.texture||pass.grouped||!texture) return -1;
    graphics_service_init(); GSCONTEXT *gs=getGSGLOBAL();
    if(!gs||owl_get_controller()->size<=12) return -2;
    pass_close();
    AthenaTexture3DTarget target;
    int code=athena_texture3d_target_begin(texture,&target); if(code<0) return code;
    capture.texture=texture; capture.saved=*gs; capture.blend=blend_mode;
    int ctx=gs->PrimContext;
    capture.frame=get_register(GS_CACHE_FRAME+ctx); capture.xyoffset=get_register(GS_CACHE_XYOFFSET+ctx);
    capture.scissor=get_register(GS_CACHE_SCISSOR+ctx); capture.test=get_register(GS_CACHE_TEST+ctx);
    capture.zbuf=get_register(GS_CACHE_ZBUF+ctx); capture.fba=get_register(GS_CACHE_FBA+ctx);
    gs->Width=target.width; gs->Height=target.height; gs->Interlace=0; gs->Field=0;
    gs->PSM=GS_PSM_CT32; gs->PSMZ=GS_ZBUF_16S; gs->ZBuffering=1;
    gs->OffsetX=(2048-target.width/2)*16; gs->OffsetY=(2048-target.height/2)*16;
    blend_mode=-1;
    set_register(GS_CACHE_FRAME+ctx,GS_SETREG_FRAME_1(target.address/8192,target.tbw,GS_PSM_CT32,0));
    set_register(GS_CACHE_XYOFFSET+ctx,GS_SETREG_XYOFFSET(gs->OffsetX,gs->OffsetY));
    set_register(GS_CACHE_SCISSOR+ctx,GS_SETREG_SCISSOR_1(0,target.width-1,0,target.height-1));
    set_register(GS_CACHE_ZBUF+ctx,capture.zbuf|(1ull<<32));
    set_register(GS_CACHE_FBA+ctx,0);
    /* Clear without touching depth. PATH1 was drained by target_begin. */
    owl_packet *p=owl_query_packet(CHANNEL_VIF1,11);
    owl_add_cnt_tag(p,10,0);
    owl_add_uint(p,VIF_CODE(0,0,VIF_NOP,0)); owl_add_uint(p,VIF_CODE(0,0,VIF_NOP,0));
    owl_add_uint(p,VIF_CODE(0,0,VIF_NOP,0)); owl_add_uint(p,VIF_CODE(9,0,VIF_DIRECT,0));
    owl_add_tag(p,GIF_AD,VU_GS_GIFTAG(8,1,0,0,0,0,1));
    owl_add_tag(p,GS_TEST_1+ctx,GS_SETREG_TEST(0,1,0,0,0,0,1,1));
    owl_add_tag(p,GS_XYOFFSET_1+ctx,GS_SETREG_XYOFFSET(0,0));
    owl_add_tag(p,GS_RGBAQ,0);
    prim_reg_t prim={.PRIM=6,.CTXT=ctx}; owl_add_tag(p,GS_PRIM,prim.data);
    owl_add_tag(p,GS_XYZ2,GS_SETREG_XYZ(0,0,0));
    owl_add_tag(p,GS_XYZ2,GS_SETREG_XYZ(target.width*16,target.height*16,0));
    owl_add_tag(p,GS_TEST_1+ctx,capture.test);
    owl_add_tag(p,GS_XYOFFSET_1+ctx,get_register(GS_CACHE_XYOFFSET+ctx));
    return 0;
}
void athena_render3d_capture_end(int commit) {
    if(!capture.texture) return;
    athena_render3d_group_end();
    /* Drain PATH1 before restoring the framebuffer and flushing texture cache. */
    graphics_wait_idle();
    GSCONTEXT *gs=getGSGLOBAL(); int ctx=capture.saved.PrimContext;
    set_register(GS_CACHE_FRAME+ctx,capture.frame); set_register(GS_CACHE_XYOFFSET+ctx,capture.xyoffset);
    set_register(GS_CACHE_SCISSOR+ctx,capture.scissor); set_register(GS_CACHE_TEST+ctx,capture.test);
    set_register(GS_CACHE_ZBUF+ctx,capture.zbuf); set_register(GS_CACHE_FBA+ctx,capture.fba);
    *gs=capture.saved; blend_mode=capture.blend;
    flush_gs_texcache();
    AthenaTexture3D *texture=capture.texture; capture.texture=NULL;
    athena_texture3d_target_end(texture,commit);
}
int athena_render3d_get_blend(void) { return blend_mode; }
int athena_render3d_set_blend(int mode) {
    if(mode < -1 || mode > 2 || pass.grouped || capture.texture) return -1;
    pass_close(); blend_mode=mode; return 0;
}
int athena_render3d_group_begin(void) {
    if(pass.grouped) return -1;
    dma_drain(0);
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
/* All-zero linear part (scale 0): nothing to rasterize, and no normal
 * matrix exists for lighting. */
static int collapsed(const AthenaMatrix4 *m) {
    for(int c=0;c<3;c++) for(int r=0;r<3;r++) if(m->value[c*4+r]!=0) return 0;
    return 1;
}
/* relation < 0: test the view bounds here; otherwise the caller's result. */
/* morph: NULL, or the active targets of a VU1 morph draw (then 1 is
 * returned, with nothing drawn or counted, when the mesh is not contained). */
static int draw_view(const AthenaMesh3DView *view,const AthenaMatrix4 *model,
    AthenaCamera3D *camera,const AthenaLights *lights,AthenaRender3DCull cull,
    AthenaRender3DStats *stats,int relation,const MorphSet *morph,AthenaMesh3D *dma) {
    dma_drain(0);
    if(!view||!model||!camera||!stats||(cull!=0&&cull!=1&&cull!=-1)) { athena_render3d_set_error_detail("invalid mesh, matrix, camera or cull mode"); return -1; }
    if(!athena_camera3d_update(camera)) { athena_render3d_set_error_detail("camera matrices are not finite"); return -1; }
    AthenaMesh3DView silhouette;
    if(capture.texture) {
        int code=silhouette_view(view,&silhouette); if(code<0) return code;
        view=&silhouette; lights=NULL; dma=NULL; /* transient white colors always copied */
    }
    const AthenaMesh3DView *mesh=view; /* Borrowed and immutable for this draw. */
    stats->submitted_objects++;
    graphics_service_init();
    GSCONTEXT *gs=getGSGLOBAL();
    /* -1: not tested yet; the caller's INTERSECT is tested again below. */
    int guard_inside=-1;
    float guard=guard_of(gs);
    if(relation<0) relation=athena_camera3d_box_relation_ex(camera,model,mesh->minimum,mesh->maximum,guard,&guard_inside);
    else for(int i=0;i<16;i++) if(!athena_float_isfinite(model->value[i])) { athena_render3d_set_error_detail("model matrix is not finite"); return -1; }
    if(relation<0) { athena_render3d_set_error_detail("model matrix or mesh bounds overflow in clip space"); return -1; }
    if(relation==ATHENA_FRUSTUM3D_OUTSIDE) {
        stats->culled_objects++; return 0;
    }
    if(morph&&relation==ATHENA_FRUSTUM3D_INTERSECT&&!(guard>1&&(guard_inside>=0?guard_inside:
        athena_camera3d_box_relation_guard(camera,model,mesh->minimum,mesh->maximum,guard)==ATHENA_FRUSTUM3D_INSIDE))) {
        stats->submitted_objects--; return 1;
    }
    /* Crossing only the screen edges, within the guard band: VU1 draws it
     * and the GS scissor trims it, instead of clipping in C. */
    /* Crossing the near plane only: VU1 clips it (draw_3D_near). */
    int near=0;
    if(relation==ATHENA_FRUSTUM3D_INTERSECT&&guard>1) {
        if(guard_inside<0) guard_inside=athena_camera3d_box_relation_guard(camera,model,mesh->minimum,mesh->maximum,guard)==ATHENA_FRUSTUM3D_INSIDE;
        if(guard_inside) { relation=ATHENA_FRUSTUM3D_INSIDE; stats->guard_band_objects++; }
        else if(athena_camera3d_box_near_guard(camera,model,mesh->minimum,mesh->maximum,guard)==1) {
            near=1; stats->near_clip_objects++;
        }
    }
    AthenaShade3D shade;
    if(!athena_render3d_shade_prepare(&shade,mesh,model,lights)) {
        /* Checked only here, off the common path: an unlit collapsed mesh
         * just rasterizes nothing. */
        if(mesh->normals&&collapsed(model)) { stats->culled_objects++; return 0; }
        athena_render3d_set_error_detail(!mesh->normals?"DIFFUSE material without normals":"singular or ill-conditioned model matrix for DIFFUSE lighting");
        return -1;
    }
    int vu_diffuse=shade.enabled&&(relation==ATHENA_FRUSTUM3D_INSIDE||near);
    if(morph) { near=0; relation=ATHENA_FRUSTUM3D_INSIDE; vu_diffuse=shade.enabled; }
    if(!gs||!gs->ZBuffering) { athena_render3d_set_error_detail("the screen mode has no Z buffer"); return -3; }
    if(owl_get_controller()->size<=10+R3D_BATCH*36/16+1) { athena_render3d_set_error_detail("packet buffer too small"); return -2; }
    const AthenaTexture3D *texture=mesh->material.texture;
    int indexed=mesh->indices&&mesh->chunks&&!near&&(relation==ATHENA_FRUSTUM3D_INSIDE||morph);
    vu_mpg **selected=indexed?(morph?&indexed_morph_program:&indexed_program):morph?&morph_program:near?&near_program:texture?&texture_program:vu_diffuse?&diffuse_program:&program;
    int alpha=mesh->material.alpha_mask?1+(int)(mesh->material.alpha_cutoff*128+.5f):0;
    int same=pass.open&&pass.program==selected&&pass.texture_stamp==athena_texture3d_stamp(texture)&&pass.camera_stamp==camera->stamp&&
        pass.context==gs->PrimContext&&pass.alpha==alpha&&pass.blend==blend_mode;
    /* Close before binding: a first bind may wait for idle and upload. */
    if(pass.open&&!same) pass_close();
    if(!same) {
        AthenaTexture3DBinding binding={0};
        if(texture) {
            int code=athena_texture3d_bind(mesh->material.texture,&binding);
            if(code<0) { athena_render3d_set_error_detail("texture cannot be made resident in VRAM"); return code; }
        }
        if(!*selected) {
            *selected=indexed?vu_mpg_load_buffer(morph?embed_vu_code_ptr(VU1Draw3DIndexedMorph):embed_vu_code_ptr(VU1Draw3DIndexed),
                morph?embed_vu_code_size(VU1Draw3DIndexedMorph):embed_vu_code_size(VU1Draw3DIndexed),VECTOR_UNIT_1,false):morph?vu_mpg_load_buffer(embed_vu_code_ptr(VU1Draw3DMorph),embed_vu_code_size(VU1Draw3DMorph),VECTOR_UNIT_1,false):
                near?vu_mpg_load_buffer(embed_vu_code_ptr(VU1Draw3DNear),embed_vu_code_size(VU1Draw3DNear),VECTOR_UNIT_1,false):
                vu_mpg_load_buffer(texture?embed_vu_code_ptr(VU1Draw3DTexture):vu_diffuse?embed_vu_code_ptr(VU1Draw3DDiffuse):embed_vu_code_ptr(VU1Draw3DCS),
                texture?embed_vu_code_size(VU1Draw3DTexture):vu_diffuse?embed_vu_code_size(VU1Draw3DDiffuse):embed_vu_code_size(VU1Draw3DCS),VECTOR_UNIT_1,false);
            if(!*selected) { athena_render3d_set_error_detail("VU1 microprogram cannot be allocated"); return -2; }
        }
        int code=pass_open(selected,texture,&binding,camera,model,gs,alpha,stats);
        if(code<0) return code;
    } else {
        /* The microprogram reads the object constants when it starts. Within
         * a shared pass, wait for the previous object's last chunk to end. */
        owl_packet *p=owl_query_packet(CHANNEL_VIF1,6);
        owl_add_cnt_tag(p,0,owl_vif_code_double(VIF_CODE(0,0,VIF_FLUSHE,0),VIF_CODE(0,0,VIF_NOP,0)));
        inline_matrix(p,5,model);
    }
    if(vu_diffuse) upload_shade(&shade);
    upload_tint(texture!=NULL);
    int fog=fog_prepare(athena_lights_peek(lights),gs);
    Submission submission={pass.address,gs->PrimContext,texture!=NULL,cull,stats,near||morph?(texture?128.0f:255.0f):1.0f,fog,dma};
    if(indexed) prepare_indexed(&submission,vu_diffuse);
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
        if(indexed)for(uint32_t batch=0;batch<mesh->chunk_count;batch++)submit_indexed(mesh,batch,vu_diffuse,0,morph,&submission);
        else for(uint32_t first=0;first<mesh->vertex_count;first+=R3D_MORPH_BATCH) {
            uint32_t count=mesh->vertex_count-first; if(count>R3D_MORPH_BATCH) count=R3D_MORPH_BATCH;
            submit_morph_chunk(mesh,first,count,morph,vu_diffuse,&submission);
        }
        stats->vu_morph_objects++;
        stats->source_triangles+=mesh->vertex_count/3;
        stats->triangles+=mesh->vertex_count/3;
    } else if(near) {
        for(uint32_t first=0;first<mesh->vertex_count;first+=R3D_NEAR_BATCH) {
            uint32_t count=mesh->vertex_count-first; if(count>R3D_NEAR_BATCH) count=R3D_NEAR_BATCH;
            submit_corners(mesh,first,count,vu_diffuse,&submission);
        }
        stats->source_triangles+=mesh->vertex_count/3;
        stats->triangles+=mesh->vertex_count/3;
    } else if(relation==ATHENA_FRUSTUM3D_INSIDE) {
        if(indexed)for(uint32_t batch=0;batch<mesh->chunk_count;batch++)submit_indexed(mesh,batch,vu_diffuse,0,NULL,&submission);
        else for(uint32_t first=0;first<mesh->vertex_count;first+=R3D_BATCH) {
            uint32_t count=mesh->vertex_count-first; if(count>R3D_BATCH) count=R3D_BATCH;
            submit_corners(mesh,first,count,vu_diffuse,&submission);
        }
        stats->source_triangles+=mesh->vertex_count/3;
        stats->triangles+=mesh->vertex_count/3;
    } else {
        AthenaMatrix4 gpu_view=camera->view_projection,clip_matrix;
        for(int i=1;i<16;i+=4) gpu_view.value[i]=-gpu_view.value[i];
        ath_matrix4_multiply(&clip_matrix,&gpu_view,model);
        stats->cpu_clip_objects++;
        result=athena_render3d_clip_mesh_textured(mesh,&clip_matrix,&shade,submit_clipped,&submission,stats);
        if(result<0) athena_render3d_set_error_detail("clipping failed: non-finite clip coordinates or UVs beyond Model3D.UV_LIMIT");
    }
    if(!pass.grouped) pass_close();
    stats->draw_passes++; return result;
}
static int draw_mesh(const AthenaMesh3D *source,const AthenaMatrix4 *model,
    AthenaCamera3D *camera,const AthenaLights *lights,AthenaRender3DCull cull,
    AthenaRender3DStats *stats,int relation) {
    if(!source) return -1;
    AthenaMesh3DView view; athena_mesh3d_view(source,&view);
    /* The mesh's own immutable streams: referenced in place by DMA. */
    return draw_view(&view,model,camera,lights,cull,stats,relation,NULL,(AthenaMesh3D *)source);
}
int athena_render3d_draw_mesh(const AthenaMesh3D *mesh,const AthenaMatrix4 *model,
    AthenaCamera3D *camera,const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *stats) {
    return draw_mesh(mesh,model,camera,lights,cull,stats,-1);
}
static void submit_skinned_chunk(const AthenaMesh3DView *v,uint32_t first,uint32_t count,Submission *submission) {
    uint32_t padded=(count+3)&~3u;
    /* Flags 2, GIF tag 2, five stream headers and the kick 2, plus the data. */
    uint32_t vertex_bytes=36+(submission->textured?8:0);
    size_t qwc=11+(submission->textured?1:0)+padded*vertex_bytes/16;
    owl_packet *p=owl_query_packet(CHANNEL_VIF1,qwc);
    prim_reg_t prim={.PRIM=GS_PRIM_PRIM_TRIANGLE,.IIP=1,.TME=submission->textured,.ABE=blend_mode>=0,.FGE=submission->fog?1:0,.CTXT=submission->context};
    giftag_t tag={.EOP=1,.PRE=1,.PRIM=prim.data,.NREG=3};
    owl_add_unpack_data_cnt(p,26,1,0);
    owl_add_float(p,submission->textured?128:255); owl_add_uint(p,0); owl_add_uint(p,submission->textured); owl_add_float(p,submission->cull);
    owl_add_unpack_data_cnt(p,0,1,1); owl_add_ulong(p,tag.data); owl_add_ulong(p,register_list(submission->fog));
    inline_packed(p,2,&v->positions[first],count,UNPACK_V3_32,12,1);
    inline_packed(p,34,&v->normals[first],count,UNPACK_V3_32,12,1);
    inline_packed(p,66,&v->colors[first],count,UNPACK_V4_8,4,1);
    inline_packed(p,98,&v->joints[first*4],count,UNPACK_V4_8,4,1);
    inline_packed(p,130,&v->weights8[first*4],count,UNPACK_V4_8,4,1);
    if(submission->textured) inline_packed(p,162,&v->texcoords[first],count,UNPACK_V2_32,8,1);
    owl_add_cnt_tag(p,1,0);
    owl_add_uint(p,VIF_CODE(0,0,VIF_FLUSH,0));
    owl_add_uint(p,VIF_CODE(0,0,VIF_NOP,0));
    owl_add_uint(p,VIF_CODE(count,0,VIF_ITOP,0));
    owl_add_uint(p,VIF_CODE(submission->address,0,VIF_MSCALF,0));
    submission->stats->vu_batches++;
    submission->stats->geometry_bytes+=padded*vertex_bytes;
}
int athena_render3d_draw_skinned_contained(const AthenaMesh3DView *v,const AthenaMatrix4 *palette,
    uint32_t joint_count,AthenaCamera3D *camera,const AthenaLights *lights,AthenaRender3DCull cull,
    AthenaRender3DStats *stats) {
    dma_drain(0);
    if(!v||!palette||!camera||!stats||(cull!=0&&cull!=1&&cull!=-1)||!v->joints||!v->weights8||!v->normals||
        (v->material.texture&&!v->texcoords)||!joint_count||joint_count>ATHENA_RENDER3D_SKIN_JOINTS||v->joint_count>joint_count) {
        athena_render3d_set_error_detail("VU1 skinning needs joints, weights, normals, texture UVs and at most 24 joints"); return -1;
    }
    AthenaMesh3DView silhouette;
    if(capture.texture) {
        int code=silhouette_view(v,&silhouette); if(code<0) return code;
        v=&silhouette; lights=NULL;
    }
    if(!athena_camera3d_update(camera)) { athena_render3d_set_error_detail("camera matrices are not finite"); return -1; }
    for(uint32_t j=0;j<joint_count;j++) for(int k=0;k<16;k++) if(!athena_float_isfinite(palette[j].value[k])) {
        athena_render3d_set_error_detail("joint palette is not finite"); return -1;
    }
    stats->submitted_objects++;
    AthenaMatrix4 identity; ath_matrix4_identity(&identity);
    AthenaShade3D shade;
    if(!athena_render3d_shade_prepare(&shade,v,&identity,lights)) { athena_render3d_set_error_detail("skinned mesh lighting cannot be prepared"); return -1; }
    if(!shade.enabled) {
        /* Unlit: white ambient and no lights leave the vertex color as is. */
        ath_matrix4_identity(&shade.normal_matrix); memset(&shade.lights,0,sizeof(shade.lights));
        shade.lights.ambient[0]=shade.lights.ambient[1]=shade.lights.ambient[2]=1;
        shade.lights.stamp=UINT64_MAX; /* not any Lights state, nor the NULL (black) one */
    }
    graphics_service_init();
    GSCONTEXT *gs=getGSGLOBAL();
    if(!gs||!gs->ZBuffering) return -3;
    if(owl_get_controller()->size<=12+32*(v->material.texture?44:36)/16+1) return -2;
    int indexed=v->indices&&v->chunks;
    vu_mpg **selected=indexed?&indexed_skin_program:&skinned_program;
    int alpha=v->material.alpha_mask?1+(int)(v->material.alpha_cutoff*128+.5f):0;
    AthenaTexture3D *texture=v->material.texture;
    int same=pass.open&&pass.program==selected&&pass.texture_stamp==athena_texture3d_stamp(texture)&&pass.camera_stamp==camera->stamp&&pass.context==gs->PrimContext&&
        pass.alpha==alpha&&pass.blend==blend_mode;
    if(pass.open&&!same) pass_close();
    if(!same) {
        if(!*selected) {
            *selected=vu_mpg_load_buffer(indexed?embed_vu_code_ptr(VU1Draw3DIndexedSkinned):embed_vu_code_ptr(VU1Draw3DSkinned),
                indexed?embed_vu_code_size(VU1Draw3DIndexedSkinned):embed_vu_code_size(VU1Draw3DSkinned),VECTOR_UNIT_1,false);
            if(!*selected) return -2;
        }
        AthenaTexture3DBinding binding={0};
        if(texture) {
            int code=athena_texture3d_bind(texture,&binding);
            if(code<0) { athena_render3d_set_error_detail("texture cannot be made resident in VRAM"); return code; }
        }
        int code=pass_open(selected,texture,&binding,camera,&identity,gs,alpha,stats);
        if(code<0) return code;
    } else {
        owl_packet *p=owl_query_packet(CHANNEL_VIF1,6);
        owl_add_cnt_tag(p,0,owl_vif_code_double(VIF_CODE(0,0,VIF_FLUSHE,0),VIF_CODE(0,0,VIF_NOP,0)));
        inline_matrix(p,5,&identity);
    }
    /* Normal matrix, lights and the palette (four columns per joint). */
    upload_shade(&shade);
    upload_tint(texture!=NULL);
    owl_packet *p=owl_query_packet(CHANNEL_VIF1,1+joint_count*4);
    owl_add_unpack_data_cnt(p,R3D_SKIN_PALETTE,joint_count*4,0);
    for(uint32_t j=0;j<joint_count;j++) for(int c=0;c<4;c++) owl_add_uquad_ptr(p,(void *)&palette[j].value[c*4]);
    int fog=fog_prepare(athena_lights_peek(lights),gs);
    Submission submission={pass.address,gs->PrimContext,texture!=NULL,cull,stats,1,fog,NULL};
    if(indexed) {
        prepare_indexed(&submission,1);
        for(uint32_t batch=0;batch<v->chunk_count;batch++) submit_indexed(v,batch,1,1,NULL,&submission);
    }
    else for(uint32_t first=0;first<v->vertex_count;first+=R3D_SKIN_BATCH) {
        uint32_t count=v->vertex_count-first; if(count>R3D_SKIN_BATCH) count=R3D_SKIN_BATCH;
        submit_skinned_chunk(v,first,count,&submission);
    }
    stats->source_triangles+=v->vertex_count/3; stats->triangles+=v->vertex_count/3;
    if(!pass.grouped) pass_close();
    stats->draw_passes++; return 0;
}
int athena_render3d_draw_view(const AthenaMesh3DView *view,const AthenaMatrix4 *model,
    AthenaCamera3D *camera,const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *stats) {
    return draw_view(view,model,camera,lights,cull,stats,-1,NULL,NULL);
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
    return draw_view(view,model,camera,lights,cull,stats,-1,&morph,NULL);
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
    athena_render3d_capture_end(0);
    athena_render3d_group_end();
    free(silhouette_colors); silhouette_colors=NULL; silhouette_capacity=0;
    blend_mode=-1;
    athena_render3d_frame_stats(NULL,1);
    owl_wait_generation(owl_flush_generation());
    dma_drain(1); free(retained); retained=NULL; retained_capacity=0;
    if(program) { vu_mpg_unload(program); program=NULL; }
    if(diffuse_program) { vu_mpg_unload(diffuse_program); diffuse_program=NULL; }
    if(texture_program) { vu_mpg_unload(texture_program); texture_program=NULL; }
    if(skinned_program) { vu_mpg_unload(skinned_program); skinned_program=NULL; }
    if(near_program) { vu_mpg_unload(near_program); near_program=NULL; }
    if(morph_program) { vu_mpg_unload(morph_program); morph_program=NULL; }
    if(indexed_program) {vu_mpg_unload(indexed_program);indexed_program=NULL;}
    if(indexed_skin_program) {vu_mpg_unload(indexed_skin_program);indexed_skin_program=NULL;}
    if(indexed_morph_program) {vu_mpg_unload(indexed_morph_program);indexed_morph_program=NULL;}
}

void athena_render3d_retirement_stats(AthenaMeshRetirementStats *out) {
    if(!out) return;
    memset(out,0,sizeof(*out));
    out->references=retained_count;
    out->capacity_bytes=retained_capacity*sizeof(*retained);
    for(unsigned i=0;i<retained_count;i++)
        if(!owl_generation_read(retained[retained_head+i].generation)) out->pending_references++;
}
