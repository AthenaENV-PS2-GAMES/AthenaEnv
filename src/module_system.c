#include <kernel.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <def_mods.h>
#include <dbgprintf.h>

#include <usbhdfsd-common.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <fileio.h>
#include <io_common.h>

#include <macros.h>

#define started_from(device) (strstr(path, device) == path)

uint8_t no_dependencies[4] = {
	EMPTY_ENTRY, 
	EMPTY_ENTRY,
	EMPTY_ENTRY,
	EMPTY_ENTRY
};

#define iop_deps(a, b, c, d) ((uint8_t []) { a, b, c, d })
#define iop_dependency(dep) iop_deps(iopman_dependency(dep), EMPTY_ENTRY, EMPTY_ENTRY, EMPTY_ENTRY)

#define require_iop_module(module) \
	do { \
		if (!(module)) { \
			dbgprintf("AthenaEnv: failed to register IOP module\n"); \
			return; \
		} \
	} while (0)

static int mcserv_init_callback(void *mod) {
	module_entry *entry = (module_entry *)mod;
	if (entry && entry->started) {
		mcInit(MC_TYPE_XMC);
	}
	return 0;
}

void register_iop_modules() {
	module_entry *iomanX_entry = 
		iopman_register_module_buffer("iomanX", iomanX, no_dependencies, NULL, NULL);
	require_iop_module(iomanX_entry);

	module_entry *fileXio_entry = 
		iopman_register_module_buffer("fileXio", fileXio, iop_dependency(iomanX_entry), fileXioInit, fileXioExit);
	require_iop_module(fileXio_entry);

	module_entry *sio2man_entry = 
		iopman_register_module_buffer("sio2man", sio2man, iop_dependency(fileXio_entry), NULL, NULL);
	require_iop_module(sio2man_entry);

	module_entry *mcman_entry = 
		iopman_register_module_buffer("mcman", mcman, iop_dependency(sio2man_entry), NULL, NULL);
	require_iop_module(mcman_entry);

	module_entry *mcserv_entry = 
		iopman_register_module_buffer("mcserv", mcserv, iop_dependency(mcman_entry), mcserv_init_callback, NULL);
	require_iop_module(mcserv_entry);
	iopman_start_module_at_boot(mcserv_entry);

	module_entry *padman_entry = 
		iopman_register_module_buffer("padman", padman, iop_dependency(sio2man_entry), NULL, NULL);
	require_iop_module(padman_entry);
	iopman_start_module_at_boot(padman_entry);

	module_entry *cdfs_entry = 
		iopman_register_module_buffer("cdfs", cdfs, iop_dependency(fileXio_entry), NULL, NULL);
	require_iop_module(cdfs_entry);
	iopman_start_module_at_boot(cdfs_entry);

	module_entry *usbd_entry = 
		iopman_register_module_buffer("usbd", usbd, no_dependencies, NULL, NULL);
	require_iop_module(usbd_entry);

	module_entry *bdm_entry = 
		iopman_register_module_buffer("bdm", bdm, iop_dependency(fileXio_entry), NULL, NULL);
	require_iop_module(bdm_entry);

	module_entry *bdmfs_fatfs_entry = 
		iopman_register_module_buffer("bdmfs_fatfs", bdmfs_fatfs, iop_dependency(bdm_entry), NULL, NULL);
	require_iop_module(bdmfs_fatfs_entry);

	module_entry *usbmass_bd_entry =
		iopman_register_module_buffer("usbmass_bd", usbmass_bd,
			iop_deps(bdmfs_fatfs_entry->id, usbd_entry->id, EMPTY_ENTRY, EMPTY_ENTRY),
			NULL, NULL);
	require_iop_module(usbmass_bd_entry);

	module_entry *poweroff_entry =
		iopman_register_module_buffer("poweroff", poweroff, no_dependencies, NULL, NULL);
	require_iop_module(poweroff_entry);

	module_entry *freeram_entry =
		iopman_register_module_buffer("freeram", freeram, no_dependencies, NULL, NULL);
	require_iop_module(freeram_entry);
}

void prepare_IOP() {
    dbgprintf("AthenaEnv: Starting IOP Reset...\n");
    SifInitRpc(0);
    #if defined(RESET_IOP)
    while (!SifIopReset("", 0)){};
    #endif
    while (!SifIopSync()){};
    SifInitRpc(0);
    dbgprintf("AthenaEnv: IOP reset done.\n");

    // install sbv patch fix
    dbgprintf("AthenaEnv: Installing SBV Patches...\n");
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
}

bool wait_device(char *path) {
    dbgprintf("waiting for '%s'\n", path);
    struct stat buffer;
    int ret = -1;
    int retries = 500;

    while (ret != 0 && retries > 0) {
        ret = stat(path, &buffer);
        /* Wait until the device is ready */
        nopdelay();
        retries--;
    }

    return ret == 0;
}

char *get_boot_device(const char* path) {
	char * device = NULL;

	if (started_from("mass")) {
		device = "bdm";
	} else if (started_from("mc")) {
		device = "mcserv";
	} else if (started_from("cdfs") || started_from("cdrom")) {
		device = "cdfs";
	}

	return device;
}

char *get_block_device(const char* path) {
	char massdev[7] = { 0 };
	strncpy(massdev, path, 6);
	int fd = fileXioDopen(massdev);
	if (fd >= 0) {
		char dev_name[10];
		if (fileXioIoctl2(fd, USBMASS_IOCTL_GET_DRIVERNAME, NULL, 0, dev_name, sizeof(dev_name) - 1) >= 0) {
			fileXioDclose(fd);

			if (!strncmp(dev_name, "usb", 3)) {
				return "usbmass_bd";
			}
		}
	}

	return NULL;
}
