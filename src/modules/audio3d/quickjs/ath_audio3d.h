#ifndef ATHENA_AUDIO3D_BINDING_H
#define ATHENA_AUDIO3D_BINDING_H
#include <ath_env.h>
JSModuleDef *athena_audio3d_js_init(JSContext *ctx);
void athena_audio3d_js_cleanup(JSContext *ctx);
#endif
