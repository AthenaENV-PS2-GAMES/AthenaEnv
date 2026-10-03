/* Host stub: DelayThread() sleeps the calling pthread. */
#pragma once
#include <unistd.h>
static inline int DelayThread(int us) { usleep(us); return 0; }
