#ifndef ATHENA_NAV_H
#define ATHENA_NAV_H
#include <stdint.h>
#include <athena/scene3d.h>
/* Navigation on an XZ grid: per-cell costs (0 blocked, 1..255 multiplier of
 * the distance walked through the cell), A* with a binary heap over fixed
 * arrays (no allocation per search), 8-way moves without corner cutting,
 * path smoothing across cost-1 cells, and a crowd of agents that follow paths
 * with arrival slow-down and simple separation, optionally driving Scene3D
 * nodes. World x,z map to cell (floor((x - origin_x) / cell),
 * floor((z - origin_z) / cell)). Main thread only. */
#define ATHENA_NAV_MAX_CELLS (512u*512u)
#define ATHENA_NAV_MAX_PATH 512u
typedef struct AthenaNavGrid AthenaNavGrid;
typedef struct AthenaNavCrowd AthenaNavCrowd;
AthenaNavGrid *athena_nav_grid_create(uint32_t width,uint32_t depth,float cell,float origin_x,float origin_z);
void athena_nav_grid_retain(AthenaNavGrid *g);
void athena_nav_grid_release(AthenaNavGrid *g);
void athena_nav_grid_size(const AthenaNavGrid *g,uint32_t *width,uint32_t *depth,float *cell,float *ox,float *oz);
/* Every cell starts walkable with cost 1. */
int athena_nav_set_cost(AthenaNavGrid *g,int32_t cx,int32_t cz,uint8_t cost);
/* Outside the grid reads 0 (blocked). */
uint8_t athena_nav_get_cost(const AthenaNavGrid *g,int32_t cx,int32_t cz);
int athena_nav_fill(AthenaNavGrid *g,int32_t cx0,int32_t cz0,int32_t cx1,int32_t cz1,uint8_t cost);
/* width * depth costs, row-major (x fastest). */
int athena_nav_set_costs(AthenaNavGrid *g,const uint8_t *costs,uint32_t count);
int athena_nav_world_to_cell(const AthenaNavGrid *g,float x,float z,int32_t *cx,int32_t *cz);
/* 1 when every cell the segment crosses is walkable (supercover). */
int athena_nav_line_of_sight(const AthenaNavGrid *g,float x0,float z0,float x1,float z1);
typedef struct { int diagonal,smooth; uint32_t max_iterations; } AthenaNavQuery;
/* Path from (x0,z0) to (x1,z1) as world x,z pairs of cell centres (first
 * the start's, last the exact target), up to max points in out. Returns the
 * point count, 0 when unreachable (or start/target blocked), -1 invalid,
 * -2 when the path is longer than max points. Start and target are kept as
 * given (not snapped). Output is valid only on a positive return; scratch
 * storage is reused from the completed A* heap, without allocation. */
int athena_nav_find_path(AthenaNavGrid *g,float x0,float z0,float x1,float z1,const AthenaNavQuery *q,float *out,uint32_t max);
/* Centre of the walkable cell nearest to x,z within radius cells; 1 found. */
int athena_nav_nearest_walkable(const AthenaNavGrid *g,float x,float z,uint32_t radius,float out[2]);
uint32_t athena_nav_last_expanded(const AthenaNavGrid *g);

typedef enum { ATHENA_NAV_IDLE=0, ATHENA_NAV_MOVING=1, ATHENA_NAV_ARRIVED=2 } AthenaNavState;
typedef struct { float speed,radius; int face; } AthenaNavAgentDesc;
AthenaNavCrowd *athena_nav_crowd_create(AthenaNavGrid *grid);
void athena_nav_crowd_retain(AthenaNavCrowd *c);
void athena_nav_crowd_release(AthenaNavCrowd *c);
#define ATHENA_NAV_MAX_AGENTS 128u
/* Returns the agent id or -1. y is kept for the node position. */
int athena_nav_agent_add(AthenaNavCrowd *c,float x,float y,float z,const AthenaNavAgentDesc *desc);
int athena_nav_agent_remove(AthenaNavCrowd *c,int id);
/* Drives node (retained; NULL stops): position (and yaw when face) each update. */
int athena_nav_agent_bind(AthenaNavCrowd *c,int id,AthenaNode3D *node);
/* Plans a path to x,z with the crowd's query; 1 moving, 0 unreachable. */
int athena_nav_agent_move_to(AthenaNavCrowd *c,int id,float x,float z);
int athena_nav_agent_stop(AthenaNavCrowd *c,int id);
int athena_nav_agent_set_position(AthenaNavCrowd *c,int id,float x,float y,float z);
int athena_nav_agent_set_speed(AthenaNavCrowd *c,int id,float speed);
typedef struct { float x,y,z,vx,vz,yaw,speed; AthenaNavState state; uint32_t waypoint,waypoints; } AthenaNavAgentInfo;
int athena_nav_agent_info(const AthenaNavCrowd *c,int id,AthenaNavAgentInfo *out);
void athena_nav_crowd_set_query(AthenaNavCrowd *c,const AthenaNavQuery *q);
/* Moves every agent by dt seconds. Returns how many arrived this update. */
int athena_nav_crowd_update(AthenaNavCrowd *c,float dt);
#endif
