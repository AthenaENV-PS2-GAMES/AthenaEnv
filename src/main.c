#include <kernel.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdio.h>

#include <memory.h>
#include <strUtils.h>
#include <ath_env.h>
#include <def_mods.h>
#include <excepHandler.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <fileio.h>
#include <ps2sdkapi.h>

#include <dirent.h>
#include <errno.h>

#include <readini.h>
#include <erl.h>
#include <iop_manager.h>
#include <macros.h>
#include <excepHandler.h>

char boot_path[255] = { 0 };
bool dark_mode = true;
char default_script[128] = "main.js";
char default_cfg[128] = "athena.ini";

char MountPoint[32+6+1]; // max partition name + 'hdd0:/' + '\0'

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

extern struct export_list_t {
    char * name;
    void * pointer;
} export_list[];

static char * prohibit_list[] = {
    "_edata", "_end_bss", "_fbss", "_fdata", "_fini",
    "_ftext", "_init", "main", 
    0
};

static void export_symbols() {
    struct export_list_t * p;
    int i, prohibit;
    
    for (p = export_list; p->name; p++) {
        prohibit = 0;
        for (i = 0; prohibit_list[i]; i++) {
            if (!(strcmp(prohibit_list[i], p->name))) {
                prohibit = 1;
                break;
            }
        }
        if (!prohibit)
            erl_add_global_symbol(p->name, (u32)p->pointer);
    }
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

int main(int argc, char **argv) {
    IniReader ini;
    bool ignore_ini = false;
    bool reset_iop = true;

    init_memory_manager();
    register_iop_modules();

    if (argv[0] && !strncmp(argv[0], "cdrom0", 6))
        chdir("cdfs:/");

    getcwd(boot_path, sizeof(boot_path));

    // EE_SIO requires the SIF/IOP boot environment to be available.
    dbginit();
    dbgprintf("[AthenaCore] Entered main(argc=%d)\n", argc);
    dbgprintf("[AthenaCore] Memory manager initialized\n");
    dbgprintf("[AthenaCore] IOP modules registered\n");

    dbgprintf("\n========================================\n");
    dbgprintf("       AthenaEnv Core (Minimal)      \n");
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
                iopman_load_module(iopman_search_module("usbmass_bd"), 0, NULL);
            } else {
                iopman_load_module(iopman_search_module(boot_device), 0, NULL);
            }
        } else {
            iopman_load_module(iopman_search_module("fileXio"), 0, NULL);
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
            iopman_load_module(iopman_search_module(boot_device), 0, NULL);
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

    export_symbols();

    const char* err_msg = NULL;
    setjmp(*get_reset_buf());

    dbgprintf("[AthenaCore] Running script: %s\n", default_script);
    err_msg = run_script(default_script, false);

    if (err_msg != NULL) {
        dbgprintf("\n==================== [ATHENA CORE ERROR] ====================\n");
        dbgprintf("%s\n", err_msg);
        dbgprintf("=============================================================\n");
        printf("\n[AthenaCore Error]: %s\n", err_msg);

        // Render On-Screen Crash Screen on TV
        athena_display_crash_screen("JavaScript Uncaught Exception", err_msg, dark_mode);

        // Infinite loop to keep console output visible
        while (1) {
            SleepThread();
        }
    }

    dbgprintf("[AthenaCore] Execution finished successfully.\n");
    return 0;
}
