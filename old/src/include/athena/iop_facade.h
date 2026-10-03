#ifndef ATHENA_IOP_FACADE_H
#define ATHENA_IOP_FACADE_H

#include <stdint.h>
#include <stddef.h>

#include <iop_manager.h>

typedef struct AthenaIopModuleInfo {
    char name[64];
    uint8_t id;
} AthenaIopModuleInfo;

typedef struct AthenaIopModuleListing {
    AthenaIopModuleInfo *modules;
    int count;
} AthenaIopModuleListing;

typedef struct AthenaIopMemoryStats {
    uint32_t free;
    uint32_t used;
} AthenaIopMemoryStats;

module_entry *athena_iop_register_module(const char *name, void *data, size_t size,
                                         const uint8_t dependencies[4],
                                         iopman_func init, iopman_func end);

int athena_iop_load_module(module_entry *module, int arg_len, const char *args);
module_entry *athena_iop_get_module_by_name(const char *name);
module_entry *athena_iop_get_module_by_id(uint8_t id);

AthenaIopModuleListing *athena_iop_list_modules(void);
void athena_iop_module_listing_free(AthenaIopModuleListing *listing);

void athena_iop_reset(void);
void athena_iop_get_memory_stats(AthenaIopMemoryStats *stats);

#endif /* ATHENA_IOP_FACADE_H */
