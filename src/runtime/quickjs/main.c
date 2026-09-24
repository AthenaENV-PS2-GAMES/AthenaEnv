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

    const char *err_msg = NULL;
    /* std.reload() longjmps back here after switching the entry script. */
    setjmp(*get_reset_buf());

    dbgprintf("[AthenaCore] Running script: %s\n", athena_boot_entry());
    err_msg = run_script(athena_boot_entry(), false);

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

    athena_modules_shutdown();
    dbgprintf("[AthenaCore] Execution finished successfully.\n");
    return 0;
}
