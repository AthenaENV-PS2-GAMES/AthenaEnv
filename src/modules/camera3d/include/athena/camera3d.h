#ifndef ATHENA_CAMERA3D_H
#define ATHENA_CAMERA3D_H
#include <stdbool.h>
#include <athena/matrix4.h>

/* Right-handed world, camera faces -Z; column-major matrices / column vectors.
 * Projection uses reversed depth: near -> +1, far -> -1, GS depth GEQUAL.
 * C objects are value types: never expose their fields as owning JS wrappers. */
typedef struct {
    AthenaVector4 position, target, up;
    float fov_y_degrees, aspect, near_clip, far_clip;
    AthenaMatrix4 view, projection, view_projection;
    bool dirty;
} AthenaCamera3D;
void athena_camera3d_init(AthenaCamera3D *camera);
int athena_camera3d_set_projection(AthenaCamera3D *camera, float fov_y_degrees,
    float aspect, float near_clip, float far_clip);
int athena_camera3d_set_position(AthenaCamera3D *camera, float x, float y, float z);
int athena_camera3d_look_at(AthenaCamera3D *camera, float x, float y, float z);
int athena_camera3d_set_up(AthenaCamera3D *camera, float x, float y, float z);
int athena_camera3d_update(AthenaCamera3D *camera);
typedef enum { ATHENA_FRUSTUM3D_OUTSIDE=0, ATHENA_FRUSTUM3D_INTERSECT=1,
    ATHENA_FRUSTUM3D_INSIDE=2 } AthenaFrustum3DRelation;
/* Returns -1 for invalid computation. INSIDE includes a rounding margin so
 * only strictly contained boxes bypass precise triangle clipping. */
int athena_camera3d_box_relation(AthenaCamera3D *camera,const AthenaMatrix4 *model,
    const float minimum[3],const float maximum[3]);
/* Conservative homogeneous AABB test; local box, optional model matrix.
 * Returns -1 for nonfinite computation, 0 outside, 1 potentially visible. */
int athena_camera3d_box_visible(AthenaCamera3D *camera, const AthenaMatrix4 *model,
    const float minimum[3], const float maximum[3]);
#endif
