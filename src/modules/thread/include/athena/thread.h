#ifndef ATH_NATIVE_THREAD_H
#define ATH_NATIVE_THREAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ATHENA_MAX_TASKS 128
#define ATHENA_THREAD_DEFAULT_STACK_SIZE 16384
#define ATHENA_THREAD_DEFAULT_PRIORITY 16

typedef struct AthenaThread AthenaThread;

typedef struct {
    int id;
    int status;
    size_t stack_size;
    char name[64];
} AthenaTaskInfo;

typedef void (*AthenaThreadFunc)(void *arg);
typedef void (*AthenaThreadOwnerInvalidator)(void *owner);

/* Native thread lifecycle */
AthenaThread *athena_thread_core_create(const char *name, AthenaThreadFunc func, void *arg, size_t stack_size, int priority);
int athena_thread_core_start(AthenaThread *thread);
int athena_thread_core_stop(AthenaThread *thread);
void athena_thread_core_destroy(AthenaThread *thread);
/* Drops the owner without blocking; an active thread is reaped once it exits. */
void athena_thread_core_release(AthenaThread *thread);
int athena_thread_core_is_current(const AthenaThread *thread);
int athena_thread_core_stop_requested(void);
AthenaThread *athena_thread_core_get_by_id(int id);
int athena_thread_core_is_system_id(int id);
int athena_thread_core_wait(AthenaThread *thread);
void athena_thread_core_wait_all(void);
void athena_thread_core_finalize(AthenaThread *thread);
void athena_thread_core_worker_finished(AthenaThread *thread);
void athena_thread_core_set_owner(AthenaThread *thread,
    void *owner, AthenaThreadOwnerInvalidator invalidate);

/* Native thread properties */
int athena_thread_core_get_id(const AthenaThread *thread);
const char *athena_thread_core_get_name(const AthenaThread *thread);
void athena_thread_core_set_name(AthenaThread *thread, const char *name);
int athena_thread_core_get_status(const AthenaThread *thread);

/* Global task manager */
void athena_thread_manager_init(void);
int athena_thread_core_kill_by_id(int id);
int athena_thread_core_get_tasks(AthenaTaskInfo *out_tasks, int max_count);

#endif /* ATH_NATIVE_THREAD_H */
