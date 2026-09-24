#include <kernel.h>

#include <athena.h>
#include <athena/exceptions.h>

/*
 * Entry point of the native runtime (RUNTIME=native). The core boots exactly
 * as it does for QuickJS, then control goes to the application's
 * athena_main(). No scripting engine is linked.
 */

int main(int argc, char **argv) {
    if (athena_boot(argc, argv) < 0) {
        athena_display_crash_screen("Boot failure", "A native module failed to initialize.", dark_mode);
        return 1;
    }

    int ret = athena_main(argc, argv);

    athena_modules_quiesce();
    athena_modules_shutdown();
    return ret;
}
