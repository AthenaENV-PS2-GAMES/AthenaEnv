#include <athena/screen.h>
#include <owl_packet.h>

static Color athena_clear_color = GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x80, 0x00);
static AthenaImage *athena_screen_buffers[3];
static AthenaImage *athena_screen_current_buffers[3];
static bool athena_screen_buffers_initialized;

void athena_screen_flip(void) {
    flipScreen();
}

void athena_screen_clear(Color color) {
    clearScreen(color);
}

void athena_screen_wait_vblank(void) {
    graphicWaitVblankStart();
}

void athena_screen_set_vsync(bool enabled) {
    setVSync(enabled);
}

void athena_screen_set_frame_counter(bool enabled) {
    toggleFrameCounter(enabled);
}

void athena_screen_set_clear_color(Color color) {
    athena_clear_color = color;
}

Color athena_screen_get_clear_color(void) {
    return athena_clear_color;
}

int athena_screen_get_free_vram(int mode) {
    return getFreeVRAM(mode);
}

float athena_screen_get_fps(int interval) {
    return FPSCounter(interval);
}

void athena_screen_get_mode(AthenaVideoMode *out) {
    if (!out)
        return;
    GSCONTEXT *gsGlobal = getGSGLOBAL();
    out->mode = gsGlobal->Mode;
    out->width = gsGlobal->Width;
    out->height = gsGlobal->Height;
    out->psm = gsGlobal->PSM;
    out->interlace = gsGlobal->Interlace;
    out->field = gsGlobal->Field;
    out->psmz = gsGlobal->PSMZ;
    out->zbuffering = gsGlobal->ZBuffering;
    out->double_buffering = gsGlobal->DoubleBuffering;
    out->pass_count = 0;
}

void athena_screen_set_mode(const AthenaVideoMode *mode) {
    if (!mode)
        return;
    setVideoMode(mode->mode, mode->width, mode->height, mode->psm,
                 mode->interlace, mode->field, mode->zbuffering, mode->psmz,
                 mode->double_buffering, mode->pass_count);
}

uint64_t athena_screen_alpha_equation(int a, int b, int c, int d, int fix) {
    return ALPHA_EQUATION(a, b, c, d, fix);
}

void athena_screen_set_param(int param, uint64_t value) {
    set_screen_param(param, value);
}

uint64_t athena_screen_get_param(int param) {
    return get_screen_param(param);
}

bool athena_screen_get_alpha_blend(int param, AthenaAlphaBlend *out) {
    if (!out || param != ALPHA_BLEND_EQUATION)
        return false;
    uint64_t value = get_screen_param(param);
    alpha_reg alpha = { .data = value };
    out->a = alpha.fields.a;
    out->b = alpha.fields.b;
    out->c = alpha.fields.c;
    out->d = alpha.fields.d;
    out->fix = alpha.fields.fix;
    return true;
}

bool athena_screen_get_scissor_bounds(int param, AthenaScissorBounds *out) {
    if (!out || param != SCISSOR_BOUNDS)
        return false;
    uint64_t value = get_screen_param(param);
    scissor_reg scissor = { .data = value };
    out->x1 = scissor.fields.x0;
    out->y1 = scissor.fields.y0;
    out->x2 = scissor.fields.x1;
    out->y2 = scissor.fields.y1;
    return true;
}

bool athena_screen_buffers_ready(void) {
    return athena_screen_buffers_initialized;
}

int athena_screen_init_buffers(void) {
    if (athena_screen_buffers_initialized)
        return -1;

    for (int i = 0; i < 3; i++) {
        AthenaImage *image = athena_image_wrap(main_screen_buffer[i], false);
        if (!image)
            return -1;
        athena_screen_buffers[i] = image;
        athena_screen_current_buffers[i] = image;
    }

    athena_screen_buffers_initialized = true;
    return 0;
}

AthenaImage *athena_screen_get_buffer(int buffer_id) {
    if (!athena_screen_buffers_initialized || buffer_id < 0 || buffer_id > 2)
        return NULL;
    return athena_screen_current_buffers[buffer_id];
}

void athena_screen_set_buffer(int buffer_id, AthenaImage *image, uint32_t mask) {
    if (!athena_screen_buffers_initialized || buffer_id < 0 || buffer_id > 2 || !image)
        return;

    set_screen_buffer((eScreenBuffers)buffer_id, image->tex, mask);

    if (!getGSGLOBAL()->PrimContext)
        athena_screen_current_buffers[buffer_id] = image;
}

void athena_screen_reset_buffers(void) {
    if (!athena_screen_buffers_initialized)
        return;

    for (int i = 0; i < 3; i++) {
        set_screen_buffer((eScreenBuffers)i, athena_screen_buffers[i]->tex, 0);
        athena_screen_current_buffers[i] = athena_screen_buffers[i];
    }
}

int athena_screen_switch_context(void) {
    return screen_switch_context();
}

void athena_screen_flush(void) {
    owl_flush_packet();
}
