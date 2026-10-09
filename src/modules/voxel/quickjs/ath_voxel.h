#ifndef ATHENA_VOXEL_BINDING_H
#define ATHENA_VOXEL_BINDING_H
#include <ath_env.h>
JSModuleDef *athena_voxel_js_init(JSContext *ctx);
void athena_voxel_js_cleanup(JSContext *ctx);
#endif
