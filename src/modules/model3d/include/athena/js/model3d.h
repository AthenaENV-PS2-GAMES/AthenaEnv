#ifndef ATHENA_MODEL3D_JS_H
#define ATHENA_MODEL3D_JS_H
#include <ath_env.h>
#include <athena/model3d.h>
AthenaInstance3D *athena_instance3d_from_value(JSContext *ctx,JSValueConst value);
AthenaMesh3D *athena_mesh3d_from_value(JSContext *ctx,JSValueConst value);
#endif
