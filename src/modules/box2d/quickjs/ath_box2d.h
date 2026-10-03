#ifndef ATH_BOX2D_H
#define ATH_BOX2D_H

#include <quickjs.h>

JSModuleDef *athena_box2d_init(JSContext *ctx);
void athena_box2d_cleanup(JSContext *ctx);

#endif
