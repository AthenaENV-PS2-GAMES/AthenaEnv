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

#endif /* ATHENA_LOOP_H */
