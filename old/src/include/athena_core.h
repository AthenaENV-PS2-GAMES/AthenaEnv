#ifndef ATHENA_CORE_H
#define ATHENA_CORE_H

#include <stdbool.h>

/* Shared runtime state (no QuickJS dependency). */
extern char boot_path[255];
extern char default_script[128];
extern bool boot_logo;
extern bool dark_mode;

typedef int (*AthenaAppMainFn)(int argc, char **argv);

void init_bootlogo(void);
bool bootlogo_finished(void);
void set_default_script(const char *path);
const char *athena_get_default_script(void);

void athena_register_app_main(AthenaAppMainFn fn);
int athena_run_app_main(int argc, char **argv);

/* Register a C application entry before main() via GCC constructor. */
#define ATHENA_APP_ENTRY(fn)                                                    \
    static void athena_app_register_##fn(void) __attribute__((constructor));   \
    static void athena_app_register_##fn(void) { athena_register_app_main(fn); }

#endif /* ATHENA_CORE_H */
