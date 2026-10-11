#include <athena/float_bits.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include <athena/model3d.h>
#if defined(__mips__)
#include <kernel.h>
#endif
struct AthenaMesh3D {
    uint64_t refs;
    /* Last DMA generation whose chain references the streams (Render3D). */
    uint64_t dma_generation;
    uint32_t count,stream_count,chunk_count;
    uint16_t *indices; uint8_t *chunk_indices; AthenaMesh3DChunk *chunks;
    AthenaPosition3D *positions; AthenaColor3D *colors;
    AthenaPosition3D *normals;
    AthenaTexcoord3D *texcoords;
    uint8_t *joints; float *weights; uint8_t *weights8; uint32_t joint_count;
    AthenaPosition3D *target_positions,*target_normals; uint32_t target_count;
    float target_minimum[ATHENA_MODEL3D_MAX_TARGETS][3],target_maximum[ATHENA_MODEL3D_MAX_TARGETS][3];
    int flat_normals; uint16_t *deform_reuse;
    AthenaMaterial3D material;
    float minimum[3],maximum[3];
};
struct AthenaInstance3D {
    uint64_t refs;
    AthenaMesh3D *mesh;
    AthenaVector4 position,scale;
    AthenaQuaternion rotation;
    AthenaMatrix4 transform;
    int dirty;
};
const char *athena_model3d_error(int result) {
    switch(result) {
        case ATHENA_MODEL3D_EINVAL: return "Invalid geometry or transform";
        case ATHENA_MODEL3D_ENOMEM: return "Out of memory";
        case ATHENA_MODEL3D_EIO: return "Cannot read model";
        case ATHENA_MODEL3D_EVRAM: return "Texture cannot be made resident in VRAM";
        case ATHENA_MODEL3D_EFORMAT: return "Malformed model";
        case ATHENA_MODEL3D_EUNSUPPORTED: return "Model feature is not supported by the static pipeline";
        default: return "OK";
    }
}
/* R5900 GCC assumes hardware floats cannot be NaN/Inf and folds isfinite()
 * to true. TypedArray input still carries arbitrary IEEE bits: inspect those. */

void athena_material3d_default(AthenaMaterial3D *m) {
    *m=(AthenaMaterial3D){.shading=ATHENA_MATERIAL3D_UNLIT,.base_color={1,1,1,1},.alpha_cutoff=.5f};
}
int athena_material3d_validate(const AthenaMaterial3D *m) {
    if(!m||(m->shading!=ATHENA_MATERIAL3D_UNLIT&&m->shading!=ATHENA_MATERIAL3D_DIFFUSE)) return 0;
    for(unsigned j=0;j<4;j++) if(!athena_float_isfinite(m->base_color[j])||m->base_color[j]<0||m->base_color[j]>1) return 0;
    if((m->alpha_mask!=0&&m->alpha_mask!=1)||!athena_float_isfinite(m->alpha_cutoff)||m->alpha_cutoff<0||m->alpha_cutoff>1) return 0;
    return 1;
}
/* Float: the R5900 emulates double in software. Dividing by the largest
 * component first keeps the squares in [1,3]: no overflow or underflow. */
static int normalize(AthenaPosition3D *out,float x,float y,float z) {
    float m=fabsf(x); if(fabsf(y)>m) m=fabsf(y); if(fabsf(z)>m) m=fabsf(z);
    if(!(m>0)||!athena_float_isfinite(m)) return 0;
    x/=m; y/=m; z/=m;
    float length=sqrtf(x*x+y*y+z*z);
    *out=(AthenaPosition3D){x/length,y/length,z/length}; return 1;
}
/* The unit normal of triangle abc, or 0 when it is degenerate (or its edges
 * overflow). Validation and flat generation share it, so every triangle
 * validation accepts gets a normal. Edges are rescaled before the cross
 * product, which then neither overflows nor underflows. */
static int face_normal(const float a[3],const float b[3],const float c[3],AthenaPosition3D *out) {
    float u[3],v[3],m=0;
    for(int j=0;j<3;j++) {
        u[j]=b[j]-a[j]; v[j]=c[j]-a[j];
        if(fabsf(u[j])>m) m=fabsf(u[j]);
        if(fabsf(v[j])>m) m=fabsf(v[j]);
    }
    if(!(m>0)||!athena_float_isfinite(m)) return 0;
    for(int j=0;j<3;j++) { u[j]/=m; v[j]/=m; }
    return normalize(out,u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]);
}
/* Validation as a resumable pass list. The order of the passes, and so which
 * issue is reported first, is that of the original single function. Each
 * pass visits `limit` items; the head passes run one check each. */
enum { V_HEAD_A=0, V_SKIN, V_HEAD_B, V_TARGETS, V_HEAD_C, V_POSITIONS, V_COLORS, V_INDICES, V_TEXCOORDS,
    V_NORMALS, V_FACES, V_END };
typedef struct { int pass; uint32_t i; } AthenaValidate;
static uint32_t triangle_corners(const AthenaGeometry3D *g) { return g->indices?g->index_count:g->vertex_count; }
static uint64_t validate_limit(const AthenaGeometry3D *g,int pass) {
    switch(pass) {
    case V_HEAD_A: case V_HEAD_B: case V_HEAD_C: return 1;
    case V_SKIN: return g->joints?g->skin_count:0;
    case V_TARGETS: return (uint64_t)g->target_count*g->vertex_count*3;
    case V_POSITIONS: return (uint64_t)g->vertex_count*3;
    case V_COLORS: return g->colors?(uint64_t)g->color_count*4:0;
    case V_INDICES: return g->indices?triangle_corners(g):0;
    case V_TEXCOORDS: return g->texcoords?(uint64_t)g->texcoord_count*2:0;
    case V_NORMALS: return g->normals?g->normal_count:0;
    case V_FACES: return !g->normals&&g->material&&g->material->shading==ATHENA_MATERIAL3D_DIFFUSE?triangle_corners(g)/3:0;
    default: return 0;
    }
}
/* Items [begin,end) of one pass: tight loops, so a bounded step costs what the
 * original single function did. Returns the first issue, else VALID. */
static AthenaGeometry3DIssue validate_range(const AthenaGeometry3D *g,int pass,uint32_t begin,uint32_t end,uint32_t *offset) {
    uint32_t count=g?triangle_corners(g):0;
    switch(pass) {
    case V_HEAD_A:
        if(!g||!g->positions||g->vertex_count==0||g->vertex_count>ATHENA_MODEL3D_MAX_VERTICES)
            return ATHENA_GEOMETRY3D_VERTEX_COUNT;
        if((g->colors && g->color_count!=g->vertex_count)||(!g->colors && g->color_count))
            return ATHENA_GEOMETRY3D_COLOR_COUNT;
        if((g->normals&&g->normal_count!=g->vertex_count)||(!g->normals&&g->normal_count))
            return ATHENA_GEOMETRY3D_NORMAL_COUNT;
        if((g->texcoords&&g->texcoord_count!=g->vertex_count)||(!g->texcoords&&g->texcoord_count)||
            (g->material&&g->material->texture&&!g->texcoords)) return ATHENA_GEOMETRY3D_TEXCOORD_COUNT;
        if(g->material&&!athena_material3d_validate(g->material)) return ATHENA_GEOMETRY3D_MATERIAL;
        if((g->indices && !g->index_count)||(!g->indices && g->index_count))
            return ATHENA_GEOMETRY3D_INDEX_COUNT;
        if((!g->joints)!=(!g->weights)||(g->joints&&g->skin_count!=g->vertex_count)||(!g->joints&&g->skin_count))
            return ATHENA_GEOMETRY3D_SKIN_COUNT;
        return ATHENA_GEOMETRY3D_VALID;
    case V_SKIN:
        for(uint32_t i=begin;i<end;i++) {
            float maximum=0;
            for(int k=0;k<4;k++) {
                float w=g->weights[i*4+k];
                if(g->joints[i*4+k]>=ATHENA_MODEL3D_MAX_JOINTS||!athena_float_isfinite(w)||w<0) {
                    if(offset) *offset=i*4+k;
                    return ATHENA_GEOMETRY3D_SKIN;
                }
                if(w>maximum) maximum=w;
            }
            if(!(maximum>0)) { if(offset) *offset=i*4; return ATHENA_GEOMETRY3D_SKIN; }
        }
        return ATHENA_GEOMETRY3D_VALID;
    case V_HEAD_B:
        if(g->target_count>ATHENA_MODEL3D_MAX_TARGETS||(!g->target_positions)!=(!g->target_count)||
            (g->target_normals&&(!g->target_count||!g->normals))) return ATHENA_GEOMETRY3D_TARGET_COUNT;
        return ATHENA_GEOMETRY3D_VALID;
    case V_TARGETS:
        for(uint32_t i=begin;i<end;i++)
            if(!athena_float_isfinite(g->target_positions[i])||(g->target_normals&&!athena_float_isfinite(g->target_normals[i]))) {
                if(offset) *offset=i;
                return ATHENA_GEOMETRY3D_TARGET;
            }
        return ATHENA_GEOMETRY3D_VALID;
    case V_HEAD_C:
        if(count==0||count>ATHENA_MODEL3D_MAX_VERTICES||count%3) return ATHENA_GEOMETRY3D_TRIANGLE_COUNT;
        return ATHENA_GEOMETRY3D_VALID;
    case V_POSITIONS:
        for(uint32_t i=begin;i<end;i++) if(!athena_float_isfinite(g->positions[i])) { if(offset) *offset=i; return ATHENA_GEOMETRY3D_POSITION; }
        return ATHENA_GEOMETRY3D_VALID;
    case V_COLORS:
        for(uint32_t i=begin;i<end;i++)
            if(!athena_float_isfinite(g->colors[i])||g->colors[i]<0||g->colors[i]>1) {
                if(offset) *offset=i;
                return ATHENA_GEOMETRY3D_COLOR;
            }
        return ATHENA_GEOMETRY3D_VALID;
    case V_INDICES:
        for(uint32_t i=begin;i<end;i++) if(g->indices[i]>=g->vertex_count) { if(offset) *offset=i; return ATHENA_GEOMETRY3D_INDEX; }
        return ATHENA_GEOMETRY3D_VALID;
    case V_TEXCOORDS:
        for(uint32_t i=begin;i<end;i++)
            if(!athena_float_isfinite(g->texcoords[i])||fabsf(g->texcoords[i])>ATHENA_MODEL3D_UV_LIMIT) {
                if(offset) *offset=i;
                return ATHENA_GEOMETRY3D_TEXCOORD;
            }
        return ATHENA_GEOMETRY3D_VALID;
    case V_NORMALS:
        for(uint32_t i=begin;i<end;i++) {
            const float *n=&g->normals[i*3];
            if(!athena_float_isfinite(n[0])||!athena_float_isfinite(n[1])||!athena_float_isfinite(n[2])||
                (n[0]==0&&n[1]==0&&n[2]==0)) {
                if(offset) *offset=i*3;
                return ATHENA_GEOMETRY3D_NORMAL;
            }
        }
        return ATHENA_GEOMETRY3D_VALID;
    case V_FACES:
        for(uint32_t t=begin;t<end;t++) {
            uint32_t c=t*3;
            const float *a=&g->positions[(g->indices?g->indices[c]:c)*3];
            const float *b=&g->positions[(g->indices?g->indices[c+1]:c+1)*3];
            const float *d=&g->positions[(g->indices?g->indices[c+2]:c+2)*3];
            AthenaPosition3D n;
            if(!face_normal(a,b,d,&n)) { if(offset) *offset=c; return ATHENA_GEOMETRY3D_NORMAL; }
        }
        return ATHENA_GEOMETRY3D_VALID;
    default: return ATHENA_GEOMETRY3D_VALID;
    }
}
/* Visits at most *budget items. Returns -1 while passes remain, otherwise
 * the final issue (ATHENA_GEOMETRY3D_VALID when everything passed). */
static int validate_run(const AthenaGeometry3D *g,AthenaValidate *v,uint32_t *budget,uint32_t *offset) {
    while(v->pass<V_END) {
        uint64_t limit=validate_limit(g,v->pass);
        while(v->i<limit) {
            if(!*budget) return -1;
            uint64_t end=limit-v->i>*budget?v->i+*budget:limit;
            AthenaGeometry3DIssue issue=validate_range(g,v->pass,v->i,(uint32_t)end,offset);
            if(issue!=ATHENA_GEOMETRY3D_VALID) { v->pass=V_END; return issue; }
            *budget-=(uint32_t)end-v->i; v->i=(uint32_t)end;
        }
        v->pass++; v->i=0;
    }
    return ATHENA_GEOMETRY3D_VALID;
}
AthenaGeometry3DIssue athena_geometry3d_validate(const AthenaGeometry3D *g,uint32_t *offset) {
    if(offset) *offset=0;
    AthenaValidate v={0}; uint32_t budget=UINT32_MAX;
    return (AthenaGeometry3DIssue)validate_run(g,&v,&budget,offset);
}
#ifndef ATHENA_MODEL3D_INDEXED
#define ATHENA_MODEL3D_INDEXED 1
#endif
/* Mesh builder: the one construction path (athena_mesh3d_create runs it to
 * the end). Stages and their item budgets (one item: one vertex/corner/
 * component/triangle visited):
 *   validate, plan (corners of the batches formed), targets (target vertices),
 *   streams (corners), normals (triangles), reuse (corners). Allocation, the
 *   plan's exact-size copies and the final cache write-back are single steps. */
enum { B_VALIDATE=0, B_PLAN, B_ALLOC, B_TARGETS, B_STREAMS, B_NORMALS, B_REUSE, B_FINISH, B_DONE };
struct AthenaMesh3DBuilder {
    const AthenaGeometry3D *g; AthenaMesh3D *m; uint16_t *sources;
    AthenaValidate v; int stage,failed;
    uint32_t corners,count,i,t;
    /* Indexed batch plan, kept until it is committed or abandoned. */
    AthenaMesh3DChunk *chunks; uint16_t *plan_indices,*plan_sources; uint8_t *local;
    uint32_t capacity,first,used,batches,max_vertices,max_corners;
    /* Source identities for CPU deformation (optional metadata). */
    uint16_t *reuse_first,*reuse; unsigned duplicates;
};
static void mesh_free(AthenaMesh3D *m) {
    if(!m) return;
    free(m->positions); free(m->colors); free(m->normals); free(m->texcoords);
    free(m->joints); free(m->weights); free(m->weights8);
    free(m->target_positions); free(m->target_normals); free(m->deform_reuse);free(m->indices);free(m->chunks);free(m->chunk_indices); free(m);
}
static void plan_abandon(AthenaMesh3DBuilder *b) {
    free(b->chunks); free(b->plan_indices); free(b->plan_sources); free(b->local);
    b->chunks=NULL; b->plan_indices=NULL; b->plan_sources=NULL; b->local=NULL;
}
void athena_mesh3d_builder_destroy(AthenaMesh3DBuilder *b) {
    if(!b) return;
    plan_abandon(b); free(b->sources); free(b->reuse_first); free(b->reuse); mesh_free(b->m); free(b);
}
int athena_mesh3d_builder_begin(const AthenaGeometry3D *g,AthenaMesh3DBuilder **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    AthenaMesh3DBuilder *b=calloc(1,sizeof(*b));
    if(!b) return ATHENA_MODEL3D_ENOMEM;
    b->g=g; *out=b; return 0;
}
/* Starts the batch plan: prepare batches once, instead of gathering streams
 * on every draw. Source identities may recur between batches; deform_reuse
 * preserves CPU sharing. Optional optimization: no gain or allocation failure
 * leaves triangle soup (returns 0 with no plan). */
static void plan_begin(AthenaMesh3DBuilder *b) {
    const AthenaGeometry3D *g=b->g; AthenaMesh3D *m=b->m;
    if(!ATHENA_MODEL3D_INDEXED||!g->indices||(!g->normals&&m->material.shading==ATHENA_MATERIAL3D_DIFFUSE)) return;
    b->max_vertices=g->target_count?16:ATHENA_MESH3D_INDEXED_VERTICES;
    b->max_corners=g->target_count?24:ATHENA_MESH3D_INDEXED_CORNERS;
    b->capacity=(b->corners+23)/24+1;
    /* Morph's vertex limit can end a batch after as few as 15 corners. */
    if(g->target_count) b->capacity=(b->corners+14)/15+1;
    b->chunks=calloc(b->capacity,sizeof(*b->chunks));
    b->plan_indices=malloc(b->corners*sizeof(*b->plan_indices));
    b->plan_sources=malloc((b->corners+b->capacity*3)*sizeof(*b->plan_sources));
    b->local=memalign(16,b->capacity*ATHENA_MESH3D_INDEXED_INDEX_BYTES);
    if(!b->chunks||!b->plan_indices||!b->plan_sources||!b->local) { plan_abandon(b); return; }
    memset(b->local,0,b->capacity*ATHENA_MESH3D_INDEXED_INDEX_BYTES);
}
/* Forms batches until `budget` corners were consumed. Returns 1 when the plan
 * is committed, 0 when more remains, -1 when it was abandoned. */
static int plan_step(AthenaMesh3DBuilder *b,uint32_t *budget) {
    const AthenaGeometry3D *g=b->g; AthenaMesh3D *m=b->m;
    while(b->first<b->corners) {
        if(!*budget) return 0;
        uint16_t unique[ATHENA_MESH3D_INDEXED_VERTICES];uint32_t n=0,end=b->first;
        uint8_t *packed=b->local+b->batches*ATHENA_MESH3D_INDEXED_INDEX_BYTES;
        while(end<b->corners&&end-b->first+3<=b->max_corners) {
            uint32_t old=n;uint8_t tri[3];
            for(unsigned k=0;k<3;k++) {
                uint16_t source=g->indices[end+k];uint32_t j=0;
                while(j<n&&unique[j]!=source)j++;
                if(j==n) { if(n==b->max_vertices) { n=b->max_vertices+1;break; } unique[n++]=source; }
                tri[k]=j;
            }
            if(n>b->max_vertices) { n=old;break; }
            for(unsigned k=0;k<3;k++) { packed[end-b->first+k]=tri[k];b->plan_indices[end+k]=b->used+tri[k]; }
            end+=3;
        }
        uint32_t padded=(n+3)&~3u;
        /* uint16 stream indices must remain below the legacy vertex limit. */
        if(b->used+padded>ATHENA_MODEL3D_MAX_VERTICES) { plan_abandon(b); return -1; }
        for(uint32_t i=0;i<padded;i++)b->plan_sources[b->used+i]=unique[i<n?i:0];
        b->chunks[b->batches++]=(AthenaMesh3DChunk){b->first,b->used,n,end-b->first};
        *budget=*budget>end-b->first?*budget-(end-b->first):0;
        b->first=end;b->used+=padded;
    }
    uint32_t stride=16+(g->normals?12:0)+(g->texcoords?8:0)+(g->joints?24:0)+
        g->target_count*12*(g->target_normals?2:1);
    size_t cost=(size_t)b->used*stride+b->corners*sizeof(*b->plan_indices)+b->batches*(sizeof(*b->chunks)+ATHENA_MESH3D_INDEXED_INDEX_BYTES);
    if(cost>=(size_t)b->corners*stride) { plan_abandon(b); return -1; }
    AthenaMesh3DChunk *exact_chunks=malloc(b->batches*sizeof(*b->chunks));
    uint8_t *exact_local=memalign(16,b->batches*ATHENA_MESH3D_INDEXED_INDEX_BYTES);
    if(!exact_chunks||!exact_local) { free(exact_chunks);free(exact_local);plan_abandon(b); return -1; }
    memcpy(exact_chunks,b->chunks,b->batches*sizeof(*b->chunks));
    memcpy(exact_local,b->local,b->batches*ATHENA_MESH3D_INDEXED_INDEX_BYTES);
    free(b->chunks);free(b->local);
    m->indices=b->plan_indices;m->chunks=exact_chunks;m->chunk_indices=exact_local;m->chunk_count=b->batches;
    b->count=b->used; b->sources=b->plan_sources;
    b->plan_indices=NULL; b->plan_sources=NULL; b->chunks=NULL; b->local=NULL;
    return 1;
}
static uint32_t stream_source(const AthenaMesh3DBuilder *b,uint32_t i) {
    return b->sources?b->sources[i]:b->g->indices?b->g->indices[i]:i;
}
static int alloc_streams(AthenaMesh3DBuilder *b) {
    const AthenaGeometry3D *g=b->g; AthenaMesh3D *m=b->m; uint32_t count=b->count;
    m->positions=memalign(16,(count+4)*sizeof(*m->positions));
    m->colors=memalign(16,(count+4)*sizeof(*m->colors));
    if(g->normals||m->material.shading==ATHENA_MATERIAL3D_DIFFUSE) m->normals=memalign(16,(count+4)*sizeof(*m->normals));
    if(g->texcoords) m->texcoords=memalign(16,(count+4)*sizeof(*m->texcoords));
    if(g->joints) { m->joints=malloc((count+4)*4); m->weights=malloc(count*4*sizeof(float)); m->weights8=malloc((count+4)*4); }
    /* Four padding vertices after the last target: VIF unpacks whole groups
     * of four vertices (Render3D's VU1 morph sends the deltas as streams). */
    if(g->target_count) m->target_positions=calloc((size_t)g->target_count*count+4,sizeof(*m->target_positions));
    if(g->target_normals) m->target_normals=calloc((size_t)g->target_count*count+4,sizeof(*m->target_normals));
    if(!m->positions||!m->colors||((g->normals||m->material.shading==ATHENA_MATERIAL3D_DIFFUSE)&&!m->normals)||
        (g->texcoords&&!m->texcoords)||(g->joints&&(!m->joints||!m->weights||!m->weights8))||
        (g->target_count&&!m->target_positions)||(g->target_normals&&!m->target_normals)) return ATHENA_MODEL3D_ENOMEM;
    m->target_count=g->target_count;
    /* VIF unpacks whole quadwords of four vertices: padding reads zeros.
     * Every element below `count` is written by the streams stage. */
    if(m->joints) { memset(m->joints+count*4,0,16); memset(m->weights8+count*4,0,16); }
    memset(m->positions+count,0,4*sizeof(*m->positions));
    memset(m->colors+count,0,4*sizeof(*m->colors));
    if(m->normals) memset(m->normals+count,0,4*sizeof(*m->normals));
    if(m->texcoords) memset(m->texcoords+count,0,4*sizeof(*m->texcoords));
    return 0;
}
static void stream_vertex(AthenaMesh3DBuilder *b,uint32_t i) {
    const AthenaGeometry3D *g=b->g; AthenaMesh3D *m=b->m;
    uint32_t source=stream_source(b,i);
    const float *p=&g->positions[source*3];
    m->positions[i]=(AthenaPosition3D){p[0],p[1],p[2]};
    if(g->texcoords) m->texcoords[i]=(AthenaTexcoord3D){g->texcoords[source*2],g->texcoords[source*2+1]};
    if(g->joints) {
        const float *w=&g->weights[source*4];
        int largest=0,total=0;
        for(int k=1;k<4;k++) if(w[k]>w[largest]) largest=k;
        /* Rescale first: finite weights may sum beyond FLT_MAX. */
        float scaled[4],sum=0;
        for(int k=0;k<4;k++) { scaled[k]=w[k]/w[largest]; sum+=scaled[k]; }
        for(int k=0;k<4;k++) {
            m->joints[i*4+k]=(uint8_t)g->joints[source*4+k]; m->weights[i*4+k]=scaled[k]/sum;
            if(w[k]>0&&g->joints[source*4+k]+1u>m->joint_count) m->joint_count=g->joints[source*4+k]+1u;
            m->weights8[i*4+k]=(uint8_t)lroundf(scaled[k]/sum*255); total+=m->weights8[i*4+k];
        }
        /* The largest weight absorbs the rounding: the sum is exactly 255. */
        m->weights8[i*4+largest]=(uint8_t)(m->weights8[i*4+largest]+255-total);
    }
    const float *c=g->colors?&g->colors[source*4]:NULL;
    /* Validated: the product is in [0, 255]. Adding .5 and truncating rounds
     * like lroundf() for these non-negative values, without its library
     * call per channel (the R5900 FPU has no rounding instruction). */
    uint8_t baked[4]; for(unsigned j=0;j<4;j++) baked[j]=(uint8_t)((c?c[j]:1)*m->material.base_color[j]*255.0f+.5f);
    m->colors[i]=(AthenaColor3D){baked[0],baked[1],baked[2],baked[3]};
    if(g->normals) {
        const float *n=&g->normals[source*3];
        if(!normalize(&m->normals[i],n[0],n[1],n[2])) m->normals[i]=(AthenaPosition3D){0,0,0};
    }
    for(int j=0;j<3;j++) {
        if(i==0||p[j]<m->minimum[j]) m->minimum[j]=p[j];
        if(i==0||p[j]>m->maximum[j]) m->maximum[j]=p[j];
    }
}
static void stream_target(AthenaMesh3DBuilder *b,uint32_t t,uint32_t i) {
    const AthenaGeometry3D *g=b->g; AthenaMesh3D *m=b->m; uint32_t count=b->count;
    uint32_t source=t*g->vertex_count+stream_source(b,i);
    const float *d=&g->target_positions[source*3];
    m->target_positions[t*count+i]=(AthenaPosition3D){d[0],d[1],d[2]};
    if(g->target_normals) {
        const float *n=&g->target_normals[source*3];
        m->target_normals[t*count+i]=(AthenaPosition3D){n[0],n[1],n[2]};
    }
    for(int j=0;j<3;j++) {
        if(i==0||d[j]<m->target_minimum[t][j]) m->target_minimum[t][j]=d[j];
        if(i==0||d[j]>m->target_maximum[t][j]) m->target_maximum[t][j]=d[j];
    }
}
static int builder_run(AthenaMesh3DBuilder *b,uint32_t budget) {
    const AthenaGeometry3D *g=b->g; AthenaMesh3D *m=b->m;
    while(budget) {
        switch(b->stage) {
        case B_VALIDATE: {
            int issue=validate_run(g,&b->v,&budget,NULL);
            if(issue<0) return 0;
            if(issue!=ATHENA_GEOMETRY3D_VALID) return ATHENA_MODEL3D_EINVAL;
            b->corners=b->count=triangle_corners(g);
            m=b->m=calloc(1,sizeof(*m)); if(!m) return ATHENA_MODEL3D_ENOMEM;
            athena_material3d_default(&m->material); if(g->material) m->material=*g->material;
            plan_begin(b);
            b->stage=b->chunks?B_PLAN:B_ALLOC; budget=budget>1?budget-1:0; break;
        }
        case B_PLAN: {
            int done=plan_step(b,&budget);
            if(!done) return 0; /* 1: committed, -1: abandoned (triangle soup) */
            b->stage=B_ALLOC; budget=budget>1?budget-1:0; break;
        }
        case B_ALLOC:
            if(alloc_streams(b)<0) return ATHENA_MODEL3D_ENOMEM;
            b->stage=g->target_count?B_TARGETS:B_STREAMS; b->i=b->t=0; budget=budget>1?budget-1:0; break;
        case B_TARGETS:
            while(budget&&b->t<g->target_count) {
                stream_target(b,b->t,b->i); budget--;
                if(++b->i==b->count) { b->i=0; b->t++; }
            }
            if(b->t==g->target_count) { b->stage=B_STREAMS; b->i=0; }
            break;
        case B_STREAMS:
            while(budget&&b->i<b->count) { stream_vertex(b,b->i++); budget--; }
            if(b->i==b->count) {
                m->flat_normals=m->normals&&!g->normals;
                b->stage=m->flat_normals?B_NORMALS:B_REUSE; b->i=0;
            }
            break;
        case B_NORMALS:
            while(budget&&b->i<b->count) {
                uint32_t i=b->i;
                const AthenaPosition3D *p=&m->positions[i]; AthenaPosition3D n;
                const float a[3]={p[0].x,p[0].y,p[0].z},bb[3]={p[1].x,p[1].y,p[1].z},c[3]={p[2].x,p[2].y,p[2].z};
                face_normal(a,bb,c,&n); /* validation accepted this triangle: never degenerate */
                m->normals[i]=m->normals[i+1]=m->normals[i+2]=n;
                b->i+=3; budget--;
            }
            if(b->i>=b->count) { b->stage=B_REUSE; b->i=0; }
            break;
        case B_REUSE:
            /* Retain source identity for CPU fallback deformation. Optional
             * metadata: failed allocation leaves the original per-corner
             * computation available. Generated flat normals differ across
             * faces, so do not share them. */
            if(b->i==0) {
                if(!(g->indices&&(g->joints||g->target_count)&&!m->flat_normals)) { b->stage=B_FINISH; break; }
                b->reuse_first=malloc(g->vertex_count*sizeof(*b->reuse_first));
                b->reuse=malloc(b->count*sizeof(*b->reuse));
                if(!b->reuse_first||!b->reuse) { b->stage=B_FINISH; break; }
                memset(b->reuse_first,0xff,g->vertex_count*sizeof(*b->reuse_first));
                b->i=0; b->duplicates=0;
                if(!b->count) { b->stage=B_FINISH; break; }
            }
            while(budget&&b->i<b->count) {
                uint32_t i=b->i++,source=stream_source(b,i);
                if(b->reuse_first[source]==UINT16_MAX) b->reuse_first[source]=i;
                else b->duplicates++;
                b->reuse[i]=b->reuse_first[source]; budget--;
            }
            if(b->i==b->count) {
                if(b->duplicates) { m->deform_reuse=b->reuse; b->reuse=NULL; }
                b->stage=B_FINISH;
            }
            break;
        case B_FINISH:
            free(b->reuse_first); free(b->reuse); b->reuse_first=NULL; b->reuse=NULL;
            athena_texture3d_retain(m->material.texture);
            m->refs=1; m->count=b->corners;m->stream_count=b->count;free(b->sources); b->sources=NULL;
            /* The streams are immutable from here: write them back once, so DMA can
             * read them in place (Render3D REF tags) instead of copying per draw. */
#if defined(__mips__)
            {
                uint32_t count=b->count;
                if(m->chunk_indices) SyncDCache(m->chunk_indices,m->chunk_indices+m->chunk_count*ATHENA_MESH3D_INDEXED_INDEX_BYTES);
                SyncDCache(m->positions,(uint8_t *)(m->positions+count+4));
                SyncDCache(m->colors,(uint8_t *)(m->colors+count+4));
                if(m->normals) SyncDCache(m->normals,(uint8_t *)(m->normals+count+4));
                if(m->texcoords) SyncDCache(m->texcoords,(uint8_t *)(m->texcoords+count+4));
            }
#endif
            b->stage=B_DONE; return 1;
        default: return 1;
        }
    }
    return b->stage==B_DONE?1:0;
}
int athena_mesh3d_builder_step(AthenaMesh3DBuilder *b,uint32_t max_items) {
    if(!b) return ATHENA_MODEL3D_EINVAL;
    if(b->failed) return b->failed;
    if(b->stage==B_DONE) return 1;
    int code=builder_run(b,max_items?max_items:ATHENA_MESH3D_BUILD_ITEMS);
    if(code<0) b->failed=code;
    return code;
}
int athena_mesh3d_builder_take(AthenaMesh3DBuilder *b,AthenaMesh3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    if(!b||b->failed||b->stage!=B_DONE||!b->m) return ATHENA_MODEL3D_EINVAL;
    *out=b->m; b->m=NULL; return 0;
}
int athena_mesh3d_create(const AthenaGeometry3D *g,AthenaMesh3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    AthenaMesh3DBuilder *b=NULL;
    int code=athena_mesh3d_builder_begin(g,&b);
    if(code<0) return code;
    do code=athena_mesh3d_builder_step(b,UINT32_MAX); while(code==0);
    if(code==1) code=athena_mesh3d_builder_take(b,out);
    athena_mesh3d_builder_destroy(b);
    return code;
}
int athena_mesh3d_dma_claim(AthenaMesh3D *m,uint64_t generation) {
    if(!m||m->dma_generation==generation) return 0;
    m->dma_generation=generation; return 1;
}
void athena_mesh3d_retain(AthenaMesh3D *m) { if(m) m->refs++; }
void athena_mesh3d_release(AthenaMesh3D *m) {
    if(m && --m->refs==0) {
        athena_texture3d_release(m->material.texture);
        free(m->positions); free(m->colors); free(m->normals); free(m->texcoords);
        free(m->joints); free(m->weights); free(m->weights8);
        free(m->target_positions); free(m->target_normals); free(m->deform_reuse);free(m->indices);free(m->chunks);free(m->chunk_indices); free(m);
    }
}
void athena_mesh3d_view(const AthenaMesh3D *m,AthenaMesh3DView *out) {
    *out=(AthenaMesh3DView){.positions=m->positions,.colors=m->colors,.vertex_count=m->count,
        .normals=m->normals,.material=m->material,.texcoords=m->texcoords,
        .joints=m->joints,.weights=m->weights,.joint_count=m->joint_count,.weights8=m->weights8,
        .target_count=m->target_count,.target_positions=m->target_positions,.target_normals=m->target_normals,
        .target_minimum=m->target_minimum,.target_maximum=m->target_maximum,.flat_normals=m->flat_normals,.deform_reuse=m->deform_reuse,.indices=m->indices,.stream_vertex_count=m->stream_count,
        .chunks=m->chunks,.chunk_count=m->chunk_count,.chunk_indices=m->chunk_indices};
    memcpy(out->minimum,m->minimum,sizeof(m->minimum)); memcpy(out->maximum,m->maximum,sizeof(m->maximum));
}
uint32_t athena_mesh3d_target_count(const AthenaMesh3D *m) { return m?m->target_count:0; }
AthenaInstance3D *athena_instance3d_create(AthenaMesh3D *m) {
    if(!m) return NULL;
    AthenaInstance3D *i=memalign(16,sizeof(*i)); if(!i) return NULL;
    memset(i,0,sizeof(*i)); i->refs=1; i->mesh=m; athena_mesh3d_retain(m);
    i->position.w=1; i->scale=(AthenaVector4){1,1,1,0};
    athena_quaternion_identity(&i->rotation); i->dirty=1; return i;
}
void athena_instance3d_retain(AthenaInstance3D *i) { if(i) i->refs++; }
void athena_instance3d_release(AthenaInstance3D *i) {
    if(i&&--i->refs==0) { athena_mesh3d_release(i->mesh); free(i); }
}
static int finite3(float x,float y,float z) { return athena_float_isfinite(x)&&athena_float_isfinite(y)&&athena_float_isfinite(z); }
int athena_instance3d_set_position(AthenaInstance3D *i,float x,float y,float z) {
    if(!i||!finite3(x,y,z)) return 0;
    i->position=(AthenaVector4){x,y,z,1}; i->dirty=1; return 1;
}
int athena_instance3d_set_scale(AthenaInstance3D *i,float x,float y,float z) {
    if(!i||!finite3(x,y,z)) return 0;
    i->scale=(AthenaVector4){x,y,z,0}; i->dirty=1; return 1;
}
int athena_instance3d_set_rotation(AthenaInstance3D *i,float x,float y,float z,float w) {
    AthenaQuaternion q={x,y,z,w},n;
    if(!i||!athena_quaternion_normalize(&n,&q)) return 0;
    i->rotation=n; i->dirty=1; return 1;
}
int athena_instance3d_set_euler(AthenaInstance3D *i,float x,float y,float z) {
    if(!i||!finite3(x,y,z)) return 0;
    AthenaQuaternion q;
    if(!athena_quaternion_euler(&q,x,y,z)) return 0;
    i->rotation=q; i->dirty=1; return 1;
}
void athena_instance3d_get_position(const AthenaInstance3D *i,AthenaVector4 *out) { *out=i->position; }
void athena_instance3d_get_trs(const AthenaInstance3D *i,float position[3],AthenaQuaternion *rotation,float scale[3]) {
    position[0]=i->position.x; position[1]=i->position.y; position[2]=i->position.z;
    *rotation=i->rotation; scale[0]=i->scale.x; scale[1]=i->scale.y; scale[2]=i->scale.z;
}
const AthenaMatrix4 *athena_instance3d_transform(AthenaInstance3D *i) {
    if(i->dirty) {
        if(!athena_quaternion_trs(&i->transform,&i->position,&i->rotation,&i->scale)) return NULL;
        i->dirty=0;
    }
    return &i->transform;
}
const AthenaMesh3D *athena_instance3d_mesh(const AthenaInstance3D *i) { return i->mesh; }
