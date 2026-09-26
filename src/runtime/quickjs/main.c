#include <kernel.h>
#include <stdio.h>

#include <athena/boot.h>
#include <athena/module.h>
#include <athena/exceptions.h>
#include <ath_env.h>

int main(int argc, char **argv) {
    if (athena_boot(argc, argv) < 0) {
        athena_display_crash_screen("Boot failure", "A native module failed to initialize.", dark_mode);
        return 1;
    }

    for (;;) {
        const char *entry = athena_boot_entry();
        const char *err_msg;
        const char *next;

        dbgprintf("[AthenaCore] Running script: %s\n", entry);
        err_msg = run_script(entry, false);

        /* std.reload(), or a launched script going back to its launcher. */
        next = athena_runtime_next_script(entry, err_msg);
        if (next) {
            dbgprintf("[AthenaCore] Switching to script: %s\n", next);
            set_default_script(next);
            continue;
        }

        if (err_msg != NULL) {
            dbgprintf("\n==================== [ATHENA CORE ERROR] ====================\n");
            dbgprintf("%s\n", err_msg);
            dbgprintf("=============================================================\n");
            printf("\n[AthenaCore Error]: %s\n", err_msg);

            // Render On-Screen Crash Screen on TV; it never returns.
            athena_display_crash_screen("JavaScript Uncaught Exception", err_msg, dark_mode);
        }
        break;
    }

    athena_modules_shutdown();
    dbgprintf("[AthenaCore] Execution finished successfully.\n");
    return 0;
}
