#ifndef ATHENA_TEXTURE3D_BACKEND_H
#define ATHENA_TEXTURE3D_BACKEND_H
#include <athena/texture3d.h>
/* Private platform boundary; core ownership is also tested without hardware. */
typedef struct { uint64_t tex0,tex1,clamp; } AthenaTexture3DBinding;
int athena_texture3d_decode(const char *path,uint32_t **pixels,uint32_t *width,uint32_t *height);
void *athena_texture3d_backend_create(const AthenaTexture3DPixels *pixels);
int athena_texture3d_backend_bind(void *backend,AthenaTexture3DBinding *out);
void athena_texture3d_backend_destroy(void *backend);
int athena_texture3d_bind(AthenaTexture3D *texture,AthenaTexture3DBinding *out);
#endif
