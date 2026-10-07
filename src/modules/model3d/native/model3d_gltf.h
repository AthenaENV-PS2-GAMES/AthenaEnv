#ifndef ATHENA_MODEL3D_GLTF_H
#define ATHENA_MODEL3D_GLTF_H
/* Private: glTF primitive conversion shared with the gltf3d module, which
 * includes cgltf.h from this folder (the implementation is in model3d_load.c). */
#include <athena/model3d.h>
#include "cgltf/cgltf.h"
/* Textures already loaded during one file load, by image and sampling, so
 * primitives sharing an image share one Texture3D (RAM and VRAM). The cache
 * holds one reference per entry; release it once the load is done. */
typedef struct {
    const cgltf_image *image; AthenaTexture3DFilter filter; AthenaTexture3DWrap wrap;
    AthenaTexture3D *texture;
} AthenaGltfTextureEntry;
typedef struct { AthenaGltfTextureEntry *items; uint32_t count,capacity; } AthenaGltfTextureCache;
void athena_model3d_gltf_cache_release(AthenaGltfTextureCache *cache);
/* cache may be NULL (no sharing). */
int athena_model3d_gltf_primitive(const char *path,cgltf_primitive *prim,
    const AthenaMaterial3D *material_override,AthenaGltfTextureCache *cache,AthenaMesh3D **out);
/* Required extensions both loaders implement or may ignore; others are
 * rejected with a detail naming them. */
int athena_model3d_gltf_extensions(const cgltf_data *data);
#endif
