#ifndef ATHENA_BOOT_H
#define ATHENA_BOOT_H

#include <stdbool.h>

/*
 * Runtime-independent startup: memory accounting, IOP reset and boot device
 * drivers, athena.ini, exception handlers and native module initialization.
 * Both runtimes (QuickJS and native C) call athena_boot() first.
 */

#ifdef __cplusplus
extern "C" {
#endif

extern char boot_path[255];
extern bool dark_mode;
extern char MountPoint[32 + 6 + 1]; /* max partition name + 'hdd0:/' + '\0' */

/* Returns 0 on success, < 0 if a native module failed to initialize. */
int athena_boot(int argc, char **argv);

/* Entry point selected by --script / athena.ini (default "main.js"). */
const char *athena_boot_entry(void);
void set_default_script(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* ATHENA_BOOT_H */
