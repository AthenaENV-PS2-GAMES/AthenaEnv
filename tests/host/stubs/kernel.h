/* Host stub of the PS2SDK EE kernel calls the tested modules use. */
#pragma once
#include <pthread.h>
#include <semaphore.h>
typedef struct { int current_priority; } ee_thread_status_t;
#define THS_DORMANT 0x10
static inline int GetThreadId(void) { return 1; }
/* The script thread runs at the default priority. */
static inline int ReferThreadStatus(int id, ee_thread_status_t *s) { (void)id; s->current_priority = 16; return 0; }
static inline int ChangeThreadPriority(int id, int priority) { (void)id; (void)priority; return 0; }
static inline void ExitThread(void) { pthread_exit(0); }
static inline void SyncDCache(void *start, void *end) { (void)start; (void)end; }

/* Counting semaphores, backed by POSIX ones; ids are per translation unit. */
typedef struct {
    int count;
    int max_count;
    int init_count;
    int wait_threads;
    unsigned int attr;
    unsigned int option;
} ee_sema_t;

#define HOST_SEMAS 64
static sem_t host_semas[HOST_SEMAS];
static int host_sema_used[HOST_SEMAS];

static inline int CreateSema(ee_sema_t *config) {
    for (int i = 0; i < HOST_SEMAS; i++) {
        if (!host_sema_used[i] && sem_init(&host_semas[i], 0, (unsigned)config->init_count) == 0) {
            host_sema_used[i] = 1;
            return i;
        }
    }
    return -1;
}
static inline int DeleteSema(int id) {
    if (id < 0 || id >= HOST_SEMAS || !host_sema_used[id])
        return -1;
    sem_destroy(&host_semas[id]);
    host_sema_used[id] = 0;
    return 0;
}
static inline int WaitSema(int id) {
    while (sem_wait(&host_semas[id]) != 0)
        ;
    return id;
}
static inline int SignalSema(int id) { return sem_post(&host_semas[id]) == 0 ? id : -1; }
static inline int PollSema(int id) { return sem_trywait(&host_semas[id]) == 0 ? id : -1; }
