#ifndef ATHENA_COLLISION_H
#define ATHENA_COLLISION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Light 2D collision and simple physics, for games that want predictable,
 * tile-friendly movement rather than a rigid body simulation (that is
 * Box2D). A world holds axis-aligned rectangles and circles in a spatial
 * hash, and optionally a grid of tiles (solid, one-way platforms and floor
 * slopes). Bodies move with sweeps along each axis, so they slide along
 * walls, land on floors and never tunnel through thin tiles.
 *
 * Everything but athena_collision_draw_debug() is plain math with no GS
 * access, so it runs and is tested on a host. A C game loop:
 *
 *     AthenaCollisionWorldDesc wdesc;
 *     AthenaCollisionBodyDesc bdesc;
 *     AthenaBodyId player;
 *
 *     athena_collision_world_desc_init(&wdesc);
 *     wdesc.gravity_y = 900.0f;
 *     AthenaCollisionWorld *world = athena_collision_world_create(&wdesc);
 *     athena_collision_body_desc_init(&bdesc);
 *     bdesc.type = ATHENA_BODY_DYNAMIC;
 *     bdesc.x = 32; bdesc.y = 32; bdesc.w = 16; bdesc.h = 24;
 *     athena_collision_add(world, &bdesc, &player);
 *     ...athena_collision_set_grid() with the level...
 *     for (;;) {
 *         AthenaBody *body = athena_collision_get(world, player);
 *         body->vx = input * 120.0f;
 *         if (jump && body->on_ground)
 *             body->vy = -320.0f;
 *         athena_collision_step(world, dt);
 *         ...draw at body->x, body->y...
 *     }
 *
 * Units: positions in world units (y pointing down), velocities in units
 * per second, gravity in units per second squared, time in seconds. A
 * rectangle is placed by its top-left corner, a circle by its center.
 *
 * Movement is split in two sweeps, x then y. Against slopes, one-way
 * platforms and when stepping up, circles act as their bounding box.
 */

/* Largest coordinate or size accepted by the JavaScript binding. */
#define ATHENA_COLLISION_MAX_COORD 1.0e7f
/* Most bodies in a world: ids keep the slot in 20 bits. */
#define ATHENA_COLLISION_MAX_BODIES 0xFFFFFu
/* Most columns or rows of a grid. */
#define ATHENA_COLLISION_MAX_GRID_SIDE 4096u
/* Most tiles in a grid: 2 MB of ids (1024 x 1024, 4096 x 256...). */
#define ATHENA_COLLISION_MAX_TILES (1u << 20)
/*
 * Distance under which surfaces touch without overlapping: a body resting
 * on a floor or sliding along a wall is not blocked by it.
 */
#define ATHENA_COLLISION_EPSILON 0.001f
/* How far a body may sit inside a one-way platform or slope and still land. */
#define ATHENA_COLLISION_TOLERANCE 0.01f

/* Error codes: negative results of the functions below. */
#define ATHENA_COLLISION_EINVAL (-1)
#define ATHENA_COLLISION_ENOMEM (-2)
#define ATHENA_COLLISION_ERANGE (-3)

typedef enum {
	ATHENA_SHAPE_RECT,
	ATHENA_SHAPE_CIRCLE
} AthenaShapeType;

typedef enum {
	/* Never moved by a step; blocks others. Walls, crates. */
	ATHENA_BODY_STATIC,
	/*
	 * Moved by its velocity in each step, through everything: moving
	 * platforms, elevators. Blocks others and carries dynamic bodies that
	 * stand on it.
	 */
	ATHENA_BODY_KINEMATIC,
	/* Gravity, damping and velocity in each step, blocked by others. */
	ATHENA_BODY_DYNAMIC
} AthenaBodyType;

/*
 * A body id: slot in the low 20 bits, plus one, and a generation above it,
 * so an id of a removed body never names the body that reuses its slot.
 * 0 is never a valid id.
 */
typedef uint32_t AthenaBodyId;

#define ATHENA_BODY_NONE 0u
#define ATHENA_COLLISION_ID_SLOT(id) (((id) & 0xFFFFFu) - 1u)

/*
 * A shape outside a world, for the stateless tests: a rectangle (x, y is its
 * top-left corner) or a circle (x, y is its center).
 */
typedef struct {
	AthenaShapeType type;
	float x, y;
	float w, h;
	float r;
} AthenaShape;

/*
 * One blocking contact: another body (`other`) or a tile (`tile` >= 0 at
 * `column`, `row`), and the normal of the surface hit, pointing away from
 * it: (0, -1) for a floor.
 */
typedef struct {
	AthenaBodyId other;
	int32_t tile;
	int32_t column, row;
	float normal_x, normal_y;
} AthenaCollisionContact;

/*
 * A body. Read any field; write velocities, filters and physics settings
 * directly. After writing a position or a size, call
 * athena_collision_refresh() (or use the setters), which updates the
 * spatial hash.
 */
typedef struct {
	AthenaShapeType shape;
	AthenaBodyType type;
	/* Rectangle: top-left corner and size. Circle: center and radius r. */
	float x, y, w, h, r;
	float vx, vy;
	/*
	 * Two bodies collide when each one's mask has a bit of the other's
	 * layer. Tiles collide with bodies whose mask has a bit of the grid's
	 * layer.
	 */
	uint32_t layer, mask;
	/* Never blocks nor is blocked: only found by queries and pairs. */
	bool sensor;
	/* Blocks only bodies coming from above: jump-through platforms. */
	bool one_way;
	/* Falls through one-way bodies and tiles while set. */
	bool drop_through;
	/* Dynamic bodies: gravity multiplier (default 1). */
	float gravity_scale;
	/* Dynamic bodies: velocity decay rate in 1/s (0 = none). */
	float damping;
	/* Dynamic bodies: velocity kept after hitting something, 0..1. */
	float bounce;
	/* Dynamic bodies: speed limits per axis (0 = none). */
	float max_speed_x, max_speed_y;

	/* Contact state after the last move or step of this body. */
	bool on_ground;
	bool on_ceiling;
	/* -1: blocked moving left, 1: moving right, 0: not blocked. */
	int8_t on_wall;
	/* Body stood on, or ATHENA_BODY_NONE (air or a tile). */
	AthenaBodyId ground;
	/*
	 * A kinematic body pushed this body into something solid during the
	 * current step (or athena_collision_translate() since it): it is caught
	 * between them. Cleared when the next step starts.
	 */
	bool crushed;

	/* Free for the application (the JavaScript binding keeps its object). */
	void *user;
} AthenaBody;

typedef struct {
	AthenaShapeType shape;
	AthenaBodyType type;
	float x, y, w, h, r;
	float vx, vy;
	uint32_t layer, mask;
	bool sensor, one_way;
	float gravity_scale, damping, bounce;
	float max_speed_x, max_speed_y;
	void *user;
} AthenaCollisionBodyDesc;

/* Defaults: a static rectangle, layer 1, mask all, gravity scale 1. */
void athena_collision_body_desc_init(AthenaCollisionBodyDesc *desc);

/* Tile kinds of a grid. */
typedef enum {
	ATHENA_TILE_EMPTY,
	ATHENA_TILE_SOLID,
	/* Blocks only from above, unless the body has drop_through. */
	ATHENA_TILE_ONE_WAY,
	/*
	 * A floor whose height goes linearly from `left` to `right` (fractions
	 * of the tile height, 0 = the tile's bottom, 1 = its top); below it the
	 * tile is solid. Walked up and down smoothly; the high side is a wall.
	 */
	ATHENA_TILE_SLOPE
} AthenaTileKindType;

typedef struct {
	AthenaTileKindType type;
	float left, right;
} AthenaTileKind;

/*
 * A grid of tiles: `columns` x `rows` cells of tile_width x tile_height,
 * its top-left corner at (x, y). `tiles` (row-major, copied; NULL for all 0)
 * holds tile ids, as TileMap does, and `kinds[id]` says how each id
 * collides: ids of `kind_count` or more are empty.
 */
typedef struct {
	uint32_t columns, rows;
	float tile_width, tile_height;
	float x, y;
	const uint16_t *tiles;
	const AthenaTileKind *kinds;
	uint32_t kind_count;
	uint32_t layer;
} AthenaCollisionGridDesc;

typedef struct AthenaCollisionWorld AthenaCollisionWorld;

typedef struct {
	/* Side of the spatial hash cells: about the size of a typical body. */
	float cell_size;
	float gravity_x, gravity_y;
} AthenaCollisionWorldDesc;

/* Defaults: cells of 64, no gravity. */
void athena_collision_world_desc_init(AthenaCollisionWorldDesc *desc);

/* A new, empty world, or NULL when memory is short or desc is invalid. */
AthenaCollisionWorld *athena_collision_world_create(const AthenaCollisionWorldDesc *desc);
void athena_collision_world_destroy(AthenaCollisionWorld *world);

void athena_collision_set_gravity(AthenaCollisionWorld *world, float x, float y);
void athena_collision_get_gravity(const AthenaCollisionWorld *world, float *x, float *y);
float athena_collision_cell_size(const AthenaCollisionWorld *world);

/*
 * Called by athena_collision_step() when a dynamic body begins a blocking
 * contact, after that body moved: it lands or steps onto another body, or
 * starts pushing a wall or a ceiling. Resting contacts (standing, walking
 * along a floor, pushing the same wall) are not repeated every step. The
 * world must not be changed from it: collect the contacts and act on them
 * after the step.
 */
typedef void (*AthenaCollisionContactFunc)(void *opaque, AthenaBodyId body,
	const AthenaCollisionContact *contact);

void athena_collision_set_contact_func(AthenaCollisionWorld *world,
	AthenaCollisionContactFunc func, void *opaque);

/*
 * Called at the end of athena_collision_step() when a sensor starts
 * (`entered`) or stops overlapping another body that it collides with
 * (layers and masks): pickups, damage zones, triggers. Pairs of two static
 * bodies are left out; a pair of sensors is reported once. A body removed
 * while overlapping gets no exit. Overlaps are only tracked while a
 * function is set. As with contacts, the world must not change from it.
 */
typedef void (*AthenaCollisionSensorFunc)(void *opaque, AthenaBodyId sensor,
	AthenaBodyId other, bool entered);

void athena_collision_set_sensor_func(AthenaCollisionWorld *world,
	AthenaCollisionSensorFunc func, void *opaque);

/* ---- Bodies ------------------------------------------------------------ */

/*
 * Adds a body and stores its id in `*id`. Returns 0, EINVAL for a size that
 * is not positive, or ENOMEM (also past ATHENA_COLLISION_MAX_BODIES).
 */
int athena_collision_add(AthenaCollisionWorld *world,
	const AthenaCollisionBodyDesc *desc, AthenaBodyId *id);

/* Removes a body; false when `id` is not in the world. */
bool athena_collision_remove(AthenaCollisionWorld *world, AthenaBodyId id);

/* The body, or NULL when `id` is not in the world. */
AthenaBody *athena_collision_get(AthenaCollisionWorld *world, AthenaBodyId id);

/* Bodies in the world. */
uint32_t athena_collision_body_count(const AthenaCollisionWorld *world);

/*
 * Slots, to visit every body: slot `i` of [0, capacity) holds a body or is
 * free (NULL). athena_collision_slot_id() gives the id of a slot's body.
 */
uint32_t athena_collision_body_capacity(const AthenaCollisionWorld *world);
AthenaBody *athena_collision_slot(AthenaCollisionWorld *world, uint32_t slot);
AthenaBodyId athena_collision_slot_id(const AthenaCollisionWorld *world, uint32_t slot);

/* Updates the spatial hash after a body's position, size or shape changed. */
void athena_collision_refresh(AthenaCollisionWorld *world, AthenaBodyId id);
/*
 * Teleports a body, without collisions. Its contact state is cleared: the
 * next move or step finds its contacts again (and reports them as new).
 */
void athena_collision_set_position(AthenaCollisionWorld *world, AthenaBodyId id,
	float x, float y);

/*
 * Moves a body by (dx, dy) without collisions. A kinematic body carries the
 * dynamic bodies standing on it and pushes those in its way (see
 * athena_collision_step()): this is how a platform animated by the
 * application (a tween, a path) moves its riders. Other bodies are
 * teleported, as by athena_collision_set_position().
 */
void athena_collision_translate(AthenaCollisionWorld *world, AthenaBodyId id,
	float dx, float dy);

/* Axis-aligned bounds of a body. */
void athena_collision_body_bounds(const AthenaBody *body, float *x0, float *y0,
	float *x1, float *y1);

/* ---- Tiles ------------------------------------------------------------- */

/*
 * Uses a grid of tiles, replacing the previous one. Returns 0, EINVAL (a
 * size of 0, a bad kind) or ENOMEM; on error the old grid stays.
 */
int athena_collision_set_grid(AthenaCollisionWorld *world,
	const AthenaCollisionGridDesc *desc);
void athena_collision_clear_grid(AthenaCollisionWorld *world);
bool athena_collision_has_grid(const AthenaCollisionWorld *world);
/* The grid's geometry (tiles and kinds point into the world) or false. */
bool athena_collision_get_grid(const AthenaCollisionWorld *world,
	AthenaCollisionGridDesc *desc);

/* Tile id at a cell, or -1 outside the grid. */
int32_t athena_collision_get_tile(const AthenaCollisionWorld *world,
	int32_t column, int32_t row);
/* Changes a cell; false outside the grid. */
bool athena_collision_set_tile(AthenaCollisionWorld *world, int32_t column,
	int32_t row, uint16_t tile);
/* Cell holding a point; false outside the grid. */
bool athena_collision_cell_at(const AthenaCollisionWorld *world, float x,
	float y, int32_t *column, int32_t *row);

/*
 * Whether a point is inside something solid: a solid tile, a slope below
 * its surface (when the grid's layer is in `mask`) or a body on a layer of
 * `mask` that is not a sensor. One-way platforms are not solid.
 */
bool athena_collision_point_solid(AthenaCollisionWorld *world, float x, float y,
	uint32_t mask);

/* ---- Movement ---------------------------------------------------------- */

typedef struct {
	/* Distance actually moved. */
	float dx, dy;
	bool on_ground;
	bool hit_ceiling;
	/* -1 blocked moving left, 1 moving right, 0 not blocked. */
	int8_t hit_wall;
	/* What stopped the x sweep and the y sweep, in that order. */
	uint32_t contact_count;
	AthenaCollisionContact contacts[2];
} AthenaCollisionMove;

/*
 * Moves a body by (dx, dy): along x, then along y, each time stopping at
 * the first solid thing in the way (other bodies that collide with it,
 * solid tiles, one-way platforms from above, slopes). Walking into a slope
 * follows its floor; a body that stood on the ground steps up ledges and
 * sticks to floors going down as far as the steepest slope of the grid
 * needs for the distance moved (at most half a tile per sweep: fast bodies
 * move in several sweeps). Sensors move freely, and kinematic bodies as
 * athena_collision_translate() does. Updates the body's contact state;
 * `result` may be NULL.
 */
void athena_collision_move(AthenaCollisionWorld *world, AthenaBodyId id,
	float dx, float dy, AthenaCollisionMove *result);

/*
 * Advances the world by `dt` seconds: kinematic bodies move by their
 * velocity, carrying the dynamic bodies that stand on them and pushing
 * those in their way (a pushed body that cannot get out is `crushed`);
 * dynamic bodies get gravity, damping and speed limits, then move with
 * athena_collision_move(). A blocked axis loses its velocity, or bounces
 * back with `bounce`. Then sensor overlaps are updated.
 */
void athena_collision_step(AthenaCollisionWorld *world, float dt);

/* ---- Queries ----------------------------------------------------------- */

/*
 * Queries write up to `max` ids to `out` and return how many bodies match,
 * which may be more than `max`. Bodies match when their layer has a bit of
 * `mask`; sensors are included. Touching is not overlapping.
 */
uint32_t athena_collision_query_rect(AthenaCollisionWorld *world, float x,
	float y, float w, float h, uint32_t mask, AthenaBodyId *out, uint32_t max);
uint32_t athena_collision_query_circle(AthenaCollisionWorld *world, float x,
	float y, float r, uint32_t mask, AthenaBodyId *out, uint32_t max);
uint32_t athena_collision_query_point(AthenaCollisionWorld *world, float x,
	float y, uint32_t mask, AthenaBodyId *out, uint32_t max);
/* Bodies overlapping `id` that it collides with (layer and mask), sensors too. */
uint32_t athena_collision_overlapping(AthenaCollisionWorld *world,
	AthenaBodyId id, AthenaBodyId *out, uint32_t max);

typedef struct {
	AthenaBodyId a, b;
} AthenaBodyPair;

/*
 * Overlapping pairs with `a` on a layer of `layer_a` and `b` on a layer of
 * `layer_b` (masks are not used; sensors are included). Each pair is
 * reported once, even when both bodies are on both layers.
 */
uint32_t athena_collision_pairs(AthenaCollisionWorld *world, uint32_t layer_a,
	uint32_t layer_b, AthenaBodyPair *out, uint32_t max);

typedef struct {
	/* Bodies on a layer of `mask`; the grid when its layer is in it. */
	uint32_t mask;
	/* Skipped body, or ATHENA_BODY_NONE. */
	AthenaBodyId ignore;
	/* Also hit sensors. */
	bool sensors;
} AthenaRaycastDesc;

typedef struct {
	float x, y;
	float normal_x, normal_y;
	/* Of the segment, 0..1. */
	float fraction;
	AthenaBodyId body;
	/* Tile id hit, or -1 for a body. */
	int32_t tile;
	int32_t column, row;
} AthenaRayHit;

/*
 * First thing the segment from (x1, y1) to (x2, y2) hits. Shapes and solid
 * tiles that contain the start are ignored; one-way tiles and bodies are
 * only hit from above. `desc` may be NULL (every layer, no sensors).
 */
bool athena_collision_raycast(AthenaCollisionWorld *world, float x1, float y1,
	float x2, float y2, const AthenaRaycastDesc *desc, AthenaRayHit *hit);

/* ---- Shapes outside a world -------------------------------------------- */

/* Whether two shapes overlap (touching is not overlapping). */
bool athena_collision_shape_overlap(const AthenaShape *a, const AthenaShape *b);

/*
 * The shortest translation that moves `a` out of `b`, or false when they do
 * not overlap.
 */
bool athena_collision_shape_resolve(const AthenaShape *a, const AthenaShape *b,
	float *dx, float *dy);

/*
 * Where the segment from (x1, y1) to (x2, y2) enters `shape`: fraction of
 * the segment and surface normal. false when it misses or starts inside.
 */
bool athena_collision_segment_shape(float x1, float y1, float x2, float y2,
	const AthenaShape *shape, float *fraction, float *normal_x, float *normal_y);

/* ---- Debug drawing (collision_gs.c) ------------------------------------ */

typedef struct {
	bool bodies;
	bool tiles;
} AthenaCollisionDebug;

/*
 * Outlines the bodies and tiles visible through the current 2D view (the
 * Camera2D): static bodies in white, kinematic in blue, dynamic in green,
 * sensors in yellow, tiles in red. Main thread only.
 */
void athena_collision_draw_debug(AthenaCollisionWorld *world,
	const AthenaCollisionDebug *debug);

#endif /* ATHENA_COLLISION_H */
