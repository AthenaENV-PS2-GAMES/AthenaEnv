#ifndef ATHENA_RENDER3D_H
#define ATHENA_RENDER3D_H
#include <athena/model3d.h>
#include <athena/camera3d.h>
#include <athena/lights.h>
typedef struct AthenaBatch3D AthenaBatch3D;
typedef struct {
    /* submitted includes culled objects; triangles counts output sent to VU1.
     * draw_passes counts accepted objects; pipeline_passes counts the GS/VU1
     * passes actually emitted (barriers, program, camera and GS state), which
     * a group shares among consecutive objects of the same pipeline. */
    uint32_t submitted_objects,culled_objects,draw_passes,triangles,vu_batches;
    uint32_t source_triangles,clipped_triangles,rejected_triangles,pipeline_passes;
    /* Copied position/color/normal/UV payload including per-chunk padding.
     * Excludes tags, constants, texture/program uploads, GS state and 2D. */
    uint64_t geometry_bytes;
    /* Objects crossing the screen edges drawn by VU1 without clipping: inside
     * the GS guard band (see athena_render3d_guard_band()). */
    uint32_t guard_band_objects;
    /* Objects crossing the near plane (inside the guard band otherwise)
     * clipped on VU1 by vu1/draw_3D_near.vcl. Their triangles count as
     * submitted: the VU1 output is not read back. */
    uint32_t near_clip_objects;
    /* Meshes with morph targets blended on VU1 (vu1/draw_3D_morph.vcl). */
    uint32_t vu_morph_objects;
} AthenaRender3DStats;
/* The guard band factor of the current screen mode: how far beyond the
 * screen (in screen widths/heights from the centre, at least 1) primitives
 * still fit the GS's coordinate range with a margin. 1 without a GS. */
float athena_render3d_guard_band(void);
/* Opaque unlit/diffuse triangle lists, optionally textured; no transparent sort.
 * draw never advances game state. Inputs are copied into the owned DMA ring,
 * so releasing mesh/instance after draw cannot invalidate pending submission.
 * Precise homogeneous clipping in C for intersecting object bounds; VU1
 * transforms fully contained objects. Clipping uses fixed scratch buffers.
 * GS TEST/ZBUF and textured TEX0/TEX1/CLAMP are restored; Screen needs depth.
 * Stats accumulate in draw(); batch_draw() resets them for the batch. */
typedef enum { ATHENA_RENDER3D_CULL_NONE=0, ATHENA_RENDER3D_CULL_BACK=1,
    ATHENA_RENDER3D_CULL_FRONT=-1 } AthenaRender3DCull;
int athena_render3d_draw(AthenaInstance3D *instance,AthenaCamera3D *camera,
    AthenaRender3DCull cull,AthenaRender3DStats *stats);
/* Borrowed lights, snapshotted into the packet. NULL means black ambient and
 * no directional lights; unlit materials ignore lights. No implicit updates. */
int athena_render3d_draw_lit(AthenaInstance3D *instance,AthenaCamera3D *camera,
    const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *stats);
/* Same pass with an explicit model matrix (e.g. a Scene3D world transform).
 * Mesh and matrix are borrowed for the call; their data is copied to DMA. */
int athena_render3d_draw_mesh(const AthenaMesh3D *mesh,const AthenaMatrix4 *model,
    AthenaCamera3D *camera,const AthenaLights *lights,AthenaRender3DCull cull,
    AthenaRender3DStats *stats);
/* draw_mesh() for streams the caller owns, such as vertices skinned on the CPU
 * this frame: the view (positions, colors, normals, UVs, bounds, material) is
 * copied into the packet during the call, so its buffers can be reused. Joints
 * and weights in the view are ignored. */
int athena_render3d_draw_view(const AthenaMesh3DView *view,const AthenaMatrix4 *model,
    AthenaCamera3D *camera,const AthenaLights *lights,AthenaRender3DCull cull,
    AthenaRender3DStats *stats);
/* Morph targets blended on VU1: weights has view->target_count entries;
 * view->minimum/maximum must bound the morphed mesh (the conservative morph
 * box). Returns 1, drawing nothing, when VU1 cannot take it: normal deltas,
 * more than ATHENA_RENDER3D_MORPH_TARGETS nonzero weights, or bounds not
 * inside the GS guard band (crossing near/far); the caller then blends on
 * the EE and draws the result with athena_render3d_draw_view(). */
#define ATHENA_RENDER3D_MORPH_TARGETS 4u
int athena_render3d_draw_morph(const AthenaMesh3DView *view,const AthenaMatrix4 *model,const float *weights,
    AthenaCamera3D *camera,const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *stats);
/* Skinned meshes deformed on VU1 (vu1/draw_3D_skinned.vcl): palette[j] is
 * world(joint j) * inverse_bind, in world space, for joint_count joints
 * (<= ATHENA_RENDER3D_SKIN_JOINTS). The caller has proved the deformed mesh
 * inside the frustum (conservative bounds); the view needs joints, weights
 * and normals and no texture. Other skinned draws deform on the EE and use
 * draw_view(). Returns -1 for those inputs. */
#define ATHENA_RENDER3D_SKIN_JOINTS 24u
int athena_render3d_draw_skinned_contained(const AthenaMesh3DView *view,const AthenaMatrix4 *palette,
    uint32_t joint_count,AthenaCamera3D *camera,const AthenaLights *lights,AthenaRender3DCull cull,
    AthenaRender3DStats *stats);
/* draw_mesh() for a caller that already proved the mesh bounds under model
 * fully inside the camera frustum, e.g. a conservative world AABB tested
 * INSIDE. Skips the per-object frustum test and never clips; passing a mesh
 * that is not contained draws primitives outside the guard band. */
int athena_render3d_draw_mesh_contained(const AthenaMesh3D *mesh,const AthenaMatrix4 *model,
    AthenaCamera3D *camera,const AthenaLights *lights,AthenaRender3DCull cull,
    AthenaRender3DStats *stats);
/* Between group_begin() and group_end(), consecutive draws that share camera,
 * GS context, VU1 program and texture reuse one pass: the barrier, program,
 * camera constants and GS state are emitted once and only the per-object
 * constants change. The caller must not draw anything else, flip, or change
 * camera or GS state until group_end(), which closes the pass even after a
 * failed draw. Batch and Scene3D draws group internally. begin returns -1 when
 * a group is already open. */
int athena_render3d_group_begin(void);
void athena_render3d_group_end(void);
AthenaBatch3D *athena_batch3d_create(void);
void athena_batch3d_destroy(AthenaBatch3D *batch);
int athena_batch3d_add(AthenaBatch3D *batch,AthenaInstance3D *instance);
void athena_batch3d_clear(AthenaBatch3D *batch);
uint32_t athena_batch3d_size(const AthenaBatch3D *batch);
int athena_batch3d_draw(AthenaBatch3D *batch,AthenaCamera3D *camera,
    AthenaRender3DCull cull,AthenaRender3DStats *stats);
int athena_batch3d_draw_lit(AthenaBatch3D *batch,AthenaCamera3D *camera,
    const AthenaLights *lights,AthenaRender3DCull cull,AthenaRender3DStats *stats);
void athena_render3d_module_shutdown(void);
#endif
