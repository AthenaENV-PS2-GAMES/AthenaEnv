/* Host stand-in: exercise the real batch, geometry, camera and frustum logic.
 * This cannot verify rasterization, VIF execution or GS state on the PS2. */
#include <athena/render3d.h>
#include <stddef.h>
#include <math.h>
#include <assert.h>
#include "render3d_clip.h"
/* Draws that skipped the frustum test; read by the Scene3D test. */
unsigned host_contained_draws;
/* Same pass key as render3d_gs.c: program, texture and camera. */
static struct { int open,grouped,program; const void *texture; const AthenaCamera3D *camera; } pass;
int athena_render3d_group_begin(void) {
    if(pass.grouped) return -1;
    pass.grouped=1; return 0;
}
void athena_render3d_group_end(void) { pass.open=0; pass.grouped=0; }
static int count_chunk(const AthenaVector4 *p,const AthenaColor3D *colors,const AthenaTexcoord3D *uv,uint32_t count,void *opaque) {
    (void)p; (void)colors;
    AthenaRender3DStats *s=opaque;
    s->vu_batches++; s->geometry_bytes+=((count+3)&~3u)*(20+(uv?8:0));
    return 0;
}
int athena_render3d_draw_lit(AthenaInstance3D *i,AthenaCamera3D *c,
    const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *s) {
    if(!i) return -1;
    const AthenaMatrix4 *m=athena_instance3d_transform(i);
    if(!m) return -1;
    return athena_render3d_draw_mesh(athena_instance3d_mesh(i),m,c,lights,cull,s);
}
/* Last view drawn, for the Scene3D skinning test. */
AthenaMesh3DView host_last_view;
static AthenaPosition3D recorded_positions[4096],recorded_normals[4096];
static AthenaColor3D recorded_colors[4096];
/* Tests observe original triangle order, independently of compact storage. */
static void record_view(const AthenaMesh3DView *v) {
    assert(v->vertex_count<=4096);host_last_view=*v;
    for(uint32_t i=0;i<v->vertex_count;i++) {
        uint32_t j=athena_mesh3d_corner(v,i);recorded_positions[i]=v->positions[j];recorded_colors[i]=v->colors[j];
        if(v->normals)recorded_normals[i]=v->normals[j];
    }
    host_last_view.positions=recorded_positions;host_last_view.colors=recorded_colors;
    if(v->normals)host_last_view.normals=recorded_normals;
    host_last_view.indices=NULL;host_last_view.chunks=NULL;host_last_view.chunk_indices=NULL;
    host_last_view.chunk_count=0;host_last_view.stream_vertex_count=v->vertex_count;host_last_view.deform_reuse=NULL;
}
/* Tests may raise it to exercise guard band paths. */
float host_guard_band=1;
float athena_render3d_guard_band(void) { return host_guard_band; }
static int draw_view(const AthenaMesh3DView *view,const AthenaMatrix4 *m,AthenaCamera3D *c,
    const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *s,int relation) {
    if(!view||!m||!c||!s||(cull!=0&&cull!=1&&cull!=-1)) return -1;
    if(!athena_camera3d_update(c)) return -1;
    AthenaMesh3DView v=*view; record_view(&v);
    s->submitted_objects++;
    if(relation<0) relation=athena_camera3d_box_relation(c,m,v.minimum,v.maximum);
    else host_contained_draws++;
    if(relation<0) return -1;
    if(!relation) { s->culled_objects++; return 0; }
    AthenaShade3D shade;
    if(!athena_render3d_shade_prepare(&shade,&v,m,lights)) return -1;
    int program=v.material.texture?2:shade.enabled&&relation==ATHENA_FRUSTUM3D_INSIDE?1:0;
    if(!pass.open||pass.program!=program||pass.texture!=v.material.texture||pass.camera!=c) {
        pass.open=1; pass.program=program; pass.texture=v.material.texture; pass.camera=c;
        s->pipeline_passes++;
    }
    if(!pass.grouped) pass.open=0;
    s->draw_passes++;
    if(relation==ATHENA_FRUSTUM3D_INSIDE) {
        s->source_triangles+=v.vertex_count/3; s->triangles+=v.vertex_count/3;
        s->vu_batches+=(v.vertex_count+47)/48;
        s->geometry_bytes+=((v.vertex_count+3)&~3u)*((shade.enabled?28:16)+(v.material.texture?8:0)); return 0;
    }
    AthenaMatrix4 clip_matrix;
    ath_matrix4_multiply(&clip_matrix,&c->view_projection,m);
    if(!v.material.texture) v.texcoords=NULL;
    s->cpu_clip_objects++;
    return athena_render3d_clip_mesh_textured(&v,&clip_matrix,&shade,count_chunk,s,s);
}
static int draw_mesh(const AthenaMesh3D *mesh,const AthenaMatrix4 *m,AthenaCamera3D *c,
    const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *s,int relation) {
    if(!mesh) return -1;
    AthenaMesh3DView v; athena_mesh3d_view(mesh,&v);
    return draw_view(&v,m,c,lights,cull,s,relation);
}
int athena_render3d_draw_mesh(const AthenaMesh3D *mesh,const AthenaMatrix4 *m,AthenaCamera3D *c,
    const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *s) {
    return draw_mesh(mesh,m,c,lights,cull,s,-1);
}
/* VU1 skinning stand-in: the same blend with the 8-bit weights the VU reads,
 * recorded as host_last_view; counts host_vu_skins. */
unsigned host_vu_skins;
static AthenaPosition3D vu_positions[4096],vu_normals[4096];
int athena_render3d_draw_skinned_contained(const AthenaMesh3DView *v,const AthenaMatrix4 *palette,
    uint32_t joint_count,AthenaCamera3D *c,const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *s) {
    (void)lights;
    if(!v||!palette||!c||!s||(cull!=0&&cull!=1&&cull!=-1)||!v->joints||!v->weights8||!v->normals||
        (v->material.texture&&!v->texcoords)||!joint_count||joint_count>ATHENA_RENDER3D_SKIN_JOINTS||v->joint_count>joint_count||
        v->vertex_count>4096) return -1;
    for(uint32_t i=0;i<athena_mesh3d_stream_count(v);i++) {
        float m[16]={0};
        for(int k=0;k<4;k++) {
            float w=v->weights8[i*4+k]/255.0f; const float *pm=palette[v->joints[i*4+k]].value;
            for(int e=0;e<16;e++) m[e]+=pm[e]*w;
        }
        const AthenaPosition3D *p=&v->positions[i],*n=&v->normals[i];
        vu_positions[i]=(AthenaPosition3D){m[0]*p->x+m[4]*p->y+m[8]*p->z+m[12],m[1]*p->x+m[5]*p->y+m[9]*p->z+m[13],
            m[2]*p->x+m[6]*p->y+m[10]*p->z+m[14]};
        float x=m[0]*n->x+m[4]*n->y+m[8]*n->z,y=m[1]*n->x+m[5]*n->y+m[9]*n->z,z=m[2]*n->x+m[6]*n->y+m[10]*n->z;
        float l=sqrtf(x*x+y*y+z*z); vu_normals[i]=(AthenaPosition3D){x/l,y/l,z/l};
    }
    AthenaMesh3DView deformed=*v;deformed.positions=vu_positions;deformed.normals=vu_normals;record_view(&deformed);
    host_vu_skins++;
    s->submitted_objects++; s->draw_passes++; s->pipeline_passes++;
    s->source_triangles+=v->vertex_count/3; s->triangles+=v->vertex_count/3;
    s->vu_batches+=(v->vertex_count+29)/30; s->geometry_bytes+=((v->vertex_count+3)&~3u)*(v->material.texture?44:36);
    return 0;
}
int athena_render3d_draw_view(const AthenaMesh3DView *v,const AthenaMatrix4 *m,AthenaCamera3D *c,
    const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *s) {
    return draw_view(v,m,c,lights,cull,s,-1);
}
/* VU1 morph stand-in: the program's blend (at most 4 nonzero weights, no
 * normal deltas, flat normals from the morphed faces), recorded as
 * host_last_view; counts host_vu_morphs. Tests may set host_morph_refuse to
 * exercise the EE fallback. */
unsigned host_vu_morphs; int host_morph_refuse;
int athena_render3d_draw_morph(const AthenaMesh3DView *v,const AthenaMatrix4 *m,const float *weights,
    AthenaCamera3D *c,const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *s) {
    (void)m; (void)c; (void)lights; (void)cull;
    if(!v||!weights||!v->target_count||!s||v->vertex_count>4096) return -1;
    if(host_morph_refuse||v->target_normals) return 1;
    uint32_t active=0;
    for(uint32_t t=0;t<v->target_count;t++) if(weights[t]!=0) active++;
    if(active>ATHENA_RENDER3D_MORPH_TARGETS) return 1;
    for(uint32_t i=0;i<athena_mesh3d_stream_count(v);i++) {
        AthenaPosition3D p=v->positions[i];
        for(uint32_t t=0;t<v->target_count;t++) {
            const AthenaPosition3D *d=&v->target_positions[t*athena_mesh3d_stream_count(v)+i];
            p.x+=weights[t]*d->x; p.y+=weights[t]*d->y; p.z+=weights[t]*d->z;
        }
        vu_positions[i]=p;
        if(v->normals) vu_normals[i]=v->normals[i];
    }
    if(v->normals&&v->flat_normals) for(uint32_t i=0;i+2<v->vertex_count;i+=3) {
        AthenaPosition3D a=vu_positions[i],b=vu_positions[i+1],e=vu_positions[i+2];
        float ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z,wx=e.x-a.x,wy=e.y-a.y,wz=e.z-a.z;
        float nx=uy*wz-uz*wy,ny=uz*wx-ux*wz,nz=ux*wy-uy*wx,l=sqrtf(nx*nx+ny*ny+nz*nz);
        if(l>1e-12f) vu_normals[i]=vu_normals[i+1]=vu_normals[i+2]=(AthenaPosition3D){nx/l,ny/l,nz/l};
    }
    AthenaMesh3DView deformed=*v;deformed.positions=vu_positions;if(v->normals)deformed.normals=vu_normals;record_view(&deformed);
    host_vu_morphs++;
    s->submitted_objects++; s->draw_passes++; s->pipeline_passes++; s->vu_morph_objects++;
    s->source_triangles+=v->vertex_count/3; s->triangles+=v->vertex_count/3;
    s->vu_batches+=(v->vertex_count+32)/33;
    return 0;
}
int athena_render3d_draw_mesh_contained(const AthenaMesh3D *mesh,const AthenaMatrix4 *m,AthenaCamera3D *c,
    const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *s) {
    return draw_mesh(mesh,m,c,lights,cull,s,ATHENA_FRUSTUM3D_INSIDE);
}
int athena_render3d_draw(AthenaInstance3D *i,AthenaCamera3D *c,
    AthenaRender3DCull cull,AthenaRender3DStats *s) {
    return athena_render3d_draw_lit(i,c,NULL,cull,s);
}
