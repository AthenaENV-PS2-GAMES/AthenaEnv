#ifndef ATHENA_MODEL3D_JS_H
#define ATHENA_MODEL3D_JS_H
#include <ath_env.h>
#include <athena/model3d.h>
AthenaInstance3D *athena_instance3d_from_value(JSContext *ctx,JSValueConst value);
AthenaMesh3D *athena_mesh3d_from_value(JSContext *ctx,JSValueConst value);
/* The material argument of Model3D.load(): undefined gives the default. On
 * success the caller owns a reference to out->texture (may be NULL) and
 * releases it with athena_texture3d_release() after loading. */
int athena_model3d_js_material(JSContext *ctx,JSValueConst value,AthenaMaterial3D *out);
#endif
