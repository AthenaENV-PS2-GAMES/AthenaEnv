#ifndef ATHENA_SCENE3D_BINDING_H
#define ATHENA_SCENE3D_BINDING_H
#include <ath_env.h>
JSModuleDef *athena_scene3d_js_init(JSContext *ctx);
void athena_scene3d_js_cleanup(JSContext *ctx);
#endif
