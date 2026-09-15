#include <kernel.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include <dbgprintf.h>

#include "thread.h"

extern void *_gp;

typedef struct {
    int id;
    int status;
    size_t stack_size;
    char name[64];
    bool active;
    bool is_system;
    struct AthenaThread *thread;
} AthenaTaskRecord;

static AthenaTaskRecord s_tasks[ATHENA_MAX_TASKS];
static bool s_manager_initialized = false;

struct AthenaThread {
    int id;
    int priority;
    size_t stack_size;
    void *stack;
    char name[64];
    AthenaThreadFunc func;
    void *arg;
    atomic_bool stop_requested;
    atomic_bool running;
    atomic_bool started;
    int completion_semaphore;
};

void athena_thread_manager_init(void) {
    if (s_manager_initialized) return;
    s_manager_initialized = true;

    for (int i = 0; i < ATHENA_MAX_TASKS; i++) {
        s_tasks[i].id = -1;
        s_tasks[i].status = -1;
        s_tasks[i].stack_size = 0;
        s_tasks[i].name[0] = '\0';
        s_tasks[i].active = false;
        s_tasks[i].is_system = false;
        s_tasks[i].thread = NULL;
    }

    /* Discover threads already running during boot */
    ee_thread_status_t info;
    for (int tid = 1; tid <= 16; tid++) {
        memset(&info, 0, sizeof(info));
        if (ReferThreadStatus(tid, &info) >= 0 && info.status != 0) {
            const char *desc = "System: Thread";
            if (tid == 1) {
                desc = "AthenaEnv: JavaScript Runtime";
            } else if (tid == 2) {
                desc = "Kernel: Thread manager";
            }

            for (int i = 0; i < ATHENA_MAX_TASKS; i++) {
                if (!s_tasks[i].active) {
                    s_tasks[i].id = tid;
                    s_tasks[i].status = info.status;
                    s_tasks[i].stack_size = info.stack_size;
                    snprintf(s_tasks[i].name, sizeof(s_tasks[i].name), "%s", desc);
                    s_tasks[i].active = true;
                    s_tasks[i].is_system = true;
                    break;
                }
            }
        }
    }

    dbgprintf("[AthenaCore] Thread manager initialized\n");
}

AthenaThread *athena_thread_core_create(const char *name, AthenaThreadFunc func, void *arg, size_t stack_size, int priority) {
    if (!s_manager_initialized) {
        athena_thread_manager_init();
    }

    if (stack_size == 0) stack_size = ATHENA_THREAD_DEFAULT_STACK_SIZE;
    if (priority <= 0 || priority > 127) priority = ATHENA_THREAD_DEFAULT_PRIORITY;

    AthenaThread *thread = (AthenaThread *)malloc(sizeof(AthenaThread));
    if (!thread) return NULL;

    memset(thread, 0, sizeof(*thread));
    thread->stack_size = stack_size;
    thread->priority = priority;
    thread->func = func;
    thread->arg = arg;
    atomic_init(&thread->stop_requested, false);
    atomic_init(&thread->running, false);
    atomic_init(&thread->started, false);

    if (name && name[0] != '\0') {
        snprintf(thread->name, sizeof(thread->name), "%s", name);
    } else {
        snprintf(thread->name, sizeof(thread->name), "Athena: Worker thread");
    }

    thread->stack = memalign(128, stack_size);
    if (!thread->stack) {
        free(thread);
        return NULL;
    }

    ee_thread_t thread_param;
    memset(&thread_param, 0, sizeof(thread_param));
    thread_param.gp_reg = &_gp;
    thread_param.func = (void *)func;
    thread_param.stack_size = stack_size;
    thread_param.stack = thread->stack;
    thread_param.initial_priority = priority;
    thread_param.option = (u32)thread->name;

    thread->id = CreateThread(&thread_param);
    if (thread->id < 0) {
        free(thread->stack);
        free(thread);
        return NULL;
    }

    ee_sema_t completion = {
        .max_count = 1,
        .init_count = 0,
        .attr = 0,
        .option = 0
    };
    thread->completion_semaphore = CreateSema(&completion);
    if (thread->completion_semaphore < 0) {
        DeleteThread(thread->id);
        free(thread->stack);
        free(thread);
        return NULL;
    }

    /* Register thread in task manager table */
    for (int i = 0; i < ATHENA_MAX_TASKS; i++) {
        if (!s_tasks[i].active) {
            s_tasks[i].id = thread->id;
            s_tasks[i].status = 0;
            s_tasks[i].stack_size = stack_size;
            snprintf(s_tasks[i].name, sizeof(s_tasks[i].name), "%s", thread->name);
            s_tasks[i].active = true;
            s_tasks[i].is_system = false;
            s_tasks[i].thread = thread;
            break;
        }
    }

    dbgprintf("[AthenaCore] Thread '%s' (ID %d) created\n", thread->name, thread->id);
    return thread;
}

int athena_thread_core_start(AthenaThread *thread) {
    if (!thread || thread->id < 0) return -1;
    if (atomic_load_explicit(&thread->started, memory_order_acquire))
        return -1;
    atomic_store_explicit(&thread->running, true, memory_order_release);
    atomic_store_explicit(&thread->started, true, memory_order_release);
    int rc = StartThread(thread->id, thread->arg);
    if (rc < 0) {
        atomic_store_explicit(&thread->started, false, memory_order_release);
        atomic_store_explicit(&thread->running, false, memory_order_release);
    }
    return rc;
}

int athena_thread_core_stop(AthenaThread *thread) {
    if (!thread || thread->id < 0) return -1;
    atomic_store_explicit(&thread->stop_requested, true, memory_order_release);
    return 0;
}

void athena_thread_core_destroy(AthenaThread *thread) {
    if (!thread) return;
    atomic_store_explicit(&thread->stop_requested, true, memory_order_release);
    if (!atomic_load_explicit(&thread->running, memory_order_acquire))
        athena_thread_core_finalize(thread);
}

int athena_thread_core_is_current(const AthenaThread *thread) {
    return thread && thread->id == GetThreadId();
}

int athena_thread_core_stop_requested(void) {
    int id = GetThreadId();
    for (int i = 0; i < ATHENA_MAX_TASKS; i++) {
        if (s_tasks[i].active && s_tasks[i].id == id && s_tasks[i].thread) {
            return atomic_load_explicit(&s_tasks[i].thread->stop_requested,
                                        memory_order_acquire);
        }

    }
    return 0;
}

AthenaThread *athena_thread_core_get_by_id(int id) {
    if (id < 0) return NULL;
    for (int i = 0; i < ATHENA_MAX_TASKS; i++) {
        if (s_tasks[i].active && s_tasks[i].id == id)
            return s_tasks[i].thread;
    }
    return NULL;
}

int athena_thread_core_wait(AthenaThread *thread) {
    if (!thread || athena_thread_core_is_current(thread)) return -1;
    if (!atomic_load_explicit(&thread->started, memory_order_acquire))
        return 0;
    if (atomic_load_explicit(&thread->running, memory_order_acquire))
        return WaitSema(thread->completion_semaphore);
    return 0;
}

void athena_thread_core_finalize(AthenaThread *thread) {
    if (!thread || athena_thread_core_is_current(thread) ||
        atomic_load_explicit(&thread->running, memory_order_acquire))
        return;

    if (thread->id >= 0) {
        DeleteThread(thread->id);
        for (int i = 0; i < ATHENA_MAX_TASKS; i++) {
            if (s_tasks[i].active && s_tasks[i].id == thread->id) {
                s_tasks[i].id = -1;
                s_tasks[i].active = false;
                s_tasks[i].name[0] = '\0';
                s_tasks[i].thread = NULL;
                break;
            }
        }
        thread->id = -1;
    }
    if (thread->completion_semaphore >= 0) {
        DeleteSema(thread->completion_semaphore);
        thread->completion_semaphore = -1;
    }
    free(thread->stack);
    thread->stack = NULL;
    free(thread);
}

void athena_thread_core_wait_all(void) {
    for (int i = 0; i < ATHENA_MAX_TASKS; i++) {
        if (s_tasks[i].active && s_tasks[i].thread)
            atomic_store_explicit(&s_tasks[i].thread->stop_requested, true,
                                  memory_order_release);
    }

    for (int i = 0; i < ATHENA_MAX_TASKS; i++) {
        if (s_tasks[i].active && s_tasks[i].thread)
            athena_thread_core_wait(s_tasks[i].thread);
    }
}

void athena_thread_core_worker_finished(AthenaThread *thread) {
    if (!thread) return;
    atomic_store_explicit(&thread->running, false, memory_order_release);
    SignalSema(thread->completion_semaphore);
}

int athena_thread_core_get_id(const AthenaThread *thread) {
    return thread ? thread->id : -1;
}

const char *athena_thread_core_get_name(const AthenaThread *thread) {
    return thread ? thread->name : "";
}

void athena_thread_core_set_name(AthenaThread *thread, const char *name) {
    if (!thread || !name) return;

    snprintf(thread->name, sizeof(thread->name), "%s", name);

    for (int i = 0; i < ATHENA_MAX_TASKS; i++) {
        if (s_tasks[i].active && s_tasks[i].id == thread->id) {
            snprintf(s_tasks[i].name, sizeof(s_tasks[i].name), "%s", thread->name);
            break;
        }
    }
}

int athena_thread_core_get_status(const AthenaThread *thread) {
    if (!thread || thread->id < 0) return -1;

    ee_thread_status_t info;
    memset(&info, 0, sizeof(info));
    if (ReferThreadStatus(thread->id, &info) >= 0) {
        return info.status;
    }
    return -1;
}

int athena_thread_core_kill_by_id(int id) {
    if (id < 0) return -1;

    for (int i = 0; i < ATHENA_MAX_TASKS; i++) {
        if (s_tasks[i].active && s_tasks[i].id == id) {
            /* TODO(gil-coverage): item 3.4 must reject system threads here. */
            if (s_tasks[i].thread) {
                atomic_store_explicit(&s_tasks[i].thread->stop_requested, true,
                                      memory_order_release);
                return 0;
            }
            return -1;
        }
    }
    return -1;
}

int athena_thread_core_get_tasks(AthenaTaskInfo *out_tasks, int max_count) {
    if (!s_manager_initialized) {
        athena_thread_manager_init();
    }

    int count = 0;
    ee_thread_status_t info;

    for (int i = 0; i < ATHENA_MAX_TASKS && count < max_count; i++) {
        if (s_tasks[i].active) {
            memset(&info, 0, sizeof(info));
            if (ReferThreadStatus(s_tasks[i].id, &info) >= 0) {
                s_tasks[i].status = info.status;
                if (info.stack_size > 0 && s_tasks[i].stack_size == 0) {
                    s_tasks[i].stack_size = info.stack_size;
                }
            }

            out_tasks[count].id = s_tasks[i].id;
            out_tasks[count].status = s_tasks[i].status;
            out_tasks[count].stack_size = s_tasks[i].stack_size;
            snprintf(out_tasks[count].name, sizeof(out_tasks[count].name), "%s", s_tasks[i].name);
            count++;
        }
    }

    return count;
}
