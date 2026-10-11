#ifndef ATHENA_JS_COLLISION3D_H
#define ATHENA_JS_COLLISION3D_H
#include <ath_env.h>
#include <athena/collision3d.h>
/* The native world of a live Collision3D.World, or NULL with a TypeError. */
AthenaCollision3DWorld *athena_collision3d_world_from_value(JSContext *ctx,JSValueConst value);
/* For optional consumers that cannot include this module (Audio3D occlusion): 1 when the straight
 * segment from..to meets geometry of `mask` before reaching `to` (blocked = 1, else 0), 0 on success
 * with no hit; -1 (exception cleared) when the value is not a live world. */
int athena_collision3d_js_segment_blocked(JSContext *ctx,JSValueConst world,const float from[3],const float to[3],uint32_t mask,float *blocked);
#endif
