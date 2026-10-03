#ifndef ATHENA_SCREEN_H
#define ATHENA_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

#include <graphics.h>
#include <athena/image.h>

typedef struct AthenaVideoMode {
    int mode;
    int width;
    int height;
    int psm;
    int interlace;
    int field;
    int psmz;
    bool zbuffering;
    bool double_buffering;
    uint32_t pass_count;
} AthenaVideoMode;

typedef struct AthenaAlphaBlend {
    int a;
    int b;
    int c;
    int d;
    int fix;
} AthenaAlphaBlend;

typedef struct AthenaScissorBounds {
    int x1;
    int y1;
    int x2;
    int y2;
} AthenaScissorBounds;

void athena_screen_flip(void);
void athena_screen_clear(Color color);
void athena_screen_wait_vblank(void);
void athena_screen_set_vsync(bool enabled);
void athena_screen_set_frame_counter(bool enabled);
void athena_screen_set_clear_color(Color color);
Color athena_screen_get_clear_color(void);

int athena_screen_get_free_vram(int mode);
float athena_screen_get_fps(int interval);

void athena_screen_get_mode(AthenaVideoMode *out);
void athena_screen_set_mode(const AthenaVideoMode *mode);

uint64_t athena_screen_alpha_equation(int a, int b, int c, int d, int fix);
void athena_screen_set_param(int param, uint64_t value);
uint64_t athena_screen_get_param(int param);

bool athena_screen_get_alpha_blend(int param, AthenaAlphaBlend *out);
bool athena_screen_get_scissor_bounds(int param, AthenaScissorBounds *out);

int athena_screen_init_buffers(void);
AthenaImage *athena_screen_get_buffer(int buffer_id);
void athena_screen_set_buffer(int buffer_id, AthenaImage *image, uint32_t mask);
void athena_screen_reset_buffers(void);
bool athena_screen_buffers_ready(void);

int athena_screen_switch_context(void);
void athena_screen_flush(void);

#endif /* ATHENA_SCREEN_H */
