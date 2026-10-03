#include <kernel.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "def_mods.h"
#include <athena/debug.h>

#include <usbhdfsd-common.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <fileio.h>
#include <io_common.h>

#include <athena/macros.h>

#define started_from(device) (strstr(path, device) == path)

uint8_t no_dependencies[4] = {
	EMPTY_ENTRY, 
	EMPTY_ENTRY,
	EMPTY_ENTRY,
	EMPTY_ENTRY
};

#define iop_dependency(dep) ((uint8_t []) { iopman_dependency(dep), EMPTY_ENTRY, EMPTY_ENTRY, EMPTY_ENTRY })

#define require_iop_module(module) \
	do { \
		if (!(module)) { \
			dbgprintf("AthenaEnv: failed to register IOP module\n"); \
			return; \
		} \
	} while (0)

/*
 * Drivers every build needs for file I/O. Boot device drivers (memory card,
 * USB mass storage, disc) and other IRX are registered by their Athena
 * modules through the register_iop hook, right after this function.
 */
void register_iop_modules() {
	module_entry *iomanX_entry = 
		iopman_register_module_buffer("iomanX", iomanX, no_dependencies, NULL, NULL);
	require_iop_module(iomanX_entry);

	module_entry *fileXio_entry = 
		iopman_register_module_buffer("fileXio", fileXio, iop_dependency(iomanX_entry), fileXioInit, fileXioExit);
	require_iop_module(fileXio_entry);
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
		char dev_name[10] = { 0 };   /* the driver name may fill it without a terminator */
		int ret = fileXioIoctl2(fd, USBMASS_IOCTL_GET_DRIVERNAME, NULL, 0, dev_name, sizeof(dev_name) - 1);
		fileXioDclose(fd);
		if (ret >= 0) {
			/* BDM driver names: "usb", "sdc" (MX4SIO), "ata" (internal HDD), "sd" (i.LINK). */
			if (!strcmp(dev_name, "usb"))
				return "usbmass_bd";
			if (!strcmp(dev_name, "sdc"))
				return "mx4sio_bd";
			if (!strcmp(dev_name, "ata"))
				return "ata_bd";
			if (!strcmp(dev_name, "sd"))
				return "IEEE1394_bd";
		}
	}

	return NULL;
}
