#ifndef ATHENA_RENDER3D_H
#define ATHENA_RENDER3D_H
#include <athena/model3d.h>
#include <athena/camera3d.h>
typedef struct AthenaBatch3D AthenaBatch3D;
typedef struct {
    /* submitted includes culled objects; triangles counts output sent to VU1. */
    uint32_t submitted_objects,culled_objects,draw_passes,triangles,vu_batches;
    uint32_t source_triangles,clipped_triangles,rejected_triangles;
} AthenaRender3DStats;
/* Stage 1: opaque color triangle lists; no textures/lighting/transparent sort.
 * draw never advances game state. Inputs are copied into the owned DMA ring,
 * so releasing mesh/instance after draw cannot invalidate pending submission.
 * Precise homogeneous clipping in C for intersecting object bounds; VU1
 * transforms fully contained objects. Clipping uses fixed scratch buffers.
 * GS TEST/ZBUF are restored; Screen must have a depth buffer allocated.
 * Stats accumulate in draw(); batch_draw() resets them for the batch. */
typedef enum { ATHENA_RENDER3D_CULL_NONE=0, ATHENA_RENDER3D_CULL_BACK=1,
    ATHENA_RENDER3D_CULL_FRONT=-1 } AthenaRender3DCull;
int athena_render3d_draw(AthenaInstance3D *instance,AthenaCamera3D *camera,
    AthenaRender3DCull cull,AthenaRender3DStats *stats);
AthenaBatch3D *athena_batch3d_create(void);
void athena_batch3d_destroy(AthenaBatch3D *batch);
int athena_batch3d_add(AthenaBatch3D *batch,AthenaInstance3D *instance);
void athena_batch3d_clear(AthenaBatch3D *batch);
uint32_t athena_batch3d_size(const AthenaBatch3D *batch);
int athena_batch3d_draw(AthenaBatch3D *batch,AthenaCamera3D *camera,
    AthenaRender3DCull cull,AthenaRender3DStats *stats);
void athena_render3d_module_shutdown(void);
#endif
