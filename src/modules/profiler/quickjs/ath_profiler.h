#ifndef ATHENA_PROFILER_BINDING_H
#define ATHENA_PROFILER_BINDING_H
#include <ath_env.h>
JSModuleDef *athena_profiler_js_init(JSContext *ctx);
void athena_profiler_js_cleanup(JSContext *ctx);
#endif
