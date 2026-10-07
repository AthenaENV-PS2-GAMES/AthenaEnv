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
 * matrices). Not supported: morph targets and the KHR extensions Model3D rejects;
 * CUBICSPLINE samplers keep their keyframe values and play linearly. */
typedef struct AthenaGltf3D AthenaGltf3D;
/* material, when given, replaces every primitive's material (Model3D.load). */
int athena_gltf3d_load(const char *path,const AthenaMaterial3D *material,AthenaGltf3D **out);
void athena_gltf3d_release(AthenaGltf3D *scene);
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
