#ifndef ATHENA_JOB_H
#define ATHENA_JOB_H

#include <stdbool.h>

#include <athena/thread.h>

/*
 * Background jobs: work that runs on a worker thread while the script
 * thread keeps drawing frames, such as reading a file, writing a save or
 * extracting an archive. Modules describe a kind of job once (AthenaJobType)
 * and submit jobs with their own data; the core runs them on a small shared
 * pool of workers and keeps their state:
 *
 *     static int read_run(AthenaJob *job, void *data) {
 *         ReadJob *read = data;              // module data, owned by the job
 *         ...                                // athena_job_should_stop() to cancel
 *         return 0;                          // or a negative module error
 *     }
 *     static const AthenaJobType read_type = {
 *         "Read", read_run, read_free, MY_ERR_CANCELLED, ATHENA_JOB_PRIORITY_IO,
 *     };
 *     AthenaJob *job = athena_job_submit(&read_type, data);
 *     ... athena_job_state(job, &result) != ATHENA_JOB_RUNNING ...
 *     athena_job_release(job);
 *
 * Fields of the data that the worker updates while the script reads them
 * (progress) are accessed under athena_job_lock(). The pool stops with the
 * runtime (athena_thread_core_wait_all): queued jobs end cancelled.
 */

/* Workers of the pool, started on demand. I/O is serialized by the IOP, so
 * two are enough to keep a short job (a sound effect) from waiting behind a
 * long one (an archive). */
#define ATHENA_JOB_WORKERS 2
#define ATHENA_JOB_STACK_SIZE (32 * 1024)

/* Mostly waiting for the IOP (memory card, disc, USB): above the script thread. */
#define ATHENA_JOB_PRIORITY_IO (ATHENA_THREAD_DEFAULT_PRIORITY - 1)
/* Using the CPU (decompression, decoding): below it, so frames keep coming. */
#define ATHENA_JOB_PRIORITY_CPU (ATHENA_THREAD_DEFAULT_PRIORITY + 1)

typedef enum {
    ATHENA_JOB_RUNNING,     /* queued or running */
    ATHENA_JOB_DONE,
    ATHENA_JOB_FAILED,
    ATHENA_JOB_CANCELLED,
} AthenaJobState;

typedef struct AthenaJob AthenaJob;

typedef struct {
    /* For diagnostics. */
    const char *name;
    /* Runs on a worker: >= 0 when done, a negative module error otherwise. */
    int (*run)(AthenaJob *job, void *data);
    /* Frees the data once neither the worker nor the handle uses it; any thread. Optional. */
    void (*release)(void *data);
    /* What run() returns when it stopped because athena_job_should_stop(). */
    int cancelled_result;
    /* ATHENA_JOB_PRIORITY_IO or ATHENA_JOB_PRIORITY_CPU. */
    int priority;
} AthenaJobType;

/*
 * Queues a job and returns its handle, or NULL (no memory, no worker could be
 * started, runtime stopping); `data` is released on failure too.
 */
AthenaJob *athena_job_submit(const AthenaJobType *type, void *data);

/* State and, once settled, the value run() returned (or cancelled_result). */
AthenaJobState athena_job_state(AthenaJob *job, int *result);

/* The data given to athena_job_submit(). */
void *athena_job_data(AthenaJob *job);

/* Guards the data fields the worker changes while the job runs. Not recursive. */
void athena_job_lock(AthenaJob *job);
void athena_job_unlock(AthenaJob *job);

/* Asks the job to stop; a queued job ends cancelled without running. */
void athena_job_cancel(AthenaJob *job);

/* Worker side: true once the job was cancelled or the runtime is stopping. */
bool athena_job_should_stop(AthenaJob *job);

/* Blocks until the job settles or `timeout_ms` passes (< 0: no limit); true when settled. */
bool athena_job_wait(AthenaJob *job, int timeout_ms);

/*
 * Drops the handle: cancels the job if it is still running and frees it once
 * the worker is done with it. Never blocks.
 */
void athena_job_release(AthenaJob *job);

/* Runtime shutdown (athena_thread_core_wait_all): cancels queued jobs, stops and joins the workers. */
void athena_job_pool_stop(void);

#endif /* ATHENA_JOB_H */
