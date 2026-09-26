#include <stdlib.h>
#include <string.h>

#include <athena/archive.h>
#include <athena/job.h>

/*
 * Archive jobs run on the shared job pool (athena/job.h), below the script
 * thread since decompressing uses the CPU. An AthenaArchiveJob is the pool's
 * AthenaJob; its data is an ArJob. zlib keeps its state on the heap, so the
 * pool's 32 KB stack is enough for paths and tar blocks.
 */

typedef enum JobKind {
    JOB_EXTRACT,
    JOB_READ,
} JobKind;

typedef struct {
    /* Immutable while the job runs. */
    JobKind kind;
    char *path;
    char *dest;
    char *name;
    char **include;
    int include_count;
    bool overwrite;
    uint64_t max_size;
    /* Under athena_job_lock(); state and result come from the pool. */
    AthenaArchiveJobStatus status;
    void *data;
    size_t size;
} ArJob;

/* What the archive callbacks need: the running job and its data. */
typedef struct {
    AthenaJob *job;
    ArJob *ar;
} ArRun;

static AthenaJob *core(AthenaArchiveJob *job)
{
    return (AthenaJob *)job;
}

static bool job_includes(const ArJob *ar, const char *name)
{
    if (ar->include_count == 0)
        return true;
    for (int i = 0; i < ar->include_count; i++) {
        const char *item = ar->include[i];
        size_t len = strlen(item);
        if (!strcmp(name, item))
            return true;
        if (len && item[len - 1] == '/' && !strncmp(name, item, len))
            return true;
    }
    return false;
}

static int job_filter(const AthenaArchiveEntry *entry, void *user)
{
    ArRun *run = user;

    if (athena_job_should_stop(run->job))
        return -1;
    if (!job_includes(run->ar, entry->name))
        return 0;
    athena_job_lock(run->job);
    run->ar->status.entries_total++;
    run->ar->status.bytes_total += entry->size;
    athena_job_unlock(run->job);
    return 1;
}

static int job_progress(const AthenaArchiveEntry *entry, int index, int count, void *user)
{
    ArRun *run = user;
    AthenaArchiveJobStatus *status = &run->ar->status;

    if (athena_job_should_stop(run->job))
        return -1;
    athena_job_lock(run->job);
    status->entries_done = index;
    status->entries_total = count;
    strncpy(status->entry, entry->name, sizeof(status->entry) - 1);
    status->entry[sizeof(status->entry) - 1] = '\0';
    athena_job_unlock(run->job);
    return 0;
}

static int job_written(uint64_t total, void *user)
{
    ArRun *run = user;

    if (athena_job_should_stop(run->job))
        return -1;
    athena_job_lock(run->job);
    run->ar->status.bytes_done = total;
    athena_job_unlock(run->job);
    return 0;
}

/* Read jobs report the entry's declared size before decompressing it. */
static void job_describe_read(ArRun *run, AthenaArchive *archive)
{
    const AthenaArchiveEntry *entries;
    AthenaArchiveJobStatus *status = &run->ar->status;
    int count;

    if (athena_archive_entries(archive, &entries, &count) < 0)
        return;
    athena_job_lock(run->job);
    for (int i = count - 1; i >= 0; i--) {
        if (run->ar->name ? !strcmp(entries[i].name, run->ar->name) : i == 0) {
            status->entries_total = 1;
            status->bytes_total = entries[i].size;
            strncpy(status->entry, entries[i].name, sizeof(status->entry) - 1);
            break;
        }
    }
    athena_job_unlock(run->job);
}

static int job_run(AthenaJob *job, void *arg)
{
    ArJob *ar = arg;
    ArRun run = { job, ar };
    AthenaArchive *archive = NULL;
    const char *detail = "";
    void *data = NULL;
    size_t size = 0;
    int ret;

    ret = athena_archive_open(ar->path, &archive);
    if (ret < 0) {
        detail = ar->path;
    } else if (ar->kind == JOB_EXTRACT) {
        AthenaArchiveExtractOptions options = {
            .overwrite = ar->overwrite,
            .max_size = ar->max_size,
            .filter = job_filter,
            .progress = job_progress,
            .written = job_written,
            .user = &run,
        };
        ret = athena_archive_extract(archive, ar->dest, &options);
        detail = athena_archive_error_detail(archive);
    } else {
        job_describe_read(&run, archive);
        if (athena_job_should_stop(job))
            ret = ATHENA_ARCHIVE_ERR_ABORTED;
        else
            ret = athena_archive_read(archive, ar->name, (size_t)ar->max_size, &data, &size);
        detail = athena_archive_error_detail(archive);
    }

    athena_job_lock(job);
    if (ret >= 0) {
        if (ar->kind == JOB_EXTRACT) {
            ar->status.entries_done = ret;
        } else {
            ar->status.entries_done = 1;
            ar->status.bytes_done = size;
            ar->data = data;
            ar->size = size;
            data = NULL;
        }
    } else {
        strncpy(ar->status.detail, detail, sizeof(ar->status.detail) - 1);
    }
    ar->status.entry[0] = '\0';
    athena_job_unlock(job);
    free(data);
    athena_archive_close(archive);
    return ret;
}

static void job_free(void *arg)
{
    ArJob *ar = arg;

    for (int i = 0; i < ar->include_count; i++)
        free(ar->include[i]);
    free(ar->include);
    free(ar->path);
    free(ar->dest);
    free(ar->name);
    free(ar->data);
    free(ar);
}

/* Aborting is only ever asked by the job itself (cancel, runtime stop). */
static const AthenaJobType archive_job_type = {
    "Archive", job_run, job_free, ATHENA_ARCHIVE_ERR_ABORTED, ATHENA_JOB_PRIORITY_CPU,
};

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
    ArJob *ar;
    bool ok = true;

    if (!path || !path[0])
        return NULL;
    ar = calloc(1, sizeof(*ar));
    if (!ar)
        return NULL;
    ar->kind = kind;
    ar->path = job_strdup(path, &ok);
    ar->dest = job_strdup(dest && dest[0] ? dest : NULL, &ok);
    ar->name = job_strdup(name, &ok);
    ar->overwrite = options ? options->overwrite : true;
    ar->max_size = options ? options->max_size : 0;
    if (options && options->include_count > 0) {
        ar->include = calloc((size_t)options->include_count, sizeof(*ar->include));
        if (!ar->include)
            ok = false;
        for (int i = 0; ok && i < options->include_count; i++) {
            ar->include[i] = job_strdup(options->include[i], &ok);
            ar->include_count = i + 1;
        }
    }
    if (!ok) {
        job_free(ar);
        return NULL;
    }
    return (AthenaArchiveJob *)athena_job_submit(&archive_job_type, ar);
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
    ArJob *ar = athena_job_data(core(job));
    AthenaArchiveJobState state;
    int result = 0;

    /* The pool's states are in the same order as AthenaArchiveJobState. */
    state = (AthenaArchiveJobState)athena_job_state(core(job), &result);
    athena_job_lock(core(job));
    *out = ar->status;
    athena_job_unlock(core(job));
    out->state = state;
    out->result = state == ATHENA_ARCHIVE_JOB_RUNNING ? 0 : result;
}

void athena_archive_job_cancel(AthenaArchiveJob *job)
{
    athena_job_cancel(core(job));
}

bool athena_archive_job_wait(AthenaArchiveJob *job, int timeout_ms)
{
    return athena_job_wait(core(job), timeout_ms);
}

int athena_archive_job_take_data(AthenaArchiveJob *job, void **out_data, size_t *out_size)
{
    ArJob *ar = athena_job_data(core(job));
    int ret = ATHENA_ARCHIVE_ERR_ARGUMENT;

    *out_data = NULL;
    *out_size = 0;
    if (athena_job_state(core(job), NULL) != ATHENA_JOB_DONE)
        return ret;
    athena_job_lock(core(job));
    if (ar->data) {
        *out_data = ar->data;
        *out_size = ar->size;
        ar->data = NULL;
        ret = ATHENA_ARCHIVE_OK;
    }
    athena_job_unlock(core(job));
    return ret;
}

/* Cancels and waits for the job (it stops at the next block), then frees everything. */
void athena_archive_job_destroy(AthenaArchiveJob *job)
{
    if (!job)
        return;
    athena_job_cancel(core(job));
    athena_job_wait(core(job), -1);
    athena_job_release(core(job));
}
