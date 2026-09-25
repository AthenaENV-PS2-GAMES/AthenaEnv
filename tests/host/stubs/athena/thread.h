/* Host stub of src/modules/thread/include/athena/thread.h (same signatures). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#define ATHENA_THREAD_DEFAULT_PRIORITY 16
typedef struct AthenaThread AthenaThread;
typedef void (*AthenaThreadFunc)(void *arg);
AthenaThread *athena_thread_core_create(const char *name, AthenaThreadFunc func, void *arg, size_t stack_size, int priority);
int athena_thread_core_start(AthenaThread *thread);
int athena_thread_core_stop(AthenaThread *thread);
void athena_thread_core_destroy(AthenaThread *thread);
int athena_thread_core_stop_requested(void);
int athena_thread_core_wait(AthenaThread *thread);
void athena_thread_core_finalize(AthenaThread *thread);
void athena_thread_core_worker_finished(AthenaThread *thread);
int athena_thread_core_get_status(const AthenaThread *thread);
