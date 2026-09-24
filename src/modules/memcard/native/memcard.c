#include <libmc.h>

#include <athena/iop_manager.h>

iopman_define_module(sio2man);
iopman_define_module(mcman);
iopman_define_module(mcserv);

static int mcserv_started(void *mod) {
    module_entry *entry = (module_entry *)mod;
    if (entry && entry->started)
        mcInit(MC_TYPE_XMC);
    return 0;
}

/* sio2man may already be registered by another module (gamepad). */
void athena_memcard_register_iop(void) {
    static const char *const after_filexio[] = { "fileXio", NULL };
    static const char *const after_sio2man[] = { "sio2man", NULL };
    static const char *const after_mcman[] = { "mcman", NULL };
    module_entry *mcserv_entry;

    if (!iopman_ensure_module_buffer("sio2man", sio2man, after_filexio, NULL, NULL))
        return;
    if (!iopman_ensure_module_buffer("mcman", mcman, after_sio2man, NULL, NULL))
        return;
    mcserv_entry = iopman_ensure_module_buffer("mcserv", mcserv, after_mcman, mcserv_started, NULL);
    if (mcserv_entry)
        iopman_start_module_at_boot(mcserv_entry);
}
