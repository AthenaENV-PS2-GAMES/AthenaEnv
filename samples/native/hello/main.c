/*
 * Minimal AthenaEnv application in C, without QuickJS.
 *
 *   node tools/modules.js configure --modules=system
 *   make RUNTIME=native APP_SRCS=samples/native/hello/main.c
 */
#include <kernel.h>
#include <debug.h>

#include <athena.h>

int athena_main(int argc, char **argv) {
    init_scr();
    scr_printf("\n  AthenaEnv native runtime\n\n");
    scr_printf("  Boot path: %s\n", boot_path);
    scr_printf("  Modules:  ");
    for (int i = 0; athena_native_modules[i].id; i++)
        scr_printf(" %s", athena_native_modules[i].id);
    scr_printf("\n  Used memory: %u bytes\n", (unsigned)get_used_memory());

    SleepThread();
    return 0;
}
