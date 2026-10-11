#ifndef ATHENA_DRAW_PACKET_HARNESS_H
#define ATHENA_DRAW_PACKET_HARNESS_H
#include <athena/graphics.h>
#include <athena/graphics/owl_packet.h>
typedef void (*TestVertex)(uint64_t xy,uint64_t uv,uint64_t color,unsigned prim);
extern int test_bind_result;
extern bool test_upload_once;
extern unsigned test_upload_bind_calls;
extern unsigned test_packets,test_queries,test_marks,test_bind_calls,test_vertices;
void test_packet_begin(unsigned ring,TestVertex vertex,bool prefill);
void test_packet_end(void);
void test_packet_checkpoint(void);
void test_view(AthenaViewKind kind);
#endif
