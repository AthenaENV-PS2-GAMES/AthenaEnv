#ifndef ATHENA_LOOP_H
#define ATHENA_LOOP_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Frame clock of a game loop, measured on the EE's 64-bit bus-clock counter.
 * C applications drive it directly:
 *
 *     AthenaLoopClock clock;
 *     athena_loop_clock_reset(&clock, ATHENA_LOOP_DEFAULT_MAX_DELTA);
 *     for (;;) {
 *         athena_loop_clock_tick(&clock);
 *         for (int n = athena_loop_clock_steps(&clock, STEP, 5); n > 0; n--)
 *             update(STEP);
 *         clearScreen(color);
 *         draw(athena_loop_clock_alpha(&clock, STEP));
 *         flipScreen();
 *     }
 */

/* Longest real delta reported, in seconds; stalls (loading) are cut to it. */
#define ATHENA_LOOP_DEFAULT_MAX_DELTA 0.25f
/* Fixed steps run per frame at most; the backlog beyond it is dropped. */
#define ATHENA_LOOP_DEFAULT_MAX_STEPS 5

typedef struct {
    uint64_t last;          /* bus-clock ticks of the previous tick */
    bool started;           /* false until the first tick */
    float max_delta;        /* upper bound of real_delta; <= 0 disables it */
    float time_scale;       /* delta = real_delta * time_scale */
    float real_delta;       /* seconds since the previous tick */
    float delta;            /* scaled seconds since the previous tick */
    double real_elapsed;    /* sum of every real_delta */
    double elapsed;         /* sum of every delta */
    double accumulator;     /* scaled time not yet consumed by fixed steps */
    uint32_t frames;        /* ticks since the reset */
    float fps;              /* frames per second over the last second */
    double fps_time;        /* real time of the current FPS window */
    uint32_t fps_frames;    /* frames of the current FPS window */
} AthenaLoopClock;

/* Restarts the clock. The first tick after a reset reports a delta of zero. */
void athena_loop_clock_reset(AthenaLoopClock *clock, float max_delta);

/* Starts a new frame and returns its scaled delta in seconds. */
float athena_loop_clock_tick(AthenaLoopClock *clock);

/* Seconds elapsed since the last tick: the work time of the current frame. */
float athena_loop_clock_since_tick(const AthenaLoopClock *clock);

/*
 * Adds the scaled delta of the current frame to the accumulator and returns
 * how many fixed steps of `step` seconds it holds, at most `max_steps`. Call
 * it once per tick. A backlog above `max_steps` is dropped.
 */
int athena_loop_clock_steps(AthenaLoopClock *clock, float step, int max_steps);

/* Fraction (0..1) of a fixed step left in the accumulator, for interpolation. */
float athena_loop_clock_alpha(const AthenaLoopClock *clock, float step);

/*
 * Systems: callbacks that modules (tweens, cameras, particles, debug
 * overlays...) register once and that the game loop runs every frame around
 * the application's own update and draw, in this order:
 *
 *     PRE_UPDATE(dt)    once per frame
 *     UPDATE(step)      before each application update: once per frame with
 *                       dt, or once per fixed step
 *     POST_UPDATE(dt)   once per frame, after the updates
 *     PRE_DRAW(alpha)   before the application draw
 *     POST_DRAW(alpha)  after the application draw (overlays)
 *
 * Within a phase, systems run by ascending priority, then in the order they
 * were added. The JavaScript Loop runs them; a C game loop calls
 * athena_loop_systems_run() itself. Main thread only.
 */
typedef enum {
    ATHENA_LOOP_PRE_UPDATE,
    ATHENA_LOOP_UPDATE,
    ATHENA_LOOP_POST_UPDATE,
    ATHENA_LOOP_PRE_DRAW,
    ATHENA_LOOP_POST_DRAW,
    ATHENA_LOOP_PHASE_COUNT
} AthenaLoopPhase;

#define ATHENA_LOOP_PHASE_BIT(phase) (1u << (phase))

/*
 * Runs one phase of a system. `value` is the phase argument described above;
 * for PRE_UPDATE and POST_UPDATE it is the real (unscaled) delta when the
 * system asked for real time. A negative return stops the game loop.
 */
typedef int (*AthenaLoopSystemFunc)(void *opaque, AthenaLoopPhase phase, float value);

typedef struct {
    const char *name;           /* optional and unique; copied */
    int priority;               /* lower runs first */
    uint32_t phases;            /* ATHENA_LOOP_PHASE_BIT() mask of the phases to run */
    bool real_time;             /* PRE/POST_UPDATE get the real delta */
    AthenaLoopSystemFunc func;
    /*
     * Optional; called once the registry no longer uses `opaque`. Removing a
     * system while phases run defers this until the phase ends.
     */
    void (*release)(void *opaque);
    void *opaque;
} AthenaLoopSystemDesc;

/* Error codes of athena_loop_system_add(). */
#define ATHENA_LOOP_SYSTEM_EINVAL (-1)  /* no func, or no phase */
#define ATHENA_LOOP_SYSTEM_EEXIST (-2)  /* name already registered */
#define ATHENA_LOOP_SYSTEM_ENOMEM (-3)

/*
 * Registers a system and returns its id (> 0), or a negative error code. A
 * system added while a phase runs starts with the next phase.
 */
int athena_loop_system_add(const AthenaLoopSystemDesc *desc);

/* Unregisters a system; false when `id` is not registered. */
bool athena_loop_system_remove(int id);

/* Id of the system called `name`, or 0. */
int athena_loop_system_find(const char *name);

/*
 * Copies the ids of the registered systems, in run order, into `ids` (at
 * most `max`) and returns how many systems are registered.
 */
int athena_loop_system_list(int *ids, int max);

/* Description of system `id`, or NULL. Valid until the system is removed. */
const AthenaLoopSystemDesc *athena_loop_system_get(int id);

/*
 * Runs `phase` of every system that asked for it. `real_value` replaces
 * `value` in PRE/POST_UPDATE for real-time systems. Returns 0, or the
 * negative result of the first system that failed, whose id is stored in
 * `failed_id` when not NULL; the remaining systems of the phase do not run.
 */
int athena_loop_systems_run(AthenaLoopPhase phase, float value, float real_value,
    int *failed_id);

/* Removes every system. */
void athena_loop_systems_clear(void);

#endif /* ATHENA_LOOP_H */
