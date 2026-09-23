#ifndef ATH_ENV_H
#define ATH_ENV_H

#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <setjmp.h>

#include "../quickjs/quickjs-libc.h"
#include <dbgprintf.h>
#include <macros.h>

#define ATHENA_PROP_INT32(item) 	JS_PROP_INT32_DEF(stringify(item),    item,   JS_PROP_CONFIGURABLE )
#define countof(x) (sizeof(x) / sizeof((x)[0]))

extern char boot_path[255];
extern bool dark_mode;

void poweroffHandler(void *arg);

const char* run_script(const char* script, bool isBuffer);
void destroy_vm(JSContext* ctx);
jmp_buf *get_reset_buf();
void set_default_script(const char* path);

JSModuleDef *athena_push_module(JSContext* ctx, JSModuleInitFunc *func, const JSCFunctionListEntry *func_list, int len, const char* module_name);

/*
 * Registers a native class on the runtime of `ctx`. The id is allocated once
 * per process (JS_NewClassID keeps a non-zero id) and the class is added to
 * every new runtime, so modules keep working if the runtime is recreated.
 * Returns 0 on success, -1 on failure. Class prototypes are per context and
 * must still be set by the caller on each initialization.
 */
int athena_register_class(JSContext *ctx, JSClassID *class_id, const JSClassDef *class_def);

#endif
