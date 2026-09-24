/*
 * Move a square with the D-pad, in C, without QuickJS.
 *
 *   node tools/modules.js configure --modules=screen,draw,gamepad
 *   make RUNTIME=native APP_SRCS=samples/native/move/main.c
 */
#include <libpad.h>

#include <athena.h>
#include <athena/color.h>
#include <athena/draw.h>
#include <athena/gamepad.h>
#include <athena/screen.h>

#define SIZE 32
#define SPEED 4

int athena_main(int argc, char **argv) {
    AthenaScreenMode mode;
    float x, y;

    graphics_service_init();
    if (athena_screen_get_mode(&mode) != ATHENA_SCREEN_OK)
        return 1;
    if (athena_gamepad_core_init() != ATHENA_GAMEPAD_OK)
        dbgprintf("[move] gamepad unavailable\n");

    x = (mode.width - SIZE) / 2.0f;
    y = (mode.height - SIZE) / 2.0f;

    for (;;) {
        athena_gamepad_core_update();
        if (athena_gamepad_core_pressed(0, PAD_LEFT) && x > 0) x -= SPEED;
        if (athena_gamepad_core_pressed(0, PAD_RIGHT) && x < mode.width - SIZE) x += SPEED;
        if (athena_gamepad_core_pressed(0, PAD_UP) && y > 0) y -= SPEED;
        if (athena_gamepad_core_pressed(0, PAD_DOWN) && y < mode.height - SIZE) y += SPEED;

        clearScreen(athena_color_new(16, 16, 32, ATHENA_COLOR_DEFAULT_ALPHA));
        draw_sprite(x, y, SIZE, SIZE, athena_color_new(255, 160, 0, ATHENA_COLOR_DEFAULT_ALPHA));
        flipScreen();
    }
    return 0;
}
