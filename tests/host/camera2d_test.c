/* Host test of the 2D view math (graphics/native/view.c) and the camera behavior (camera2d/native/camera2d.c). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <athena/graphics/view.h>
#include <athena/camera2d.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define NEAR(a, b, eps) (fabsf((a) - (b)) <= (eps))

static const float SCREEN_W = 640.0f, SCREEN_H = 448.0f;

static void test_view(void) {
    AthenaAffine2D m, inv, back, id;
    AthenaRect2D r = { 0, 0, 10, 20 }, out;
    float x, y;

    athena_affine_identity(&id);
    CHECK(athena_affine_classify(&id) == ATHENA_VIEW_IDENTITY, "identity");
    m = id; m.tx = 0.5f;
    CHECK(athena_affine_classify(&m) == ATHENA_VIEW_AXIS, "a half-pixel shift is not the identity");
    m = id; m.xx = 2.0f;
    CHECK(athena_affine_classify(&m) == ATHENA_VIEW_AXIS, "scale");
    m = id; m.xy = 0.25f;
    CHECK(athena_affine_classify(&m) == ATHENA_VIEW_ROTATED, "shear/rotation");

    /* Rounding noise snaps to the fast paths. */
    m = id; m.xx = 1.0f + 1e-8f; m.xy = 1e-9f;
    athena_view_set(&m);
    CHECK(athena_view_kind() == ATHENA_VIEW_IDENTITY && athena_view_matrix.xy == 0.0f &&
        athena_view_matrix.xx == 1.0f, "near identity kind %d", athena_view_kind());
    m = (AthenaAffine2D){ 0.0f, -2.0f, 3.0f, 0.0f, 5.0f, 7.0f };
    athena_view_set(&m);
    CHECK(athena_view_kind() == ATHENA_VIEW_ROTATED && NEAR(athena_view_scale(), sqrtf(6.0f), 1e-5f),
        "rotated view scale %f", athena_view_scale());
    athena_view_apply(1.0f, 1.0f, &x, &y);
    CHECK(NEAR(x, 3.0f, 1e-6f) && NEAR(y, 10.0f, 1e-6f), "apply %f %f", x, y);
    athena_view_set(NULL);
    CHECK(athena_view_kind() == ATHENA_VIEW_IDENTITY, "reset");

    /* Inverse and composition. */
    m = (AthenaAffine2D){ 1.5f, 0.5f, -0.25f, 2.0f, 30.0f, -12.0f };
    CHECK(athena_affine_invert(&m, &inv), "invertible");
    athena_affine_multiply(&m, &inv, &back);
    CHECK(NEAR(back.xx, 1, 1e-5f) && NEAR(back.xy, 0, 1e-5f) && NEAR(back.yx, 0, 1e-5f) &&
        NEAR(back.yy, 1, 1e-5f) && NEAR(back.tx, 0, 1e-4f) && NEAR(back.ty, 0, 1e-4f),
        "m * inverse = %f %f %f %f %f %f", back.xx, back.xy, back.yx, back.yy, back.tx, back.ty);
    athena_affine_multiply(&m, &inv, &m);   /* aliasing */
    CHECK(NEAR(m.xx, 1, 1e-5f) && NEAR(m.tx, 0, 1e-4f), "aliased multiply");
    m = (AthenaAffine2D){ 1, 2, 2, 4, 0, 0 };
    CHECK(!athena_affine_invert(&m, &inv), "singular");

    /* Bounds of a rotated rectangle: 90 degrees swaps the sides. */
    m = (AthenaAffine2D){ 0, -1, 1, 0, 100, 0 };
    athena_affine_bounds(&m, &r, &out);
    CHECK(NEAR(out.x0, 80, 1e-5f) && NEAR(out.x1, 100, 1e-5f) && NEAR(out.y0, 0, 1e-5f) &&
        NEAR(out.y1, 10, 1e-5f), "bounds %f %f %f %f", out.x0, out.y0, out.x1, out.y1);

    /* Culler: world rectangles through the view against a clip rectangle. */
    {
        AthenaViewCuller c = { { 2, 0, 0, 2, -100, 0 }, { 0, 0, 640, 448 }, false };

        CHECK(athena_view_culler_visible(&c, 60, 10, 10, 10), "inside: 20..40 on screen");
        CHECK(athena_view_culler_visible(&c, 40, 10, 10, 10), "touching the left edge");
        CHECK(!athena_view_culler_visible(&c, 30, 10, 10, 10), "left of the clip");
        CHECK(!athena_view_culler_visible(&c, 371, 10, 10, 10), "right of the clip");
        CHECK(athena_view_culler_visible(&c, 70, 10, -30, 10), "negative width (flipped sprite)");
        CHECK(!athena_view_culler_visible(&c, 60, 300, 10, 10), "below the clip");
        /* Turned 45 degrees: the bounding box decides. */
        c = (AthenaViewCuller){ { 0.70710678f, -0.70710678f, 0.70710678f, 0.70710678f, 320, 0 },
            { 0, 0, 640, 448 }, true };
        CHECK(athena_view_culler_visible(&c, 0, 0, 10, 10), "turned, at the top");
        CHECK(!athena_view_culler_visible(&c, -500, -500, 10, 10), "turned, far away");
    }

    CHECK(!athena_view_get_world(&m), "no world view");
    athena_view_set_world(&id);
    CHECK(athena_view_get_world(&m) && m.xx == 1.0f, "world view");
    athena_view_set_world(NULL);
    CHECK(!athena_view_get_world(NULL), "world view cleared");
}

static void test_identity_and_conversions(void) {
    AthenaCamera2D cam;
    AthenaAffine2D m;
    AthenaRect2D vis;
    float sx, sy, wx, wy;

    athena_camera2d_init(&cam, SCREEN_W, SCREEN_H);
    athena_camera2d_matrix(&cam, 1, 1, &m);
    CHECK(athena_affine_classify(&m) == ATHENA_VIEW_IDENTITY,
        "a new camera is the identity: %f %f %f %f %f %f", m.xx, m.xy, m.yx, m.yy, m.tx, m.ty);

    /* Moving right by 100 shifts the world left by 100. */
    cam.x += 100.0f;
    athena_camera2d_world_to_screen(&cam, 420.0f, 224.0f, &sx, &sy);
    CHECK(NEAR(sx, 320.0f, 1e-4f) && NEAR(sy, 224.0f, 1e-4f), "world to screen %f %f", sx, sy);

    /* Zoom about the anchor, rotation, round trips. */
    cam.zoom_x = cam.zoom_y = 2.0f;
    cam.rotation = 0.7f;
    cam.pixel_snap = false;
    athena_camera2d_world_to_screen(&cam, cam.x, cam.y, &sx, &sy);
    CHECK(NEAR(sx, 320.0f, 1e-3f) && NEAR(sy, 224.0f, 1e-3f), "position stays at the anchor %f %f", sx, sy);
    athena_camera2d_world_to_screen(&cam, 123.0f, -45.0f, &sx, &sy);
    athena_camera2d_screen_to_world(&cam, sx, sy, &wx, &wy);
    CHECK(NEAR(wx, 123.0f, 1e-2f) && NEAR(wy, -45.0f, 1e-2f), "round trip %f %f", wx, wy);
    /* Turned 90 degrees clockwise, the camera's right is the world's down. */
    cam.rotation = (float)M_PI / 2.0f;
    athena_camera2d_world_to_screen(&cam, cam.x, cam.y + 10.0f, &sx, &sy);
    CHECK(NEAR(sx, 340.0f, 1e-3f) && NEAR(sy, 224.0f, 1e-3f),
        "turned 90 degrees, world down shows right: %f %f", sx, sy);

    /* Visible area halves at zoom 2. */
    cam.rotation = 0.0f;
    athena_camera2d_visible_rect(&cam, &vis);
    CHECK(NEAR(vis.x1 - vis.x0, 320.0f, 1e-3f) && NEAR(vis.y1 - vis.y0, 224.0f, 1e-3f) &&
        NEAR(vis.x0, cam.x - 160.0f, 1e-3f), "visible %f %f %f %f", vis.x0, vis.y0, vis.x1, vis.y1);
    CHECK(athena_camera2d_rect_visible(&cam, vis.x1 - 1.0f, vis.y0, 5.0f, 5.0f), "edge box visible");
    CHECK(!athena_camera2d_rect_visible(&cam, vis.x1 + 1.0f, vis.y0, 5.0f, 5.0f), "box past the edge");

    /* Pixel snap rounds the translation only. */
    athena_camera2d_init(&cam, SCREEN_W, SCREEN_H);
    cam.x += 0.4f;
    athena_camera2d_matrix(&cam, 1, 1, &m);
    CHECK(m.tx == 0.0f, "snapped translation %f", m.tx);
    cam.x += 0.2f;
    athena_camera2d_matrix(&cam, 1, 1, &m);
    CHECK(m.tx == -1.0f, "snapped translation %f", m.tx);
    cam.pixel_snap = false;
    athena_camera2d_matrix(&cam, 1, 1, &m);
    CHECK(NEAR(m.tx, -0.6f, 1e-4f), "unsnapped translation %f", m.tx);
}

static void test_viewport_and_parallax(void) {
    AthenaCamera2D cam;
    AthenaAffine2D m;
    float sx, sy;

    athena_camera2d_init(&cam, SCREEN_W, SCREEN_H);
    cam.viewport_x = 320.0f;
    cam.viewport_y = 0.0f;
    cam.viewport_w = 320.0f;
    cam.viewport_h = 448.0f;
    cam.x = 160.0f;
    cam.y = 224.0f;
    /* The right half of the screen shows world (0, 0)-(320, 448). */
    athena_camera2d_world_to_screen(&cam, 0.0f, 0.0f, &sx, &sy);
    CHECK(NEAR(sx, 320.0f, 1e-4f) && NEAR(sy, 0.0f, 1e-4f), "viewport origin %f %f", sx, sy);

    /* Parallax 0 is the screen pose; 0.5 moves half as much. */
    cam.x += 200.0f;
    cam.zoom_x = cam.zoom_y = 2.0f;
    athena_camera2d_matrix(&cam, 0.0f, 0.0f, &m);
    CHECK(m.xx == 1.0f && m.yy == 1.0f && NEAR(m.tx, 320.0f, 1e-4f) && NEAR(m.ty, 0.0f, 1e-4f),
        "parallax 0: %f %f %f %f", m.xx, m.yy, m.tx, m.ty);
    cam.zoom_x = cam.zoom_y = 1.0f;
    athena_camera2d_matrix(&cam, 0.5f, 1.0f, &m);
    CHECK(NEAR(m.tx, 320.0f - 100.0f, 1e-4f) && NEAR(m.ty, 0.0f, 1e-4f), "parallax 0.5: %f %f", m.tx, m.ty);
}

/* Runs `seconds` of updates at `fps` with a fixed target. */
static void run(AthenaCamera2D *cam, float seconds, float fps) {
    int frames = (int)(seconds * fps + 0.5f);
    for (int i = 0; i < frames; i++)
        athena_camera2d_update(cam, 1.0f / fps);
}

static void test_follow(void) {
    AthenaCamera2D a, b;
    float expected, sx, sy;

    /* Rigid follow. */
    athena_camera2d_init(&a, SCREEN_W, SCREEN_H);
    athena_camera2d_follow(&a, 0, 0);
    athena_camera2d_set_target(&a, 1000.0f, 50.0f);
    athena_camera2d_update(&a, 1.0f / 60.0f);
    CHECK(a.x == 1000.0f && a.y == 50.0f, "rigid %f %f", a.x, a.y);

    /* Smoothing does not depend on the frame rate. */
    athena_camera2d_init(&a, SCREEN_W, SCREEN_H);
    athena_camera2d_init(&b, SCREEN_W, SCREEN_H);
    athena_camera2d_follow(&a, 5, 5);
    athena_camera2d_follow(&b, 5, 5);
    athena_camera2d_set_target(&a, 1320.0f, 224.0f);
    athena_camera2d_set_target(&b, 1320.0f, 224.0f);
    run(&a, 1.0f, 60.0f);
    run(&b, 1.0f, 30.0f);
    expected = 1320.0f - 1000.0f * expf(-5.0f);
    CHECK(NEAR(a.x, expected, 0.05f) && NEAR(b.x, expected, 0.05f),
        "60 fps %f, 30 fps %f, expected %f", a.x, b.x, expected);

    /* Dead zone: 64 px wide at zoom 1 lets the target move 32 px each way. */
    athena_camera2d_init(&a, SCREEN_W, SCREEN_H);
    athena_camera2d_follow(&a, 0, 0);
    a.deadzone_w = 64.0f;
    athena_camera2d_set_target(&a, 320.0f + 30.0f, 224.0f);
    athena_camera2d_update(&a, 1.0f / 60.0f);
    CHECK(a.x == 320.0f, "inside the dead zone %f", a.x);
    athena_camera2d_set_target(&a, 320.0f + 50.0f, 224.0f);
    athena_camera2d_update(&a, 1.0f / 60.0f);
    CHECK(NEAR(a.x, 338.0f, 1e-4f), "pushed by the edge %f", a.x);
    athena_camera2d_set_target(&a, 320.0f, 224.0f);
    athena_camera2d_update(&a, 1.0f / 60.0f);
    CHECK(NEAR(a.x, 338.0f, 1e-4f), "back inside: holds %f", a.x);
    /* At zoom 2 the same screen dead zone is half as wide in the world. */
    a.zoom_x = 2.0f;
    athena_camera2d_set_target(&a, 338.0f + 20.0f, 224.0f);
    athena_camera2d_update(&a, 1.0f / 60.0f);
    CHECK(NEAR(a.x, 342.0f, 1e-4f), "zoomed dead zone %f", a.x);

    /* Lookahead leads the motion and holds when the target stops. */
    athena_camera2d_init(&a, SCREEN_W, SCREEN_H);
    athena_camera2d_follow(&a, 0, 0);
    a.lookahead_x = 40.0f;
    a.lookahead_lerp = 10.0f;
    for (int i = 0; i < 120; i++) {
        athena_camera2d_set_target(&a, 320.0f + i * 2.0f, 224.0f);
        athena_camera2d_update(&a, 1.0f / 60.0f);
    }
    CHECK(a.look_x > 39.0f && NEAR(a.x, 320.0f + 119 * 2.0f + a.look_x, 1e-3f), "lookahead %f x %f", a.look_x, a.x);
    run(&a, 1.0f, 60.0f);
    CHECK(a.look_x > 39.0f, "held lookahead %f", a.look_x);

    /* The lookahead is in screen pixels: a quarter of the world distance at zoom 4. */
    athena_camera2d_init(&a, SCREEN_W, SCREEN_H);
    athena_camera2d_follow(&a, 0, 0);
    a.zoom_x = a.zoom_y = 4.0f;
    a.lookahead_x = 40.0f;
    a.lookahead_lerp = 10.0f;
    for (int i = 0; i < 120; i++) {
        athena_camera2d_set_target(&a, 320.0f + i * 2.0f, 224.0f);
        athena_camera2d_update(&a, 1.0f / 60.0f);
    }
    athena_camera2d_world_to_screen(&a, 320.0f + 119 * 2.0f, 224.0f, &sx, &sy);
    CHECK(NEAR(a.x - (320.0f + 119 * 2.0f), a.look_x / 4.0f, 1e-3f) && sx < 320.0f - 39.0f && sx > 320.0f - 40.5f,
        "zoomed lookahead: %f world, target at screen x %f", a.x - (320.0f + 119 * 2.0f), sx);

    /* The dead zone is a screen box: turned 90 degrees, its width is along world y. */
    athena_camera2d_init(&a, SCREEN_W, SCREEN_H);
    athena_camera2d_follow(&a, 0, 0);
    a.rotation = (float)M_PI / 2.0f;
    a.deadzone_w = 64.0f;
    athena_camera2d_set_target(&a, 320.0f, 224.0f + 30.0f);
    athena_camera2d_update(&a, 1.0f / 60.0f);
    CHECK(NEAR(a.x, 320.0f, 1e-3f) && NEAR(a.y, 224.0f, 1e-3f), "turned dead zone holds %f %f", a.x, a.y);
    athena_camera2d_set_target(&a, 330.0f, 224.0f + 30.0f);
    athena_camera2d_update(&a, 1.0f / 60.0f);
    CHECK(NEAR(a.x, 330.0f, 1e-3f) && NEAR(a.y, 224.0f, 1e-3f),
        "no dead zone across the screen: follows world x %f %f", a.x, a.y);
    athena_camera2d_set_target(&a, 330.0f, 224.0f + 50.0f);
    athena_camera2d_update(&a, 1.0f / 60.0f);
    CHECK(NEAR(a.y, 224.0f + 18.0f, 1e-3f), "pushed along the screen's x: y %f", a.y);

    /* Offset, snap, unfollow. */
    athena_camera2d_init(&a, SCREEN_W, SCREEN_H);
    athena_camera2d_follow(&a, 2, 2);
    a.offset_y = -30.0f;
    athena_camera2d_set_target(&a, 500.0f, 500.0f);
    athena_camera2d_snap(&a);
    CHECK(a.x == 500.0f && a.y == 470.0f, "snap %f %f", a.x, a.y);
    athena_camera2d_unfollow(&a);
    athena_camera2d_update(&a, 0.1f);
    CHECK(a.x == 500.0f && !a.following, "unfollow");
}

static void test_bounds(void) {
    AthenaCamera2D cam;

    athena_camera2d_init(&cam, SCREEN_W, SCREEN_H);
    athena_camera2d_set_bounds(&cam, 0, 0, 2000, 1000);
    cam.x = -500.0f;
    cam.y = 5000.0f;
    athena_camera2d_clamp(&cam);
    CHECK(cam.x == 320.0f && cam.y == 1000.0f - 224.0f, "clamped %f %f", cam.x, cam.y);
    /* Zooming in shows less, so the camera can go closer to the edges. */
    cam.zoom_x = cam.zoom_y = 2.0f;
    cam.x = -500.0f;
    athena_camera2d_clamp(&cam);
    CHECK(cam.x == 160.0f, "zoomed clamp %f", cam.x);
    /* A world smaller than the view is centered. */
    cam.zoom_x = cam.zoom_y = 1.0f;
    athena_camera2d_set_bounds(&cam, 100, 100, 200, 100);
    athena_camera2d_clamp(&cam);
    CHECK(cam.x == 200.0f && cam.y == 150.0f, "centered %f %f", cam.x, cam.y);
    /* Rotated 90 degrees the view is 448 wide in the world. */
    athena_camera2d_set_bounds(&cam, 0, 0, 2000, 2000);
    cam.rotation = (float)M_PI / 2.0f;
    cam.x = 0.0f;
    athena_camera2d_clamp(&cam);
    CHECK(NEAR(cam.x, 224.0f, 1e-2f), "rotated clamp %f", cam.x);
    /* Ignoring the rotation, the unturned 640 wide view is clamped instead. */
    cam.rotation = 0.8f;
    cam.x = 0.0f;
    athena_camera2d_clamp(&cam);
    CHECK(cam.x > 320.0f + 1.0f, "turned 0.8: pushed past 320 (%f)", cam.x);
    cam.bounds_ignore_rotation = true;
    cam.x = 0.0f;
    athena_camera2d_clamp(&cam);
    CHECK(cam.x == 320.0f, "ignoring the rotation: 320 (%f)", cam.x);
    cam.bounds_ignore_rotation = false;

    /* The follow goal is clamped too: no lag when the target comes back. */
    athena_camera2d_init(&cam, SCREEN_W, SCREEN_H);
    athena_camera2d_set_bounds(&cam, 0, 0, 2000, 448);
    athena_camera2d_follow(&cam, 0, 0);
    athena_camera2d_set_target(&cam, -300.0f, 224.0f);
    athena_camera2d_update(&cam, 1.0f / 60.0f);
    CHECK(cam.x == 320.0f && cam.goal_x == 320.0f, "goal clamped %f %f", cam.x, cam.goal_x);
}

static void test_zones(void) {
    AthenaCamera2D cam;
    AthenaCamera2DZone zones[2] = {
        { .x = 0, .y = 0, .w = 640, .h = 448 },
        { .x = 640, .y = 0, .w = 1280, .h = 448, .zoom = 2.0f },
    };

    athena_camera2d_init(&cam, SCREEN_W, SCREEN_H);
    athena_camera2d_follow(&cam, 0, 0);
    athena_camera2d_set_zones(&cam, zones, 2, 0.5f);
    athena_camera2d_set_target(&cam, 100.0f, 100.0f);
    athena_camera2d_update(&cam, 1.0f / 60.0f);
    CHECK(cam.zone == 0 && (cam.events & ATHENA_CAMERA2D_EVENT_ZONE) && cam.x == 320.0f,
        "zone 0: %d x %f", cam.zone, cam.x);
    /* Entering zone 1 glides for 0.5 s, then clamps to it at its zoom. */
    athena_camera2d_set_target(&cam, 700.0f, 100.0f);
    athena_camera2d_update(&cam, 1.0f / 60.0f);
    CHECK(cam.zone == 1 && cam.zone_blend.active, "zone 1 blending");
    CHECK(cam.x > 320.0f && cam.x < 820.0f, "mid glide %f", cam.x);
    run(&cam, 0.6f, 60.0f);
    CHECK(!cam.zone_blend.active && cam.zoom_x == 2.0f && NEAR(cam.x, 640.0f + 160.0f, 1e-3f),
        "after glide x %f zoom %f", cam.x, cam.zoom_x);
    /* Between zones the last one holds. */
    athena_camera2d_set_target(&cam, 5000.0f, 100.0f);
    athena_camera2d_update(&cam, 1.0f / 60.0f);
    CHECK(cam.zone == 1 && NEAR(cam.x, 1920.0f - 160.0f, 1e-3f), "outside: zone %d x %f", cam.zone, cam.x);
}

static void test_timed(void) {
    AthenaCamera2D cam;
    uint32_t seen = 0;

    athena_camera2d_init(&cam, SCREEN_W, SCREEN_H);
    athena_camera2d_zoom_to(&cam, 4.0f, 1.0f);
    run(&cam, 0.5f, 60.0f);
    CHECK(NEAR(cam.zoom_x, 2.0f, 1e-3f), "geometric midpoint %f", cam.zoom_x);
    for (int i = 0; i < 40; i++) {
        athena_camera2d_update(&cam, 1.0f / 60.0f);
        seen |= cam.events;
    }
    CHECK(cam.zoom_x == 4.0f && cam.zoom_y == 4.0f && (seen & ATHENA_CAMERA2D_EVENT_ZOOM), "zoom done");

    /* A pan suspends the follow; the follow resumes from where it ends. */
    athena_camera2d_init(&cam, SCREEN_W, SCREEN_H);
    athena_camera2d_follow(&cam, 0, 0);
    athena_camera2d_set_target(&cam, 320.0f, 224.0f);
    athena_camera2d_pan_to(&cam, 1000.0f, 224.0f, 0.5f);
    seen = 0;
    for (int i = 0; i < 40 && !(seen & ATHENA_CAMERA2D_EVENT_PAN); i++) {
        athena_camera2d_update(&cam, 1.0f / 60.0f);
        CHECK(cam.x >= 320.0f && cam.x <= 1000.0f, "panning %f", cam.x);
        seen |= cam.events;
    }
    CHECK(cam.x == 1000.0f && (seen & ATHENA_CAMERA2D_EVENT_PAN), "pan done %f", cam.x);
    athena_camera2d_update(&cam, 1.0f / 60.0f);
    CHECK(cam.x == 320.0f, "follow resumed %f", cam.x);

    /* Fade, flash, letterbox. */
    athena_camera2d_init(&cam, SCREEN_W, SCREEN_H);
    athena_camera2d_fade(&cam, 0x000000, 128.0f, 1.0f);
    athena_camera2d_flash(&cam, 0xFFFFFF, 100.0f, 0.5f);
    athena_camera2d_letterbox(&cam, 0.1f, 0.5f);
    run(&cam, 0.25f, 60.0f);
    CHECK(NEAR(cam.fade_alpha, 32.0f, 1e-2f) && NEAR(cam.flash_alpha, 50.0f, 1e-2f) &&
        NEAR(cam.letterbox, 0.05f, 1e-4f), "effects mid: %f %f %f", cam.fade_alpha, cam.flash_alpha, cam.letterbox);
    seen = 0;
    for (int i = 0; i < 50; i++) {
        athena_camera2d_update(&cam, 1.0f / 60.0f);
        seen |= cam.events;
    }
    CHECK(cam.fade_alpha == 128.0f && cam.flash_alpha == 0.0f && cam.letterbox == 0.1f &&
        (seen & ATHENA_CAMERA2D_EVENT_FADE) && (seen & ATHENA_CAMERA2D_EVENT_FLASH) &&
        (seen & ATHENA_CAMERA2D_EVENT_LETTERBOX), "effects done");
    athena_camera2d_fade(&cam, 0, 0.0f, 0.0f);
    CHECK(cam.fade_alpha == 0.0f && !cam.fade_tween.active, "instant fade");
}

static void test_shake(void) {
    AthenaCamera2D cam;
    float peak = 0.0f;
    uint32_t seen = 0;
    AthenaAffine2D m;

    athena_camera2d_init(&cam, SCREEN_W, SCREEN_H);
    athena_camera2d_shake(&cam, 8.0f, 0.5f, 25.0f, 0.05f);
    for (int i = 0; i < 29; i++) {
        athena_camera2d_update(&cam, 1.0f / 60.0f);
        float d = fmaxf(fabsf(cam.shake_x), fabsf(cam.shake_y));
        CHECK(d <= 8.0f && fabsf(cam.shake_angle) <= 0.05f, "shake bounded %f %f", d, cam.shake_angle);
        if (d > peak) peak = d;
    }
    CHECK(peak > 1.0f, "shakes %f", peak);
    athena_camera2d_matrix(&cam, 1, 1, &m);
    for (int i = 0; i < 5; i++) {
        athena_camera2d_update(&cam, 1.0f / 60.0f);
        seen |= cam.events;
    }
    CHECK(cam.shake_x == 0.0f && cam.shake_y == 0.0f && cam.shake_angle == 0.0f &&
        (seen & ATHENA_CAMERA2D_EVENT_SHAKE), "shake ends");
    athena_camera2d_matrix(&cam, 1, 1, &m);
    CHECK(athena_affine_classify(&m) == ATHENA_VIEW_IDENTITY, "back to the identity");

    /* A weak shake does not cut a strong one short. */
    athena_camera2d_shake(&cam, 10.0f, 1.0f, 25.0f, 0.0f);
    athena_camera2d_shake(&cam, 2.0f, 0.1f, 25.0f, 0.0f);
    CHECK(cam.shake_intensity == 10.0f && cam.shake_duration == 1.0f, "kept the strong shake");
}

static void test_auto_zoom_and_blend(void) {
    AthenaCamera2D a, b, out;

    athena_camera2d_init(&a, SCREEN_W, SCREEN_H);
    athena_camera2d_follow(&a, 0, 0);
    a.auto_zoom = true;
    a.zoom_lerp = 0.0f;
    a.auto_zoom_margin = 20.0f;
    athena_camera2d_set_target_box(&a, 500.0f, 224.0f, 1200.0f, 100.0f);
    athena_camera2d_update(&a, 1.0f / 60.0f);
    CHECK(NEAR(a.zoom_x, 0.5f, 1e-4f), "fit clamped to min: %f", a.zoom_x);
    athena_camera2d_set_target_box(&a, 500.0f, 224.0f, 300.0f, 100.0f);
    athena_camera2d_update(&a, 1.0f / 60.0f);
    CHECK(NEAR(a.zoom_x, 2.0f, 1e-4f), "fit clamped to max: %f", a.zoom_x);
    athena_camera2d_set_target_box(&a, 500.0f, 224.0f, 800.0f, 100.0f);
    athena_camera2d_update(&a, 1.0f / 60.0f);
    CHECK(NEAR(a.zoom_x, 600.0f / 800.0f, 1e-4f), "fit %f", a.zoom_x);

    athena_camera2d_init(&a, SCREEN_W, SCREEN_H);
    athena_camera2d_init(&b, SCREEN_W, SCREEN_H);
    b.x = 1000.0f;
    b.zoom_x = b.zoom_y = 4.0f;
    a.rotation = 3.0f;
    b.rotation = -3.0f;
    b.viewport_x = 320.0f;
    b.viewport_w = 320.0f;
    b.viewport_h = 448.0f;
    athena_camera2d_blend(&a, &b, 0.0f, &out);
    CHECK(out.x == a.x && out.zoom_x == 1.0f && NEAR(out.rotation, 3.0f, 1e-5f) &&
        out.viewport_x == 0.0f && out.viewport_w == 640.0f, "blend start");
    athena_camera2d_blend(&a, &b, 0.5f, &out);
    CHECK(NEAR(out.zoom_x, 2.0f, 1e-4f) && NEAR(out.viewport_w, 480.0f, 1e-3f) &&
        NEAR(out.rotation, 3.0f + (2.0f * (float)M_PI - 6.0f) * 0.5f, 1e-4f),
        "blend middle: zoom %f width %f rotation %f", out.zoom_x, out.viewport_w, out.rotation);
    athena_camera2d_blend(&a, &b, 1.0f, &out);
    CHECK(out.x == 1000.0f && NEAR(out.zoom_x, 4.0f, 1e-4f), "blend end");
}

static void test_speed_zoom_trauma_kick_zones(void) {
    AthenaCamera2D cam;
    float peak = 0.0f;

    /* Speed zoom: at rest the maximum, fast the minimum, in between a blend. */
    athena_camera2d_init(&cam, SCREEN_W, SCREEN_H);
    athena_camera2d_follow(&cam, 0, 0);
    cam.speed_zoom = true;
    cam.zoom_lerp = 0.0f;
    cam.speed_zoom_min = 0.5f;
    cam.speed_zoom_max = 1.0f;
    cam.speed_zoom_speed = 300.0f;
    for (int i = 0; i < 120; i++) {
        athena_camera2d_set_target(&cam, 320.0f + i * 10.0f, 224.0f);   /* 600 units/s */
        athena_camera2d_update(&cam, 1.0f / 60.0f);
    }
    CHECK(NEAR(cam.zoom_x, 0.5f, 1e-3f), "fast: minimum zoom %f", cam.zoom_x);
    for (int i = 0; i < 240; i++)
        athena_camera2d_update(&cam, 1.0f / 60.0f);
    CHECK(NEAR(cam.zoom_x, 1.0f, 1e-2f), "stopped: back to the maximum %f", cam.zoom_x);

    /* Trauma: bounded by intensity * trauma^2, decays to zero. */
    athena_camera2d_init(&cam, SCREEN_W, SCREEN_H);
    athena_camera2d_add_trauma(&cam, 0.5f, 20.0f, 0.1f, 1.0f, 25.0f);
    athena_camera2d_add_trauma(&cam, 0.3f, 20.0f, 0.1f, 1.0f, 25.0f);
    CHECK(NEAR(cam.trauma, 0.8f, 1e-5f), "trauma adds %f", cam.trauma);
    athena_camera2d_add_trauma(&cam, 5.0f, 20.0f, 0.1f, 1.0f, 25.0f);
    CHECK(cam.trauma == 1.0f, "trauma capped at 1");
    for (int i = 0; i < 30; i++) {
        athena_camera2d_update(&cam, 1.0f / 60.0f);
        CHECK(fabsf(cam.shake_x) <= 20.0f && fabsf(cam.shake_angle) <= 0.1f, "trauma bounded");
        peak = fmaxf(peak, fabsf(cam.shake_x));
    }
    CHECK(peak > 1.0f && NEAR(cam.trauma, 0.5f, 1e-3f), "trauma shakes and decays: %f %f", peak, cam.trauma);
    for (int i = 0; i < 31; i++)
        athena_camera2d_update(&cam, 1.0f / 60.0f);
    CHECK(cam.trauma == 0.0f && fabsf(cam.shake_x) < 1e-3f, "trauma gone");

    /* Kick: pushed at once, springs back to nothing; kicks add up. */
    athena_camera2d_kick(&cam, 10.0f, -4.0f, 0.2f);
    athena_camera2d_update(&cam, 0.0f);
    CHECK(cam.shake_x == 10.0f && cam.shake_y == -4.0f, "kick %f %f", cam.shake_x, cam.shake_y);
    athena_camera2d_update(&cam, 0.1f);
    CHECK(NEAR(cam.shake_x, 2.5f, 1e-4f), "half way: a quarter left %f", cam.shake_x);
    athena_camera2d_kick(&cam, 10.0f, 0.0f, 0.2f);
    athena_camera2d_update(&cam, 0.0f);
    CHECK(NEAR(cam.shake_x, 12.5f, 1e-4f), "kicks add up %f", cam.shake_x);
    athena_camera2d_update(&cam, 0.25f);
    CHECK(cam.shake_x == 0.0f, "kick over");
    athena_camera2d_kick(&cam, 10.0f, 0.0f, 1.0f);
    athena_camera2d_add_trauma(&cam, 1.0f, 20.0f, 0.1f, 1.0f, 25.0f);
    athena_camera2d_stop_shake(&cam);
    athena_camera2d_update(&cam, 0.01f);
    CHECK(cam.shake_x == 0.0f && cam.trauma == 0.0f, "stopShake stops everything");

    /* Zones with their own offset and smoothing; the previous zone is kept. */
    {
        AthenaCamera2DZone zones[2] = {
            { 0, 0, 2000, 1000, 0, false, 0, 0, false, 0, 0 },
            { 2000, 0, 2000, 1000, 0, true, 0, 0, true, 0, -50 },
        };
        athena_camera2d_init(&cam, SCREEN_W, SCREEN_H);
        athena_camera2d_follow(&cam, 2, 2);
        athena_camera2d_set_zones(&cam, zones, 2, 0);
        athena_camera2d_set_target(&cam, 1000.0f, 500.0f);
        athena_camera2d_snap(&cam);
        athena_camera2d_update(&cam, 1.0f / 60.0f);
        CHECK(cam.zone == 0 && cam.previous_zone == -1 && cam.y == 500.0f, "zone 0");
        athena_camera2d_set_target(&cam, 3000.0f, 500.0f);
        athena_camera2d_update(&cam, 1.0f / 60.0f);
        CHECK(cam.zone == 1 && cam.previous_zone == 0, "previous zone %d", cam.previous_zone);
        CHECK(cam.x == 3000.0f && cam.y == 450.0f, "zone lerp 0 (rigid) and offset: %f %f", cam.x, cam.y);
    }
}

int main(void) {
    test_view();
    test_identity_and_conversions();
    test_viewport_and_parallax();
    test_follow();
    test_bounds();
    test_zones();
    test_timed();
    test_shake();
    test_auto_zoom_and_blend();
    test_speed_zoom_trauma_kick_zones();
    if (failures) {
        printf("camera2d_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("camera2d_test: ok\n");
    return 0;
}
