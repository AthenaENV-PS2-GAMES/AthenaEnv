#ifndef ATHENA_TEXTURE3D_BACKEND_H
#define ATHENA_TEXTURE3D_BACKEND_H
#include <stddef.h>
#include <athena/texture3d.h>
/* Private platform boundary; core ownership is also tested without hardware. */
typedef struct { uint64_t tex0,tex1,clamp; } AthenaTexture3DBinding;
int athena_texture3d_decode(const char *path,uint32_t **pixels,uint32_t *width,uint32_t *height);
/* Same as athena_texture3d_decode, but safe on a worker thread (no detail string). */
int athena_texture3d_decode_quiet(const char *path,uint32_t **pixels,uint32_t *width,uint32_t *height);
int athena_texture3d_decode_memory(const void *data,size_t size,uint32_t **pixels,uint32_t *width,uint32_t *height);
/* Retained-copy form of decode: 128-byte aligned, zero-padded to 16 bytes, GS alpha, free()-able, caller-owned.
 * Dimensions are not validated (callers apply their own power-of-two policy). */
int athena_texture3d_decode_canonical(const char *path,int quiet,uint32_t **pixels,uint32_t *width,uint32_t *height);
int athena_texture3d_decode_canonical_memory(const void *data,size_t size,uint32_t **pixels,uint32_t *width,uint32_t *height);
/* Phase 5C.4: decode_canonical plus the optional exact T4/T8 streams (see AthenaTexture3DIndexed in texture3d.h).
 * *indexed is always set (NULL when absent or on error); the caller owns it and frees it with athena_texture3d_indexed_free(). */
int athena_texture3d_decode_canonical_indexed(const char *path,int quiet,uint32_t **pixels,AthenaTexture3DIndexed **indexed,
    uint32_t *width,uint32_t *height);
int athena_texture3d_decode_canonical_memory_indexed(const void *data,size_t size,uint32_t **pixels,AthenaTexture3DIndexed **indexed,
    uint32_t *width,uint32_t *height);
/* Transactional form (Phase 5C.5B): on success *indexed is consumed and set to NULL; on failure (NULL result)
 * *indexed is left untouched with its caller. indexed itself may be NULL, *indexed may be NULL. */
void *athena_texture3d_backend_create_adopt(const AthenaTexture3DPixels *pixels,AthenaTexture3DIndexed **indexed);
/* 1 when the backend owns a T4/T8 index stream plus CLUT, i.e. uploads without reading the canonical RGBA. */
int athena_texture3d_backend_indexed_resident(const void *backend);
/* Rebuilds the canonical RGBA (128-byte aligned, zero padded to 16 bytes, GS alpha) from the backend's indices and
 * CLUT. ATHENA_MODEL3D_ENOMEM leaves *out NULL and the backend untouched; EINVAL when not indexed-resident. */
int athena_texture3d_backend_materialize(const void *backend,uint32_t **out);
void *athena_texture3d_backend_create(const AthenaTexture3DPixels *pixels);
/* Always consumes indexed (may be NULL): adopted by the backend or freed, never left with the caller. */
void *athena_texture3d_backend_create_indexed(const AthenaTexture3DPixels *pixels,AthenaTexture3DIndexed *indexed);
int athena_texture3d_backend_bind(void *backend,AthenaTexture3DBinding *out);
/* Takes ownership of backend (may be NULL) and of the texture's CPU pixels,
 * freeing them once the GS can no longer read them (possibly later). */
void athena_texture3d_backend_destroy(void *backend,void *pixels);
/* Frees the deferred releases that are provably idle; force waits for the
 * GS first and frees all of them. */
void athena_texture3d_collect(int force);
/* Unique binding revision, independent of allocator address reuse; NULL is zero.
 * Static textures keep their identity; target capture advances the revision. */
uint64_t athena_texture3d_stamp(const AthenaTexture3D *texture);
typedef struct { uint32_t address,width,height,tbw; } AthenaTexture3DTarget;
void *athena_texture3d_backend_create_target(const AthenaTexture3DPixels *pixels);
int athena_texture3d_backend_target_begin(void *backend,AthenaTexture3DTarget *out);
void athena_texture3d_backend_target_end(void *backend,int commit);
int athena_texture3d_target_begin(AthenaTexture3D *texture,AthenaTexture3DTarget *out);
void athena_texture3d_target_end(AthenaTexture3D *texture,int commit);
int athena_texture3d_bind(AthenaTexture3D *texture,AthenaTexture3DBinding *out);
#endif
