#ifndef ATHENA_CAMERARIG3D_H
#define ATHENA_CAMERARIG3D_H
#include <stdint.h>
#include <athena/camera3d.h>
#include <athena/scene3d.h>
/* Native camera controllers: the script configures a rig and feeds input;
 * following, orbiting and smoothing run in C every frame. A rig writes its
 * camera's position and look target (Camera3D setters, so view stays valid).
 * Node targets are read through their world transform: rigs run after the
 * scene update and skip a frame whose target is stale. */
typedef struct AthenaCameraRig3D AthenaCameraRig3D;
#define ATHENA_RIG3D_EINVAL (-1)
#define ATHENA_RIG3D_ENOMEM (-2)
/* Default Loop priority: after Scene3D systems (0) update world transforms. */
#define ATHENA_RIG3D_LOOP_PRIORITY 50
/* release_camera (may be NULL) is called with camera when the rig is freed:
 * the JS binding uses it to keep the camera handle alive. C callers pass NULL
 * and keep the camera alive themselves. */
typedef void (*AthenaRig3DCameraRelease)(AthenaCamera3D *camera);
/* Follow: eye = target world * offset (local space, default) or target
 * position + offset (world space); look = target world * look_offset. */
AthenaCameraRig3D *athena_rig3d_follow_create(AthenaCamera3D *camera,AthenaRig3DCameraRelease release_camera,
    AthenaNode3D *target);
int athena_rig3d_set_offset(AthenaCameraRig3D *rig,float x,float y,float z,int local);
int athena_rig3d_set_look_offset(AthenaCameraRig3D *rig,float x,float y,float z);
/* Orbit: eye on a sphere around the centre (target world position + offset,
 * or a fixed point without target). Yaw about +Y (0 looks down -Z), pitch
 * up positive, radians. */
AthenaCameraRig3D *athena_rig3d_orbit_create(AthenaCamera3D *camera,AthenaRig3DCameraRelease release_camera,
    AthenaNode3D *target);
int athena_rig3d_set_center(AthenaCameraRig3D *rig,float x,float y,float z);
int athena_rig3d_set_angles(AthenaCameraRig3D *rig,float yaw,float pitch);
/* Input deltas, clamped to the limits. */
int athena_rig3d_rotate(AthenaCameraRig3D *rig,float yaw,float pitch);
int athena_rig3d_set_distance(AthenaCameraRig3D *rig,float distance);
int athena_rig3d_zoom(AthenaCameraRig3D *rig,float delta);
int athena_rig3d_set_limits(AthenaCameraRig3D *rig,float pitch_min,float pitch_max,float distance_min,float distance_max);
/* Yaw speed in rad/s added every update. */
int athena_rig3d_set_auto_rotate(AthenaCameraRig3D *rig,float speed);
void athena_rig3d_get_angles(const AthenaCameraRig3D *rig,float *yaw,float *pitch,float *distance);
/* Common: target (NULL: orbit uses its fixed centre; follow does nothing),
 * smoothing sharpness in 1/s for eye and look (0 = rigid), snap. */
int athena_rig3d_set_target(AthenaCameraRig3D *rig,AthenaNode3D *target);
int athena_rig3d_set_sharpness(AthenaCameraRig3D *rig,float eye,float look);
/* Next update jumps to the goal without smoothing. */
void athena_rig3d_snap(AthenaCameraRig3D *rig);
void athena_rig3d_set_enabled(AthenaCameraRig3D *rig,int enabled);
int athena_rig3d_enabled(const AthenaCameraRig3D *rig);
void athena_rig3d_retain(AthenaCameraRig3D *rig);
void athena_rig3d_release(AthenaCameraRig3D *rig);
/* Updates one rig: 1 when it moved the camera, 0 when skipped (disabled, no
 * target, stale target, degenerate look), EINVAL for a bad dt. */
int athena_rig3d_update(AthenaCameraRig3D *rig,float dt);
/* Updates every enabled rig; returns how many moved their camera. */
int athena_rig3d_update_all(float dt);
int athena_rig3d_attach_loop(int priority,void *owner);
int athena_rig3d_detach_loop(void);
void athena_rig3d_detach_owner(void *owner);
int athena_rig3d_loop_system(void);
#endif
