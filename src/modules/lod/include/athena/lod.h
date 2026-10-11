#ifndef ATHENA_LOD_H
#define ATHENA_LOD_H
#include <stdint.h>
#include <athena/scene3d.h>
/* Distance-based level of detail for Scene3D nodes. A group gives a node a
 * list of meshes, each used up to a distance from the camera; beyond the
 * last level (or the global draw distance) the node is hidden. Selection
 * uses the node's world position of the last scene update and changes the
 * node's mesh and visibility only when the level changes, so it runs before
 * Scene3D update() (one frame of latency). A hysteresis band around each
 * threshold stops flicker at the boundary. The group owns the node's
 * visibility while enabled. Main thread only. */
#define ATHENA_LOD_MAX_LEVELS 8u
typedef struct AthenaLODGroup AthenaLODGroup;
typedef struct { AthenaMesh3D *mesh; float until; } AthenaLODLevel;
/* Levels by increasing `until` (> 0); a NULL mesh hides the node in that
 * band (e.g. culled beyond a distance but not yet the last level). Node and
 * meshes are retained. hysteresis: fraction of each threshold, 0..0.5. */
AthenaLODGroup *athena_lod_group_create(AthenaNode3D *node,const AthenaLODLevel *levels,uint32_t count,float hysteresis);
void athena_lod_group_destroy(AthenaLODGroup *group);
void athena_lod_group_set_enabled(AthenaLODGroup *group,int enabled);
int athena_lod_group_enabled(const AthenaLODGroup *group);
/* Current level index, or -1 when hidden; -2 before the first update. */
int athena_lod_group_level(const AthenaLODGroup *group);
float athena_lod_group_distance(const AthenaLODGroup *group);
/* Global multiplier of every threshold (quality setting), default 1. */
int athena_lod_set_bias(float bias);
/* Nodes farther than this are hidden whatever their levels (<= 0: none). */
int athena_lod_set_draw_distance(float distance);
float athena_lod_draw_distance(void);
/* Optional spatial index (off by default). With cell_size > 0 enabled groups are binned by their node's
 * world position into a hash of at most `capacity` cells (1..65535, 0 = 1024; groups beyond that stay in
 * an always-evaluated overflow set, so results never change, only the saving shrinks). Positions are cached
 * and refreshed through Scene3D's moved hooks, so static groups cost nothing per update. A cell is skipped
 * only when every group in it is already hidden by distance (level -1) and the cell is entirely beyond the
 * farthest threshold of those groups (with hysteresis and bias): that is exactly the state the full
 * traversal would recompute, so selection and hysteresis are identical to cell_size = 0. 0 turns it off.
 * -1 on bad arguments or when memory runs out (the index is then off). Tune cell_size to roughly the
 * largest `until` of the groups: much smaller cells cost more cells per update than they save. */
int athena_lod_set_grid(float cell_size,uint32_t capacity);
float athena_lod_grid_cell_size(void);
/* groups: enabled groups; evaluated: groups whose level was computed; cells/cells_skipped: grid cells
 * visited and skipped whole (0 without the grid). */
typedef struct { uint32_t groups,hidden,changes; uint32_t per_level[ATHENA_LOD_MAX_LEVELS]; uint32_t evaluated,cells,cells_skipped; } AthenaLODStats;
/* Selects every enabled group's level for a camera at eye. */
void athena_lod_update(const float eye[3],AthenaLODStats *stats);
void athena_lod_module_shutdown(void);
#endif
