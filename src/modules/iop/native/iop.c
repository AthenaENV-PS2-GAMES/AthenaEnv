#include <smem.h>

#include <athena/iop.h>

AthenaIopResult athena_iop_get_memory_stats(AthenaIopMemoryStats *out) {
    int32_t free_memory = 0;
    module_entry *freeram = iopman_search_module("freeram");

    if (!freeram || iopman_load_module(freeram, 0, NULL) == MODULE_STATUS_ERROR)
        return ATHENA_IOP_ERR_FREERAM;
    if (smem_read(IOP_FREERAM_ADDR, &free_memory, sizeof(free_memory)) < 0)
        return ATHENA_IOP_ERR_READ;
    if (free_memory < 0 || free_memory > IOP_TOTAL_RAM)
        return ATHENA_IOP_ERR_INVALID;
    out->free = free_memory;
    out->used = IOP_TOTAL_RAM - free_memory;
    return ATHENA_IOP_OK;
}

iopman_define_module(freeram);

void athena_iop_register_iop(void) {
    static const char *const no_dependencies[] = { NULL };
    iopman_ensure_module_buffer("freeram", freeram, no_dependencies, NULL, NULL);
}
