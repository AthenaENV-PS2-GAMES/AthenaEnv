/* Host stand-in: exercise the real batch, geometry, camera and frustum logic.
 * This cannot verify rasterization, VIF execution or GS state on the PS2. */
#include <athena/render3d.h>
#include "render3d_clip.h"
static int count_chunk(const AthenaVector4 *p,const AthenaColor3D *colors,uint32_t count,void *opaque) {
    (void)p; (void)colors; (void)count;
    ((AthenaRender3DStats *)opaque)->vu_batches++; return 0;
}
int athena_render3d_draw(AthenaInstance3D *i,AthenaCamera3D *c,
    AthenaRender3DCull cull,AthenaRender3DStats *s) {
    if(!i||!c||!s||(cull!=0&&cull!=1&&cull!=-1)) return -1;
    const AthenaMatrix4 *m=athena_instance3d_transform(i);
    if(!m||!athena_camera3d_update(c)) return -1;
    AthenaMesh3DView v; athena_mesh3d_view(athena_instance3d_mesh(i),&v);
    s->submitted_objects++;
    int relation=athena_camera3d_box_relation(c,m,v.minimum,v.maximum);
    if(relation<0) return -1;
    if(!relation) { s->culled_objects++; return 0; }
    s->draw_passes++;
    if(relation==ATHENA_FRUSTUM3D_INSIDE) {
        s->source_triangles+=v.vertex_count/3; s->triangles+=v.vertex_count/3;
        s->vu_batches+=(v.vertex_count+47)/48; return 0;
    }
    AthenaMatrix4 clip_matrix;
    ath_matrix4_multiply(&clip_matrix,&c->view_projection,m);
    return athena_render3d_clip_mesh(&v,&clip_matrix,count_chunk,s,s);
}
