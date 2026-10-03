#ifndef ATHENA_SCREEN_H
#define ATHENA_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

#include <athena/graphics.h>

/*
 * Display configuration on top of the graphics service. Every setter
 * validates its input, so C applications get the same guarantees as the
 * JavaScript Screen module.
 */

typedef enum {
    ATHENA_SCREEN_OK = 0,
    ATHENA_SCREEN_ERR_NOT_READY = -1,   /* graphics service not initialized */
    ATHENA_SCREEN_ERR_INVALID = -2,     /* argument out of range */
    ATHENA_SCREEN_ERR_ALLOC = -3,       /* video buffers could not be allocated */
} AthenaScreenResult;

typedef struct {
    int mode;              /* GS_MODE_NTSC, GS_MODE_PAL, GS_MODE_DTV_* */
    int width;             /* 64..2048, multiple of 64 */
    int height;            /* 1..2048 */
    int psm;               /* GS_PSM_CT16, CT16S, CT24, CT32 */
    int interlace;         /* GS_INTERLACED or GS_NONINTERLACED */
    int field;             /* GS_FIELD or GS_FRAME */
    int psmz;              /* GS_ZBUF_16, 16S, 24, 32 */
    bool zbuffering;
    bool double_buffering;
    int pass_count;        /* must be 0 */
} AthenaScreenMode;

bool athena_screen_ready(void);

/* Current mode; ATHENA_SCREEN_ERR_NOT_READY before graphics init. */
AthenaScreenResult athena_screen_get_mode(AthenaScreenMode *out);

/*
 * Validates and applies a video mode. On ATHENA_SCREEN_ERR_INVALID, *error
 * (if not NULL) names the offending field.
 */
AthenaScreenResult athena_screen_set_mode(const AthenaScreenMode *mode, const char **error);

/* Encodes an ALPHA register; a/b/c/d in 0..2, fix in 0..255. */
AthenaScreenResult athena_screen_alpha_equation(int a, int b, int c, int d, int fix, uint64_t *out);

/* Encodes a scissor rectangle; must fit inside the current framebuffer. */
AthenaScreenResult athena_screen_scissor(int x0, int y0, int x1, int y1, uint64_t *out);

/*
 * Rendering parameters (ALPHA_TEST_ENABLE .. COLOR_CLAMP_MODE). The value is
 * validated against the parameter's range; ALPHA_BLEND_EQUATION and
 * SCISSOR_BOUNDS take registers built with the encoders above.
 */
bool athena_screen_param_valid(int param, uint64_t value);
AthenaScreenResult athena_screen_set_param(int param, uint64_t value);
AthenaScreenResult athena_screen_get_param(int param, uint64_t *out);

/* Bytes of VRAM still free; 0 before graphics init. */
uint32_t athena_screen_free_vram(void);

#endif /* ATHENA_SCREEN_H */
