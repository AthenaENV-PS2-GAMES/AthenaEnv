#ifndef ATHENA_TEXTURE3D_H
#define ATHENA_TEXTURE3D_H
#include <stdint.h>
#include <stddef.h>
#define ATHENA_TEXTURE3D_MAX_SIZE 512u
typedef struct AthenaTexture3D AthenaTexture3D;
typedef enum { ATHENA_TEXTURE3D_NEAREST=0, ATHENA_TEXTURE3D_LINEAR=1 } AthenaTexture3DFilter;
/* Per-axis addressing flags: clamp to edge (0) or repeat (power-of-two
 * sizes make it native on the GS). */
typedef enum { ATHENA_TEXTURE3D_CLAMP=0, ATHENA_TEXTURE3D_REPEAT_U=1, ATHENA_TEXTURE3D_REPEAT_V=2,
    ATHENA_TEXTURE3D_REPEAT=3 } AthenaTexture3DWrap;
typedef struct {
    uint32_t width,height;
    AthenaTexture3DFilter filter;
    /* Copied little-endian 0xAABBGGRR pixels, alpha 0..255. The opaque pass
     * ignores alpha; alpha-mask materials test it. The stored copy (view())
     * holds GS alpha, 0..0x80. */
    const uint32_t *pixels;
    uint32_t pixel_count;
    AthenaTexture3DWrap wrap; /* zero (CLAMP) keeps the original behaviour */
} AthenaTexture3DPixels;
/* Main thread only. Immutable, independent of Image handles. Power-of-two
 * dimensions 1..512; clamp-to-edge or repeat sampling, no mipmaps. CPU data retained
 * for re-upload after video-mode changes. Final release defers cleanup until GS idle. */
int athena_texture3d_create(const AthenaTexture3DPixels *pixels,AthenaTexture3D **out);
int athena_texture3d_load(const char *path,AthenaTexture3DFilter filter,AthenaTexture3D **out);
/* load() with an addressing mode. Paletted (4/8-bit) and 16-bit images are
 * expanded to canonical 32-bit pixels. The GS backend losslessly uploads T4/T8
 * when at most 16/256 distinct GS RGBA colors reduce texture+CLUT VRAM. */
int athena_texture3d_load_ex(const char *path,AthenaTexture3DFilter filter,AthenaTexture3DWrap wrap,AthenaTexture3D **out);
/* load_ex() from PNG/JPEG/BMP bytes in memory (borrowed for the call). */
int athena_texture3d_load_memory(const void *data,size_t size,AthenaTexture3DFilter filter,AthenaTexture3DWrap wrap,
    AthenaTexture3D **out);
void athena_texture3d_retain(AthenaTexture3D *texture);
void athena_texture3d_release(AthenaTexture3D *texture);
/* Makes the texture resident in VRAM now (uploading it and waiting for the
 * GS once), so the first draw does not stall mid-frame. Idempotent. Returns
 * ATHENA_MODEL3D_EVRAM when it does not fit. */
int athena_texture3d_upload(AthenaTexture3D *texture);
/* Borrowed immutable pixels: caller must retain texture while reading. */
void athena_texture3d_view(const AthenaTexture3D *texture,AthenaTexture3DPixels *out);
#endif
