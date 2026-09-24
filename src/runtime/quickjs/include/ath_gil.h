#ifndef ATH_GIL_H
#define ATH_GIL_H

void athena_js_gil_init(void);
void athena_js_gil_lock(void);
void athena_js_gil_unlock(void);
void athena_js_gil_destroy(void);

#endif /* ATH_GIL_H */
