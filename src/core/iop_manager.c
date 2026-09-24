#include <athena/iop_manager.h>
#include <stdlib.h>
#include <string.h>

#include <sifrpc.h>
#include <loadfile.h>
#include <iopheap.h>
#include <iopcontrol.h>
#include <smod.h>

#include <sbv_patches.h>
#include <smem.h>

#include <athena/debug.h>

#define MODULE_REGISTRY_SIZE 64

module_entry module_registry[MODULE_REGISTRY_SIZE] = { 0 };

uint32_t registry_entries = 0;

const uint8_t default_incompatibility_id_mask[4] = {
    EMPTY_ENTRY,
    EMPTY_ENTRY,
    EMPTY_ENTRY,
    EMPTY_ENTRY
};

module_entry *iopman_register_module(char* name, void *data, uint32_t size, uint8_t dependencies[4], void *init_func, void *end_func) {
    if (registry_entries >= MODULE_REGISTRY_SIZE || !name || !dependencies) {
        return NULL;
    }

    module_registry[registry_entries].id = registry_entries;

    module_registry[registry_entries].name = name;
    module_registry[registry_entries].data = data;
    module_registry[registry_entries].size = size;

    memcpy(&module_registry[registry_entries].dependencies, dependencies, 4);
    memcpy(&module_registry[registry_entries].incompatibilities, &default_incompatibility_id_mask, 4);
    
    module_registry[registry_entries].init = (iopman_func)init_func;
    module_registry[registry_entries].end = (iopman_func)end_func;

    return &module_registry[registry_entries++];
}

void iopman_add_incompatible_module(module_entry *module, module_entry *incompatibility) {
    if (!module || !incompatibility || module == incompatibility) {
        return;
    }

    for (uint8_t i = 0; i < 4; i++) {
        if (module->incompatibilities[i] == EMPTY_ENTRY) {
            module->incompatibilities[i] = incompatibility->id;
            for (uint8_t j = 0; j < 4; j++) {
                if (incompatibility->incompatibilities[j] == EMPTY_ENTRY) {
                    incompatibility->incompatibilities[j] = module->id;
                    return;
                }
            }
            dbgprintf("AthenaEnv: IOP incompatibility table is full for '%s'\n",
                incompatibility->name);
            return;
        }
    }

    dbgprintf("AthenaEnv: IOP incompatibility table is full for '%s'\n",
        module->name);
}

static module_entry *incompatible_module = NULL;

module_entry *iopman_get_incompatible_module() {
    return incompatible_module;
}

static int iopman_load_module_internal(module_entry *module, int arglen,
    char *args, bool loading[MODULE_REGISTRY_SIZE]) {
    if (!module || module->id >= registry_entries) {
        return MODULE_STATUS_ERROR;
    }

    if (module->started)
        return MODULE_STATUS_LOADED;

    if (loading[module->id]) {
        dbgprintf("AthenaEnv: IOP dependency cycle at '%s'\n", module->name);
        return MODULE_STATUS_ERROR;
    }
    loading[module->id] = true;

    for (uint8_t i = 0; i < 4; i++) {
        uint8_t incompatibility_id = module->incompatibilities[i];
        if (incompatibility_id == EMPTY_ENTRY) {
            continue;
        }
        if (incompatibility_id >= registry_entries) {
            loading[module->id] = false;
            return MODULE_STATUS_ERROR;
        }
        if (module_registry[incompatibility_id].started) {
            incompatible_module = &module_registry[incompatibility_id];
            loading[module->id] = false;
            return MODULE_STATUS_INCOMPATIBILITY;
        }
    }

    for (uint8_t i = 0; i < 4; i++) {
        uint8_t dependency_id = module->dependencies[i];
        int dependency_result;
        if (dependency_id == EMPTY_ENTRY) {
            continue;
        }
        if (dependency_id >= registry_entries) {
            loading[module->id] = false;
            return MODULE_STATUS_ERROR;
        }
        dependency_result = iopman_load_module_internal(
            &module_registry[dependency_id], 0, NULL, loading);
        if (dependency_result == MODULE_STATUS_ERROR ||
            dependency_result == MODULE_STATUS_INCOMPATIBILITY) {
            loading[module->id] = false;
            return dependency_result;
        }
    }

    int arg_length = arglen;
    char *arg_data = args;

    if (!arg_length || !arg_data) {
        arg_length = module->arglen;
        arg_data = module->args;
    }

    int id = 0, ret = 0;

    if (module->prepare) {
        if (module->prepare(module)) {
            loading[module->id] = false;
            return MODULE_STATUS_ERROR;
        }
    }
        
    if (!module->size) {
        char *file_path = module->data;
        id = SifExecModuleFile(file_path, arg_length, arg_data, &ret);
    } else {
        id = SifExecModuleBuffer(module->data, module->size, arg_length, arg_data, &ret);
    }

    bool success = (id > 0 && ret != 1);
    module->started = success;

    if (module->init && success)
        module->init(module);

    loading[module->id] = false;
    return success ? MODULE_STATUS_LOADED : MODULE_STATUS_ERROR;
}

int iopman_load_module(module_entry *module, int arglen, char *args) {
    bool loading[MODULE_REGISTRY_SIZE] = { false };
    incompatible_module = NULL;
    return iopman_load_module_internal(module, arglen, args, loading);
}

module_entry *iopman_search_module(const char *name) {
    if (!name) {
        return NULL;
    }

    for (uint32_t i = 0; i < registry_entries; i++) {
        if (!strcmp(module_registry[i].name, name)) {
            return &module_registry[i];
        }
    }

    return NULL;
}

module_entry *iopman_get_module(uint8_t id) {
    if (id < registry_entries) {
        return &module_registry[id];
    }

    return NULL;
}

module_entry *iopman_get_modules(uint32_t *top) {
    if (top) 
        *top = registry_entries;

    return module_registry;
}

void iopman_reset() {
    incompatible_module = NULL;
    for (uint32_t i = 0; i < registry_entries; i++) {
        if (module_registry[i].started) {
            if (module_registry[i].end) {
                module_registry[i].end(&module_registry[i]);
            }
            module_registry[i].started = false;
        }
    }

    dbgprintf("AthenaEnv: Starting IOP Reset...\n");
    SifInitRpc(0);
    while (!SifIopReset("", 0)){};

    while (!SifIopSync()){};
    SifInitRpc(0);
    dbgprintf("AthenaEnv: IOP reset done.\n");

    SifLoadFileInit();

    // install sbv patch fix
    dbgprintf("AthenaEnv: Installing SBV Patches...\n");
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
}

void iopman_modules_apply(iopman_func func) {
    if (!func) {
        return;
    }

    for (uint32_t i = 0; i < registry_entries; i++) {
        func(&module_registry[i]);
    }
}

module_entry *iopman_ensure_module(char *name, void *data, uint32_t size,
    const char *const *dependencies, void *init_func, void *end_func) {
    uint8_t ids[4] = { EMPTY_ENTRY, EMPTY_ENTRY, EMPTY_ENTRY, EMPTY_ENTRY };
    module_entry *module = iopman_search_module(name);

    if (module)
        return module;
    for (int i = 0; dependencies && dependencies[i]; i++) {
        module_entry *dependency = iopman_search_module(dependencies[i]);
        if (i >= 4 || !dependency) {
            dbgprintf("AthenaEnv: IOP module '%s' needs '%s', which is not registered\n",
                name, dependencies[i]);
            return NULL;
        }
        ids[i] = dependency->id;
    }
    return iopman_register_module(name, data, size, ids, init_func, end_func);
}
