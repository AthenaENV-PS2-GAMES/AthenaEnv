#include <math.h>
#include <timer.h>

#include <athena/loop.h>

void athena_loop_clock_reset(AthenaLoopClock *clock, float max_delta) {
    clock->last = 0;
    clock->started = false;
    clock->max_delta = max_delta;
    clock->time_scale = 1.0f;
    clock->real_delta = 0.0f;
    clock->delta = 0.0f;
    clock->real_elapsed = 0.0;
    clock->elapsed = 0.0;
    clock->accumulator = 0.0;
    clock->frames = 0;
    clock->fps = 0.0f;
    clock->fps_time = 0.0;
    clock->fps_frames = 0;
}

float athena_loop_clock_tick(AthenaLoopClock *clock) {
    uint64_t now = GetTimerSystemTime();
    float real_delta = 0.0f;

    if (clock->started) {
        double seconds = (double)(now - clock->last) / (double)kBUSCLK;

        /* The FPS window uses the unclamped time, so stalls show up in it. */
        clock->fps_time += seconds;
        clock->fps_frames++;
        if (clock->fps_time >= 1.0) {
            clock->fps = (float)(clock->fps_frames / clock->fps_time);
            clock->fps_time = 0.0;
            clock->fps_frames = 0;
        }

        real_delta = (float)seconds;
        if (clock->max_delta > 0.0f && real_delta > clock->max_delta)
            real_delta = clock->max_delta;
    }

    clock->started = true;
    clock->last = now;
    clock->real_delta = real_delta;
    clock->delta = real_delta * clock->time_scale;
    clock->real_elapsed += clock->real_delta;
    clock->elapsed += clock->delta;
    clock->frames++;
    return clock->delta;
}

float athena_loop_clock_since_tick(const AthenaLoopClock *clock) {
    if (!clock->started)
        return 0.0f;
    return (float)((double)(GetTimerSystemTime() - clock->last) / (double)kBUSCLK);
}

int athena_loop_clock_steps(AthenaLoopClock *clock, float step, int max_steps) {
    int steps;

    if (step <= 0.0f || max_steps <= 0)
        return 0;

    clock->accumulator += clock->delta;
    steps = (int)(clock->accumulator / step);
    if (steps > max_steps) {
        steps = max_steps;
        clock->accumulator = fmod(clock->accumulator, step);
    } else {
        clock->accumulator -= steps * (double)step;
    }
    return steps;
}

float athena_loop_clock_alpha(const AthenaLoopClock *clock, float step) {
    float alpha;

    if (step <= 0.0f)
        return 1.0f;
    alpha = (float)(clock->accumulator / step);
    return alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
}
