#ifndef ATHENA_MODULE_H
#define ATHENA_MODULE_H

#include "../quickjs/quickjs.h"

typedef struct {
    const char *id;           /* Identifier, e.g. "system" */
    const char *module_name;  /* QuickJS module name, e.g. "System" */
    const char *global_alias; /* Global alias for globalThis (or NULL if none) */
    JSModuleDef *(*init)(JSContext *ctx);
    void (*cleanup)(JSContext *ctx);
} AthenaModuleEntry;

#ifdef __cplusplus
extern "C" {
#endif

void athena_register_all_modules(JSContext *ctx);
const char *athena_get_modules_bootstrap_script(void);
void athena_cleanup_all_modules(JSContext *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ATHENA_MODULE_H */
