#include <athena/iop_manager.h>

iopman_define_module(iLinkman);
iopman_define_module(bdm);
iopman_define_module(bdmfs_fatfs);
iopman_define_module(IEEE1394_bd);

/* bdm and bdmfs_fatfs may already be registered by other storage modules. */
void athena_ilink_register_iop(void) {
    static const char *const after_filexio[] = { "fileXio", NULL };
    static const char *const after_bdm[] = { "bdm", NULL };
    static const char *const after_fatfs_and_ilinkman[] = { "bdmfs_fatfs", "iLinkman", NULL };

    if (!iopman_ensure_module_buffer("iLinkman", iLinkman, after_filexio, NULL, NULL))
        return;
    if (!iopman_ensure_module_buffer("bdm", bdm, after_filexio, NULL, NULL))
        return;
    if (!iopman_ensure_module_buffer("bdmfs_fatfs", bdmfs_fatfs, after_bdm, NULL, NULL))
        return;
    iopman_ensure_module_buffer("IEEE1394_bd", IEEE1394_bd, after_fatfs_and_ilinkman, NULL, NULL);
}
