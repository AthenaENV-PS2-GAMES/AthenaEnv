#ifndef ATH_NATIVE_TIMER_H
#define ATH_NATIVE_TIMER_H

#include <stdbool.h>
#include <time.h>

typedef struct AthenaTimer AthenaTimer;

AthenaTimer *athena_timer_core_create(void);
clock_t athena_timer_core_get_time(const AthenaTimer *timer);
void athena_timer_core_pause(AthenaTimer *timer);
void athena_timer_core_resume(AthenaTimer *timer);
void athena_timer_core_reset(AthenaTimer *timer);
void athena_timer_core_set_time(AthenaTimer *timer, clock_t value);
bool athena_timer_core_is_playing(const AthenaTimer *timer);
void athena_timer_core_destroy(AthenaTimer *timer);

#endif /* ATH_NATIVE_TIMER_H */
