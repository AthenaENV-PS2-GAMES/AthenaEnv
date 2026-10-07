#ifndef ATHENA_JS_COLLISION3D_H
#define ATHENA_JS_COLLISION3D_H
#include <ath_env.h>
#include <athena/collision3d.h>
/* The native world of a live Collision3D.World, or NULL with a TypeError. */
AthenaCollision3DWorld *athena_collision3d_world_from_value(JSContext *ctx,JSValueConst value);
#endif
