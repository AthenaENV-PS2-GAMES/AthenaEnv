#ifndef ATHENA_GLTF3D_H
#define ATHENA_GLTF3D_H
#include <stdint.h>
#include <athena/model3d.h>
#include <athena/scene3d.h>
#include <athena/animation3d.h>
/* glTF 2.0 / GLB scenes: the node hierarchy becomes Scene3D nodes (TRS, or a
 * matrix decomposed into TRS), every triangle primitive a Model3D mesh, and
 * every animation an Animation3D clip whose targets are node indices, so
 * athena_player3d_create(clip, nodes, count) plays it on the loaded nodes.
 * Skins become AthenaSkin3D on their nodes (JOINTS_0/WEIGHTS_0, inverse bind
 * matrices), morph targets and CUBICSPLINE tracks within Model3D/Animation3D
 * limits. The KHR extensions Model3D rejects are not supported. */
typedef struct AthenaGltf3D AthenaGltf3D;
/* material, when given, replaces every primitive's material (Model3D.load). */
int athena_gltf3d_load(const char *path,const AthenaMaterial3D *material,AthenaGltf3D **out);
/* Borrowed self-contained GLB bytes, <= ATHENA_MODEL3D_MEMORY_MAX_BYTES.
 * origin is a diagnostic name only. No external I/O or GS upload; copies
 * geometry, skins, morphs, animations and decoded pixels before returning.
 * Unaligned input needs a temporary aligned copy. Main thread only. */
int athena_gltf3d_load_memory(const void *bytes,size_t size,const char *origin,
    const AthenaMaterial3D *material,AthenaGltf3D **out);
void athena_gltf3d_release(AthenaGltf3D *scene);

/* Resumable GLB loading. begin_memory copies the origin string, retains
 * material.texture and allocates the loader's own copy of the bytes
 * (<= MEMORY_MAX_BYTES); the bytes are *borrowed* and read by the first steps
 * (the copy phase, COPY_CHUNK bytes each), so they must stay valid and
 * unchanged until athena_gltf3d_loader_source_needed() returns 0 (also
 * after an error or destroy). Each later step does one bounded unit of
 * main-thread work: the parse, validation, one primitive (decoding its
 * embedded textures) or meshless node, the hierarchy links, one skin or one
 * animation. A single unit is atomic and has no time limit. Step returns 0
 * while work remains, 1 when finished, or a negative AthenaModel3DResult
 * (sticky; athena_model3d_detail() describes it). Take the scene once done.
 * Destroy at any point frees everything not taken. No GS upload; main
 * thread only. */
typedef struct AthenaGltf3DLoader AthenaGltf3DLoader;
#define ATHENA_GLTF3D_COPY_CHUNK (256u*1024u)
typedef enum { ATHENA_GLTF3D_PHASE_COPY=0, ATHENA_GLTF3D_PHASE_PARSE, ATHENA_GLTF3D_PHASE_DEPENDENCIES, ATHENA_GLTF3D_PHASE_VALIDATE, ATHENA_GLTF3D_PHASE_NODES,
    ATHENA_GLTF3D_PHASE_LINKS, ATHENA_GLTF3D_PHASE_SKINS, ATHENA_GLTF3D_PHASE_CLIPS,
    ATHENA_GLTF3D_PHASE_DONE } AthenaGltf3DPhase;
int athena_gltf3d_loader_begin_memory(const void *bytes,size_t size,const char *origin,
    const AthenaMaterial3D *material,AthenaGltf3DLoader **out);
int athena_gltf3d_loader_step(AthenaGltf3DLoader *loader);
/* Explicit resolver for glTF/GLB files that name external buffers and images by
 * relative URI (the loader performs no I/O). begin_external takes a GLB or a
 * glTF JSON document like begin_memory. After the parse step the phase is
 * DEPENDENCIES and the caller reads each listed file and passes its bytes to
 * athena_gltf3d_loader_provide (borrowed, <= MEMORY_MAX_BYTES each, at most
 * 64 files): they must stay valid and unchanged until the loader is taken or
 * destroyed. Step returns ATHENA_GLTF3D_WAITING (no work done) until all are
 * provided. URIs are percent-decoded; absolute, scheme, drive and ".." URIs
 * are rejected, data: URIs and the GLB BIN need no file. */
#define ATHENA_GLTF3D_WAITING 2
/* begin_memory/begin_external with flags (ATHENA_LOADER_EXTERNAL, ATHENA_LOADER_BORROW from
 * model3d.h). BORROW with 4-byte aligned bytes skips the copy phase: the bytes are used in
 * place and must stay valid and unchanged until take or destroy (source_needed stays 1);
 * unaligned input falls back to the snapshot copy. */
int athena_gltf3d_loader_begin(const void *bytes,size_t size,const char *origin,
    const AthenaMaterial3D *material,int flags,AthenaGltf3DLoader **out);
/* Runs at least one unit, then more while less than budget_ms elapsed (budget_ms <= 0:
 * exactly one); *ran counts the units. Returns like step; WAITING leaves *ran at 0. */
int athena_gltf3d_loader_run(AthenaGltf3DLoader *loader,float budget_ms,uint32_t *ran);
/* Units, total and largest unit time of a phase (AthenaGltf3DPhase) so far. */
void athena_gltf3d_loader_phase_stats(const AthenaGltf3DLoader *loader,int phase,AthenaLoaderPhaseStats *out);
int athena_gltf3d_loader_begin_external(const void *bytes,size_t size,const char *origin,
    const AthenaMaterial3D *material,AthenaGltf3DLoader **out);
uint32_t athena_gltf3d_loader_dependency_count(const AthenaGltf3DLoader *loader);
const char *athena_gltf3d_loader_dependency_uri(const AthenaGltf3DLoader *loader,uint32_t index);
int athena_gltf3d_loader_dependency_image(const AthenaGltf3DLoader *loader,uint32_t index);
int athena_gltf3d_loader_dependency_provided(const AthenaGltf3DLoader *loader,uint32_t index);
int athena_gltf3d_loader_provide(AthenaGltf3DLoader *loader,uint32_t index,const void *bytes,size_t size);
/* 1 while the caller's bytes may still be read by a step. */
int athena_gltf3d_loader_source_needed(const AthenaGltf3DLoader *loader);
/* Phase the last step ran, and the next one. */
int athena_gltf3d_loader_last_phase(const AthenaGltf3DLoader *loader);
int athena_gltf3d_loader_phase(const AthenaGltf3DLoader *loader);
/* Units finished and total; total is 0 until validation has counted them. */
void athena_gltf3d_loader_progress(const AthenaGltf3DLoader *loader,uint32_t *done,uint32_t *total);
/* Transfers the finished scene to the caller (release it); once only. */
int athena_gltf3d_loader_take(AthenaGltf3DLoader *loader,AthenaGltf3D **out);
void athena_gltf3d_loader_destroy(AthenaGltf3DLoader *loader);
/* A group holding the default scene's root nodes; borrowed. */
AthenaNode3D *athena_gltf3d_root(const AthenaGltf3D *scene);
/* Every glTF node, in file order (the clips' target indices); borrowed. */
uint32_t athena_gltf3d_node_count(const AthenaGltf3D *scene);
AthenaNode3D *athena_gltf3d_node(const AthenaGltf3D *scene,uint32_t index);
/* Node and clip names; "" when the file gives none. */
const char *athena_gltf3d_node_name(const AthenaGltf3D *scene,uint32_t index);
uint32_t athena_gltf3d_clip_count(const AthenaGltf3D *scene);
AthenaClip3D *athena_gltf3d_clip(const AthenaGltf3D *scene,uint32_t index);
const char *athena_gltf3d_clip_name(const AthenaGltf3D *scene,uint32_t index);
#endif
