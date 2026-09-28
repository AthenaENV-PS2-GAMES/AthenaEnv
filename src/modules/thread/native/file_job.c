#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <athena/file_job.h>

/* Whole-file reads on the job pool (athena/file_job.h). */

typedef struct {
    char *path;
    size_t max;
    /* Under athena_job_lock(): the worker writes them while the script polls. */
    size_t done;
    size_t total;
    uint8_t *data;
    size_t size;
} FileRead;

static int file_read_run(AthenaJob *job, void *user)
{
    FileRead *read = user;
    FILE *file = fopen(read->path, "rb");
    uint8_t *data;
    size_t done = 0;
    long size;

    if (!file)
        return ATHENA_FILE_READ_OPEN;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return ATHENA_FILE_READ_IO;
    }
    if ((unsigned long)size > read->max) {
        fclose(file);
        return ATHENA_FILE_READ_TOO_LARGE;
    }
    data = malloc((size_t)size + 1);
    if (!data) {
        fclose(file);
        return ATHENA_FILE_READ_NOMEM;
    }
    athena_job_lock(job);
    read->total = (size_t)size;
    athena_job_unlock(job);
    while (done < (size_t)size) {
        size_t chunk = (size_t)size - done;
        size_t got;

        if (athena_job_should_stop(job)) {
            fclose(file);
            free(data);
            return ATHENA_FILE_READ_CANCELLED;
        }
        if (chunk > ATHENA_FILE_READ_CHUNK)
            chunk = ATHENA_FILE_READ_CHUNK;
        got = fread(data + done, 1, chunk, file);
        if (got != chunk) {
            fclose(file);
            free(data);
            return ATHENA_FILE_READ_IO;
        }
        done += got;
        athena_job_lock(job);
        read->done = done;
        athena_job_unlock(job);
    }
    fclose(file);
    data[done] = 0;
    athena_job_lock(job);
    read->data = data;
    read->size = done;
    athena_job_unlock(job);
    return ATHENA_FILE_READ_OK;
}

static void file_read_release(void *user)
{
    FileRead *read = user;

    free(read->path);
    free(read->data);
    free(read);
}

static const AthenaJobType file_read_type = {
    "File read", file_read_run, file_read_release, ATHENA_FILE_READ_CANCELLED,
    ATHENA_JOB_PRIORITY_IO,
};

AthenaJob *athena_file_read_submit(const char *path, size_t max_bytes)
{
    FileRead *read;

    if (!path)
        return NULL;
    read = calloc(1, sizeof(*read));
    if (!read)
        return NULL;
    read->path = malloc(strlen(path) + 1);
    if (!read->path) {
        free(read);
        return NULL;
    }
    strcpy(read->path, path);
    read->max = max_bytes ? max_bytes : ATHENA_FILE_READ_DEFAULT_MAX;
    return athena_job_submit(&file_read_type, read);
}

void athena_file_read_progress(AthenaJob *job, size_t *done, size_t *total)
{
    FileRead *read = athena_job_data(job);

    athena_job_lock(job);
    *done = read->done;
    *total = read->total;
    athena_job_unlock(job);
}

uint8_t *athena_file_read_take(AthenaJob *job, size_t *size)
{
    FileRead *read = athena_job_data(job);
    uint8_t *data;

    athena_job_lock(job);
    data = read->data;
    *size = read->size;
    read->data = NULL;
    read->size = 0;
    athena_job_unlock(job);
    return data;
}
