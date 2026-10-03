#include <athena/camera3d.h>

void cameraSave(athena_camera_state *out);
void cameraRestore(const athena_camera_state *state);

void athena_camera3d_position(float x, float y, float z)
{
    setCameraPosition(x, y, z);
}

void athena_camera3d_target(float x, float y, float z)
{
    setCameraTarget(x, y, z);
}

void athena_camera3d_orbit(float yaw, float pitch)
{
    orbitCamera(yaw, pitch);
}

void athena_camera3d_turn(float yaw, float pitch)
{
    turnCamera(yaw, pitch);
}

void athena_camera3d_dolly(float dist)
{
    dollyCamera(dist);
}

void athena_camera3d_zoom(float dist)
{
    zoomCamera(dist);
}

void athena_camera3d_pan(float x, float y)
{
    panCamera(x, y);
}

void athena_camera3d_update(void)
{
    cameraUpdate();
}

void athena_camera3d_save(AthenaCamera3dState *state)
{
    cameraSave(state);
}

void athena_camera3d_restore(const AthenaCamera3dState *state)
{
    cameraRestore(state);
}

VECTOR *athena_camera3d_get_position(void)
{
    return getCameraPosition();
}
