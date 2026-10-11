#ifndef ATHENA_COLLISION3D_BINDING_H
#define ATHENA_COLLISION3D_BINDING_H
#include <ath_env.h>
JSModuleDef *athena_collision3d_js_init(JSContext *ctx);
void athena_collision3d_js_cleanup(JSContext *ctx);
#endif
