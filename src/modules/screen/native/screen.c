#include <stddef.h>

#include <athena/screen.h>

bool athena_screen_ready(void) {
    return getGSGLOBAL() != NULL;
}

AthenaScreenResult athena_screen_get_mode(AthenaScreenMode *out) {
    GSCONTEXT *global = getGSGLOBAL();

    if (!global)
        return ATHENA_SCREEN_ERR_NOT_READY;
    out->mode = global->Mode;
    out->width = global->Width;
    out->height = global->Height;
    out->psm = global->PSM;
    out->interlace = global->Interlace;
    out->field = global->Field;
    out->psmz = global->PSMZ;
    out->zbuffering = global->ZBuffering;
    out->double_buffering = global->DoubleBuffering;
    out->pass_count = 0;
    return ATHENA_SCREEN_OK;
}

static const char *screen_mode_error(const AthenaScreenMode *m) {
    if (m->pass_count != 0)
        return "pass_count is not supported";
    if (m->width <= 0 || m->width > 2048 || (m->width % 64) != 0 ||
        m->height <= 0 || m->height > 2048)
        return "dimensions must be positive, width-aligned, and <= 2048";
    if (m->interlace != GS_INTERLACED && m->interlace != GS_NONINTERLACED)
        return "interlace is invalid";
    if (m->field != GS_FIELD && m->field != GS_FRAME)
        return "field is invalid";
    if (m->psm != GS_PSM_CT16 && m->psm != GS_PSM_CT16S &&
        m->psm != GS_PSM_CT24 && m->psm != GS_PSM_CT32)
        return "psm is invalid";
    if (m->psmz != GS_ZBUF_16 && m->psmz != GS_ZBUF_16S &&
        m->psmz != GS_ZBUF_24 && m->psmz != GS_ZBUF_32)
        return "psmz is invalid";
    if (m->mode != GS_MODE_NTSC && m->mode != GS_MODE_PAL &&
        m->mode != GS_MODE_DTV_480P && m->mode != GS_MODE_DTV_576P &&
        m->mode != GS_MODE_DTV_720P && m->mode != GS_MODE_DTV_1080I)
        return "mode is invalid";
    return NULL;
}

AthenaScreenResult athena_screen_set_mode(const AthenaScreenMode *mode, const char **error) {
    const char *message = screen_mode_error(mode);

    if (error)
        *error = message;
    if (message)
        return ATHENA_SCREEN_ERR_INVALID;
    if (setVideoMode((s16)mode->mode, mode->width, mode->height, mode->psm,
            (s16)mode->interlace, (s16)mode->field, mode->zbuffering,
            mode->psmz, mode->double_buffering, (uint8_t)mode->pass_count) < 0) {
        if (error)
            *error = "failed to allocate video buffers";
        return ATHENA_SCREEN_ERR_ALLOC;
    }
    return ATHENA_SCREEN_OK;
}

AthenaScreenResult athena_screen_alpha_equation(int a, int b, int c, int d, int fix, uint64_t *out) {
    if (a < 0 || a > 2 || b < 0 || b > 2 || c < 0 || c > 2 ||
        d < 0 || d > 2 || fix < 0 || fix > 255)
        return ATHENA_SCREEN_ERR_INVALID;
    *out = ALPHA_EQUATION(a, b, c, d, fix);
    return ATHENA_SCREEN_OK;
}

AthenaScreenResult athena_screen_scissor(int x0, int y0, int x1, int y1, uint64_t *out) {
    GSCONTEXT *global = getGSGLOBAL();

    if (!global)
        return ATHENA_SCREEN_ERR_NOT_READY;
    if (x0 < 0 || y0 < 0 || x1 < x0 || y1 < y0 ||
        x1 >= global->Width || y1 >= global->Height)
        return ATHENA_SCREEN_ERR_INVALID;
    *out = GS_SETREG_SCISSOR_1(x0, x1, y0, y1);
    return ATHENA_SCREEN_OK;
}

bool athena_screen_param_valid(int param, uint64_t value) {
    switch (param) {
    case ALPHA_TEST_ENABLE:
    case DST_ALPHA_TEST_ENABLE:
    case DEPTH_TEST_ENABLE:
    case PIXEL_ALPHA_BLEND_ENABLE:
        return value <= 1;
    case ALPHA_TEST_METHOD:
        return value <= ALPHA_NEQUAL;
    case ALPHA_TEST_FAIL:
        return value <= ALPHA_FAIL_RGB_ONLY;
    case DST_ALPHA_TEST_METHOD:
        return value <= DEST_ALPHA_ONE;
    case DEPTH_TEST_METHOD:
        return value <= DEPTH_GREATER;
    case COLOR_CLAMP_MODE:
        return value <= 1;
    case ALPHA_TEST_REF:
        return value <= 255;
    case ALPHA_BLEND_EQUATION: {
        alpha_reg alpha = { .data = value };
        uint64_t encoded;
        return athena_screen_alpha_equation(alpha.fields.a, alpha.fields.b,
            alpha.fields.c, alpha.fields.d, alpha.fields.fix, &encoded) == ATHENA_SCREEN_OK &&
            encoded == value;
    }
    case SCISSOR_BOUNDS: {
        scissor_reg scissor = { .data = value };
        uint64_t encoded;
        return athena_screen_scissor(scissor.fields.x0, scissor.fields.y0,
            scissor.fields.x1, scissor.fields.y1, &encoded) == ATHENA_SCREEN_OK &&
            encoded == value;
    }
    default:
        return false;
    }
}

AthenaScreenResult athena_screen_set_param(int param, uint64_t value) {
    if (!athena_screen_param_valid(param, value))
        return ATHENA_SCREEN_ERR_INVALID;
    set_screen_param((uint8_t)param, value);
    return ATHENA_SCREEN_OK;
}

AthenaScreenResult athena_screen_get_param(int param, uint64_t *out) {
    if (param < ALPHA_TEST_ENABLE || param > COLOR_CLAMP_MODE)
        return ATHENA_SCREEN_ERR_INVALID;
    *out = get_screen_param((uint8_t)param);
    return ATHENA_SCREEN_OK;
}

uint32_t athena_screen_free_vram(void) {
    if (!getGSGLOBAL())
        return 0;
    return (uint32_t)getFreeVRAM(VRAM_SIZE) - (uint32_t)getFreeVRAM(VRAM_USED_TOTAL);
}
