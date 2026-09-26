#ifndef ATHENA_JS_JOB_H
#define ATHENA_JS_JOB_H

#include <stdbool.h>

#include <quickjs.h>
#include <athena/job.h>

/*
 * The JavaScript side of background jobs (athena/job.h), shared by every
 * module: one Job class whose objects can be awaited (then), polled
 * (poll() -> { state, result | error, ...progress }), waited for and
 * cancelled. A module describes its kind of job once:
 *
 *     static const AthenaJsJobKind read_kind = {
 *         "MemoryCard", read_settle, read_status, read_free_user,
 *     };
 *     return athena_js_job_new(ctx, &read_kind, athena_job_submit(...), user);
 *
 * settle() converts the native outcome to a JavaScript value exactly once,
 * on the script thread, the first time the job is seen settled; that is the
 * place for script-thread work such as uploading a sample or creating a font.
 */

typedef struct AthenaJsJobKind {
    /* Module name, for messages ("MemoryCard.poll: expected a MemoryCard job"). */
    const char *owner;
    /*
     * Stores the job's value in `outcome`, or its error with `failed` set;
     * `state` is DONE, FAILED or CANCELLED. Returns 0, or -1 with an
     * exception pending (the job then fails with it).
     */
    int (*settle)(JSContext *ctx, AthenaJob *job, AthenaJobState state, int result,
        void *user, JSValue *outcome, bool *failed);
    /* Adds the module's progress fields to a poll() object. Optional. */
    void (*status)(JSContext *ctx, AthenaJob *job, void *user, JSValue object);
    /* Frees `user` when the handle goes away. Optional. */
    void (*free_user)(JSRuntime *rt, void *user);
    /*
     * Lets go of the native job when the handle goes away; athena_job_release
     * (never blocks) when NULL. A module whose jobs hold a limited resource can
     * cancel and wait instead.
     */
    void (*release)(AthenaJob *job);
    /* Cancels the job for cancel(); athena_job_cancel when NULL. Optional. */
    void (*cancel)(AthenaJob *job, void *user);
} AthenaJsJobKind;

/* Registers the Job class on a new context; the thread module does it. */
void athena_js_job_class_init(JSContext *ctx);

/*
 * Wraps a submitted job in a Job object that owns it and `user`. With a NULL
 * `job` (submission failed) it throws `name`: cannot start and frees `user`.
 */
JSValue athena_js_job_new(JSContext *ctx, const AthenaJsJobKind *kind, AthenaJob *job,
    void *user, const char *name);

/* Module functions such as MemoryCard.poll(job), checking the job is one of `kind`. */
JSValue athena_js_job_poll(JSContext *ctx, JSValueConst job, const AthenaJsJobKind *kind,
    const char *name);
JSValue athena_js_job_wait(JSContext *ctx, JSValueConst job, int argc, JSValueConst *argv,
    const AthenaJsJobKind *kind, const char *name);
JSValue athena_js_job_cancel(JSContext *ctx, JSValueConst job, const AthenaJsJobKind *kind,
    const char *name);

/* True when `value` is a job of `kind` (any kind when NULL); never throws. */
bool athena_js_job_is(JSValueConst value, const AthenaJsJobKind *kind);

/* The native job of `value`, or NULL with a TypeError when it is not a job of `kind`. */
AthenaJob *athena_js_job_native(JSContext *ctx, JSValueConst value, const AthenaJsJobKind *kind,
    const char *name);

#endif /* ATHENA_JS_JOB_H */
