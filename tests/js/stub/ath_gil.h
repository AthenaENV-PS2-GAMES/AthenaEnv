/* Host stand-in for src/runtime/quickjs/include/ath_gil.h: the runner has one script thread. */
#ifndef ATH_GIL_H
#define ATH_GIL_H

void athena_js_gil_lock(void);
void athena_js_gil_unlock(void);

#endif
