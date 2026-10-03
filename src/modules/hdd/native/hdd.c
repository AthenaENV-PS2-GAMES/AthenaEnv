#include <athena/iop_manager.h>

iopman_define_module(ps2dev9);
iopman_define_module(bdm);
iopman_define_module(bdmfs_fatfs);
iopman_define_module(ata_bd);

/* bdm and bdmfs_fatfs may already be registered by other storage modules. */
void athena_hdd_register_iop(void) {
    static const char *const after_filexio[] = { "fileXio", NULL };
    static const char *const after_bdm[] = { "bdm", NULL };
    static const char *const after_fatfs_and_dev9[] = { "bdmfs_fatfs", "ps2dev9", NULL };

    if (!iopman_ensure_module_buffer("ps2dev9", ps2dev9, after_filexio, NULL, NULL))
        return;
    if (!iopman_ensure_module_buffer("bdm", bdm, after_filexio, NULL, NULL))
        return;
    if (!iopman_ensure_module_buffer("bdmfs_fatfs", bdmfs_fatfs, after_bdm, NULL, NULL))
        return;
    iopman_ensure_module_buffer("ata_bd", ata_bd, after_fatfs_and_dev9, NULL, NULL);
}
