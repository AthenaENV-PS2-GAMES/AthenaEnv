/*
 * A small platformer in C, without QuickJS: the Collision C API
 * (athena/collision.h) with a tile grid (floor, walls, a one-way platform
 * and a hill of slopes), a moving platform and a player stepped by the
 * world. The debug overlay draws every collision shape.
 *
 *   LEFT / RIGHT   walk         CROSS   jump
 *   DOWN           drop through one-way platforms
 *
 *   node tools/modules.js configure --modules=screen,draw,gamepad,loop,collision
 *   make RUNTIME=native APP_SRCS=samples/native/collision/main.c
 */
#include <libpad.h>

#include <athena.h>
#include <athena/collision.h>
#include <athena/color.h>
#include <athena/draw.h>
#include <athena/gamepad.h>
#include <athena/loop.h>
#include <athena/screen.h>

#define TILE 32.0f
#define COLUMNS 20
#define ROWS 14
#define STEP (1.0f / 60.0f)

enum { EMPTY, SOLID, ONE_WAY, UP, DOWN };

static const char *const LEVEL[ROWS] = {
	"#..................#",
	"#..................#",
	"#..................#",
	"#..................#",
	"#..................#",
	"#.......----.......#",
	"#..................#",
	"#..................#",
	"#..................#",
	"#.............../#\\#",
	"#............../###.",
	"#......./#\\...#####.",
	"#....../###\\..#####.",
	"####################",
};

static const AthenaTileKind KINDS[] = {
	[EMPTY] = { ATHENA_TILE_EMPTY, 0.0f, 0.0f },
	[SOLID] = { ATHENA_TILE_SOLID, 0.0f, 0.0f },
	[ONE_WAY] = { ATHENA_TILE_ONE_WAY, 0.0f, 0.0f },
	[UP] = { ATHENA_TILE_SLOPE, 0.0f, 1.0f },
	[DOWN] = { ATHENA_TILE_SLOPE, 1.0f, 0.0f },
};

static Color color(int r, int g, int b)
{
	return athena_color_new(r, g, b, ATHENA_COLOR_DEFAULT_ALPHA);
}

static void load_level(AthenaCollisionWorld *world)
{
	static uint16_t tiles[COLUMNS * ROWS];
	AthenaCollisionGridDesc grid = {
		.columns = COLUMNS, .rows = ROWS, .tile_width = TILE, .tile_height = TILE,
		.tiles = tiles, .kinds = KINDS, .kind_count = 5, .layer = 1,
	};

	for (int r = 0; r < ROWS; r++) {
		for (int c = 0; c < COLUMNS; c++) {
			char ch = LEVEL[r][c];

			tiles[r * COLUMNS + c] = ch == '#' ? SOLID : ch == '-' ? ONE_WAY :
				ch == '/' ? UP : ch == '\\' ? DOWN : EMPTY;
		}
	}
	athena_collision_set_grid(world, &grid);
}

int athena_main(int argc, char **argv) {
	AthenaCollisionWorldDesc wdesc;
	AthenaCollisionBodyDesc bdesc;
	AthenaCollisionDebug debug = { .bodies = true, .tiles = true };
	AthenaCollisionWorld *world;
	AthenaBodyId player, platform;
	AthenaLoopClock clock;

	graphics_service_init();
	if (athena_gamepad_core_init() != ATHENA_GAMEPAD_OK)
		dbgprintf("[collision] gamepad unavailable\n");

	athena_collision_world_desc_init(&wdesc);
	wdesc.gravity_y = 1400.0f;
	world = athena_collision_world_create(&wdesc);
	if (!world)
		return 1;
	load_level(world);

	athena_collision_body_desc_init(&bdesc);
	bdesc.type = ATHENA_BODY_DYNAMIC;
	bdesc.x = 64.0f;
	bdesc.y = 300.0f;
	bdesc.w = 18.0f;
	bdesc.h = 28.0f;
	bdesc.max_speed_y = 900.0f;
	athena_collision_add(world, &bdesc, &player);

	athena_collision_body_desc_init(&bdesc);
	bdesc.type = ATHENA_BODY_KINEMATIC;
	bdesc.x = 80.0f;
	bdesc.y = 250.0f;
	bdesc.w = 96.0f;
	bdesc.h = 12.0f;
	bdesc.vx = 70.0f;
	athena_collision_add(world, &bdesc, &platform);

	athena_loop_clock_reset(&clock, ATHENA_LOOP_DEFAULT_MAX_DELTA);
	for (;;) {
		athena_loop_clock_tick(&clock);
		athena_gamepad_core_update();
		for (int n = athena_loop_clock_steps(&clock, STEP, ATHENA_LOOP_DEFAULT_MAX_STEPS); n > 0; n--) {
			AthenaBody *p = athena_collision_get(world, player);
			AthenaBody *moving = athena_collision_get(world, platform);

			p->vx = 0.0f;
			if (athena_gamepad_core_pressed(0, PAD_LEFT))
				p->vx = -180.0f;
			if (athena_gamepad_core_pressed(0, PAD_RIGHT))
				p->vx = 180.0f;
			if (athena_gamepad_core_pressed(0, PAD_CROSS) && p->on_ground)
				p->vy = -520.0f;
			p->drop_through = athena_gamepad_core_pressed(0, PAD_DOWN);
			/* The platform turns around at the ends of its track. */
			if ((moving->x > 400.0f && moving->vx > 0.0f) || (moving->x < 64.0f && moving->vx < 0.0f))
				moving->vx = -moving->vx;
			athena_collision_step(world, STEP);
		}

		clearScreen(color(16, 16, 32));
		{
			const AthenaBody *p = athena_collision_get(world, player);

			draw_sprite(p->x, p->y, (int)p->w, (int)p->h, color(255, 160, 0));
		}
		athena_collision_draw_debug(world, &debug);
		flipScreen();
	}
	return 0;
}
