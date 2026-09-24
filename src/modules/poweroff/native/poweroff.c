#include <athena/iop_manager.h>

iopman_define_module(poweroff);

/* Registered only; athena.ini ("poweroff = true") or IOP.loadModule() starts it. */
void athena_poweroff_register_iop(void) {
    static const char *const no_dependencies[] = { NULL };
    iopman_ensure_module_buffer("poweroff", poweroff, no_dependencies, NULL, NULL);
}
