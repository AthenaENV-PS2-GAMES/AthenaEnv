/* Host stub of the PS2SDK EE kernel calls sound_stream.c uses. */
#pragma once
#include <pthread.h>
typedef struct { int current_priority; } ee_thread_status_t;
#define THS_DORMANT 0x10
static inline int GetThreadId(void) { return 1; }
/* The script thread runs at the default priority. */
static inline int ReferThreadStatus(int id, ee_thread_status_t *s) { (void)id; s->current_priority = 16; return 0; }
static inline void ExitThread(void) { pthread_exit(0); }
static inline void SyncDCache(void *start, void *end) { (void)start; (void)end; }
