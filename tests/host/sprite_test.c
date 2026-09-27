/* Host test of sheets, clips, playback, placement and batches (sprite/native/sprite.c). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <athena/sprite.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define NEAR(a, b, eps) (fabsf((a) - (b)) <= (eps))

/* Events recorded by record(): "F<position>", "L", "E", space separated. */
static char events[1024];

static void record(void *opaque, AthenaSpriteEventType type, uint32_t position, uint32_t frame)
{
	char item[16];

	(void)opaque;
	(void)frame;
	if (type == ATHENA_SPRITE_EVENT_FRAME)
		snprintf(item, sizeof(item), "F%u ", (unsigned)position);
	else
		snprintf(item, sizeof(item), "%s ", type == ATHENA_SPRITE_EVENT_LOOP ? "L" : "E");
	if (strlen(events) + strlen(item) < sizeof(events))
		strcat(events, item);
}

static void grid_sheet(AthenaSpriteSheet *sheet)
{
	AthenaSpriteGrid grid = { .frame_w = 16, .frame_h = 16, .texture_w = 64, .texture_h = 32 };

	athena_sprite_sheet_init(sheet);
	CHECK(athena_sprite_sheet_add_grid(sheet, &grid) == 8, "grid frames");
}

static int clip(AthenaSpriteSheet *sheet, const char *name, const char *frames, float duration,
	AthenaSpriteMode mode, uint32_t loops)
{
	uint16_t list[64];
	int count = athena_sprite_parse_frames(frames, sheet->frame_count, list, 64);

	return athena_sprite_sheet_add_clip(sheet, name, list, (uint32_t)count, &duration, 1, mode, loops);
}

static void test_grid(void)
{
	AthenaSpriteSheet sheet;
	AthenaSpriteGrid grid = { .frame_w = 32, .frame_h = 32, .margin = 1, .spacing = 2,
		.texture_w = 128, .texture_h = 64 };

	athena_sprite_sheet_init(&sheet);
	/* (128 - 2 + 2) / 34 = 3 columns, (64 - 2 + 2) / 34 = 1 row. */
	CHECK(athena_sprite_sheet_add_grid(&sheet, &grid) == 3, "fit %u", sheet.frame_count);
	CHECK(sheet.frames[2].x == 1 + 2 * 34 && sheet.frames[2].y == 1 && sheet.frames[2].w == 32,
		"cell 2 at %f %f", sheet.frames[2].x, sheet.frames[2].y);
	CHECK(sheet.frames[0].source_w == 32 && sheet.frames[0].offset_x == 0, "untrimmed");

	grid = (AthenaSpriteGrid){ .frame_w = 10, .frame_h = 10, .columns = 4, .rows = 4, .first = 5, .count = 3 };
	CHECK(athena_sprite_sheet_add_grid(&sheet, &grid) == 3, "first/count");
	CHECK(sheet.frames[3].x == 10 && sheet.frames[3].y == 10, "cell 5 at %f %f",
		sheet.frames[3].x, sheet.frames[3].y);
	grid.count = 12;
	CHECK(athena_sprite_sheet_add_grid(&sheet, &grid) == ATHENA_SPRITE_ERANGE, "count past the grid");
	grid = (AthenaSpriteGrid){ .frame_w = 10, .frame_h = 10 };
	CHECK(athena_sprite_sheet_add_grid(&sheet, &grid) == ATHENA_SPRITE_EINVAL, "no size to fit");
	grid = (AthenaSpriteGrid){ .frame_w = 100, .frame_h = 10, .texture_w = 64, .texture_h = 64 };
	CHECK(athena_sprite_sheet_add_grid(&sheet, &grid) == ATHENA_SPRITE_EINVAL, "frame wider than the texture");
	CHECK(sheet.frame_count == 6, "failed grids add nothing");
	athena_sprite_sheet_clear(&sheet);
	CHECK(sheet.frame_count == 0 && sheet.frames == NULL, "clear");
}

static void test_frames(void)
{
	AthenaSpriteSheet sheet;
	AthenaSpriteFrame frame = { .x = 4, .y = 8, .w = 10, .h = 12, .offset_x = 3, .offset_y = 2,
		.source_w = 16, .source_h = 16, .duration = 0.1f };
	AthenaSpriteFrame bad;

	athena_sprite_sheet_init(&sheet);
	CHECK(athena_sprite_sheet_add_frame(&sheet, &frame, NULL) == 0, "unnamed");
	CHECK(athena_sprite_sheet_add_frame(&sheet, &frame, "walk_0.png") == 1, "named");
	CHECK(athena_sprite_sheet_add_frame(&sheet, &frame, "walk_0.png") == ATHENA_SPRITE_EEXIST, "duplicate name");
	CHECK(athena_sprite_sheet_add_frame(&sheet, &frame, "walk_1.png") == 2, "named 2");
	CHECK(athena_sprite_sheet_find_frame(&sheet, "walk_1.png") == 2 &&
		athena_sprite_sheet_find_frame(&sheet, "nope") == -1, "find frame");
	CHECK(sheet.frame_names[0] == NULL, "frames before the first name stay unnamed");

	bad = frame; bad.w = 0;
	CHECK(athena_sprite_sheet_add_frame(&sheet, &bad, NULL) == ATHENA_SPRITE_EINVAL, "zero width");
	bad = frame; bad.offset_x = 8;
	CHECK(athena_sprite_sheet_add_frame(&sheet, &bad, NULL) == ATHENA_SPRITE_EINVAL, "trim past the source");
	bad = frame; bad.x = NAN;
	CHECK(athena_sprite_sheet_add_frame(&sheet, &bad, NULL) == ATHENA_SPRITE_EINVAL, "NaN");
	bad = frame; bad.source_w = bad.source_h = 0; bad.offset_x = 1;
	CHECK(athena_sprite_frame_check(&bad) == 0 && bad.source_w == 11 && bad.source_h == 14,
		"source size from the rectangle %f %f", bad.source_w, bad.source_h);

	/* Many frames: the name table grows with them. */
	for (int i = 0; i < 100; i++) {
		char name[16];
		snprintf(name, sizeof(name), "f%d", i);
		CHECK(athena_sprite_sheet_add_frame(&sheet, &frame, name) == 3 + i, "grow %d", i);
	}
	CHECK(athena_sprite_sheet_find_frame(&sheet, "f99") == 102, "find after growing");
	athena_sprite_sheet_clear(&sheet);
}

static void test_parse(void)
{
	uint16_t out[16];

	CHECK(athena_sprite_parse_frames("4-7", 10, out, 16) == 4 && out[0] == 4 && out[3] == 7, "range");
	CHECK(athena_sprite_parse_frames("0, 2 ,5-3", 10, out, 16) == 5 && out[1] == 2 &&
		out[2] == 5 && out[4] == 3, "list and descending range");
	CHECK(athena_sprite_parse_frames("3", 10, out, 16) == 1 && out[0] == 3, "single");
	CHECK(athena_sprite_parse_frames("0-9", 10, out, 4) == 10 && out[3] == 3, "count past max");
	CHECK(athena_sprite_parse_frames("0-10", 10, out, 16) == ATHENA_SPRITE_ERANGE, "out of range");
	CHECK(athena_sprite_parse_frames("", 10, out, 16) == ATHENA_SPRITE_EINVAL, "empty");
	CHECK(athena_sprite_parse_frames("1,", 10, out, 16) == ATHENA_SPRITE_EINVAL, "trailing comma");
	CHECK(athena_sprite_parse_frames("1-", 10, out, 16) == ATHENA_SPRITE_EINVAL, "open range");
	CHECK(athena_sprite_parse_frames("a", 10, out, 16) == ATHENA_SPRITE_EINVAL, "letters");
	CHECK(athena_sprite_parse_frames("-1", 10, out, 16) == ATHENA_SPRITE_EINVAL, "negative");
	CHECK(athena_sprite_parse_frames("99999999", 10, out, 16) == ATHENA_SPRITE_EINVAL, "overflow");
}

static void test_clips(void)
{
	AthenaSpriteSheet sheet;
	uint16_t frames[3] = { 0, 1, 2 };
	float durations[3] = { 0.1f, 0.2f, 0.3f };
	int index;

	grid_sheet(&sheet);
	index = athena_sprite_sheet_add_clip(&sheet, "a", frames, 3, durations, 3, ATHENA_SPRITE_LOOP, 0);
	CHECK(index == 0 && NEAR(sheet.clips[0].length, 0.6f, 1e-6f), "clip length");
	CHECK(athena_sprite_sheet_add_clip(&sheet, "b", frames, 3, durations, 2, ATHENA_SPRITE_LOOP, 0) ==
		ATHENA_SPRITE_EINVAL, "duration count");
	CHECK(athena_sprite_sheet_add_clip(&sheet, "b", frames, 3, NULL, 0, ATHENA_SPRITE_LOOP, 0) ==
		ATHENA_SPRITE_EINVAL, "frames without durations");
	frames[2] = 8;
	CHECK(athena_sprite_sheet_add_clip(&sheet, "b", frames, 3, durations, 1, ATHENA_SPRITE_LOOP, 0) ==
		ATHENA_SPRITE_ERANGE, "frame out of range");
	frames[2] = 2;
	durations[1] = 0;
	CHECK(athena_sprite_sheet_add_clip(&sheet, "b", frames, 3, durations, 3, ATHENA_SPRITE_LOOP, 0) ==
		ATHENA_SPRITE_EINVAL, "zero duration");
	durations[1] = 1e-6f;
	index = athena_sprite_sheet_add_clip(&sheet, "b", frames, 3, durations, 3, ATHENA_SPRITE_LOOP, 0);
	CHECK(index == 1 && sheet.clips[1].durations[1] == ATHENA_SPRITE_MIN_DURATION, "tiny duration clamped");
	/* Replacing keeps the index. */
	CHECK(athena_sprite_sheet_add_clip(&sheet, "a", frames, 1, durations, 1, ATHENA_SPRITE_PINGPONG, 2) == 0 &&
		sheet.clip_count == 2 && sheet.clips[0].count == 1 && sheet.clips[0].loops == 2, "replace");
	CHECK(athena_sprite_sheet_find_clip(&sheet, "b") == 1 && athena_sprite_sheet_find_clip(&sheet, "c") == -1,
		"find clip");
	{
		char long_name[ATHENA_SPRITE_NAME_MAX + 1];
		memset(long_name, 'x', sizeof(long_name) - 1);
		long_name[sizeof(long_name) - 1] = 0;
		CHECK(athena_sprite_sheet_add_clip(&sheet, long_name, frames, 1, durations, 1, ATHENA_SPRITE_LOOP, 0) ==
			ATHENA_SPRITE_EINVAL, "long name");
	}
	/* Frame durations from the sheet. */
	for (uint32_t i = 0; i < sheet.frame_count; i++)
		sheet.frames[i].duration = 0.05f * (float)(i + 1);
	index = athena_sprite_sheet_add_clip(&sheet, "own", frames, 3, NULL, 0, ATHENA_SPRITE_LOOP, 0);
	CHECK(index == 2 && NEAR(sheet.clips[2].durations[2], 0.15f, 1e-6f), "frame durations");
	athena_sprite_sheet_clear(&sheet);
}

static void test_loop(void)
{
	AthenaSpriteSheet sheet;
	AthenaSpriteAnim anim;
	int c;

	grid_sheet(&sheet);
	c = clip(&sheet, "run", "4-7", 0.1f, ATHENA_SPRITE_LOOP, 0);
	athena_sprite_anim_init(&anim);
	CHECK(athena_sprite_anim_frame(&anim, &sheet) == 0 && !anim.playing, "init shows frame 0");
	CHECK(!athena_sprite_anim_play(&anim, &sheet, 9), "unknown clip");
	CHECK(athena_sprite_anim_play(&anim, &sheet, c), "play");
	CHECK(athena_sprite_anim_frame(&anim, &sheet) == 4, "first frame");

	events[0] = 0;
	/* The first advance reports the first frame. */
	CHECK(!athena_sprite_anim_advance(&anim, &sheet, 0.05f, record, NULL), "no change yet");
	CHECK(strcmp(events, "F0 ") == 0, "first frame event: %s", events);
	events[0] = 0;
	CHECK(athena_sprite_anim_advance(&anim, &sheet, 0.06f, record, NULL), "changed");
	CHECK(anim.position == 1 && NEAR(anim.time, 0.01f, 1e-5f), "position 1, %f", anim.time);
	/* 0.3 more: 2, 3, then loop to 0. */
	events[0] = 0;
	athena_sprite_anim_advance(&anim, &sheet, 0.3f, record, NULL);
	CHECK(strcmp(events, "F2 F3 L F0 ") == 0, "loop events: %s", events);
	CHECK(anim.cycles == 1 && anim.position == 0, "cycle count %u", anim.cycles);

	/* A long stall skips whole cycles with one LOOP. */
	events[0] = 0;
	athena_sprite_anim_advance(&anim, &sheet, 100.0f + 0.15f, record, NULL);
	CHECK(anim.cycles == 251, "skipped cycles %u", anim.cycles);
	CHECK(anim.position == 1 && anim.time < 0.1f, "state after the stall %u %f", anim.position, anim.time);
	CHECK(strcmp(events, "L F1 F2 F3 L F0 F1 ") == 0, "one LOOP for the skipped cycles: %s", events);

	/* Speed, pause, invalid steps. */
	anim.speed = 2.0f;
	anim.time = 0;
	athena_sprite_anim_advance(&anim, &sheet, 0.05f, NULL, NULL);
	CHECK(anim.position == 2, "speed 2");
	anim.speed = 0;
	CHECK(!athena_sprite_anim_advance(&anim, &sheet, 1.0f, NULL, NULL) && anim.position == 2, "speed 0");
	anim.speed = 1;
	CHECK(!athena_sprite_anim_advance(&anim, &sheet, -1.0f, NULL, NULL), "negative step");
	CHECK(!athena_sprite_anim_advance(&anim, &sheet, NAN, NULL, NULL), "NaN step");
	CHECK(!athena_sprite_anim_advance(&anim, &sheet, INFINITY, NULL, NULL), "infinite step");
	anim.playing = false;
	CHECK(!athena_sprite_anim_advance(&anim, &sheet, 1.0f, NULL, NULL) && anim.position == 2, "paused");

	/* Seek. */
	anim.playing = true;
	CHECK(athena_sprite_anim_seek(&anim, &sheet, 3) && anim.time == 0, "seek");
	CHECK(!athena_sprite_anim_seek(&anim, &sheet, 4), "seek past the clip");
	events[0] = 0;
	athena_sprite_anim_advance(&anim, &sheet, 0.01f, record, NULL);
	CHECK(strcmp(events, "F3 ") == 0, "seek reports its frame: %s", events);

	/* Show a still frame. */
	CHECK(athena_sprite_anim_show(&anim, &sheet, 6) && athena_sprite_anim_frame(&anim, &sheet) == 6 &&
		!anim.playing && anim.clip == -1, "show");
	CHECK(!athena_sprite_anim_show(&anim, &sheet, 8), "show out of range");
	CHECK(athena_sprite_anim_progress(&anim, &sheet) == 0, "progress without a clip");
	athena_sprite_sheet_clear(&sheet);
}

static void test_once(void)
{
	AthenaSpriteSheet sheet;
	AthenaSpriteAnim anim;
	int c;

	grid_sheet(&sheet);
	c = clip(&sheet, "die", "0-2", 0.1f, ATHENA_SPRITE_LOOP, 1);
	athena_sprite_anim_init(&anim);
	athena_sprite_anim_play(&anim, &sheet, c);
	events[0] = 0;
	athena_sprite_anim_advance(&anim, &sheet, 0.15f, record, NULL);
	CHECK(NEAR(athena_sprite_anim_progress(&anim, &sheet), 0.5f, 1e-4f), "progress %f",
		athena_sprite_anim_progress(&anim, &sheet));
	athena_sprite_anim_advance(&anim, &sheet, 5.0f, record, NULL);
	CHECK(strcmp(events, "F0 F1 F2 E ") == 0, "once events: %s", events);
	CHECK(anim.finished && !anim.playing && anim.position == 2, "ends on the last frame");
	CHECK(athena_sprite_anim_progress(&anim, &sheet) == 1.0f, "progress 1");
	/* The end fires once: the sample's bug. */
	events[0] = 0;
	for (int i = 0; i < 10; i++)
		athena_sprite_anim_advance(&anim, &sheet, 0.1f, record, NULL);
	CHECK(events[0] == 0, "no more events: %s", events);
	/* Seeking a finished clip plays it again. */
	CHECK(athena_sprite_anim_seek(&anim, &sheet, 1) && anim.playing && !anim.finished, "seek restarts");
	events[0] = 0;
	athena_sprite_anim_advance(&anim, &sheet, 0.25f, record, NULL);
	CHECK(strcmp(events, "F1 F2 E ") == 0, "after seek: %s", events);

	/* Three cycles. */
	c = clip(&sheet, "three", "0-1", 0.1f, ATHENA_SPRITE_LOOP, 3);
	athena_sprite_anim_play(&anim, &sheet, c);
	events[0] = 0;
	athena_sprite_anim_advance(&anim, &sheet, 10.0f, record, NULL);
	CHECK(strcmp(events, "F0 F1 L F0 F1 L F0 F1 E ") == 0, "three cycles: %s", events);
	athena_sprite_sheet_clear(&sheet);
}

static void test_pingpong(void)
{
	AthenaSpriteSheet sheet;
	AthenaSpriteAnim anim;
	int c;

	grid_sheet(&sheet);
	c = clip(&sheet, "pp", "0-3", 0.1f, ATHENA_SPRITE_PINGPONG, 0);
	athena_sprite_anim_init(&anim);
	athena_sprite_anim_play(&anim, &sheet, c);
	events[0] = 0;
	/* 0 1 2 3 2 1 | 0 1: ends not repeated. */
	athena_sprite_anim_advance(&anim, &sheet, 0.75f, record, NULL);
	CHECK(strcmp(events, "F0 F1 F2 F3 F2 F1 L F0 F1 ") == 0, "pingpong: %s", events);
	CHECK(anim.cycles == 1 && anim.direction == 1, "cycle");
	/* Progress on the way back. */
	athena_sprite_anim_seek(&anim, &sheet, 3);
	athena_sprite_anim_advance(&anim, &sheet, 0.15f, NULL, NULL);
	CHECK(anim.position == 2 && anim.direction == -1, "turned %u %d", anim.position, anim.direction);
	CHECK(NEAR(athena_sprite_anim_progress(&anim, &sheet), 0.45f / 0.6f, 1e-4f), "progress back %f",
		athena_sprite_anim_progress(&anim, &sheet));
	/* A stall skips whole cycles and keeps the direction. */
	athena_sprite_anim_advance(&anim, &sheet, 60.0f, NULL, NULL);
	CHECK(anim.position == 2 && anim.direction == -1, "stall state %u %d", anim.position, anim.direction);

	c = clip(&sheet, "pp1", "0-2", 0.1f, ATHENA_SPRITE_PINGPONG, 1);
	athena_sprite_anim_play(&anim, &sheet, c);
	events[0] = 0;
	athena_sprite_anim_advance(&anim, &sheet, 1.0f, record, NULL);
	CHECK(strcmp(events, "F0 F1 F2 F1 F0 E ") == 0, "pingpong once ends on the first frame: %s", events);
	CHECK(anim.finished && athena_sprite_anim_frame(&anim, &sheet) == 0, "finished at 0");

	/* One frame: every cycle is a loop. */
	c = clip(&sheet, "one", "5", 0.1f, ATHENA_SPRITE_PINGPONG, 2);
	athena_sprite_anim_play(&anim, &sheet, c);
	events[0] = 0;
	athena_sprite_anim_advance(&anim, &sheet, 1.0f, record, NULL);
	CHECK(strcmp(events, "F0 L F0 E ") == 0, "one frame: %s", events);
	athena_sprite_sheet_clear(&sheet);
}

static void test_replace_while_playing(void)
{
	AthenaSpriteSheet sheet;
	AthenaSpriteAnim anim;
	int c;

	grid_sheet(&sheet);
	c = clip(&sheet, "run", "0-7", 0.1f, ATHENA_SPRITE_LOOP, 0);
	athena_sprite_anim_init(&anim);
	athena_sprite_anim_play(&anim, &sheet, c);
	athena_sprite_anim_advance(&anim, &sheet, 0.65f, NULL, NULL);
	CHECK(anim.position == 6, "position 6");
	clip(&sheet, "run", "0-2", 0.1f, ATHENA_SPRITE_LOOP, 0);
	CHECK(athena_sprite_anim_frame(&anim, &sheet) == 2, "clamped to the shorter clip");
	athena_sprite_anim_advance(&anim, &sheet, 0.1f, NULL, NULL);
	CHECK(anim.position == 0, "wraps %u", anim.position);
	athena_sprite_sheet_clear(&sheet);
}

static void test_quad(void)
{
	AthenaSpriteFrame frame = { .x = 100, .y = 50, .w = 10, .h = 20, .offset_x = 2, .offset_y = 4,
		.source_w = 16, .source_h = 32 };
	AthenaSpriteDraw draw;
	AthenaSpriteQuad q;

	athena_sprite_draw_init(&draw);
	draw.x = 200;
	draw.y = 100;
	CHECK(athena_sprite_quad(&frame, &draw, 0.0f, &q), "quad");
	CHECK(q.x == 202 && q.y == 104 && q.w == 10 && q.h == 20, "trim offset %f %f", q.x, q.y);
	CHECK(q.u1 == 100 && q.u2 == 110 && q.v1 == 50 && q.v2 == 70 && q.angle == 0, "uv");

	/* Bottom-center origin, scale 2. */
	draw.origin_x = 0.5f;
	draw.origin_y = 1.0f;
	draw.scale_x = draw.scale_y = 2.0f;
	athena_sprite_quad(&frame, &draw, 0.0f, &q);
	CHECK(q.x == 200 + (2 - 8) * 2 && q.y == 100 + (4 - 32) * 2 && q.w == 20 && q.h == 40,
		"origin and scale %f %f %f %f", q.x, q.y, q.w, q.h);

	/* Flip mirrors inside the untrimmed box: 16 - 2 - 10 = 4. */
	draw.scale_x = draw.scale_y = 1.0f;
	draw.flip_x = true;
	athena_sprite_quad(&frame, &draw, 0.0f, &q);
	CHECK(q.x == 200 + 4 - 8 && q.u1 == 110 && q.u2 == 100, "flip x %f %f %f", q.x, q.u1, q.u2);
	draw.flip_y = true;
	athena_sprite_quad(&frame, &draw, 0.0f, &q);
	CHECK(q.y == 100 + (32 - 4 - 20) - 32 && q.v1 == 70 && q.v2 == 50, "flip y %f", q.y);

	/* Rotation: the center turns about the origin. */
	draw.flip_x = draw.flip_y = false;
	draw.origin_x = draw.origin_y = 0;
	frame = (AthenaSpriteFrame){ .x = 0, .y = 0, .w = 10, .h = 10, .source_w = 10, .source_h = 10 };
	draw.rotation = 3.14159265f / 2.0f;
	athena_sprite_quad(&frame, &draw, 0.0f, &q);
	/* Center (5, 5) turned 90 degrees clockwise (y down) is (-5, 5). */
	CHECK(NEAR(q.x + 5, 200 - 5, 1e-3f) && NEAR(q.y + 5, 100 + 5, 1e-3f) && q.angle == draw.rotation,
		"rotation center %f %f", q.x + 5, q.y + 5);
	/* About its own center: the center stays. */
	draw.origin_x = draw.origin_y = 0.5f;
	athena_sprite_quad(&frame, &draw, 0.0f, &q);
	CHECK(NEAR(q.x, 195, 1e-3f) && NEAR(q.y, 95, 1e-3f), "centered rotation %f %f", q.x, q.y);

	draw.scale_x = 0;
	CHECK(!athena_sprite_quad(&frame, &draw, 0.0f, &q), "zero scale draws nothing");
	draw.scale_x = -1;
	CHECK(!athena_sprite_quad(&frame, &draw, 0.0f, &q), "negative scale draws nothing");
}

static void test_animator(void)
{
	AthenaSpriteSheet sheet;
	AthenaSpriteAnimatorEntry entries[3];
	AthenaTileSprite sprites[2];
	uint32_t written;
	int c;

	grid_sheet(&sheet);
	c = clip(&sheet, "spin", "0-3", 0.1f, ATHENA_SPRITE_LOOP, 0);
	memset(sprites, 0, sizeof(sprites));
	for (int i = 0; i < 3; i++) {
		athena_sprite_anim_init(&entries[i].anim);
		athena_sprite_anim_play(&entries[i].anim, &sheet, c);
		entries[i].sprite = (uint32_t)i;
		entries[i].shown = ATHENA_SPRITE_NOT_SHOWN;
		entries[i].flip_x = entries[i].flip_y = false;
	}
	entries[1].flip_x = true;
	entries[1].anim.position = 2;
	sprites[0].x = 7;

	written = athena_sprite_animator_update(entries, 3, &sheet, 0.0f, sprites, 2);
	CHECK(written == 2, "first write %u (the third sprite is past the buffer)", written);
	CHECK(sprites[0].u1 == 0 && sprites[0].u2 == 16 && sprites[0].v2 == 16 && sprites[0].x == 7,
		"uv only %f %f", sprites[0].u1, sprites[0].u2);
	CHECK(sprites[1].u1 == 48 && sprites[1].u2 == 32, "flipped frame 2 %f %f", sprites[1].u1, sprites[1].u2);
	CHECK(athena_sprite_animator_update(entries, 3, &sheet, 0.05f, sprites, 2) == 0, "unchanged frames not written");
	CHECK(athena_sprite_animator_update(entries, 3, &sheet, 0.05f, sprites, 2) == 2, "next frames");
	CHECK(sprites[0].u1 == 16 && entries[2].anim.position == 1, "advanced (also past the buffer)");
	athena_sprite_sheet_clear(&sheet);
}

static void test_next(void)
{
	AthenaSpriteSheet sheet;
	AthenaSpriteAnim anim;
	int attack, idle;

	grid_sheet(&sheet);
	attack = clip(&sheet, "attack", "0-1", 0.1f, ATHENA_SPRITE_LOOP, 1);
	CHECK(athena_sprite_sheet_set_next(&sheet, attack, "idle") == 0, "set next before idle exists");
	CHECK(athena_sprite_sheet_set_next(&sheet, 9, "idle") == ATHENA_SPRITE_EINVAL, "set next of a bad clip");
	athena_sprite_anim_init(&anim);
	athena_sprite_anim_play(&anim, &sheet, attack);
	events[0] = 0;
	/* Without the idle clip, the attack just ends. */
	athena_sprite_anim_advance(&anim, &sheet, 0.25f, record, NULL);
	CHECK(strcmp(events, "F0 F1 E ") == 0 && anim.finished, "next not found: %s", events);
	idle = clip(&sheet, "idle", "4-5", 0.1f, ATHENA_SPRITE_LOOP, 0);
	athena_sprite_anim_play(&anim, &sheet, attack);
	events[0] = 0;
	/* 0.25 s: attack 0, 1, end at 0.2, idle 4 for the 0.05 left. */
	athena_sprite_anim_advance(&anim, &sheet, 0.25f, record, NULL);
	CHECK(strcmp(events, "F0 F1 E F0 ") == 0, "chained events: %s", events);
	CHECK(anim.clip == idle && anim.playing && !anim.finished && NEAR(anim.time, 0.05f, 1e-5f),
		"playing idle with the time left %d %f", anim.clip, anim.time);
	CHECK(athena_sprite_anim_frame(&anim, &sheet) == 4, "idle frame");
	/* A cycle of clips that end: bounded, no hang. */
	athena_sprite_sheet_set_next(&sheet, idle, NULL);
	{
		int a = clip(&sheet, "a", "0", 0.01f, ATHENA_SPRITE_LOOP, 1);
		int b = clip(&sheet, "b", "1", 0.01f, ATHENA_SPRITE_LOOP, 1);

		athena_sprite_sheet_set_next(&sheet, a, "b");
		athena_sprite_sheet_set_next(&sheet, b, "a");
		athena_sprite_anim_play(&anim, &sheet, a);
		athena_sprite_anim_advance(&anim, &sheet, 100.0f, NULL, NULL);
		CHECK(anim.playing, "a cycle of clips keeps playing");
	}
	athena_sprite_sheet_clear(&sheet);
}

static void test_slices_inset(void)
{
	AthenaSpriteSheet sheet;
	AthenaSpriteRect r = { 2, 4, 6, 8 }, out;
	AthenaSpriteFrame frame = { .x = 16, .y = 0, .w = 16, .h = 16, .source_w = 16, .source_h = 16 };
	AthenaSpriteDraw draw;
	AthenaSpriteQuad q;
	int slice;

	grid_sheet(&sheet);
	slice = athena_sprite_sheet_set_slice(&sheet, "hit", 2, 3, &r);
	CHECK(slice == 0 && athena_sprite_sheet_find_slice(&sheet, "hit") == 0, "slice");
	CHECK(!athena_sprite_sheet_slice(&sheet, slice, 1, &out), "no rect before");
	CHECK(athena_sprite_sheet_slice(&sheet, slice, 4, &out) && out.w == 6, "rect in range");
	CHECK(!athena_sprite_sheet_slice(&sheet, slice, 5, &out), "no rect after");
	CHECK(athena_sprite_sheet_set_slice(&sheet, "hit", 3, 1, NULL) == 0 &&
		!athena_sprite_sheet_slice(&sheet, slice, 3, &out), "remove one frame");
	CHECK(athena_sprite_sheet_set_slice(&sheet, "hit", 7, 2, &r) == ATHENA_SPRITE_ERANGE, "past the frames");
	/* Frames added after the slice have none. */
	{
		AthenaSpriteGrid grid = { .frame_w = 8, .frame_h = 8, .texture_w = 8, .texture_h = 8 };
		athena_sprite_sheet_add_grid(&sheet, &grid);
		CHECK(!athena_sprite_sheet_slice(&sheet, slice, 8, &out), "new frame");
		CHECK(athena_sprite_sheet_set_slice(&sheet, "hit", 8, 1, &r) == 0 &&
			athena_sprite_sheet_slice(&sheet, slice, 8, &out), "grown to the new frame");
	}

	athena_sprite_draw_init(&draw);
	draw.x = 100; draw.y = 50;
	draw.origin_x = 0.5f; draw.origin_y = 1.0f;
	draw.scale_x = draw.scale_y = 2;
	CHECK(athena_sprite_place_rect(&frame, &draw, &r, &out) && out.x == 100 + (2 - 8) * 2 &&
		out.y == 50 + (4 - 16) * 2 && out.w == 12 && out.h == 16, "placed %f %f", out.x, out.y);
	draw.flip_x = true;
	athena_sprite_place_rect(&frame, &draw, &r, &out);
	CHECK(out.x == 100 + (16 - 2 - 6 - 8) * 2, "placed flipped %f", out.x);

	athena_sprite_draw_init(&draw);
	athena_sprite_quad(&frame, &draw, 0.5f, &q);
	CHECK(q.u1 == 16.5f && q.u2 == 31.5f && q.v1 == 0.5f && q.v2 == 15.5f && q.w == 16, "inset");
	draw.flip_x = true;
	athena_sprite_quad(&frame, &draw, 0.5f, &q);
	CHECK(q.u1 == 31.5f && q.u2 == 16.5f, "inset flipped");
	athena_sprite_quad(&frame, &draw, 100.0f, &q);
	CHECK(q.u1 == 24 && q.u2 == 24, "inset clamped to half the frame");
	athena_sprite_sheet_clear(&sheet);
	CHECK(sheet.slices == NULL && sheet.slice_count == 0, "clear frees slices");
}

static void test_corners(void)
{
	AthenaSpriteFrame frame = { .x = 100, .y = 50, .w = 10, .h = 20, .source_w = 10, .source_h = 20 };
	AthenaSpriteDraw draw;
	AthenaSpriteCorners c;
	AthenaSpriteQuad q;

	athena_sprite_draw_init(&draw);
	draw.x = 5;
	draw.y = 7;
	CHECK(athena_sprite_corners(&frame, &draw, 0.0f, &c), "corners");
	/* Top-left, top-right, bottom-left, bottom-right; as the quad without rotation. */
	athena_sprite_quad(&frame, &draw, 0.0f, &q);
	CHECK(c.x[0] == q.x && c.y[0] == q.y && c.x[3] == q.x + q.w && c.y[3] == q.y + q.h,
		"corners match the quad");
	CHECK(c.u[0] == 100 && c.v[0] == 50 && c.u[1] == 110 && c.v[2] == 70 && c.u[3] == 110,
		"corner texels");
	draw.flip_x = true;
	athena_sprite_corners(&frame, &draw, 0.0f, &c);
	CHECK(c.u[0] == 110 && c.u[1] == 100, "flipped texels");
	draw.flip_x = false;

	/* Turned 90 degrees clockwise in the atlas: covers 20 x 10 texels from (100, 50). */
	frame.rotated = true;
	athena_sprite_corners(&frame, &draw, 0.0f, &c);
	CHECK(c.x[0] == 5 && c.x[1] == 15 && c.y[2] == 27, "turned frame keeps its shown size");
	/* Shown top-left (0, 0) is stored at (h, 0); top-right (w, 0) at (h, w); bottom-left at (0, 0). */
	CHECK(c.u[0] == 120 && c.v[0] == 50 && c.u[1] == 120 && c.v[1] == 60 &&
		c.u[2] == 100 && c.v[2] == 50 && c.u[3] == 100 && c.v[3] == 60,
		"turned texels %f %f / %f %f / %f %f / %f %f", c.u[0], c.v[0], c.u[1], c.v[1],
		c.u[2], c.v[2], c.u[3], c.v[3]);
	athena_sprite_corners(&frame, &draw, 1.0f, &c);
	CHECK(c.u[0] == 119 && c.v[0] == 51 && c.u[3] == 101 && c.v[3] == 59, "turned inset");

	/* Rotation turns the corners about the origin. */
	frame.rotated = false;
	draw.x = draw.y = 0;
	draw.rotation = 3.14159265f / 2.0f;
	athena_sprite_corners(&frame, &draw, 0.0f, &c);
	CHECK(NEAR(c.x[1], 0, 1e-4f) && NEAR(c.y[1], 10, 1e-4f), "rotated corner %f %f", c.x[1], c.y[1]);
	draw.scale_x = 0;
	CHECK(!athena_sprite_corners(&frame, &draw, 0.0f, &c), "zero scale");
}

static void test_files(void)
{
	AthenaSpriteSheet sheet;
	AthenaSpriteGrid grid = { .frame_w = 16, .frame_h = 16, .texture_w = 64, .texture_h = 48 };
	char *p;
	static const char json[] = "{ \"frames\": { \"image\": { \"frame\": {} } },\n"
		"  \"meta\": { \"app\": \"x\", \"image\" : \"hero.png\", \"size\": {} } }";

	athena_sprite_sheet_init(&sheet);
	CHECK(athena_sprite_sheet_add_grid(&sheet, &grid) == 12 && grid.columns == 4 && grid.rows == 3,
		"grid writes back %u x %u", grid.columns, grid.rows);
	athena_sprite_sheet_clear(&sheet);

	p = athena_sprite_path_join("tests/sprites/hero.json", "hero.png");
	CHECK(p && strcmp(p, "tests/sprites/hero.png") == 0, "join %s", p ? p : "(null)");
	free(p);
	p = athena_sprite_path_join("mass:game\\hero.json", "hero.png");
	CHECK(p && strcmp(p, "mass:game\\hero.png") == 0, "join backslash %s", p ? p : "(null)");
	free(p);
	p = athena_sprite_path_join("mass:hero.json", "hero.png");
	CHECK(p && strcmp(p, "mass:hero.png") == 0, "join device %s", p ? p : "(null)");
	free(p);
	p = athena_sprite_path_join("dir/hero.json", "host:abs.png");
	CHECK(p && strcmp(p, "host:abs.png") == 0, "absolute kept");
	free(p);
	p = athena_sprite_path_join(NULL, "a.png");
	CHECK(p && strcmp(p, "a.png") == 0, "no base");
	free(p);

	/* A frame named "image" is not the texture: meta's is. */
	p = athena_sprite_find_meta_image(json, sizeof(json) - 1);
	CHECK(p && strcmp(p, "hero.png") == 0, "meta image %s", p ? p : "(null)");
	free(p);
	CHECK(!athena_sprite_find_meta_image("{ \"meta\": { \"image\": \"a\\\"b.png\" } }", 34),
		"escapes left to the parser");
	CHECK(!athena_sprite_find_meta_image("{ \"frames\": {} }", 16), "no meta");
	CHECK(!athena_sprite_find_meta_image("{ \"meta\": { \"image\": \"", 22), "truncated");
}

int main(void)
{
	test_grid();
	test_frames();
	test_parse();
	test_clips();
	test_loop();
	test_once();
	test_pingpong();
	test_replace_while_playing();
	test_quad();
	test_animator();
	test_next();
	test_slices_inset();
	test_corners();
	test_files();
	if (failures) {
		printf("sprite: %d failure(s)\n", failures);
		return 1;
	}
	printf("sprite: all tests passed\n");
	return 0;
}
