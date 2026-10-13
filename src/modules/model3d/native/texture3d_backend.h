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
void *athena_texture3d_backend_create(const AthenaTexture3DPixels *pixels);
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
