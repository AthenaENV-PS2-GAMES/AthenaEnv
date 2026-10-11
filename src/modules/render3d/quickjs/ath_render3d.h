#ifndef ATHENA_RENDER3D_BINDING_H
#define ATHENA_RENDER3D_BINDING_H
#include <ath_env.h>
JSModuleDef *athena_render3d_js_init(JSContext *ctx);
void athena_render3d_js_cleanup(JSContext *ctx);
#endif
