
#include <reent.h>
#include <kernel.h>
#include <malloc.h>
#include <stdbool.h>
#include <athena/memory.h>
#include "ee_tools.h"

typedef struct {
	size_t binary_size;
	size_t allocs_size;
	size_t stack_size;
	size_t allocs_peak;
	size_t allocation_failures;
} AthenaMemory;

static AthenaMemory prog_mem;

/* EE threads share one CPU. Mask interrupts only for the counter update;
 * never call the allocator or a blocking SDK function in this section.
 * DIntr returns the previous enable state, including already-disabled callers.
 */
static void account_resize(size_t old_size, size_t new_size) {
    int enabled = DIntr();
    prog_mem.allocs_size = prog_mem.allocs_size - old_size + new_size;
    if (prog_mem.allocs_size > prog_mem.allocs_peak)
        prog_mem.allocs_peak = prog_mem.allocs_size;
    if (enabled) EIntr();
}

static void account_failure(void) {
    int enabled = DIntr();
    prog_mem.allocation_failures++;
    if (enabled) EIntr();
}

/*
 * Size of an allocated block. The chunk header word also holds the
 * allocator's flag bits (PREV_INUSE changes when a neighbour is freed), so it
 * would not match between malloc and free and the counter would drift.
 */
static size_t block_size(void *ptr) {
    return _malloc_usable_size_r(_REENT, ptr);
}

void *malloc(size_t size) {
    void *ptr = _malloc_r(_REENT, size);

    if (ptr) {
        account_resize(0, block_size(ptr));
    } else if (size) {
        account_failure();
    }

    return ptr;
}

void *realloc(void *memblock, size_t size) {
    size_t old_size = memblock ? block_size(memblock) : 0;
    void *ptr = _realloc_r(_REENT, memblock, size);

    if (ptr) {
        account_resize(old_size, block_size(ptr));
    } else if (size == 0) {
        /* realloc(p, 0) freed the block; any other NULL left it allocated. */
        account_resize(old_size, 0);
    } else {
        account_failure();
    }

    return ptr;
}

void *calloc(size_t number, size_t size) {
    void *ptr = _calloc_r(_REENT, number, size);

    if (ptr) {
        account_resize(0, block_size(ptr));
    } else if (number && size) {
        account_failure();
    }

    return ptr;
}

void *memalign(size_t alignment, size_t size) {
    void *ptr = _memalign_r(_REENT, alignment, size);

    if (ptr) {
        account_resize(0, block_size(ptr));
    } else if (size) {
        account_failure();
    }

    return ptr;
}

void free(void* ptr) {
    if (ptr) {
        account_resize(block_size(ptr), 0);
    }

    _free_r(_REENT, ptr);
}


void init_memory_manager() {
    prog_mem.binary_size = (unsigned long)&_end - (unsigned long)&__start;
    /* Linker symbol whose address is the size (MAIN_STACK_SIZE in the Makefile). */
    prog_mem.stack_size = (size_t)&_stack_size;
}

size_t get_binary_size() {  return prog_mem.binary_size;    }
size_t get_allocs_size() {
    int enabled = DIntr();
    size_t size = prog_mem.allocs_size;
    if (enabled) EIntr();
    return size;
}
size_t get_stack_size() {  return prog_mem.stack_size;    }
size_t get_used_memory() {  return prog_mem.stack_size + get_allocs_size() + prog_mem.binary_size;    }

size_t get_allocs_peak() {
    int enabled = DIntr();
    size_t peak = prog_mem.allocs_peak;
    if (enabled) EIntr();
    return peak;
}

size_t get_allocation_failures() {
    int enabled = DIntr();
    size_t failures = prog_mem.allocation_failures;
    if (enabled) EIntr();
    return failures;
}

void get_memory_stats(AthenaMemoryStats *stats) {
    if (!stats) return;
    /* mallinfo acquires newlib's recursive heap lock. Do not mask interrupts
     * around it; it can wait for the lock owner. */
    struct mallinfo heap = _mallinfo_r(_REENT);
    int enabled = DIntr();
    stats->allocs_size = prog_mem.allocs_size;
    stats->allocs_peak = prog_mem.allocs_peak;
    stats->allocation_failures = prog_mem.allocation_failures;
    if (enabled) EIntr();
    stats->heap_reserved = heap.arena + heap.hblkhd;
    stats->heap_allocated = heap.uordblks;
    stats->heap_free = heap.fordblks;
    stats->heap_free_chunks = heap.ordblks;
    stats->heap_top_free = heap.keepcost > heap.fordblks ? heap.fordblks : heap.keepcost;
    stats->heap_non_top_free = heap.fordblks - stats->heap_top_free;
}
