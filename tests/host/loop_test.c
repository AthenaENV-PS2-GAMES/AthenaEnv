/* Host test of the Loop module clock: deltas, clamping, time scale, fixed steps and FPS. */
#include <math.h>
#include <stdio.h>

#include "loop.c"

uint64_t host_bus_ticks = 1000;

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int near(double a, double b) { return fabs(a - b) < 1e-5; }

static void advance(double seconds) { host_bus_ticks += (uint64_t)(seconds * kBUSCLK); }

int main(void) {
    AthenaLoopClock clock;
    const float step = 1.0f / 60.0f;
    int steps;

    athena_loop_clock_reset(&clock, ATHENA_LOOP_DEFAULT_MAX_DELTA);
    CHECK(athena_loop_clock_since_tick(&clock) == 0.0f, "since tick before the first tick");
    CHECK(athena_loop_clock_tick(&clock) == 0.0f, "first delta %f", clock.delta);
    CHECK(clock.frames == 1, "frames %u", clock.frames);

    advance(0.016);
    CHECK(near(athena_loop_clock_tick(&clock), 0.016), "delta %f", clock.delta);
    advance(0.004);
    CHECK(near(athena_loop_clock_since_tick(&clock), 0.004), "since tick %f",
        athena_loop_clock_since_tick(&clock));

    /* A stall is cut to max_delta; zero disables the cut. */
    advance(1.996);
    CHECK(near(athena_loop_clock_tick(&clock), 0.25), "clamped delta %f", clock.delta);
    clock.max_delta = 0.0f;
    advance(0.5);
    CHECK(near(athena_loop_clock_tick(&clock), 0.5), "unclamped delta %f", clock.delta);
    CHECK(near(clock.elapsed, 0.016 + 0.25 + 0.5), "elapsed %f", clock.elapsed);

    /* Time scale changes the scaled time only. */
    clock.time_scale = 0.5f;
    advance(0.02);
    CHECK(near(athena_loop_clock_tick(&clock), 0.01), "scaled delta %f", clock.delta);
    CHECK(near(clock.real_delta, 0.02), "real delta %f", clock.real_delta);
    CHECK(near(clock.real_elapsed - clock.elapsed, 0.01), "elapsed %f real %f",
        clock.elapsed, clock.real_elapsed);
    clock.time_scale = 0.0f;
    advance(0.02);
    CHECK(athena_loop_clock_tick(&clock) == 0.0f, "paused delta %f", clock.delta);

    /* Fixed steps: the remainder carries over to the next frame. */
    athena_loop_clock_reset(&clock, ATHENA_LOOP_DEFAULT_MAX_DELTA);
    athena_loop_clock_tick(&clock);
    CHECK(athena_loop_clock_steps(&clock, step, 5) == 0, "steps on the first frame");
    advance(0.025);
    athena_loop_clock_tick(&clock);
    steps = athena_loop_clock_steps(&clock, step, 5);
    CHECK(steps == 1, "steps %d", steps);
    CHECK(near(athena_loop_clock_alpha(&clock, step), (0.025 - step) / step), "alpha %f",
        athena_loop_clock_alpha(&clock, step));
    advance(0.010);
    athena_loop_clock_tick(&clock);
    steps = athena_loop_clock_steps(&clock, step, 5);
    CHECK(steps == 1, "carried steps %d", steps);

    /* A backlog above max_steps is dropped, keeping less than one step. */
    advance(0.2);
    athena_loop_clock_tick(&clock);
    steps = athena_loop_clock_steps(&clock, step, 5);
    CHECK(steps == 5, "capped steps %d", steps);
    CHECK(clock.accumulator >= 0.0 && clock.accumulator < step, "accumulator %f", clock.accumulator);
    CHECK(athena_loop_clock_steps(&clock, 0.0f, 5) == 0, "zero step");
    CHECK(athena_loop_clock_alpha(&clock, 0.0f) == 1.0f, "alpha without a step");

    /* FPS over one-second windows, unclamped. */
    athena_loop_clock_reset(&clock, ATHENA_LOOP_DEFAULT_MAX_DELTA);
    athena_loop_clock_tick(&clock);
    for (int i = 0; i < 60; i++) {
        advance(1.0 / 50.0);
        athena_loop_clock_tick(&clock);
    }
    CHECK(fabs(clock.fps - 50.0f) < 0.5f, "fps %f", clock.fps);

    printf("loop: %d failures\n", failures);
    return failures != 0;
}
