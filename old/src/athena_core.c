#include <string.h>

#include <athena_core.h>

char boot_path[255] = { 0 };
char default_script[128] = "main.js";
bool boot_logo = true;
bool dark_mode = true;

static AthenaAppMainFn athena_registered_app_main;

void set_default_script(const char *path) {
    strcpy(default_script, path);
    default_script[strlen(path)] = '\0';
}

const char *athena_get_default_script(void) {
    return default_script;
}

void athena_register_app_main(AthenaAppMainFn fn) {
    athena_registered_app_main = fn;
}

int athena_run_app_main(int argc, char **argv) {
    if (!athena_registered_app_main)
        return -1;
    return athena_registered_app_main(argc, argv);
}
