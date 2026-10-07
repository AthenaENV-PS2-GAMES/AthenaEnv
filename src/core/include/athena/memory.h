#ifndef ATHENA_MEMORY_H
#define ATHENA_MEMORY_H

#include <stddef.h>


extern char __start;
extern char _end;
/* Main thread stack size: the value is the symbol's address. */
extern char _stack_size;

typedef struct {
    size_t allocs_size;           /* usable bytes live in Athena's wrappers */
    size_t allocs_peak;
    size_t allocation_failures;
    size_t heap_reserved;         /* newlib arena + mapped regions */
    size_t heap_allocated;        /* newlib in-use chunks, including headers */
    size_t heap_free;             /* bytes in free chunks */
    size_t heap_free_chunks;
    size_t heap_top_free;         /* releasable top chunk */
    size_t heap_non_top_free;     /* free bytes outside top chunk */
} AthenaMemoryStats;

void init_memory_manager();
void get_memory_stats(AthenaMemoryStats *stats);

size_t get_binary_size();
/* Thread-safe snapshot of allocator usable bytes. Heap wrappers are thread-only:
 * the SDK heap lock may block; never allocate/free in interrupt handlers. */
size_t get_allocs_size();
size_t get_allocs_peak();
size_t get_allocation_failures();
size_t get_stack_size();
size_t get_used_memory();
#endif /* ATHENA_MEMORY_H */
