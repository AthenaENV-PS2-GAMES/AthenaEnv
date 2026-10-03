#include <stdlib.h>

#include <athena/timer.h>

AthenaTimer *athena_timer_create(void)
{
    AthenaTimer *timer = malloc(sizeof(*timer));
    if (!timer)
        return NULL;

    timer->tick = clock();
    timer->is_playing = true;
    return timer;
}

void athena_timer_destroy(AthenaTimer *timer)
{
    free(timer);
}

clock_t athena_timer_get_time(const AthenaTimer *timer)
{
    if (timer->is_playing)
        return clock() - timer->tick;
    return timer->tick;
}

void athena_timer_set_time(AthenaTimer *timer, clock_t time)
{
    if (timer->is_playing)
        timer->tick = clock() - time;
    else
        timer->tick = time;
}

void athena_timer_pause(AthenaTimer *timer)
{
    if (timer->is_playing) {
        timer->is_playing = false;
        timer->tick = clock() - timer->tick;
    }
}

void athena_timer_resume(AthenaTimer *timer)
{
    if (!timer->is_playing) {
        timer->is_playing = true;
        timer->tick = clock() - timer->tick;
    }
}

void athena_timer_reset(AthenaTimer *timer)
{
    if (timer->is_playing)
        timer->tick = clock();
    else
        timer->tick = 0;
}

bool athena_timer_is_playing(const AthenaTimer *timer)
{
    return timer->is_playing;
}
