/* Host stand-in: exercise the real batch, geometry, camera and frustum logic.
 * This cannot verify rasterization, VIF execution or GS state on the PS2. */
#include <athena/render3d.h>
#include <stddef.h>
#include "render3d_clip.h"
/* Draws that skipped the frustum test; read by the Scene3D test. */
unsigned host_contained_draws;
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
static int draw_mesh(const AthenaMesh3D *mesh,const AthenaMatrix4 *m,AthenaCamera3D *c,
    const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *s,int relation) {
    if(!mesh||!m||!c||!s||(cull!=0&&cull!=1&&cull!=-1)) return -1;
    if(!athena_camera3d_update(c)) return -1;
    AthenaMesh3DView v; athena_mesh3d_view(mesh,&v);
    s->submitted_objects++;
    if(relation<0) relation=athena_camera3d_box_relation(c,m,v.minimum,v.maximum);
    else host_contained_draws++;
    if(relation<0) return -1;
    if(!relation) { s->culled_objects++; return 0; }
    AthenaShade3D shade;
    if(!athena_render3d_shade_prepare(&shade,&v,m,lights)) return -1;
    s->draw_passes++;
    if(relation==ATHENA_FRUSTUM3D_INSIDE) {
        s->source_triangles+=v.vertex_count/3; s->triangles+=v.vertex_count/3;
        s->vu_batches+=(v.vertex_count+47)/48;
        s->geometry_bytes+=((v.vertex_count+3)&~3u)*((shade.enabled?28:16)+(v.material.texture?8:0)); return 0;
    }
    AthenaMatrix4 clip_matrix;
    ath_matrix4_multiply(&clip_matrix,&c->view_projection,m);
    if(!v.material.texture) v.texcoords=NULL;
    return athena_render3d_clip_mesh_textured(&v,&clip_matrix,&shade,count_chunk,s,s);
}
int athena_render3d_draw_mesh(const AthenaMesh3D *mesh,const AthenaMatrix4 *m,AthenaCamera3D *c,
    const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *s) {
    return draw_mesh(mesh,m,c,lights,cull,s,-1);
}
int athena_render3d_draw_mesh_contained(const AthenaMesh3D *mesh,const AthenaMatrix4 *m,AthenaCamera3D *c,
    const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *s) {
    return draw_mesh(mesh,m,c,lights,cull,s,ATHENA_FRUSTUM3D_INSIDE);
}
int athena_render3d_draw(AthenaInstance3D *i,AthenaCamera3D *c,
    AthenaRender3DCull cull,AthenaRender3DStats *s) {
    return athena_render3d_draw_lit(i,c,NULL,cull,s);
}
