#include <math.h>
#include <athena/float_bits.h>
#include "render3d_clip.h"
static float distance_to_plane(const AthenaClipVertex3D *v,unsigned plane) {
    return v->position[3]+(plane&1?-v->position[plane/2]:v->position[plane/2]);
}
static int same_position(const AthenaClipVertex3D *a,const AthenaClipVertex3D *b) {
    for(unsigned j=0;j<4;j++) if(a->position[j]!=b->position[j]) return 0;
    return 1;
}
static int append(AthenaClipVertex3D *out,unsigned *count,const AthenaClipVertex3D *v) {
    if(*count && same_position(&out[*count-1],v)) return 1;
    if(*count>=ATHENA_CLIP3D_MAX_VERTICES) return 0;
    out[(*count)++]=*v; return 1;
}
static float clamp(float value,float maximum) { return value<0?0:value>maximum?maximum:value; }
/* An interpolated value within its endpoints: float rounding may step past. */
static float between(float value,float a,float b) {
    float lo=a<b?a:b,hi=a<b?b:a; return value<lo?lo:value>hi?hi:value;
}
int athena_render3d_clip_triangle(const AthenaClipVertex3D input[3],
    AthenaClipVertex3D output[ATHENA_CLIP3D_MAX_VERTICES],int *clipped) {
    if(!input||!output||!clipped) return -1;
    *clipped=0;
    unsigned common=63,combined=0;
    for(unsigned i=0;i<3;i++) {
        for(unsigned j=0;j<4;j++)
            if(!athena_float_isfinite(input[i].position[j]) ||
                !athena_float_isfinite(input[i].color[j]) || input[i].color[j]<0 || input[i].color[j]>255)
                return -1;
        for(unsigned j=0;j<2;j++) if(!athena_float_isfinite(input[i].texcoord[j])||
            fabsf(input[i].texcoord[j])>ATHENA_MODEL3D_UV_LIMIT) return -1;
        unsigned flags=0;
        for(unsigned plane=0;plane<6;plane++) {
            /* w+x may overflow float near FLT_MAX: reject instead of clipping NaN. */
            float distance=distance_to_plane(&input[i],plane);
            if(!athena_float_isfinite(distance)) return -1;
            if(distance<0) flags|=1u<<plane;
        }
        common&=flags; combined|=flags;
    }
    if(common) return 0;
    if(!combined) {
        for(unsigned i=0;i<3;i++) if(input[i].position[3]<=0) return -1;
        memcpy(output,input,3*sizeof(*input)); return 3;
    }
    *clipped=1;
    AthenaClipVertex3D buffers[2][ATHENA_CLIP3D_MAX_VERTICES];
    memcpy(buffers[0],input,3*sizeof(*input));
    unsigned count=3,source=0;
    for(unsigned plane=0;plane<6;plane++) {
        AthenaClipVertex3D *in=buffers[source],*out=buffers[source^1];
        unsigned next=0;
        const AthenaClipVertex3D *a=&in[count-1];
        float da=distance_to_plane(a,plane);
        for(unsigned i=0;i<count;i++) {
            const AthenaClipVertex3D *b=&in[i]; float db=distance_to_plane(b,plane);
            if((da>=0)!=(db>=0)) {
                /* da and db have opposite signs, so t is in [0,1]. Weighted
                 * positions cannot overflow, unlike b-a for huge opposite
                 * values. Colors and UVs are clamped: float rounding may step
                 * past an endpoint. */
                float t=da/(da-db); AthenaClipVertex3D intersection;
                for(unsigned j=0;j<4;j++) {
                    intersection.position[j]=a->position[j]*(1-t)+b->position[j]*t;
                    intersection.color[j]=clamp(a->color[j]+(b->color[j]-a->color[j])*t,255);
                }
                for(unsigned j=0;j<2;j++) intersection.texcoord[j]=between(a->texcoord[j]+(b->texcoord[j]-a->texcoord[j])*t,
                    a->texcoord[j],b->texcoord[j]);
                /* Snap to the current plane, avoiding a tiny negative distance
                 * from interpolation rounding on a subsequent test. */
                intersection.position[plane/2]=(plane&1?1:-1)*intersection.position[3];
                if(!append(out,&next,&intersection)) return -1;
            }
            if(db>=0 && !append(out,&next,b)) return -1;
            a=b; da=db;
        }
        if(next>1 && same_position(&out[0],&out[next-1])) next--;
        if(next<3) return 0;
        count=next; source^=1;
    }
    for(unsigned i=0;i<count;i++) if(buffers[source][i].position[3]<=0) return -1;
    memcpy(output,buffers[source],count*sizeof(*output)); return (int)count;
}
static uint8_t color_byte(float value) {
    if(value<=0) return 0;
    if(value>=255) return 255;
    return (uint8_t)(value+0.5f);
}
int athena_render3d_clip_mesh_textured(const AthenaMesh3DView *mesh,const AthenaMatrix4 *matrix,
    const AthenaShade3D *shade,AthenaClipEmitTextured3D emit,void *opaque,AthenaRender3DStats *stats) {
    if(!mesh||!matrix||!emit||!stats||!mesh->positions||!mesh->colors||mesh->vertex_count%3||
        mesh->vertex_count>ATHENA_MODEL3D_MAX_VERTICES) return -1;
    if(shade&&shade->enabled&&!mesh->normals) return -1;
    for(unsigned i=0;i<16;i++) if(!athena_float_isfinite(matrix->value[i])) return -1;
    AthenaVector4 positions[ATHENA_RENDER3D_CHUNK+4]={{0}};
    AthenaColor3D colors[ATHENA_RENDER3D_CHUNK+4]={{0}};
    AthenaTexcoord3D texcoords[ATHENA_RENDER3D_CHUNK+4]={{0}};
    uint32_t used=0;
    for(uint32_t first=0;first<mesh->vertex_count;first+=3) {
        AthenaClipVertex3D triangle[3],polygon[ATHENA_CLIP3D_MAX_VERTICES];
        for(unsigned i=0;i<3;i++) {
            uint32_t corner=athena_mesh3d_corner(mesh,first+i);
            AthenaPosition3D p=mesh->positions[corner];
            AthenaVector4 in={p.x,p.y,p.z,1},out;
            ath_matrix4_apply(&out,matrix,&in);
            if(!athena_float_isfinite(out.x)||!athena_float_isfinite(out.y)||
                !athena_float_isfinite(out.z)||!athena_float_isfinite(out.w)) return -1;
            AthenaColor3D color=mesh->colors[corner];
            triangle[i]=(AthenaClipVertex3D){.position={out.x,out.y,out.z,out.w},.color={color.r,color.g,color.b,color.a}};
            if(mesh->texcoords) {
                triangle[i].texcoord[0]=mesh->texcoords[corner].u;
                triangle[i].texcoord[1]=mesh->texcoords[corner].v;
            }
            if(shade&&shade->enabled) athena_render3d_shade_color_at(shade,&mesh->positions[corner],&mesh->normals[corner],&color,triangle[i].color);
        }
        int clipped=0,count=athena_render3d_clip_triangle(triangle,polygon,&clipped);
        if(count<0) return -1;
        stats->source_triangles++;
        if(!count) { stats->rejected_triangles++; continue; }
        if(clipped) stats->clipped_triangles++;
        for(int j=1;j<count-1;j++) {
            unsigned corners[3]={0,(unsigned)j,(unsigned)j+1};
            for(unsigned k=0;k<3;k++) {
                const AthenaClipVertex3D *v=&polygon[corners[k]];
                positions[used]=(AthenaVector4){v->position[0],v->position[1],v->position[2],v->position[3]};
                colors[used]=(AthenaColor3D){color_byte(v->color[0]),color_byte(v->color[1]),
                    color_byte(v->color[2]),color_byte(v->color[3])};
                texcoords[used]=(AthenaTexcoord3D){v->texcoord[0],v->texcoord[1]};
                used++;
            }
            stats->triangles++;
            if(used==ATHENA_RENDER3D_CHUNK) {
                int result=emit(positions,colors,mesh->texcoords?texcoords:NULL,used,opaque); if(result<0) return result;
                used=0;
            }
        }
    }
    return used?emit(positions,colors,mesh->texcoords?texcoords:NULL,used,opaque):0;
}
typedef struct { AthenaClipEmit3D emit; void *opaque; } ColorEmit;
static int emit_color(const AthenaVector4 *positions,const AthenaColor3D *colors,
    const AthenaTexcoord3D *uv,uint32_t count,void *opaque) {
    (void)uv; ColorEmit *e=opaque; return e->emit(positions,colors,count,e->opaque);
}
int athena_render3d_clip_mesh_lit(const AthenaMesh3DView *mesh,const AthenaMatrix4 *matrix,
    const AthenaShade3D *shade,AthenaClipEmit3D emit,void *opaque,AthenaRender3DStats *stats) {
    if(!emit) return -1;
    ColorEmit e={emit,opaque};
    return athena_render3d_clip_mesh_textured(mesh,matrix,shade,emit_color,&e,stats);
}
int athena_render3d_clip_mesh(const AthenaMesh3DView *mesh,const AthenaMatrix4 *matrix,
    AthenaClipEmit3D emit,void *opaque,AthenaRender3DStats *stats) {
    return athena_render3d_clip_mesh_lit(mesh,matrix,NULL,emit,opaque,stats);
}
