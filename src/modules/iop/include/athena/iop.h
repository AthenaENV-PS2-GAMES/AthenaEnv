#ifndef ATHENA_IOP_H
#define ATHENA_IOP_H

#include <stdint.h>

#include <athena/iop_manager.h>

/*
 * IOP inspection on top of the core IOP manager (<athena/iop_manager.h>),
 * which already covers listing, searching, loading and resetting modules.
 */

typedef enum {
    ATHENA_IOP_OK = 0,
    ATHENA_IOP_ERR_FREERAM = -1,   /* freeram IRX could not be loaded */
    ATHENA_IOP_ERR_READ = -2,      /* SIF memory read failed */
    ATHENA_IOP_ERR_INVALID = -3,   /* value outside the IOP RAM size */
} AthenaIopResult;

typedef struct {
    int32_t free;
    int32_t used;
} AthenaIopMemoryStats;

/* Loads freeram on demand and reads the IOP's free memory. */
AthenaIopResult athena_iop_get_memory_stats(AthenaIopMemoryStats *out);

#endif /* ATHENA_IOP_H */
