#include <athena/iop_manager.h>

iopman_define_module(usbd);
iopman_define_module(bdm);
iopman_define_module(bdmfs_fatfs);
iopman_define_module(usbmass_bd);

/* usbd may already be registered by another module (gamepad). */
void athena_usbmass_register_iop(void) {
    static const char *const no_dependencies[] = { NULL };
    static const char *const after_filexio[] = { "fileXio", NULL };
    static const char *const after_bdm[] = { "bdm", NULL };
    static const char *const after_fatfs_and_usbd[] = { "bdmfs_fatfs", "usbd", NULL };

    if (!iopman_ensure_module_buffer("usbd", usbd, no_dependencies, NULL, NULL))
        return;
    if (!iopman_ensure_module_buffer("bdm", bdm, after_filexio, NULL, NULL))
        return;
    if (!iopman_ensure_module_buffer("bdmfs_fatfs", bdmfs_fatfs, after_bdm, NULL, NULL))
        return;
    iopman_ensure_module_buffer("usbmass_bd", usbmass_bd, after_fatfs_and_usbd, NULL, NULL);
}
