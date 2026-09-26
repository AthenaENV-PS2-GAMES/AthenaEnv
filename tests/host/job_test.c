/* Host test of the shared job pool (src/modules/thread/native/job.c): states, cancelling, releasing, stopping. */
#include "host_runtime.h"

#include "job.c"

#define ERR_FAILED (-5)
#define ERR_CANCELLED (-9)

typedef struct {
    int value;              /* run(): < 0 fails with it */
    volatile int gate;      /* run() waits for it to become non-zero, unless cancelled */
    volatile int started;
    volatile int ran;
    int progress;           /* written under athena_job_lock() */
    int *released;          /* incremented by release() */
} Work;

static int work_run(AthenaJob *job, void *data) {
    Work *work = data;

    work->started = 1;
    while (!work->gate) {
        if (athena_job_should_stop(job))
            return ERR_CANCELLED;
        athena_job_lock(job);
        work->progress++;
        athena_job_unlock(job);
        sleep_ms(1);
    }
    work->ran = 1;
    return work->value;
}

static void work_release(void *data) {
    Work *work = data;
    if (work->released)
        (*work->released)++;
    free(work);
}

static const AthenaJobType work_type = {
    "Test", work_run, work_release, ERR_CANCELLED, ATHENA_JOB_PRIORITY_IO,
};

static Work *work_new(int value, int gate, int *released) {
    Work *work = calloc(1, sizeof(*work));
    work->value = value;
    work->gate = gate;
    work->released = released;
    return work;
}

static bool eventually(bool (*condition)(void *), void *arg) {
    for (int i = 0; i < 2000; i++) {
        if (condition(arg))
            return true;
        sleep_ms(1);
    }
    return false;
}

static bool is_started(void *arg) { return ((Work *)arg)->started != 0; }
static bool is_zero(void *arg) { return *(volatile int *)arg == 0; }
static bool is_released(void *arg) { return *(volatile int *)arg != 0; }

int main(void) {
    int result = 0, released = 0;
    Work *work, *blocker1, *blocker2;
    AthenaJob *job, *b1, *b2;

    /* Done, with the value run() returned. */
    work = work_new(7, 1, NULL);
    job = athena_job_submit(&work_type, work);
    CHECK(job != NULL, "submit");
    CHECK(athena_job_wait(job, 5000), "wait");
    CHECK(athena_job_state(job, &result) == ATHENA_JOB_DONE && result == 7, "done %d", result);
    CHECK(athena_job_data(job) == work && work->ran, "data and run");
    athena_job_release(job);

    /* Failed and cancelled results. */
    job = athena_job_submit(&work_type, work_new(ERR_FAILED, 1, NULL));
    athena_job_wait(job, 5000);
    CHECK(athena_job_state(job, &result) == ATHENA_JOB_FAILED && result == ERR_FAILED, "failed %d", result);
    athena_job_release(job);

    /* Cancelling a running job: run() sees should_stop(). */
    work = work_new(0, 0, NULL);
    job = athena_job_submit(&work_type, work);
    CHECK(eventually(is_started, work), "started");
    athena_job_cancel(job);
    CHECK(athena_job_wait(job, 5000), "cancel settles");
    CHECK(athena_job_state(job, &result) == ATHENA_JOB_CANCELLED && result == ERR_CANCELLED, "cancelled");
    athena_job_lock(job);
    CHECK(work->progress > 0, "progress under the lock");
    athena_job_unlock(job);
    athena_job_release(job);

    /* Both workers busy: a third job queues, and cancelling it ends it without running. */
    blocker1 = work_new(0, 0, NULL);
    blocker2 = work_new(0, 0, NULL);
    b1 = athena_job_submit(&work_type, blocker1);
    b2 = athena_job_submit(&work_type, blocker2);
    CHECK(eventually(is_started, blocker1) && eventually(is_started, blocker2), "two workers");
    work = work_new(1, 1, &released);
    job = athena_job_submit(&work_type, work);
    sleep_ms(20);
    CHECK(!work->started && athena_job_state(job, NULL) == ATHENA_JOB_RUNNING, "queued behind the workers");
    athena_job_cancel(job);
    CHECK(athena_job_state(job, &result) == ATHENA_JOB_CANCELLED, "queued job cancelled at once");
    athena_job_release(job);
    CHECK(released == 1, "released at once: %d", released);

    /* Releasing a queued job frees it without running; a running one frees when done. */
    released = 0;
    job = athena_job_submit(&work_type, work_new(1, 1, &released));
    athena_job_release(job);
    CHECK(released == 1, "queued job released: %d", released);
    released = 0;
    blocker1->released = &released;
    athena_job_release(b1);
    CHECK(eventually(is_released, &released), "running job freed after cancelling: %d", released);

    /* Queued jobs run once a worker is free. */
    work = work_new(3, 1, NULL);
    job = athena_job_submit(&work_type, work);
    CHECK(athena_job_wait(job, 5000) && athena_job_state(job, &result) == ATHENA_JOB_DONE && result == 3,
        "queued job ran");
    athena_job_release(job);

    /* Stopping the pool: queued jobs cancelled, running ones asked to stop, workers joined. */
    blocker1 = work_new(0, 0, NULL);
    b1 = athena_job_submit(&work_type, blocker1);
    CHECK(eventually(is_started, blocker1), "blockers running");
    released = 0;
    job = athena_job_submit(&work_type, work_new(1, 1, &released));
    athena_job_pool_stop();
    CHECK(athena_job_state(b1, NULL) == ATHENA_JOB_CANCELLED && athena_job_state(b2, NULL) == ATHENA_JOB_CANCELLED,
        "running jobs cancelled by the stop");
    CHECK(athena_job_state(job, NULL) == ATHENA_JOB_CANCELLED, "queued job cancelled by the stop");
    CHECK(eventually(is_zero, &threads_alive), "workers joined: %d alive", threads_alive);
    athena_job_release(b1);
    athena_job_release(b2);
    athena_job_release(job);
    CHECK(released == 1, "queued job freed: %d", released);

    /* The pool starts again for the next script. */
    job = athena_job_submit(&work_type, work_new(5, 1, NULL));
    CHECK(athena_job_wait(job, 5000) && athena_job_state(job, &result) == ATHENA_JOB_DONE && result == 5,
        "pool restarts");
    athena_job_release(job);
    athena_job_pool_stop();

    /* Submitting without a run function fails and releases the data. */
    released = 0;
    {
        static const AthenaJobType broken = { "Broken", NULL, work_release, 0, 0 };
        CHECK(athena_job_submit(&broken, work_new(0, 1, &released)) == NULL && released == 1, "invalid type");
    }

    printf("job: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
