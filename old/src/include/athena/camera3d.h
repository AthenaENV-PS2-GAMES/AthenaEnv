#ifndef ATHENA_CAMERA3D_H
#define ATHENA_CAMERA3D_H

#include <render.h>

typedef athena_camera_state AthenaCamera3dState;

void athena_camera3d_position(float x, float y, float z);
void athena_camera3d_target(float x, float y, float z);
void athena_camera3d_orbit(float yaw, float pitch);
void athena_camera3d_turn(float yaw, float pitch);
void athena_camera3d_dolly(float dist);
void athena_camera3d_zoom(float dist);
void athena_camera3d_pan(float x, float y);
void athena_camera3d_update(void);
void athena_camera3d_save(AthenaCamera3dState *state);
void athena_camera3d_restore(const AthenaCamera3dState *state);

VECTOR *athena_camera3d_get_position(void);

#endif /* ATHENA_CAMERA3D_H */
