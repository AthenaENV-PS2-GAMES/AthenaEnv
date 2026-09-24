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

static uint32_t vu_code_qwc[VECTOR_UNIT_SIZE] = { VU0_QWC, VU1_QWC };
static uint32_t vu_code_mem_map[VECTOR_UNIT_SIZE] = { VU0_CODE, VU1_CODE };

static uint32_t vu_code_qwc_used[VECTOR_UNIT_SIZE] = { 0, 0 };

static struct vu_mpg_cache mpg_cache[VECTOR_UNIT_SIZE] = {
    { .entries = { NULL }, .dests = { 0 } },
    { .entries = { NULL }, .dests = { 0 } }
};

static uint32_t vu1_static_version = 1;

static inline uint32_t mpg_count_instr(uint32_t size) {
    uint32_t count = size / 2;
    if (count & 1)
        count++;

    return count;
}

static inline uint32_t mpg_packet_size(uint32_t size) {
    return (size >> 8);
}

static void vu_mpg_upload(void *src, uint32_t dst, uint32_t qwc, int vu) {
    uint32_t dest = dst;
    uint32_t count = qwc;

    owl_packet *internal_packet = owl_query_packet(vu, mpg_packet_size(count) + 1);

    uint32_t *l_start = (uint32_t *)src;

    while (count > 0) {
        uint16_t curr_count = count > 256 ? 256 : count;

        owl_add_tag(internal_packet,
            (VIF_CODE(0, 0, VIF_NOP, 0) |
                (uint64_t)VIF_CODE(dest, curr_count & 0xFF, VIF_MPG, 0) << 32),
            DMA_TAG(curr_count / 2, 0, DMA_REF, 0, (const u128 *)l_start, 0));

        l_start += curr_count * 2;
        count -= curr_count;
        dest += curr_count;
    }
}

vu_mpg *vu_mpg_load_buffer(void *ptr, uint32_t size, int dst, bool autofree) {
    vu_mpg *mpg;

    if (!ptr || dst < 0 || dst >= VECTOR_UNIT_SIZE)
        return NULL;
    mpg = (vu_mpg *)malloc(sizeof(vu_mpg));
    if (!mpg)
        return NULL;

    mpg->code = ptr;
    mpg->size = size;
    mpg->free = autofree;
    mpg->qwc = mpg_count_instr(size);
    mpg->dst = dst;

    return mpg;
}

vu_mpg *vu_mpg_load_file(const char *path, int dst) {
    FILE *f = fopen(path, "rb");
    long size;
    void *ptr;
    vu_mpg *mpg;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) < 0 || (size = ftell(f)) <= 0 ||
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

    mpg = vu_mpg_load_buffer(ptr, size, dst, true);
    if (!mpg)
        free(ptr);
    return mpg;
}

void vu_mpg_unload(vu_mpg *mpg) {
    int i;

    if (!mpg)
        return;
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
    int mpg_addr = 0;

    int i = 0;
    for (; i < MPG_CACHE_SIZE; i++) {
        if (mpg == mpg_cache[mpg->dst].entries[i]) {
            mpg_addr = mpg_cache[mpg->dst].dests[i];
            vu_mpg_cycle_cache(i, mpg->dst);
            return mpg_addr;
        }

        if (!mpg_cache[mpg->dst].entries[i]) {
            break;
        }
    }

    i -= (i < MPG_CACHE_SIZE ? 0 : 1);

    vu_mpg *tmp = mpg_cache[mpg->dst].entries[i];
    for (; (i >= 0 && (vu_code_qwc_used[mpg->dst] + mpg->qwc) > vu_code_qwc[mpg->dst]); i--, tmp = mpg_cache[mpg->dst].entries[i]) {
        if (tmp) {
            vu_code_qwc_used[mpg->dst] -= tmp->qwc;
            mpg_cache[mpg->dst].entries[i] = NULL;
        }
    }

    if (i < 0)
        i++;

    mpg_cache[mpg->dst].entries[i] = mpg;
    mpg_addr = mpg_cache[mpg->dst].dests[i] = vu_code_qwc_used[mpg->dst];

    vu_code_qwc_used[mpg->dst] += mpg->qwc;

    if (dma_transfer) {
        vu_mpg_upload(mpg->code, mpg_cache[mpg->dst].dests[i], mpg->qwc, mpg->dst);
    } else {
        memcpy((void *)(vu_code_mem_map[mpg->dst] + (mpg_addr << 4)), mpg->code, mpg->qwc << 3);
        asm __volatile__ ( // Direct copies are only used for VU0: preload CMSAR0.
            "ctc2.i       %0,	  $vi27	    \n"
            :
            : "r" (mpg_addr)
            :
        );
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
