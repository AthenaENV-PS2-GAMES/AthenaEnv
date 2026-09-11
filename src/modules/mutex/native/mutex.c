#include <kernel.h>
#include <stdlib.h>

#include "mutex.h"

struct AthenaMutex {
    int semaphore_id;
};

#ifndef EA_THFIFO
#define EA_THFIFO 0
#endif

AthenaMutex *athena_mutex_core_create(void) {
    AthenaMutex *mutex = malloc(sizeof(*mutex));
    if (!mutex) return NULL;

    ee_sema_t config = {
        .max_count = 1,
        .init_count = 1,
        .attr = EA_THFIFO,
        .option = 0
    };

    mutex->semaphore_id = CreateSema(&config);
    if (mutex->semaphore_id < 0) {
        free(mutex);
        return NULL;
    }
    return mutex;
}

int athena_mutex_core_lock(AthenaMutex *mutex) {
    return WaitSema(mutex->semaphore_id);
}

int athena_mutex_core_unlock(AthenaMutex *mutex) {
    return SignalSema(mutex->semaphore_id);
}

void athena_mutex_core_destroy(AthenaMutex *mutex) {
    if (!mutex) return;
    if (mutex->semaphore_id >= 0) {
        DeleteSema(mutex->semaphore_id);
        mutex->semaphore_id = -1;
    }
    free(mutex);
}
