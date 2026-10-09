#ifndef ATHENA_LOD_BINDING_H
#define ATHENA_LOD_BINDING_H
#include <ath_env.h>
JSModuleDef *athena_lod_js_init(JSContext *ctx);
void athena_lod_js_cleanup(JSContext *ctx);
#endif
