#ifndef ATHENA_TEXTURE3D_H
#define ATHENA_TEXTURE3D_H
#include <stdint.h>
#include <stddef.h>
#define ATHENA_TEXTURE3D_MAX_SIZE 512u
/* Opt-in (default 0): release the canonical RGBA copy of bound T4/T8 textures that called allow_compaction(). */
#ifndef ATHENA_TEXTURE3D_COMPACT_CANONICAL
#define ATHENA_TEXTURE3D_COMPACT_CANONICAL 0
#endif
typedef struct AthenaTexture3D AthenaTexture3D;
typedef enum { ATHENA_TEXTURE3D_NEAREST=0, ATHENA_TEXTURE3D_LINEAR=1 } AthenaTexture3DFilter;
/* GPU-owned CT32 silhouette target: power-of-two width/height in 64..512.
 * No CPU pixel snapshot; capture before sampling (also after video mode changes). */
int athena_texture3d_create_target(uint32_t width,uint32_t height,AthenaTexture3DFilter filter,AthenaTexture3D **out);
/* Per-axis addressing flags: clamp to edge (0) or repeat (power-of-two
 * sizes make it native on the GS). */
typedef enum { ATHENA_TEXTURE3D_CLAMP=0, ATHENA_TEXTURE3D_REPEAT_U=1, ATHENA_TEXTURE3D_REPEAT_V=2,
    ATHENA_TEXTURE3D_REPEAT=3 } AthenaTexture3DWrap;
typedef struct {
    uint32_t width,height;
    AthenaTexture3DFilter filter;
    /* Copied little-endian 0xAABBGGRR pixels, alpha 0..255. The opaque pass
     * ignores alpha; alpha-mask/blended materials read it. The stored copy (view())
     * holds GS alpha, 0..0x80. */
    const uint32_t *pixels;
    uint32_t pixel_count;
    AthenaTexture3DWrap wrap; /* zero (CLAMP) keeps the original behaviour */
} AthenaTexture3DPixels;
/* Main thread only. Static textures are immutable, independent of Image handles. Power-of-two
 * dimensions 1..512; clamp-to-edge or repeat sampling, no mipmaps. CPU data retained
 * for re-upload after video-mode changes (the canonical RGBA copy, or for an opted-in T4/T8 texture the GS index/CLUT streams). Final release defers cleanup until GS idle. */
int athena_texture3d_create(const AthenaTexture3DPixels *pixels,AthenaTexture3D **out);
int athena_texture3d_load(const char *path,AthenaTexture3DFilter filter,AthenaTexture3D **out);
/* load() with an addressing mode. Paletted (4/8-bit) and 16-bit images are
 * expanded to canonical 32-bit pixels. The GS backend losslessly uploads T4/T8
 * when at most 16/256 distinct GS RGBA colors reduce texture+CLUT VRAM. */
int athena_texture3d_load_ex(const char *path,AthenaTexture3DFilter filter,AthenaTexture3DWrap wrap,AthenaTexture3D **out);
/* load_ex() from PNG/JPEG/BMP bytes in memory (borrowed for the call). */
int athena_texture3d_load_memory(const void *data,size_t size,AthenaTexture3DFilter filter,AthenaTexture3DWrap wrap,
    AthenaTexture3D **out);
/* The two halves of load_ex() for background loading. decode_prepared() is thread-safe (no shared
 * state, no detail string): it decodes a PNG/JPEG/BMP file and builds the retained pixel copy
 * (128-byte aligned, GS alpha) that create() would make; free it with free(). adopt() runs on the
 * main thread in O(1): on success the texture owns `prepared` (an error leaves it with the caller). */
int athena_texture3d_decode_prepared(const char *path,uint32_t **prepared,uint32_t *width,uint32_t *height);
int athena_texture3d_adopt(uint32_t *prepared,uint32_t width,uint32_t height,AthenaTexture3DFilter filter,AthenaTexture3DWrap wrap,
    AthenaTexture3D **out);
/* Phase 5C.4 additive pair: the same two halves, plus the optional exact T4/T8 index/CLUT streams that
 * decode_prepared_indexed() derived from a paletted image (*indexed is NULL when none is worthwhile).
 * Thread-safe like decode_prepared(). Free an unadopted stream set with athena_texture3d_indexed_free().
 * adopt_indexed() follows adopt(): on success the texture owns prepared AND indexed; on error both stay
 * with the caller. The streams are consumed by the first successful backend creation (a failed bind keeps them) or freed at release. */
typedef struct AthenaTexture3DIndexed AthenaTexture3DIndexed;
int athena_texture3d_decode_prepared_indexed(const char *path,uint32_t **prepared,AthenaTexture3DIndexed **indexed,
    uint32_t *width,uint32_t *height);
int athena_texture3d_adopt_indexed(uint32_t *prepared,AthenaTexture3DIndexed *indexed,uint32_t width,uint32_t height,
    AthenaTexture3DFilter filter,AthenaTexture3DWrap wrap,AthenaTexture3D **out);
void athena_texture3d_indexed_free(AthenaTexture3DIndexed *indexed);
void athena_texture3d_retain(AthenaTexture3D *texture);
void athena_texture3d_release(AthenaTexture3D *texture);
/* Makes the texture resident in VRAM now (uploading it and waiting for the
 * GS once), so the first draw does not stall mid-frame. Idempotent. Returns
 * ATHENA_MODEL3D_EVRAM when it does not fit. */
int athena_texture3d_upload(AthenaTexture3D *texture);
/* Borrowed immutable pixels: caller must retain texture while reading. Infallible for every texture that has
 * not opted in to canonical compaction (athena_texture3d_allow_compaction); the pointer stays valid until the final
 * release. For an opted-in texture whose canonical RGBA was released, view() must rebuild it and can fail under
 * memory pressure (out->pixels == NULL, pixel_count == 0): such callers must use try_view() instead. */
void athena_texture3d_view(const AthenaTexture3D *texture,AthenaTexture3DPixels *out);

/* Phase 5C.5B. Metadata only: no pixel access, no allocation, no GS work, no state change. Returns 0, or
 * ATHENA_MODEL3D_EINVAL (out zeroed) for NULL arguments. render_target is 1 for GPU-owned capture targets. */
typedef struct { uint32_t width,height; AthenaTexture3DFilter filter; AthenaTexture3DWrap wrap; int render_target; } AthenaTexture3DInfo;
int athena_texture3d_info(const AthenaTexture3D *texture,AthenaTexture3DInfo *out);
/* Fallible form of view(). Returns 0 with valid canonical pixels (128-byte aligned, GS alpha, zero padded, pointer stable
 * until the final release; repeated calls return the same pointer), or 0 with pixels == NULL for a render target (see
 * AthenaTexture3DInfo.render_target). On failure returns ATHENA_MODEL3D_ENOMEM (or EINVAL) with out initialised to the
 * texture's metadata, pixels == NULL and pixel_count == 0; the texture is unchanged and the call may be retried. Main thread only. */
int athena_texture3d_try_view(AthenaTexture3D *texture,AthenaTexture3DPixels *out);
/* Phase 5C.5D. Export-boundary guard: makes the canonical RGBA of `texture` present and pins it until the final release
 * (the same effect as a successful try_view(), without producing a pointer), so that the legacy infallible view() is
 * valid and address-stable afterwards. Returns 0 (also for a render target, which has no CPU pixels, and when canonical
 * pixels were never released), ENOMEM when the rebuild cannot allocate, or EINVAL for NULL. On failure the texture,
 * its indexed backend and its GS residency are unchanged and the call may be retried. Idempotent; main thread only.
 * Cost of success on a compacted texture: the full canonical buffer comes back (ceil16(w*h*4) bytes) for good. */
int athena_texture3d_pin_canonical(AthenaTexture3D *texture);
/* Declares that the owner will not need the legacy infallible view() after the first bind. With
 * ATHENA_TEXTURE3D_COMPACT_CANONICAL=1 such a texture whose backend is T4/T8 releases its canonical RGBA right after
 * backend creation, unless a pixel pointer was already handed out (which pins it until the final release).
 * No effect with the flag off or on render targets. Main thread only. */
void athena_texture3d_allow_compaction(AthenaTexture3D *texture);
/* Diagnostics for canonical compaction (all counters since start; pinned_* are current gauges). */
typedef struct {
    uint64_t released_buffers,released_bytes;
    uint64_t materialize_attempts,materialize_ok,materialize_failures;
    uint64_t pinned_buffers,pinned_bytes;
} AthenaTexture3DCompactStats;
void athena_texture3d_compact_stats(AthenaTexture3DCompactStats *out);
#endif
