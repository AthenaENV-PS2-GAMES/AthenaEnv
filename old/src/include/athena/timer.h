#ifndef ATHENA_TIMER_H
#define ATHENA_TIMER_H

#include <stdbool.h>
#include <time.h>

typedef struct AthenaTimer {
    bool is_playing;
    clock_t tick;
} AthenaTimer;

AthenaTimer *athena_timer_create(void);
void athena_timer_destroy(AthenaTimer *timer);
clock_t athena_timer_get_time(const AthenaTimer *timer);
void athena_timer_set_time(AthenaTimer *timer, clock_t time);
void athena_timer_pause(AthenaTimer *timer);
void athena_timer_resume(AthenaTimer *timer);
void athena_timer_reset(AthenaTimer *timer);
bool athena_timer_is_playing(const AthenaTimer *timer);

#endif /* ATHENA_TIMER_H */
