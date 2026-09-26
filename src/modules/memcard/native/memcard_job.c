#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <kernel.h>
#include <delaythread.h>

#include <athena/memcard.h>
#include <athena/mutex.h>
#include <athena/thread.h>

/* The recursive remove keeps a path buffer per directory level. */
#define JOB_STACK_SIZE (32 * 1024)
#define JOB_WAIT_POLL_US 1000
/*
 * The worker sleeps while the IOP talks to the card and barely uses the
 * CPU, so it runs above the script thread to keep the card busy.
 */
#define JOB_PRIORITY (ATHENA_THREAD_DEFAULT_PRIORITY - 1)

typedef enum JobKind {
    JOB_READ,
    JOB_WRITE,
    JOB_REMOVE,
    JOB_FORMAT,
} JobKind;

struct AthenaMemcardJob {
    AthenaThread *thread;
    AthenaMutex *mutex;
    /* Immutable while the worker runs. */
    JobKind kind;
    int port;
    char *path;
    void *input;
    size_t input_size;
    bool recursive;
    bool create_dirs;
    bool atomic;
    /* Protected by `mutex`. */
    AthenaMemcardJobStatus status;
    void *data;
    size_t size;
    bool cancel;
};

static bool job_lock(AthenaMemcardJob *job) {
    return athena_mutex_core_lock(job->mutex) >= 0;
}

static void job_unlock(AthenaMemcardJob *job) {
    athena_mutex_core_unlock(job->mutex);
}

/* Worker side: records the progress; < 0 when the script or the runtime asked to stop. */
static int job_progress(uint64_t done, uint64_t total, void *user) {
    AthenaMemcardJob *job = user;
    bool cancel = true;

    if (job_lock(job)) {
        job->status.bytes_done = done;
        if (total)
            job->status.bytes_total = total;
        cancel = job->cancel;
        job_unlock(job);
    }
    return cancel || athena_thread_core_stop_requested() ? -1 : 0;
}

static void job_worker(void *arg) {
    AthenaMemcardJob *job = arg;
    AthenaMemcardWriteOptions options = {
        .create_dirs = job->create_dirs,
        .atomic = job->atomic,
        .progress = job_progress,
        .user = job,
    };
    void *data = NULL;
    size_t size = 0;
    int ret;

    switch (job->kind) {
    case JOB_READ:
        ret = athena_memcard_read_file(job->port, job->path, &data, &size, job_progress, job);
        break;
    case JOB_WRITE:
        ret = athena_memcard_write_file(job->port, job->path, job->input, job->input_size, &options);
        break;
    case JOB_REMOVE:
        ret = athena_memcard_remove(job->port, job->path, job->recursive, job_progress, job);
        break;
    default:
        ret = athena_memcard_format(job->port);
        break;
    }

    if (job_lock(job)) {
        AthenaMemcardJobStatus *status = &job->status;
        status->result = ret;
        if (ret >= 0) {
            status->state = ATHENA_MEMCARD_JOB_DONE;
            if (job->kind == JOB_READ) {
                status->bytes_done = status->bytes_total = size;
                job->data = data;
                job->size = size;
                data = NULL;
            }
        } else {
            status->state = ret == ATHENA_MEMCARD_ERR_CANCELLED ?
                ATHENA_MEMCARD_JOB_CANCELLED : ATHENA_MEMCARD_JOB_FAILED;
        }
        job_unlock(job);
    }
    free(data);

    athena_thread_core_worker_finished(job->thread);
    ExitThread();
}

static void job_free(AthenaMemcardJob *job) {
    if (job->mutex)
        athena_mutex_core_destroy(job->mutex);
    free(job->path);
    free(job->input);
    free(job->data);
    free(job);
}

static AthenaMemcardJob *job_new(JobKind kind, int port, const char *path) {
    AthenaMemcardJob *job;

    if (port < 0 || port >= ATHENA_MEMCARD_PORTS)
        return NULL;
    job = calloc(1, sizeof(*job));
    if (!job)
        return NULL;
    job->kind = kind;
    job->port = port;
    job->mutex = athena_mutex_core_create();
    if (path)
        job->path = strdup(path);
    if (!job->mutex || (path && !job->path)) {
        job_free(job);
        return NULL;
    }
    return job;
}

static AthenaMemcardJob *job_start(AthenaMemcardJob *job) {
    if (!job)
        return NULL;
    job->thread = athena_thread_core_create("Memcard job", job_worker, job,
        JOB_STACK_SIZE, JOB_PRIORITY);
    if (!job->thread) {
        job_free(job);
        return NULL;
    }
    if (athena_thread_core_start(job->thread) < 0) {
        /* Never started, so destroy() also finalizes the thread. */
        athena_thread_core_destroy(job->thread);
        job_free(job);
        return NULL;
    }
    return job;
}

AthenaMemcardJob *athena_memcard_job_read(int port, const char *path) {
    return path ? job_start(job_new(JOB_READ, port, path)) : NULL;
}

AthenaMemcardJob *athena_memcard_job_write(int port, const char *path, const void *data,
    size_t size, const AthenaMemcardWriteOptions *options) {
    AthenaMemcardJob *job;

    if (!path || (!data && size))
        return NULL;
    job = job_new(JOB_WRITE, port, path);
    if (!job)
        return NULL;
    /* One byte at least, so an empty write still owns a buffer. */
    job->input = malloc(size ? size : 1);
    if (!job->input) {
        job_free(job);
        return NULL;
    }
    if (size)
        memcpy(job->input, data, size);
    job->input_size = size;
    job->status.bytes_total = size;
    if (options) {
        job->create_dirs = options->create_dirs;
        job->atomic = options->atomic;
    }
    return job_start(job);
}

AthenaMemcardJob *athena_memcard_job_remove(int port, const char *path, bool recursive) {
    AthenaMemcardJob *job = path ? job_new(JOB_REMOVE, port, path) : NULL;

    if (job)
        job->recursive = recursive;
    return job_start(job);
}

AthenaMemcardJob *athena_memcard_job_format(int port) {
    return job_start(job_new(JOB_FORMAT, port, NULL));
}

void athena_memcard_job_status(AthenaMemcardJob *job, AthenaMemcardJobStatus *out) {
    if (!job_lock(job)) {
        memset(out, 0, sizeof(*out));
        out->state = ATHENA_MEMCARD_JOB_FAILED;
        out->result = ATHENA_MEMCARD_ERR_IO;
        return;
    }
    *out = job->status;
    job_unlock(job);
}

void athena_memcard_job_cancel(AthenaMemcardJob *job) {
    if (job_lock(job)) {
        job->cancel = true;
        job_unlock(job);
    }
}

bool athena_memcard_job_wait(AthenaMemcardJob *job, int timeout_ms) {
    clock_t start = clock();

    for (;;) {
        AthenaMemcardJobStatus status;
        athena_memcard_job_status(job, &status);
        if (status.state != ATHENA_MEMCARD_JOB_RUNNING)
            return true;
        if (timeout_ms >= 0 &&
            (clock() - start) * 1000 / CLOCKS_PER_SEC >= (clock_t)timeout_ms)
            return false;
        DelayThread(JOB_WAIT_POLL_US);
    }
}

int athena_memcard_job_take_data(AthenaMemcardJob *job, void **data, size_t *size) {
    int ret = ATHENA_MEMCARD_ERR_ARGUMENT;

    *data = NULL;
    *size = 0;
    if (!job_lock(job))
        return ATHENA_MEMCARD_ERR_IO;
    if (job->kind == JOB_READ && job->status.state == ATHENA_MEMCARD_JOB_DONE) {
        *data = job->data;
        *size = job->size;
        job->data = NULL;
        job->size = 0;
        ret = ATHENA_MEMCARD_OK;
    }
    job_unlock(job);
    return ret;
}

/* Joins the worker; it may already have exited on its own. */
void athena_memcard_job_destroy(AthenaMemcardJob *job) {
    if (!job)
        return;
    athena_memcard_job_cancel(job);
    athena_thread_core_stop(job->thread);
    athena_thread_core_wait(job->thread);
    /*
     * worker_finished() signals just before ExitThread(); give the worker
     * time to become dormant before its stack is released.
     */
    for (int attempts = 0; attempts < 100 &&
        athena_thread_core_get_status(job->thread) != THS_DORMANT; ++attempts)
        DelayThread(100);
    athena_thread_core_finalize(job->thread);
    job_free(job);
}
