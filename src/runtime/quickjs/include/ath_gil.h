#ifndef ATH_GIL_H
#define ATH_GIL_H

#include <stddef.h>

struct JSRuntime;

/*
 * Stack that QuickJS may use on the calling thread, in bytes, below the point
 * where the thread enters the runtime. It must be smaller than the thread's
 * real stack, leaving room for the C code JavaScript calls (FreeType, image
 * decoders, printf): past it QuickJS throws a catchable "stack overflow"
 * instead of overrunning the stack. Applied on every gate handoff; threads
 * that never set it get ATH_GIL_DEFAULT_STACK_BUDGET. Call before locking.
 */
#define ATH_GIL_DEFAULT_STACK_BUDGET (8 * 1024)
void athena_js_gil_set_stack_budget(size_t bytes);

void athena_js_gil_init(void);
void athena_js_gil_lock(void);
void athena_js_gil_unlock(void);
/* Releases the gate for the last time from a thread that is about to exit. */
void athena_js_gil_leave(void);
/* Runtime whose per-thread state is swapped on each gate handoff. The caller
 * must hold the gate; pass NULL before the runtime is freed. */
void athena_js_gil_set_runtime(struct JSRuntime *rt);
void athena_js_gil_destroy(void);

#endif /* ATH_GIL_H */
