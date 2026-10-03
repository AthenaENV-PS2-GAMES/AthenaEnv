#ifndef ATHENA_MEMCARD_INTERNAL_H
#define ATHENA_MEMCARD_INTERNAL_H

#include <stdint.h>

/*
 * Driver state, kept apart from memcard.c (memcard_iop.c on the console,
 * a fake in the host tests).
 */

/* native.init hook: creates the lock every command takes. */
int athena_memcard_module_init(void);
/* native.register_iop hook. */
void athena_memcard_register_iop(void);

/* ATHENA_MEMCARD_OK when mcserv runs and libmc is bound to it. */
int memcard_driver_status(void);
/* Changes on every IOP reset: descriptors from before it are invalid. */
uint32_t memcard_driver_generation(void);

#endif /* ATHENA_MEMCARD_INTERNAL_H */
