#ifndef ATHENA_PARTICLES3D_BINDING_H
#define ATHENA_PARTICLES3D_BINDING_H
#include <ath_env.h>
JSModuleDef *athena_particles3d_js_init(JSContext *ctx);
void athena_particles3d_js_cleanup(JSContext *ctx);
#endif
