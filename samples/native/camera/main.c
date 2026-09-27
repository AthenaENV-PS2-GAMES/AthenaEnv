/*
 * A camera following a square around a world larger than the screen, in C,
 * without QuickJS: the Camera2D C API (athena/camera2d.h) with the Loop
 * clock. Everything is drawn in world coordinates between begin() and end().
 *
 *   D-pad        move           L1 / R1   zoom out / in
 *   L2 / R2      turn           CROSS     shake
 *
 *   node tools/modules.js configure --modules=screen,draw,gamepad,loop,camera2d
 *   make RUNTIME=native APP_SRCS=samples/native/camera/main.c
 */
#include <libpad.h>

#include <athena.h>
#include <athena/camera2d.h>
#include <athena/color.h>
#include <athena/draw.h>
#include <athena/gamepad.h>
#include <athena/loop.h>
#include <athena/screen.h>

#define WORLD_W 2048
#define WORLD_H 1024
#define CELL 64
#define SIZE 16
#define SPEED 180.0f

static Color color(int r, int g, int b)
{
    return athena_color_new(r, g, b, ATHENA_COLOR_DEFAULT_ALPHA);
}

/* A checkerboard world, so motion, zoom and rotation show. */
static void draw_world(void)
{
    for (int y = 0; y < WORLD_H; y += CELL)
        for (int x = 0; x < WORLD_W; x += CELL)
            if (((x + y) / CELL) % 2)
                draw_sprite((float)x, (float)y, CELL, CELL, color(40, 60, 90));
    draw_line(0, 0, WORLD_W, 0, color(255, 80, 80));
    draw_line(0, WORLD_H, WORLD_W, WORLD_H, color(255, 80, 80));
}

int athena_main(int argc, char **argv) {
    AthenaScreenMode mode;
    AthenaLoopClock clock;
    AthenaCamera2D cam;
    float x = WORLD_W / 2.0f, y = WORLD_H / 2.0f;

    graphics_service_init();
    if (athena_screen_get_mode(&mode) != ATHENA_SCREEN_OK)
        return 1;
    if (athena_gamepad_core_init() != ATHENA_GAMEPAD_OK)
        dbgprintf("[camera] gamepad unavailable\n");

    athena_camera2d_init(&cam, (float)mode.width, (float)mode.height);
    athena_camera2d_set_bounds(&cam, 0, 0, WORLD_W, WORLD_H);
    athena_camera2d_follow(&cam, 8.0f, 8.0f);          /* smoothing, per second */
    cam.deadzone_w = 48.0f;                            /* screen pixels */
    cam.deadzone_h = 32.0f;
    athena_camera2d_set_target(&cam, x, y);
    athena_camera2d_snap(&cam);
    athena_loop_clock_reset(&clock, ATHENA_LOOP_DEFAULT_MAX_DELTA);

    for (;;) {
        float dt = athena_loop_clock_tick(&clock);

        athena_gamepad_core_update();
        if (athena_gamepad_core_pressed(0, PAD_LEFT)) x -= SPEED * dt;
        if (athena_gamepad_core_pressed(0, PAD_RIGHT)) x += SPEED * dt;
        if (athena_gamepad_core_pressed(0, PAD_UP)) y -= SPEED * dt;
        if (athena_gamepad_core_pressed(0, PAD_DOWN)) y += SPEED * dt;
        if (athena_gamepad_core_pressed(0, PAD_L2)) cam.rotation -= 1.5f * dt;
        if (athena_gamepad_core_pressed(0, PAD_R2)) cam.rotation += 1.5f * dt;
        if (athena_gamepad_core_pressed(0, PAD_L1) && !cam.zoom_tween.active)
            athena_camera2d_zoom_to(&cam, cam.zoom_x > 0.5f ? cam.zoom_x / 1.5f : cam.zoom_x, 0.25f);
        if (athena_gamepad_core_pressed(0, PAD_R1) && !cam.zoom_tween.active)
            athena_camera2d_zoom_to(&cam, cam.zoom_x < 3.0f ? cam.zoom_x * 1.5f : cam.zoom_x, 0.25f);
        if (athena_gamepad_core_pressed(0, PAD_CROSS))
            athena_camera2d_shake(&cam, 6.0f, 0.3f, 25.0f, 0.02f);

        /* The camera moves after the game: target, then update. */
        athena_camera2d_set_screen(&cam, (float)mode.width, (float)mode.height);
        athena_camera2d_set_target(&cam, x, y);
        athena_camera2d_update(&cam, dt);

        clearScreen(color(16, 16, 32));
        athena_camera2d_begin(&cam, 1.0f, 1.0f);       /* world space from here */
        draw_world();
        draw_sprite(x - SIZE / 2, y - SIZE / 2, SIZE, SIZE, color(255, 160, 0));
        athena_camera2d_end(&cam);                     /* effects, then screen space */
        draw_sprite(10, 10, 40, 8, color(255, 255, 255));   /* a HUD bar */
        flipScreen();
    }
    return 0;
}
