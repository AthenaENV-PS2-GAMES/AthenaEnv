#ifndef ATHENA_MEMORY_RETIREMENT_H
#define ATHENA_MEMORY_RETIREMENT_H
#include <stddef.h>
#include <stdint.h>
typedef struct { size_t resources, cpu_payload, ready_resources; } AthenaTextureRetirementStats;
typedef struct { size_t references, pending_references, capacity_bytes; } AthenaMeshRetirementStats;
/* Main-thread observers; never collect, submit or wait. */
void athena_texture3d_retirement_stats(AthenaTextureRetirementStats *out);
void athena_render3d_retirement_stats(AthenaMeshRetirementStats *out);
#endif
