#include <stdlib.h>
#include <time.h>

#include "timer.h"

struct AthenaTimer {
    bool playing;
    clock_t tick;
};

AthenaTimer *athena_timer_core_create(void) {
    AthenaTimer *timer = malloc(sizeof(*timer));
    if (!timer) return NULL;

    timer->playing = true;
    timer->tick = clock();
    return timer;
}

clock_t athena_timer_core_get_time(const AthenaTimer *timer) {
    if (timer->playing) return clock() - timer->tick;
    return timer->tick;
}

void athena_timer_core_pause(AthenaTimer *timer) {
    if (!timer->playing) return;
    timer->tick = clock() - timer->tick;
    timer->playing = false;
}

void athena_timer_core_resume(AthenaTimer *timer) {
    if (timer->playing) return;
    timer->tick = clock() - timer->tick;
    timer->playing = true;
}

void athena_timer_core_reset(AthenaTimer *timer) {
    timer->tick = timer->playing ? clock() : 0;
}

void athena_timer_core_set_time(AthenaTimer *timer, clock_t value) {
    timer->tick = timer->playing ? clock() - value : value;
}

bool athena_timer_core_is_playing(const AthenaTimer *timer) {
    return timer->playing;
}

void athena_timer_core_destroy(AthenaTimer *timer) {
    free(timer);
}
