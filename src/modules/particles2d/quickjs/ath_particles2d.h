#ifndef ATHENA_PARTICLES2D_BINDING_H
#define ATHENA_PARTICLES2D_BINDING_H
#include <ath_env.h>
JSModuleDef *athena_particles2d_js_init(JSContext *ctx);
void athena_particles2d_js_cleanup(JSContext *ctx);
#endif
