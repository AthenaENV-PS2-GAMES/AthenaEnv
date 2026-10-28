
#include <reent.h>
#include <kernel.h>
#include <malloc.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
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

/* Fixed-size, bounded-probe metadata. No allocation/SDK calls while masked.
 * Dropped records make category totals lower bounds; never fake completeness.
 * Empty slots are reusable: lookup scans all probes (no tombstones needed). */
#if ATHENA_MEMORY_DIAGNOSTICS
#define MEMORY_RECORDS 8192u
#define MEMORY_PROBES 32u
typedef struct { void *ptr; size_t requested, usable; unsigned category; } MemoryRecord;
static MemoryRecord records[MEMORY_RECORDS];
static AthenaMemoryDiagnostics diagnostics;
static unsigned record_hash(void *ptr) {
    uintptr_t v=(uintptr_t)ptr >> 4;
    uint32_t h=(uint32_t)v;
    h^=h>>16; h*=0x7feb352du; h^=h>>15; h*=0x846ca68bu; h^=h>>16;
    return h & (MEMORY_RECORDS-1);
}
static unsigned record_remove(void *ptr) {
    unsigned start=record_hash(ptr);
    for(unsigned i=0;i<MEMORY_PROBES;i++) {
        MemoryRecord *r=&records[(start+i*(i+1)/2)&(MEMORY_RECORDS-1)];
        if(r->ptr==ptr) {
            AthenaMemoryCategoryStats *c=&diagnostics.categories[r->category];
            unsigned category=r->category;
            c->requested-=r->requested; c->usable-=r->usable; c->blocks--;
            r->ptr=NULL;
            return category;
        }
    }
    return ATHENA_MEM_CATEGORY_COUNT;
}
static void record_add(void *ptr,size_t requested,size_t usable,unsigned category) {
    unsigned start=record_hash(ptr);
    if(category>=ATHENA_MEM_CATEGORY_COUNT) category=ATHENA_MEM_OTHER;
    for(unsigned i=0;i<MEMORY_PROBES;i++) {
        MemoryRecord *r=&records[(start+i*(i+1)/2)&(MEMORY_RECORDS-1)];
        if(!r->ptr) {
            *r=(MemoryRecord){ptr,requested,usable,category};
            AthenaMemoryCategoryStats *c=&diagnostics.categories[category];
            c->requested+=requested; c->usable+=usable; c->blocks++;
            if(c->requested>c->peak_requested) c->peak_requested=c->requested;
            if(c->usable>c->peak_usable) c->peak_usable=c->usable;
            return;
        }
    }
    diagnostics.dropped_records++;
}
#endif
static void account_record(void *old,void *ptr,size_t requested,size_t usable,unsigned category) {
#if ATHENA_MEMORY_DIAGNOSTICS
    int enabled=DIntr();
    unsigned previous=old?record_remove(old):ATHENA_MEM_OTHER;
    if(old && previous==ATHENA_MEM_CATEGORY_COUNT) {
        diagnostics.unmatched_free_calls++; diagnostics.unmatched_free_usable+=usable;
        previous=ATHENA_MEM_OTHER;
    }
    if(category==ATHENA_MEM_OTHER) category=previous;
    if(ptr) record_add(ptr,requested,usable,category);
    if(enabled) EIntr();
#else
    (void)old; (void)ptr; (void)requested; (void)usable; (void)category;
#endif
}
void get_memory_diagnostics(AthenaMemoryDiagnostics *out) {
    if(!out) return;
#if ATHENA_MEMORY_DIAGNOSTICS
    int enabled=DIntr(); *out=diagnostics; if(enabled) EIntr();
    out->enabled=1; out->capacity=MEMORY_RECORDS;
    out->metadata_bytes=sizeof(records)+sizeof(diagnostics);
#else
    memset(out,0,sizeof(*out));
#endif
}

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

void *athena_malloc_tagged(size_t size, AthenaMemoryCategory category) {
    void *ptr = _malloc_r(_REENT, size);

    if (ptr) {
        size_t usable=block_size(ptr);
        account_resize(0, usable);
        account_record(NULL,ptr,size,usable,category);
    } else if (size) {
        account_failure();
    }

    return ptr;
}

void *athena_realloc_tagged(void *memblock, size_t size, AthenaMemoryCategory category) {
    size_t old_size = memblock ? block_size(memblock) : 0;
    #if ATHENA_MEMORY_DIAGNOSTICS
    size_t old_requested=0;
    int old_recorded=0;
    int enabled=DIntr();
    if(memblock) {
        unsigned start=record_hash(memblock);
        for(unsigned i=0;i<MEMORY_PROBES;i++) {
            MemoryRecord *r=&records[(start+i*(i+1)/2)&(MEMORY_RECORDS-1)];
            if(r->ptr==memblock) { old_requested=r->requested; old_recorded=1; break; }
        }
        unsigned previous=record_remove(memblock);
        if(category==ATHENA_MEM_OTHER) category=previous;
    }
    if(enabled) EIntr();
#endif
    void *ptr = _realloc_r(_REENT, memblock, size);

    if (ptr) {
        size_t usable=block_size(ptr);
        account_resize(old_size, usable);
        account_record(NULL,ptr,size,usable,category);
    } else if (size == 0) {
        /* realloc(p, 0) freed the block; any other NULL left it allocated. */
        account_resize(old_size, 0);

    } else {
#if ATHENA_MEMORY_DIAGNOSTICS
        if(memblock && old_recorded) account_record(NULL,memblock,old_requested,old_size,category);
#endif
        account_failure();
    }

    return ptr;
}

void *athena_calloc_tagged(size_t number, size_t size, AthenaMemoryCategory category) {
    void *ptr = _calloc_r(_REENT, number, size);

    if (ptr) {
        size_t usable=block_size(ptr);
        account_resize(0, usable);
        account_record(NULL,ptr,number*size,usable,category);
    } else if (number && size) {
        account_failure();
    }

    return ptr;
}

void *athena_memalign_tagged(size_t alignment, size_t size, AthenaMemoryCategory category) {
    void *ptr = _memalign_r(_REENT, alignment, size);

    if (ptr) {
        size_t usable=block_size(ptr);
        account_resize(0, usable);
        account_record(NULL,ptr,size,usable,category);
    } else if (size) {
        account_failure();
    }

    return ptr;
}

void *malloc(size_t size) { return athena_malloc_tagged(size,ATHENA_MEM_OTHER); }
void *calloc(size_t count,size_t size) { return athena_calloc_tagged(count,size,ATHENA_MEM_OTHER); }
void *memalign(size_t alignment,size_t size) { return athena_memalign_tagged(alignment,size,ATHENA_MEM_OTHER); }
void *realloc(void *ptr,size_t size) { return athena_realloc_tagged(ptr,size,ATHENA_MEM_OTHER); }
/* newlib's strdup() allocates through _malloc_r(), which bypasses the counted wrappers above, while callers release the result with
 * free(): the counter was decremented for a block it never added and `allocs` drifted negative (Phase 5C.6: 1,301 such frees in 6,000
 * COMZ2 frames, all from athena_font_render_destroy, plus 16 from realpath()). Duplicating through malloc() keeps the pair symmetric. */
char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *copy = malloc(n);
    if (copy) memcpy(copy, s, n);
    return copy;
}

void free(void* ptr) {
    if (ptr) {
        size_t usable=block_size(ptr);
        account_resize(usable, 0);
        account_record(ptr,NULL,0,usable,ATHENA_MEM_OTHER);
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
    memset(stats,0,sizeof(*stats));
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
#if defined(__mips__)
    /* Verified installed SDK: _heap_ptr initially &_end; _sbrk checks EndOfHeap.
     * _sbrk(0) is a read-only query. Heap and counters are sequential samples. */
    extern void *_sbrk(ptrdiff_t);
    extern char _ftext;
    stats->physical_ee=GetMemorySize();
    stats->linked_image_span=(uintptr_t)&_end-(uintptr_t)&_ftext;
    stats->heap_start=(uintptr_t)&_end;
    stats->heap_break=(uintptr_t)_sbrk(0);
    stats->heap_limit=(uintptr_t)EndOfHeap();
    if(stats->heap_limit>=stats->heap_start) stats->heap_capacity=stats->heap_limit-stats->heap_start;
    if(stats->heap_limit>=stats->heap_break) stats->heap_uncommitted=stats->heap_limit-stats->heap_break;
#endif
}
