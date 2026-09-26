#include <kernel.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdio.h>

#include <athena/boot.h>
#include <athena/memory.h>
#include <athena/module.h>
#include <athena/str_utils.h>
#include "def_mods.h"
#include <athena/debug.h>
#include <athena/exceptions.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <fileio.h>
#include <ps2sdkapi.h>

#include <dirent.h>
#include <errno.h>

#include <readini.h>
#include <athena/iop_manager.h>
#include <athena/macros.h>

char boot_path[255] = { 0 };
bool dark_mode = true;
static char default_script[128] = "main.js";
static char default_cfg[128] = "athena.ini";

char MountPoint[32+6+1];

static __attribute__((used)) void *bypass_modulated_libs() {
    int func = 0;
    func |= (int)_ps2sdk_ioctl;
    return (void*)func;
}

int mnt(const char* path, int index, int openmod)
{
    char PFS[5+1] = "pfs0:";
    if (index > 0)
        PFS[3] = '0' + index;

    dbgprintf("[AthenaCore] Mounting '%s' into pfs%d:\n", path, index);
    if (fileXioMount(PFS, path, openmod) < 0)
    {
        dbgprintf("[AthenaCore] Mount failed. Unmounting & trying again...\n");
        fileXioUmount(PFS);
        if (fileXioMount(PFS, path, openmod) < 0)
        {
            dbgprintf("[AthenaCore] Mount failed again!\n");
            return -1;
        } else {
            dbgprintf("[AthenaCore] Second mount succeeded!\n");
        }
    } else {
        dbgprintf("[AthenaCore] Mount successful on first attempt\n");
    }
    return 0;
}

void set_default_script(const char* path) {
    strncpy(default_script, path, sizeof(default_script) - 1);
    default_script[sizeof(default_script) - 1] = '\0';
}

const char *athena_boot_entry(void) {
    return default_script;
}

/* The driver may be absent when its module is not part of the build. */
static void load_boot_driver(const char *name) {
    module_entry *module = iopman_search_module(name);

    if (!module) {
        dbgprintf("[AthenaCore] Boot driver '%s' is not in this build\n", name);
        return;
    }
    iopman_load_module(module, 0, NULL);
}

static IniReader *active_ini = NULL;

static int apply_ini_module_config(void *mod) {
    module_entry *module = (module_entry *)mod;
    if (module && active_ini) {
        if (readini_bool(active_ini, module->name, &module->start_at_boot)) {
            dbgprintf("[AthenaCore] Config auto-start %s\n", module->name);
        }
    }
    return 0;
}

static int apply_start_module(void *mod) {
    module_entry *module = (module_entry *)mod;
    if (module && module->start_at_boot) {
        iopman_load_module(module, 0, NULL);
    }
    return 0;
}

int athena_boot(int argc, char **argv) {
    IniReader ini;
    bool ignore_ini = false;
    bool reset_iop = true;

    init_memory_manager();
    register_iop_modules();
    athena_modules_register_iop();

    if (argv[0] && !strncmp(argv[0], "cdrom0", 6))
        chdir("cdfs:/");

    getcwd(boot_path, sizeof(boot_path));

    // EE_SIO requires the SIF/IOP boot environment to be available.
    dbginit();
    dbgprintf("[AthenaCore] Booting (argc=%d)\n", argc);
    dbgprintf("[AthenaCore] Memory manager initialized\n");
    dbgprintf("[AthenaCore] IOP modules registered\n");

    dbgprintf("\n========================================\n");
    dbgprintf("               AthenaEnv                \n");
    dbgprintf("========================================\n");
    /* Identifies the binary in logs, e.g. to confirm a rebuilt ELF ran. */
    dbgprintf("[AthenaCore] Build: %s %s\n", __DATE__, __TIME__);
    dbgprintf("[AthenaCore] Boot path: %s\n", boot_path);

    if (argc > 1) {
        char* tmp_arg = NULL;
        for (int i = 1; i < argc; i++) {
            char* arg = argv[i];
            if ((tmp_arg = strpre("--script=", arg)) || (tmp_arg = strpre("-s=", arg))) {
                set_default_script(tmp_arg);
            } else if (!strcmp("--ignorecfg", arg) || !strcmp("-i", arg)) {
                ignore_ini = true;
            } else if (!strcmp("--noiopreset", arg) || !strcmp("-n", arg)) {
                reset_iop = false;
            } else if ((tmp_arg = strpre("--cfg=", arg)) || (tmp_arg = strpre("-c=", arg))) {
                strncpy(default_cfg, tmp_arg, sizeof(default_cfg) - 1);
                default_cfg[sizeof(default_cfg) - 1] = '\0';
            }
        }
    }

    char *boot_device = get_boot_device(boot_path);
    bool is_bd = boot_device ? !strncmp(boot_device, "bdm", 3) : false;

    if (reset_iop) {
        iopman_reset();

        if (boot_device) {
            if (is_bd) {
                /*
                 * mass: may be USB, MX4SIO, the internal HDD or i.LINK: start the
                 * block drivers of the build, find out which one holds the boot
                 * path (get_block_device), then keep only that one.
                 */
                static const char *const block_drivers[] = {
                    "usbmass_bd", "mx4sio_bd", "ata_bd", "IEEE1394_bd", NULL,
                };
                for (int i = 0; block_drivers[i]; i++)
                    load_boot_driver(block_drivers[i]);
            } else {
                load_boot_driver(boot_device);
            }
        } else {
            load_boot_driver("fileXio");
        }

        if (!strncmp(boot_path, "mass", 4)) {
            char temp_path[255];
            if (!strncmp(boot_path, "mass:", 5)) {
                snprintf(temp_path, sizeof(temp_path), "mass0:%s", boot_path + 5);
                chdir(temp_path);
            } else {
                strncpy(temp_path, boot_path, sizeof(temp_path) - 1);
                temp_path[sizeof(temp_path) - 1] = '\0';

                for (int i = 0; i < 5; i++) {
                    temp_path[4] = '0' + i;
                    wait_device(temp_path);
                    chdir(temp_path);

                    FILE *f = fopen(default_script, "r");
                    if (f) {
                        fclose(f);
                        break;
                    } 
                }
            }
            strncpy(boot_path, temp_path, sizeof(boot_path) - 1);
            boot_path[sizeof(boot_path) - 1] = '\0';
        }

        wait_device(boot_path);

        if (is_bd) {
            boot_device = get_block_device(boot_path);
            iopman_reset();
            load_boot_driver(boot_device);
            wait_device(boot_path);
        }
    }

    if (!ignore_ini) {
        if (!strncmp(boot_path, "cdfs", 4) && !strpre("cdrom0", default_cfg)) {
            memset(default_cfg, 0, sizeof(default_cfg));
            strcpy(default_cfg, "cdrom0:ATHENA.INI;1");
        }
            
        if (readini_open(&ini, default_cfg)) {
            active_ini = &ini;
            while (readini_getline(&ini)) {
                if (readini_emptyline(&ini)) {
                    continue;
                } else if (readini_string(&ini, "default_script", default_script)) {
                    dbgprintf("[AthenaCore] Config default_script: %s\n", default_script);
                } else {
                    iopman_modules_apply(apply_ini_module_config);
                }
            }
            active_ini = NULL;
            readini_close(&ini);
        }
    }

    installExceptionHandlers();

    if (reset_iop) {
        iopman_modules_apply(apply_start_module);
    }

    if (athena_modules_init() < 0) {
        dbgprintf("[AthenaCore] Native module initialization failed\n");
        return -1;
    }
    dbgprintf("[AthenaCore] Native modules initialized\n");

    return 0;
}
