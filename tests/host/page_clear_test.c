#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../src/modules/graphics/native/page_clear.h"
static owl_qword memory[8194];
static size_t pages, batches, expected_pages, rows;
static const uint64_t test = 0x12345678, xyoffset = 0xabcdef00;
uint32_t athena_test_dma_address(uintptr_t p) { (void)p; return 0; }
void SyncDCache(void *a,void *b) { (void)a;(void)b; }
void dmaKit_wait(owl_channel c,int n) { (void)c;(void)n; }
void dmaKit_send_chain_ucab(owl_channel c,void *base) {
    assert(c == CHANNEL_VIF1);
    owl_qword *p = base;
    size_t consumed = 0;
    while ((p->dword[0] >> 28 & 7) != DMA_END) {
        size_t total = (p->dword[0] & 0xffff) + 1;
        assert(total >= 12 && total < owl_get_controller()->size);
        size_t count = total - 11;
        assert((p[1].sword[3] & 0xffff) == 9 + count);
        assert((p[7].dword[0] & 0x7fff) == count);
        for (size_t i=0; i<count; i++,pages++) {
            unsigned x=(pages/rows)*64, y=(pages%rows)*32;
            assert(p[8+i].dword[1] == GS_SETREG_XYZ(x<<4,y<<4,0));
            assert(p[8+i].dword[0] == GS_SETREG_XYZ((x+64)<<4,(y+32)<<4,0));
        }
        assert(p[total-2].dword[0] == test);
        assert(p[total-1].dword[0] == xyoffset);
        batches++; consumed += total; p += total;
    }
    assert(consumed < owl_get_controller()->size);
}
static void run(unsigned ring,int width,int height) {
    memset(memory,0xa5,sizeof(memory)); owl_init(memory+1,ring);
    GSCONTEXT ctx={.Width=width,.Height=height,.PrimContext=1};
    pages=batches=0; rows=(height+31)/32;
    expected_pages=((width+63)/64)*rows;
    owl_page_clear(&ctx,0x80102030,test,xyoffset); owl_flush_packet();
    assert(pages == expected_pages);
    assert(batches == (expected_pages+ring/2-13)/(ring/2-12));
    for (unsigned i=0;i<4;i++) {
        assert(memory[0].sword[i]==0xa5a5a5a5);
        assert(memory[ring+1].sword[i]==0xa5a5a5a5);
    }
}
int main(void) {
    for(unsigned ring=2048;ring<=8192;ring*=2) {
        run(ring,640,448); run(ring,1920,1080); run(ring,2048,2048);
        if(ring==2048) run(ring,1408,1472); /* 22*46=1012 valid pages. */
        run(ring,64,32*(ring/2-12)); /* Synthetic exact-capacity layout. */
        run(ring,64,32*(ring/2-11)); /* Synthetic one-page overflow. */
    }
    puts("page_clear: ring boundaries, page order, register restore and canaries passed");
}
