#include <athena/graphics/owl_packet.h>
#include <debug.h>
#include <stdlib.h>
#include <string.h>

owl_controller controller = { 0 };
owl_packet internal_packet = { 0 };
static uint64_t flush_generation = 1;
static uint64_t pending_generation[CHANNEL_SIZE];
static owl_channel buffer_channel[2] = { CHANNEL_SIZE, CHANNEL_SIZE };
static uint64_t buffer_generation[2];

static void *uncached_address(void *ptr) {
#if defined(__mips__)
    return (void *)((uintptr_t)ptr | 0x30000000u);
#else
    return ptr;
#endif
}

void owl_init(void *ptr, size_t size) {
    controller.channel = CHANNEL_SIZE;

    SyncDCache(ptr, (uint8_t *)ptr + size * sizeof(owl_qword));

    controller.base = uncached_address(ptr);
    internal_packet.ptr = controller.base;

    controller.size = size/2;
    controller.alloc = 0;

    controller.context = false;
    /* owl_init is called only with a drained/unused stream. */
    flush_generation = 1;
    memset(pending_generation, 0, sizeof(pending_generation));
    buffer_channel[0] = buffer_channel[1] = CHANNEL_SIZE;
    buffer_generation[0] = buffer_generation[1] = 0;
}

void owl_flush_packet() {
    owl_qword *chain_base;

    if (controller.alloc == 0) {
        return;
    }

    owl_add_end_tag(&internal_packet, 0);

    dmaKit_wait(controller.channel, 0);
    pending_generation[controller.channel] = 0;

    chain_base = controller.base + (controller.context ? controller.size : 0);
    SyncDCache(chain_base, (uint8_t *)internal_packet.ptr);
	dmaKit_send_chain_ucab(controller.channel, (void *)chain_base);
    pending_generation[controller.channel] = flush_generation;
    buffer_generation[controller.context] = flush_generation++;
    buffer_channel[controller.context] = controller.channel;

    controller.context = (!controller.context); 

    /* The next half may have been sent through a different DMA channel.
     * Wait before the caller can overwrite it, not just before sending. */
    owl_channel previous = buffer_channel[controller.context];
    if (previous != CHANNEL_SIZE && pending_generation[previous] &&
        pending_generation[previous] == buffer_generation[controller.context]) {
        dmaKit_wait(previous, 0);
        pending_generation[previous] = 0;
    }
    buffer_channel[controller.context] = CHANNEL_SIZE;
    buffer_generation[controller.context] = 0;

    internal_packet.ptr = controller.base + (controller.context ? controller.size : 0);
    controller.alloc = 0;
}

uint64_t owl_flush_generation(void) { return flush_generation; }

int owl_generation_read(uint64_t generation) {
    if (generation >= flush_generation) return 0;
    for (int channel=0; channel<CHANNEL_SIZE; channel++)
        if (pending_generation[channel] && pending_generation[channel] <= generation)
            return 0;
    return 1;
}

void owl_wait_generation(uint64_t generation) {
    if (generation == flush_generation) {
        if (controller.alloc) owl_flush_packet();
        else flush_generation++; /* Close even an empty authored generation. */
    }
    for (int channel=0; channel<CHANNEL_SIZE; channel++) {
        if (pending_generation[channel] && pending_generation[channel] <= generation) {
            dmaKit_wait(channel, 0);
            pending_generation[channel]=0;
        }
    }
}

owl_packet *owl_query_packet(owl_channel channel, size_t size) {
    if (channel != controller.channel) {
        if (controller.channel != CHANNEL_SIZE) {
            owl_flush_packet();
            
        }

        controller.channel = channel;
    } else if ((controller.alloc + size) + 1 >= controller.size) { // + 1 for end tag
        owl_flush_packet();
    }

    controller.alloc += size;

    return &internal_packet;
}

owl_packet *owl_create_packet(owl_channel channel, size_t size, void* buf) {
    owl_packet *packet = (owl_packet *)buf;

    packet->channel = channel;
    packet->size = size-(sizeof(owl_packet)/sizeof(owl_qword));
    packet->base = uncached_address((uint8_t *)buf + sizeof(owl_packet));
    packet->ptr = packet->base;

    SyncDCache(packet->base,
        (uint8_t *)packet->base + packet->size * sizeof(owl_qword));

    return packet;
}

void owl_send_packet(owl_packet *packet) {
    packet->ptr = packet->base;

	//FlushCache(0);
	dmaKit_send_chain_ucab(packet->channel, (void *)packet->base);
}

owl_controller *owl_get_controller() {
    return &controller;
}

void vu1_set_double_buffer_settings(uint32_t base, uint32_t offset)
{
	owl_packet *internal_packet = owl_query_packet(CHANNEL_VIF1, 1);

    owl_add_tag(internal_packet, (VIF_CODE(base, 0, VIF_BASE, 0) | (uint64_t)VIF_CODE(offset, 0, VIF_OFFSET, 0) << 32), 
                        DMA_TAG(0, 0, DMA_CNT, 0, 0 , 0));

    //owl_add_tag(internal_packet, (VIF_CODE(0, 0, VIF_NOP, 0) | (uint64_t)VIF_CODE(0, 0, VIF_NOP, 0) << 32),
    //                DMA_TAG(0, 0, DMA_END, 0, 0 , 0));

}
