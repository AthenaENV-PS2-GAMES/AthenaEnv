#include <stdbool.h>
#include <athena/graphics.h>
#include <athena/graphics/owl_packet.h>
#include <athena/graphics/sync.h>
static bool finish_pending;
static uint32_t finished_frames,idle_count;
void graphics_frame_finished(void) { finished_frames++; }
uint32_t graphics_finished_frames(void) { return finished_frames; }
uint32_t graphics_idle_count(void) { return idle_count; }
void graphics_finish_wait(void) {
    if(!finish_pending) return;
    owl_wait_generation(owl_flush_generation());
    while(!GS_CSR_FINISH) {}
    GS_SETREG_CSR_FINISH(1); finish_pending=false;
}
void graphics_finish_begin(void) {
    graphics_finish_wait();
    GS_SETREG_CSR_FINISH(1); finish_pending=true;
}
void graphics_wait_idle(void) {
    if(!getGSGLOBAL()) return;
    /* FLUSHA orders all GIF paths before the PATH2 FINISH. Waiting only on
     * DMA would permit the GS to keep sampling a freed/reused VRAM block. */
    owl_packet *p=owl_query_packet(CHANNEL_VIF1,1);
    owl_add_cnt_tag(p,0,owl_vif_code_double(VIF_CODE(0,0,VIF_FLUSHA,0),VIF_CODE(0,0,VIF_NOP,0)));
    set_finish(); graphics_finish_wait();
    idle_count++;
}
