
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
} AthenaMemory;

static AthenaMemory prog_mem;

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
        prog_mem.allocs_size += block_size(ptr);
    }

    return ptr;
}

void *realloc(void *memblock, size_t size) {
    size_t old_size = memblock ? block_size(memblock) : 0;
    void *ptr = _realloc_r(_REENT, memblock, size);

    if (ptr) {
        prog_mem.allocs_size += block_size(ptr) - old_size;
    } else if (size == 0) {
        /* realloc(p, 0) freed the block; any other NULL left it allocated. */
        prog_mem.allocs_size -= old_size;
    }

    return ptr;
}

void *calloc(size_t number, size_t size) {
    void *ptr = _calloc_r(_REENT, number, size);

    if (ptr) {
        prog_mem.allocs_size += block_size(ptr);
    }

    return ptr;
}

void *memalign(size_t alignment, size_t size) {
    void *ptr = _memalign_r(_REENT, alignment, size);

    if (ptr) {
        prog_mem.allocs_size += block_size(ptr);
    }

    return ptr;
}

void free(void* ptr) {
    if (ptr) {
        prog_mem.allocs_size -= block_size(ptr);
    }

    _free_r(_REENT, ptr);
}


void init_memory_manager() {
    prog_mem.binary_size = (unsigned long)&_end - (unsigned long)&__start;
    /* Linker symbol whose address is the size (MAIN_STACK_SIZE in the Makefile). */
    prog_mem.stack_size = (size_t)&_stack_size;
}

size_t get_binary_size() {  return prog_mem.binary_size;    }
size_t get_allocs_size() {  return prog_mem.allocs_size;    }
size_t get_stack_size() {  return prog_mem.stack_size;    }
size_t get_used_memory() {  return prog_mem.stack_size + prog_mem.allocs_size + prog_mem.binary_size;    }
