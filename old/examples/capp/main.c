/*
 * AthenaEnv C API showcase.
 *
 * Demonstrates pad input, 2D drawing, fonts, screen management, vec2 math,
 * timers, and system stats in a single interactive loop (no QuickJS).
 *
 * Controls (port 0):
 *   Left stick  — move the orb
 *   Cross/Square/Triangle/Circle — change orb color
 *   L1 / R1     — shrink / grow orb
 *   Select      — reset position
 *   Start       — exit
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <athena/athena.h>

#ifdef ATHENA_GRAPHICS
#include <graphics.h>

#define FONT_CANDIDATES \
    "fonts/minecraft.ttf", "fonts/LEMONMILK-Light.otf", "fonts/Segoe UI.ttf"

#define PLAYER_RADIUS_DEFAULT 24.0f
#define STICK_SPEED           3.5f

typedef struct CappPlayer {
    float x;
    float y;
    float radius;
    Color color;
} CappPlayer;

typedef struct CappScene {
    int width;
    int height;
    AthenaPad *pad;
    AthenaFont *font;
    AthenaTimer *timer;
    CappPlayer player;
    bool running;
} CappScene;

static void capp_wait_boot_logo(void) {
    while (!bootlogo_finished() && boot_logo)
        usleep(1000);
}

static AthenaFont *capp_load_first_font(void) {
    static const char *const paths[] = { FONT_CANDIDATES };

    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        AthenaFont *font = athena_font_load(paths[i]);
        if (font) {
            athena_font_set_scale(font, 0.85f);
            athena_font_set_color(font, athena_color_rgba(220, 230, 255, 255));
            return font;
        }
    }

    return NULL;
}

static void capp_init_scene(CappScene *scene) {
    AthenaVideoMode mode;

    memset(scene, 0, sizeof(*scene));
    athena_screen_get_mode(&mode);

    scene->width = mode.width > 0 ? mode.width : 640;
    scene->height = mode.height > 0 ? mode.height : 448;
    scene->player.x = scene->width * 0.5f;
    scene->player.y = scene->height * 0.5f;
    scene->player.radius = PLAYER_RADIUS_DEFAULT;
    scene->player.color = athena_color_rgba(80, 180, 255, 255);
    scene->running = true;

    athena_screen_set_frame_counter(true);
    athena_screen_set_clear_color(athena_color_rgba(16, 20, 36, 255));
    /* Required for 2D: default init enables depth test (see hello.js / pong.js). */
    athena_screen_set_param(DEPTH_TEST_ENABLE, 0);

    scene->pad = athena_pad_open(0);
    scene->font = capp_load_first_font();
    scene->timer = athena_timer_create();
}

static void capp_destroy_scene(CappScene *scene) {
    if (scene->timer)
        athena_timer_destroy(scene->timer);
    if (scene->font)
        athena_font_destroy(scene->font);
    if (scene->pad)
        athena_pad_close(scene->pad);
}

static Color capp_cycle_border_color(const AthenaTimer *timer) {
    clock_t ms = athena_timer_get_time(timer);
    float t = (float)(ms % 4000) / 4000.0f;
    float wave = 0.5f + 0.5f * sinf(t * 6.2831853f);

    return athena_color_rgba(
        (uint32_t)(90.0f + wave * 120.0f),
        (uint32_t)(120.0f + (1.0f - wave) * 80.0f),
        (uint32_t)(200.0f + wave * 40.0f),
        255);
}

static void capp_draw_background(const CappScene *scene) {
    Color top = athena_color_rgba(24, 32, 58, 255);
    Color bottom = athena_color_rgba(10, 14, 28, 255);
    float w = (float)scene->width;
    float h = (float)scene->height;

    athena_draw_quad_gouraud(
        0.0f, 0.0f, w, 0.0f, w, h, 0.0f, h,
        top, top, bottom, bottom);

    Color grid = athena_color_rgba(40, 52, 78, 90);
    for (int x = 0; x < scene->width; x += 64)
        athena_draw_line((float)x, 0.0f, (float)x, h, grid);
    for (int y = 0; y < scene->height; y += 64)
        athena_draw_line(0.0f, (float)y, w, (float)y, grid);

    Color border = capp_cycle_border_color(scene->timer);
    const float inset = 12.0f;
    const float thick = 3.0f;

    athena_draw_rect(inset, inset, w - inset * 2.0f, thick, border);
    athena_draw_rect(inset, h - inset - thick, w - inset * 2.0f, thick, border);
    athena_draw_rect(inset, inset, thick, h - inset * 2.0f, border);
    athena_draw_rect(w - inset - thick, inset, thick, h - inset * 2.0f, border);
}

static void capp_update_player(CappScene *scene) {
    AthenaPad *pad = scene->pad;
    CappPlayer *player = &scene->player;
    float margin = player->radius + 8.0f;

    if (!pad)
        return;

    player->x += (float)pad->lx * STICK_SPEED * 0.01f;
    player->y += (float)pad->ly * STICK_SPEED * 0.01f;

    if (player->x < margin)
        player->x = margin;
    if (player->y < margin)
        player->y = margin;
    if (player->x > scene->width - margin)
        player->x = scene->width - margin;
    if (player->y > scene->height - margin)
        player->y = scene->height - margin;

    if (athena_pad_pressed(pad, PAD_CROSS))
        player->color = athena_color_rgba(70, 210, 120, 255);
    if (athena_pad_pressed(pad, PAD_SQUARE))
        player->color = athena_color_rgba(90, 140, 255, 255);
    if (athena_pad_pressed(pad, PAD_TRIANGLE))
        player->color = athena_color_rgba(255, 110, 90, 255);
    if (athena_pad_pressed(pad, PAD_CIRCLE))
        player->color = athena_color_rgba(255, 210, 70, 255);

    if (athena_pad_just_pressed(pad, PAD_L1))
        player->radius -= 2.0f;
    if (athena_pad_just_pressed(pad, PAD_R1))
        player->radius += 2.0f;
    if (player->radius < 8.0f)
        player->radius = 8.0f;
    if (player->radius > 64.0f)
        player->radius = 64.0f;

    if (athena_pad_just_pressed(pad, PAD_SELECT)) {
        player->x = scene->width * 0.5f;
        player->y = scene->height * 0.5f;
        player->radius = PLAYER_RADIUS_DEFAULT;
    }

    if (athena_pad_just_pressed(pad, PAD_CROSS))
        athena_pad_rumble(pad->port, 1, 1);

    if (athena_pad_just_pressed(pad, PAD_START))
        scene->running = false;
}

static void capp_draw_player(const CappScene *scene) {
    const CappPlayer *player = &scene->player;
    Color glow = athena_color_set_a(player->color, 80);

    athena_draw_circle(player->x, player->y, player->radius + 6.0f, glow, true);
    athena_draw_circle(player->x, player->y, player->radius, player->color, true);
    athena_draw_circle(player->x, player->y, player->radius * 0.35f,
                       athena_color_rgba(255, 255, 255, 180), true);

    athena_draw_line(player->x - player->radius, player->y,
                     player->x + player->radius, player->y,
                     athena_color_rgba(255, 255, 255, 60));
    athena_draw_line(player->x, player->y - player->radius,
                     player->x, player->y + player->radius,
                     athena_color_rgba(255, 255, 255, 60));
}

static void capp_draw_hud(CappScene *scene, float fps, const AthenaMemoryStats *mem) {
    char line[96];
    char pos_buf[48];
    AthenaVec2 center;
    AthenaVec2 *player_vec;
    AthenaVec2 *delta;
    float dist;

    if (!scene->font)
        return;

    athena_font_set_color(scene->font, athena_color_rgba(230, 235, 255, 255));
    athena_font_print(scene->font, 28.0f, 28.0f, "AthenaEnv C API Playground");

    snprintf(line, sizeof(line), "FPS: %.1f   Pads: %d",
             fps, athena_pad_connected_count());
    athena_font_print(scene->font, 28.0f, 48.0f, line);

    snprintf(line, sizeof(line), "Core: %u KiB used",
             mem->used/1024);
    athena_font_print(scene->font, 28.0f, 68.0f, line);

    center.x = scene->width * 0.5f;
    center.y = scene->height * 0.5f;
    player_vec = athena_vec2_create(scene->player.x, scene->player.y);
    delta = athena_vec2_sub(player_vec, &center);
    if (player_vec && delta) {
        dist = athena_vec2_norm(delta);
        athena_vec2_tostring(player_vec, pos_buf, sizeof(pos_buf));
        snprintf(line, sizeof(line), "Pos %s  dist(center): %.1f", pos_buf, dist);
        athena_font_print(scene->font, 28.0f, 88.0f, line);
    }
    athena_vec2_destroy(delta);
    athena_vec2_destroy(player_vec);

    if (scene->pad) {
        snprintf(line, sizeof(line), "LX:%4d LY:%4d  BTNS:0x%08X",
                 scene->pad->lx, scene->pad->ly, scene->pad->btns);
        athena_font_print(scene->font, 28.0f, (float)scene->height - 36.0f, line);
    }

    athena_font_set_color(scene->font, athena_color_rgba(170, 180, 210, 255));
    athena_font_print(scene->font, 28.0f, (float)scene->height - 56.0f,
                      "Stick:move  Face:color  L1/R1:size  Select:reset  Start:exit");
}

static int capp_graphics_main(void) {
    CappScene scene;
    AthenaMemoryStats mem;
    float fps;

    capp_wait_boot_logo();
    capp_init_scene(&scene);

    while (scene.running) {
        if (scene.pad)
            athena_pad_update(scene.pad);

        capp_update_player(&scene);

        athena_screen_clear(athena_screen_get_clear_color());
        capp_draw_background(&scene);
        capp_draw_player(&scene);

        athena_system_get_memory_stats(&mem);
        fps = athena_screen_get_fps(60);
        capp_draw_hud(&scene, fps, &mem);

        athena_screen_flip();
    }

    capp_destroy_scene(&scene);
    athena_screen_clear(athena_color_rgba(0, 0, 0, 255));
    athena_screen_flip();

    return 0;
}

#endif /* ATHENA_GRAPHICS */

static int capp_headless_main(void) {
    AthenaPad *pad = athena_pad_open(0);
    AthenaTimer *timer = athena_timer_create();
    int frames = 0;

    while (frames < 300) {
        if (pad) {
            athena_pad_update(pad);
            if (athena_pad_just_pressed(pad, PAD_START))
                break;
        }

        frames++;
        usleep(16666);
    }

    if (timer)
        athena_timer_destroy(timer);
    if (pad)
        athena_pad_close(pad);

    return 0;
}

static int capp_app_main(int argc, char **argv) {
    (void)argc;
    (void)argv;

#ifdef ATHENA_GRAPHICS
    return capp_graphics_main();
#else
    return capp_headless_main();
#endif
}

ATHENA_APP_ENTRY(capp_app_main);
