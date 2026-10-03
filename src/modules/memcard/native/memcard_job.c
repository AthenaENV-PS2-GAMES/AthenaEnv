#include <stdlib.h>
#include <string.h>

#include <athena/memcard.h>
#include <athena/job.h>

/*
 * Memory card jobs run on the shared job pool (athena/job.h). An
 * AthenaMemcardJob is the pool's AthenaJob; its data is a McJob.
 */

typedef enum JobKind {
    JOB_READ,
    JOB_WRITE,
    JOB_REMOVE,
    JOB_FORMAT,
} JobKind;

typedef struct {
    /* Immutable while the job runs. */
    JobKind kind;
    int port;
    char *path;
    void *input;
    size_t input_size;
    bool recursive;
    bool create_dirs;
    bool atomic;
    /* Under athena_job_lock(). */
    uint64_t bytes_done;
    uint64_t bytes_total;
    void *data;                 /* read jobs, once done */
    size_t size;
} McJob;

/* What the progress callback needs: the running job and its data. */
typedef struct {
    AthenaJob *job;
    McJob *mc;
} McRun;

static AthenaJob *core(AthenaMemcardJob *job) {
    return (AthenaJob *)job;
}

/* Worker side: records the progress; < 0 when the script or the runtime asked to stop. */
static int job_progress(uint64_t done, uint64_t total, void *user) {
    McRun *run = user;

    athena_job_lock(run->job);
    run->mc->bytes_done = done;
    if (total)
        run->mc->bytes_total = total;
    athena_job_unlock(run->job);
    return athena_job_should_stop(run->job) ? -1 : 0;
}

static int job_run(AthenaJob *job, void *data) {
    McJob *mc = data;
    McRun run = { job, mc };
    AthenaMemcardWriteOptions options = {
        .create_dirs = mc->create_dirs,
        .atomic = mc->atomic,
        .progress = job_progress,
        .user = &run,
    };
    void *read = NULL;
    size_t size = 0;
    int ret;

    switch (mc->kind) {
    case JOB_READ:
        ret = athena_memcard_read_file(mc->port, mc->path, &read, &size, job_progress, &run);
        break;
    case JOB_WRITE:
        ret = athena_memcard_write_file(mc->port, mc->path, mc->input, mc->input_size, &options);
        break;
    case JOB_REMOVE:
        ret = athena_memcard_remove(mc->port, mc->path, mc->recursive, job_progress, &run);
        break;
    default:
        ret = athena_memcard_format(mc->port);
        break;
    }

    if (ret >= 0 && mc->kind == JOB_READ) {
        athena_job_lock(job);
        mc->bytes_done = mc->bytes_total = size;
        mc->data = read;
        mc->size = size;
        athena_job_unlock(job);
        read = NULL;
    }
    free(read);
    return ret;
}

static void job_free(void *data) {
    McJob *mc = data;

    free(mc->path);
    free(mc->input);
    free(mc->data);
    free(mc);
}

static const AthenaJobType memcard_job_type = {
    "Memory card", job_run, job_free, ATHENA_MEMCARD_ERR_CANCELLED, ATHENA_JOB_PRIORITY_IO,
};

static McJob *job_new(JobKind kind, int port, const char *path) {
    McJob *mc;

    if (port < 0 || port >= ATHENA_MEMCARD_PORTS)
        return NULL;
    mc = calloc(1, sizeof(*mc));
    if (!mc)
        return NULL;
    mc->kind = kind;
    mc->port = port;
    if (path && !(mc->path = strdup(path))) {
        free(mc);
        return NULL;
    }
    return mc;
}

static AthenaMemcardJob *job_start(McJob *mc) {
    return mc ? (AthenaMemcardJob *)athena_job_submit(&memcard_job_type, mc) : NULL;
}

AthenaMemcardJob *athena_memcard_job_read(int port, const char *path) {
    return path ? job_start(job_new(JOB_READ, port, path)) : NULL;
}

AthenaMemcardJob *athena_memcard_job_write(int port, const char *path, const void *data,
    size_t size, const AthenaMemcardWriteOptions *options) {
    McJob *mc;

    if (!path || (!data && size))
        return NULL;
    mc = job_new(JOB_WRITE, port, path);
    if (!mc)
        return NULL;
    /* One byte at least, so an empty write still owns a buffer. */
    mc->input = malloc(size ? size : 1);
    if (!mc->input) {
        job_free(mc);
        return NULL;
    }
    if (size)
        memcpy(mc->input, data, size);
    mc->input_size = size;
    mc->bytes_total = size;
    if (options) {
        mc->create_dirs = options->create_dirs;
        mc->atomic = options->atomic;
    }
    return job_start(mc);
}

AthenaMemcardJob *athena_memcard_job_remove(int port, const char *path, bool recursive) {
    McJob *mc = path ? job_new(JOB_REMOVE, port, path) : NULL;

    if (mc)
        mc->recursive = recursive;
    return job_start(mc);
}

AthenaMemcardJob *athena_memcard_job_format(int port) {
    return job_start(job_new(JOB_FORMAT, port, NULL));
}

void athena_memcard_job_status(AthenaMemcardJob *job, AthenaMemcardJobStatus *out) {
    McJob *mc = athena_job_data(core(job));
    int result = 0;

    /* The pool's states are in the same order as AthenaMemcardJobState. */
    out->state = (AthenaMemcardJobState)athena_job_state(core(job), &result);
    out->result = out->state == ATHENA_MEMCARD_JOB_RUNNING ? 0 : result;
    athena_job_lock(core(job));
    out->bytes_done = mc->bytes_done;
    out->bytes_total = mc->bytes_total;
    athena_job_unlock(core(job));
}

void athena_memcard_job_cancel(AthenaMemcardJob *job) {
    athena_job_cancel(core(job));
}

bool athena_memcard_job_wait(AthenaMemcardJob *job, int timeout_ms) {
    return athena_job_wait(core(job), timeout_ms);
}

int athena_memcard_job_take_data(AthenaMemcardJob *job, void **data, size_t *size) {
    McJob *mc = athena_job_data(core(job));
    int ret = ATHENA_MEMCARD_ERR_ARGUMENT;

    *data = NULL;
    *size = 0;
    if (mc->kind != JOB_READ || athena_job_state(core(job), NULL) != ATHENA_JOB_DONE)
        return ret;
    athena_job_lock(core(job));
    *data = mc->data;
    *size = mc->size;
    mc->data = NULL;
    mc->size = 0;
    athena_job_unlock(core(job));
    return ATHENA_MEMCARD_OK;
}

/*
 * Cancels and waits for the job, which then holds no card handle: at most
 * one block of work remains (a format runs to the end). Frees everything.
 */
void athena_memcard_job_destroy(AthenaMemcardJob *job) {
    if (!job)
        return;
    athena_job_cancel(core(job));
    athena_job_wait(core(job), -1);
    athena_job_release(core(job));
}
