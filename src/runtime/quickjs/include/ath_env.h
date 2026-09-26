#ifndef ATH_ENV_H
#define ATH_ENV_H

#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <setjmp.h>

#include <quickjs-libc.h>
#include <athena/boot.h>
#include <athena/module.h>
#include <athena/debug.h>
#include <athena/macros.h>

#define ATHENA_PROP_INT32(item) 	JS_PROP_INT32_DEF(stringify(item),    item,   JS_PROP_CONFIGURABLE )
#define countof(x) (sizeof(x) / sizeof((x)[0]))

void poweroffHandler(void *arg);

const char* run_script(const char* script, bool isBuffer);
void destroy_vm(JSContext* ctx);

/*
 * Switching scripts without restarting the ELF.
 *
 * std.reload(script, { returnTo }) requests the switch: the running script is
 * interrupted (uncatchable), run_script() tears its VM down as after an
 * error, and main() starts `script`. With `returnTo`, that script returns to
 * `returnTo` when it ends, fails, or the player holds SELECT+START on the
 * pad in port 1 for a second; std.lastRun() then tells how it went.
 */
typedef enum {
    ATHENA_RUN_NONE,        /* no script ran before this one */
    ATHENA_RUN_FINISHED,    /* ended normally */
    ATHENA_RUN_ERROR,       /* uncaught exception or missing file */
    ATHENA_RUN_EXITED,      /* left with SELECT+START */
    ATHENA_RUN_RELOADED,    /* called std.reload() */
} AthenaRunStatus;

/* Requests the switch; `return_to` may be NULL. Paths are copied. */
void athena_runtime_request_reload(const char *script, const char *return_to);

/* Non-zero while the running script must stop: a switch is pending. Polls SELECT+START. */
int athena_runtime_stop_requested(void);

/*
 * Called by main() after run_script(script) returned `error` (NULL on
 * success). Records the outcome and returns the script to run next, or NULL
 * to keep the default behavior (crash screen on error, exit otherwise).
 */
const char *athena_runtime_next_script(const char *script, const char *error);

/*
 * Outcome of the previous script: its path, its error (NULL when none) and
 * what it printed (the last 16 KiB). Any pointer argument may be NULL.
 */
AthenaRunStatus athena_runtime_last_run(const char **script, const char **error,
                                        const char **output);

/* Script output for std.lastRun() (ath_output.c). */
/* Records output (console.log, print, dumped errors) of the running script. */
void athena_runtime_output(const char *text, size_t length);
/* Makes the running script's output the last output, and clears it. */
void athena_runtime_output_rotate(void);
/* Output of the previous script (its last 16 KiB), never NULL. */
const char *athena_runtime_output_last(void);

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
