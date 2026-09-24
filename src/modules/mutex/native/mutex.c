#include <kernel.h>
#include <stdlib.h>

#include <athena/mutex.h>

struct AthenaMutex {
    int semaphore_id;
    int state_semaphore;
    int waiting;
    int held;
    bool closing;
    bool destroy_pending;
};

#ifndef EA_THFIFO
#define EA_THFIFO 0
#endif

static void mutex_state_lock(AthenaMutex *mutex) {
    WaitSema(mutex->state_semaphore);
}

static void mutex_state_unlock(AthenaMutex *mutex) {
    SignalSema(mutex->state_semaphore);
}

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
    ee_sema_t state_config = {
        .max_count = 1,
        .init_count = 1,
        .attr = 0,
        .option = 0
    };
    mutex->state_semaphore = CreateSema(&state_config);
    if (mutex->state_semaphore < 0) {
        DeleteSema(mutex->semaphore_id);
        free(mutex);
        return NULL;
    }
    mutex->waiting = 0;
    mutex->held = 0;
    mutex->closing = false;
    mutex->destroy_pending = false;
    return mutex;
}

int athena_mutex_core_lock(AthenaMutex *mutex) {
    if (!mutex || mutex->semaphore_id < 0) return -1;
    mutex_state_lock(mutex);
    if (mutex->closing) {
        mutex_state_unlock(mutex);
        return -1;
    }
    mutex->waiting++;
    mutex_state_unlock(mutex);
    int result = WaitSema(mutex->semaphore_id);
    mutex_state_lock(mutex);
    mutex->waiting--;
    if (result >= 0) mutex->held++;
    mutex_state_unlock(mutex);
    athena_mutex_core_finalize_deferred(mutex);
    return result;
}

int athena_mutex_core_unlock(AthenaMutex *mutex) {
    if (!mutex || mutex->semaphore_id < 0) return -1;
    int result = SignalSema(mutex->semaphore_id);
    if (result >= 0) {
        mutex_state_lock(mutex);
        if (mutex->held > 0) mutex->held--;
        mutex_state_unlock(mutex);
    }
    athena_mutex_core_finalize_deferred(mutex);
    return result;
}

int athena_mutex_core_destroy(AthenaMutex *mutex) {
    if (!mutex) return -1;
    mutex_state_lock(mutex);
    if (mutex->waiting != 0 || mutex->held != 0 ||
        (mutex->closing && !mutex->destroy_pending)) {
        mutex_state_unlock(mutex);
        return -1;
    }
    mutex->closing = true;
    mutex_state_unlock(mutex);
    if (mutex->semaphore_id >= 0) {
        DeleteSema(mutex->semaphore_id);
        mutex->semaphore_id = -1;
    }
    DeleteSema(mutex->state_semaphore);
    mutex->state_semaphore = -1;
    free(mutex);
    return 0;
}

void athena_mutex_core_request_destroy(AthenaMutex *mutex) {
    if (!mutex) return;
    mutex_state_lock(mutex);
    mutex->destroy_pending = true;
    mutex_state_unlock(mutex);
    athena_mutex_core_finalize_deferred(mutex);
}

void athena_mutex_core_finalize_deferred(AthenaMutex *mutex) {
    if (!mutex)
        return;
    mutex_state_lock(mutex);
    bool can_destroy = mutex->destroy_pending &&
        mutex->waiting == 0 && mutex->held == 0 && !mutex->closing;
    if (can_destroy) mutex->closing = true;
    mutex_state_unlock(mutex);
    if (can_destroy) {
        athena_mutex_core_destroy(mutex);
    }
}
