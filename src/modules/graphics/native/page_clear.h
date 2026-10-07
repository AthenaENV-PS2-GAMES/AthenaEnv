/* Shared by the renderer and host regression: no temporary page array. */
#ifndef ATHENA_PAGE_CLEAR_H
#define ATHENA_PAGE_CLEAR_H
#include <athena/graphics.h>
#include <athena/graphics/owl_packet.h>

static void owl_page_clear(GSCONTEXT *ctx, Color color, uint64_t test,
    uint64_t xyoffset)
{
    owl_controller *controller = owl_get_controller();
    if (!ctx || ctx->Width <= 0 || ctx->Height <= 0 ||
        controller->size <= 12) return;
    size_t columns = ((size_t)ctx->Width + 63) / 64;
    size_t rows = ((size_t)ctx->Height + 31) / 32;
    size_t pages = columns * rows;
    size_t capacity = controller->size - 12; /* 11 header QWs + END. */
    for (size_t first = 0; first < pages;) {
        size_t count = pages - first;
        if (count > capacity) count = capacity;
        owl_packet *packet = owl_query_packet(CHANNEL_VIF1, 11 + count);
        owl_add_cnt_tag(packet, 10 + count, 0);
        owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
        owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
        owl_add_uint(packet, VIF_CODE(0, 0, VIF_NOP, 0));
        owl_add_uint(packet, VIF_CODE(9 + count, 0, VIF_DIRECT, 0));
        owl_add_tag(packet, GIF_AD, VU_GS_GIFTAG(4, 1, 0, 1, 0, 0, 1));
        owl_add_tag(packet, GS_TEST_1 + ctx->PrimContext,
            GS_SETREG_TEST(0, 0, 0, 0, 0, 0, 1, 1));
        owl_add_tag(packet, GS_XYOFFSET_1 + ctx->PrimContext,
            GS_SETREG_XYOFFSET(0, 0));
        owl_add_tag(packet, GS_RGBAQ, color);
        owl_add_tag(packet, GS_PRIM, VU_GS_PRIM(GS_PRIM_PRIM_SPRITE,
            0, 0, 0, 0, 0, 0, ctx->PrimContext, 0));
        owl_add_tag(packet, GS_XYZ2 | (GS_XYZ2 << 4),
            VU_GS_GIFTAG(count, 1, 0, 1, 0, 1, 2));
        for (size_t i = first; i < first + count; i++) {
            unsigned x = (i / rows) * 64, y = (i % rows) * 32;
            /* Same REGLIST word order as the original pcpyld writer. */
            owl_add_tag(packet, GS_SETREG_XYZ(x << 4, y << 4, 0),
                GS_SETREG_XYZ((x + 64) << 4, (y + 32) << 4, 0));
        }
        owl_add_tag(packet, GIF_AD, VU_GS_GIFTAG(2, 1, 0, 0, 0, 0, 1));
        owl_add_tag(packet, GS_TEST_1 + ctx->PrimContext, test);
        owl_add_tag(packet, GS_XYOFFSET_1 + ctx->PrimContext, xyoffset);
        first += count;
    }
}
#endif
