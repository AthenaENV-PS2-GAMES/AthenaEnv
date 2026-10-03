#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <kernel.h>
#include <delaythread.h>

#include <athena/job.h>

#define JOB_WAIT_POLL_US 1000
/* More than the jobs that can ever be queued at once. */
#define JOB_SEMA_MAX 4096

struct AthenaJob {
    const AthenaJobType *type;
    void *data;
    /* Under the pool lock. */
    AthenaJobState state;
    int result;
    bool cancel;
    bool queued;
    /* The handle, and the pool until a worker is done with the job. */
    int refs;
    struct AthenaJob *next;
};

/*
 * Jobs wait in a FIFO; `sema` counts wake-ups (one per queued job, one per
 * worker to stop). Submitting, cancelling and releasing happen on the script
 * thread; workers only take jobs and settle them.
 */
static struct {
    int lock;               /* binary semaphore */
    int sema;
    AthenaJob *head, *tail;
    int queued;
    int idle;
    int alive;
    bool stopping;
    AthenaThread *workers[ATHENA_JOB_WORKERS];
} pool = { .lock = -1, .sema = -1 };

static void pool_lock(void) {
    WaitSema(pool.lock);
}

static void pool_unlock(void) {
    SignalSema(pool.lock);
}

static bool pool_init(void) {
    ee_sema_t lock = { .init_count = 1, .max_count = 1, .option = 0 };
    ee_sema_t work = { .init_count = 0, .max_count = JOB_SEMA_MAX, .option = 0 };

    if (pool.lock >= 0)
        return true;
    pool.lock = CreateSema(&lock);
    pool.sema = pool.lock >= 0 ? CreateSema(&work) : -1;
    if (pool.sema < 0) {
        if (pool.lock >= 0)
            DeleteSema(pool.lock);
        pool.lock = -1;
        return false;
    }
    return true;
}

static void pool_job_free(AthenaJob *job) {
    if (job->type->release)
        job->type->release(job->data);
    free(job);
}

/* Drops a reference under the lock; true when the caller must free the job after unlocking. */
static bool pool_job_unref_locked(AthenaJob *job) {
    return --job->refs == 0;
}

static void pool_job_settle_locked(AthenaJob *job, int result) {
    job->result = result;
    job->state = result >= 0 ? ATHENA_JOB_DONE :
        result == job->type->cancelled_result ? ATHENA_JOB_CANCELLED : ATHENA_JOB_FAILED;
}

/* Takes a job out of the queue if it is still there; the pool's reference goes with it. */
static bool pool_job_dequeue_locked(AthenaJob *job) {
    AthenaJob **link = &pool.head, *previous = NULL;

    if (!job->queued)
        return false;
    while (*link && *link != job) {
        previous = *link;
        link = &(*link)->next;
    }
    if (!*link)
        return false;
    *link = job->next;
    if (pool.tail == job)
        pool.tail = previous;
    job->next = NULL;
    job->queued = false;
    pool.queued--;
    return true;
}

static AthenaJob *pool_job_pop_locked(void) {
    AthenaJob *job = pool.head;

    if (job) {
        pool.head = job->next;
        if (!pool.head)
            pool.tail = NULL;
        job->next = NULL;
        job->queued = false;
        pool.queued--;
    }
    return job;
}

static void pool_job_worker(void *arg) {
    int slot = (int)(intptr_t)arg;

    for (;;) {
        AthenaJob *job;
        bool release;
        int result;

        WaitSema(pool.sema);
        pool_lock();
        if (pool.stopping) {
            pool_unlock();
            break;
        }
        job = pool_job_pop_locked();
        if (!job) {
            /* Its job was cancelled or released while queued. */
            pool_unlock();
            continue;
        }
        pool.idle--;
        if (job->cancel) {
            pool_job_settle_locked(job, job->type->cancelled_result);
            release = pool_job_unref_locked(job);
            pool.idle++;
            pool_unlock();
            if (release)
                pool_job_free(job);
            continue;
        }
        pool_unlock();

        ChangeThreadPriority(GetThreadId(), job->type->priority);
        result = job->type->run(job, job->data);

        pool_lock();
        pool_job_settle_locked(job, result);
        release = pool_job_unref_locked(job);
        pool.idle++;
        pool_unlock();
        if (release)
            pool_job_free(job);
    }

    athena_thread_core_worker_finished(pool.workers[slot]);
    ExitThread();
}

/* Starts one more worker; under the lock. */
static void pool_spawn_locked(void) {
    for (int slot = 0; slot < ATHENA_JOB_WORKERS; slot++) {
        AthenaThread *thread;

        if (pool.workers[slot])
            continue;
        thread = athena_thread_core_create("Athena job", pool_job_worker, (void *)(intptr_t)slot,
            ATHENA_JOB_STACK_SIZE, ATHENA_JOB_PRIORITY_CPU);
        if (!thread)
            return;
        pool.workers[slot] = thread;
        if (athena_thread_core_start(thread) < 0) {
            /* Never started, so destroy() also finalizes it. */
            athena_thread_core_destroy(thread);
            pool.workers[slot] = NULL;
            return;
        }
        pool.alive++;
        pool.idle++;
        return;
    }
}

AthenaJob *athena_job_submit(const AthenaJobType *type, void *data) {
    AthenaJob *job = NULL;

    if (!type || !type->run || !pool_init())
        goto fail;
    job = calloc(1, sizeof(*job));
    if (!job)
        goto fail;
    job->type = type;
    job->data = data;
    job->state = ATHENA_JOB_RUNNING;
    job->refs = 2;

    pool_lock();
    if (pool.stopping) {
        pool_unlock();
        goto fail;
    }
    if (pool.tail)
        pool.tail->next = job;
    else
        pool.head = job;
    pool.tail = job;
    job->queued = true;
    pool.queued++;
    /* One more worker when every running one is busy. */
    if (pool.alive < ATHENA_JOB_WORKERS && pool.idle < pool.queued)
        pool_spawn_locked();
    if (pool.alive == 0) {
        pool_job_dequeue_locked(job);
        pool_unlock();
        goto fail;
    }
    pool_unlock();
    SignalSema(pool.sema);
    return job;

fail:
    free(job);
    if (type && type->release)
        type->release(data);
    return NULL;
}

AthenaJobState athena_job_state(AthenaJob *job, int *result) {
    AthenaJobState state;

    pool_lock();
    state = job->state;
    if (result)
        *result = job->result;
    pool_unlock();
    return state;
}

void *athena_job_data(AthenaJob *job) {
    return job->data;
}

void athena_job_lock(AthenaJob *job) {
    (void)job;
    pool_lock();
}

void athena_job_unlock(AthenaJob *job) {
    (void)job;
    pool_unlock();
}

void athena_job_cancel(AthenaJob *job) {
    bool release = false;

    pool_lock();
    job->cancel = true;
    /* Not started yet: it ends now instead of when a worker gets to it. */
    if (pool_job_dequeue_locked(job)) {
        pool_job_settle_locked(job, job->type->cancelled_result);
        release = pool_job_unref_locked(job);
    }
    pool_unlock();
    if (release)
        pool_job_free(job);
}

bool athena_job_should_stop(AthenaJob *job) {
    bool stop;

    pool_lock();
    stop = job->cancel || pool.stopping;
    pool_unlock();
    return stop || athena_thread_core_stop_requested();
}

bool athena_job_wait(AthenaJob *job, int timeout_ms) {
    clock_t start = clock();

    while (athena_job_state(job, NULL) == ATHENA_JOB_RUNNING) {
        if (timeout_ms >= 0 &&
            (clock() - start) * 1000 / CLOCKS_PER_SEC >= (clock_t)timeout_ms)
            return false;
        DelayThread(JOB_WAIT_POLL_US);
    }
    return true;
}

void athena_job_release(AthenaJob *job) {
    bool release;

    if (!job)
        return;
    athena_job_cancel(job);
    pool_lock();
    release = pool_job_unref_locked(job);
    pool_unlock();
    if (release)
        pool_job_free(job);
}

void athena_job_pool_stop(void) {
    AthenaJob *cancelled = NULL, *job;
    int workers;

    if (pool.lock < 0)
        return;
    pool_lock();
    pool.stopping = true;
    while ((job = pool_job_pop_locked())) {
        pool_job_settle_locked(job, job->type->cancelled_result);
        if (pool_job_unref_locked(job)) {
            job->next = cancelled;
            cancelled = job;
        }
    }
    workers = pool.alive;
    pool_unlock();

    while ((job = cancelled)) {
        cancelled = job->next;
        pool_job_free(job);
    }
    for (int i = 0; i < workers; i++)
        SignalSema(pool.sema);
    for (int slot = 0; slot < ATHENA_JOB_WORKERS; slot++) {
        AthenaThread *thread = pool.workers[slot];
        if (!thread)
            continue;
        athena_thread_core_wait(thread);
        /* worker_finished() is signalled just before ExitThread(). */
        for (int attempts = 0; attempts < 100 &&
            athena_thread_core_get_status(thread) != THS_DORMANT; attempts++)
            DelayThread(100);
        athena_thread_core_finalize(thread);
        pool.workers[slot] = NULL;
    }

    pool_lock();
    while (PollSema(pool.sema) >= 0)
        ;
    pool.alive = 0;
    pool.idle = 0;
    pool.stopping = false;
    pool_unlock();
}
