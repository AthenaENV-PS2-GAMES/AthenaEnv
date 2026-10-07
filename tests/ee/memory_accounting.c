#include <kernel.h>
#include <malloc.h>
#include <reent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <athena/memory.h>
static unsigned char stacks[4][16384] __attribute__((aligned(128)));
static size_t held[4];
static int ready, go, done, failed;
static void worker(void *arg) {
    int id=(int)arg;
    void *p=malloc(101+id);
    if (!p) { failed=1; held[id]=0; }
    else held[id]=_malloc_usable_size_r(_REENT,p);
    SignalSema(ready);
    WaitSema(go);
    free(p);
    for (int i=0;i<10000;i++) {
        p=malloc(37+(i%257));
        if(!p) { failed=1; break; }
        memset(p,id,37+(i%257));
        RotateThreadReadyQueue(40);
        void *q=realloc(p,1024+(i%127));
        if(!q) { free(p); failed=1; break; }
        p=q;
        RotateThreadReadyQueue(40);
        q=realloc(p,17);
        if(!q) { free(p); failed=1; break; }
        free(q);
        p=calloc(7,19); if(!p) { failed=1; break; } free(p);
        p=memalign(128,257); if(!p) { failed=1; break; } free(p);
    }
    SignalSema(done);
    SleepThread();
}
int main(void) {
    puts("MEMORY_ACCOUNTING_EE: starting 4 native workers");
    ee_sema_t sem={.init_count=0,.max_count=4};
    ready=CreateSema(&sem); go=CreateSema(&sem); done=CreateSema(&sem);
    if(ready<0||go<0||done<0) { puts("MEMORY_ACCOUNTING_EE: FAIL semaphores"); SleepThread(); }
    int threads[4];
    for(int i=0;i<4;i++) {
        ee_thread_t t={0};
        t.func=worker; t.stack=stacks[i]; t.stack_size=sizeof(stacks[i]);
        t.gp_reg=&_gp; t.initial_priority=40;
        threads[i]=CreateThread(&t);
        if(threads[i]<0) { puts("MEMORY_ACCOUNTING_EE: FAIL CreateThread"); SleepThread(); }
    }
    size_t baseline=get_allocs_size();
    for(int i=0;i<4;i++) StartThread(threads[i],(void *)i);
    for(int i=0;i<4;i++) WaitSema(ready);
    size_t sum=0; for(int i=0;i<4;i++) sum+=held[i];
    if(get_allocs_size()!=baseline+sum) failed=1;
    for(int i=0;i<4;i++) SignalSema(go);
    for(int i=0;i<4;i++) WaitSema(done);
    if(get_allocs_size()!=baseline) failed=1;
    int enabled=DIntr();
    size_t final=get_allocs_size();
    /* A nested DIntr must still find interrupts disabled after the getter. */
    if(DIntr()!=0) failed=1;
    if(enabled) EIntr();
    printf("MEMORY_ACCOUNTING_EE: %s baseline=%u final=%u; 4 workers x 10000 rounds\n",
           failed?"FAIL":"PASS",(unsigned)baseline,(unsigned)final);
    SleepThread();
    return 0;
}
