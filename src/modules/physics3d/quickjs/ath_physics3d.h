#ifndef ATHENA_PHYSICS3D_BINDING_H
#define ATHENA_PHYSICS3D_BINDING_H
#include <ath_env.h>
JSModuleDef *athena_physics3d_js_init(JSContext *ctx);
void athena_physics3d_js_cleanup(JSContext *ctx);
#endif
