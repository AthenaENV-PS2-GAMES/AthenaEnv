/*
 * What the MemoryCard module needs in the JS runner: the host mutex and
 * thread cores, the fake card of tests/host/fake_libmc.h and a GIL that
 * does nothing (the runner has a single script thread).
 */
#include "host_runtime.h"
#include "fake_libmc.h"

int athena_memcard_module_init(void);

void athena_js_gil_lock(void) {}
void athena_js_gil_unlock(void) {}

void memcard_host_init(void) {
    /* host_runtime.h helpers meant for the C tests. */
    (void)failures;
    (void)checks;
    (void)now_ms;
    (void)sleep_ms;
    fake_reset();
    athena_memcard_module_init();
}
