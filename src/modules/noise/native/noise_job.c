#include <stdlib.h>

#include <athena/job.h>
#include <athena/noise.h>

/*
 * Background fill: the worker owns everything it touches (copies of the
 * tables and options, and its own output buffer), so nothing the script does
 * meanwhile can pull memory from under it. The script takes the buffer once
 * the job is done.
 */

/* Rows per slice: small enough to cancel quickly, large enough to be cheap. */
#define NOISE_JOB_ROWS 8

typedef struct {
    AthenaNoise noise;
    AthenaNoiseFill fill;
    size_t width, height;
    float *out;
    size_t rows_done;       /* under athena_job_lock() */
} NoiseFillJob;

static int noise_job_run(AthenaJob *job, void *data) {
    NoiseFillJob *fill = data;

    for (size_t row = 0; row < fill->height; row += NOISE_JOB_ROWS) {
        size_t rows = fill->height - row < NOISE_JOB_ROWS ?
            fill->height - row : NOISE_JOB_ROWS;

        if (athena_job_should_stop(job))
            return ATHENA_NOISE_JOB_CANCELLED;
        athena_noise_fill_rows(&fill->noise, &fill->fill, fill->out,
            fill->width, row, rows);
        athena_job_lock(job);
        fill->rows_done = row + rows;
        athena_job_unlock(job);
    }
    athena_noise_fill_finish(&fill->fill, fill->out, fill->width * fill->height);
    return 0;
}

static void noise_job_release(void *data) {
    NoiseFillJob *fill = data;

    free(fill->out);
    free(fill);
}

static const AthenaJobType noise_job_type = {
    "Noise fill", noise_job_run, noise_job_release, ATHENA_NOISE_JOB_CANCELLED,
    ATHENA_JOB_PRIORITY_CPU,
};

AthenaJob *athena_noise_fill_submit(const AthenaNoise *noise,
    const AthenaNoiseFill *fill, size_t width, size_t height) {
    NoiseFillJob *job;
    size_t count = width * height;

    if (height && count / height != width)
        return NULL;
    job = calloc(1, sizeof(*job));
    if (!job)
        return NULL;
    job->out = malloc((count ? count : 1) * sizeof(float));
    if (!job->out) {
        free(job);
        return NULL;
    }
    job->noise = *noise;
    job->fill = *fill;
    job->width = width;
    job->height = height;
    /* The job type releases the data when submission fails, too. */
    return athena_job_submit(&noise_job_type, job);
}

size_t athena_noise_fill_job_rows(AthenaJob *job) {
    NoiseFillJob *fill = athena_job_data(job);
    size_t rows;

    athena_job_lock(job);
    rows = fill->rows_done;
    athena_job_unlock(job);
    return rows;
}

float *athena_noise_fill_job_take(AthenaJob *job) {
    NoiseFillJob *fill = athena_job_data(job);
    float *out;

    athena_job_lock(job);
    out = fill->out;
    fill->out = NULL;
    athena_job_unlock(job);
    return out;
}
