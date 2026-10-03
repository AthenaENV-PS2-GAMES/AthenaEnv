#include <stdlib.h>
#include <string.h>

#include <athena/iop_facade.h>
#include <smem.h>

module_entry *athena_iop_register_module(const char *name, void *data, size_t size,
                                         const uint8_t dependencies[4],
                                         iopman_func init, iopman_func end)
{
    return iopman_register_module((char *)name, data, (uint32_t)size,
                                  (uint8_t *)dependencies, init, end);
}

int athena_iop_load_module(module_entry *module, int arg_len, const char *args)
{
    if (!module)
        return MODULE_STATUS_ERROR;

    return iopman_load_module(module, arg_len, (char *)args);
}

module_entry *athena_iop_get_module_by_name(const char *name)
{
    if (!name)
        return NULL;
    return iopman_search_module(name);
}

module_entry *athena_iop_get_module_by_id(uint8_t id)
{
    return iopman_get_module(id);
}

AthenaIopModuleListing *athena_iop_list_modules(void)
{
    uint32_t list_size = 0;
    module_entry *modules = iopman_get_modules(&list_size);
    AthenaIopModuleListing *listing = calloc(1, sizeof(*listing));
    if (!listing)
        return NULL;

    if (!list_size || !modules)
        return listing;

    listing->modules = calloc(list_size, sizeof(AthenaIopModuleInfo));
    if (!listing->modules)
        return listing;

    for (uint32_t i = 0; i < list_size; i++) {
        strncpy(listing->modules[i].name, modules[i].name, sizeof(listing->modules[i].name) - 1);
        listing->modules[i].id = modules[i].id;
        listing->count++;
    }

    return listing;
}

void athena_iop_module_listing_free(AthenaIopModuleListing *listing)
{
    if (!listing)
        return;
    free(listing->modules);
    free(listing);
}

void athena_iop_reset(void)
{
    iopman_reset();
}

void athena_iop_get_memory_stats(AthenaIopMemoryStats *stats)
{
    s32 freeram = 0;
    s32 usedram = 0;

    if (!stats)
        return;

    iopman_load_module(iopman_search_module("freeram"), 0, NULL);
    smem_read((void *)(1 * 1024 * 1024), &freeram, 4);
    usedram = (2 * 1024 * 1024) - freeram;

    stats->free = (uint32_t)freeram;
    stats->used = (uint32_t)usedram;
}
