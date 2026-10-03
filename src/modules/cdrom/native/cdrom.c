#include <athena/iop_manager.h>

iopman_define_module(cdfs);

void athena_cdrom_register_iop(void) {
    static const char *const after_filexio[] = { "fileXio", NULL };
    module_entry *cdfs_entry =
        iopman_ensure_module_buffer("cdfs", cdfs, after_filexio, NULL, NULL);

    if (cdfs_entry)
        iopman_start_module_at_boot(cdfs_entry);
}
