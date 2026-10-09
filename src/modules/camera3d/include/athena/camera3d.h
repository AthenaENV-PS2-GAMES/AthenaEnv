#ifndef ATHENA_CAMERA3D_H
#define ATHENA_CAMERA3D_H
#include <stdbool.h>
#include <stdint.h>
#include <athena/matrix4.h>

/* Right-handed world, camera faces -Z; column-major matrices / column vectors.
 * Projection uses reversed depth: near -> +1, far -> -1, GS depth GEQUAL.
 * C objects are value types: never expose their fields as owning JS wrappers.
 * Fields are read-only: setters validate and keep `view` current, and
 * update() only recomputes view_projection. */
typedef struct {
    AthenaVector4 position, target, up;
    float fov_y_degrees, aspect, near_clip, far_clip;
    AthenaMatrix4 view, projection, view_projection;
    bool dirty;
    uint64_t stamp; /* Unique revision of the current view_projection. */
} AthenaCamera3D;
void athena_camera3d_init(AthenaCamera3D *camera);
int athena_camera3d_set_projection(AthenaCamera3D *camera, float fov_y_degrees,
    float aspect, float near_clip, float far_clip);
int athena_camera3d_set_position(AthenaCamera3D *camera, float x, float y, float z);
int athena_camera3d_look_at(AthenaCamera3D *camera, float x, float y, float z);
int athena_camera3d_set_up(AthenaCamera3D *camera, float x, float y, float z);
/* Position and target together: one validation and one view build, with no
 * intermediate state that could be degenerate. 0 leaves the camera as is. */
int athena_camera3d_set_view(AthenaCamera3D *camera, const float eye[3], const float target[3]);
int athena_camera3d_update(AthenaCamera3D *camera);
typedef enum { ATHENA_FRUSTUM3D_OUTSIDE=0, ATHENA_FRUSTUM3D_INTERSECT=1,
    ATHENA_FRUSTUM3D_INSIDE=2 } AthenaFrustum3DRelation;
/* Returns -1 for invalid computation. INSIDE includes a rounding margin so
 * only strictly contained boxes bypass precise triangle clipping. */
int athena_camera3d_box_relation(AthenaCamera3D *camera,const AthenaMatrix4 *model,
    const float minimum[3],const float maximum[3]);
/* box_relation() against the frustum with x and y widened by guard (>= 1):
 * INSIDE means every corner is in front of the camera, between near and far
 * and within guard times the screen. Render3D uses it for the GS guard band:
 * such objects need no clipping, the GS scissor trims them to the screen. */
int athena_camera3d_box_relation_guard(AthenaCamera3D *camera,const AthenaMatrix4 *model,
    const float minimum[3],const float maximum[3],float guard);
/* Both at once, from the same corners: returns box_relation() and sets
 * *guard_inside to whether box_relation_guard() would be INSIDE. */
int athena_camera3d_box_relation_ex(AthenaCamera3D *camera,const AthenaMatrix4 *model,
    const float minimum[3],const float maximum[3],float guard,int *guard_inside);
/* 1 when the part of the box in front of the near plane is not empty and
 * lies inside the frustum widened in x and y by guard and before the far
 * plane: then only the near plane cuts it (Render3D clips that on VU1). The
 * test is exact for that convex part: its vertices are the box corners in
 * front of the near plane and the points where box edges cross it. */
int athena_camera3d_box_near_guard(AthenaCamera3D *camera,const AthenaMatrix4 *model,
    const float minimum[3],const float maximum[3],float guard);
/* Conservative homogeneous AABB test; local box, optional model matrix.
 * Returns -1 for nonfinite computation, 0 outside, 1 potentially visible. */
int athena_camera3d_box_visible(AthenaCamera3D *camera, const AthenaMatrix4 *model,
    const float minimum[3], const float maximum[3]);
/* Projection to a width x height viewport in pixels (origin top-left, y
 * down, as Render3D maps the screen). out = screen x, screen y and the
 * distance in front of the camera along its view axis. Returns 1 for a point
 * in front of the camera (beyond the near plane or not), 0 behind it or on
 * the camera plane (out untouched), -1 for invalid input. Updates the camera. */
int athena_camera3d_world_to_screen(AthenaCamera3D *camera,const float point[3],
    float width,float height,float out[3]);
/* The ray through the viewport point (sx, sy) in pixels: origin at the camera
 * position, direction a world unit vector. Points outside the viewport give
 * rays outside the frustum. Returns 1, or -1 for invalid input. */
int athena_camera3d_screen_to_ray(AthenaCamera3D *camera,float sx,float sy,
    float width,float height,float origin[3],float direction[3]);
#endif
