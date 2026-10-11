#ifndef ATHENA_DEBUG3D_H
#define ATHENA_DEBUG3D_H
#include <stdint.h>
#include <athena/camera3d.h>
#include <athena/model3d.h>
#include <athena/graphics/screen_lines.h>
/* World-space debug lines: segments queued by shape helpers, projected with
 * a Camera3D and clipped (near plane and screen edges) on the EE, then drawn
 * as GS lines over the frame, without depth test (always on top).
 *
 * A segment lasts one frame (seconds = 0) or `seconds` of age() time counted
 * from its first draw; age() only removes segments that were drawn at least
 * once, so a segment queued before the first draw is never lost. At most ATHENA_DEBUG3D_MAX_LINES
 * segments are queued: further ones are dropped and counted. Colors are
 * Color.new() values (GS RGBA, alpha 0x80 opaque). Main thread only; no
 * allocation after the first segment. Far-plane clipping is deliberately
 * disabled: this overlay also displays distant diagnostics. Non-finite
 * transformed endpoints are discarded. */
#define ATHENA_DEBUG3D_MAX_LINES 8192u
#define ATHENA_DEBUG3D_MAX_SEGMENTS 64u
/* 0 queued, 1 dropped (full), -1 invalid (non-finite, negative seconds). */
int athena_debug3d_line(const float a[3],const float b[3],uint32_t color,float seconds);
/* Shapes return the number of segments queued, or -1 for invalid input
 * (nothing queued then). model may be NULL (world coordinates). */
int athena_debug3d_box(const float minimum[3],const float maximum[3],const AthenaMatrix4 *model,
    uint32_t color,float seconds);
/* Three great circles (XY, XZ, YZ) of segments (3..MAX_SEGMENTS) each. */
int athena_debug3d_sphere(const float center[3],float radius,uint32_t segments,uint32_t color,float seconds);
/* The X (red), Y (green) and Z (blue) axes of matrix, size units long. */
int athena_debug3d_axes(const AthenaMatrix4 *matrix,float size,float seconds);
/* Square grid on the XZ plane around center: half_extent units each way,
 * a line every step (at most 2 * 128 + 2 lines). */
int athena_debug3d_grid(const float center[3],float half_extent,float step,uint32_t color,float seconds);
/* The view volume of camera (near and far rectangles and the edges). */
int athena_debug3d_frustum(AthenaCamera3D *camera,uint32_t color,float seconds);
/* Mesh normals from each stream vertex, length units long, under model
 * (NULL: identity). Uses inverse transpose of the linear 3x3 under model;
 * singular/ill-conditioned matrices (scaled determinant <=1e-7) return -1
 * before queueing anything. Meshes without normals queue nothing (returns 0). */
int athena_debug3d_normals(const AthenaMesh3D *mesh,const AthenaMatrix4 *model,float length,
    uint32_t color,float seconds);

/* Screen segment produced by project(): pixels, origin top-left. */
typedef AthenaScreenLine AthenaDebug3DSegment;
typedef void (*AthenaDebug3DEmit)(void *opaque,const AthenaDebug3DSegment *segment);
/* Projects every queued segment with camera to a width x height viewport,
 * clipping against the near plane and the screen edges; emits the visible
 * parts and marks all segments drawn. Returns the number emitted, or -1 for
 * an invalid camera or viewport. */
int athena_debug3d_project(AthenaCamera3D *camera,float width,float height,AthenaDebug3DEmit emit,void *opaque);
/* Batched equivalent, preserving order/color. The callback must consume/copy
 * the borrowed segments before returning; never retain them for DMA. Queue
 * mutation and reentrant projection from callbacks are unsupported. */
typedef void (*AthenaDebug3DEmitBatch)(void *opaque,const AthenaDebug3DSegment *segments,uint32_t count);
int athena_debug3d_project_batch(AthenaCamera3D *camera,float width,float height,AthenaDebug3DEmitBatch emit,void *opaque);
/* Optional CPU diagnostics. Disabled by default; no clock reads then.
 * Stats describe the most recent projection/draw (including zero lines).
 * Tick sums are wrap-safe for a call shorter than ~14.5 seconds on EE.
 * transform includes packing and VU0 perspective/viewport for unchanged
 * endpoints; clip includes C clipping/mapping of adjusted endpoints; emit
 * includes callbacks/GS packet construction, never an explicit GS wait.
 * Native total excludes GS 2D-view save/restore and age(). */
typedef struct {
    uint32_t input,emitted,rejected,batches;
    uint32_t transform_ticks,clip_ticks,emit_ticks,total_ticks;
    uint32_t fallback_batches; /* EE batches using C for extreme operands */
    int profiling,vu0;
} AthenaDebug3DStats;
void athena_debug3d_profile(int enabled);
void athena_debug3d_stats(AthenaDebug3DStats *out);
/* project() into GS lines on the current framebuffer (debug3d_gs.c). The 2D
 * view (Camera2D) is set to the identity during the call and restored. */
int athena_debug3d_draw(AthenaCamera3D *camera);
/* Ages segments by dt seconds (>= 0) and removes the expired drawn ones. */
void athena_debug3d_age(float dt);
void athena_debug3d_clear(void);
uint32_t athena_debug3d_count(void);
/* Segments dropped because the queue was full, since the last clear. */
uint32_t athena_debug3d_dropped(void);
/* While disabled, the queueing functions return 0 and queue nothing. */
void athena_debug3d_set_enabled(int enabled);
int athena_debug3d_enabled(void);
void athena_debug3d_module_shutdown(void);
#endif
