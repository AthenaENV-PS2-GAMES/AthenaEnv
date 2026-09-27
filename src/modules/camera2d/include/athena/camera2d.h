#ifndef ATHENA_CAMERA2D_H
#define ATHENA_CAMERA2D_H

#include <stdbool.h>
#include <stdint.h>

#include <athena/graphics/view.h>

/*
 * 2D camera: a position, zoom and rotation in a viewport, turned into the 2D
 * view of the graphics core (athena/graphics/view.h), which Draw, Image,
 * Font and TileMap apply in C. The behavior (follow with smoothing, dead
 * zone and lookahead, bounds, zones, shake, timed zoom and pan, fades and
 * letterbox) is plain math on this struct, with no GS access, so it runs and
 * is tested on a host. camera2d_gs.c applies a camera to the GS.
 *
 * A C game loop:
 *
 *     AthenaCamera2D cam;
 *     athena_camera2d_init(&cam, 640, 448);
 *     athena_camera2d_follow(&cam, 8.0f, 8.0f);
 *     for (;;) {
 *         ...update the player...
 *         athena_camera2d_set_target(&cam, player.x, player.y);
 *         athena_camera2d_update(&cam, dt);
 *         clearScreen(color);
 *         athena_camera2d_begin(&cam, 1.0f, 1.0f);
 *         ...draw the world...
 *         athena_camera2d_end(&cam);
 *         ...draw the HUD in screen space...
 *         flipScreen();
 *     }
 *
 * Units: positions in world units, viewport and dead zone in screen pixels,
 * rotation in radians (clockwise on screen, y pointing down), durations in
 * seconds. Smoothing speeds are rates in 1/s: each second the distance left
 * shrinks by exp(-rate), whatever the frame rate. 0 means no smoothing.
 */

#define ATHENA_CAMERA2D_MAX_ZONES 32

/* Largest coordinate accepted: keeps float math and the GS range sane. */
#define ATHENA_CAMERA2D_MAX_COORD 1.0e7f

/* Bits of AthenaCamera2D.events: what finished during the last update. */
#define ATHENA_CAMERA2D_EVENT_ZOOM      (1u << 0)
#define ATHENA_CAMERA2D_EVENT_PAN       (1u << 1)
#define ATHENA_CAMERA2D_EVENT_FADE      (1u << 2)
#define ATHENA_CAMERA2D_EVENT_FLASH     (1u << 3)
#define ATHENA_CAMERA2D_EVENT_LETTERBOX (1u << 4)
#define ATHENA_CAMERA2D_EVENT_SHAKE     (1u << 5)
/* The zone holding the target changed (see `zone`). */
#define ATHENA_CAMERA2D_EVENT_ZONE      (1u << 6)

typedef struct {
	float x, y, w, h;
	/* Zoom while the target is in the zone; <= 0 keeps the camera's own. */
	float zoom;
	/* Follow smoothing and target offset while in the zone, when set. */
	bool has_lerp;
	float lerp_x, lerp_y;
	bool has_offset;
	float offset_x, offset_y;
} AthenaCamera2DZone;

/* A value animated from `from` to `to` over `duration` seconds. */
typedef struct {
	bool active;
	float from, to;
	float time, duration;
} AthenaCamera2DTween;

typedef struct {
	/* World point shown at the anchor of the viewport. */
	float x, y;
	/* Screen pixels per world unit; > 0. */
	float zoom_x, zoom_y;
	float rotation;

	/*
	 * Viewport in screen pixels. A width or height <= 0 means the whole
	 * framebuffer, whose size comes from athena_camera2d_set_screen().
	 */
	float viewport_x, viewport_y, viewport_w, viewport_h;
	float screen_w, screen_h;
	/* Anchor inside the viewport, 0..1: (0.5, 0.5) is the center. */
	float anchor_x, anchor_y;
	/* Rounds the final translation to whole pixels (no shimmer on pixel art). */
	bool pixel_snap;

	/* The camera never shows outside these world bounds (unless smaller). */
	bool has_bounds;
	AthenaRect2D bounds;
	/*
	 * Bounds and zones clamp the view as if it were not turned. By default
	 * a turned view is clamped by its bounding box, so nothing past the
	 * bounds shows, but near an edge the camera is pushed inward; ignoring
	 * the rotation keeps it on its target and lets the view's corners show
	 * past the bounds.
	 */
	bool bounds_ignore_rotation;

	/* Follow. The target is set every frame with athena_camera2d_set_target(). */
	bool following;
	bool has_target;
	float target_x, target_y;
	/* Size of the box holding every target (0 for one): used by auto zoom. */
	float target_w, target_h;
	float lerp_x, lerp_y;
	/*
	 * Screen pixels around the anchor where the target moves freely: a box
	 * of the screen, so it turns with the camera and keeps its size on
	 * screen at any zoom.
	 */
	float deadzone_w, deadzone_h;
	/* World offset added to the target. */
	float offset_x, offset_y;
	/*
	 * Screen pixels to look ahead of the target's motion (in the direction
	 * it moves in the world), and how fast the lookahead turns around.
	 */
	float lookahead_x, lookahead_y;
	float lookahead_lerp;
	/*
	 * Follow state: the smoothed lookahead (screen pixels) and the goal
	 * the camera eases to.
	 */
	float look_x, look_y;
	float goal_x, goal_y;
	float last_target_x, last_target_y;
	bool has_last_target;

	/* Auto zoom: fits the target box (several targets) in the viewport. */
	bool auto_zoom;
	float auto_zoom_min, auto_zoom_max, auto_zoom_margin;
	float zoom_lerp;

	/*
	 * Zoom by speed: from speed_zoom_max at rest to speed_zoom_min once the
	 * target moves at speed_zoom_speed world units per second or faster.
	 * In a zone with a zoom, that zoom takes the place of the maximum.
	 */
	bool speed_zoom;
	float speed_zoom_min, speed_zoom_max, speed_zoom_speed;
	/* The target's speed, smoothed. */
	float target_speed;

	/* Zones (rooms): the one holding the target replaces the bounds. */
	AthenaCamera2DZone zones[ATHENA_CAMERA2D_MAX_ZONES];
	int zone_count;
	int zone;
	/* The zone before the last change (-1 for none): ZONE event argument. */
	int previous_zone;
	float zone_transition;
	/* Blend from the pose before a zone change, over zone_transition. */
	AthenaCamera2DTween zone_blend;
	float zone_from_x, zone_from_y, zone_from_zoom_x, zone_from_zoom_y;

	/* Timed zoom (zoomTo) and pan (panTo); a pan suspends the follow. */
	AthenaCamera2DTween zoom_tween;
	AthenaCamera2DTween pan_tween;
	float pan_from_x, pan_from_y, pan_to_x, pan_to_y;

	/* Shake: offsets from smooth noise, fading out quadratically. */
	float shake_intensity, shake_rotation, shake_frequency;
	float shake_time, shake_duration;
	uint32_t shake_seed;

	/*
	 * Trauma (0..1): impacts add to it, it decays at trauma_decay per
	 * second, and shakes by up to trauma_intensity pixels (and
	 * trauma_rotation radians) times its square.
	 */
	float trauma, trauma_decay, trauma_intensity, trauma_rotation;
	float trauma_frequency, trauma_time;

	/* Kick: a screen offset that springs back over kick_duration. */
	float kick_x, kick_y, kick_time, kick_duration;

	/* What the shake, trauma and kick add to the view this frame. */
	float shake_x, shake_y, shake_angle;

	/* Effects drawn over the viewport by athena_camera2d_end(). */
	uint32_t fade_rgb;
	float fade_alpha;           /* 0..128, as PS2 colors */
	AthenaCamera2DTween fade_tween;
	uint32_t flash_rgb;
	float flash_alpha;
	AthenaCamera2DTween flash_tween;
	/* Height of each letterbox bar, as a fraction of the viewport (0..0.5). */
	float letterbox;
	AthenaCamera2DTween letterbox_tween;

	/*
	 * athena_camera2d_end() draws, over the effects, what drives the
	 * camera: dead zone, anchor, target, goal, lookahead, bounds and zones.
	 */
	bool debug_draw;

	/* ATHENA_CAMERA2D_EVENT_* bits set by the last update. */
	uint32_t events;
} AthenaCamera2D;

/* A camera at zoom 1 showing (0, 0)-(screen_w, screen_h): the identity view. */
void athena_camera2d_init(AthenaCamera2D *cam, float screen_w, float screen_h);

/* Framebuffer size, for full-screen viewports. */
void athena_camera2d_set_screen(AthenaCamera2D *cam, float screen_w, float screen_h);

/* The viewport in screen pixels, resolved against the screen size. */
void athena_camera2d_viewport(const AthenaCamera2D *cam, AthenaRect2D *out);

/*
 * Starts following a target, with smoothing rates per axis. The goal starts
 * at the camera's position; athena_camera2d_snap() jumps to the target.
 */
void athena_camera2d_follow(AthenaCamera2D *cam, float lerp_x, float lerp_y);
void athena_camera2d_unfollow(AthenaCamera2D *cam);
/* Target of this frame: a point, or the center and size of a box of targets. */
void athena_camera2d_set_target(AthenaCamera2D *cam, float x, float y);
void athena_camera2d_set_target_box(AthenaCamera2D *cam, float x, float y,
	float w, float h);
/* Moves to the follow goal at once: no smoothing, lookahead reset. */
void athena_camera2d_snap(AthenaCamera2D *cam);

void athena_camera2d_set_bounds(AthenaCamera2D *cam, float x, float y,
	float w, float h);
void athena_camera2d_clear_bounds(AthenaCamera2D *cam);
/* Clamps the position to the active bounds (zone or camera bounds). */
void athena_camera2d_clamp(AthenaCamera2D *cam);

/* Replaces the zones (at most ATHENA_CAMERA2D_MAX_ZONES are kept). */
void athena_camera2d_set_zones(AthenaCamera2D *cam,
	const AthenaCamera2DZone *zones, int count, float transition);

void athena_camera2d_shake(AthenaCamera2D *cam, float intensity,
	float duration, float frequency, float rotation);
/*
 * Adds trauma (clamped to 1); the other arguments configure it: at full
 * trauma the view moves up to intensity pixels and turns up to
 * rotation radians; it decays by decay per second.
 */
void athena_camera2d_add_trauma(AthenaCamera2D *cam, float amount,
	float intensity, float rotation, float decay, float frequency);
/* Pushes the view by (dx, dy) screen pixels, springing back over duration seconds. */
void athena_camera2d_kick(AthenaCamera2D *cam, float dx, float dy, float duration);
/* Stops the shake, the trauma and the kick. */
void athena_camera2d_stop_shake(AthenaCamera2D *cam);

/* Timed changes; a duration <= 0 applies at once. */
void athena_camera2d_zoom_to(AthenaCamera2D *cam, float zoom, float duration);
void athena_camera2d_pan_to(AthenaCamera2D *cam, float x, float y, float duration);
/* Fades the overlay to `rgb` (0xBBGGRR) at `alpha` (0..128). */
void athena_camera2d_fade(AthenaCamera2D *cam, uint32_t rgb, float alpha,
	float duration);
/* Shows `rgb` at `alpha` and fades it out over `duration`. */
void athena_camera2d_flash(AthenaCamera2D *cam, uint32_t rgb, float alpha,
	float duration);
void athena_camera2d_letterbox(AthenaCamera2D *cam, float amount, float duration);

/*
 * Advances everything timed by `dt` seconds and moves the camera: zones,
 * follow, auto zoom, bounds, shake, effects. Sets `events`.
 */
void athena_camera2d_update(AthenaCamera2D *cam, float dt);

/*
 * World-to-screen transform, including the shake. A parallax factor of 1 is
 * the world; 0 is the screen (the viewport at zoom 1, no rotation); between
 * them, a background layer that moves slower than the world.
 */
void athena_camera2d_matrix(const AthenaCamera2D *cam, float parallax_x,
	float parallax_y, AthenaAffine2D *out);

void athena_camera2d_world_to_screen(const AthenaCamera2D *cam, float x,
	float y, float *screen_x, float *screen_y);
void athena_camera2d_screen_to_world(const AthenaCamera2D *cam, float x,
	float y, float *world_x, float *world_y);
/* World box the viewport shows (its bounding box when rotated). */
void athena_camera2d_visible_rect(const AthenaCamera2D *cam, AthenaRect2D *out);
bool athena_camera2d_rect_visible(const AthenaCamera2D *cam, float x, float y,
	float w, float h);

/*
 * `out` = `to` with its pose (position, zoom, rotation, viewport, anchor)
 * moved `t` (0..1) of the way from `from`'s: a transition between cameras.
 */
void athena_camera2d_blend(const AthenaCamera2D *from, const AthenaCamera2D *to,
	float t, AthenaCamera2D *out);

/*
 * GS side (camera2d_gs.c). begin() saves the view and clip rectangle, then
 * sets the camera's (clipped to its viewport); end() draws its effects
 * (fade, flash, letterbox) over the viewport and restores them. Pairs nest
 * up to ATHENA_CAMERA2D_MAX_DEPTH deep; begin() returns false beyond.
 */
#define ATHENA_CAMERA2D_MAX_DEPTH 8

bool athena_camera2d_begin(const AthenaCamera2D *cam, float parallax_x,
	float parallax_y);
void athena_camera2d_end(const AthenaCamera2D *cam);
/* begin() without the camera: identity view, clip set to (x, y, w, h). */
bool athena_camera2d_begin_screen(float x, float y, float w, float h);
void athena_camera2d_end_screen(void);
/* Pairs still open, and closing them all (after an error mid-draw). */
int athena_camera2d_depth(void);
void athena_camera2d_unwind(void);

#endif
