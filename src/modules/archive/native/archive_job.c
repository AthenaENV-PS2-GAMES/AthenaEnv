#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <kernel.h>
#include <delaythread.h>

#include <athena/archive.h>
#include <athena/mutex.h>
#include <athena/thread.h>

/* zlib state lives on the heap; paths and tar blocks need a few KiB. */
#define JOB_STACK_SIZE (32 * 1024)
#define JOB_WAIT_POLL_US 1000

typedef enum JobKind {
    JOB_EXTRACT,
    JOB_READ,
} JobKind;

struct AthenaArchiveJob {
    AthenaThread *thread;
    AthenaMutex *mutex;
    /* Immutable while the worker runs. */
    JobKind kind;
    char *path;
    char *dest;
    char *name;
    char **include;
    int include_count;
    bool overwrite;
    uint64_t max_size;
    /* Protected by `mutex`. */
    AthenaArchiveJobStatus status;
    void *data;
    size_t size;
    bool cancel;
};

static bool job_lock(AthenaArchiveJob *job)
{
    return athena_mutex_core_lock(job->mutex) >= 0;
}

static void job_unlock(AthenaArchiveJob *job)
{
    athena_mutex_core_unlock(job->mutex);
}

/* Worker side: true when the script or the runtime asked to stop. */
static bool job_should_stop(AthenaArchiveJob *job)
{
    bool cancel = true;

    if (job_lock(job)) {
        cancel = job->cancel;
        job_unlock(job);
    }
    return cancel || athena_thread_core_stop_requested();
}

static bool job_includes(const AthenaArchiveJob *job, const char *name)
{
    if (job->include_count == 0)
        return true;
    for (int i = 0; i < job->include_count; i++) {
        const char *item = job->include[i];
        size_t len = strlen(item);
        if (!strcmp(name, item))
            return true;
        if (len && item[len - 1] == '/' && !strncmp(name, item, len))
            return true;
    }
    return false;
}

/* Runs for every entry before anything is written: selection and totals. */
static int job_filter(const AthenaArchiveEntry *entry, void *user)
{
    AthenaArchiveJob *job = user;

    if (job_should_stop(job))
        return -1;
    if (!job_includes(job, entry->name))
        return 0;
    if (job_lock(job)) {
        job->status.entries_total++;
        job->status.bytes_total += entry->size;
        job_unlock(job);
    }
    return 1;
}

static int job_progress(const AthenaArchiveEntry *entry, int index, int count, void *user)
{
    AthenaArchiveJob *job = user;

    if (job_should_stop(job))
        return -1;
    if (job_lock(job)) {
        job->status.entries_done = index;
        job->status.entries_total = count;
        strncpy(job->status.entry, entry->name, sizeof(job->status.entry) - 1);
        job->status.entry[sizeof(job->status.entry) - 1] = '\0';
        job_unlock(job);
    }
    return 0;
}

static int job_written(uint64_t total, void *user)
{
    AthenaArchiveJob *job = user;

    if (job_should_stop(job))
        return -1;
    if (job_lock(job)) {
        job->status.bytes_done = total;
        job_unlock(job);
    }
    return 0;
}

/* Read jobs report the entry's declared size before decompressing it. */
static void job_describe_read(AthenaArchiveJob *job, AthenaArchive *archive)
{
    const AthenaArchiveEntry *entries;
    int count;

    if (athena_archive_entries(archive, &entries, &count) < 0 || !job_lock(job))
        return;
    for (int i = count - 1; i >= 0; i--) {
        if (job->name ? !strcmp(entries[i].name, job->name) : i == 0) {
            job->status.entries_total = 1;
            job->status.bytes_total = entries[i].size;
            strncpy(job->status.entry, entries[i].name, sizeof(job->status.entry) - 1);
            break;
        }
    }
    job_unlock(job);
}

static void job_worker(void *arg)
{
    AthenaArchiveJob *job = arg;
    AthenaArchive *archive = NULL;
    const char *detail = "";
    void *data = NULL;
    size_t size = 0;
    int ret;

    ret = athena_archive_open(job->path, &archive);
    if (ret < 0) {
        detail = job->path;
    } else if (job->kind == JOB_EXTRACT) {
        AthenaArchiveExtractOptions options = {
            .overwrite = job->overwrite,
            .max_size = job->max_size,
            .filter = job_filter,
            .progress = job_progress,
            .written = job_written,
            .user = job,
        };
        ret = athena_archive_extract(archive, job->dest, &options);
        detail = athena_archive_error_detail(archive);
    } else {
        job_describe_read(job, archive);
        if (job_should_stop(job))
            ret = ATHENA_ARCHIVE_ERR_ABORTED;
        else
            ret = athena_archive_read(archive, job->name, (size_t)job->max_size, &data, &size);
        detail = athena_archive_error_detail(archive);
    }

    if (job_lock(job)) {
        AthenaArchiveJobStatus *status = &job->status;
        status->result = ret;
        if (ret >= 0) {
            status->state = ATHENA_ARCHIVE_JOB_DONE;
            if (job->kind == JOB_EXTRACT) {
                status->entries_done = ret;
            } else {
                status->entries_done = 1;
                status->bytes_done = size;
                job->data = data;
                job->size = size;
                data = NULL;
            }
        } else {
            status->state = ret == ATHENA_ARCHIVE_ERR_ABORTED &&
                (job->cancel || athena_thread_core_stop_requested()) ?
                ATHENA_ARCHIVE_JOB_CANCELLED : ATHENA_ARCHIVE_JOB_FAILED;
            strncpy(status->detail, detail, sizeof(status->detail) - 1);
        }
        status->entry[0] = '\0';
        job_unlock(job);
    }
    free(data);
    athena_archive_close(archive);

    athena_thread_core_worker_finished(job->thread);
    ExitThread();
}

static void job_free(AthenaArchiveJob *job)
{
    if (job->mutex)
        athena_mutex_core_destroy(job->mutex);
    for (int i = 0; i < job->include_count; i++)
        free(job->include[i]);
    free(job->include);
    free(job->path);
    free(job->dest);
    free(job->name);
    free(job->data);
    free(job);
}

static char *job_strdup(const char *text, bool *ok)
{
    char *copy;

    if (!text)
        return NULL;
    copy = strdup(text);
    if (!copy)
        *ok = false;
    return copy;
}

static AthenaArchiveJob *job_start(JobKind kind, const char *path, const char *dest,
    const char *name, const AthenaArchiveJobOptions *options)
{
    AthenaArchiveJob *job;
    bool ok = true;

    if (!path || !path[0])
        return NULL;
    job = calloc(1, sizeof(*job));
    if (!job)
        return NULL;
    job->kind = kind;
    job->path = job_strdup(path, &ok);
    job->dest = job_strdup(dest && dest[0] ? dest : NULL, &ok);
    job->name = job_strdup(name, &ok);
    job->overwrite = options ? options->overwrite : true;
    job->max_size = options ? options->max_size : 0;
    if (options && options->include_count > 0) {
        job->include = calloc((size_t)options->include_count, sizeof(*job->include));
        if (!job->include)
            ok = false;
        for (int i = 0; ok && i < options->include_count; i++) {
            job->include[i] = job_strdup(options->include[i], &ok);
            job->include_count = i + 1;
        }
    }
    job->mutex = ok ? athena_mutex_core_create() : NULL;
    if (!job->mutex) {
        job_free(job);
        return NULL;
    }

    job->thread = athena_thread_core_create("Archive job", job_worker, job,
        JOB_STACK_SIZE, ATHENA_THREAD_DEFAULT_PRIORITY + 1);
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

AthenaArchiveJob *athena_archive_job_extract(const char *path, const char *dest_dir,
    const AthenaArchiveJobOptions *options)
{
    return job_start(JOB_EXTRACT, path, dest_dir, NULL, options);
}

AthenaArchiveJob *athena_archive_job_read(const char *path, const char *name,
    const AthenaArchiveJobOptions *options)
{
    return job_start(JOB_READ, path, NULL, name, options);
}

void athena_archive_job_status(AthenaArchiveJob *job, AthenaArchiveJobStatus *out)
{
    if (!job_lock(job)) {
        memset(out, 0, sizeof(*out));
        out->state = ATHENA_ARCHIVE_JOB_FAILED;
        out->result = ATHENA_ARCHIVE_ERR_IO;
        return;
    }
    *out = job->status;
    job_unlock(job);
}

void athena_archive_job_cancel(AthenaArchiveJob *job)
{
    if (job_lock(job)) {
        job->cancel = true;
        job_unlock(job);
    }
}

bool athena_archive_job_wait(AthenaArchiveJob *job, int timeout_ms)
{
    clock_t start = clock();

    for (;;) {
        AthenaArchiveJobStatus status;
        athena_archive_job_status(job, &status);
        if (status.state != ATHENA_ARCHIVE_JOB_RUNNING)
            return true;
        if (timeout_ms >= 0 &&
            (clock() - start) * 1000 / CLOCKS_PER_SEC >= (clock_t)timeout_ms)
            return false;
        DelayThread(JOB_WAIT_POLL_US);
    }
}

int athena_archive_job_take_data(AthenaArchiveJob *job, void **out_data, size_t *out_size)
{
    int ret = ATHENA_ARCHIVE_ERR_ARGUMENT;

    *out_data = NULL;
    *out_size = 0;
    if (!job_lock(job))
        return ATHENA_ARCHIVE_ERR_IO;
    if (job->status.state == ATHENA_ARCHIVE_JOB_DONE && job->data) {
        *out_data = job->data;
        *out_size = job->size;
        job->data = NULL;
        ret = ATHENA_ARCHIVE_OK;
    }
    job_unlock(job);
    return ret;
}

/* Joins the worker; it may already have exited on its own. */
void athena_archive_job_destroy(AthenaArchiveJob *job)
{
    if (!job)
        return;
    athena_archive_job_cancel(job);
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
