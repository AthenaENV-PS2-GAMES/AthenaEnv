#ifndef ATHENA_TEXTURE3D_H
#define ATHENA_TEXTURE3D_H
#include <stdint.h>
#define ATHENA_TEXTURE3D_MAX_SIZE 512u
typedef struct AthenaTexture3D AthenaTexture3D;
typedef enum { ATHENA_TEXTURE3D_NEAREST=0, ATHENA_TEXTURE3D_LINEAR=1 } AthenaTexture3DFilter;
typedef struct {
    uint32_t width,height;
    AthenaTexture3DFilter filter;
    /* Copied little-endian 0xAABBGGRR pixels. The opaque pass ignores alpha. */
    const uint32_t *pixels;
    uint32_t pixel_count;
} AthenaTexture3DPixels;
/* Main thread only. Immutable, independent of Image handles. Power-of-two
 * dimensions 1..512; clamp-to-edge sampling, no mipmaps. CPU data retained
 * for re-upload after video-mode changes. Final release drains GS if resident. */
int athena_texture3d_create(const AthenaTexture3DPixels *pixels,AthenaTexture3D **out);
int athena_texture3d_load(const char *path,AthenaTexture3DFilter filter,AthenaTexture3D **out);
void athena_texture3d_retain(AthenaTexture3D *texture);
void athena_texture3d_release(AthenaTexture3D *texture);
/* Borrowed immutable pixels: caller must retain texture while reading. */
void athena_texture3d_view(const AthenaTexture3D *texture,AthenaTexture3DPixels *out);
#endif
