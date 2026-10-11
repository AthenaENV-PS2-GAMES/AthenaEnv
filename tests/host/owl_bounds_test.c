/* Real allocator/writer with a DMA stand-in; check ring limits, not VU execution. */
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <athena/graphics/owl_packet.h>
static owl_qword memory[66];
static unsigned submitted;
uint32_t athena_test_dma_address(uintptr_t address) { (void)address; return 0; }
void SyncDCache(void *a,void *b) { (void)a; (void)b; }
void dmaKit_wait(owl_channel c,int flag) { (void)c; (void)flag; }
void dmaKit_send_chain_ucab(owl_channel c,void *p) {
    assert((unsigned)c<CHANNEL_SIZE);
    assert(p==memory+1 || p==memory+33); submitted++;
}
static void write_qwords(owl_channel c,size_t n) {
    owl_packet *p=owl_query_packet(c,n);
    for(size_t i=0;i<n;i++) owl_add_cnt_tag(p,0,0);
    assert(owl_get_controller()->alloc<=31);
}
static void invalid_reservation(owl_channel c,size_t n) {
    pid_t child=fork(); assert(child>=0);
    if(!child) {
        struct rlimit no_core={0,0}; setrlimit(RLIMIT_CORE,&no_core);
        owl_query_packet(c,n); _exit(0);
    }
    int status; assert(waitpid(child,&status,0)==child);
    assert(WIFSIGNALED(status) && WTERMSIG(status)==SIGABRT);
}
int main(void) {
    memset(memory,0xa5,sizeof(memory));
    owl_init(memory+1,64);
    write_qwords(CHANNEL_VIF1,31); /* Last QW reserved for END. */
    write_qwords(CHANNEL_GIF,31); /* Capacity remains enforced on channel switch. */
    write_qwords(CHANNEL_GIF,1); /* Flush before exceeding the active half. */
    owl_flush_packet(); assert(submitted==3);
    for(unsigned b=0;b<sizeof(owl_qword);b++) {
        assert(memory[0].sword[b/4]==0xa5a5a5a5u);
        assert(memory[65].sword[b/4]==0xa5a5a5a5u);
    }
    invalid_reservation(CHANNEL_VIF1,32);
    invalid_reservation(CHANNEL_GIF,SIZE_MAX);
    invalid_reservation(CHANNEL_SIZE,1);
    invalid_reservation((owl_channel)-1,1);
    puts("owl_bounds: exact capacity, channel switches, overflow and invalid channels passed");
    return 0;
}
