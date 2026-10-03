#ifndef ATHENA_RENDER3D_CLIP_H
#define ATHENA_RENDER3D_CLIP_H
#include <athena/render3d.h>
#define ATHENA_CLIP3D_MAX_VERTICES 12u
#define ATHENA_RENDER3D_CHUNK 48u
typedef struct { double position[4],color[4]; } AthenaClipVertex3D;
/* Homogeneous Sutherland-Hodgman: six planes -w <= xyz <= w.
 * Colors use RGBA8 units. Returns polygon size, zero outside, -1 invalid.
 * Input/output are separate. At most nine distinct vertices geometrically;
 * twelve slots give a checked bound even for numerical edge cases. */
int athena_render3d_clip_triangle(const AthenaClipVertex3D input[3],
    AthenaClipVertex3D output[ATHENA_CLIP3D_MAX_VERTICES],int *clipped);
typedef int (*AthenaClipEmit3D)(const AthenaVector4 *positions,const AthenaColor3D *colors,
    uint32_t count,void *opaque);
/* Fixed scratch buffers; callbacks borrow them only for the call and must
 * copy before returning. Complete triangle lists, <=48 vertices per emit. */
int athena_render3d_clip_mesh(const AthenaMesh3DView *mesh,const AthenaMatrix4 *clip_matrix,
    AthenaClipEmit3D emit,void *opaque,AthenaRender3DStats *stats);
#endif
