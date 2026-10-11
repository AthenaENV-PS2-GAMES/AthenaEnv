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
typedef struct AthenaNavRequest AthenaNavRequest;
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
 * -2 when the path is longer than max points, -3 when incremental requests
 * are pending (scratch is reserved; the existing search is not changed).
 * Start and target are kept as
 * given (not snapped). Output is valid only on a positive return; scratch
 * storage is reused from the completed A* heap, without allocation. */
int athena_nav_find_path(AthenaNavGrid *g,float x0,float z0,float x1,float z1,const AthenaNavQuery *q,float *out,uint32_t max);
/* Centre of the walkable cell nearest to x,z within radius cells; 1 found. */
int athena_nav_nearest_walkable(const AthenaNavGrid *g,float x,float z,uint32_t radius,float out[2]);
uint32_t athena_nav_last_expanded(const AthenaNavGrid *g);

/* Incremental queries, FIFO serialized per grid: one search owns the six
 * shared cell arrays until smoothing/output finish. Creation allocates a
 * request and 8 * max_points bytes of owned output, not another cell scratch.
 * Up to 16 pending requests (including the active one); completed requests
 * do not occupy the queue. Retain/release and all calls are main-thread only.
 * A request retains the grid; the queue borrows the request, so releasing
 * the last request reference cancels/unlinks it without a retain cycle.
 * Mutations that change costs invalidate ALL pending requests (EREVISION).
 * cancel_requests is the explicit grid-disposal boundary. Synchronous
 * findPath/moveTo report EBUSY while requests are queued, preserving scratch
 * and the moving agent's previous path. No implicit Loop scheduling.
 * FIFO is fair by arrival: every earlier request has a finite expansion
 * limit; one grid.step budget is shared by all phases/requests. */
#define ATHENA_NAV_MAX_PENDING 16u
enum { ATHENA_NAV_EINVAL=-1, ATHENA_NAV_EOUTPUT=-2, ATHENA_NAV_EBUSY=-3,
    ATHENA_NAV_EFULL=-4, ATHENA_NAV_EREVISION=-5, ATHENA_NAV_EITERATIONS=-6,
    ATHENA_NAV_ENOMEM=-7 };
typedef enum { ATHENA_NAV_NOT_REQUESTED=-1, ATHENA_NAV_PENDING=0, ATHENA_NAV_FOUND, ATHENA_NAV_NO_PATH,
    ATHENA_NAV_CANCELLED, ATHENA_NAV_ERROR } AthenaNavRequestState;
typedef enum { ATHENA_NAV_QUEUED=0, ATHENA_NAV_INITIALIZE, ATHENA_NAV_CLEAR_STAMPS,
    ATHENA_NAV_SEARCH, ATHENA_NAV_COUNT_PATH, ATHENA_NAV_TRACE_PATH,
    ATHENA_NAV_EMIT_PATH, ATHENA_NAV_SMOOTH_PATH, ATHENA_NAV_FINISHED } AthenaNavRequestPhase;
typedef struct {
    AthenaNavRequestState state; AthenaNavRequestPhase phase; int error;
    uint32_t revision,expanded; uint64_t work; uint32_t points,storage_bytes;
    float cost;
} AthenaNavRequestInfo;
/* max_points: 2..MAX_CELLS+1. NULL with error EINVAL/EFULL/ENOMEM on failure.
 * Coordinates outside the grid/blocked cells become NO_PATH during step.
 * Invalid/extreme coordinates are rejected at creation. No output is exposed
 * until FOUND; maxIterations is a total lifetime expansion limit. */
AthenaNavRequest *athena_nav_request_create(AthenaNavGrid *g,float x0,float z0,float x1,float z1,
    const AthenaNavQuery *query,uint32_t max_points,int *error);
void athena_nav_request_retain(AthenaNavRequest *request);
void athena_nav_request_release(AthenaNavRequest *request);
void athena_nav_request_cancel(AthenaNavRequest *request);
int athena_nav_request_info(const AthenaNavRequest *request,AthenaNavRequestInfo *out);
/* Borrowed immutable x,z pairs until request release. NULL unless FOUND. */
const float *athena_nav_request_path(const AthenaNavRequest *request,uint32_t *points);
/* Copies only a found result; returns points, 0 unless FOUND, EOUTPUT if
 * capacity is too small (no partial write), EINVAL for invalid arguments. */
int athena_nav_request_copy(const AthenaNavRequest *request,float *out,uint32_t max_points);
typedef struct { uint32_t max_work; float budget_ms; } AthenaNavBudget;
/* One work unit: initialization, one stamp clear, expansion, parent trace,
 * path emission or one cell of smoothing LOS. max_work: 1..MAX_CELLS;
 * budget_ms: 0 disables the clock limit, otherwise finite <=1000.
 * Stops at either limit, permitting at most one unit
 * of clock overshoot plus call overhead.
 * Even tiny positive budgets progress. NO allocations in step. Returns work
 * units performed or EINVAL; initialization/reconstruction/smoothing are
 * included. Clock spans must stay below the EE wrap interval (~14.5 s). */
int athena_nav_step_requests(AthenaNavGrid *g,const AthenaNavBudget *budget);
void athena_nav_cancel_requests(AthenaNavGrid *g);
uint32_t athena_nav_pending_requests(const AthenaNavGrid *g);
uint32_t athena_nav_grid_revision(const AthenaNavGrid *g);

typedef enum { ATHENA_NAV_IDLE=0, ATHENA_NAV_MOVING=1, ATHENA_NAV_ARRIVED=2,
    ATHENA_NAV_PLANNING=3 } AthenaNavState;
typedef struct { float speed,radius; int face; } AthenaNavAgentDesc;
AthenaNavCrowd *athena_nav_crowd_create(AthenaNavGrid *grid);
void athena_nav_crowd_retain(AthenaNavCrowd *c);
void athena_nav_crowd_release(AthenaNavCrowd *c);
/* Cancels only this crowd's pending agent-owned requests. Completed results
 * remain FOUND. Retained agents remain usable. */
void athena_nav_crowd_cancel_requests(AthenaNavCrowd *c);
#define ATHENA_NAV_MAX_AGENTS 128u
/* Returns the agent id or -1. y is kept for the node position. */
int athena_nav_agent_add(AthenaNavCrowd *c,float x,float y,float z,const AthenaNavAgentDesc *desc);
int athena_nav_agent_remove(AthenaNavCrowd *c,int id);
/* Drives node (retained; NULL stops): position (and yaw when face) each update. */
int athena_nav_agent_bind(AthenaNavCrowd *c,int id,AthenaNode3D *node);
/* Plans a path to x,z with the crowd's query; 1 moving, 0 unreachable. */
int athena_nav_agent_move_to(AthenaNavCrowd *c,int id,float x,float z);
/* Enqueues a path using the crowd's query. On success the agent pauses in
 * PLANNING (including separation), abandoning its previous route. Grid.step
 * adopts a found path and starts MOVING in the completion work unit; bounded
 * copy of <= MAX_PATH points is included in that unit. No allocation in step.
 * Failure/cancellation stops the agent; planning_state/error keep its outcome.
 * One owned request per agent. Replacement cancels the old request and joins
 * the FIFO tail, even at capacity. Creation failure leaves motion and any
 * previous request unchanged. Caller receives a retained request; releasing
 * that observer does not stop planning. Explicit cancel does. The crowd owns
 * another reference until completion/cancellation; the request retains only
 * the grid, never the crowd. stop/set_position/remove/crowd disposal cancel
 * pending plans. Query changes affect future requests only. */
AthenaNavRequest *athena_nav_agent_request_move_to(AthenaNavCrowd *c,int id,float x,float z,int *error);
int athena_nav_agent_stop(AthenaNavCrowd *c,int id);
int athena_nav_agent_set_position(AthenaNavCrowd *c,int id,float x,float y,float z);
int athena_nav_agent_set_speed(AthenaNavCrowd *c,int id,float speed);
typedef struct {
    float x,y,z,vx,vz,yaw,speed; AthenaNavState state; uint32_t waypoint,waypoints;
    /* NOT_REQUESTED until the first incremental plan, reset by synchronous moveTo. */
    AthenaNavRequestState planning_state; int planning_error;
} AthenaNavAgentInfo;
int athena_nav_agent_info(const AthenaNavCrowd *c,int id,AthenaNavAgentInfo *out);
void athena_nav_crowd_set_query(AthenaNavCrowd *c,const AthenaNavQuery *q);
/* Moves every agent by dt seconds. Returns how many arrived this update. */
int athena_nav_crowd_update(AthenaNavCrowd *c,float dt);
#endif
