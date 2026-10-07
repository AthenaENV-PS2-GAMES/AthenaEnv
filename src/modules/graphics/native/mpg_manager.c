#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include <athena/graphics/owl_packet.h>
#include <athena/graphics/mpg_manager.h>

struct vu_mpg_cache {
    vu_mpg *entries[MPG_CACHE_SIZE];
    uint32_t dests[MPG_CACHE_SIZE];
};

static uint32_t vu_code_qwc[VECTOR_UNIT_SIZE] = { VU0_SIZE / 8, VU1_SIZE / 8 };
#if defined(__mips__)
static uint32_t vu_code_mem_map[VECTOR_UNIT_SIZE] = { VU0_CODE, VU1_CODE };
#endif

static uint32_t vu_code_qwc_used[VECTOR_UNIT_SIZE] = { 0, 0 };

static struct vu_mpg_cache mpg_cache[VECTOR_UNIT_SIZE] = {
    { .entries = { NULL }, .dests = { 0 } },
    { .entries = { NULL }, .dests = { 0 } }
};

static uint32_t vu1_static_version = 1;

/* Code size is in uint32_t words; VIF MPG addresses/counts are 64-bit
 * instructions. Require whole DMA quadwords so REF never reads padding
 * outside the caller-owned buffer. One MPG command transfers at most 256. */
static void vu_mpg_upload(vu_mpg *mpg, uint32_t dst) {
    uint32_t count = mpg->qwc;
    uint32_t *src = mpg->code;
    SyncDCache(src, (unsigned char *)src + mpg->size * sizeof(*src));
    while (count) {
        uint32_t n = count > 256 ? 256 : count;
        owl_packet *packet = owl_query_packet((owl_channel)mpg->dst, 1);
        owl_add_tag(packet,
            VIF_CODE(0, 0, VIF_NOP, 0) |
                (uint64_t)VIF_CODE(dst, n & 0xff, VIF_MPG, 0) << 32,
            DMA_TAG(n / 2, 0, DMA_REF, 0, src, 0));
        mpg->dma_generation = owl_flush_generation();
        src += n * 2;
        dst += n;
        count -= n;
    }
}

vu_mpg *vu_mpg_load_buffer(void *ptr, uint32_t size, int dst, bool autofree) {
    vu_mpg *mpg;

    if (!ptr || ((uintptr_t)ptr & 15) || !size || (size & 3) ||
        dst < 0 || dst >= VECTOR_UNIT_SIZE || size / 2 > vu_code_qwc[dst])
        return NULL;
    mpg = (vu_mpg *)malloc(sizeof(vu_mpg));
    if (!mpg)
        return NULL;

    mpg->code = ptr;
    mpg->size = size;
    mpg->free = autofree;
    mpg->qwc = size / 2;
    mpg->dma_generation = 0;
    mpg->dst = dst;

    return mpg;
}

vu_mpg *vu_mpg_load_file(const char *path, int dst) {
    if (!path || dst < 0 || dst >= VECTOR_UNIT_SIZE) return NULL;
    FILE *f = fopen(path, "rb");
    long size;
    void *ptr;
    vu_mpg *mpg;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) < 0 || (size = ftell(f)) <= 0 ||
        (size & 15) || (unsigned long)size > vu_code_qwc[dst] * 8 ||
        fseek(f, 0, SEEK_SET) < 0) {
        fclose(f);
        return NULL;
    }
    ptr = memalign(16, size);
    if (!ptr) {
        fclose(f);
        return NULL;
    }
    if (fread(ptr, 1, size, f) != (size_t)size) {
        free(ptr);
        fclose(f);
        return NULL;
    }
    fclose(f);

    mpg = vu_mpg_load_buffer(ptr, (uint32_t)size / sizeof(uint32_t), dst, true);
    if (!mpg)
        free(ptr);
    return mpg;
}

void vu_mpg_unload(vu_mpg *mpg) {
    int i;

    if (!mpg)
        return;
    if (mpg->dma_generation) owl_wait_generation(mpg->dma_generation);
    /*
     * Forget the cache slot so a later allocation at this address is not
     * mistaken for it. Its code space is not reclaimed: addresses are
     * assigned linearly and later programs may sit above it.
     */
    for (i = 0; i < MPG_CACHE_SIZE; i++) {
        if (mpg_cache[mpg->dst].entries[i] == mpg)
            mpg_cache[mpg->dst].entries[i] = NULL;
    }
    if (mpg->free)
        free(mpg->code);

    free(mpg);
}

static void vu_mpg_cycle_cache(int idx, int dst) {
    if (!idx) return;

    vu_mpg *tmp = mpg_cache[dst].entries[idx - 1];
    mpg_cache[dst].entries[idx - 1] = mpg_cache[dst].entries[idx];
    mpg_cache[dst].entries[idx] = tmp;

    int tmp_dest = mpg_cache[dst].dests[idx - 1];
    mpg_cache[dst].dests[idx - 1] = mpg_cache[dst].dests[idx];
    mpg_cache[dst].dests[idx] = tmp_dest;
}

int vu_mpg_preload(vu_mpg *mpg, bool dma_transfer) {
    if (!mpg || mpg->dst < 0 || mpg->dst >= VECTOR_UNIT_SIZE ||
        !mpg->qwc || mpg->qwc > vu_code_qwc[mpg->dst]) return -1;
    /* Direct writes to VU1 can race execution; only VU0 has a direct path. */
    if (!dma_transfer && mpg->dst != VECTOR_UNIT_0) return -1;
#if !defined(__mips__)
    if (!dma_transfer) return -1; /* MMIO is unavailable on host. */
#endif
    int i;
    for (i = 0; i < MPG_CACHE_SIZE; i++) {
        if (mpg_cache[mpg->dst].entries[i] == mpg) {
            int address = mpg_cache[mpg->dst].dests[i];
            vu_mpg_cycle_cache(i, mpg->dst);
            return address;
        }
    }
    for (i = 0; i < MPG_CACHE_SIZE && mpg_cache[mpg->dst].entries[i]; i++) {}
    /* Repack on exhaustion rather than subtracting arbitrary holes from
     * the high-water mark, which can overlap still-resident programs. */
    if (i == MPG_CACHE_SIZE ||
        mpg->qwc > vu_code_qwc[mpg->dst] - vu_code_qwc_used[mpg->dst]) {
        memset(&mpg_cache[mpg->dst], 0, sizeof(mpg_cache[mpg->dst]));
        vu_code_qwc_used[mpg->dst] = 0;
        i = 0;
    }
    int mpg_addr = vu_code_qwc_used[mpg->dst];
    mpg_cache[mpg->dst].entries[i] = mpg;
    mpg_cache[mpg->dst].dests[i] = mpg_addr;
    vu_code_qwc_used[mpg->dst] += mpg->qwc;

    if (dma_transfer) {
        vu_mpg_upload(mpg, mpg_addr);
    } else {
#if defined(__mips__)
        memcpy((void *)(vu_code_mem_map[mpg->dst] + (mpg_addr << 3)), mpg->code, mpg->qwc << 3);
        asm __volatile__ ( // Direct copies are only used for VU0: preload CMSAR0.
            "ctc2.i       %0,	  $vi27	    \n"
            :
            : "r" (mpg_addr)
            :
        );
#endif
    }

    return mpg_addr;
}

void vu1_invalidate_static_data(void) {
    vu1_static_version++;
    if (vu1_static_version == 0)
        vu1_static_version = 1;
}

uint32_t vu1_static_data_version(void) {
    return vu1_static_version;
}
