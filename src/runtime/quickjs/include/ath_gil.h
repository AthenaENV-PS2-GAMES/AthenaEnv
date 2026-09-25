#ifndef ATH_GIL_H
#define ATH_GIL_H

struct JSRuntime;

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
