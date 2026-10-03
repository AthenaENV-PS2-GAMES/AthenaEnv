/*
 * Host implementations of the Athena mutex and thread cores (see the stubs
 * in tests/host/stubs/athena/), plus small helpers shared by the tests.
 * Include once, from the test's main file.
 */
#ifndef ATHENA_HOST_RUNTIME_H
#define ATHENA_HOST_RUNTIME_H

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <kernel.h>
#include <athena/mutex.h>
#include <athena/thread.h>

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void sleep_ms(int ms) { usleep((useconds_t)ms * 1000); }

/* Non-recursive and error-checking: taking a lock twice aborts the test. */
struct AthenaMutex { pthread_mutex_t mutex; };

AthenaMutex *athena_mutex_core_create(void) {
    pthread_mutexattr_t attr;
    AthenaMutex *m = malloc(sizeof(*m));
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(&m->mutex, &attr);
    return m;
}
int athena_mutex_core_lock(AthenaMutex *m) {
    int error = pthread_mutex_lock(&m->mutex);
    if (error) { printf("  FATAL: lock taken twice (%s)\n", strerror(error)); abort(); }
    return 0;
}
int athena_mutex_core_unlock(AthenaMutex *m) {
    int error = pthread_mutex_unlock(&m->mutex);
    if (error) { printf("  FATAL: lock released while not held (%s)\n", strerror(error)); abort(); }
    return 0;
}
int athena_mutex_core_destroy(AthenaMutex *m) {
    pthread_mutex_destroy(&m->mutex);
    free(m);
    return 0;
}

/* Threads run as pthreads; `threads_alive` counts the ones not finished. */
struct AthenaThread { pthread_t id; AthenaThreadFunc func; void *arg; };
static int threads_alive;

static void *host_thread_main(void *p) {
    AthenaThread *t = p;
    t->func(t->arg);
    return NULL;
}
AthenaThread *athena_thread_core_create(const char *name, AthenaThreadFunc func, void *arg,
    size_t stack_size, int priority) {
    AthenaThread *t = calloc(1, sizeof(*t));
    (void)name; (void)stack_size; (void)priority;
    t->func = func;
    t->arg = arg;
    return t;
}
int athena_thread_core_start(AthenaThread *t) {
    __atomic_add_fetch(&threads_alive, 1, __ATOMIC_SEQ_CST);
    if (pthread_create(&t->id, NULL, host_thread_main, t) == 0)
        return 0;
    __atomic_sub_fetch(&threads_alive, 1, __ATOMIC_SEQ_CST);
    return -1;
}
int athena_thread_core_stop(AthenaThread *t) { (void)t; return 0; }
void athena_thread_core_destroy(AthenaThread *t) { free(t); }
int athena_thread_core_stop_requested(void) { return 0; }
void athena_thread_core_worker_finished(AthenaThread *t) {
    (void)t;
    __atomic_sub_fetch(&threads_alive, 1, __ATOMIC_SEQ_CST);
}
int athena_thread_core_wait(AthenaThread *t) { return pthread_join(t->id, NULL); }
int athena_thread_core_get_status(const AthenaThread *t) { (void)t; return THS_DORMANT; }
void athena_thread_core_finalize(AthenaThread *t) { free(t); }

#endif /* ATHENA_HOST_RUNTIME_H */
