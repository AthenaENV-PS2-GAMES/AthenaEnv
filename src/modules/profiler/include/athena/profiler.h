#ifndef ATHENA_PROFILER_H
#define ATHENA_PROFILER_H
#include <stdint.h>
/* CPU time markers and per-frame counters with a short history.
 *
 * A timer scope accumulates the time between begin() and end() during a
 * frame (inclusive: nested scopes count in their parents too, and a scope
 * nested in itself counts twice); a counter sums the values added during a
 * frame. frame() closes the frame: each scope's total goes into a ring of
 * the last ATHENA_PROFILER_HISTORY frames, from which stats() computes
 * average, 95th percentile and peak. Scopes still open at frame() are split:
 * the time so far goes to the closing frame and they continue in the next.
 * Scope 0 is "frame", the time between frame() calls.
 *
 * The clock is the EE's COP0 Count register (CPU cycles, wraps after about
 * 14.5 s: a single begin/end span must be shorter); the host build uses the
 * monotonic clock. Main thread only. No allocation after the first use. */
#define ATHENA_PROFILER_MAX_SCOPES 64u
#define ATHENA_PROFILER_HISTORY 120u
#define ATHENA_PROFILER_MAX_DEPTH 32u
#define ATHENA_PROFILER_NAME_MAX 31u
typedef enum { ATHENA_PROFILER_TIMER=0, ATHENA_PROFILER_COUNTER=1 } AthenaProfilerKind;
/* Error codes. */
#define ATHENA_PROFILER_EINVAL (-1)  /* bad name, id or value */
#define ATHENA_PROFILER_EFULL (-2)   /* MAX_SCOPES names already registered */
#define ATHENA_PROFILER_EKIND (-3)   /* name registered with the other kind */
#define ATHENA_PROFILER_EDEPTH (-4)  /* more than MAX_DEPTH open scopes */
#define ATHENA_PROFILER_EOPEN (-5)   /* end() without a matching begin() */

/* Id of the scope called name (1..31 printable characters), registering it
 * with kind the first time; the id stays valid until reset(1). */
int athena_profiler_scope(const char *name,AthenaProfilerKind kind);
/* Id of an existing scope, or EINVAL. */
int athena_profiler_find(const char *name);
int athena_profiler_begin(int id);
/* Closes the innermost open scope; id >= 0 must name it (EOPEN otherwise,
 * counted in errors). */
int athena_profiler_end(int id);
/* Adds value (finite) to a counter for this frame. */
int athena_profiler_count(int id,float value);
/* Closes the frame; returns its length in milliseconds. */
float athena_profiler_frame(void);
typedef struct {
    const char *name;
    AthenaProfilerKind kind;
    /* Frames in the window (at most the frames closed since the reset). */
    uint32_t samples;
    /* Milliseconds for timers, summed values for counters, per frame. */
    float last,average,p95,peak;
    /* begin() calls (count() calls for counters): last frame and average. */
    uint32_t last_calls;
    float average_calls;
} AthenaProfilerStats;
/* Stats of scope id over the last frames frames (0 or more than the history
 * means all of it). Returns 0, or EINVAL for an unknown id. */
int athena_profiler_stats(int id,uint32_t frames,AthenaProfilerStats *out);
uint32_t athena_profiler_scope_count(void);
/* Unmatched end() calls and begin() beyond MAX_DEPTH since the reset. */
uint32_t athena_profiler_errors(void);
/* Clears the history, totals and open scopes; forget also drops the names
 * (ids become invalid), otherwise ids stay valid. */
void athena_profiler_reset(int forget);
/* Raw clock and its rate. */
uint32_t athena_profiler_ticks(void);
float athena_profiler_ticks_to_ms(uint32_t ticks);
void athena_profiler_module_shutdown(void);
#endif
