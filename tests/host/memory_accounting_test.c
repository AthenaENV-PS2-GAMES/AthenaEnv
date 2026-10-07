/* Compile the production wrappers with distinct names so libc/pthreads and
 * sanitizers keep their own allocator. The interrupt stub serializes workers. */
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <malloc.h>
#include <stdio.h>
#include <athena/memory.h>
#include <reent.h>
void *ath_malloc(size_t);
void *ath_calloc(size_t,size_t);
void *ath_realloc(void *,size_t);
void *ath_memalign(size_t,size_t);
void ath_free(void *);
char __start, _end, _stack_size;
static pthread_mutex_t critical = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int masked;
static _Thread_local int fail_alloc;
int DIntr(void) {
    if (masked) return 0;
    assert(pthread_mutex_lock(&critical)==0);
    masked=1;
    return 1;
}
int EIntr(void) {
    assert(masked);
    masked=0;
    assert(pthread_mutex_unlock(&critical)==0);
    return 0;
}
void *_malloc_r(void *r,size_t n) { (void)r; assert(!masked); return fail_alloc?NULL:malloc(n); }
void *_calloc_r(void *r,size_t n,size_t s) { (void)r; assert(!masked); return fail_alloc?NULL:calloc(n,s); }
void *_realloc_r(void *r,void *p,size_t n) {
    (void)r; assert(!masked);
    if (!n) { free(p); return NULL; }
    return fail_alloc?NULL:realloc(p,n);
}
void *_memalign_r(void *r,size_t a,size_t n) { (void)r; assert(!masked); return fail_alloc?NULL:memalign(a,n); }
void _free_r(void *r,void *p) { (void)r; assert(!masked); free(p); }
size_t _malloc_usable_size_r(void *r,void *p) { (void)r; assert(!masked); return malloc_usable_size(p); }
struct mallinfo _mallinfo_r(void *r) {
    (void)r; assert(!masked);
    return (struct mallinfo){.arena=1024*1024,.ordblks=7,.hblkhd=4096,
        .uordblks=700,.fordblks=300000,.keepcost=100000};
}
static pthread_barrier_t barrier;
static size_t held[8];
static void *worker(void *arg) {
    size_t id=(uintptr_t)arg;
    void *p=ath_malloc(101+id);
    assert(p);
    held[id]=malloc_usable_size(p);
    pthread_barrier_wait(&barrier);
    pthread_barrier_wait(&barrier);
    ath_free(p);
    for (size_t i=0;i<20000;i++) {
        p=ath_malloc(1+(i+id)%509); assert(p);
        p=ath_realloc(p,1024+(i%127)); assert(p);
        p=ath_realloc(p,17); assert(p);
        ath_free(p);
        p=ath_calloc(7,19); assert(p); ath_free(p);
        p=ath_memalign(128,257); assert(p); ath_free(p);
    }
    return NULL;
}
int main(void) {
    assert(get_allocs_size()==0);
    AthenaMemoryStats stats;
    get_memory_stats(&stats);
    assert(stats.allocs_size==0 && stats.heap_reserved==1024*1024+4096);
    assert(stats.heap_allocated==700 && stats.heap_free==300000);
    assert(stats.heap_free_chunks==7 && stats.heap_top_free==100000);
    assert(stats.heap_non_top_free==200000);
    size_t initial_failures=get_allocation_failures();
    assert(get_allocs_peak()==0);
    void *p=ath_malloc(41); assert(p);
    size_t n=malloc_usable_size(p);
    assert(get_allocs_size()==n);
    assert(get_allocs_peak()>=n);
    fail_alloc=1;
    assert(!ath_realloc(p,4096)); assert(get_allocs_size()==n);
    assert(!ath_malloc(100)); assert(!ath_calloc(2,50));
    assert(!ath_memalign(128,100)); assert(get_allocs_size()==n);
    assert(get_allocation_failures()==initial_failures+4);
    fail_alloc=0;
    assert(!ath_realloc(p,0)); assert(get_allocs_size()==0);
    ath_free(NULL);
    assert(DIntr()==1); assert(get_allocs_size()==0); assert(masked); EIntr();
    pthread_t threads[8];
    assert(pthread_barrier_init(&barrier,NULL,9)==0);
    for(size_t i=0;i<8;i++) assert(pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i)==0);
    pthread_barrier_wait(&barrier);
    n=0; for(size_t i=0;i<8;i++) n+=held[i];
    assert(get_allocs_size()==n);
    pthread_barrier_wait(&barrier);
    for(size_t i=0;i<8;i++) assert(pthread_join(threads[i],NULL)==0);
    assert(get_allocs_size()==0);
    assert(pthread_barrier_destroy(&barrier)==0);
    puts("memory_accounting: 8 workers x 20000 rounds, exact held/live totals, realloc/OOM/disabled interrupts passed");
}
