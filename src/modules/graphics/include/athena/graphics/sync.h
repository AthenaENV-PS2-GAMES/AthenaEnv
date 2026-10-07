#ifndef ATHENA_GRAPHICS_SYNC_H
#define ATHENA_GRAPHICS_SYNC_H
#include <stdint.h>
/* Main thread only. Owned GS FINISH serialization: a new request cannot
 * accidentally consume a stale or still-pending FINISH from a previous flip. */
void graphics_finish_begin(void);
void graphics_finish_wait(void);
void graphics_wait_idle(void);
/* Fences for deferred VRAM release: flips that waited for GS FINISH (the
 * double-buffered modes) and completed graphics_wait_idle() calls. A
 * resource last used before stamp S is idle once frames - S >= 2 (one more
 * full frame after the one that may still reference it) or once the idle
 * count changed. */
void graphics_frame_finished(void);
uint32_t graphics_finished_frames(void);
uint32_t graphics_idle_count(void);
#endif
