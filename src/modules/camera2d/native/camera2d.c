#include <math.h>
#include <stddef.h>
#include <string.h>

#include <athena/camera2d.h>

/*
 * Camera behavior (athena/camera2d.h): math on the struct only, no GS
 * access, so tests/host runs it as is.
 */

#define CAMERA_MIN_ZOOM 1.0e-4f
/* Target speed (world units per second) below which the lookahead holds. */
#define CAMERA_LOOK_MIN_SPEED 1.0f
/* How fast the smoothed target speed (speed zoom) follows the real one. */
#define CAMERA_SPEED_LERP 4.0f
/* Trauma keeps its own noise: a new timed shake does not make it jump. */
#define CAMERA_TRAUMA_SEED 0x7A11u
#define CAMERA_PI 3.14159265358979f

static float clampf(float value, float low, float high)
{
	return value < low ? low : (value > high ? high : value);
}

static float smooth01(float t)
{
	return t * t * (3.0f - 2.0f * t);
}

/* Fraction of the remaining distance covered in `dt` at `rate` per second. */
static float damp(float rate, float dt)
{
	if (rate <= 0.0f)
		return 1.0f;
	return 1.0f - expf(-rate * dt);
}

/* Advances a tween; returns its progress (0..1) and sets *done when it ends. */
static float tween_advance(AthenaCamera2DTween *tween, float dt, bool *done)
{
	tween->time += dt;
	if (tween->time >= tween->duration) {
		tween->time = tween->duration;
		tween->active = false;
		*done = true;
		return 1.0f;
	}
	*done = false;
	return tween->time / tween->duration;
}

static void tween_start(AthenaCamera2DTween *tween, float from, float to,
	float duration)
{
	tween->active = true;
	tween->from = from;
	tween->to = to;
	tween->time = 0.0f;
	tween->duration = duration;
}

void athena_camera2d_init(AthenaCamera2D *cam, float screen_w, float screen_h)
{
	memset(cam, 0, sizeof(*cam));
	cam->zoom_x = 1.0f;
	cam->zoom_y = 1.0f;
	cam->anchor_x = 0.5f;
	cam->anchor_y = 0.5f;
	cam->screen_w = screen_w;
	cam->screen_h = screen_h;
	cam->x = screen_w * 0.5f;
	cam->y = screen_h * 0.5f;
	cam->pixel_snap = true;
	cam->lookahead_lerp = 4.0f;
	cam->auto_zoom_min = 0.5f;
	cam->auto_zoom_max = 2.0f;
	cam->auto_zoom_margin = 32.0f;
	cam->zoom_lerp = 4.0f;
	cam->zone = -1;
	cam->previous_zone = -1;
	cam->shake_frequency = 25.0f;
	cam->speed_zoom_min = 0.8f;
	cam->speed_zoom_max = 1.0f;
	cam->speed_zoom_speed = 300.0f;
	cam->trauma_decay = 1.0f;
	cam->trauma_intensity = 16.0f;
	cam->trauma_frequency = 25.0f;
}

/* The zone in force, or NULL. */
static const AthenaCamera2DZone *camera_zone(const AthenaCamera2D *cam)
{
	if (cam->zone >= 0 && cam->zone < cam->zone_count)
		return &cam->zones[cam->zone];
	return NULL;
}

/* The follow offset and smoothing in force: the zone's when it sets them. */
static void camera_follow_offset(const AthenaCamera2D *cam, float *x, float *y)
{
	const AthenaCamera2DZone *zone = camera_zone(cam);

	*x = zone && zone->has_offset ? zone->offset_x : cam->offset_x;
	*y = zone && zone->has_offset ? zone->offset_y : cam->offset_y;
}

static void camera_follow_lerp(const AthenaCamera2D *cam, float *x, float *y)
{
	const AthenaCamera2DZone *zone = camera_zone(cam);

	*x = zone && zone->has_lerp ? zone->lerp_x : cam->lerp_x;
	*y = zone && zone->has_lerp ? zone->lerp_y : cam->lerp_y;
}

void athena_camera2d_set_screen(AthenaCamera2D *cam, float screen_w, float screen_h)
{
	cam->screen_w = screen_w;
	cam->screen_h = screen_h;
}

void athena_camera2d_viewport(const AthenaCamera2D *cam, AthenaRect2D *out)
{
	if (cam->viewport_w <= 0.0f || cam->viewport_h <= 0.0f) {
		out->x0 = 0.0f;
		out->y0 = 0.0f;
		out->x1 = cam->screen_w;
		out->y1 = cam->screen_h;
		return;
	}
	out->x0 = cam->viewport_x;
	out->y0 = cam->viewport_y;
	out->x1 = cam->viewport_x + cam->viewport_w;
	out->y1 = cam->viewport_y + cam->viewport_h;
}

/*
 * World distances from the position to the edges of what the viewport
 * shows along the world axes, at a given zoom: left, top, right, bottom.
 * With a rotation, of the bounding box of the rotated view.
 */
static void camera_extents(const AthenaCamera2D *cam, float zoom_x,
	float zoom_y, float ext[4])
{
	AthenaRect2D vp;
	float w, h, left, top, right, bottom;
	float c, s;
	float lx[4], ly[4];
	float min_x, min_y, max_x, max_y;
	int i;

	athena_camera2d_viewport(cam, &vp);
	w = vp.x1 - vp.x0;
	h = vp.y1 - vp.y0;
	left = cam->anchor_x * w / zoom_x;
	right = (1.0f - cam->anchor_x) * w / zoom_x;
	top = cam->anchor_y * h / zoom_y;
	bottom = (1.0f - cam->anchor_y) * h / zoom_y;
	if (cam->rotation == 0.0f || cam->bounds_ignore_rotation) {
		ext[0] = left;
		ext[1] = top;
		ext[2] = right;
		ext[3] = bottom;
		return;
	}
	c = cosf(cam->rotation);
	s = sinf(cam->rotation);
	lx[0] = -left;  ly[0] = -top;
	lx[1] = right;  ly[1] = -top;
	lx[2] = right;  ly[2] = bottom;
	lx[3] = -left;  ly[3] = bottom;
	min_x = max_x = lx[0] * c - ly[0] * s;
	min_y = max_y = lx[0] * s + ly[0] * c;
	for (i = 1; i < 4; i++) {
		float wx = lx[i] * c - ly[i] * s;
		float wy = lx[i] * s + ly[i] * c;
		if (wx < min_x) min_x = wx;
		if (wx > max_x) max_x = wx;
		if (wy < min_y) min_y = wy;
		if (wy > max_y) max_y = wy;
	}
	ext[0] = -min_x;
	ext[1] = -min_y;
	ext[2] = max_x;
	ext[3] = max_y;
}

/* The bounds in force: the active zone's, else the camera's. */
static const AthenaRect2D *camera_active_bounds(const AthenaCamera2D *cam,
	AthenaRect2D *zone_rect)
{
	if (cam->zone >= 0 && cam->zone < cam->zone_count) {
		const AthenaCamera2DZone *zone = &cam->zones[cam->zone];

		zone_rect->x0 = zone->x;
		zone_rect->y0 = zone->y;
		zone_rect->x1 = zone->x + zone->w;
		zone_rect->y1 = zone->y + zone->h;
		return zone_rect;
	}
	return cam->has_bounds ? &cam->bounds : NULL;
}

/* A position whose view spans [v - low, v + high], kept within [b0, b1]. */
static float clamp_axis(float v, float low, float high, float b0, float b1)
{
	/* Bounds smaller than the view: center the view on them. */
	if (b1 - b0 <= low + high)
		return (b0 + b1) * 0.5f - (high - low) * 0.5f;
	return clampf(v, b0 + low, b1 - high);
}

static void camera_clamp_point(const AthenaCamera2D *cam, float zoom_x,
	float zoom_y, float *x, float *y)
{
	AthenaRect2D zone_rect;
	const AthenaRect2D *bounds = camera_active_bounds(cam, &zone_rect);
	float ext[4];

	if (!bounds)
		return;
	camera_extents(cam, zoom_x, zoom_y, ext);
	*x = clamp_axis(*x, ext[0], ext[2], bounds->x0, bounds->x1);
	*y = clamp_axis(*y, ext[1], ext[3], bounds->y0, bounds->y1);
}

void athena_camera2d_clamp(AthenaCamera2D *cam)
{
	camera_clamp_point(cam, cam->zoom_x, cam->zoom_y, &cam->x, &cam->y);
}

void athena_camera2d_follow(AthenaCamera2D *cam, float lerp_x, float lerp_y)
{
	cam->following = true;
	cam->lerp_x = lerp_x;
	cam->lerp_y = lerp_y;
	cam->goal_x = cam->x;
	cam->goal_y = cam->y;
	cam->look_x = 0.0f;
	cam->look_y = 0.0f;
	cam->has_last_target = false;
}

void athena_camera2d_unfollow(AthenaCamera2D *cam)
{
	cam->following = false;
	cam->has_target = false;
	cam->has_last_target = false;
}

void athena_camera2d_set_target(AthenaCamera2D *cam, float x, float y)
{
	athena_camera2d_set_target_box(cam, x, y, 0.0f, 0.0f);
}

void athena_camera2d_set_target_box(AthenaCamera2D *cam, float x, float y,
	float w, float h)
{
	cam->has_target = true;
	cam->target_x = x;
	cam->target_y = y;
	cam->target_w = w;
	cam->target_h = h;
}

/* Zoom fitting the target box in the viewport, within the auto zoom limits. */
static float camera_fit_zoom(const AthenaCamera2D *cam)
{
	AthenaRect2D vp;
	float avail_w, avail_h, fit_x, fit_y;

	athena_camera2d_viewport(cam, &vp);
	avail_w = vp.x1 - vp.x0 - 2.0f * cam->auto_zoom_margin;
	avail_h = vp.y1 - vp.y0 - 2.0f * cam->auto_zoom_margin;
	if (avail_w < 1.0f) avail_w = 1.0f;
	if (avail_h < 1.0f) avail_h = 1.0f;
	fit_x = avail_w / (cam->target_w > 1.0f ? cam->target_w : 1.0f);
	fit_y = avail_h / (cam->target_h > 1.0f ? cam->target_h : 1.0f);
	return clampf(fit_x < fit_y ? fit_x : fit_y, cam->auto_zoom_min,
		cam->auto_zoom_max);
}

/*
 * Zoom the camera is heading to, when nothing animates it: auto zoom (the
 * targets' box), else speed zoom (from the zone's zoom or the maximum),
 * else the zone's zoom.
 */
static bool camera_zoom_goal(const AthenaCamera2D *cam, float *zoom)
{
	const AthenaCamera2DZone *zone = camera_zone(cam);
	bool zoned = zone && zone->zoom > 0.0f;

	if (cam->auto_zoom && cam->following && cam->has_target) {
		*zoom = camera_fit_zoom(cam);
		return true;
	}
	if (cam->speed_zoom && cam->following && cam->has_target) {
		float base = zoned ? zone->zoom : cam->speed_zoom_max;
		float ratio = cam->speed_zoom_max > 0.0f ?
			cam->speed_zoom_min / cam->speed_zoom_max : 1.0f;
		float k = cam->speed_zoom_speed > 0.0f ?
			clampf(cam->target_speed / cam->speed_zoom_speed, 0.0f, 1.0f) : 0.0f;

		*zoom = base * (1.0f + (ratio - 1.0f) * k);
		return true;
	}
	if (zoned) {
		*zoom = zone->zoom;
		return true;
	}
	return false;
}

/* Where the follow places the camera this frame, dead zone applied. */
static void camera_follow_goal(AthenaCamera2D *cam, float dt)
{
	float desired_x, desired_y;
	float offset_x, offset_y;

	camera_follow_offset(cam, &offset_x, &offset_y);
	if (cam->has_last_target && dt > 0.0f) {
		float vx = (cam->target_x - cam->last_target_x) / dt;
		float vy = (cam->target_y - cam->last_target_y) / dt;
		float k = damp(cam->lookahead_lerp, dt);

		/* Smoothed, so one frame of a teleport does not swing the zoom. */
		cam->target_speed += (sqrtf(vx * vx + vy * vy) - cam->target_speed) *
			damp(CAMERA_SPEED_LERP, dt);

		/* Looks ahead of the motion and keeps looking there once it stops. */
		if (cam->lookahead_x > 0.0f && fabsf(vx) > CAMERA_LOOK_MIN_SPEED)
			cam->look_x += ((vx > 0.0f ? cam->lookahead_x : -cam->lookahead_x) -
				cam->look_x) * k;
		if (cam->lookahead_y > 0.0f && fabsf(vy) > CAMERA_LOOK_MIN_SPEED)
			cam->look_y += ((vy > 0.0f ? cam->lookahead_y : -cam->lookahead_y) -
				cam->look_y) * k;
	}
	cam->last_target_x = cam->target_x;
	cam->last_target_y = cam->target_y;
	cam->has_last_target = true;

	/* The lookahead is in screen pixels, like the dead zone: zoom keeps it. */
	desired_x = cam->target_x + offset_x + cam->look_x / cam->zoom_x;
	desired_y = cam->target_y + offset_y + cam->look_y / cam->zoom_y;
	if (cam->deadzone_w <= 0.0f && cam->deadzone_h <= 0.0f) {
		cam->goal_x = desired_x;
		cam->goal_y = desired_y;
	} else {
		/*
		 * The dead zone is a box on screen: measure how far the target is
		 * from the goal along the screen's axes (rotation and zoom
		 * applied), and move the goal by what sticks out of the box.
		 */
		float c = cosf(cam->rotation), s = sinf(cam->rotation);
		float dx = desired_x - cam->goal_x, dy = desired_y - cam->goal_y;
		float sx = cam->zoom_x * (c * dx + s * dy);
		float sy = cam->zoom_y * (c * dy - s * dx);
		float half_w = cam->deadzone_w * 0.5f, half_h = cam->deadzone_h * 0.5f;
		float ex, ey;

		ex = sx > half_w ? sx - half_w : (sx < -half_w ? sx + half_w : 0.0f);
		ey = sy > half_h ? sy - half_h : (sy < -half_h ? sy + half_h : 0.0f);
		/* Back to world units, turned by the camera. */
		ex /= cam->zoom_x;
		ey /= cam->zoom_y;
		cam->goal_x += ex * c - ey * s;
		cam->goal_y += ex * s + ey * c;
	}
	/* A goal past the bounds would make the camera lag when coming back. */
	camera_clamp_point(cam, cam->zoom_x, cam->zoom_y, &cam->goal_x, &cam->goal_y);
}

void athena_camera2d_snap(AthenaCamera2D *cam)
{
	float zoom;

	cam->look_x = 0.0f;
	cam->look_y = 0.0f;
	cam->zone_blend.active = false;
	if (!cam->zoom_tween.active && camera_zoom_goal(cam, &zoom)) {
		cam->zoom_x = zoom;
		cam->zoom_y = zoom;
	}
	if (cam->following && cam->has_target) {
		cam->has_last_target = false;
		/* No dead zone on a snap: center on the target. */
		float offset_x, offset_y;

		camera_follow_offset(cam, &offset_x, &offset_y);
		cam->goal_x = cam->target_x + offset_x;
		cam->goal_y = cam->target_y + offset_y;
		camera_clamp_point(cam, cam->zoom_x, cam->zoom_y, &cam->goal_x,
			&cam->goal_y);
		cam->x = cam->goal_x;
		cam->y = cam->goal_y;
	}
	athena_camera2d_clamp(cam);
}

void athena_camera2d_set_bounds(AthenaCamera2D *cam, float x, float y,
	float w, float h)
{
	cam->has_bounds = true;
	cam->bounds.x0 = x;
	cam->bounds.y0 = y;
	cam->bounds.x1 = x + w;
	cam->bounds.y1 = y + h;
}

void athena_camera2d_clear_bounds(AthenaCamera2D *cam)
{
	cam->has_bounds = false;
}

void athena_camera2d_set_zones(AthenaCamera2D *cam,
	const AthenaCamera2DZone *zones, int count, float transition)
{
	if (count < 0)
		count = 0;
	if (count > ATHENA_CAMERA2D_MAX_ZONES)
		count = ATHENA_CAMERA2D_MAX_ZONES;
	if (count > 0)
		memcpy(cam->zones, zones, (size_t)count * sizeof(*zones));
	cam->zone_count = count;
	cam->zone = -1;
	cam->previous_zone = -1;
	cam->zone_transition = transition > 0.0f ? transition : 0.0f;
	cam->zone_blend.active = false;
}

/* Index of the zone holding (x, y), preferring the current one; -1 if none. */
static int camera_find_zone(const AthenaCamera2D *cam, float x, float y)
{
	int i;

	if (cam->zone >= 0 && cam->zone < cam->zone_count) {
		const AthenaCamera2DZone *z = &cam->zones[cam->zone];
		if (x >= z->x && x < z->x + z->w && y >= z->y && y < z->y + z->h)
			return cam->zone;
	}
	for (i = 0; i < cam->zone_count; i++) {
		const AthenaCamera2DZone *z = &cam->zones[i];
		if (x >= z->x && x < z->x + z->w && y >= z->y && y < z->y + z->h)
			return i;
	}
	return -1;
}

void athena_camera2d_shake(AthenaCamera2D *cam, float intensity,
	float duration, float frequency, float rotation)
{
	float remaining = 0.0f;

	if (intensity <= 0.0f || duration <= 0.0f)
		return;
	/* A weaker shake does not cut a stronger one short. */
	if (cam->shake_time < cam->shake_duration) {
		float k = 1.0f - cam->shake_time / cam->shake_duration;
		remaining = cam->shake_intensity * k * k;
	}
	if (intensity < remaining)
		return;
	cam->shake_intensity = intensity;
	cam->shake_duration = duration;
	cam->shake_frequency = frequency > 0.0f ? frequency : 25.0f;
	cam->shake_rotation = rotation;
	cam->shake_time = 0.0f;
	cam->shake_seed++;
}

void athena_camera2d_stop_shake(AthenaCamera2D *cam)
{
	cam->shake_time = cam->shake_duration = 0.0f;
	cam->trauma = cam->trauma_time = 0.0f;
	cam->kick_time = cam->kick_duration = 0.0f;
	cam->shake_x = cam->shake_y = cam->shake_angle = 0.0f;
}

void athena_camera2d_zoom_to(AthenaCamera2D *cam, float zoom, float duration)
{
	if (zoom < CAMERA_MIN_ZOOM)
		zoom = CAMERA_MIN_ZOOM;
	if (duration <= 0.0f) {
		cam->zoom_tween.active = false;
		cam->zoom_x = cam->zoom_y = zoom;
		return;
	}
	tween_start(&cam->zoom_tween, cam->zoom_x, zoom, duration);
}

void athena_camera2d_pan_to(AthenaCamera2D *cam, float x, float y, float duration)
{
	if (duration <= 0.0f) {
		cam->pan_tween.active = false;
		cam->x = cam->goal_x = x;
		cam->y = cam->goal_y = y;
		return;
	}
	tween_start(&cam->pan_tween, 0.0f, 1.0f, duration);
	cam->pan_from_x = cam->x;
	cam->pan_from_y = cam->y;
	cam->pan_to_x = x;
	cam->pan_to_y = y;
}

void athena_camera2d_fade(AthenaCamera2D *cam, uint32_t rgb, float alpha,
	float duration)
{
	alpha = clampf(alpha, 0.0f, 128.0f);
	cam->fade_rgb = rgb & 0xFFFFFF;
	if (duration <= 0.0f) {
		cam->fade_tween.active = false;
		cam->fade_alpha = alpha;
		return;
	}
	tween_start(&cam->fade_tween, cam->fade_alpha, alpha, duration);
}

void athena_camera2d_flash(AthenaCamera2D *cam, uint32_t rgb, float alpha,
	float duration)
{
	cam->flash_rgb = rgb & 0xFFFFFF;
	alpha = clampf(alpha, 0.0f, 128.0f);
	if (duration <= 0.0f) {
		cam->flash_tween.active = false;
		cam->flash_alpha = 0.0f;
		return;
	}
	cam->flash_alpha = alpha;
	tween_start(&cam->flash_tween, alpha, 0.0f, duration);
}

void athena_camera2d_letterbox(AthenaCamera2D *cam, float amount, float duration)
{
	amount = clampf(amount, 0.0f, 0.5f);
	if (duration <= 0.0f) {
		cam->letterbox_tween.active = false;
		cam->letterbox = amount;
		return;
	}
	tween_start(&cam->letterbox_tween, cam->letterbox, amount, duration);
}

/* A value in [-1, 1] for each lattice point of each shake channel. */
static float noise_lattice(uint32_t n)
{
	n ^= n >> 15;
	n *= 0x2C1B3C6Du;
	n ^= n >> 12;
	n *= 0x297A2D39u;
	n ^= n >> 15;
	return (float)(n & 0xFFFFFFu) / 8388607.5f - 1.0f;
}

/* Smooth 1D value noise in [-1, 1]; `channel` picks an independent curve. */
static float noise_1d(uint32_t seed, uint32_t channel, float x)
{
	float cell = floorf(x);
	float f = x - cell;
	uint32_t i = (uint32_t)(int32_t)cell;
	uint32_t base = seed * 0x9E3779B1u + channel * 0x85EBCA77u;
	float a = noise_lattice(base + i);
	float b = noise_lattice(base + i + 1u);

	return a + (b - a) * smooth01(f);
}

/* The timed shake, trauma and kick add up into shake_x/y/angle. */
static void camera_update_shake(AthenaCamera2D *cam, float dt)
{
	float x = 0.0f, y = 0.0f, angle = 0.0f;
	float k, falloff, phase;

	if (cam->shake_time < cam->shake_duration) {
		cam->shake_time += dt;
		if (cam->shake_time >= cam->shake_duration) {
			cam->shake_time = cam->shake_duration = 0.0f;
			cam->events |= ATHENA_CAMERA2D_EVENT_SHAKE;
		} else {
			k = 1.0f - cam->shake_time / cam->shake_duration;
			falloff = k * k;
			/* Phases stay small: shakes last seconds, not hours. */
			phase = cam->shake_time * cam->shake_frequency;
			x += cam->shake_intensity * falloff * noise_1d(cam->shake_seed, 0, phase);
			y += cam->shake_intensity * falloff * noise_1d(cam->shake_seed, 1, phase);
			angle += cam->shake_rotation * falloff * noise_1d(cam->shake_seed, 2, phase);
		}
	}

	if (cam->trauma > 0.0f) {
		float t2 = cam->trauma * cam->trauma;

		cam->trauma_time += dt;
		phase = cam->trauma_time * cam->trauma_frequency;
		x += cam->trauma_intensity * t2 * noise_1d(CAMERA_TRAUMA_SEED, 3, phase);
		y += cam->trauma_intensity * t2 * noise_1d(CAMERA_TRAUMA_SEED, 4, phase);
		angle += cam->trauma_rotation * t2 * noise_1d(CAMERA_TRAUMA_SEED, 5, phase);
		cam->trauma -= cam->trauma_decay * dt;
		if (cam->trauma <= 0.0f) {
			cam->trauma = 0.0f;
			cam->trauma_time = 0.0f;
		}
	}

	if (cam->kick_time < cam->kick_duration) {
		cam->kick_time += dt;
		if (cam->kick_time >= cam->kick_duration) {
			cam->kick_time = cam->kick_duration = 0.0f;
		} else {
			k = 1.0f - cam->kick_time / cam->kick_duration;
			x += cam->kick_x * k * k;
			y += cam->kick_y * k * k;
		}
	}

	cam->shake_x = x;
	cam->shake_y = y;
	cam->shake_angle = angle;
}

void athena_camera2d_add_trauma(AthenaCamera2D *cam, float amount,
	float intensity, float rotation, float decay, float frequency)
{
	cam->trauma = clampf(cam->trauma + amount, 0.0f, 1.0f);
	cam->trauma_intensity = intensity;
	cam->trauma_rotation = rotation;
	cam->trauma_decay = decay > 0.0f ? decay : 1.0f;
	cam->trauma_frequency = frequency > 0.0f ? frequency : 25.0f;
}

void athena_camera2d_kick(AthenaCamera2D *cam, float dx, float dy, float duration)
{
	if (duration <= 0.0f)
		return;
	/* A kick during a kick adds to what is left of it. */
	if (cam->kick_time < cam->kick_duration) {
		float k = 1.0f - cam->kick_time / cam->kick_duration;
		dx += cam->kick_x * k * k;
		dy += cam->kick_y * k * k;
	}
	cam->kick_x = dx;
	cam->kick_y = dy;
	cam->kick_time = 0.0f;
	cam->kick_duration = duration;
}

static void camera_update_effects(AthenaCamera2D *cam, float dt)
{
	bool done;
	float t;

	if (cam->fade_tween.active) {
		t = tween_advance(&cam->fade_tween, dt, &done);
		cam->fade_alpha = cam->fade_tween.from +
			(cam->fade_tween.to - cam->fade_tween.from) * t;
		if (done)
			cam->events |= ATHENA_CAMERA2D_EVENT_FADE;
	}
	if (cam->flash_tween.active) {
		t = tween_advance(&cam->flash_tween, dt, &done);
		cam->flash_alpha = cam->flash_tween.from * (1.0f - t);
		if (done)
			cam->events |= ATHENA_CAMERA2D_EVENT_FLASH;
	}
	if (cam->letterbox_tween.active) {
		t = tween_advance(&cam->letterbox_tween, dt, &done);
		cam->letterbox = cam->letterbox_tween.from +
			(cam->letterbox_tween.to - cam->letterbox_tween.from) * smooth01(t);
		if (done)
			cam->events |= ATHENA_CAMERA2D_EVENT_LETTERBOX;
	}
}

static void camera_update_zone(AthenaCamera2D *cam)
{
	float px, py;
	int found;

	if (cam->zone_count == 0)
		return;
	/* The target decides the room; without one, the camera itself. */
	if (cam->following && cam->has_target) {
		px = cam->target_x;
		py = cam->target_y;
	} else {
		px = cam->x;
		py = cam->y;
	}
	found = camera_find_zone(cam, px, py);
	/* Between zones the last one stays in force. */
	if (found < 0 || found == cam->zone)
		return;
	if (cam->zone >= 0 && cam->zone_transition > 0.0f) {
		tween_start(&cam->zone_blend, 0.0f, 1.0f, cam->zone_transition);
		cam->zone_from_x = cam->x;
		cam->zone_from_y = cam->y;
		cam->zone_from_zoom_x = cam->zoom_x;
		cam->zone_from_zoom_y = cam->zoom_y;
	} else if (cam->zones[found].zoom > 0.0f && !cam->zoom_tween.active) {
		/* A cut: the room's zoom applies at once, like its bounds. */
		cam->zoom_x = cam->zoom_y = cam->zones[found].zoom;
	}
	cam->previous_zone = cam->zone;
	cam->zone = found;
	cam->events |= ATHENA_CAMERA2D_EVENT_ZONE;
}

void athena_camera2d_update(AthenaCamera2D *cam, float dt)
{
	bool done;
	float t, zoom;
	bool blending;

	cam->events = 0;
	if (!(dt > 0.0f))
		dt = 0.0f;

	camera_update_shake(cam, dt);
	camera_update_effects(cam, dt);
	camera_update_zone(cam);
	blending = cam->zone_blend.active;

	/* Zoom: a timed zoom wins; otherwise auto or zone zoom eases in. */
	if (cam->zoom_tween.active) {
		t = tween_advance(&cam->zoom_tween, dt, &done);
		/* Geometric: zooming 1 -> 4 looks as steady as 4 -> 1. */
		zoom = done ? cam->zoom_tween.to : cam->zoom_tween.from *
			powf(cam->zoom_tween.to / cam->zoom_tween.from, smooth01(t));
		cam->zoom_x = cam->zoom_y = zoom;
		if (done)
			cam->events |= ATHENA_CAMERA2D_EVENT_ZOOM;
	} else if (camera_zoom_goal(cam, &zoom) && !blending) {
		float k = damp(cam->zoom_lerp, dt);
		cam->zoom_x += (zoom - cam->zoom_x) * k;
		cam->zoom_y += (zoom - cam->zoom_y) * k;
	}

	if (cam->pan_tween.active) {
		t = smooth01(tween_advance(&cam->pan_tween, dt, &done));
		cam->x = cam->pan_from_x + (cam->pan_to_x - cam->pan_from_x) * t;
		cam->y = cam->pan_from_y + (cam->pan_to_y - cam->pan_from_y) * t;
		if (done) {
			cam->events |= ATHENA_CAMERA2D_EVENT_PAN;
			/* The follow resumes from here, smoothly. */
			cam->goal_x = cam->x;
			cam->goal_y = cam->y;
		}
	} else if (cam->following && cam->has_target) {
		camera_follow_goal(cam, dt);
		if (!blending) {
			float lerp_x, lerp_y;

			camera_follow_lerp(cam, &lerp_x, &lerp_y);
			cam->x += (cam->goal_x - cam->x) * damp(lerp_x, dt);
			cam->y += (cam->goal_y - cam->y) * damp(lerp_y, dt);
		}
	}

	if (blending) {
		/*
		 * Zone change: glide from the old pose to where the new room puts
		 * the camera, at the room's zoom. The goal is taken again from the
		 * target, as the follow clamped it at the old zoom.
		 */
		float goal_x = cam->x, goal_y = cam->y;
		float zoom_x = cam->zone_from_zoom_x, zoom_y = cam->zone_from_zoom_y;

		if (camera_zoom_goal(cam, &zoom))
			zoom_x = zoom_y = zoom;
		if (cam->following && cam->has_target) {
			float offset_x, offset_y;

			camera_follow_offset(cam, &offset_x, &offset_y);
			goal_x = cam->target_x + offset_x + cam->look_x / zoom_x;
			goal_y = cam->target_y + offset_y + cam->look_y / zoom_y;
		}
		camera_clamp_point(cam, zoom_x, zoom_y, &goal_x, &goal_y);
		t = smooth01(tween_advance(&cam->zone_blend, dt, &done));
		cam->x = cam->zone_from_x + (goal_x - cam->zone_from_x) * t;
		cam->y = cam->zone_from_y + (goal_y - cam->zone_from_y) * t;
		cam->zoom_x = cam->zone_from_zoom_x + (zoom_x - cam->zone_from_zoom_x) * t;
		cam->zoom_y = cam->zone_from_zoom_y + (zoom_y - cam->zone_from_zoom_y) * t;
		/* Mid-glide the view may cross the old room: no clamp yet. */
		if (!done)
			return;
		cam->goal_x = goal_x;
		cam->goal_y = goal_y;
	}
	athena_camera2d_clamp(cam);
}

void athena_camera2d_matrix(const AthenaCamera2D *cam, float parallax_x,
	float parallax_y, AthenaAffine2D *out)
{
	AthenaRect2D vp;
	float w, h, anchor_x, anchor_y;
	float pos_x, pos_y, zoom_x, zoom_y, rotation, c, s;

	athena_camera2d_viewport(cam, &vp);
	w = vp.x1 - vp.x0;
	h = vp.y1 - vp.y0;
	anchor_x = vp.x0 + cam->anchor_x * w;
	anchor_y = vp.y0 + cam->anchor_y * h;

	/*
	 * Parallax scales the pose's distance from the screen pose: the one
	 * showing the world's origin at the viewport's corner at zoom 1.
	 */
	pos_x = cam->x;
	pos_y = cam->y;
	zoom_x = cam->zoom_x;
	zoom_y = cam->zoom_y;
	rotation = cam->rotation;
	if (parallax_x != 1.0f) {
		float home = cam->anchor_x * w;
		pos_x = home + (cam->x - home) * parallax_x;
		zoom_x = 1.0f + (cam->zoom_x - 1.0f) * parallax_x;
	}
	if (parallax_y != 1.0f) {
		float home = cam->anchor_y * h;
		pos_y = home + (cam->y - home) * parallax_y;
		zoom_y = 1.0f + (cam->zoom_y - 1.0f) * parallax_y;
	}
	if (parallax_x != 1.0f || parallax_y != 1.0f)
		rotation *= (parallax_x + parallax_y) * 0.5f;
	if (zoom_x < CAMERA_MIN_ZOOM) zoom_x = CAMERA_MIN_ZOOM;
	if (zoom_y < CAMERA_MIN_ZOOM) zoom_y = CAMERA_MIN_ZOOM;
	rotation += cam->shake_angle;

	/* screen = anchor + shake + Z * R(-rotation) * (world - pos) */
	if (rotation == 0.0f) {
		c = 1.0f;
		s = 0.0f;
	} else {
		c = cosf(rotation);
		s = sinf(rotation);
	}
	out->xx = zoom_x * c;
	out->xy = zoom_x * s;
	out->yx = -zoom_y * s;
	out->yy = zoom_y * c;
	out->tx = anchor_x + cam->shake_x - (out->xx * pos_x + out->xy * pos_y);
	out->ty = anchor_y + cam->shake_y - (out->yx * pos_x + out->yy * pos_y);
	if (cam->pixel_snap) {
		out->tx = floorf(out->tx + 0.5f);
		out->ty = floorf(out->ty + 0.5f);
	}
}

void athena_camera2d_world_to_screen(const AthenaCamera2D *cam, float x,
	float y, float *screen_x, float *screen_y)
{
	AthenaAffine2D m;

	athena_camera2d_matrix(cam, 1.0f, 1.0f, &m);
	athena_affine_apply(&m, x, y, screen_x, screen_y);
}

void athena_camera2d_screen_to_world(const AthenaCamera2D *cam, float x,
	float y, float *world_x, float *world_y)
{
	AthenaAffine2D m, inverse;

	athena_camera2d_matrix(cam, 1.0f, 1.0f, &m);
	/* Zoom is kept above zero, so the matrix always inverts. */
	if (!athena_affine_invert(&m, &inverse)) {
		*world_x = cam->x;
		*world_y = cam->y;
		return;
	}
	athena_affine_apply(&inverse, x, y, world_x, world_y);
}

void athena_camera2d_visible_rect(const AthenaCamera2D *cam, AthenaRect2D *out)
{
	AthenaAffine2D m, inverse;
	AthenaRect2D vp;

	athena_camera2d_viewport(cam, &vp);
	athena_camera2d_matrix(cam, 1.0f, 1.0f, &m);
	if (!athena_affine_invert(&m, &inverse)) {
		out->x0 = out->x1 = cam->x;
		out->y0 = out->y1 = cam->y;
		return;
	}
	athena_affine_bounds(&inverse, &vp, out);
}

bool athena_camera2d_rect_visible(const AthenaCamera2D *cam, float x, float y,
	float w, float h)
{
	AthenaRect2D visible;

	athena_camera2d_visible_rect(cam, &visible);
	return x + w >= visible.x0 && x <= visible.x1 &&
		y + h >= visible.y0 && y <= visible.y1;
}

static float lerpf(float a, float b, float t)
{
	return a + (b - a) * t;
}

void athena_camera2d_blend(const AthenaCamera2D *from, const AthenaCamera2D *to,
	float t, AthenaCamera2D *out)
{
	AthenaRect2D a, b;
	float turn;

	t = clampf(t, 0.0f, 1.0f);
	athena_camera2d_viewport(from, &a);
	athena_camera2d_viewport(to, &b);
	*out = *to;
	out->x = lerpf(from->x, to->x, t);
	out->y = lerpf(from->y, to->y, t);
	/* Geometric zoom, as zoom_to. */
	out->zoom_x = from->zoom_x * powf(to->zoom_x / from->zoom_x, t);
	out->zoom_y = from->zoom_y * powf(to->zoom_y / from->zoom_y, t);
	/* The short way round. */
	turn = fmodf(to->rotation - from->rotation, 2.0f * CAMERA_PI);
	if (turn > CAMERA_PI) turn -= 2.0f * CAMERA_PI;
	if (turn < -CAMERA_PI) turn += 2.0f * CAMERA_PI;
	out->rotation = from->rotation + turn * t;
	out->anchor_x = lerpf(from->anchor_x, to->anchor_x, t);
	out->anchor_y = lerpf(from->anchor_y, to->anchor_y, t);
	out->viewport_x = lerpf(a.x0, b.x0, t);
	out->viewport_y = lerpf(a.y0, b.y0, t);
	out->viewport_w = lerpf(a.x1 - a.x0, b.x1 - b.x0, t);
	out->viewport_h = lerpf(a.y1 - a.y0, b.y1 - b.y0, t);
}
