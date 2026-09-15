#include <kernel.h>
#include <dbgprintf.h>
#include <stdbool.h>

#include <ath_gil.h>

static int athena_js_gil_semaphore = -1;
static bool athena_js_gil_initialized;

void athena_js_gil_init(void) {
    if (athena_js_gil_initialized) {
        dbgprintf("[AthenaCore] Warning: QuickJS GIL already initialized\n");
        return;
    }

    ee_sema_t config = {
        .max_count = 1,
        .init_count = 1,
        .attr = 0,
        .option = 0
    };

    athena_js_gil_semaphore = CreateSema(&config);
    if (athena_js_gil_semaphore < 0) {
        dbgprintf("[AthenaCore] Fatal: unable to create QuickJS GIL semaphore\n");
        Exit(1);
        return;
    }

    athena_js_gil_initialized = true;
}

void athena_js_gil_lock(void) {
    if (!athena_js_gil_initialized || athena_js_gil_semaphore < 0) {
        dbgprintf("[AthenaCore] Warning: QuickJS GIL lock requested before init or after destroy\n");
        return;
    }

    if (WaitSema(athena_js_gil_semaphore) < 0) {
        dbgprintf("[AthenaCore] Fatal: unable to acquire QuickJS GIL semaphore\n");
        Exit(1);
    }
}

void athena_js_gil_unlock(void) {
    if (!athena_js_gil_initialized || athena_js_gil_semaphore < 0) {
        dbgprintf("[AthenaCore] Warning: QuickJS GIL unlock requested before init or after destroy\n");
        return;
    }

    if (SignalSema(athena_js_gil_semaphore) < 0) {
        dbgprintf("[AthenaCore] Warning: unable to release QuickJS GIL semaphore\n");
    }
}

void athena_js_gil_destroy(void) {
    if (!athena_js_gil_initialized || athena_js_gil_semaphore < 0) {
        dbgprintf("[AthenaCore] Warning: QuickJS GIL destroy requested before init or after destroy\n");
        return;
    }

    if (DeleteSema(athena_js_gil_semaphore) < 0) {
        dbgprintf("[AthenaCore] Warning: unable to destroy QuickJS GIL semaphore\n");
    }

    athena_js_gil_semaphore = -1;
    athena_js_gil_initialized = false;
}
