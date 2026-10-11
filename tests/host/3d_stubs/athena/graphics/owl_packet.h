/* Packet writer stand-in; DMA/VIF field encodings match the production header.
 * Enables host validation of production packet construction and DMA lifetimes. */
#ifndef ATHENA_PACKET_TEST_OWL_H
#define ATHENA_PACKET_TEST_OWL_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
typedef enum { CHANNEL_VIF0,CHANNEL_VIF1,CHANNEL_GIF,CHANNEL_FROMIPU,CHANNEL_TOIPU,
    CHANNEL_SIF0,CHANNEL_SIF1,CHANNEL_SIF2,CHANNEL_FROMSPR,CHANNEL_TOSPR,CHANNEL_SIZE } owl_channel;
typedef union { uint64_t dword[2]; uint32_t sword[4]; float f[4]; } owl_qword;
typedef struct { owl_channel channel; owl_qword *base; size_t size,alloc; bool context; } owl_controller;
typedef struct { owl_qword *base; owl_channel channel; size_t size; owl_qword *ptr; } owl_packet;
#define DMA_CNT 1
#define DMA_REF 3
#define DMA_END 7
/* Preserve host pointers on x86_64 through borrowed address tokens. */
uint32_t athena_test_dma_address(uintptr_t address);
#define DMA_TAG(q,p,id,irq,addr,spr) ((uint64_t)(q)|((uint64_t)(id)<<28)|((uint64_t)((addr)?athena_test_dma_address((uintptr_t)(addr)):0)<<32))
#define UNPACK_V3_32 8
#define UNPACK_V2_32 4
#define UNPACK_V4_32 12
#define UNPACK_V4_8 14
#define VIF_MPG 74
#define VIF_NOP 0
#define VIF_STCYCL 1
#define VIF_BASE 3
#define VIF_OFFSET 2
#define VIF_ITOP 4
#define VIF_FLUSHE 16
#define VIF_FLUSH 17
#define VIF_FLUSHA 19
#define VIF_MSCALF 21
#define VIF_CODE(imm,num,cmd,irq) ((uint32_t)(imm)|((uint32_t)(num)<<16)|((uint32_t)(cmd)<<24))
#define DRAW_STQ2_REGLIST 0x512u
#define register_vu_program(name) static uint32_t test_vu_code_##name[16]
#define embed_vu_code_ptr(name) test_vu_code_##name
#define embed_vu_code_size(name) 16
void SyncDCache(void *,void *);
void dmaKit_wait(owl_channel,int);
void dmaKit_send_chain_ucab(owl_channel,void *);
void owl_init(void *,size_t);
#ifndef ATHENA_OWL_DIAGNOSTICS
#define ATHENA_OWL_DIAGNOSTICS 0
#endif
typedef struct {
    uint64_t queries,flushes,capacity_flushes,channel_flushes;
    uint64_t submit_waits,reuse_waits,fence_waits,submitted_qwords;
    size_t peak_half_qwords;
} owl_packet_stats;
void owl_packet_stats_reset(void);
void owl_packet_stats_read(owl_packet_stats *);
void owl_flush_packet(void);
owl_packet *owl_query_packet(owl_channel,size_t);
owl_controller *owl_get_controller(void);
uint64_t owl_flush_generation(void);
int owl_generation_read(uint64_t);
void owl_wait_generation(uint64_t);
void vu1_set_double_buffer_settings(uint32_t,uint32_t);
static inline void owl_add_uint(owl_packet *p,uint32_t v) { memcpy(p->ptr,&v,4); p->ptr=(owl_qword *)((char *)p->ptr+4); }
static inline void owl_add_float(owl_packet *p,float v) { memcpy(p->ptr,&v,4); p->ptr=(owl_qword *)((char *)p->ptr+4); }
static inline void owl_add_ulong(owl_packet *p,uint64_t v) { memcpy(p->ptr,&v,8); p->ptr=(owl_qword *)((char *)p->ptr+8); }
static inline void owl_add_tag(owl_packet *p,uint64_t hi,uint64_t lo) { p->ptr->dword[0]=lo; p->ptr->dword[1]=hi; p->ptr++; }
static inline void owl_add_uquad_ptr(owl_packet *p,const void *v) { memcpy(p->ptr,v,16); p->ptr++; }
#define owl_add_cnt_tag(p,count,upper) owl_add_tag(p,upper,DMA_TAG(count,0,DMA_CNT,0,0,0))
#define owl_add_end_tag(p,count) owl_add_tag(p,0,DMA_TAG(count,0,DMA_END,0,0,0))
#define owl_vif_code_double(a,b) ((uint64_t)(b)|((uint64_t)(a)<<32))
static inline void owl_add_unpack_data_cnt(owl_packet *p,uint32_t dest,uint32_t count,uint8_t top) {
    owl_add_ulong(p,DMA_TAG(count,0,DMA_CNT,0,0,0));
    owl_add_uint(p,VIF_CODE(0x101,0,VIF_STCYCL,0));
    owl_add_uint(p,VIF_CODE(dest|(1u<<14)|((uint32_t)top<<15),count,0x6c,0));
}
#endif
