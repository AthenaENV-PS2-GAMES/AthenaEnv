#ifndef ATHENA_ANIMATION3D_JS_H
#define ATHENA_ANIMATION3D_JS_H
#include <ath_env.h>
#include <athena/animation3d.h>
/* A new Animation3D.Clip handle holding its own reference to clip. */
JSValue athena_clip3d_to_value(JSContext *ctx,AthenaClip3D *clip);
#endif
