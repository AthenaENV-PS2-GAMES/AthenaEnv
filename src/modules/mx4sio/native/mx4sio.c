#include <athena/iop_manager.h>

iopman_define_module(sio2man);
iopman_define_module(bdm);
iopman_define_module(bdmfs_fatfs);
iopman_define_module(mx4sio_bd);

/* sio2man, bdm and bdmfs_fatfs may already be registered by other modules (gamepad, memcard, usbmass). */
void athena_mx4sio_register_iop(void) {
    static const char *const after_filexio[] = { "fileXio", NULL };
    static const char *const after_bdm[] = { "bdm", NULL };
    static const char *const after_fatfs_and_sio2man[] = { "bdmfs_fatfs", "sio2man", NULL };

    if (!iopman_ensure_module_buffer("sio2man", sio2man, after_filexio, NULL, NULL))
        return;
    if (!iopman_ensure_module_buffer("bdm", bdm, after_filexio, NULL, NULL))
        return;
    if (!iopman_ensure_module_buffer("bdmfs_fatfs", bdmfs_fatfs, after_bdm, NULL, NULL))
        return;
    iopman_ensure_module_buffer("mx4sio_bd", mx4sio_bd, after_fatfs_and_sio2man, NULL, NULL);
}
