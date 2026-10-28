#ifndef ATHENA_MEMORY_H
#define ATHENA_MEMORY_H

#include <stddef.h>
#include <stdint.h>


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
    size_t physical_ee, linked_image_span;
    uintptr_t heap_start, heap_break, heap_limit;
    size_t heap_capacity, heap_uncommitted;
    size_t heap_non_top_free;     /* free bytes outside top chunk */
} AthenaMemoryStats;

/* Optional bounded attribution; totals are subsets of allocs_size. */
#ifndef ATHENA_MEMORY_DIAGNOSTICS
#define ATHENA_MEMORY_DIAGNOSTICS 0
#endif
typedef enum {
    ATHENA_MEM_OTHER, ATHENA_MEM_IMAGE, ATHENA_MEM_TEXTURE,
    ATHENA_MEM_GEOMETRY, ATHENA_MEM_FONT, ATHENA_MEM_AUDIO,
    ATHENA_MEM_STACK, ATHENA_MEM_STAGING, ATHENA_MEM_GRAPHICS,
    ATHENA_MEM_CATEGORY_COUNT
} AthenaMemoryCategory;
typedef struct {
    size_t requested, usable, peak_requested, peak_usable, blocks;
} AthenaMemoryCategoryStats;
typedef struct {
    int enabled;
    size_t capacity, metadata_bytes, dropped_records;
    size_t unmatched_free_calls, unmatched_free_usable;
    AthenaMemoryCategoryStats categories[ATHENA_MEM_CATEGORY_COUNT];
} AthenaMemoryDiagnostics;
void get_memory_diagnostics(AthenaMemoryDiagnostics *out);
void *athena_malloc_tagged(size_t size, AthenaMemoryCategory category);
void *athena_calloc_tagged(size_t count, size_t size, AthenaMemoryCategory category);
void *athena_memalign_tagged(size_t alignment, size_t size, AthenaMemoryCategory category);
void *athena_realloc_tagged(void *ptr, size_t size, AthenaMemoryCategory category);
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
