/* Host test of the light collision and physics core (collision/native/collision.c). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <athena/collision.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define NEAR(a, b, eps) (fabsf((a) - (b)) <= (eps))

static const float DT = 1.0f / 60.0f;

static AthenaCollisionWorld *new_world(float gravity) {
    AthenaCollisionWorldDesc desc;

    athena_collision_world_desc_init(&desc);
    desc.gravity_y = gravity;
    return athena_collision_world_create(&desc);
}

static AthenaBodyId add_rect(AthenaCollisionWorld *w, AthenaBodyType type, float x, float y,
    float width, float height) {
    AthenaCollisionBodyDesc desc;
    AthenaBodyId id = 0;

    athena_collision_body_desc_init(&desc);
    desc.type = type;
    desc.x = x;
    desc.y = y;
    desc.w = width;
    desc.h = height;
    if (athena_collision_add(w, &desc, &id) != 0)
        printf("  add failed\n");
    return id;
}

static AthenaBodyId add_circle(AthenaCollisionWorld *w, AthenaBodyType type, float x, float y,
    float r) {
    AthenaCollisionBodyDesc desc;
    AthenaBodyId id = 0;

    athena_collision_body_desc_init(&desc);
    desc.shape = ATHENA_SHAPE_CIRCLE;
    desc.type = type;
    desc.x = x;
    desc.y = y;
    desc.r = r;
    athena_collision_add(w, &desc, &id);
    return id;
}

/* Kinds: 0 empty, 1 solid, 2 one-way, 3 slope rising right, 4 slope falling right, 5 half. */
static const AthenaTileKind KINDS[] = {
    { ATHENA_TILE_EMPTY, 0, 0 },
    { ATHENA_TILE_SOLID, 0, 0 },
    { ATHENA_TILE_ONE_WAY, 0, 0 },
    { ATHENA_TILE_SLOPE, 0, 1 },
    { ATHENA_TILE_SLOPE, 1, 0 },
    { ATHENA_TILE_SLOPE, 0.5f, 1 },
};

/* A grid of 16x16 tiles from rows of characters: '#' solid, '-' one-way, '/' and '\\' slopes. */
static void set_grid(AthenaCollisionWorld *w, const char *const *rows, uint32_t count) {
    uint32_t columns = (uint32_t)strlen(rows[0]), r, c;
    uint16_t *tiles = calloc(columns * count, sizeof(uint16_t));
    AthenaCollisionGridDesc desc;

    for (r = 0; r < count; r++) {
        for (c = 0; c < columns; c++) {
            char ch = rows[r][c];
            tiles[r * columns + c] = ch == '#' ? 1 : ch == '-' ? 2 : ch == '/' ? 3 :
                ch == '\\' ? 4 : ch == 'h' ? 5 : 0;
        }
    }
    memset(&desc, 0, sizeof(desc));
    desc.columns = columns;
    desc.rows = count;
    desc.tile_width = desc.tile_height = 16.0f;
    desc.tiles = tiles;
    desc.kinds = KINDS;
    desc.kind_count = sizeof(KINDS) / sizeof(KINDS[0]);
    desc.layer = 1;
    if (athena_collision_set_grid(w, &desc) != 0)
        printf("  set_grid failed\n");
    free(tiles);
}

static void test_bodies_and_ids(void) {
    AthenaCollisionWorld *w = new_world(0);
    AthenaCollisionBodyDesc desc;
    AthenaBodyId a, b, c, out[4];

    a = add_rect(w, ATHENA_BODY_STATIC, 0, 0, 10, 10);
    b = add_rect(w, ATHENA_BODY_STATIC, 20, 0, 10, 10);
    CHECK(a && b && a != b, "ids %u %u", a, b);
    CHECK(athena_collision_body_count(w) == 2, "count");
    CHECK(athena_collision_remove(w, a) && !athena_collision_remove(w, a), "remove once");
    CHECK(!athena_collision_get(w, a), "stale id after remove");
    c = add_rect(w, ATHENA_BODY_STATIC, 40, 0, 10, 10);
    CHECK(ATHENA_COLLISION_ID_SLOT(c) == ATHENA_COLLISION_ID_SLOT(a) && c != a,
        "slot reused with a new generation");
    CHECK(!athena_collision_get(w, a) && athena_collision_get(w, c), "old id stays stale");
    CHECK(!athena_collision_get(w, 0) && !athena_collision_get(w, 0xFFFFFu), "invalid ids");
    athena_collision_body_desc_init(&desc);
    desc.w = 0;
    desc.h = 5;
    CHECK(athena_collision_add(w, &desc, &a) == ATHENA_COLLISION_EINVAL, "empty rect");
    desc.shape = ATHENA_SHAPE_CIRCLE;
    desc.r = -1;
    CHECK(athena_collision_add(w, &desc, &a) == ATHENA_COLLISION_EINVAL, "negative radius");
    CHECK(athena_collision_query_point(w, 45, 5, ~0u, out, 4) == 1 && out[0] == c, "query after reuse");
    CHECK(athena_collision_query_point(w, 5, 5, ~0u, out, 4) == 0, "removed body not found");
    athena_collision_set_position(w, c, 200, 200);
    CHECK(athena_collision_query_point(w, 45, 5, ~0u, out, 4) == 0 &&
        athena_collision_query_point(w, 205, 205, ~0u, out, 4) == 1, "hash follows set_position");
    athena_collision_world_destroy(w);
}

static float rnd(uint32_t *seed) {
    *seed = *seed * 1664525u + 1013904223u;
    return (float)(*seed >> 8) / 16777216.0f;
}

/* The spatial hash must find exactly what a scan of every body finds. */
static void test_hash_against_scan(void) {
    AthenaCollisionWorld *w = new_world(0);
    static AthenaBodyId ids[1500], found[1500];
    uint32_t seed = 12345, i, q, mismatches = 0;

#define RND() rnd(&seed)
    for (i = 0; i < 1500; i++) {
        float size = RND() < 0.05f ? 200 + RND() * 600 : 2 + RND() * 40;
        float x = RND() * 4000 - 2000, y = RND() * 4000 - 2000;

        ids[i] = RND() < 0.3f ? add_circle(w, ATHENA_BODY_DYNAMIC, x, y, size / 2)
            : add_rect(w, ATHENA_BODY_STATIC, x, y, size, size);
    }
    for (i = 0; i < 1500; i += 3)
        athena_collision_remove(w, ids[i]);
    for (i = 1; i < 1500; i += 3) {
        float x = RND() * 4000 - 2000, y = RND() * 4000 - 2000;

        athena_collision_set_position(w, ids[i], x, y);
    }
    for (q = 0; q < 200; q++) {
        float x = RND() * 4400 - 2200, y = RND() * 4400 - 2200, s = RND() * (q % 10 ? 100 : 3000);
        uint32_t n = athena_collision_query_rect(w, x, y, s, s, ~0u, found, 1500), expected = 0, j;
        AthenaShape box = { ATHENA_SHAPE_RECT, x, y, s, s, 0 };

        for (j = 0; j < athena_collision_body_capacity(w); j++) {
            AthenaBody *body = athena_collision_slot(w, j);
            AthenaShape shape;

            if (!body)
                continue;
            shape = (AthenaShape){ body->shape, body->x, body->y, body->w, body->h, body->r };
            if (athena_collision_shape_overlap(&box, &shape))
                expected++;
        }
        if (n != expected)
            mismatches++;
        for (j = 0; j < n && j < 1500; j++) {
            uint32_t k;
            for (k = j + 1; k < n && k < 1500; k++)
                if (found[j] == found[k])
                    mismatches++;
        }
    }
    CHECK(mismatches == 0, "%u query mismatches against a scan", mismatches);
    CHECK(athena_collision_body_count(w) == 1000, "count %u", athena_collision_body_count(w));
    athena_collision_world_destroy(w);
}

static void test_move_bodies(void) {
    AthenaCollisionWorld *w = new_world(0);
    AthenaBodyId wall = add_rect(w, ATHENA_BODY_STATIC, 100, 0, 20, 200);
    AthenaBodyId box = add_rect(w, ATHENA_BODY_DYNAMIC, 50, 50, 20, 20);
    AthenaCollisionMove mv;
    AthenaBody *b = athena_collision_get(w, box);

    athena_collision_move(w, box, 100, 0, &mv);
    CHECK(NEAR(b->x, 80, 1e-4f) && mv.hit_wall == 1 && NEAR(mv.dx, 30, 1e-4f), "stops at the wall: x %f", b->x);
    CHECK(mv.contact_count == 1 && mv.contacts[0].other == wall && mv.contacts[0].normal_x == -1 &&
        mv.contacts[0].tile == -1, "contact with the wall");
    /* Sliding along the wall: y is free. */
    athena_collision_move(w, box, 5, 30, &mv);
    CHECK(NEAR(b->x, 80, 1e-4f) && NEAR(b->y, 80, 1e-4f) && mv.hit_wall == 1, "slides %f %f", b->x, b->y);
    /* Fast movement does not tunnel through a thin wall. */
    add_rect(w, ATHENA_BODY_STATIC, 0, 300, 400, 1);
    athena_collision_set_position(w, box, 10, 200);
    athena_collision_move(w, box, 0, 5000, &mv);
    CHECK(NEAR(b->y, 280, 1e-3f) && mv.on_ground && b->on_ground, "lands on a thin floor: y %f", b->y);
    /* Filters: a mask without the wall's layer passes through. */
    b->mask = 2;
    athena_collision_set_position(w, box, 50, 50);
    athena_collision_move(w, box, 100, 0, &mv);
    CHECK(NEAR(b->x, 150, 1e-4f) && !mv.hit_wall, "mask ignores the wall: x %f", b->x);
    b->mask = ~0u;
    /* Sensors never block. */
    athena_collision_get(w, wall)->sensor = true;
    athena_collision_set_position(w, box, 50, 50);
    athena_collision_move(w, box, 100, 0, &mv);
    CHECK(NEAR(b->x, 150, 1e-4f), "sensor wall does not block");
    athena_collision_world_destroy(w);
}

static void test_circles(void) {
    AthenaCollisionWorld *w = new_world(0);
    AthenaBodyId ball = add_circle(w, ATHENA_BODY_DYNAMIC, 0, 0, 10);
    AthenaBodyId post = add_circle(w, ATHENA_BODY_STATIC, 50, 6, 10);
    AthenaBodyId crate = add_rect(w, ATHENA_BODY_STATIC, 40, 100, 20, 20);
    AthenaCollisionMove mv;
    AthenaBody *b = athena_collision_get(w, ball);
    float expected = 50 - sqrtf(400 - 36);

    athena_collision_move(w, ball, 100, 0, &mv);
    CHECK(NEAR(b->x, expected, 1e-3f) && mv.contacts[0].other == post, "circle vs circle: %f want %f", b->x, expected);
    /* A circle passing a rectangle's corner. */
    athena_collision_set_position(w, ball, 0, 95);
    athena_collision_move(w, ball, 100, 0, &mv);
    expected = 40 - sqrtf(100 - 25);
    CHECK(NEAR(b->x, expected, 1e-3f) && mv.contacts[0].other == crate, "circle vs corner: %f want %f", b->x, expected);
    athena_collision_set_position(w, ball, 0, 89);
    athena_collision_move(w, ball, 100, 0, &mv);
    CHECK(NEAR(b->x, 100, 1e-4f) && !mv.hit_wall, "circle clears the corner");
    /* A rectangle against a circle. */
    {
        AthenaBodyId r = add_rect(w, ATHENA_BODY_DYNAMIC, 0, 300, 10, 10);
        AthenaBody *rb;

        add_circle(w, ATHENA_BODY_STATIC, 100, 300, 5);
        athena_collision_move(w, r, 200, 0, &mv);
        rb = athena_collision_get(w, r);
        CHECK(NEAR(rb->x, 100 - sqrtf(25) - 10, 1e-3f), "rect vs circle %f", rb->x);
    }
    athena_collision_world_destroy(w);
}

static void test_tiles_and_platformer(void) {
    static const char *const level[] = {
        "..........",
        "..........",
        "....--....",
        "..........",
        "#........#",
        "##########",
    };
    AthenaCollisionWorld *w = new_world(900);
    AthenaBodyId p = add_rect(w, ATHENA_BODY_DYNAMIC, 40, 20, 12, 14);
    AthenaBody *b = athena_collision_get(w, p);
    AthenaCollisionMove mv;
    int i;

    set_grid(w, level, 6);
    CHECK(athena_collision_get_tile(w, 0, 5) == 1 && athena_collision_get_tile(w, -1, 0) == -1 &&
        athena_collision_get_tile(w, 10, 0) == -1, "get_tile");
    for (i = 0; i < 120; i++)
        athena_collision_step(w, DT);
    CHECK(b->on_ground && NEAR(b->y + b->h, 80, 1e-3f) && b->vy == 0, "falls onto the floor: bottom %f", b->y + b->h);
    /* Walking into the right wall. */
    for (i = 0; i < 60; i++) {
        b->vx = 200;
        athena_collision_step(w, DT);
    }
    CHECK(NEAR(b->x + b->w, 144, 1e-3f) && b->on_wall == 1 && b->vx == 0, "right wall: %f", b->x + b->w);
    CHECK(b->on_ground, "still on the ground against the wall");
    /* Jumping through the one-way platform from below, then landing on it. */
    athena_collision_set_position(w, p, 70, 66);
    b->vx = 0;
    b->vy = -400;
    for (i = 0; i < 12; i++)
        athena_collision_step(w, DT);
    CHECK(b->y + b->h < 32, "passes up through the one-way: bottom %f", b->y + b->h);
    for (i = 0; i < 120; i++)
        athena_collision_step(w, DT);
    CHECK(b->on_ground && NEAR(b->y + b->h, 32, 1e-3f), "lands on the one-way: bottom %f", b->y + b->h);
    /* drop_through falls off it. */
    b->drop_through = true;
    for (i = 0; i < 30; i++)
        athena_collision_step(w, DT);
    b->drop_through = false;
    CHECK(NEAR(b->y + b->h, 80, 1e-3f), "drops through: bottom %f", b->y + b->h);
    /* Ceilings stop a jump. */
    athena_collision_set_position(w, p, 70, 40);
    athena_collision_move(w, p, 0, -30, &mv);
    CHECK(mv.dy == -30 && !mv.hit_ceiling, "one-way has no ceiling");
    athena_collision_set_tile(w, 4, 0, 1);
    athena_collision_set_position(w, p, 70, 40);
    athena_collision_move(w, p, 0, -50, &mv);
    CHECK(mv.hit_ceiling && NEAR(b->y, 16, 1e-3f) && mv.contacts[0].tile == 1 &&
        mv.contacts[0].column == 4 && mv.contacts[0].row == 0, "ceiling tile: y %f", b->y);
    /* Moving with dy 0 on the ground keeps on_ground. */
    athena_collision_set_position(w, p, 40, 80 - 14);
    athena_collision_move(w, p, 0, 1, NULL);
    CHECK(b->on_ground, "grounded");
    athena_collision_move(w, p, 5, 0, NULL);
    CHECK(b->on_ground, "still grounded moving with dy 0");
    athena_collision_world_destroy(w);
}

/* Floor y under the box [x0, x1] of the hill in test_slopes: its highest point. */
static float hill_floor(float x0, float x1) {
    float floor_y = 80;
    int c;

    for (c = 0; c < 14; c++) {
        float t0 = c * 16.0f, t1 = t0 + 16.0f, lo = fmaxf(x0, t0), hi = fminf(x1, t1), h = 0;

        if (hi - lo <= 0.001f)
            continue;
        /* Heights of this column's surface, in pixels above y = 80. */
        if (c == 3) h = hi - t0;
        else if (c == 4) h = 16 + hi - t0;
        else if (c == 5 || c == 6) h = 32;
        else if (c == 7) h = 16 + t1 - lo;
        else if (c == 8) h = t1 - lo;
        if (80 - h < floor_y)
            floor_y = 80 - h;
    }
    return floor_y;
}

static void test_slopes(void) {
    /*
     * A hill: ramp up two tiles (45 degrees), a plateau, ramp down, floor.
     * Columns 3-4 rise right, 5-6 plateau (solid at row 3), 7-8 fall.
     */
    static const char *const level[] = {
        "..............",
        "..............",
        "..............",
        "..../##\\......",
        ".../####\\.....",
        "##############",
    };
    /* Up to 20 px per step at 30 FPS: more than a tile, taken in parts. */
    static const float speeds[4] = { 60, 150, 300, 600 };
    AthenaCollisionWorld *w = new_world(900);
    AthenaBodyId p = add_rect(w, ATHENA_BODY_DYNAMIC, 4, 60, 10, 12);
    AthenaBody *b = athena_collision_get(w, p);
    int i, run;

    set_grid(w, level, 6);
    for (i = 0; i < 60; i++)
        athena_collision_step(w, DT);
    CHECK(b->on_ground && NEAR(b->y + b->h, 80, 1e-3f), "on the floor: %f", b->y + b->h);
    /* At 60 and 30 FPS, slow to fast: the bottom follows the surface under the highest corner. */
    for (run = 0; run < 8; run++) {
        float speed = speeds[run % 4], dt = run < 4 ? DT : 2 * DT;
        float max_error = 0, lowest_top = 1e9f, previous_x;
        int stalls = 0, airborne = 0;

        athena_collision_set_position(w, p, 4, 68);
        b->vx = b->vy = 0;
        athena_collision_move(w, p, 0, 1, NULL);
        previous_x = b->x;
        for (i = 0; i < 2000 && b->x < 170; i++) {
            b->vx = speed;
            athena_collision_step(w, dt);
            if (fabsf(b->y + b->h - hill_floor(b->x, b->x + b->w)) > max_error)
                max_error = fabsf(b->y + b->h - hill_floor(b->x, b->x + b->w));
            if (!b->on_ground)
                airborne++;
            if (b->x - previous_x < speed * dt * 0.9f)
                stalls++;
            previous_x = b->x;
            if (b->y < lowest_top)
                lowest_top = b->y;
        }
        CHECK(b->x >= 170, "speed %g dt %g: crossed the hill: x %f", speed, dt, b->x);
        CHECK(max_error < 0.05f, "speed %g dt %g: follows the surface, max error %f", speed, dt, max_error);
        CHECK(airborne == 0, "speed %g dt %g: never airborne (%d frames)", speed, dt, airborne);
        CHECK(stalls == 0, "speed %g dt %g: never stalls (%d frames)", speed, dt, stalls);
        CHECK(NEAR(lowest_top, 48 - 12, 0.05f), "speed %g dt %g: reached the plateau: top %f", speed, dt,
            lowest_top);
        /* And back left over it. */
        max_error = 0;
        for (i = 0; i < 2000 && b->x > 8; i++) {
            b->vx = -speed;
            athena_collision_step(w, dt);
            if (fabsf(b->y + b->h - hill_floor(b->x, b->x + b->w)) > max_error)
                max_error = fabsf(b->y + b->h - hill_floor(b->x, b->x + b->w));
            if (!b->on_ground)
                airborne++;
        }
        CHECK(b->x <= 8 && airborne == 0 && max_error < 0.05f,
            "speed %g dt %g: crossed back: x %f airborne %d error %f", speed, dt, b->x, airborne, max_error);
    }
    /* Landing on a slope from above. */
    athena_collision_set_position(w, p, 51, 0);
    b->vx = 0;
    b->vy = 0;
    for (i = 0; i < 60; i++)
        athena_collision_step(w, DT);
    CHECK(b->on_ground && NEAR(b->y + b->h, 80 - 13, 0.05f), "lands on the slope: bottom %f", b->y + b->h);
    athena_collision_world_destroy(w);
}

static void test_slope_walls_and_steps(void) {
    /* A slope whose high side faces the walker, a half slope and a one-pixel step. */
    static const char *const level[] = {
        "..........",
        "....\\.....",
        "##########",
    };
    AthenaCollisionWorld *w = new_world(0);
    AthenaBodyId p = add_rect(w, ATHENA_BODY_DYNAMIC, 40, 16, 12, 16);
    AthenaBody *b = athena_collision_get(w, p);
    AthenaCollisionMove mv;

    set_grid(w, level, 3);
    athena_collision_move(w, p, 0, 1, NULL);
    CHECK(b->on_ground, "grounded");
    for (int i = 0; i < 20; i++)
        athena_collision_move(w, p, 2, 0, &mv);
    CHECK(NEAR(b->x + b->w, 64, 1e-3f) && mv.hit_wall == 1 && mv.contacts[0].tile == 4,
        "the high side of a slope is a wall: %f", b->x + b->w);
    /* From the low side it is climbed. */
    athena_collision_set_position(w, p, 81, 16);
    athena_collision_move(w, p, 0, 1, NULL);
    athena_collision_move(w, p, -2, 0, &mv);
    athena_collision_move(w, p, -4, 0, &mv);
    CHECK(b->on_ground && mv.hit_wall == 0 && b->y + b->h < 32, "climbs from the low side: bottom %f", b->y + b->h);
    athena_collision_world_destroy(w);

    /* Without slopes in the grid there is no step up: a 1 px ledge is a wall. */
    {
        static const char *const flat[] = { "..........", "..........", "##########" };
        AthenaCollisionWorld *w2 = new_world(0);
        AthenaBodyId q = add_rect(w2, ATHENA_BODY_DYNAMIC, 10, 16, 10, 16);
        AthenaBody *qb = athena_collision_get(w2, q);

        set_grid(w2, flat, 3);
        add_rect(w2, ATHENA_BODY_STATIC, 60, 31, 20, 1);
        athena_collision_move(w2, q, 0, 1, NULL);
        athena_collision_move(w2, q, 100, 0, &mv);
        CHECK(NEAR(qb->x, 50, 1e-3f) && mv.hit_wall == 1, "body ledge blocks: x %f", qb->x);
        /* Walking off a ledge: the body falls instead of sticking. */
        athena_collision_set_tile(w2, 9, 2, 0);
        athena_collision_set_tile(w2, 8, 2, 0);
        athena_collision_set_tile(w2, 7, 2, 0);
        athena_collision_set_tile(w2, 6, 2, 0);
        athena_collision_set_position(w2, q, 84, 16);
        athena_collision_move(w2, q, 0, 1, NULL);
        CHECK(qb->on_ground, "on the edge");
        athena_collision_move(w2, q, 30, 0, NULL);
        CHECK(!qb->on_ground && NEAR(qb->y, 16, 1e-4f), "leaves the ledge: ground %d y %f", qb->on_ground, qb->y);
        athena_collision_world_destroy(w2);
    }
}

static void test_physics(void) {
    AthenaCollisionWorld *w = new_world(1000);
    AthenaBodyId floor = add_rect(w, ATHENA_BODY_STATIC, -1000, 200, 2000, 20);
    AthenaBodyId ball = add_circle(w, ATHENA_BODY_DYNAMIC, 0, 0, 8);
    AthenaBodyId lift = add_rect(w, ATHENA_BODY_KINEMATIC, 300, 150, 60, 10);
    AthenaBodyId rider = add_rect(w, ATHENA_BODY_DYNAMIC, 320, 130, 10, 20);
    AthenaBody *b = athena_collision_get(w, ball), *r, *l;
    float peak = 1e9f, x0;
    int i, bounces = 0;
    bool falling = true;

    (void)floor;
    b->bounce = 0.5f;
    for (i = 0; i < 600; i++) {
        float vy = b->vy;
        athena_collision_step(w, DT);
        if (falling && vy > 0 && b->vy < 0)
            bounces++;
        falling = b->vy >= 0;
        if (bounces == 1 && b->y < peak)
            peak = b->y;
    }
    CHECK(bounces >= 2, "bounces %d", bounces);
    CHECK(peak > 8 && peak < 192 - 8, "first bounce peak %f", peak);
    CHECK(b->on_ground && b->vy == 0 && NEAR(b->y, 192, 1e-3f), "comes to rest: y %f vy %f", b->y, b->vy);

    /* Damping and speed limits. */
    b->gravity_scale = 0;
    b->bounce = 0;
    b->vx = 100;
    b->damping = 2;
    athena_collision_step(w, 0.5f);
    CHECK(NEAR(b->vx, 100 * expf(-1.0f), 1e-2f), "damping: vx %f", b->vx);
    b->damping = 0;
    b->max_speed_x = 30;
    b->vx = 500;
    athena_collision_step(w, DT);
    CHECK(b->vx == 30, "max speed: vx %f", b->vx);

    /* The rider lands on the platform, then rides it. */
    for (i = 0; i < 30; i++)
        athena_collision_step(w, DT);
    r = athena_collision_get(w, rider);
    l = athena_collision_get(w, lift);
    CHECK(r->on_ground && r->ground == lift, "rider stands on the platform");
    l->vx = 60;
    l->vy = -30;
    x0 = r->x;
    for (i = 0; i < 60; i++)
        athena_collision_step(w, DT);
    CHECK(NEAR(r->x - x0, 60, 0.05f) && NEAR(r->y + r->h, l->y, 0.05f) && r->on_ground && r->ground == lift,
        "carried: moved %f, bottom %f top %f", r->x - x0, r->y + r->h, l->y);
    l->vy = 45;
    for (i = 0; i < 40; i++)
        athena_collision_step(w, DT);
    CHECK(NEAR(r->y + r->h, l->y, 0.05f) && r->on_ground, "rides down: bottom %f top %f", r->y + r->h, l->y);
    athena_collision_world_destroy(w);
}

static int contacts_seen;
static AthenaBodyId last_other;
static void on_contact(void *opaque, AthenaBodyId body, const AthenaCollisionContact *contact) {
    (void)opaque;
    (void)body;
    contacts_seen++;
    last_other = contact->other;
}

static void test_contacts_queries_pairs(void) {
    AthenaCollisionWorld *w = new_world(0);
    AthenaBodyId a = add_rect(w, ATHENA_BODY_DYNAMIC, 0, 0, 10, 10);
    AthenaBodyId wall = add_rect(w, ATHENA_BODY_STATIC, 30, 0, 10, 10);
    AthenaBodyId s = add_rect(w, ATHENA_BODY_STATIC, 5, 5, 10, 10);
    AthenaBodyId c = add_circle(w, ATHENA_BODY_STATIC, 100, 100, 10);
    AthenaBodyId out[8];
    AthenaBodyPair pairs[8];
    uint32_t n;

    athena_collision_get(w, s)->sensor = true;
    athena_collision_get(w, s)->layer = 4;
    athena_collision_set_contact_func(w, on_contact, NULL);
    athena_collision_get(w, a)->vx = 3000;
    athena_collision_step(w, DT);
    CHECK(contacts_seen == 1 && last_other == wall, "contact callback: %d", contacts_seen);
    /* Pushing the same wall again is not a new contact; leaving and coming back is. */
    athena_collision_get(w, a)->vx = 3000;
    athena_collision_step(w, DT);
    CHECK(contacts_seen == 1, "pushing the same wall: %d", contacts_seen);
    athena_collision_step(w, DT);
    athena_collision_get(w, a)->vx = 3000;
    athena_collision_step(w, DT);
    CHECK(contacts_seen == 2, "pushing again after a step off it: %d", contacts_seen);

    /* Landing is one contact; standing is none; landing on a body is a new one. */
    {
        AthenaCollisionWorld *g = new_world(1000);
        AthenaBodyId floor = add_rect(g, ATHENA_BODY_STATIC, -100, 100, 400, 10);
        AthenaBodyId crate = add_rect(g, ATHENA_BODY_STATIC, 100, 90, 20, 10);
        AthenaBodyId p = add_rect(g, ATHENA_BODY_DYNAMIC, 0, 0, 10, 10);
        int i;

        athena_collision_set_contact_func(g, on_contact, NULL);
        contacts_seen = 0;
        for (i = 0; i < 120; i++)
            athena_collision_step(g, DT);
        CHECK(contacts_seen == 1 && last_other == floor, "landing once: %d", contacts_seen);
        athena_collision_set_position(g, p, 105, 50);
        for (i = 0; i < 60; i++)
            athena_collision_step(g, DT);
        CHECK(contacts_seen == 2 && last_other == crate, "landing on the crate: %d", contacts_seen);
        athena_collision_world_destroy(g);
    }

    n = athena_collision_query_rect(w, 0, 0, 100, 100, ~0u, out, 8);
    CHECK(n == 4, "query all %u", n);
    n = athena_collision_query_rect(w, 0, 0, 100, 100, 4, out, 8);
    CHECK(n == 1 && out[0] == s, "query by layer");
    n = athena_collision_query_circle(w, 100, 85, 5.5f, ~0u, out, 8);
    CHECK(n == 1 && out[0] == c, "query circle");
    n = athena_collision_query_circle(w, 100, 85, 4.5f, ~0u, out, 8);
    CHECK(n == 0, "query circle misses");
    CHECK(athena_collision_query_rect(w, 0, 0, 100, 100, ~0u, out, 2) == 4, "count beyond max");

    /* The sensor overlaps a after moving it back. */
    athena_collision_set_position(w, a, 0, 0);
    n = athena_collision_overlapping(w, a, out, 8);
    CHECK(n == 1 && out[0] == s, "overlapping finds the sensor: %u", n);
    athena_collision_get(w, a)->mask = 1;
    CHECK(athena_collision_overlapping(w, a, out, 8) == 0, "overlapping honors masks");

    /* Pairs: bullets (layer 2) against enemies (layer 8), and a layer against itself. */
    {
        AthenaCollisionWorld *p = new_world(0);
        AthenaBodyId e1 = add_rect(p, ATHENA_BODY_STATIC, 0, 0, 10, 10);
        AthenaBodyId e2 = add_rect(p, ATHENA_BODY_STATIC, 5, 0, 10, 10);
        AthenaBodyId bullet = add_circle(p, ATHENA_BODY_DYNAMIC, 12, 5, 2);

        athena_collision_get(p, e1)->layer = 8;
        athena_collision_get(p, e2)->layer = 8;
        athena_collision_get(p, bullet)->layer = 2;
        n = athena_collision_pairs(p, 2, 8, pairs, 8);
        CHECK(n == 1 && pairs[0].a == bullet && pairs[0].b == e2, "bullet pairs %u", n);
        n = athena_collision_pairs(p, 8, 8, pairs, 8);
        CHECK(n == 1 && ((pairs[0].a == e1 && pairs[0].b == e2) || (pairs[0].a == e2 && pairs[0].b == e1)),
            "self pairs once: %u", n);
        n = athena_collision_pairs(p, 10, 10, pairs, 8);
        CHECK(n == 2, "mixed layers reported once each: %u", n);
        athena_collision_world_destroy(p);
    }
    athena_collision_world_destroy(w);
}

static void test_raycast(void) {
    static const char *const level[] = {
        "..........",
        "....#.....",
        "......-...",
        "../.......",
        "##########",
    };
    AthenaCollisionWorld *w = new_world(0);
    AthenaBodyId box = add_rect(w, ATHENA_BODY_STATIC, 200, 10, 10, 10);
    AthenaBodyId round = add_circle(w, ATHENA_BODY_STATIC, 20, 200, 5);
    AthenaRaycastDesc desc = { ~0u, ATHENA_BODY_NONE, false };
    AthenaRayHit hit;

    set_grid(w, level, 5);
    CHECK(athena_collision_raycast(w, 0, 24, 160, 24, NULL, &hit) && hit.tile == 1 &&
        hit.column == 4 && hit.row == 1 && NEAR(hit.x, 64, 1e-3f) && hit.normal_x == -1,
        "solid tile: x %f tile %d", hit.x, hit.tile);
    CHECK(athena_collision_raycast(w, 8, 0, 8, 100, NULL, &hit) && hit.row == 4 &&
        NEAR(hit.y, 64, 1e-3f) && hit.normal_y == -1, "floor below: y %f", hit.y);
    /* One-way: from above only. */
    CHECK(athena_collision_raycast(w, 100, 0, 100, 100, NULL, &hit) && hit.tile == 2 &&
        NEAR(hit.y, 32, 1e-3f), "one-way from above: y %f tile %d", hit.y, hit.tile);
    CHECK(athena_collision_raycast(w, 100, 60, 100, 0, NULL, &hit) == false, "one-way from below misses");
    /* Slope: straight down onto the middle of the rising tile at column 2 (surface at 56). */
    CHECK(athena_collision_raycast(w, 40, 0, 40, 100, NULL, &hit) && hit.tile == 3 &&
        NEAR(hit.y, 56, 1e-2f) && hit.normal_y < 0 && hit.normal_x < 0, "slope: y %f n %f %f",
        hit.y, hit.normal_x, hit.normal_y);
    /* Bodies, their layers and ignore. */
    CHECK(athena_collision_raycast(w, 100, 15, 300, 15, NULL, &hit) && hit.body == box &&
        NEAR(hit.x, 200, 1e-3f) && NEAR(hit.fraction, 0.5f, 1e-5f), "body hit x %f", hit.x);
    desc.ignore = box;
    CHECK(!athena_collision_raycast(w, 100, 15, 300, 15, &desc, &hit), "ignored body");
    desc.ignore = ATHENA_BODY_NONE;
    desc.mask = 2;
    CHECK(!athena_collision_raycast(w, 0, 24, 160, 24, &desc, &hit), "grid layer not in mask");
    CHECK(athena_collision_raycast(w, 0, 200, 100, 200, NULL, &hit) && hit.body == round &&
        NEAR(hit.x, 15, 1e-3f), "circle x %f", hit.x);
    /* Starting inside a solid tile or body ignores it. */
    CHECK(athena_collision_raycast(w, 72, 24, 72, 100, NULL, &hit) && hit.tile == 1 && hit.row == 4, "leaves the start tile");
    CHECK(!athena_collision_raycast(w, 205, 15, 205, 25, NULL, &hit), "starts inside the body");
    /* From outside the grid. */
    CHECK(athena_collision_raycast(w, -50, 72, 50, 72, NULL, &hit) && NEAR(hit.x, 0, 1e-3f) &&
        hit.normal_x == -1, "enters the grid from the left: x %f", hit.x);
    CHECK(athena_collision_raycast(w, 500, 72, 100, 72, NULL, &hit) && NEAR(hit.x, 160, 1e-3f) &&
        hit.normal_x == 1, "enters the grid from the right: x %f", hit.x);
    CHECK(!athena_collision_raycast(w, -50, -50, 500, -40, NULL, &hit), "passes above everything");
    athena_collision_world_destroy(w);
}

static void test_shapes(void) {
    AthenaShape r1 = { ATHENA_SHAPE_RECT, 0, 0, 10, 10, 0 }, r2 = { ATHENA_SHAPE_RECT, 8, 3, 10, 10, 0 };
    AthenaShape r3 = { ATHENA_SHAPE_RECT, 10, 0, 5, 5, 0 };
    AthenaShape c1 = { ATHENA_SHAPE_CIRCLE, 0, 0, 0, 0, 5 }, c2 = { ATHENA_SHAPE_CIRCLE, 8, 0, 0, 0, 5 };
    AthenaShape c3 = { ATHENA_SHAPE_CIRCLE, 5, 5, 0, 0, 2 };
    float dx, dy, t, nx, ny;

    CHECK(athena_collision_shape_overlap(&r1, &r2) && !athena_collision_shape_overlap(&r1, &r3),
        "rects (touching is not overlapping)");
    CHECK(athena_collision_shape_resolve(&r1, &r2, &dx, &dy) && dx == -2 && dy == 0, "rect mtv %f %f", dx, dy);
    CHECK(athena_collision_shape_overlap(&c1, &c2) && athena_collision_shape_resolve(&c1, &c2, &dx, &dy) &&
        NEAR(dx, -2, 1e-5f) && dy == 0, "circle mtv %f %f", dx, dy);
    CHECK(athena_collision_shape_resolve(&c3, &r1, &dx, &dy) && NEAR(dy, -7, 1e-5f) && dx == 0,
        "circle inside rect %f %f", dx, dy);
    CHECK(athena_collision_shape_resolve(&r1, &c3, &dx, &dy) && NEAR(dy, 7, 1e-5f), "rect out of circle %f", dy);
    CHECK(!athena_collision_shape_resolve(&r1, &r3, &dx, &dy), "no mtv when touching");
    CHECK(athena_collision_segment_shape(-10, 5, 20, 5, &r1, &t, &nx, &ny) && NEAR(t, 1.0f / 3.0f, 1e-5f) &&
        nx == -1, "segment rect t %f", t);
    CHECK(athena_collision_segment_shape(0, -20, 0, 20, &c1, &t, &nx, &ny) && NEAR(t, 0.375f, 1e-5f) &&
        NEAR(ny, -1, 1e-5f), "segment circle t %f", t);
    CHECK(!athena_collision_segment_shape(5, 5, 50, 5, &r1, &t, &nx, &ny), "segment from inside");
}

/*
 * A grid with a slope (steepness 1) and a one-tile wall: whatever the speed
 * and frame rate, a walker stops at the wall. With one sweep per step, the
 * step-up allowance (|dx| * steepness) grew past a tile at 20 px per step.
 */
static void test_fast_walls(void) {
    static const char *const level[] = {
        "..........",
        "..........",
        "/.....#...",
        "##########",
    };
    static const float speeds[] = { 120, 600, 1200, 3000 };
    size_t s;
    int f;

    for (f = 0; f < 2; f++) {
        for (s = 0; s < sizeof(speeds) / sizeof(speeds[0]); s++) {
            float dt = f ? 2 * DT : DT;
            AthenaCollisionWorld *w = new_world(900);
            AthenaBodyId p = add_rect(w, ATHENA_BODY_DYNAMIC, 20, 34, 10, 14);
            AthenaBody *b;
            int i;

            set_grid(w, level, 4);
            for (i = 0; i < 60; i++) {
                athena_collision_get(w, p)->vx = speeds[s];
                athena_collision_step(w, dt);
            }
            b = athena_collision_get(w, p);
            CHECK(NEAR(b->x + b->w, 96, 1e-3f) && NEAR(b->y + b->h, 48, 1e-3f) && b->on_wall == 1,
                "speed %g dt %g: stops at the wall, right %f bottom %f", speeds[s], dt,
                b->x + b->w, b->y + b->h);
            athena_collision_world_destroy(w);
        }
    }
    /* A teleport through move() is cut into at most 64 sweeps, and still stops. */
    {
        AthenaCollisionWorld *w = new_world(0);
        AthenaBodyId p = add_rect(w, ATHENA_BODY_DYNAMIC, 20, 34, 10, 14);
        AthenaCollisionMove mv;

        set_grid(w, level, 4);
        athena_collision_move(w, p, 0, 1, NULL);
        athena_collision_move(w, p, 5000000, 0, &mv);
        CHECK(mv.hit_wall == 1 && NEAR(athena_collision_get(w, p)->x, 86, 1e-3f), "huge move stops");
        athena_collision_world_destroy(w);
    }
}

static void test_kinematic(void) {
    AthenaCollisionWorld *w = new_world(900);
    AthenaBodyId floor = add_rect(w, ATHENA_BODY_STATIC, -500, 100, 2000, 10);
    AthenaBodyId wall = add_rect(w, ATHENA_BODY_STATIC, -40, 0, 10, 100);
    AthenaBodyId door = add_rect(w, ATHENA_BODY_KINEMATIC, 60, 40, 20, 60);
    AthenaBodyId p = add_rect(w, ATHENA_BODY_DYNAMIC, 30, 86, 10, 14);
    AthenaBodyId lift = add_rect(w, ATHENA_BODY_KINEMATIC, 300, 80, 40, 8);
    AthenaBodyId rider = add_rect(w, ATHENA_BODY_DYNAMIC, 310, 60, 10, 20);
    AthenaBody *b, *d, *r, *l;
    float x0;
    int i, crushed = 0;

    (void)floor;
    for (i = 0; i < 10; i++)
        athena_collision_step(w, DT);
    b = athena_collision_get(w, p);
    CHECK(b->on_ground && athena_collision_get(w, rider)->ground == lift, "settled");

    /*
     * The door slides left: it pushes the player ahead of it until the wall,
     * where the player is crushed (and the game decides what happens).
     */
    athena_collision_get(w, door)->vx = -120;
    for (i = 0; i < 120; i++) {
        athena_collision_step(w, DT);
        b = athena_collision_get(w, p);
        d = athena_collision_get(w, door);
        if (b->crushed) {
            crushed++;
            break;
        }
        if (b->x + b->w > d->x + 0.01f)
            break;
    }
    CHECK(crushed == 1, "crushed before the door passed through (step %d)", i);
    CHECK(b->crushed && NEAR(b->x, -30, 1e-3f), "crushed against the wall: x %f", b->x);
    (void)wall;
    athena_collision_get(w, door)->vx = 0;
    athena_collision_step(w, DT);
    CHECK(!athena_collision_get(w, p)->crushed, "crushed clears on the next step");

    /* A lift moved by the application (a tween) carries its rider. */
    r = athena_collision_get(w, rider);
    x0 = r->x;
    for (i = 0; i < 30; i++) {
        athena_collision_translate(w, lift, 2, -1);
        athena_collision_step(w, DT);
    }
    r = athena_collision_get(w, rider);
    l = athena_collision_get(w, lift);
    CHECK(NEAR(r->x - x0, 60, 1e-2f) && NEAR(r->y + r->h, l->y, 1e-2f) && r->on_ground && r->ground == lift,
        "translate carries: moved %f, bottom %f top %f", r->x - x0, r->y + r->h, l->y);
    /* So does move() on a kinematic body, through everything. */
    {
        AthenaCollisionMove mv;

        athena_collision_move(w, lift, -20, 0, &mv);
        r = athena_collision_get(w, rider);
        CHECK(mv.dx == -20 && NEAR(r->x - x0, 40, 1e-2f), "move carries: %f", r->x - x0);
    }
    /* A lift rising into a body from below lifts it; one-way lifts do not push sideways. */
    {
        AthenaBodyId box = add_rect(w, ATHENA_BODY_DYNAMIC, 500, 40, 10, 10);
        AthenaBodyId ledge = add_rect(w, ATHENA_BODY_KINEMATIC, 495, 60, 20, 5);
        AthenaBody *bx;

        athena_collision_get(w, box)->gravity_scale = 0;
        athena_collision_translate(w, ledge, 0, -15);
        bx = athena_collision_get(w, box);
        CHECK(NEAR(bx->y + bx->h, 45, 1e-3f), "pushed up: bottom %f", bx->y + bx->h);
        athena_collision_get(w, ledge)->one_way = true;
        athena_collision_set_position(w, ledge, 470, 40);
        athena_collision_translate(w, ledge, 20, 0);
        CHECK(NEAR(athena_collision_get(w, box)->x, 500, 1e-3f), "one-way does not push sideways");
    }
    athena_collision_world_destroy(w);

    /*
     * A lift rising into a ceiling with its rider: the rider is crushed
     * (it used to be left inside the lift, and fell through it).
     */
    {
        AthenaCollisionWorld *e = new_world(900);
        AthenaBodyId up, man;
        AthenaBody *m;

        add_rect(e, ATHENA_BODY_STATIC, 0, 0, 100, 10);
        up = add_rect(e, ATHENA_BODY_KINEMATIC, 20, 60, 40, 8);
        man = add_rect(e, ATHENA_BODY_DYNAMIC, 30, 40, 10, 20);
        for (i = 0; i < 10; i++)
            athena_collision_step(e, DT);
        CHECK(athena_collision_get(e, man)->ground == up, "rides the lift");
        athena_collision_get(e, up)->vy = -120;
        crushed = 0;
        for (i = 0; i < 40 && !crushed; i++) {
            athena_collision_step(e, DT);
            crushed = athena_collision_get(e, man)->crushed;
        }
        m = athena_collision_get(e, man);
        /* Crushed at the ceiling (y 10); gravity pulls it down in the same step. */
        CHECK(crushed && NEAR(m->y, 10, 0.5f), "crushed against the ceiling: y %f (step %d)", m->y, i);
        athena_collision_world_destroy(e);
    }
}

/* Random segments: the ray through the hash cells finds what a scan of every body finds. */
static void test_raycast_against_scan(void) {
    AthenaCollisionWorld *w = new_world(0);
    AthenaRaycastDesc desc = { ~0u, ATHENA_BODY_NONE, true };
    uint32_t seed = 777, i, q, mismatches = 0;

    for (i = 0; i < 800; i++) {
        float size = rnd(&seed) < 0.03f ? 300 + rnd(&seed) * 500 : 2 + rnd(&seed) * 30;
        float x = rnd(&seed) * 3000 - 1500, y = rnd(&seed) * 3000 - 1500;

        if (rnd(&seed) < 0.3f)
            add_circle(w, ATHENA_BODY_STATIC, x, y, size / 2);
        else
            add_rect(w, ATHENA_BODY_STATIC, x, y, size, size);
    }
    for (q = 0; q < 400; q++) {
        float x1 = rnd(&seed) * 3200 - 1600, y1 = rnd(&seed) * 3200 - 1600;
        float x2 = rnd(&seed) * 3200 - 1600, y2 = rnd(&seed) * 3200 - 1600;
        float best = 2.0f;
        AthenaRayHit hit;
        bool found;

        /* Some along a row, a column, a corner diagonal of the cells, or short. */
        if (q % 7 == 0)
            y2 = y1;
        else if (q % 7 == 1)
            x2 = x1;
        else if (q % 7 == 2)
            x1 = y1 = 0, x2 = y2 = 64 * (float)(q % 20);
        else if (q % 7 == 3)
            x2 = x1 + 50, y2 = y1 - 30;
        for (i = 0; i < athena_collision_body_capacity(w); i++) {
            AthenaBody *b = athena_collision_slot(w, i);
            AthenaShape s;
            float t, nx, ny;

            if (!b)
                continue;
            s = (AthenaShape){ b->shape, b->x, b->y, b->w, b->h, b->r };
            if (athena_collision_segment_shape(x1, y1, x2, y2, &s, &t, &nx, &ny) && t < best)
                best = t;
        }
        found = athena_collision_raycast(w, x1, y1, x2, y2, &desc, &hit);
        if (found != (best <= 1.0f) || (found && hit.fraction != best))
            mismatches++;
    }
    CHECK(mismatches == 0, "%u raycasts differ from a scan", mismatches);
    athena_collision_world_destroy(w);
}

static int enters, exits;
static AthenaBodyId last_sensor, last_entered;
static void on_sensor(void *opaque, AthenaBodyId sensor, AthenaBodyId other, bool entered) {
    (void)opaque;
    if (entered) {
        enters++;
        last_entered = other;
    } else {
        exits++;
    }
    last_sensor = sensor;
}

static void test_sensors(void) {
    AthenaCollisionWorld *w = new_world(0);
    AthenaBodyId zone = add_rect(w, ATHENA_BODY_STATIC, 100, 0, 50, 50);
    AthenaBodyId wall = add_rect(w, ATHENA_BODY_STATIC, 120, 35, 10, 10);
    AthenaBodyId p = add_rect(w, ATHENA_BODY_DYNAMIC, 0, 10, 10, 10);
    AthenaBodyId bullet = add_circle(w, ATHENA_BODY_DYNAMIC, 0, 40, 2);
    int i;

    (void)wall;
    athena_collision_get(w, zone)->sensor = true;
    athena_collision_get(w, bullet)->sensor = true;
    athena_collision_get(w, bullet)->mask = 0;   /* in no layer's mask: never paired */
    athena_collision_step(w, DT);
    CHECK(enters == 0, "no events without a function");
    athena_collision_set_sensor_func(w, on_sensor, NULL);
    athena_collision_step(w, DT);
    CHECK(enters == 0 && exits == 0, "static wall inside a static sensor is not a pair: %d", enters);
    athena_collision_get(w, p)->vx = 600;   /* 10 px per step */
    for (i = 0; i < 12; i++)
        athena_collision_step(w, DT);
    CHECK(enters == 1 && last_sensor == zone && last_entered == p, "enters once: %d", enters);
    for (i = 0; i < 12; i++)
        athena_collision_step(w, DT);
    CHECK(exits == 1 && enters == 1, "exits once: %d %d", enters, exits);
    /* Removed while inside: no exit. */
    athena_collision_set_position(w, p, 110, 30);
    athena_collision_get(w, p)->vx = 0;
    athena_collision_step(w, DT);
    CHECK(enters == 2, "enters again");
    athena_collision_remove(w, p);
    athena_collision_step(w, DT);
    CHECK(exits == 1, "no exit for a removed body: %d", exits);
    /* Two sensors are one pair. */
    athena_collision_get(w, bullet)->mask = ~0u;
    athena_collision_set_position(w, bullet, 110, 40);
    athena_collision_step(w, DT);
    CHECK(enters == 3, "sensor pair once: %d", enters);
    /* Unset: tracking stops, and restarts from nothing. */
    athena_collision_set_sensor_func(w, NULL, NULL);
    athena_collision_step(w, DT);
    athena_collision_set_sensor_func(w, on_sensor, NULL);
    athena_collision_step(w, DT);
    CHECK(enters == 4, "restarts from no overlap: %d", enters);
    athena_collision_world_destroy(w);
}

/*
 * The sensor pairs, found from the sensors or from the moving bodies
 * (whichever are fewer), are the ones a scan of every pair finds.
 */
static void test_sensors_against_scan(void) {
    int config;

    for (config = 0; config < 2; config++) {
        /* Many sensors and few movers, then the other way around. */
        float sensor_rate = config ? 0.1f : 0.6f, mover_rate = config ? 0.6f : 0.1f;
        AthenaCollisionWorld *w = new_world(0);
        uint32_t seed = 99 + config, i, j, expected = 0;

        for (i = 0; i < 400; i++) {
            AthenaBodyType type = rnd(&seed) < mover_rate ? ATHENA_BODY_DYNAMIC : ATHENA_BODY_STATIC;
            float x = rnd(&seed) * 600, y = rnd(&seed) * 600, size = 4 + rnd(&seed) * 30;
            AthenaBodyId id = rnd(&seed) < 0.3f ? add_circle(w, type, x, y, size / 2) :
                add_rect(w, type, x, y, size, size);
            AthenaBody *b = athena_collision_get(w, id);

            b->sensor = rnd(&seed) < sensor_rate;
            b->layer = 1u << (uint32_t)(rnd(&seed) * 3);
            b->mask = rnd(&seed) < 0.2f ? 1u : ~0u;
        }
        for (i = 0; i < athena_collision_body_capacity(w); i++) {
            for (j = i + 1; j < athena_collision_body_capacity(w); j++) {
                AthenaBody *a = athena_collision_slot(w, i), *b = athena_collision_slot(w, j);
                AthenaShape sa = { a->shape, a->x, a->y, a->w, a->h, a->r };
                AthenaShape sb = { b->shape, b->x, b->y, b->w, b->h, b->r };

                if ((a->sensor || b->sensor) &&
                    !(a->type == ATHENA_BODY_STATIC && b->type == ATHENA_BODY_STATIC) &&
                    (a->mask & b->layer) && (b->mask & a->layer) &&
                    athena_collision_shape_overlap(&sa, &sb))
                    expected++;
            }
        }
        enters = exits = 0;
        athena_collision_set_sensor_func(w, on_sensor, NULL);
        athena_collision_step(w, DT);
        CHECK(enters == (int)expected && expected > 0, "config %d: %d enters, %u pairs", config,
            enters, expected);
        athena_collision_step(w, DT);
        CHECK(enters == (int)expected && exits == 0, "config %d: no events while nothing moves",
            config);
        athena_collision_world_destroy(w);
    }
}

static void test_point_solid(void) {
    static const char *const level[] = { "....", "#-/.", "####" };
    AthenaCollisionWorld *w = new_world(0);
    AthenaBodyId s = add_rect(w, ATHENA_BODY_STATIC, 100, 0, 10, 10);

    set_grid(w, level, 3);
    CHECK(athena_collision_point_solid(w, 8, 24, ~0u), "solid tile");
    CHECK(!athena_collision_point_solid(w, 24, 20, ~0u), "one-way is not solid");
    CHECK(athena_collision_point_solid(w, 44, 26, ~0u) && !athena_collision_point_solid(w, 36, 26, ~0u),
        "slope below and above its surface");
    CHECK(!athena_collision_point_solid(w, 8, 24, 2), "grid layer not in the mask");
    CHECK(athena_collision_point_solid(w, 105, 5, ~0u), "solid body");
    athena_collision_get(w, s)->sensor = true;
    CHECK(!athena_collision_point_solid(w, 105, 5, ~0u), "sensors are not solid");
    athena_collision_world_destroy(w);
}

int main(void) {
    test_bodies_and_ids();
    test_hash_against_scan();
    test_move_bodies();
    test_circles();
    test_tiles_and_platformer();
    test_slopes();
    test_slope_walls_and_steps();
    test_physics();
    test_contacts_queries_pairs();
    test_raycast();
    test_raycast_against_scan();
    test_shapes();
    test_fast_walls();
    test_kinematic();
    test_sensors();
    test_sensors_against_scan();
    test_point_solid();
    if (failures) {
        printf("collision_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("collision_test: ok\n");
    return 0;
}
