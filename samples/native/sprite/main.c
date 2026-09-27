/*
 * An animated sprite in C, without QuickJS: the Sprite C API
 * (athena/sprite.h) cuts texture.png in 32 x 32 frames, plays a run clip
 * while the D-pad is held and an idle one otherwise, flips to face the
 * direction of motion, and counts steps with a frame event.
 *
 *   D-pad        walk              CROSS     attack (plays once)
 *
 *   node tools/modules.js configure --modules=screen,draw,gamepad,loop,image,sprite
 *   make RUNTIME=native APP_SRCS=samples/native/sprite/main.c
 */
#include <libpad.h>

#include <athena.h>
#include <athena/color.h>
#include <athena/gamepad.h>
#include <athena/image.h>
#include <athena/loop.h>
#include <athena/screen.h>
#include <athena/sprite.h>

#define CELL 32.0f
#define SPEED 160.0f

static Color color(int r, int g, int b)
{
    return athena_color_new(r, g, b, ATHENA_COLOR_DEFAULT_ALPHA);
}

/* Adds a clip of the frames in `range` with the same duration for each. */
static int add_clip(AthenaSpriteSheet *sheet, const char *name, const char *range,
    float fps, AthenaSpriteMode mode, uint32_t loops)
{
    uint16_t frames[32];
    float duration = 1.0f / fps;
    int count = athena_sprite_parse_frames(range, sheet->frame_count, frames, 32);

    if (count <= 0 || count > 32)
        return -1;
    return athena_sprite_sheet_add_clip(sheet, name, frames, (uint32_t)count,
        &duration, 1, mode, loops);
}

typedef struct {
    AthenaSpriteAnim anim;
    int run;
    int attacking;
    int steps;
} Hero;

/* Frame events: position 2 of the run clip is a footstep; the attack ends. */
static void on_event(void *opaque, AthenaSpriteEventType type, uint32_t position,
    uint32_t frame)
{
    Hero *hero = opaque;

    if (type == ATHENA_SPRITE_EVENT_FRAME && position == 2 && hero->anim.clip == hero->run)
        hero->steps++;
    if (type == ATHENA_SPRITE_EVENT_END)
        hero->attacking = 0;
}

int athena_main(int argc, char **argv) {
    AthenaSpriteSheet sheet;
    AthenaSpriteGrid grid = { .frame_w = CELL, .frame_h = CELL };
    Hero hero = { .attacking = 0, .steps = 0 };
    AthenaSpriteDraw pose;
    AthenaLoopClock clock;
    AthenaImage *image;
    int idle, attack;
    float x = 320.0f, y = 300.0f;

    graphics_service_init();
    if (athena_gamepad_core_init() != ATHENA_GAMEPAD_OK)
        dbgprintf("[sprite] gamepad unavailable\n");
    image = athena_image_create("texture.png", false);
    if (!image || !athena_image_is_loaded(image)) {
        dbgprintf("[sprite] texture.png not found\n");
        return 1;
    }

    athena_sprite_sheet_init(&sheet);
    grid.texture_w = (float)image->surface->Width;
    grid.texture_h = (float)image->surface->Height;
    if (athena_sprite_sheet_add_grid(&sheet, &grid) <= 0)
        return 1;
    idle = add_clip(&sheet, "idle", "0-3", 4.0f, ATHENA_SPRITE_PINGPONG, 0);
    hero.run = add_clip(&sheet, "run", "8-15", 12.0f, ATHENA_SPRITE_LOOP, 0);
    attack = add_clip(&sheet, "attack", "16-21", 10.0f, ATHENA_SPRITE_LOOP, 1);
    if (idle < 0 || hero.run < 0 || attack < 0)
        return 1;

    athena_sprite_anim_init(&hero.anim);
    athena_sprite_anim_play(&hero.anim, &sheet, idle);
    athena_sprite_draw_init(&pose);
    pose.origin_x = 0.5f;                          /* bottom center */
    pose.origin_y = 1.0f;
    pose.scale_x = pose.scale_y = 2.0f;
    athena_loop_clock_reset(&clock, ATHENA_LOOP_DEFAULT_MAX_DELTA);

    for (;;) {
        float dt = athena_loop_clock_tick(&clock);
        float dx = 0.0f, dy = 0.0f;
        int wanted;

        athena_gamepad_core_update();
        if (athena_gamepad_core_pressed(0, PAD_LEFT)) dx -= 1.0f;
        if (athena_gamepad_core_pressed(0, PAD_RIGHT)) dx += 1.0f;
        if (athena_gamepad_core_pressed(0, PAD_UP)) dy -= 1.0f;
        if (athena_gamepad_core_pressed(0, PAD_DOWN)) dy += 1.0f;
        if (athena_gamepad_core_pressed(0, PAD_CROSS) && !hero.attacking) {
            hero.attacking = 1;
            athena_sprite_anim_play(&hero.anim, &sheet, attack);
        }
        if (!hero.attacking) {
            x += dx * SPEED * dt;
            y += dy * SPEED * dt;
            /* Only switch clips on a change, or the clip restarts every frame. */
            wanted = dx != 0.0f || dy != 0.0f ? hero.run : idle;
            if (hero.anim.clip != wanted || hero.anim.finished)
                athena_sprite_anim_play(&hero.anim, &sheet, wanted);
            if (dx != 0.0f)
                pose.flip_x = dx < 0.0f;
        }
        athena_sprite_anim_advance(&hero.anim, &sheet, dt, on_event, &hero);

        clearScreen(color(24, 28, 40));
        pose.x = x;
        pose.y = y;
        athena_sprite_draw(image, &sheet, athena_sprite_anim_frame(&hero.anim, &sheet), &pose);
        /* One bar per footstep, up to 20. */
        draw_sprite(10, 10, (hero.steps % 20 + 1) * 8, 8, color(255, 255, 255));
        flipScreen();
    }
    return 0;
}
