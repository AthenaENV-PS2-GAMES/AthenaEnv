#ifndef ATHENA_LOOP_QUICKJS_H
#define ATHENA_LOOP_QUICKJS_H

#include <ath_env.h>

JSModuleDef *athena_loop_init(JSContext *ctx);
void athena_loop_cleanup(JSContext *ctx);

#endif
