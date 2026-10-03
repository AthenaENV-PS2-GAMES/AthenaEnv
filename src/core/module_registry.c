#include <string.h>

#include <athena/module.h>
#include <athena/debug.h>

static int athena_modules_count(void) {
    int count = 0;
    while (athena_native_modules[count].id)
        count++;
    return count;
}

void athena_modules_register_iop(void) {
    for (int i = 0; athena_native_modules[i].id; i++) {
        if (athena_native_modules[i].register_iop)
            athena_native_modules[i].register_iop();
    }
}

int athena_modules_init(void) {
    for (int i = 0; athena_native_modules[i].id; i++) {
        const AthenaNativeModule *module = &athena_native_modules[i];
        if (module->init && module->init() < 0) {
            dbgprintf("[AthenaCore] Module '%s' failed to initialize\n", module->id);
            return -1;
        }
    }
    return 0;
}

void athena_modules_shutdown(void) {
    for (int i = athena_modules_count() - 1; i >= 0; i--) {
        if (athena_native_modules[i].shutdown)
            athena_native_modules[i].shutdown();
    }
}

void athena_modules_quiesce(void) {
    for (int i = 0; athena_native_modules[i].id; i++) {
        if (athena_native_modules[i].quiesce)
            athena_native_modules[i].quiesce();
    }
}

int athena_modules_stop_requested(void) {
    for (int i = 0; athena_native_modules[i].id; i++) {
        if (athena_native_modules[i].stop_requested && athena_native_modules[i].stop_requested())
            return 1;
    }
    return 0;
}

bool athena_module_enabled(const char *id) {
    for (int i = 0; athena_native_modules[i].id; i++) {
        if (!strcmp(athena_native_modules[i].id, id))
            return true;
    }
    return false;
}
