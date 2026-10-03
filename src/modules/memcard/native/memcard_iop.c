#include <stdio.h>
#include <libmc.h>

#include <athena/debug.h>
#include <athena/iop_manager.h>
#include <athena/memcard.h>

#include "memcard_internal.h"

iopman_define_module(sio2man);
iopman_define_module(mcman);
iopman_define_module(mcserv);

static module_entry *mcserv_entry;
/* mcInit() result for the IOP boot counted in init_generation. */
static int init_result = -1;
static uint32_t init_generation;

static int mcserv_started(void *mod) {
    module_entry *entry = (module_entry *)mod;

    if (entry && entry->started) {
        /* libmc rebinds by itself after an IOP reboot. */
        init_result = mcInit(MC_TYPE_XMC);
        init_generation = iopman_reset_count();
        if (init_result < 0)
            dbgprintf("[Memcard] mcInit failed: %d\n", init_result);
    }
    return 0;
}

/* sio2man may already be registered by another module (gamepad). */
void athena_memcard_register_iop(void) {
    static const char *const after_filexio[] = { "fileXio", NULL };
    static const char *const after_sio2man[] = { "sio2man", NULL };
    static const char *const after_mcman[] = { "mcman", NULL };

    if (!iopman_ensure_module_buffer("sio2man", sio2man, after_filexio, NULL, NULL))
        return;
    if (!iopman_ensure_module_buffer("mcman", mcman, after_sio2man, NULL, NULL))
        return;
    mcserv_entry = iopman_ensure_module_buffer("mcserv", mcserv, after_mcman, mcserv_started, NULL);
    if (mcserv_entry)
        iopman_start_module_at_boot(mcserv_entry);
}

int memcard_driver_status(void) {
    if (!mcserv_entry || !mcserv_entry->started || init_result < 0 ||
        init_generation != iopman_reset_count())
        return ATHENA_MEMCARD_ERR_NOT_READY;
    return ATHENA_MEMCARD_OK;
}

uint32_t memcard_driver_generation(void) {
    return iopman_reset_count();
}

int athena_memcard_prepare(void) {
    if (memcard_driver_status() == ATHENA_MEMCARD_OK)
        return ATHENA_MEMCARD_OK;
    /* Disabled in athena.ini or dropped by an IOP reset: start it now. */
    if (mcserv_entry && !mcserv_entry->started &&
        iopman_load_module(mcserv_entry, 0, NULL) != MODULE_STATUS_LOADED)
        dbgprintf("[Memcard] could not start mcserv\n");
    return memcard_driver_status();
}
