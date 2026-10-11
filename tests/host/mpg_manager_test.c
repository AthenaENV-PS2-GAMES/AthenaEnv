#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <athena/graphics/owl_packet.h>
#include <athena/graphics/mpg_manager.h>
static owl_qword memory[2050];
static uint32_t code[4096] __attribute__((aligned(16)));
static const void *addresses[1024];
static unsigned tokens, tags, instructions, waits;
static uint32_t expected_dest;
static const uint32_t *expected_src;
uint32_t athena_test_dma_address(uintptr_t p) {
    assert(!(p&15) && tokens<1024); addresses[tokens]=(const void *)p; return ++tokens;
}
void SyncDCache(void *a,void *b) { (void)a;(void)b; }
void dmaKit_wait(owl_channel c,int n) { (void)c;(void)n; waits++; }
void dmaKit_send_chain_ucab(owl_channel c,void *base) {
    assert(c==CHANNEL_VIF0 || c==CHANNEL_VIF1);
    owl_qword *p=base;
    size_t used=0;
    while ((p->dword[0]>>28&7)!=DMA_END) {
        assert((p->dword[0]>>28&7)==DMA_REF);
        unsigned n=(p->sword[3]>>16)&255; if(!n) n=256;
        assert((p->sword[3]>>24&127)==VIF_MPG);
        assert((p->sword[3]&0xffff)==expected_dest);
        assert((p->dword[0]&0xffff)==n/2);
        assert(addresses[p->sword[1]-1]==expected_src);
        const uint32_t *payload=addresses[p->sword[1]-1];
        assert(payload[0]==0 && payload[n*2-1]==0);
        expected_src+=n*2; expected_dest+=n; instructions+=n; tags++; p++; used++;
    }
    assert(used<owl_get_controller()->size);
}
static void upload(unsigned words,int unit) {
    vu_mpg *p=vu_mpg_load_buffer(code,words,unit,false); assert(p);
    expected_dest=vu_mpg_preload(p,true); expected_src=code;
    instructions=tags=0; owl_flush_packet();
    assert(instructions==words/2 && tags==(words/2+255)/256);
    unsigned old=tokens; assert(vu_mpg_preload(p,true)>=0); assert(tokens==old);
    vu_mpg_unload(p);
}
int main(void) {
    memset(memory,0xa5,sizeof(memory)); owl_init(memory+1,2048);
    assert(!vu_mpg_load_buffer(NULL,4,1,false));
    assert(!vu_mpg_load_buffer(code+1,4,1,false));
    for(unsigned n=0;n<4;n++) assert(!vu_mpg_load_buffer(code,n,1,false));
    assert(!vu_mpg_load_buffer(code,4097,1,false));
    assert(!vu_mpg_load_buffer(code,1028,0,false));
    assert(vu_mpg_preload(NULL,true)==-1);
    assert(!vu_mpg_load_file(NULL,1));
    assert(!vu_mpg_load_buffer(code,4,-1,false));
    assert(!vu_mpg_load_buffer(code,4,2,false));
    upload(4,1); upload(512,1); upload(516,1); upload(1024,0); upload(4096,1);
    /* Program slots remain non-overlapping after removing a middle entry. */
    vu_mpg *a=vu_mpg_load_buffer(code,512,1,false);
    vu_mpg *b=vu_mpg_load_buffer(code,512,1,false);
    vu_mpg *c=vu_mpg_load_buffer(code,512,1,false);
    expected_src=code; expected_dest=vu_mpg_preload(a,true); owl_flush_packet();
    unsigned addr_a=expected_dest-256;
    expected_src=code; expected_dest=vu_mpg_preload(b,true); owl_flush_packet();
    vu_mpg_unload(a);
    unsigned cached=tokens; assert(vu_mpg_preload(b,true)>=0); assert(tokens==cached);
    expected_src=code; expected_dest=vu_mpg_preload(c,true);
    assert(expected_dest>=addr_a+512); owl_flush_packet();
    vu_mpg_unload(b); vu_mpg_unload(c);
    /* Reset on code-memory exhaustion, including one full-size program. */
    upload(4096,1); upload(4096,1);
    /* Unload submits/waits even when REF is still in the authored ring. */
    a=vu_mpg_load_buffer(code,4,1,false); expected_src=code;
    expected_dest=vu_mpg_preload(a,true); unsigned before=waits;
    vu_mpg_unload(a); assert(waits>before);
    vu_mpg *slots[17];
    for(unsigned i=0;i<17;i++) {
        slots[i]=vu_mpg_load_buffer(code,4,0,false); assert(slots[i]);
        expected_src=code; int address=vu_mpg_preload(slots[i],true);
        if(i==16) assert(address==0);
        expected_dest=address; owl_flush_packet();
    }
    for(unsigned i=0;i<17;i++) vu_mpg_unload(slots[i]);
    /* A tiny ring forces flushes between REF tags within one upload. */
    owl_init(memory+1,8); expected_src=code; expected_dest=0; instructions=tags=0;
    a=vu_mpg_load_buffer(code,4096,1,false);
    assert(vu_mpg_preload(a,true)==0); owl_flush_packet();
    assert(instructions==2048 && tags==8); vu_mpg_unload(a);
    char path[]="/tmp/athena-mpg-XXXXXX"; int fd=mkstemp(path); assert(fd>=0);
    assert(write(fd,code,32)==32); close(fd);
    a=vu_mpg_load_file(path,1); assert(a && a->size==8 && a->qwc==4);
    expected_src=a->code; expected_dest=0;
    assert(vu_mpg_preload(a,true)==0);
    vu_mpg_unload(a); /* REF payload inspected before the owned buffer is freed. */
    FILE *bad=fopen(path,"wb"); assert(bad); assert(fwrite(code,1,15,bad)==15); fclose(bad);
    assert(!vu_mpg_load_file(path,1)); unlink(path);
    for(unsigned i=0;i<4;i++) {
        assert(memory[0].sword[i]==0xa5a5a5a5);
        assert(memory[2049].sword[i]==0xa5a5a5a5);
    }
    puts("mpg_manager: units, MPG boundaries, full VU memory, cache holes and DMA lifetime passed");
}
