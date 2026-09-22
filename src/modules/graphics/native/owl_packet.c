#include <owl_packet.h>
#include <debug.h>
#include <stdlib.h>

owl_controller controller = { 0 };
owl_packet internal_packet = { 0 };

void owl_init(void *ptr, size_t size) {
    controller.channel = CHANNEL_SIZE;

    SyncDCache(ptr, (uint8_t *)ptr + size * sizeof(owl_qword));

    controller.base = (owl_qword *)((uint32_t)(ptr) | 0x30000000);
    internal_packet.ptr = controller.base;

    controller.size = size/2;
    controller.alloc = 0;

    controller.context = false;
}

void owl_flush_packet() {
    owl_qword *chain_base;

    if (controller.alloc == 0) {
        return;
    }

    owl_add_end_tag(&internal_packet, 0);

    dmaKit_wait(controller.channel, 0);

    chain_base = controller.base + (controller.context ? controller.size : 0);
    SyncDCache(chain_base, (uint8_t *)internal_packet.ptr);
	dmaKit_send_chain_ucab(controller.channel, (void *)chain_base);

    controller.context = (!controller.context); 

    internal_packet.ptr = controller.base + (controller.context ? controller.size : 0);
    controller.alloc = 0;
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
    packet->base = (owl_qword *)(((uint32_t)(buf) + sizeof(owl_packet)) | 0x30000000);
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
