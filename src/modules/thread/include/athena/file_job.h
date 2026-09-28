#ifndef ATHENA_FILE_JOB_H
#define ATHENA_FILE_JOB_H

#include <stddef.h>
#include <stdint.h>

#include <athena/job.h>

/*
 * Reading a whole file on a worker of the job pool (athena/job.h), in
 * chunks, so a large level or a file on slow storage (disc, USB) does not
 * stall the frames:
 *
 *     AthenaJob *job = athena_file_read_submit("levels/1.json", 0);
 *     ... athena_job_state(job, &result) == ATHENA_JOB_DONE ...
 *     size_t size;
 *     uint8_t *data = athena_file_read_take(job, &size);
 *     ...
 *     free(data);
 *     athena_job_release(job);
 */

/* Bytes read per chunk; progress and cancellation are checked between chunks. */
#define ATHENA_FILE_READ_CHUNK (64u * 1024u)
/* Largest file read when no limit is given. */
#define ATHENA_FILE_READ_DEFAULT_MAX (16u * 1024u * 1024u)

/* Results of a read job (athena_job_state). */
#define ATHENA_FILE_READ_OK 0
#define ATHENA_FILE_READ_OPEN (-1)
#define ATHENA_FILE_READ_TOO_LARGE (-2)
#define ATHENA_FILE_READ_IO (-3)
#define ATHENA_FILE_READ_NOMEM (-4)
#define ATHENA_FILE_READ_CANCELLED (-5)

/*
 * Queues the read of `path`, refusing files larger than `max_bytes` (0:
 * ATHENA_FILE_READ_DEFAULT_MAX). NULL when it cannot be queued.
 */
AthenaJob *athena_file_read_submit(const char *path, size_t max_bytes);

/*
 * Bytes read so far and the file's size (0 until it is known), for
 * progress. Any thread.
 */
void athena_file_read_progress(AthenaJob *job, size_t *done, size_t *total);

/*
 * The data of a finished read (malloc'd, with a NUL after the last byte so
 * text can be used as is), which the caller then owns; NULL when the read
 * did not finish or the data was already taken.
 */
uint8_t *athena_file_read_take(AthenaJob *job, size_t *size);

#endif /* ATHENA_FILE_JOB_H */
