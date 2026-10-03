#include <kernel.h>
#include <quickjs.h>
#include <athena/debug.h>
#include <stdbool.h>
#include <string.h>

#include <ath_gil.h>

/* EE kernel thread ids are below 256. */
#define ATHENA_JS_GIL_MAX_THREADS 256

typedef struct {
    bool suspended;
    JSRuntimeThreadState state;
    size_t stack_budget;        /* 0: ATH_GIL_DEFAULT_STACK_BUDGET */
} AthenaJsGilThreadSlot;

static int athena_js_gil_semaphore = -1;
static bool athena_js_gil_initialized;
static JSRuntime *athena_js_gil_runtime;
static AthenaJsGilThreadSlot athena_js_gil_slots[ATHENA_JS_GIL_MAX_THREADS];

static AthenaJsGilThreadSlot *athena_js_gil_current_slot(void) {
    int id = GetThreadId();
    if (id < 0 || id >= ATHENA_JS_GIL_MAX_THREADS) return NULL;
    return &athena_js_gil_slots[id];
}

/* The QuickJS frame chain, pending exception and stack bounds belong to the
 * native thread that owns the gate, so they are swapped on every handoff. */
static void athena_js_gil_enter_runtime(void) {
    if (!athena_js_gil_runtime) return;

    AthenaJsGilThreadSlot *slot = athena_js_gil_current_slot();
    /* JS_Enter/JS_Resume derive the stack limit from it. */
    JS_SetMaxStackSize(athena_js_gil_runtime, slot && slot->stack_budget ?
        slot->stack_budget : ATH_GIL_DEFAULT_STACK_BUDGET);
    if (slot && slot->suspended) {
        JS_Resume(athena_js_gil_runtime, &slot->state);
        slot->suspended = false;
    } else {
        JS_Enter(athena_js_gil_runtime);
    }
}

static void athena_js_gil_suspend_runtime(void) {
    if (!athena_js_gil_runtime) return;

    AthenaJsGilThreadSlot *slot = athena_js_gil_current_slot();
    if (!slot) {
        dbgprintf("[AthenaCore] Warning: QuickJS GIL released by unknown thread id\n");
        return;
    }
    JS_Suspend(athena_js_gil_runtime, &slot->state);
    slot->suspended = true;
}

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

    athena_js_gil_runtime = NULL;
    memset(athena_js_gil_slots, 0, sizeof(athena_js_gil_slots));
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

    athena_js_gil_enter_runtime();
}

void athena_js_gil_unlock(void) {
    if (!athena_js_gil_initialized || athena_js_gil_semaphore < 0) {
        dbgprintf("[AthenaCore] Warning: QuickJS GIL unlock requested before init or after destroy\n");
        return;
    }

    athena_js_gil_suspend_runtime();

    if (SignalSema(athena_js_gil_semaphore) < 0) {
        dbgprintf("[AthenaCore] Warning: unable to release QuickJS GIL semaphore\n");
    }
}

void athena_js_gil_leave(void) {
    if (!athena_js_gil_initialized || athena_js_gil_semaphore < 0) {
        dbgprintf("[AthenaCore] Warning: QuickJS GIL leave requested before init or after destroy\n");
        return;
    }

    if (athena_js_gil_runtime)
        JS_Leave(athena_js_gil_runtime);
    AthenaJsGilThreadSlot *slot = athena_js_gil_current_slot();
    if (slot)
        slot->suspended = false;

    if (SignalSema(athena_js_gil_semaphore) < 0) {
        dbgprintf("[AthenaCore] Warning: unable to release QuickJS GIL semaphore\n");
    }
}

void athena_js_gil_set_stack_budget(size_t bytes) {
    AthenaJsGilThreadSlot *slot = athena_js_gil_current_slot();

    if (slot)
        slot->stack_budget = bytes;
}

void athena_js_gil_set_runtime(JSRuntime *rt) {
    athena_js_gil_runtime = rt;
    memset(athena_js_gil_slots, 0, sizeof(athena_js_gil_slots));
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
    athena_js_gil_runtime = NULL;
    athena_js_gil_initialized = false;
}
