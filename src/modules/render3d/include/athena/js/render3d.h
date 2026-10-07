#ifndef ATHENA_RENDER3D_JS_H
#define ATHENA_RENDER3D_JS_H
#include <ath_env.h>
#include <athena/render3d.h>
/* Writes the Stats fields to obj: defined on a new object (define=1) or
 * assigned on a reused one. Returns -1 with a pending exception. */
int athena_render3d_js_put_stats(JSContext *ctx,JSValueConst obj,int define,const AthenaRender3DStats *stats);
#endif
