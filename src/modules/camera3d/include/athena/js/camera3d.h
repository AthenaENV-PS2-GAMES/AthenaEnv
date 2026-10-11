#ifndef ATHENA_CAMERA3D_JS_H
#define ATHENA_CAMERA3D_JS_H
#include <ath_env.h>
#include <athena/camera3d.h>
AthenaCamera3D *athena_camera3d_from_value(JSContext *ctx,JSValueConst value);
/* Only for cameras of JS handles: keeps the camera alive after its handle is
 * disposed or collected, until the matching release. */
void athena_camera3d_js_retain(AthenaCamera3D *camera);
void athena_camera3d_js_release(AthenaCamera3D *camera);
#endif
