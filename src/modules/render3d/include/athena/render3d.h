#ifndef ATHENA_RENDER3D_H
#define ATHENA_RENDER3D_H
#include <athena/model3d.h>
#include <athena/camera3d.h>
#include <athena/lights.h>
typedef struct AthenaBatch3D AthenaBatch3D;
typedef struct {
    /* submitted includes culled objects; triangles counts output sent to VU1. */
    uint32_t submitted_objects,culled_objects,draw_passes,triangles,vu_batches;
    uint32_t source_triangles,clipped_triangles,rejected_triangles;
    /* Copied position/color/normal/UV payload including per-chunk padding.
     * Excludes tags, constants, texture/program uploads, GS state and 2D. */
    uint64_t geometry_bytes;
} AthenaRender3DStats;
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
/* draw_mesh() for a caller that already proved the mesh bounds under model
 * fully inside the camera frustum, e.g. a conservative world AABB tested
 * INSIDE. Skips the per-object frustum test and never clips; passing a mesh
 * that is not contained draws primitives outside the guard band. */
int athena_render3d_draw_mesh_contained(const AthenaMesh3D *mesh,const AthenaMatrix4 *model,
    AthenaCamera3D *camera,const AthenaLights *lights,AthenaRender3DCull cull,
    AthenaRender3DStats *stats);
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
