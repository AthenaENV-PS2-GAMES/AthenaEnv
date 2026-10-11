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
/* External files a glTF/GLB names by relative URI (buffers and images), as the
 * loader's explicit resolver table: the caller reads each one (the loader never
 * does I/O) and provides borrowed bytes that must outlive the load. uri is
 * stored percent-decoded. */
/* image: 1 when only images use the file (informational; lookups go by uri). */
typedef struct { char *uri; int image; const void *bytes; size_t size; } AthenaGltfDependency;
typedef struct { AthenaGltfDependency *items; uint32_t count,provided; } AthenaGltfDependencies;
#define ATHENA_GLTF_MAX_DEPENDENCIES 64u
void athena_model3d_gltf_deps_release(AthenaGltfDependencies *deps);
/* Parses a GLB or a glTF JSON document in memory. Buffers/images with a relative
 * file URI become dependencies (absolute, scheme, drive and ".." URIs are rejected);
 * data: URIs and embedded BIN need none. Count and size limits apply. */
int athena_model3d_gltf_parse_external(const void *bytes,size_t size,cgltf_data **out,AthenaGltfDependencies *deps);
/* Records borrowed bytes (<= MEMORY_MAX_BYTES) for dependency `index`, once. */
int athena_model3d_gltf_deps_provide(AthenaGltfDependencies *deps,uint32_t index,const void *bytes,size_t size);
/* Once every dependency is provided: binds all buffers (BIN, dependencies, base64). */
int athena_model3d_gltf_bind_buffers(cgltf_data *data,const AthenaGltfDependencies *deps);
typedef struct { AthenaGltfTextureEntry *items; uint32_t count,capacity; const AthenaGltfDependencies *deps; } AthenaGltfTextureCache;
void athena_model3d_gltf_cache_release(AthenaGltfTextureCache *cache);
/* cache may be NULL (no sharing). */
int athena_model3d_gltf_primitive(const char *path,cgltf_primitive *prim,
    const AthenaMaterial3D *material_override,AthenaGltfTextureCache *cache,AthenaMesh3D **out);
/* The same conversion as a resumable build (see athena_mesh3d_builder_step):
 * material and texture decode, one morph target attribute, a slice of vertex
 * or index reads, or a Mesh3D builder stage per step (max_items 0: default).
 * path, prim, the override material and cache stay borrowed until destroy;
 * texture decode and a target attribute are single atomic steps. Step
 * returns 0 while work remains, 1 when finished, or a negative result
 * (sticky; the detail explains it). Take once, then destroy. */
typedef struct AthenaGltfPrimitiveBuild AthenaGltfPrimitiveBuild;
int athena_model3d_gltf_primitive_begin(const char *path,cgltf_primitive *prim,
    const AthenaMaterial3D *material_override,AthenaGltfTextureCache *cache,AthenaGltfPrimitiveBuild **out);
int athena_model3d_gltf_primitive_step(AthenaGltfPrimitiveBuild *build,uint32_t max_items);
int athena_model3d_gltf_primitive_take(AthenaGltfPrimitiveBuild *build,AthenaMesh3D **out);
void athena_model3d_gltf_primitive_destroy(AthenaGltfPrimitiveBuild *build);
/* Required extensions both loaders implement or may ignore; others are
 * rejected with a detail naming them. */
int athena_model3d_gltf_extensions(const cgltf_data *data);
/* Parses a self-contained GLB and binds its BIN without reading any files.
 * Rejects all buffer URIs and external image URIs before construction.
 * An unaligned input copy, if needed, is owned by data and freed by cgltf_free. */
int athena_model3d_gltf_parse_memory(const void *bytes,size_t size,cgltf_data **out);
#endif
