#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <athena/collision.h>

/*
 * Bodies live in slots of a growable array, indexed by the low bits of
 * their ids. The spatial hash maps each cell a body covers to a bucket of
 * slot indices; bodies covering many cells go to a separate list that every
 * query scans instead. Buckets may mix cells: they only give candidates,
 * which are then tested against the actual shapes.
 */

#define EPS ATHENA_COLLISION_EPSILON
#define TOL ATHENA_COLLISION_TOLERANCE
/* Bodies over more cells than this go to the large list. */
#define LARGE_CELLS 16
/* Queries over more cells than this scan every body instead. */
#define BRUTE_CELLS 1024
#define MIN_BUCKETS 64u
/* Keeps cell coordinates far from the int32 limits. */
#define MAX_CELL 1.0e9f
#define NO_SLOT 0xFFFFFFFFu
#define NO_GAP 1.0e30f
#define GENERATION_MASK 0xFFFu
/* Most horizontal sweeps of one move over slopes. */
#define MAX_PARTS 64

typedef struct {
	float x0, y0, x1, y1;
} Box;

typedef struct {
	AthenaBody body;
	uint32_t generation;
	bool alive;
	bool large;
	/* Cells the body is in, when not large. */
	int32_t cx0, cy0, cx1, cy1;
	/* Last query that visited the body, to report it once. */
	uint32_t stamp;
	uint32_t next_free;
} Slot;

typedef struct {
	uint32_t *items;
	uint32_t count, capacity;
} Bucket;

struct AthenaCollisionWorld {
	float cell_size, inv_cell;
	float gravity_x, gravity_y;
	AthenaCollisionContactFunc contact_func;
	void *contact_opaque;
	AthenaCollisionSensorFunc sensor_func;
	void *sensor_opaque;
	/*
	 * Sensor overlaps of the last step, as sorted (sensor id << 32 | other
	 * id) keys, and the list being built for the current one.
	 */
	uint64_t *touching, *touching_next;
	uint32_t touching_count, touching_capacity, touching_next_capacity;

	Slot *slots;
	/* Slots used so far, free or not, and allocated. */
	uint32_t slot_count, slot_capacity;
	uint32_t free_head;
	uint32_t body_count;

	Bucket *buckets;
	uint32_t bucket_count;
	/*
	 * Large bodies. The capacity of this list and of the scratch always
	 * covers every body, so neither grows while bodies move.
	 */
	uint32_t *large;
	uint32_t large_count, large_capacity;
	uint32_t *scratch;
	uint32_t scratch_count, scratch_capacity;
	/* Riders and pushed bodies of a kinematic move; covers every body too. */
	uint32_t *aux;
	uint32_t aux_capacity;
	uint32_t stamp;

	bool has_grid;
	uint32_t columns, rows;
	float grid_x, grid_y, tile_w, tile_h;
	uint16_t *tiles;
	AthenaTileKind *kinds;
	uint32_t kind_count;
	uint32_t grid_layer;
	/* Steepest slope of the grid: height per unit of width. */
	float steepness;
};

typedef AthenaCollisionWorld World;

/* ---- Helpers ------------------------------------------------------------ */

static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static Box body_box(const AthenaBody *body)
{
	Box box;

	if (body->shape == ATHENA_SHAPE_CIRCLE) {
		box.x0 = body->x - body->r;
		box.y0 = body->y - body->r;
		box.x1 = body->x + body->r;
		box.y1 = body->y + body->r;
	} else {
		box.x0 = body->x;
		box.y0 = body->y;
		box.x1 = body->x + body->w;
		box.y1 = body->y + body->h;
	}
	return box;
}

void athena_collision_body_bounds(const AthenaBody *body, float *x0, float *y0,
	float *x1, float *y1)
{
	Box box = body_box(body);

	*x0 = box.x0;
	*y0 = box.y0;
	*x1 = box.x1;
	*y1 = box.y1;
}

static AthenaShape body_shape(const AthenaBody *body)
{
	AthenaShape shape = { body->shape, body->x, body->y, body->w, body->h, body->r };
	return shape;
}

static AthenaBodyId make_id(const World *world, uint32_t index)
{
	return (world->slots[index].generation << 20) | (index + 1u);
}

static Slot *slot_of(World *world, AthenaBodyId id)
{
	uint32_t index = ATHENA_COLLISION_ID_SLOT(id);

	if (id == ATHENA_BODY_NONE || index >= world->slot_count)
		return NULL;
	if (!world->slots[index].alive || make_id(world, index) != id)
		return NULL;
	return &world->slots[index];
}

/* Two bodies block each other: neither is a sensor and the filters agree. */
static bool blocks(const AthenaBody *mover, const AthenaBody *other)
{
	return !other->sensor && (mover->mask & other->layer) && (other->mask & mover->layer);
}

static int grow(void **items, uint32_t *capacity, uint32_t needed, size_t size)
{
	uint32_t next = *capacity ? *capacity : 8u;
	void *grown;

	if (needed <= *capacity)
		return 0;
	while (next < needed)
		next = next > 0x7FFFFFFFu / 2u ? needed : next * 2u;
	grown = realloc(*items, (size_t)next * size);
	if (!grown)
		return -1;
	*items = grown;
	*capacity = next;
	return 0;
}

/* ---- Settings ----------------------------------------------------------- */

void athena_collision_world_desc_init(AthenaCollisionWorldDesc *desc)
{
	desc->cell_size = 64.0f;
	desc->gravity_x = 0.0f;
	desc->gravity_y = 0.0f;
}

void athena_collision_body_desc_init(AthenaCollisionBodyDesc *desc)
{
	memset(desc, 0, sizeof(*desc));
	desc->shape = ATHENA_SHAPE_RECT;
	desc->type = ATHENA_BODY_STATIC;
	desc->layer = 1u;
	desc->mask = 0xFFFFFFFFu;
	desc->gravity_scale = 1.0f;
}

AthenaCollisionWorld *athena_collision_world_create(const AthenaCollisionWorldDesc *desc)
{
	World *world;

	if (!(desc->cell_size >= 1.0f))
		return NULL;
	world = calloc(1, sizeof(*world));
	if (!world)
		return NULL;
	world->buckets = calloc(MIN_BUCKETS, sizeof(Bucket));
	if (!world->buckets) {
		free(world);
		return NULL;
	}
	world->bucket_count = MIN_BUCKETS;
	world->cell_size = desc->cell_size;
	world->inv_cell = 1.0f / desc->cell_size;
	world->gravity_x = desc->gravity_x;
	world->gravity_y = desc->gravity_y;
	world->free_head = NO_SLOT;
	return world;
}

void athena_collision_world_destroy(AthenaCollisionWorld *world)
{
	uint32_t i;

	if (!world)
		return;
	for (i = 0; i < world->bucket_count; i++)
		free(world->buckets[i].items);
	free(world->buckets);
	free(world->slots);
	free(world->large);
	free(world->scratch);
	free(world->aux);
	free(world->touching);
	free(world->touching_next);
	free(world->tiles);
	free(world->kinds);
	free(world);
}

void athena_collision_set_gravity(AthenaCollisionWorld *world, float x, float y)
{
	world->gravity_x = x;
	world->gravity_y = y;
}

void athena_collision_get_gravity(const AthenaCollisionWorld *world, float *x, float *y)
{
	*x = world->gravity_x;
	*y = world->gravity_y;
}

float athena_collision_cell_size(const AthenaCollisionWorld *world)
{
	return world->cell_size;
}

void athena_collision_set_contact_func(AthenaCollisionWorld *world,
	AthenaCollisionContactFunc func, void *opaque)
{
	world->contact_func = func;
	world->contact_opaque = opaque;
}

void athena_collision_set_sensor_func(AthenaCollisionWorld *world,
	AthenaCollisionSensorFunc func, void *opaque)
{
	world->sensor_func = func;
	world->sensor_opaque = opaque;
	/* Without a function nothing is tracked: a new one starts from no overlap. */
	if (!func)
		world->touching_count = 0;
}

/* ---- Spatial hash -------------------------------------------------------- */

static int32_t cell_coord(const World *world, float v)
{
	float c = floorf(v * world->inv_cell);

	return (int32_t)clampf(c, -MAX_CELL, MAX_CELL);
}

static uint32_t cell_bucket(const World *world, int32_t cx, int32_t cy)
{
	uint32_t h = (uint32_t)cx * 73856093u ^ (uint32_t)cy * 19349663u;

	h ^= h >> 15;
	return h & (world->bucket_count - 1u);
}

static void bucket_remove(Bucket *bucket, uint32_t index)
{
	uint32_t i;

	for (i = 0; i < bucket->count; i++) {
		if (bucket->items[i] == index) {
			bucket->items[i] = bucket->items[--bucket->count];
			return;
		}
	}
}

static void hash_remove(World *world, uint32_t index)
{
	Slot *slot = &world->slots[index];
	int32_t cx, cy;
	uint32_t i;

	if (slot->large) {
		for (i = 0; i < world->large_count; i++) {
			if (world->large[i] == index) {
				world->large[i] = world->large[--world->large_count];
				break;
			}
		}
		slot->large = false;
		return;
	}
	for (cy = slot->cy0; cy <= slot->cy1; cy++)
		for (cx = slot->cx0; cx <= slot->cx1; cx++)
			bucket_remove(&world->buckets[cell_bucket(world, cx, cy)], index);
}

/*
 * Puts a body in the cells of its box. Never fails: a body whose buckets
 * cannot grow goes to the large list, which always has room.
 */
static void hash_insert(World *world, uint32_t index)
{
	Slot *slot = &world->slots[index];
	Box box = body_box(&slot->body);
	int32_t cx0 = cell_coord(world, box.x0), cy0 = cell_coord(world, box.y0);
	int32_t cx1 = cell_coord(world, box.x1), cy1 = cell_coord(world, box.y1);
	int64_t cells = ((int64_t)cx1 - cx0 + 1) * ((int64_t)cy1 - cy0 + 1);
	int32_t cx, cy;

	slot->cx0 = cx0;
	slot->cy0 = cy0;
	slot->cx1 = cx1;
	slot->cy1 = cy1;
	if (cells <= LARGE_CELLS) {
		for (cy = cy0; cy <= cy1; cy++) {
			for (cx = cx0; cx <= cx1; cx++) {
				Bucket *bucket = &world->buckets[cell_bucket(world, cx, cy)];

				if (grow((void **)&bucket->items, &bucket->capacity,
						bucket->count + 1u, sizeof(uint32_t)) < 0)
					goto fallback;
				bucket->items[bucket->count++] = index;
			}
		}
		slot->large = false;
		return;
fallback:
		/* Undo the cells already added: [cy0, cy) whole rows, then row cy up to cx. */
		{
			int32_t ux, uy;

			for (uy = cy0; uy <= cy; uy++)
				for (ux = cx0; ux <= (uy == cy ? cx - 1 : cx1); ux++)
					bucket_remove(&world->buckets[cell_bucket(world, ux, uy)], index);
		}
	}
	slot->large = true;
	world->large[world->large_count++] = index;
}

static void hash_refresh(World *world, uint32_t index)
{
	Slot *slot = &world->slots[index];
	Box box = body_box(&slot->body);

	if (!slot->large && cell_coord(world, box.x0) == slot->cx0 &&
		cell_coord(world, box.y0) == slot->cy0 &&
		cell_coord(world, box.x1) == slot->cx1 &&
		cell_coord(world, box.y1) == slot->cy1)
		return;
	hash_remove(world, index);
	hash_insert(world, index);
}

/* More buckets as bodies are added: two bodies per bucket at most. */
static void hash_resize(World *world)
{
	uint32_t count = world->bucket_count, i;
	Bucket *buckets, *old = world->buckets;
	uint32_t old_count = world->bucket_count;

	while (count < world->body_count / 2u && count < (1u << 20))
		count *= 2u;
	if (count == world->bucket_count)
		return;
	buckets = calloc(count, sizeof(Bucket));
	if (!buckets)
		return;
	world->buckets = buckets;
	world->bucket_count = count;
	world->large_count = 0;
	for (i = 0; i < world->slot_count; i++) {
		if (world->slots[i].alive) {
			world->slots[i].large = false;
			hash_insert(world, i);
		}
	}
	for (i = 0; i < old_count; i++)
		free(old[i].items);
	free(old);
}

static uint32_t next_stamp(World *world)
{
	uint32_t i;

	if (++world->stamp == 0) {
		for (i = 0; i < world->slot_count; i++)
			world->slots[i].stamp = 0;
		world->stamp = 1;
	}
	return world->stamp;
}

static bool boxes_touch(const Box *a, const Box *b)
{
	return a->x0 <= b->x1 && b->x0 <= a->x1 && a->y0 <= b->y1 && b->y0 <= a->y1;
}

/* Fills the scratch with the slots whose box touches `query`. */
static void gather(World *world, const Box *query)
{
	int32_t cx0 = cell_coord(world, query->x0), cy0 = cell_coord(world, query->y0);
	int32_t cx1 = cell_coord(world, query->x1), cy1 = cell_coord(world, query->y1);
	int64_t cells = ((int64_t)cx1 - cx0 + 1) * ((int64_t)cy1 - cy0 + 1);
	uint32_t stamp = next_stamp(world), i;
	int32_t cx, cy;

	world->scratch_count = 0;
	if (cells > BRUTE_CELLS || cells > (int64_t)world->body_count * 4) {
		for (i = 0; i < world->slot_count; i++) {
			Slot *slot = &world->slots[i];
			Box box;

			if (!slot->alive)
				continue;
			box = body_box(&slot->body);
			if (boxes_touch(&box, query))
				world->scratch[world->scratch_count++] = i;
		}
		return;
	}
	for (cy = cy0; cy <= cy1; cy++) {
		for (cx = cx0; cx <= cx1; cx++) {
			Bucket *bucket = &world->buckets[cell_bucket(world, cx, cy)];

			for (i = 0; i < bucket->count; i++) {
				Slot *slot = &world->slots[bucket->items[i]];
				Box box;

				if (slot->stamp == stamp)
					continue;
				slot->stamp = stamp;
				box = body_box(&slot->body);
				if (boxes_touch(&box, query))
					world->scratch[world->scratch_count++] = bucket->items[i];
			}
		}
	}
	for (i = 0; i < world->large_count; i++) {
		Slot *slot = &world->slots[world->large[i]];
		Box box = body_box(&slot->body);

		if (boxes_touch(&box, query))
			world->scratch[world->scratch_count++] = world->large[i];
	}
}

static void gather_cell(World *world, int32_t cx, int32_t cy, uint32_t stamp)
{
	Bucket *bucket = &world->buckets[cell_bucket(world, cx, cy)];
	uint32_t i;

	for (i = 0; i < bucket->count; i++) {
		Slot *slot = &world->slots[bucket->items[i]];

		if (slot->stamp == stamp)
			continue;
		slot->stamp = stamp;
		world->scratch[world->scratch_count++] = bucket->items[i];
	}
}

/*
 * Fills the scratch with the bodies of the cells a segment crosses (and the
 * large ones): a long diagonal ray visits a line of cells, not every cell of
 * its bounding box. Candidates only: the caller tests the segment.
 */
static void gather_segment(World *world, float x1, float y1, float x2, float y2)
{
	Box box = { minf(x1, x2), minf(y1, y2), maxf(x1, x2), maxf(y1, y2) };
	int32_t cx = cell_coord(world, x1), cy = cell_coord(world, y1);
	int32_t ex = cell_coord(world, x2), ey = cell_coord(world, y2);
	int64_t span_x = ex > cx ? (int64_t)ex - cx : (int64_t)cx - ex;
	int64_t span_y = ey > cy ? (int64_t)ey - cy : (int64_t)cy - ey;
	int64_t steps = span_x + span_y + 1, n;
	float dx = x2 - x1, dy = y2 - y1, size = world->cell_size;
	float next_x, next_y, delta_x, delta_y;
	int32_t step_x, step_y;
	uint32_t stamp, i;

	/* Along a row or a column, over a small box, or past what the hash is worth: the box. */
	if ((span_x + 1) * (span_y + 1) <= steps * 2 || steps > BRUTE_CELLS ||
		steps > (int64_t)world->body_count * 4) {
		gather(world, &box);
		return;
	}
	/* Amanatides and Woo over the cells; dx and dy are not 0 here. */
	step_x = dx > 0.0f ? 1 : -1;
	step_y = dy > 0.0f ? 1 : -1;
	next_x = ((float)(cx + (step_x > 0)) * size - x1) / dx;
	next_y = ((float)(cy + (step_y > 0)) * size - y1) / dy;
	delta_x = size / fabsf(dx);
	delta_y = size / fabsf(dy);
	stamp = next_stamp(world);
	world->scratch_count = 0;
	for (n = 0; n < steps; n++) {
		gather_cell(world, cx, cy, stamp);
		if (cx == ex && cy == ey)
			break;
		/* Through a corner, or close to one: both neighbours. */
		if (fabsf(next_x - next_y) <= 1e-5f) {
			gather_cell(world, cx + step_x, cy, stamp);
			gather_cell(world, cx, cy + step_y, stamp);
		}
		/* An axis at its last cell stays there: rounding never walks past the end. */
		if (cy == ey || (cx != ex && next_x < next_y)) {
			cx += step_x;
			next_x += delta_x;
		} else {
			cy += step_y;
			next_y += delta_y;
		}
	}
	for (i = 0; i < world->large_count; i++) {
		Box large = body_box(&world->slots[world->large[i]].body);

		if (boxes_touch(&large, &box))
			world->scratch[world->scratch_count++] = world->large[i];
	}
}

/* ---- Bodies -------------------------------------------------------------- */

static bool size_valid(const AthenaCollisionBodyDesc *desc)
{
	if (desc->shape == ATHENA_SHAPE_CIRCLE)
		return desc->r > 0.0f;
	return desc->shape == ATHENA_SHAPE_RECT && desc->w > 0.0f && desc->h > 0.0f;
}

int athena_collision_add(AthenaCollisionWorld *world,
	const AthenaCollisionBodyDesc *desc, AthenaBodyId *id)
{
	uint32_t index, needed = world->body_count + 1u;
	Slot *slot;
	AthenaBody *body;

	if (!size_valid(desc) || desc->type > ATHENA_BODY_DYNAMIC)
		return ATHENA_COLLISION_EINVAL;
	if (world->body_count >= ATHENA_COLLISION_MAX_BODIES)
		return ATHENA_COLLISION_ENOMEM;
	if (grow((void **)&world->large, &world->large_capacity, needed, sizeof(uint32_t)) < 0 ||
		grow((void **)&world->scratch, &world->scratch_capacity, needed, sizeof(uint32_t)) < 0 ||
		grow((void **)&world->aux, &world->aux_capacity, needed, sizeof(uint32_t)) < 0)
		return ATHENA_COLLISION_ENOMEM;
	if (world->free_head != NO_SLOT) {
		index = world->free_head;
		world->free_head = world->slots[index].next_free;
	} else {
		if (grow((void **)&world->slots, &world->slot_capacity, world->slot_count + 1u,
				sizeof(Slot)) < 0)
			return ATHENA_COLLISION_ENOMEM;
		index = world->slot_count++;
		world->slots[index].generation = 0;
	}
	slot = &world->slots[index];
	slot->generation = (slot->generation + 1u) & GENERATION_MASK;
	slot->alive = true;
	slot->large = false;
	slot->stamp = 0;
	body = &slot->body;
	memset(body, 0, sizeof(*body));
	body->shape = desc->shape;
	body->type = desc->type;
	body->x = desc->x;
	body->y = desc->y;
	if (desc->shape == ATHENA_SHAPE_CIRCLE) {
		body->r = desc->r;
		body->w = body->h = desc->r * 2.0f;
	} else {
		body->w = desc->w;
		body->h = desc->h;
	}
	body->vx = desc->vx;
	body->vy = desc->vy;
	body->layer = desc->layer;
	body->mask = desc->mask;
	body->sensor = desc->sensor;
	body->one_way = desc->one_way;
	body->gravity_scale = desc->gravity_scale;
	body->damping = desc->damping;
	body->bounce = desc->bounce;
	body->max_speed_x = desc->max_speed_x;
	body->max_speed_y = desc->max_speed_y;
	body->user = desc->user;
	world->body_count++;
	hash_insert(world, index);
	hash_resize(world);
	*id = make_id(world, index);
	return 0;
}

bool athena_collision_remove(AthenaCollisionWorld *world, AthenaBodyId id)
{
	Slot *slot = slot_of(world, id);
	uint32_t index;

	if (!slot)
		return false;
	index = (uint32_t)(slot - world->slots);
	hash_remove(world, index);
	slot->alive = false;
	slot->body.user = NULL;
	slot->next_free = world->free_head;
	world->free_head = index;
	world->body_count--;
	return true;
}

AthenaBody *athena_collision_get(AthenaCollisionWorld *world, AthenaBodyId id)
{
	Slot *slot = slot_of(world, id);

	return slot ? &slot->body : NULL;
}

uint32_t athena_collision_body_count(const AthenaCollisionWorld *world)
{
	return world->body_count;
}

uint32_t athena_collision_body_capacity(const AthenaCollisionWorld *world)
{
	return world->slot_count;
}

AthenaBody *athena_collision_slot(AthenaCollisionWorld *world, uint32_t slot)
{
	if (slot >= world->slot_count || !world->slots[slot].alive)
		return NULL;
	return &world->slots[slot].body;
}

AthenaBodyId athena_collision_slot_id(const AthenaCollisionWorld *world, uint32_t slot)
{
	if (slot >= world->slot_count || !world->slots[slot].alive)
		return ATHENA_BODY_NONE;
	return make_id(world, slot);
}

void athena_collision_refresh(AthenaCollisionWorld *world, AthenaBodyId id)
{
	Slot *slot = slot_of(world, id);

	if (!slot)
		return;
	if (slot->body.shape == ATHENA_SHAPE_CIRCLE)
		slot->body.w = slot->body.h = slot->body.r * 2.0f;
	hash_refresh(world, (uint32_t)(slot - world->slots));
}

void athena_collision_set_position(AthenaCollisionWorld *world, AthenaBodyId id,
	float x, float y)
{
	Slot *slot = slot_of(world, id);

	if (!slot)
		return;
	slot->body.x = x;
	slot->body.y = y;
	/* A teleport breaks every contact: the next move or step finds them again. */
	slot->body.on_ground = slot->body.on_ceiling = false;
	slot->body.on_wall = 0;
	slot->body.ground = ATHENA_BODY_NONE;
	hash_refresh(world, (uint32_t)(slot - world->slots));
}

/* ---- Tiles --------------------------------------------------------------- */

int athena_collision_set_grid(AthenaCollisionWorld *world,
	const AthenaCollisionGridDesc *desc)
{
	uint64_t count = (uint64_t)desc->columns * desc->rows;
	uint16_t *tiles;
	AthenaTileKind *kinds = NULL;
	float steepness = 0.0f;
	uint32_t i;

	if (desc->columns == 0 || desc->rows == 0 || count > ATHENA_COLLISION_MAX_TILES ||
		desc->columns > ATHENA_COLLISION_MAX_GRID_SIDE || desc->rows > ATHENA_COLLISION_MAX_GRID_SIDE ||
		!(desc->tile_width > 0.0f) || !(desc->tile_height > 0.0f) ||
		desc->kind_count > 65536u || (desc->kind_count && !desc->kinds))
		return ATHENA_COLLISION_EINVAL;
	for (i = 0; i < desc->kind_count; i++) {
		const AthenaTileKind *kind = &desc->kinds[i];

		if (kind->type > ATHENA_TILE_SLOPE)
			return ATHENA_COLLISION_EINVAL;
		if (kind->type == ATHENA_TILE_SLOPE) {
			if (!(kind->left >= 0.0f && kind->left <= 1.0f &&
					kind->right >= 0.0f && kind->right <= 1.0f))
				return ATHENA_COLLISION_EINVAL;
			steepness = maxf(steepness, fabsf(kind->right - kind->left) *
				desc->tile_height / desc->tile_width);
		}
	}
	tiles = malloc((size_t)count * sizeof(uint16_t));
	if (!tiles)
		return ATHENA_COLLISION_ENOMEM;
	if (desc->kind_count) {
		kinds = malloc((size_t)desc->kind_count * sizeof(AthenaTileKind));
		if (!kinds) {
			free(tiles);
			return ATHENA_COLLISION_ENOMEM;
		}
		memcpy(kinds, desc->kinds, (size_t)desc->kind_count * sizeof(AthenaTileKind));
	}
	if (desc->tiles)
		memcpy(tiles, desc->tiles, (size_t)count * sizeof(uint16_t));
	else
		memset(tiles, 0, (size_t)count * sizeof(uint16_t));
	free(world->tiles);
	free(world->kinds);
	world->tiles = tiles;
	world->kinds = kinds;
	world->kind_count = desc->kind_count;
	world->columns = desc->columns;
	world->rows = desc->rows;
	world->tile_w = desc->tile_width;
	world->tile_h = desc->tile_height;
	world->grid_x = desc->x;
	world->grid_y = desc->y;
	world->grid_layer = desc->layer;
	world->steepness = steepness;
	world->has_grid = true;
	return 0;
}

void athena_collision_clear_grid(AthenaCollisionWorld *world)
{
	free(world->tiles);
	free(world->kinds);
	world->tiles = NULL;
	world->kinds = NULL;
	world->kind_count = 0;
	world->has_grid = false;
	world->steepness = 0.0f;
}

bool athena_collision_has_grid(const AthenaCollisionWorld *world)
{
	return world->has_grid;
}

bool athena_collision_get_grid(const AthenaCollisionWorld *world,
	AthenaCollisionGridDesc *desc)
{
	if (!world->has_grid)
		return false;
	desc->columns = world->columns;
	desc->rows = world->rows;
	desc->tile_width = world->tile_w;
	desc->tile_height = world->tile_h;
	desc->x = world->grid_x;
	desc->y = world->grid_y;
	desc->tiles = world->tiles;
	desc->kinds = world->kinds;
	desc->kind_count = world->kind_count;
	desc->layer = world->grid_layer;
	return true;
}

int32_t athena_collision_get_tile(const AthenaCollisionWorld *world,
	int32_t column, int32_t row)
{
	if (!world->has_grid || column < 0 || row < 0 ||
		(uint32_t)column >= world->columns || (uint32_t)row >= world->rows)
		return -1;
	return world->tiles[(size_t)row * world->columns + (uint32_t)column];
}

bool athena_collision_set_tile(AthenaCollisionWorld *world, int32_t column,
	int32_t row, uint16_t tile)
{
	if (athena_collision_get_tile(world, column, row) < 0)
		return false;
	world->tiles[(size_t)row * world->columns + (uint32_t)column] = tile;
	return true;
}

/* Column or row of a coordinate, clamped to [-1, count]. */
static int32_t tile_coord(float v, float origin, float size, uint32_t count)
{
	float c = floorf((v - origin) / size);

	return (int32_t)clampf(c, -1.0f, (float)count);
}

bool athena_collision_cell_at(const AthenaCollisionWorld *world, float x,
	float y, int32_t *column, int32_t *row)
{
	int32_t c, r;

	if (!world->has_grid)
		return false;
	c = tile_coord(x, world->grid_x, world->tile_w, world->columns);
	r = tile_coord(y, world->grid_y, world->tile_h, world->rows);
	if (c < 0 || r < 0 || (uint32_t)c >= world->columns || (uint32_t)r >= world->rows)
		return false;
	*column = c;
	*row = r;
	return true;
}

/* Tile cells touching a box, clamped to the grid; false when none. */
typedef struct {
	int32_t c0, r0, c1, r1;
} CellRange;

static bool tile_range(const World *world, float x0, float y0, float x1, float y1,
	CellRange *range)
{
	range->c0 = tile_coord(x0, world->grid_x, world->tile_w, world->columns);
	range->c1 = tile_coord(x1, world->grid_x, world->tile_w, world->columns);
	range->r0 = tile_coord(y0, world->grid_y, world->tile_h, world->rows);
	range->r1 = tile_coord(y1, world->grid_y, world->tile_h, world->rows);
	if (range->c0 < 0)
		range->c0 = 0;
	if (range->r0 < 0)
		range->r0 = 0;
	if (range->c1 >= (int32_t)world->columns)
		range->c1 = (int32_t)world->columns - 1;
	if (range->r1 >= (int32_t)world->rows)
		range->r1 = (int32_t)world->rows - 1;
	return range->c0 <= range->c1 && range->r0 <= range->r1;
}

static const AthenaTileKind *tile_kind(const World *world, int32_t column, int32_t row,
	uint16_t *id)
{
	*id = world->tiles[(size_t)row * world->columns + (uint32_t)column];
	if (*id >= world->kind_count || world->kinds[*id].type == ATHENA_TILE_EMPTY)
		return NULL;
	return &world->kinds[*id];
}

static Box tile_box(const World *world, int32_t column, int32_t row)
{
	Box box;

	box.x0 = world->grid_x + (float)column * world->tile_w;
	box.y0 = world->grid_y + (float)row * world->tile_h;
	box.x1 = box.x0 + world->tile_w;
	box.y1 = box.y0 + world->tile_h;
	return box;
}

/* Floor y of a slope over [x0, x1] (inside the tile): its highest point. */
static float slope_floor(const World *world, const AthenaTileKind *kind,
	const Box *tile, float x0, float x1)
{
	float span = kind->right - kind->left;
	float h0 = kind->left + span * (clampf(x0, tile->x0, tile->x1) - tile->x0) / world->tile_w;
	float h1 = kind->left + span * (clampf(x1, tile->x0, tile->x1) - tile->x0) / world->tile_w;

	return tile->y1 - maxf(h0, h1) * world->tile_h;
}

/* Upward normal of a slope's surface. */
static void slope_normal(const World *world, const AthenaTileKind *kind, float *nx,
	float *ny)
{
	float k = -(kind->right - kind->left) * world->tile_h / world->tile_w;
	float length = sqrtf(k * k + 1.0f);

	*nx = k / length;
	*ny = -1.0f / length;
}

static bool grid_blocks(const World *world, const AthenaBody *body)
{
	return world->has_grid && (body->mask & world->grid_layer);
}

/* ---- Sweeps -------------------------------------------------------------- */

/*
 * The moving body along axis `a` (0 = x, 1 = y) in direction `dir` (+1 or
 * -1). `o` is the other axis. Gaps are the distance the mover can travel
 * before touching an obstacle; NO_GAP when it passes by.
 */
typedef struct {
	int a, o, dir;
	bool circle;
	float lo[2], hi[2];
	float c[2], r;
	const AthenaBody *body;
} Mover;

typedef struct {
	float dist;
	bool blocked;
	AthenaCollisionContact contact;
} Sweep;

static float gap_box(const Mover *m, const float lo[2], const float hi[2])
{
	int a = m->a, o = m->o;

	if (!(m->lo[o] < hi[o] - EPS && m->hi[o] > lo[o] + EPS))
		return NO_GAP;
	return m->dir > 0 ? lo[a] - m->hi[a] : m->lo[a] - hi[a];
}

/* Distance along the axis between an interval's edge and a circle, at the closest line. */
static float gap_circle_vs(const Mover *m, const float lo[2], const float hi[2])
{
	int a = m->a, o = m->o;
	float p = clampf(m->c[o], lo[o], hi[o]), e = m->c[o] - p, hc;

	if (fabsf(e) >= m->r - EPS)
		return NO_GAP;
	hc = sqrtf(m->r * m->r - e * e);
	return m->dir > 0 ? lo[a] - (m->c[a] + hc) : (m->c[a] - hc) - hi[a];
}

static float gap_vs_circle(const Mover *m, const float c[2], float r)
{
	int a = m->a, o = m->o;
	float hc, e;

	if (m->circle) {
		float radius = m->r + r;

		e = c[o] - m->c[o];
		if (fabsf(e) >= radius - EPS)
			return NO_GAP;
		hc = sqrtf(radius * radius - e * e);
		return m->dir > 0 ? (c[a] - hc) - m->c[a] : m->c[a] - (c[a] + hc);
	}
	e = c[o] - clampf(c[o], m->lo[o], m->hi[o]);
	if (fabsf(e) >= r - EPS)
		return NO_GAP;
	hc = sqrtf(r * r - e * e);
	return m->dir > 0 ? (c[a] - hc) - m->hi[a] : m->lo[a] - (c[a] + hc);
}

/* Gap to a solid box, for a box or a circle mover. */
static float gap_solid(const Mover *m, const float lo[2], const float hi[2])
{
	return m->circle ? gap_circle_vs(m, lo, hi) : gap_box(m, lo, hi);
}

static void consider(Sweep *sweep, float gap, AthenaBodyId other, int32_t tile,
	int32_t column, int32_t row, float nx, float ny)
{
	if (gap >= NO_GAP || gap < -EPS)
		return;
	if (gap < 0.0f)
		gap = 0.0f;
	if (gap >= sweep->dist)
		return;
	sweep->dist = gap;
	sweep->blocked = true;
	sweep->contact.other = other;
	sweep->contact.tile = tile;
	sweep->contact.column = column;
	sweep->contact.row = row;
	sweep->contact.normal_x = nx;
	sweep->contact.normal_y = ny;
}

static void mover_init(Mover *m, const AthenaBody *body, int axis, float d)
{
	Box box = body_box(body);

	m->a = axis;
	m->o = 1 - axis;
	m->dir = d > 0.0f ? 1 : -1;
	m->circle = body->shape == ATHENA_SHAPE_CIRCLE;
	m->lo[0] = box.x0;
	m->lo[1] = box.y0;
	m->hi[0] = box.x1;
	m->hi[1] = box.y1;
	m->c[0] = body->x;
	m->c[1] = body->y;
	m->r = body->r;
	m->body = body;
}

/*
 * Sweeps against the tiles. `step_up` > 0 lets a grounded mover walk into
 * tiles whose top is at most that far above its bottom (the lift that
 * follows puts it on top).
 */
static void sweep_tiles(const World *world, const Mover *m, float d, float step_up,
	Sweep *sweep)
{
	float lo[2] = { m->lo[0], m->lo[1] }, hi[2] = { m->hi[0], m->hi[1] };
	float nx = m->a == 0 ? (float)-m->dir : 0.0f, ny = m->a == 1 ? (float)-m->dir : 0.0f;
	CellRange range;
	int32_t c, r;

	if (m->dir > 0)
		hi[m->a] += d;
	else
		lo[m->a] -= d;
	if (!tile_range(world, lo[0], lo[1], hi[0], hi[1], &range))
		return;
	for (r = range.r0; r <= range.r1; r++) {
		for (c = range.c0; c <= range.c1; c++) {
			uint16_t id;
			const AthenaTileKind *kind = tile_kind(world, c, r, &id);
			Box t;
			float tlo[2], thi[2], gap;

			if (!kind)
				continue;
			t = tile_box(world, c, r);
			tlo[0] = t.x0;
			tlo[1] = t.y0;
			thi[0] = t.x1;
			thi[1] = t.y1;
			switch (kind->type) {
			case ATHENA_TILE_SOLID:
				gap = gap_solid(m, tlo, thi);
				if (step_up > 0.0f && gap < NO_GAP && t.y0 >= m->hi[1] - step_up)
					continue;
				consider(sweep, gap, ATHENA_BODY_NONE, id, c, r, nx, ny);
				break;
			case ATHENA_TILE_ONE_WAY:
				if (m->a != 1 || m->dir < 0 || m->body->drop_through ||
					m->hi[1] > t.y0 + TOL)
					continue;
				consider(sweep, gap_box(m, tlo, thi), ATHENA_BODY_NONE, id, c, r, nx, ny);
				break;
			case ATHENA_TILE_SLOPE:
				if (m->a == 0) {
					/* The high side is a wall up to the floor at that edge. */
					float h = m->dir > 0 ? kind->left : kind->right;
					float edge = t.y1 - h * world->tile_h;

					if (!(m->lo[1] < t.y1 - EPS && m->hi[1] > edge + TOL))
						continue;
					if (step_up > 0.0f && edge >= m->hi[1] - step_up)
						continue;
					gap = m->dir > 0 ? t.x0 - m->hi[0] : m->lo[0] - t.x1;
					consider(sweep, gap, ATHENA_BODY_NONE, id, c, r, nx, ny);
				} else if (m->dir > 0) {
					float floor, sx, sy;

					if (!(m->lo[0] < t.x1 - EPS && m->hi[0] > t.x0 + EPS))
						continue;
					floor = slope_floor(world, kind, &t, m->lo[0], m->hi[0]);
					if (m->hi[1] > floor + TOL)
						continue;
					slope_normal(world, kind, &sx, &sy);
					consider(sweep, floor - m->hi[1], ATHENA_BODY_NONE, id, c, r, sx, sy);
				} else {
					/* The underside is flat. */
					if (m->lo[1] < t.y1 - TOL)
						continue;
					consider(sweep, gap_solid(m, tlo, thi), ATHENA_BODY_NONE, id, c, r, nx, ny);
				}
				break;
			default:
				break;
			}
		}
	}
}

static void sweep_bodies(World *world, uint32_t self, const Mover *m, float d,
	Sweep *sweep)
{
	float nx = m->a == 0 ? (float)-m->dir : 0.0f, ny = m->a == 1 ? (float)-m->dir : 0.0f;
	Box query = { m->lo[0], m->lo[1], m->hi[0], m->hi[1] };
	uint32_t i;

	if (m->a == 0) {
		if (m->dir > 0)
			query.x1 += d;
		else
			query.x0 -= d;
	} else {
		if (m->dir > 0)
			query.y1 += d;
		else
			query.y0 -= d;
	}
	gather(world, &query);
	for (i = 0; i < world->scratch_count; i++) {
		uint32_t index = world->scratch[i];
		const AthenaBody *other = &world->slots[index].body;
		float gap;

		if (index == self || !blocks(m->body, other))
			continue;
		if (other->one_way) {
			Box ob = body_box(other);

			if (m->a != 1 || m->dir < 0 || m->body->drop_through || m->hi[1] > ob.y0 + TOL)
				continue;
		}
		if (other->shape == ATHENA_SHAPE_CIRCLE) {
			float c[2] = { other->x, other->y };

			gap = gap_vs_circle(m, c, other->r);
		} else {
			float lo[2] = { other->x, other->y };
			float hi[2] = { other->x + other->w, other->y + other->h };

			gap = gap_solid(m, lo, hi);
		}
		consider(sweep, gap, make_id(world, index), -1, -1, -1, nx, ny);
	}
}

/* How far a body can move by `d` along `axis` (always >= 0) and what stops it. */
static Sweep sweep_axis(World *world, uint32_t index, int axis, float d, float step_up)
{
	const AthenaBody *body = &world->slots[index].body;
	Sweep sweep;
	Mover m;

	memset(&sweep, 0, sizeof(sweep));
	sweep.dist = fabsf(d);
	if (d == 0.0f || !body->mask)
		return sweep;
	mover_init(&m, body, axis, d);
	if (grid_blocks(world, body))
		sweep_tiles(world, &m, fabsf(d), step_up, &sweep);
	sweep_bodies(world, index, &m, fabsf(d), &sweep);
	return sweep;
}

/* Whether a box overlaps a solid part of the grid (the ceiling test of lifts). */
static bool box_in_tiles(const World *world, const Box *box)
{
	CellRange range;
	int32_t c, r;

	if (!tile_range(world, box->x0 + EPS, box->y0 + EPS, box->x1 - EPS, box->y1 - EPS,
			&range))
		return false;
	for (r = range.r0; r <= range.r1; r++) {
		for (c = range.c0; c <= range.c1; c++) {
			uint16_t id;
			const AthenaTileKind *kind = tile_kind(world, c, r, &id);
			Box t;

			if (!kind || kind->type == ATHENA_TILE_ONE_WAY)
				continue;
			t = tile_box(world, c, r);
			if (!(box->x0 < t.x1 - EPS && box->x1 > t.x0 + EPS &&
					box->y0 < t.y1 - EPS && box->y1 > t.y0 + EPS))
				continue;
			if (kind->type == ATHENA_TILE_SOLID)
				return true;
			if (box->y1 > slope_floor(world, kind, &t, box->x0, box->x1) + TOL)
				return true;
		}
	}
	return false;
}

/*
 * After a horizontal move: the highest floor (solid top or slope) at most
 * `budget` above the body's bottom that the body sinks into. Returns the
 * lift (<= 0), or 0 when there is none.
 */
static float floor_lift(const World *world, const AthenaBody *body, float budget)
{
	Box box = body_box(body);
	float best = box.y1;
	CellRange range;
	int32_t c, r;

	if (!tile_range(world, box.x0 + EPS, box.y1 - budget, box.x1 - EPS, box.y1 - EPS, &range))
		return 0.0f;
	for (r = range.r0; r <= range.r1; r++) {
		for (c = range.c0; c <= range.c1; c++) {
			uint16_t id;
			const AthenaTileKind *kind = tile_kind(world, c, r, &id);
			Box t;
			float top;

			if (!kind || kind->type == ATHENA_TILE_ONE_WAY)
				continue;
			t = tile_box(world, c, r);
			if (!(box.x0 < t.x1 - EPS && box.x1 > t.x0 + EPS))
				continue;
			top = kind->type == ATHENA_TILE_SOLID ? t.y0 :
				slope_floor(world, kind, &t, box.x0, box.x1);
			if (top < box.y1 - EPS && top >= box.y1 - budget && top < best)
				best = top;
		}
	}
	return best - box.y1;
}

/* ---- Movement ------------------------------------------------------------ */

static void add_contact(AthenaCollisionMove *move, const AthenaCollisionContact *contact)
{
	if (move->contact_count < 2)
		move->contacts[move->contact_count++] = *contact;
}

/* athena_collision_move(); `blocked_y` tells whether the y sweep stopped. */
static void move_body(World *world, uint32_t index, float dx, float dy,
	AthenaCollisionMove *move, bool *blocked_y)
{
	AthenaBody *body = &world->slots[index].body;
	bool was_ground = body->on_ground, lifted = false, tiles = grid_blocks(world, body);
	/* Still on a floor during the horizontal move (it can walk off a ledge). */
	bool grounded = was_ground, stuck = false;
	AthenaBodyId ground = ATHENA_BODY_NONE;
	Sweep sweep;

	memset(move, 0, sizeof(*move));
	*blocked_y = false;
	if (body->sensor) {
		body->x += dx;
		body->y += dy;
		move->dx = dx;
		move->dy = dy;
		body->on_ground = body->on_ceiling = false;
		body->on_wall = 0;
		body->ground = ATHENA_BODY_NONE;
		hash_refresh(world, index);
		return;
	}

	if (dx != 0.0f) {
		/*
		 * With slopes, a sweep climbs at most |part| * steepness: parts of
		 * half a tile keep that below a tile, so a fast body never takes a
		 * wall for a step.
		 */
		float max_part = tiles && world->steepness > 0.0f ?
			0.5f * minf(world->tile_w, world->tile_h / world->steepness) : fabsf(dx);
		/* Absurd distances (a teleport by move()) keep at most MAX_PARTS sweeps. */
		float parts = clampf(ceilf(fabsf(dx) / max_part), 1.0f, (float)MAX_PARTS);
		float part = dx / parts;
		/*
		 * Enough to climb the steepest slope of the grid over one part; never
		 * more than half a tile, even for the long parts of a huge move.
		 */
		float budget = minf(fabsf(part), max_part) * world->steepness + TOL;
		int remaining = (int)parts;

		while (remaining-- > 0) {
			float moved;

			sweep = sweep_axis(world, index, 0, part, grounded && tiles ? budget : 0.0f);
			moved = part > 0.0f ? sweep.dist : -sweep.dist;
			body->x += moved;
			move->dx += moved;
			if (sweep.blocked) {
				move->hit_wall = dx > 0.0f ? 1 : -1;
				add_contact(move, &sweep.contact);
				remaining = 0;
			}
			if (moved != 0.0f && tiles) {
				float lift = floor_lift(world, body, budget);

				if (lift < 0.0f) {
					Box box;

					body->y += lift;
					box = body_box(body);
					if (box_in_tiles(world, &box)) {
						/* No headroom: the step or slope acts as a wall. */
						body->y -= lift;
						body->x -= moved;
						move->dx -= moved;
						move->hit_wall = dx > 0.0f ? 1 : -1;
						remaining = 0;
					} else {
						move->dy += lift;
						lifted = grounded = true;
					}
				}
			}
			/*
			 * Going down a slope, the floor drops at most `budget` over one
			 * part: stick to it after each part. Nothing that close below
			 * means the body walked off a ledge, and it falls.
			 */
			if (grounded && dy >= 0.0f && moved != 0.0f) {
				Sweep down = sweep_axis(world, index, 1, budget + TOL, 0.0f);

				if (down.blocked) {
					body->y += down.dist;
					move->dy += down.dist;
					ground = down.contact.other;
					stuck = true;
				} else {
					grounded = false;
				}
			}
		}
	}

	if (dy != 0.0f) {
		float moved;

		sweep = sweep_axis(world, index, 1, dy, 0.0f);
		moved = dy > 0.0f ? sweep.dist : -sweep.dist;
		body->y += moved;
		move->dy += moved;
		if (sweep.blocked) {
			*blocked_y = true;
			if (dy > 0.0f) {
				move->on_ground = true;
				ground = sweep.contact.other;
			} else {
				move->hit_ceiling = true;
			}
			add_contact(move, &sweep.contact);
		}
	}
	if (grounded && (lifted || stuck) && dy >= 0.0f)
		move->on_ground = true;
	/* Keep standing when the body did not move along x (or was stopped at once). */
	if (!move->on_ground && grounded && dy >= 0.0f) {
		sweep = sweep_axis(world, index, 1, 2.0f * TOL, 0.0f);
		if (sweep.blocked) {
			body->y += sweep.dist;
			move->dy += sweep.dist;
			move->on_ground = true;
			ground = sweep.contact.other;
		}
	}
	body->on_ground = move->on_ground;
	body->on_ceiling = move->hit_ceiling;
	body->on_wall = move->hit_wall;
	body->ground = move->on_ground ? ground : ATHENA_BODY_NONE;
	hash_refresh(world, index);
}

static bool boxes_overlap(const Box *a, const Box *b)
{
	return a->x0 < b->x1 - EPS && b->x0 < a->x1 - EPS &&
		a->y0 < b->y1 - EPS && b->y0 < a->y1 - EPS;
}

/*
 * How far a kinematic body moving from `from` to `to` must push `box` along
 * one axis so it stays ahead: 0 when the box was not in its way.
 */
static float push_distance(const Box *from, const Box *to, const Box *box, int axis,
	float d)
{
	const float flo[2] = { from->x0, from->y0 }, fhi[2] = { from->x1, from->y1 };
	const float tlo[2] = { to->x0, to->y0 }, thi[2] = { to->x1, to->y1 };
	const float blo[2] = { box->x0, box->y0 }, bhi[2] = { box->x1, box->y1 };
	int o = 1 - axis;

	/* In the way: across the other axis of the moved body, and ahead of where it was. */
	if (d == 0.0f || !(blo[o] < thi[o] - EPS && bhi[o] > tlo[o] + EPS))
		return 0.0f;
	if (d > 0.0f)
		return blo[axis] >= fhi[axis] - EPS && thi[axis] > blo[axis] ? thi[axis] - blo[axis] : 0.0f;
	return bhi[axis] <= flo[axis] + EPS && tlo[axis] < bhi[axis] ? tlo[axis] - bhi[axis] : 0.0f;
}

/*
 * Moves a kinematic body through everything by (dx, dy): the dynamic bodies
 * standing on it move along (with collisions, still standing on it), and
 * the dynamic bodies in its way are pushed ahead of it. A pushed body that
 * something solid keeps overlapping it is crushed.
 */
static void kinematic_move(World *world, uint32_t index, float dx, float dy)
{
	AthenaBodyId id = make_id(world, index);
	AthenaBody *platform = &world->slots[index].body;
	Box from = body_box(platform), to = from, query;
	uint32_t riders = 0, count = 0, i;

	if (dx == 0.0f && dy == 0.0f)
		return;
	to.x0 += dx;
	to.x1 += dx;
	to.y0 += dy;
	to.y1 += dy;
	/* Riders touch its top; pushed bodies are in the area it sweeps. */
	query.x0 = minf(from.x0, to.x0);
	query.y0 = minf(from.y0, to.y0) - TOL;
	query.x1 = maxf(from.x1, to.x1);
	query.y1 = maxf(from.y1, to.y1);
	gather(world, &query);
	for (i = 0; i < world->scratch_count; i++) {
		uint32_t other = world->scratch[i];
		const AthenaBody *body = &world->slots[other].body;

		if (other == index || body->type != ATHENA_BODY_DYNAMIC)
			continue;
		if (body->on_ground && body->ground == id) {
			/* Riders first, in the lower part of aux. */
			world->aux[count++] = world->aux[riders];
			world->aux[riders++] = other;
		} else if (!platform->sensor && blocks(body, platform)) {
			world->aux[count++] = other;
		}
	}
	platform->x += dx;
	platform->y += dy;
	hash_refresh(world, index);
	for (i = 0; i < count; i++) {
		uint32_t other = world->aux[i];
		AthenaBody *body = &world->slots[other].body;
		AthenaCollisionMove move;
		bool blocked_y;

		if (i < riders) {
			Box box;

			move_body(world, other, dx, dy, &move, &blocked_y);
			box = body_box(body);
			/*
			 * Held back (a lift rising into a ceiling): the platform went
			 * into the rider, which is crushed, like a pushed body.
			 */
			if (boxes_overlap(&box, &to)) {
				body->crushed = true;
			} else {
				body->on_ground = true;
				body->ground = id;
			}
			continue;
		}
		{
			Box box = body_box(body);
			float px = platform->one_way ? 0.0f : push_distance(&from, &to, &box, 0, dx);
			float py = push_distance(&from, &to, &box, 1, dy);

			/* A one-way platform only lifts what is on top of it. */
			if (platform->one_way && py > 0.0f)
				py = 0.0f;
			if (px == 0.0f && py == 0.0f)
				continue;
			move_body(world, other, px, py, &move, &blocked_y);
			box = body_box(body);
			if (boxes_overlap(&box, &to))
				body->crushed = true;
		}
	}
}

void athena_collision_translate(AthenaCollisionWorld *world, AthenaBodyId id,
	float dx, float dy)
{
	Slot *slot = slot_of(world, id);

	if (!slot)
		return;
	if (slot->body.type == ATHENA_BODY_KINEMATIC)
		kinematic_move(world, (uint32_t)(slot - world->slots), dx, dy);
	else
		athena_collision_set_position(world, id, slot->body.x + dx, slot->body.y + dy);
}

void athena_collision_move(AthenaCollisionWorld *world, AthenaBodyId id,
	float dx, float dy, AthenaCollisionMove *result)
{
	Slot *slot = slot_of(world, id);
	AthenaCollisionMove move;
	bool blocked_y;

	if (!slot) {
		if (result)
			memset(result, 0, sizeof(*result));
		return;
	}
	if (slot->body.type == ATHENA_BODY_KINEMATIC) {
		kinematic_move(world, (uint32_t)(slot - world->slots), dx, dy);
		memset(&move, 0, sizeof(move));
		move.dx = dx;
		move.dy = dy;
	} else {
		move_body(world, (uint32_t)(slot - world->slots), dx, dy, &move, &blocked_y);
	}
	if (result)
		*result = move;
}

/* Velocity after hitting something: bounced, or 0 when slower than gravity's pull. */
static float bounce_velocity(float v, float bounce, float gravity_step)
{
	float out = -v * bounce;

	return fabsf(out) <= fabsf(gravity_step) * 2.0f ? 0.0f : out;
}

static float limit(float v, float max)
{
	return max > 0.0f ? clampf(v, -max, max) : v;
}

/*
 * Whether a contact of a step is new for the body: it landed or stepped onto
 * another body, or started pushing a wall or a ceiling. Resting contacts
 * (standing, walking along the floor, pushing the same wall) repeat every
 * step and are not reported again.
 */
static bool contact_began(const AthenaCollisionContact *contact, const AthenaBody *before,
	const AthenaCollisionMove *move)
{
	if (contact->normal_y < 0.0f)
		return !before->on_ground || before->ground != contact->other;
	if (contact->normal_y > 0.0f)
		return !before->on_ceiling;
	return before->on_wall != move->hit_wall;
}

static int compare_keys(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;

	return x < y ? -1 : x > y;
}

static uint64_t touch_key(AthenaBodyId sensor, AthenaBodyId other)
{
	return (uint64_t)sensor << 32 | other;
}

/*
 * Adds the sensor overlap of slots i and j, if they make one: a sensor and a
 * body its filters agree with, not both static. The key names the sensor
 * first; two sensors, the lower slot. -1 when out of memory.
 */
static int touch_add(World *world, uint32_t i, uint32_t j, uint32_t *count)
{
	const AthenaBody *a = &world->slots[i].body, *b = &world->slots[j].body;
	AthenaShape shape_a, shape_b;
	bool a_first;

	if ((!a->sensor && !b->sensor) ||
		(a->type == ATHENA_BODY_STATIC && b->type == ATHENA_BODY_STATIC) ||
		!(a->mask & b->layer) || !(b->mask & a->layer))
		return 0;
	shape_a = body_shape(a);
	shape_b = body_shape(b);
	if (!athena_collision_shape_overlap(&shape_a, &shape_b))
		return 0;
	if (grow((void **)&world->touching_next, &world->touching_next_capacity,
			*count + 1u, sizeof(uint64_t)) < 0)
		return -1;
	a_first = a->sensor && (!b->sensor || i < j);
	world->touching_next[(*count)++] = a_first ?
		touch_key(make_id(world, i), make_id(world, j)) :
		touch_key(make_id(world, j), make_id(world, i));
	return 0;
}

/*
 * Lists the sensor overlaps of this step, sorted, and reports the
 * difference with the last step's list: new keys entered, missing ones
 * exited (unless a body of the pair is gone).
 *
 * Every pair has a sensor and a body that is not static: the pairs are
 * found from the smaller of the two groups, one query per member. Many
 * coins and one player make one query, as do many bullets and one zone.
 */
static void sensors_update(World *world)
{
	uint32_t count = 0, sensors = 0, movers = 0, i, j, a, b;
	bool from_sensors;
	uint64_t *swap;

	if (!world->sensor_func)
		return;
	for (i = 0; i < world->slot_count; i++) {
		const AthenaBody *body = &world->slots[i].body;

		if (!world->slots[i].alive)
			continue;
		sensors += body->sensor;
		movers += body->type != ATHENA_BODY_STATIC;
	}
	from_sensors = sensors <= movers;
	for (i = 0; sensors && movers && i < world->slot_count; i++) {
		const AthenaBody *body = &world->slots[i].body;
		Box box;

		if (!world->slots[i].alive ||
			(from_sensors ? !body->sensor : body->type == ATHENA_BODY_STATIC))
			continue;
		box = body_box(body);
		gather(world, &box);
		for (j = 0; j < world->scratch_count; j++) {
			uint32_t index = world->scratch[j];
			const AthenaBody *other = &world->slots[index].body;

			/* Two members of the group: the pair once, from the lower slot. */
			if (index == i || (index < i &&
					(from_sensors ? other->sensor : other->type != ATHENA_BODY_STATIC)))
				continue;
			/* Out of memory: this step reports nothing, the next one catches up. */
			if (touch_add(world, i, index, &count) < 0)
				return;
		}
	}
	/* The list is still NULL before the first overlap: qsort() must not see it. */
	if (count > 1)
		qsort(world->touching_next, count, sizeof(uint64_t), compare_keys);
	for (a = b = 0; a < world->touching_count || b < count; ) {
		uint64_t old = a < world->touching_count ? world->touching[a] : UINT64_MAX;
		uint64_t now = b < count ? world->touching_next[b] : UINT64_MAX;

		if (old == now) {
			a++;
			b++;
		} else if (now < old) {
			world->sensor_func(world->sensor_opaque, (AthenaBodyId)(now >> 32),
				(AthenaBodyId)now, true);
			b++;
		} else {
			if (slot_of(world, (AthenaBodyId)(old >> 32)) && slot_of(world, (AthenaBodyId)old))
				world->sensor_func(world->sensor_opaque, (AthenaBodyId)(old >> 32),
					(AthenaBodyId)old, false);
			a++;
		}
	}
	swap = world->touching;
	world->touching = world->touching_next;
	world->touching_next = swap;
	i = world->touching_capacity;
	world->touching_capacity = world->touching_next_capacity;
	world->touching_next_capacity = i;
	world->touching_count = count;
}

void athena_collision_step(AthenaCollisionWorld *world, float dt)
{
	uint32_t i, c;

	if (!(dt > 0.0f))
		return;
	for (i = 0; i < world->slot_count; i++)
		world->slots[i].body.crushed = false;
	/* Platforms first: riders are carried, bodies in the way pushed. */
	for (i = 0; i < world->slot_count; i++) {
		AthenaBody *body = &world->slots[i].body;

		if (world->slots[i].alive && body->type == ATHENA_BODY_KINEMATIC)
			kinematic_move(world, i, body->vx * dt, body->vy * dt);
	}
	for (i = 0; i < world->slot_count; i++) {
		Slot *slot = &world->slots[i];
		AthenaBody *body = &slot->body;
		AthenaCollisionMove move;
		AthenaBody before;
		float gx, gy;
		bool blocked_y;

		if (!slot->alive || body->type != ATHENA_BODY_DYNAMIC)
			continue;
		before = *body;
		gx = world->gravity_x * body->gravity_scale * dt;
		gy = world->gravity_y * body->gravity_scale * dt;
		body->vx += gx;
		body->vy += gy;
		if (body->damping > 0.0f) {
			float keep = expf(-body->damping * dt);

			body->vx *= keep;
			body->vy *= keep;
		}
		body->vx = limit(body->vx, body->max_speed_x);
		body->vy = limit(body->vy, body->max_speed_y);
		move_body(world, i, body->vx * dt, body->vy * dt, &move, &blocked_y);
		/* move_body() may not grow the slots: `body` is still valid. */
		if (move.hit_wall)
			body->vx = bounce_velocity(body->vx, body->bounce, gx);
		if (blocked_y)
			body->vy = bounce_velocity(body->vy, body->bounce, gy);
		else if (move.on_ground && body->vy > 0.0f)
			body->vy = 0.0f;
		if (world->contact_func)
			for (c = 0; c < move.contact_count; c++)
				if (contact_began(&move.contacts[c], &before, &move))
					world->contact_func(world->contact_opaque, make_id(world, i),
						&move.contacts[c]);
	}
	sensors_update(world);
}

/* ---- Shapes -------------------------------------------------------------- */

static bool circle_rect_overlap(float cx, float cy, float r, float x, float y,
	float w, float h)
{
	float dx = cx - clampf(cx, x, x + w), dy = cy - clampf(cy, y, y + h);

	return dx * dx + dy * dy < r * r;
}

bool athena_collision_shape_overlap(const AthenaShape *a, const AthenaShape *b)
{
	if (a->type == ATHENA_SHAPE_RECT && b->type == ATHENA_SHAPE_RECT)
		return a->x < b->x + b->w && b->x < a->x + a->w &&
			a->y < b->y + b->h && b->y < a->y + a->h;
	if (a->type == ATHENA_SHAPE_CIRCLE && b->type == ATHENA_SHAPE_CIRCLE) {
		float dx = a->x - b->x, dy = a->y - b->y, r = a->r + b->r;

		return dx * dx + dy * dy < r * r;
	}
	if (a->type == ATHENA_SHAPE_CIRCLE)
		return circle_rect_overlap(a->x, a->y, a->r, b->x, b->y, b->w, b->h);
	return circle_rect_overlap(b->x, b->y, b->r, a->x, a->y, a->w, a->h);
}

/* Moves circle (cx, cy, r) out of a rectangle. */
static bool circle_rect_resolve(float cx, float cy, float r, const AthenaShape *rect,
	float *dx, float *dy)
{
	float qx = clampf(cx, rect->x, rect->x + rect->w);
	float qy = clampf(cy, rect->y, rect->y + rect->h);
	float ex = cx - qx, ey = cy - qy, d2 = ex * ex + ey * ey;

	if (d2 >= r * r)
		return false;
	if (d2 > 0.0f) {
		float d = sqrtf(d2), push = r - d;

		*dx = ex / d * push;
		*dy = ey / d * push;
		return true;
	}
	/* The center is inside: out through the nearest side. */
	{
		float left = cx - rect->x, right = rect->x + rect->w - cx;
		float top = cy - rect->y, bottom = rect->y + rect->h - cy;
		float best = minf(minf(left, right), minf(top, bottom));

		*dx = *dy = 0.0f;
		if (best == top)
			*dy = -(top + r);
		else if (best == bottom)
			*dy = bottom + r;
		else if (best == left)
			*dx = -(left + r);
		else
			*dx = right + r;
	}
	return true;
}

bool athena_collision_shape_resolve(const AthenaShape *a, const AthenaShape *b,
	float *dx, float *dy)
{
	if (a->type == ATHENA_SHAPE_RECT && b->type == ATHENA_SHAPE_RECT) {
		float left = a->x + a->w - b->x, right = b->x + b->w - a->x;
		float up = a->y + a->h - b->y, down = b->y + b->h - a->y;
		float best;

		if (!athena_collision_shape_overlap(a, b))
			return false;
		best = minf(minf(left, right), minf(up, down));
		*dx = *dy = 0.0f;
		if (best == up)
			*dy = -up;
		else if (best == down)
			*dy = down;
		else if (best == left)
			*dx = -left;
		else
			*dx = right;
		return true;
	}
	if (a->type == ATHENA_SHAPE_CIRCLE && b->type == ATHENA_SHAPE_CIRCLE) {
		float ex = a->x - b->x, ey = a->y - b->y, r = a->r + b->r;
		float d2 = ex * ex + ey * ey, d;

		if (d2 >= r * r)
			return false;
		if (d2 == 0.0f) {
			*dx = 0.0f;
			*dy = -r;
			return true;
		}
		d = sqrtf(d2);
		*dx = ex / d * (r - d);
		*dy = ey / d * (r - d);
		return true;
	}
	if (a->type == ATHENA_SHAPE_CIRCLE)
		return circle_rect_resolve(a->x, a->y, a->r, b, dx, dy);
	if (!circle_rect_resolve(b->x, b->y, b->r, a, dx, dy))
		return false;
	*dx = -*dx;
	*dy = -*dy;
	return true;
}

/* Segment p + t d, t in [0, 1], against a box; hits from outside only. */
static bool segment_box(float px, float py, float dx, float dy, const Box *box,
	float *t, float *nx, float *ny)
{
	float enter = -NO_GAP, leave = NO_GAP, enx = 0.0f, eny = 0.0f;
	const float p[2] = { px, py }, d[2] = { dx, dy };
	const float lo[2] = { box->x0, box->y0 }, hi[2] = { box->x1, box->y1 };
	int a;

	for (a = 0; a < 2; a++) {
		float t0, t1, n;

		if (d[a] == 0.0f) {
			if (p[a] <= lo[a] || p[a] >= hi[a])
				return false;
			continue;
		}
		t0 = (lo[a] - p[a]) / d[a];
		t1 = (hi[a] - p[a]) / d[a];
		n = -1.0f;
		if (t0 > t1) {
			float swap = t0;

			t0 = t1;
			t1 = swap;
			n = 1.0f;
		}
		if (t0 > enter) {
			enter = t0;
			enx = a == 0 ? n : 0.0f;
			eny = a == 1 ? n : 0.0f;
		}
		leave = minf(leave, t1);
	}
	if (enter > leave || enter < 0.0f || enter > 1.0f || leave <= 0.0f)
		return false;
	*t = enter;
	*nx = enx;
	*ny = eny;
	return true;
}

static bool segment_circle(float px, float py, float dx, float dy, float cx,
	float cy, float r, float *t, float *nx, float *ny)
{
	float fx = px - cx, fy = py - cy;
	float a = dx * dx + dy * dy, b = 2.0f * (fx * dx + fy * dy);
	float c = fx * fx + fy * fy - r * r, disc, hit;

	if (c < 0.0f || a == 0.0f)
		return false;
	disc = b * b - 4.0f * a * c;
	if (disc < 0.0f)
		return false;
	hit = (-b - sqrtf(disc)) / (2.0f * a);
	if (hit < 0.0f || hit > 1.0f)
		return false;
	*t = hit;
	*nx = (fx + hit * dx) / r;
	*ny = (fy + hit * dy) / r;
	return true;
}

/* Segment against a convex polygon (Cyrus-Beck); hits from outside only. */
static bool segment_polygon(float px, float py, float dx, float dy, const float *xs,
	const float *ys, int count, float *t, float *nx, float *ny)
{
	float enter = -NO_GAP, leave = NO_GAP, enx = 0.0f, eny = 0.0f, mx = 0.0f, my = 0.0f;
	int i;

	for (i = 0; i < count; i++) {
		mx += xs[i];
		my += ys[i];
	}
	mx /= (float)count;
	my /= (float)count;
	for (i = 0; i < count; i++) {
		int j = (i + 1) % count;
		float ex = xs[j] - xs[i], ey = ys[j] - ys[i], length = sqrtf(ex * ex + ey * ey);
		float ux, uy, num, den;

		if (length < EPS)
			continue;
		ux = ey / length;
		uy = -ex / length;
		if (ux * ((xs[i] + xs[j]) * 0.5f - mx) + uy * ((ys[i] + ys[j]) * 0.5f - my) < 0.0f) {
			ux = -ux;
			uy = -uy;
		}
		num = ux * (xs[i] - px) + uy * (ys[i] - py);
		den = ux * dx + uy * dy;
		if (den == 0.0f) {
			if (num < 0.0f)
				return false;
		} else if (den < 0.0f) {
			if (num / den > enter) {
				enter = num / den;
				enx = ux;
				eny = uy;
			}
		} else {
			leave = minf(leave, num / den);
		}
	}
	if (enter > leave || enter < 0.0f || enter > 1.0f)
		return false;
	*t = enter;
	*nx = enx;
	*ny = eny;
	return true;
}

bool athena_collision_segment_shape(float x1, float y1, float x2, float y2,
	const AthenaShape *shape, float *fraction, float *normal_x, float *normal_y)
{
	if (shape->type == ATHENA_SHAPE_CIRCLE)
		return segment_circle(x1, y1, x2 - x1, y2 - y1, shape->x, shape->y, shape->r,
			fraction, normal_x, normal_y);
	{
		Box box = { shape->x, shape->y, shape->x + shape->w, shape->y + shape->h };

		return segment_box(x1, y1, x2 - x1, y2 - y1, &box, fraction, normal_x, normal_y);
	}
}

/* ---- Queries ------------------------------------------------------------- */

static uint32_t query_shape(World *world, const AthenaShape *shape, uint32_t mask,
	AthenaBodyId *out, uint32_t max)
{
	Box box;
	uint32_t i, count = 0;

	if (shape->type == ATHENA_SHAPE_CIRCLE) {
		box.x0 = shape->x - shape->r;
		box.y0 = shape->y - shape->r;
		box.x1 = shape->x + shape->r;
		box.y1 = shape->y + shape->r;
	} else {
		box.x0 = shape->x;
		box.y0 = shape->y;
		box.x1 = shape->x + shape->w;
		box.y1 = shape->y + shape->h;
	}
	gather(world, &box);
	for (i = 0; i < world->scratch_count; i++) {
		uint32_t index = world->scratch[i];
		const AthenaBody *body = &world->slots[index].body;
		AthenaShape other;

		if (!(body->layer & mask))
			continue;
		other = body_shape(body);
		if (!athena_collision_shape_overlap(shape, &other))
			continue;
		if (count < max)
			out[count] = make_id(world, index);
		count++;
	}
	return count;
}

uint32_t athena_collision_query_rect(AthenaCollisionWorld *world, float x,
	float y, float w, float h, uint32_t mask, AthenaBodyId *out, uint32_t max)
{
	AthenaShape shape = { ATHENA_SHAPE_RECT, x, y, w, h, 0.0f };

	return query_shape(world, &shape, mask, out, max);
}

uint32_t athena_collision_query_circle(AthenaCollisionWorld *world, float x,
	float y, float r, uint32_t mask, AthenaBodyId *out, uint32_t max)
{
	AthenaShape shape = { ATHENA_SHAPE_CIRCLE, x, y, r * 2.0f, r * 2.0f, r };

	return query_shape(world, &shape, mask, out, max);
}

/* Rectangles hold their top and left edges, not the bottom and right ones. */
static bool point_in_body(const AthenaBody *body, float x, float y)
{
	if (body->shape == ATHENA_SHAPE_CIRCLE) {
		float dx = x - body->x, dy = y - body->y;

		return dx * dx + dy * dy < body->r * body->r;
	}
	return x >= body->x && x < body->x + body->w && y >= body->y && y < body->y + body->h;
}

uint32_t athena_collision_query_point(AthenaCollisionWorld *world, float x,
	float y, uint32_t mask, AthenaBodyId *out, uint32_t max)
{
	Box box = { x, y, x, y };
	uint32_t i, count = 0;

	gather(world, &box);
	for (i = 0; i < world->scratch_count; i++) {
		uint32_t index = world->scratch[i];
		const AthenaBody *body = &world->slots[index].body;

		if (!(body->layer & mask) || !point_in_body(body, x, y))
			continue;
		if (count < max)
			out[count] = make_id(world, index);
		count++;
	}
	return count;
}

bool athena_collision_point_solid(AthenaCollisionWorld *world, float x, float y,
	uint32_t mask)
{
	Box box = { x, y, x, y };
	int32_t column, row;
	uint32_t i;

	if (world->has_grid && (world->grid_layer & mask) &&
		athena_collision_cell_at(world, x, y, &column, &row)) {
		uint16_t id;
		const AthenaTileKind *kind = tile_kind(world, column, row, &id);

		if (kind && kind->type == ATHENA_TILE_SOLID)
			return true;
		if (kind && kind->type == ATHENA_TILE_SLOPE) {
			Box tile = tile_box(world, column, row);

			if (y >= slope_floor(world, kind, &tile, x, x))
				return true;
		}
	}
	gather(world, &box);
	for (i = 0; i < world->scratch_count; i++) {
		const AthenaBody *body = &world->slots[world->scratch[i]].body;

		if (!body->sensor && (body->layer & mask) && point_in_body(body, x, y))
			return true;
	}
	return false;
}

uint32_t athena_collision_overlapping(AthenaCollisionWorld *world,
	AthenaBodyId id, AthenaBodyId *out, uint32_t max)
{
	Slot *slot = slot_of(world, id);
	uint32_t self, i, count = 0;
	AthenaShape shape;
	Box box;

	if (!slot)
		return 0;
	self = (uint32_t)(slot - world->slots);
	shape = body_shape(&slot->body);
	box = body_box(&slot->body);
	gather(world, &box);
	for (i = 0; i < world->scratch_count; i++) {
		uint32_t index = world->scratch[i];
		const AthenaBody *a = &world->slots[self].body, *b = &world->slots[index].body;
		AthenaShape other;

		if (index == self || !(a->mask & b->layer) || !(b->mask & a->layer))
			continue;
		other = body_shape(b);
		if (!athena_collision_shape_overlap(&shape, &other))
			continue;
		if (count < max)
			out[count] = make_id(world, index);
		count++;
	}
	return count;
}

uint32_t athena_collision_pairs(AthenaCollisionWorld *world, uint32_t layer_a,
	uint32_t layer_b, AthenaBodyPair *out, uint32_t max)
{
	uint32_t i, j, count = 0;

	for (i = 0; i < world->slot_count; i++) {
		const AthenaBody *a = &world->slots[i].body;
		AthenaShape shape;
		Box box;

		if (!world->slots[i].alive || !(a->layer & layer_a))
			continue;
		shape = body_shape(a);
		box = body_box(a);
		gather(world, &box);
		for (j = 0; j < world->scratch_count; j++) {
			uint32_t index = world->scratch[j];
			const AthenaBody *b = &world->slots[index].body;
			AthenaShape other;

			if (index == i || !(b->layer & layer_b))
				continue;
			/* Both on both layers: reported from the lower slot only. */
			if ((b->layer & layer_a) && (a->layer & layer_b) && index < i)
				continue;
			other = body_shape(b);
			if (!athena_collision_shape_overlap(&shape, &other))
				continue;
			if (count < max) {
				out[count].a = make_id(world, i);
				out[count].b = make_id(world, index);
			}
			count++;
		}
	}
	return count;
}

/* ---- Raycast ------------------------------------------------------------- */

typedef struct {
	float px, py, dx, dy;
	float best;
	AthenaRayHit hit;
} Ray;

/*
 * Walks the tiles along the ray in order (Amanatides and Woo), from where
 * it enters the grid, and stops at the first tile hit before `ray->best`.
 */
static void ray_tiles(const World *world, Ray *ray)
{
	Box grid = { world->grid_x, world->grid_y,
		world->grid_x + (float)world->columns * world->tile_w,
		world->grid_y + (float)world->rows * world->tile_h };
	const float p[2] = { ray->px, ray->py }, d[2] = { ray->dx, ray->dy };
	const float lo[2] = { grid.x0, grid.y0 }, hi[2] = { grid.x1, grid.y1 };
	const float size[2] = { world->tile_w, world->tile_h };
	const uint32_t count[2] = { world->columns, world->rows };
	float enter = 0.0f, leave = 1.0f, next[2], delta[2];
	int32_t cell[2], step[2];
	int axis = -1, a, guard;

	/* Clip the segment to the grid. */
	for (a = 0; a < 2; a++) {
		float t0, t1;

		if (d[a] == 0.0f) {
			if (p[a] < lo[a] || p[a] > hi[a])
				return;
			continue;
		}
		t0 = (lo[a] - p[a]) / d[a];
		t1 = (hi[a] - p[a]) / d[a];
		if (t0 > t1) {
			float swap = t0;

			t0 = t1;
			t1 = swap;
		}
		if (t0 > enter) {
			enter = t0;
			axis = a;
		}
		leave = minf(leave, t1);
	}
	if (enter > leave)
		return;
	for (a = 0; a < 2; a++) {
		float at = p[a] + d[a] * enter;
		int32_t c = tile_coord(at, lo[a], size[a], count[a]);

		/* On the far border, entering backwards: the last cell. */
		if (c >= (int32_t)count[a])
			c = (int32_t)count[a] - 1;
		if (c < 0)
			c = 0;
		cell[a] = c;
		if (d[a] > 0.0f) {
			step[a] = 1;
			next[a] = (lo[a] + (float)(c + 1) * size[a] - p[a]) / d[a];
			delta[a] = size[a] / d[a];
		} else if (d[a] < 0.0f) {
			step[a] = -1;
			next[a] = (lo[a] + (float)c * size[a] - p[a]) / d[a];
			delta[a] = -size[a] / d[a];
		} else {
			step[a] = 0;
			next[a] = NO_GAP;
			delta[a] = NO_GAP;
		}
	}
	for (guard = (int)(count[0] + count[1]) + 2; guard > 0; guard--) {
		float cell_leave = minf(minf(next[0], next[1]), leave);
		uint16_t id;
		const AthenaTileKind *kind;

		if (enter > ray->best)
			return;
		kind = tile_kind(world, cell[0], cell[1], &id);
		if (kind) {
			Box t = tile_box(world, cell[0], cell[1]);
			float ht = 0.0f, hnx = 0.0f, hny = 0.0f;
			bool hit = false;

			if (kind->type == ATHENA_TILE_SOLID) {
				/* A tile holding the start is ignored. */
				if (axis >= 0) {
					ht = enter;
					hnx = axis == 0 ? (float)-step[0] : 0.0f;
					hny = axis == 1 ? (float)-step[1] : 0.0f;
					hit = true;
				}
			} else if (kind->type == ATHENA_TILE_ONE_WAY) {
				if (ray->dy > 0.0f && ray->py <= t.y0) {
					ht = (t.y0 - ray->py) / ray->dy;
					if (ht >= enter - EPS && ht <= cell_leave + EPS) {
						float x = ray->px + ray->dx * ht;

						hit = x >= t.x0 && x <= t.x1;
						hny = -1.0f;
					}
				}
			} else {
				float xs[4] = { t.x0, t.x1, t.x1, t.x0 };
				float ys[4] = { t.y1, t.y1, t.y1 - kind->right * world->tile_h,
					t.y1 - kind->left * world->tile_h };

				hit = segment_polygon(ray->px, ray->py, ray->dx, ray->dy, xs, ys, 4,
					&ht, &hnx, &hny);
			}
			if (hit && ht <= ray->best) {
				ray->best = ht;
				ray->hit.body = ATHENA_BODY_NONE;
				ray->hit.tile = id;
				ray->hit.column = cell[0];
				ray->hit.row = cell[1];
				ray->hit.normal_x = hnx;
				ray->hit.normal_y = hny;
				return;
			}
		}
		if (cell_leave >= leave)
			return;
		axis = next[0] < next[1] ? 0 : 1;
		enter = next[axis];
		next[axis] += delta[axis];
		cell[axis] += step[axis];
		if (cell[axis] < 0 || cell[axis] >= (int32_t)count[axis])
			return;
	}
}

bool athena_collision_raycast(AthenaCollisionWorld *world, float x1, float y1,
	float x2, float y2, const AthenaRaycastDesc *desc, AthenaRayHit *hit)
{
	AthenaRaycastDesc all = { 0xFFFFFFFFu, ATHENA_BODY_NONE, false };
	Ray ray;
	uint32_t i;

	if (!desc)
		desc = &all;
	memset(&ray, 0, sizeof(ray));
	ray.px = x1;
	ray.py = y1;
	ray.dx = x2 - x1;
	ray.dy = y2 - y1;
	ray.best = 2.0f;
	gather_segment(world, x1, y1, x2, y2);
	for (i = 0; i < world->scratch_count; i++) {
		uint32_t index = world->scratch[i];
		const AthenaBody *body = &world->slots[index].body;
		AthenaShape shape;
		float t, nx, ny;

		if (!(body->layer & desc->mask) || (body->sensor && !desc->sensors) ||
			make_id(world, index) == desc->ignore)
			continue;
		shape = body_shape(body);
		if (!athena_collision_segment_shape(x1, y1, x2, y2, &shape, &t, &nx, &ny))
			continue;
		if (body->one_way && !(ny < 0.0f && ray.dy > 0.0f))
			continue;
		if (t < ray.best) {
			ray.best = t;
			ray.hit.body = make_id(world, index);
			ray.hit.tile = -1;
			ray.hit.column = ray.hit.row = -1;
			ray.hit.normal_x = nx;
			ray.hit.normal_y = ny;
		}
	}
	if (world->has_grid && (world->grid_layer & desc->mask))
		ray_tiles(world, &ray);
	if (ray.best > 1.0f)
		return false;
	ray.hit.fraction = ray.best;
	ray.hit.x = x1 + ray.dx * ray.best;
	ray.hit.y = y1 + ray.dy * ray.best;
	*hit = ray.hit;
	return true;
}
