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

#endif
