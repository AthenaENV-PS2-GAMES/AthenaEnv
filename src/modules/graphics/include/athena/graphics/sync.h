#ifndef ATHENA_GRAPHICS_SYNC_H
#define ATHENA_GRAPHICS_SYNC_H
/* Main thread only. Owned GS FINISH serialization: a new request cannot
 * accidentally consume a stale or still-pending FINISH from a previous flip. */
void graphics_finish_begin(void);
void graphics_finish_wait(void);
void graphics_wait_idle(void);
#endif
