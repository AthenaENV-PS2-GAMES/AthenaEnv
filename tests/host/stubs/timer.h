/* Host stub of the PS2SDK EE timer: the test drives the bus-clock counter. */
#pragma once
#include <stdint.h>
#define kBUSCLK 147456000
extern uint64_t host_bus_ticks;
static inline uint64_t GetTimerSystemTime(void) { return host_bus_ticks; }
