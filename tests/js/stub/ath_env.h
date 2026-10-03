/*
 * Host stand-in for src/runtime/quickjs/include/ath_env.h (which needs the
 * PS2SDK and quickjs-libc): only what module bindings use.
 */
#ifndef ATH_ENV_H
#define ATH_ENV_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <quickjs.h>

#define countof(x) (sizeof(x) / sizeof((x)[0]))

JSModuleDef *athena_push_module(JSContext *ctx, JSModuleInitFunc *func, const JSCFunctionListEntry *func_list,
    int len, const char *module_name);
int athena_register_class(JSContext *ctx, JSClassID *class_id, const JSClassDef *class_def);

/* Script output (src/runtime/quickjs/ath_output.c, linked in the runner). */
void athena_runtime_output(const char *text, size_t length);
const char *athena_runtime_output_current(size_t *length);
unsigned int athena_runtime_output_version(void);

#endif
