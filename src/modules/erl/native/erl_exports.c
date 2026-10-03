#include <string.h>

#include <tamtypes.h>
#include <erl.h>

#include <athena/erl.h>

/*
 * export_list[] is generated after a first link (build-exports.sh) and lists
 * every global symbol of the binary, so ERL modules loaded at runtime can
 * resolve against it. It is only built when this module is selected.
 */
extern struct export_list_t {
    char * name;
    void * pointer;
} export_list[];

static char * prohibit_list[] = {
    "_edata", "_end_bss", "_fbss", "_fdata", "_fini",
    "_ftext", "_init", "main",
    0
};

int athena_erl_init(void) {
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

    return 0;
}
