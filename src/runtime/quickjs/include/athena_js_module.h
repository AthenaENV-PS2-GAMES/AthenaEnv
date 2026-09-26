#ifndef ATHENA_JS_MODULE_H
#define ATHENA_JS_MODULE_H

#include <quickjs.h>

/* QuickJS binding of a module. Generated into src/generated/js_registry.c. */
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

/*
 * Source of the JavaScript module (module.json "js") imported as
 * `module_name`, not NUL-terminated, or NULL when the build has none.
 */
const char *athena_find_js_module(const char *module_name, size_t *length);

#ifdef __cplusplus
}
#endif

#endif /* ATHENA_JS_MODULE_H */
