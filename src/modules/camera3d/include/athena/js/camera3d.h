#ifndef ATHENA_CAMERA3D_JS_H
#define ATHENA_CAMERA3D_JS_H
#include <ath_env.h>
#include <athena/camera3d.h>
AthenaCamera3D *athena_camera3d_from_value(JSContext *ctx,JSValueConst value);
#endif
