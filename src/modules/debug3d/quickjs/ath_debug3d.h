#ifndef ATHENA_DEBUG3D_BINDING_H
#define ATHENA_DEBUG3D_BINDING_H
#include <ath_env.h>
JSModuleDef *athena_debug3d_js_init(JSContext *ctx);
void athena_debug3d_js_cleanup(JSContext *ctx);
#endif
