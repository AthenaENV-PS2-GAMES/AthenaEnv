#include <math.h>
#include <stdint.h>
#include <string.h>

#include <ath_env.h>

#include <athena/loop.h>
#include <athena/camera2d.h>

#include "ath_camera2d.h"

/*
 * Camera2D.Camera wraps an AthenaCamera2D (athena/camera2d.h). Every live
 * camera is in a list that a native Loop system updates after the game's
 * update; the same system begins the current camera before the game draws
 * and ends it after, before overlays such as Debug. The system is added on
 * first use, so scripts that never touch a camera run exactly as before.
 *
 * Cameras exist on the main script's context only: they drive the GS.
 */

#define CAMERA_SYSTEM_NAME "camera2d"
/* Before other systems in every phase: begin the world first, end it first. */
#define CAMERA_SYSTEM_PRIORITY (-1000)
#define CAMERA_MAX_ZOOM 1000.0f
#define CAMERA_MAX_RATE 1000.0f
#define CAMERA_MAX_TIME 1.0e6f
#define CAMERA_MAX_ANGLE 1.0e4f
#define CAMERA_MAX_SIZE 1.0e6f
/* Depth of Camera2D.push(). */
#define CAMERA_STACK_MAX 8
/* Most tiles drawRepeat() draws in one call. */
#define CAMERA_REPEAT_MAX 1024

/* Timed changes whose promise settles when their event fires. */
enum {
    SETTLE_ZOOM,
    SETTLE_PAN,
    SETTLE_FADE,
    SETTLE_FLASH,
    SETTLE_LETTERBOX,
    SETTLE_COUNT
};

static const uint32_t settle_events[SETTLE_COUNT] = {
    ATHENA_CAMERA2D_EVENT_ZOOM,
    ATHENA_CAMERA2D_EVENT_PAN,
    ATHENA_CAMERA2D_EVENT_FADE,
    ATHENA_CAMERA2D_EVENT_FLASH,
    ATHENA_CAMERA2D_EVENT_LETTERBOX,
};

typedef struct CameraObject {
    AthenaCamera2D cam;
    /* Its JSObject, to keep it alive while the list is walked. */
    void *object;
    /* undefined, an object with x and y, or an array of them. */
    JSValue target;
    /* setZones({ onChange }) */
    JSValue on_zone;
    /* Resolve functions of pending promises, or undefined. */
    JSValue settle[SETTLE_COUNT];
    struct CameraObject *prev, *next;
    /*
     * False until something positions the camera: meanwhile it stays at
     * its viewport's center, the identity view, even when the video mode
     * or the viewport changes.
     */
    bool placed;
    /*
     * True while a promise is pending: the camera then holds a reference
     * to itself, so a script awaiting cam.panTo() without keeping `cam`
     * still gets its answer (the camera list alone does not keep cameras
     * alive). Dropped when the last promise settles, or at cleanup.
     */
    bool pinned;
    /*
     * Follow { interpolate }: the target is sampled before each Loop step
     * and after the last one, and the camera moves in preDraw to the
     * sample blended by the Loop's alpha, as games interpolate what they
     * draw with fixedStep.
     */
    bool interpolate;
    bool has_samples, pending;
    float prev_x, prev_y, cur_x, cur_y, box_w, box_h;
    float frame_dt;
    /* Real (unscaled) time: keeps moving and fading while the game is paused. */
    bool real_time;
} CameraObject;

typedef enum {
    ATOM_X, ATOM_Y, ATOM_W, ATOM_H, ATOM_ZOOM, ATOM_LENGTH, ATOM_WIDTH,
    ATOM_HEIGHT, ATOM_DRAW, ATOM_COUNT
} CameraAtom;

static const char *const camera_atom_names[ATOM_COUNT] = {
    "x", "y", "w", "h", "zoom", "length", "width", "height", "draw"
};

static JSClassID camera_class_id;

/*
 * Module state, owned by the context that initialized the module first:
 * the main script (workers cannot draw). The camera list outlives it, as
 * cameras unlink themselves when the runtime finalizes them.
 */
static struct {
    JSContext *ctx;
    JSAtom atoms[ATOM_COUNT];
    JSValue main;
    /* A camera, or null. */
    JSValue current;
    int system_id;
    /* Records the real delta for real-time cameras (runs just before). */
    int real_system_id;
    float real_dt;
    /* Camera stack depth after the system's begin; 0 when it began none. */
    int applied_depth;
    struct {
        bool active;
        bool real_time;
        JSValue from, to, ease, settle;
        float time, duration;
        /* Eased progress of this frame. */
        float t;
    } transition;
    /* Camera2D.push()/pop(): the cameras (or null) to go back to. */
    JSValue stack[CAMERA_STACK_MAX];
    int stack_count;
    /* Culled draws of the last frame, for Camera2D.getStats(). */
    uint32_t culled_last;
} state;

static CameraObject *camera_list;

/* ---------------------------------------------------------------------- */
/* Numbers                                                                */

/*
 * A number within +-limit into a float: 0, 1 when not a finite number in
 * range, -1 on error. The EE has no infinities nor NaN: float32 values are
 * checked by their bits and other numbers as doubles (soft-float, IEEE).
 */
static int camera_to_float(JSContext *ctx, JSValueConst value, float *out,
    float limit) {
    double number;
    uint32_t bits;

    switch (JS_VALUE_GET_TAG(value)) {
    case JS_TAG_INT:
        *out = (float)JS_VALUE_GET_INT(value);
        return fabsf(*out) <= limit ? 0 : 1;
    case JS_CUSTOM_TAG_FLOAT32:
        *out = JS_VALUE_GET_FLOAT32(value);
        memcpy(&bits, out, sizeof(bits));
        if ((bits & 0x7F800000u) == 0x7F800000u)
            return 1;
        return fabsf(*out) <= limit ? 0 : 1;
    }
    if (!JS_IsNumber(value))
        return 1;
    if (JS_ToFloat64(ctx, &number, value))
        return -1;
    if (!(fabs(number) <= (double)limit))
        return 1;
    *out = (float)number;
    return 0;
}

/* A number in [min, max], or a thrown TypeError/RangeError naming `name`. */
static int camera_number(JSContext *ctx, JSValueConst value, float *out,
    float min, float max, const char *name) {
    float limit = fabsf(min) > fabsf(max) ? fabsf(min) : fabsf(max);
    int result;

    if (!JS_IsNumber(value)) {
        JS_ThrowTypeError(ctx, "%s must be a number", name);
        return -1;
    }
    result = camera_to_float(ctx, value, out, limit);
    if (result < 0)
        return -1;
    if (result > 0 || *out < min || *out > max) {
        JS_ThrowRangeError(ctx, "%s must be a finite number from %g to %g",
            name, (double)min, (double)max);
        return -1;
    }
    return 0;
}

static JSValueConst camera_arg(int argc, JSValueConst *argv, int index) {
    return index < argc ? argv[index] : JS_UNDEFINED;
}

/* Optional property: 0 when absent or undefined, 1 when read, -1 on error. */
static int camera_option(JSContext *ctx, JSValueConst options, const char *key,
    JSValue *out) {
    JSValue value;

    if (JS_IsUndefined(options))
        return 0;
    value = JS_GetPropertyStr(ctx, options, key);
    if (JS_IsException(value))
        return -1;
    if (JS_IsUndefined(value))
        return 0;
    *out = value;
    return 1;
}

static int camera_check_options(JSContext *ctx, JSValueConst options,
    const char *name) {
    if (JS_IsUndefined(options) || (JS_IsObject(options) &&
        !JS_IsFunction(ctx, options)))
        return 0;
    JS_ThrowTypeError(ctx, "%s options must be an object", name);
    return -1;
}

/* A numeric option: untouched when absent. */
static int camera_option_number(JSContext *ctx, JSValueConst options,
    const char *key, float *out, float min, float max, const char *name) {
    JSValue value;
    int found = camera_option(ctx, options, key, &value), result;

    if (found <= 0)
        return found;
    result = camera_number(ctx, value, out, min, max, name);
    JS_FreeValue(ctx, value);
    return result;
}

static int camera_option_bool(JSContext *ctx, JSValueConst options,
    const char *key, bool *out) {
    JSValue value;
    int found = camera_option(ctx, options, key, &value);

    if (found <= 0)
        return found;
    *out = JS_ToBool(ctx, value) != 0;
    JS_FreeValue(ctx, value);
    return 0;
}

/* Property `atom` of an object as a number, or a thrown error naming it. */
static int camera_field(JSContext *ctx, JSValueConst object, CameraAtom atom,
    float *out, float min, float max, const char *name) {
    JSValue value = JS_GetProperty(ctx, object, state.atoms[atom]);
    char label[64];
    int result;

    if (JS_IsException(value))
        return -1;
    snprintf(label, sizeof(label), "%s.%s", name, camera_atom_names[atom]);
    result = camera_number(ctx, value, out, min, max, label);
    JS_FreeValue(ctx, value);
    return result;
}

/* A number for both, [a, b], { x, y } or { w, h }. */
static int camera_pair(JSContext *ctx, JSValueConst value, float *a, float *b,
    float min, float max, const char *name) {
    if (JS_IsNumber(value)) {
        if (camera_number(ctx, value, a, min, max, name))
            return -1;
        *b = *a;
        return 0;
    }
    if (JS_IsArray(ctx, value) > 0) {
        JSValue first = JS_GetPropertyUint32(ctx, value, 0);
        JSValue second = JS_GetPropertyUint32(ctx, value, 1);
        int result = JS_IsException(first) || JS_IsException(second) ||
            camera_number(ctx, first, a, min, max, name) ||
            camera_number(ctx, second, b, min, max, name) ? -1 : 0;
        JS_FreeValue(ctx, first);
        JS_FreeValue(ctx, second);
        return result;
    }
    if (JS_IsObject(value)) {
        JSValue has_x = JS_GetProperty(ctx, value, state.atoms[ATOM_X]);
        bool xy;

        if (JS_IsException(has_x))
            return -1;
        xy = !JS_IsUndefined(has_x);
        JS_FreeValue(ctx, has_x);
        if (camera_field(ctx, value, xy ? ATOM_X : ATOM_W, a, min, max, name) ||
            camera_field(ctx, value, xy ? ATOM_Y : ATOM_H, b, min, max, name))
            return -1;
        return 0;
    }
    JS_ThrowTypeError(ctx, "%s must be a number, [a, b], {x, y} or {w, h}", name);
    return -1;
}

/* { x, y, w, h } with a size of at least `min_size`. */
static int camera_rect(JSContext *ctx, JSValueConst value, float rect[4],
    float min_size, const char *name) {
    if (!JS_IsObject(value)) {
        JS_ThrowTypeError(ctx, "%s must be an object {x, y, w, h}", name);
        return -1;
    }
    if (camera_field(ctx, value, ATOM_X, &rect[0], -ATHENA_CAMERA2D_MAX_COORD,
            ATHENA_CAMERA2D_MAX_COORD, name) ||
        camera_field(ctx, value, ATOM_Y, &rect[1], -ATHENA_CAMERA2D_MAX_COORD,
            ATHENA_CAMERA2D_MAX_COORD, name) ||
        camera_field(ctx, value, ATOM_W, &rect[2], min_size, CAMERA_MAX_SIZE * 10.0f,
            name) ||
        camera_field(ctx, value, ATOM_H, &rect[3], min_size, CAMERA_MAX_SIZE * 10.0f,
            name))
        return -1;
    return 0;
}

/* Four rectangle arguments, or one {x, y, w, h}. */
static int camera_rect_args(JSContext *ctx, int argc, JSValueConst *argv,
    float rect[4], float min_size, const char *name) {
    if (argc >= 4) {
        if (camera_number(ctx, argv[0], &rect[0], -ATHENA_CAMERA2D_MAX_COORD,
                ATHENA_CAMERA2D_MAX_COORD, name) ||
            camera_number(ctx, argv[1], &rect[1], -ATHENA_CAMERA2D_MAX_COORD,
                ATHENA_CAMERA2D_MAX_COORD, name) ||
            camera_number(ctx, argv[2], &rect[2], min_size, CAMERA_MAX_SIZE * 10.0f,
                name) ||
            camera_number(ctx, argv[3], &rect[3], min_size, CAMERA_MAX_SIZE * 10.0f,
                name))
            return -1;
        return 0;
    }
    return camera_rect(ctx, camera_arg(argc, argv, 0), rect, min_size, name);
}

static int camera_set_number(JSContext *ctx, JSValueConst object, CameraAtom atom,
    float value) {
    return JS_SetProperty(ctx, object, state.atoms[atom],
        JS_NewFloat64(ctx, (double)value)) < 0 ? -1 : 0;
}

static JSValue camera_new_point(JSContext *ctx, float x, float y) {
    JSValue point = JS_NewObject(ctx);

    if (JS_IsException(point))
        return point;
    if (camera_set_number(ctx, point, ATOM_X, x) ||
        camera_set_number(ctx, point, ATOM_Y, y)) {
        JS_FreeValue(ctx, point);
        return JS_EXCEPTION;
    }
    return point;
}

static JSValue camera_new_rect(JSContext *ctx, const AthenaRect2D *rect) {
    JSValue object = camera_new_point(ctx, rect->x0, rect->y0);

    if (JS_IsException(object))
        return object;
    if (camera_set_number(ctx, object, ATOM_W, rect->x1 - rect->x0) ||
        camera_set_number(ctx, object, ATOM_H, rect->y1 - rect->y0)) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    return object;
}

/* ---------------------------------------------------------------------- */
/* Cameras                                                                */

static CameraObject *camera_of(JSValueConst value) {
    return JS_GetOpaque(value, camera_class_id);
}

static JSValue camera_value(CameraObject *camera) {
    return JS_MKPTR(JS_TAG_OBJECT, camera->object);
}

static void camera_sync_screen(CameraObject *camera) {
    int width, height;

    athena_view_screen_size(&width, &height);
    athena_camera2d_set_screen(&camera->cam, (float)width, (float)height);
    if (!camera->placed) {
        AthenaRect2D vp;

        athena_camera2d_viewport(&camera->cam, &vp);
        camera->cam.x = camera->cam.goal_x = (vp.x1 - vp.x0) * camera->cam.anchor_x;
        camera->cam.y = camera->cam.goal_y = (vp.y1 - vp.y0) * camera->cam.anchor_y;
    }
}

static int camera_system(void *opaque, AthenaLoopPhase phase, float value);

/* The module's context only; adds the Loop system on first use. */
/* Records the real delta of the frame, for real-time cameras. */
static int camera_real_system(void *opaque, AthenaLoopPhase phase, float value) {
    (void)opaque;
    if (phase == ATHENA_LOOP_POST_UPDATE)
        state.real_dt = value;
    return 0;
}

static int camera_ready(JSContext *ctx) {
    if (ctx != state.ctx) {
        JS_ThrowInternalError(ctx, "Camera2D is only available on the main script");
        return -1;
    }
    if (state.real_system_id <= 0) {
        /* Before the main system, whose postUpdate reads what it records. */
        AthenaLoopSystemDesc desc = {
            .name = CAMERA_SYSTEM_NAME ".realtime",
            .priority = CAMERA_SYSTEM_PRIORITY - 1,
            .phases = ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_POST_UPDATE),
            .real_time = true,
            .func = camera_real_system,
        };
        int id = athena_loop_system_add(&desc);

        if (id > 0)
            state.real_system_id = id;
    }
    if (state.system_id <= 0) {
        AthenaLoopSystemDesc desc = {
            .name = CAMERA_SYSTEM_NAME,
            .priority = CAMERA_SYSTEM_PRIORITY,
            .phases = ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_UPDATE) |
                ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_POST_UPDATE) |
                ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_PRE_DRAW) |
                ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_POST_DRAW),
            .real_time = false,
            .func = camera_system,
        };
        int id = athena_loop_system_add(&desc);

        /* Without the system, Camera2D.update() and begin()/end() still work. */
        if (id > 0)
            state.system_id = id;
    }
    return 0;
}

static CameraObject *camera_this(JSContext *ctx, JSValueConst this_val) {
    CameraObject *camera = JS_GetOpaque2(ctx, this_val, camera_class_id);

    if (!camera || camera_ready(ctx))
        return NULL;
    camera_sync_screen(camera);
    return camera;
}

/* Settles a pending promise of `camera` with `value`. */
static void camera_settle(JSContext *ctx, CameraObject *camera, int slot,
    bool value) {
    JSValue resolve = camera->settle[slot], arg, result;

    if (JS_IsUndefined(resolve))
        return;
    camera->settle[slot] = JS_UNDEFINED;
    arg = JS_NewBool(ctx, value);
    result = JS_Call(ctx, resolve, JS_UNDEFINED, 1, &arg);
    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, resolve);
}

/*
 * Pins the camera while it has pending promises, unpins it after (see
 * `pinned`). Unpinning may free the camera: callers hold a reference.
 */
static void camera_update_pin(JSContext *ctx, CameraObject *camera) {
    bool pending = false;
    int slot;

    for (slot = 0; slot < SETTLE_COUNT; slot++)
        pending |= !JS_IsUndefined(camera->settle[slot]);
    if (pending == camera->pinned)
        return;
    camera->pinned = pending;
    if (pending)
        JS_DupValue(ctx, camera_value(camera));
    else
        JS_FreeValue(ctx, camera_value(camera));
}

/*
 * The promise of a timed change just started in slot `slot`: the previous
 * one settles with false, this one with true when the change ends (now,
 * for a duration <= 0).
 */
static JSValue camera_promise(JSContext *ctx, CameraObject *camera, int slot,
    float duration) {
    JSValue funcs[2], promise;

    camera_settle(ctx, camera, slot, false);
    promise = JS_NewPromiseCapability(ctx, funcs);
    if (JS_IsException(promise)) {
        camera_update_pin(ctx, camera);
        return promise;
    }
    JS_FreeValue(ctx, funcs[1]);
    camera->settle[slot] = funcs[0];
    if (duration <= 0.0f)
        camera_settle(ctx, camera, slot, true);
    camera_update_pin(ctx, camera);
    return promise;
}

/* Reads the follow targets into the camera. 0, or -1 on exception. */
static int camera_read_target(JSContext *ctx, CameraObject *camera) {
    JSValueConst target = camera->target;
    float x, y;
    int rx, ry;

    if (JS_IsUndefined(target))
        return 0;
    if (JS_IsArray(ctx, target) > 0) {
        JSValue length_value = JS_GetProperty(ctx, target, state.atoms[ATOM_LENGTH]);
        uint32_t length, i;
        float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
        bool any = false;

        if (JS_IsException(length_value))
            return -1;
        if (JS_ToUint32(ctx, &length, length_value)) {
            JS_FreeValue(ctx, length_value);
            return -1;
        }
        JS_FreeValue(ctx, length_value);
        for (i = 0; i < length; i++) {
            JSValue item = JS_GetPropertyUint32(ctx, target, i);

            if (JS_IsException(item))
                return -1;
            rx = ry = 1;
            if (JS_IsObject(item)) {
                JSValue vx = JS_GetProperty(ctx, item, state.atoms[ATOM_X]);
                JSValue vy = JS_GetProperty(ctx, item, state.atoms[ATOM_Y]);

                rx = JS_IsException(vx) ? -1 :
                    camera_to_float(ctx, vx, &x, ATHENA_CAMERA2D_MAX_COORD);
                ry = JS_IsException(vy) ? -1 :
                    camera_to_float(ctx, vy, &y, ATHENA_CAMERA2D_MAX_COORD);
                JS_FreeValue(ctx, vx);
                JS_FreeValue(ctx, vy);
            }
            JS_FreeValue(ctx, item);
            if (rx < 0 || ry < 0)
                return -1;
            /* Entries without a position (removed players...) are skipped. */
            if (rx || ry)
                continue;
            if (!any) {
                x0 = x1 = x;
                y0 = y1 = y;
                any = true;
            } else {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
        }
        if (any)
            athena_camera2d_set_target_box(&camera->cam, (x0 + x1) * 0.5f,
                (y0 + y1) * 0.5f, x1 - x0, y1 - y0);
        return 0;
    }
    {
        JSValue vx = JS_GetProperty(ctx, target, state.atoms[ATOM_X]);
        JSValue vy = JS_GetProperty(ctx, target, state.atoms[ATOM_Y]);

        rx = JS_IsException(vx) ? -1 :
            camera_to_float(ctx, vx, &x, ATHENA_CAMERA2D_MAX_COORD);
        ry = JS_IsException(vy) ? -1 :
            camera_to_float(ctx, vy, &y, ATHENA_CAMERA2D_MAX_COORD);
        JS_FreeValue(ctx, vx);
        JS_FreeValue(ctx, vy);
        if (rx < 0 || ry < 0)
            return -1;
        /* A position that is not a number keeps the last one. */
        if (!rx && !ry)
            athena_camera2d_set_target(&camera->cam, x, y);
    }
    return 0;
}

/*
 * Advances a camera whose target is set, settles what finished and calls
 * onChange(zone, previous). 0, or -1 on exception.
 */
static int camera_advance(JSContext *ctx, CameraObject *camera, float dt) {
    uint32_t events;
    int slot;

    athena_camera2d_update(&camera->cam, dt);
    events = camera->cam.events;
    for (slot = 0; slot < SETTLE_COUNT; slot++) {
        if (events & settle_events[slot])
            camera_settle(ctx, camera, slot, true);
    }
    /* The caller holds a reference: unpinning cannot free the camera here. */
    camera_update_pin(ctx, camera);
    if ((events & ATHENA_CAMERA2D_EVENT_ZONE) &&
        JS_IsFunction(ctx, camera->on_zone)) {
        JSValue self = JS_DupValue(ctx, camera_value(camera));
        JSValue func = JS_DupValue(ctx, camera->on_zone);
        JSValue args[2] = {
            JS_NewInt32(ctx, camera->cam.zone),
            JS_NewInt32(ctx, camera->cam.previous_zone),
        };
        JSValue result = JS_Call(ctx, func, self, 2, args);

        JS_FreeValue(ctx, func);
        JS_FreeValue(ctx, self);
        if (JS_IsException(result))
            return -1;
        JS_FreeValue(ctx, result);
    }
    return 0;
}

/* Reads the target and advances: one camera outside the Loop's phases. */
static int camera_update_one(JSContext *ctx, CameraObject *camera, float dt) {
    camera_sync_screen(camera);
    if (camera->cam.following && camera_read_target(ctx, camera))
        return -1;
    return camera_advance(ctx, camera, dt);
}

/* The target now, as the interpolation's newest sample. */
static int camera_sample(JSContext *ctx, CameraObject *camera, bool newest) {
    if (camera_read_target(ctx, camera))
        return -1;
    if (!camera->cam.has_target)
        return 0;
    if (newest) {
        camera->cur_x = camera->cam.target_x;
        camera->cur_y = camera->cam.target_y;
        camera->box_w = camera->cam.target_w;
        camera->box_h = camera->cam.target_h;
    } else {
        camera->prev_x = camera->cam.target_x;
        camera->prev_y = camera->cam.target_y;
    }
    if (!camera->has_samples) {
        camera->prev_x = camera->cur_x = camera->cam.target_x;
        camera->prev_y = camera->cur_y = camera->cam.target_y;
        camera->has_samples = true;
    }
    return 0;
}

static bool camera_interpolating(const CameraObject *camera) {
    return camera->interpolate && camera->cam.following;
}

/* Loop UPDATE, before each step: the target before the step. */
static int camera_visit_step(JSContext *ctx, CameraObject *camera, float value) {
    (void)value;
    if (!camera_interpolating(camera))
        return 0;
    return camera_sample(ctx, camera, false);
}

/*
 * Loop POST_UPDATE: cameras move now, except interpolated ones, which
 * take the newest sample and move in preDraw.
 */
static int camera_visit_post_update(JSContext *ctx, CameraObject *camera,
    float value) {
    float dt = camera->real_time ? state.real_dt : value;

    if (!camera_interpolating(camera))
        return camera_update_one(ctx, camera, dt);
    camera_sync_screen(camera);
    if (camera_sample(ctx, camera, true))
        return -1;
    camera->frame_dt = dt;
    camera->pending = true;
    return 0;
}

/* Loop PRE_DRAW: interpolated cameras follow the target blended by alpha. */
static int camera_visit_pre_draw(JSContext *ctx, CameraObject *camera,
    float alpha) {
    if (!camera->pending)
        return 0;
    camera->pending = false;
    if (camera->has_samples)
        athena_camera2d_set_target_box(&camera->cam,
            camera->prev_x + (camera->cur_x - camera->prev_x) * alpha,
            camera->prev_y + (camera->cur_y - camera->prev_y) * alpha,
            camera->box_w, camera->box_h);
    return camera_advance(ctx, camera, camera->frame_dt);
}

static int camera_visit_update(JSContext *ctx, CameraObject *camera, float dt) {
    return camera_update_one(ctx, camera, dt);
}

static void camera_transition_clear(JSContext *ctx) {
    state.transition.active = false;
    JS_FreeValue(ctx, state.transition.from);
    JS_FreeValue(ctx, state.transition.to);
    JS_FreeValue(ctx, state.transition.ease);
    state.transition.from = JS_UNDEFINED;
    state.transition.to = JS_UNDEFINED;
    state.transition.ease = JS_UNDEFINED;
}

/* Settles the transition's promise with `value` and forgets the transition. */
static void camera_transition_end(JSContext *ctx, bool value) {
    JSValue resolve = state.transition.settle, arg, result;

    state.transition.settle = JS_UNDEFINED;
    camera_transition_clear(ctx);
    if (JS_IsUndefined(resolve))
        return;
    arg = JS_NewBool(ctx, value);
    result = JS_Call(ctx, resolve, JS_UNDEFINED, 1, &arg);
    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, resolve);
}

static float smooth01(float t) {
    return t * t * (3.0f - 2.0f * t);
}

static int camera_update_transition(JSContext *ctx, float dt) {
    float t, eased;

    if (!state.transition.active)
        return 0;
    state.transition.time += dt;
    t = state.transition.time / state.transition.duration;
    if (t > 1.0f)
        t = 1.0f;
    eased = smooth01(t);
    if (JS_IsFunction(ctx, state.transition.ease)) {
        JSValue func = JS_DupValue(ctx, state.transition.ease);
        JSValue arg = JS_NewFloat64(ctx, (double)t);
        JSValue result = JS_Call(ctx, func, JS_UNDEFINED, 1, &arg);
        int valid;

        JS_FreeValue(ctx, func);
        if (JS_IsException(result))
            return -1;
        valid = camera_to_float(ctx, result, &eased, 10.0f);
        JS_FreeValue(ctx, result);
        if (valid < 0)
            return -1;
        if (valid > 0) {
            JS_ThrowTypeError(ctx,
                "Camera2D.transition ease must return a finite number");
            return -1;
        }
        /* The ease may have ended or replaced the transition. */
        if (!state.transition.active)
            return 0;
    }
    state.transition.t = eased;
    if (t >= 1.0f) {
        /* The target is current already: just drop the blend. */
        camera_transition_end(ctx, true);
    }
    return 0;
}

/* Calls `visit` on every camera. 0, or -1 on the first exception. */
static int camera_each(JSContext *ctx,
    int (*visit)(JSContext *ctx, CameraObject *camera, float value), float value) {
    CameraObject *camera = camera_list;
    JSValue held = camera ? JS_DupValue(ctx, camera_value(camera)) : JS_UNDEFINED;

    /*
     * A target's getter may drop cameras and let the collector finalize
     * them: hold the current and the next camera while walking.
     */
    while (camera) {
        int result = visit(ctx, camera, value);
        CameraObject *next = camera->next;
        JSValue held_next = next ? JS_DupValue(ctx, camera_value(next)) :
            JS_UNDEFINED;

        JS_FreeValue(ctx, held);
        if (result) {
            JS_FreeValue(ctx, held_next);
            return -1;
        }
        held = held_next;
        camera = next;
    }
    return 0;
}

/* Updates every camera, then the transition: Camera2D.update(). */
static int camera_update_all(JSContext *ctx, float dt) {
    if (camera_each(ctx, camera_visit_update, dt))
        return -1;
    return camera_update_transition(ctx, dt);
}

/* The camera the system applies: the current one, or a blend in a transition. */
static const AthenaCamera2D *camera_pose(AthenaCamera2D *blend) {
    CameraObject *current;

    if (state.transition.active) {
        CameraObject *from = camera_of(state.transition.from);
        CameraObject *to = camera_of(state.transition.to);

        if (from && to) {
            camera_sync_screen(from);
            camera_sync_screen(to);
            athena_camera2d_blend(&from->cam, &to->cam, state.transition.t, blend);
            return blend;
        }
    }
    current = camera_of(state.current);
    if (!current)
        return NULL;
    camera_sync_screen(current);
    return &current->cam;
}

static void camera_apply(void) {
    AthenaCamera2D blend;
    const AthenaCamera2D *cam;
    AthenaAffine2D m;

    /* Pairs a failed or careless frame left open. */
    athena_camera2d_unwind();
    state.applied_depth = 0;
    cam = camera_pose(&blend);
    if (!cam) {
        athena_view_set_world(NULL);
        return;
    }
    if (athena_camera2d_begin(cam, 1.0f, 1.0f)) {
        state.applied_depth = athena_camera2d_depth();
        athena_camera2d_matrix(cam, 1.0f, 1.0f, &m);
        athena_view_set_world(&m);
    }
}

static void camera_unapply(void) {
    AthenaCamera2D blend;

    if (!state.applied_depth)
        return;
    while (athena_camera2d_depth() > state.applied_depth)
        athena_camera2d_end(NULL);
    athena_camera2d_end(camera_pose(&blend));
    state.applied_depth = 0;
}

static int camera_system(void *opaque, AthenaLoopPhase phase, float value) {
    JSContext *ctx = state.ctx;

    (void)opaque;
    if (!ctx)
        return 0;
    switch (phase) {
    case ATHENA_LOOP_UPDATE:
        return camera_each(ctx, camera_visit_step, value) ? -1 : 0;
    case ATHENA_LOOP_POST_UPDATE:
        if (camera_each(ctx, camera_visit_post_update, value))
            return -1;
        return camera_update_transition(ctx,
            state.transition.real_time ? state.real_dt : value) ? -1 : 0;
    case ATHENA_LOOP_PRE_DRAW:
        if (camera_each(ctx, camera_visit_pre_draw, value))
            return -1;
        /* What the previous frame's drawing culled, for getStats(). */
        state.culled_last = athena_view_culled;
        athena_view_culled = 0;
        camera_apply();
        return 0;
    case ATHENA_LOOP_POST_DRAW:
        camera_unapply();
        return 0;
    default:
        return 0;
    }
}

static void camera_finalizer(JSRuntime *rt, JSValue value) {
    CameraObject *camera = JS_GetOpaque(value, camera_class_id);
    int slot;

    if (!camera)
        return;
    if (camera->prev)
        camera->prev->next = camera->next;
    else if (camera_list == camera)
        camera_list = camera->next;
    if (camera->next)
        camera->next->prev = camera->prev;
    JS_FreeValueRT(rt, camera->target);
    JS_FreeValueRT(rt, camera->on_zone);
    for (slot = 0; slot < SETTLE_COUNT; slot++)
        JS_FreeValueRT(rt, camera->settle[slot]);
    js_free_rt(rt, camera);
}

/* Follow targets, the zone callback and resolve functions live in C memory. */
static void camera_gc_mark(JSRuntime *rt, JSValueConst value, JS_MarkFunc *mark) {
    CameraObject *camera = JS_GetOpaque(value, camera_class_id);
    int slot;

    if (!camera)
        return;
    JS_MarkValue(rt, camera->target, mark);
    JS_MarkValue(rt, camera->on_zone, mark);
    for (slot = 0; slot < SETTLE_COUNT; slot++)
        JS_MarkValue(rt, camera->settle[slot], mark);
}

static JSClassDef camera_class = {
    "Camera",
    .finalizer = camera_finalizer,
    .gc_mark = camera_gc_mark,
};

static JSValue camera_apply_options(JSContext *ctx, CameraObject *camera,
    JSValueConst options);

static JSValue camera_create(JSContext *ctx, JSValueConst new_target,
    JSValueConst options) {
    JSValue proto = JS_UNDEFINED, object, result;
    CameraObject *camera;
    int width, height, slot;

    if (!JS_IsUndefined(new_target)) {
        proto = JS_GetPropertyStr(ctx, new_target, "prototype");
        if (JS_IsException(proto))
            return JS_EXCEPTION;
        object = JS_NewObjectProtoClass(ctx, proto, camera_class_id);
        JS_FreeValue(ctx, proto);
    } else {
        object = JS_NewObjectClass(ctx, camera_class_id);
    }
    if (JS_IsException(object))
        return object;
    camera = js_mallocz(ctx, sizeof(*camera));
    if (!camera) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    athena_view_screen_size(&width, &height);
    athena_camera2d_init(&camera->cam, (float)width, (float)height);
    camera->object = JS_VALUE_GET_PTR(object);
    camera->target = JS_UNDEFINED;
    camera->on_zone = JS_UNDEFINED;
    for (slot = 0; slot < SETTLE_COUNT; slot++)
        camera->settle[slot] = JS_UNDEFINED;
    camera->next = camera_list;
    if (camera_list)
        camera_list->prev = camera;
    camera_list = camera;
    JS_SetOpaque(object, camera);

    result = camera_apply_options(ctx, camera, options);
    if (JS_IsException(result)) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    return object;
}

static JSValue camera_ctor(JSContext *ctx, JSValueConst new_target, int argc,
    JSValueConst *argv) {
    if (camera_ready(ctx))
        return JS_EXCEPTION;
    return camera_create(ctx, new_target, camera_arg(argc, argv, 0));
}

static int camera_set_current(JSContext *ctx, JSValueConst camera) {
    if (state.transition.active)
        camera_transition_end(ctx, false);
    JS_FreeValue(ctx, state.current);
    state.current = JS_DupValue(ctx, camera);
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Properties                                                             */

enum {
    PROP_X, PROP_Y, PROP_ZOOM, PROP_ZOOM_X, PROP_ZOOM_Y, PROP_ROTATION,
    PROP_PIXEL_SNAP, PROP_ANCHOR, PROP_VIEWPORT, PROP_BOUNDS, PROP_ZONE,
    PROP_FOLLOWING, PROP_SHAKING, PROP_LETTERBOX, PROP_FADE, PROP_CURRENT,
    PROP_BOUNDS_IGNORE_ROTATION, PROP_REAL_TIME, PROP_DEBUG, PROP_TRAUMA,
};

static JSValue camera_get(JSContext *ctx, JSValueConst this_val, int magic) {
    CameraObject *camera = camera_this(ctx, this_val);
    AthenaCamera2D *cam;
    AthenaRect2D rect;

    if (!camera)
        return JS_EXCEPTION;
    cam = &camera->cam;
    switch (magic) {
    case PROP_X: return JS_NewFloat64(ctx, cam->x);
    case PROP_Y: return JS_NewFloat64(ctx, cam->y);
    case PROP_ZOOM:
    case PROP_ZOOM_X: return JS_NewFloat64(ctx, cam->zoom_x);
    case PROP_ZOOM_Y: return JS_NewFloat64(ctx, cam->zoom_y);
    case PROP_ROTATION: return JS_NewFloat64(ctx, cam->rotation);
    case PROP_PIXEL_SNAP: return JS_NewBool(ctx, cam->pixel_snap);
    case PROP_ANCHOR: return camera_new_point(ctx, cam->anchor_x, cam->anchor_y);
    case PROP_VIEWPORT:
        athena_camera2d_viewport(cam, &rect);
        return camera_new_rect(ctx, &rect);
    case PROP_BOUNDS:
        return cam->has_bounds ? camera_new_rect(ctx, &cam->bounds) : JS_NULL;
    case PROP_BOUNDS_IGNORE_ROTATION: return JS_NewBool(ctx, cam->bounds_ignore_rotation);
    case PROP_REAL_TIME: return JS_NewBool(ctx, camera->real_time);
    case PROP_DEBUG: return JS_NewBool(ctx, cam->debug_draw);
    case PROP_TRAUMA: return JS_NewFloat64(ctx, cam->trauma);
    case PROP_ZONE: return JS_NewInt32(ctx, cam->zone);
    case PROP_FOLLOWING: return JS_NewBool(ctx, cam->following);
    case PROP_SHAKING: return JS_NewBool(ctx, cam->shake_time < cam->shake_duration);
    case PROP_LETTERBOX: return JS_NewFloat64(ctx, cam->letterbox);
    case PROP_FADE: return JS_NewFloat64(ctx, cam->fade_alpha);
    case PROP_CURRENT: {
        CameraObject *current = camera_of(state.current);
        return JS_NewBool(ctx, current == camera);
    }
    }
    return JS_UNDEFINED;
}

static JSValue camera_set(JSContext *ctx, JSValueConst this_val, JSValueConst value,
    int magic) {
    CameraObject *camera = camera_this(ctx, this_val);
    AthenaCamera2D *cam;
    float number, second, rect[4];

    if (!camera)
        return JS_EXCEPTION;
    cam = &camera->cam;
    switch (magic) {
    case PROP_X:
    case PROP_Y:
        if (camera_number(ctx, value, &number, -ATHENA_CAMERA2D_MAX_COORD,
                ATHENA_CAMERA2D_MAX_COORD, magic == PROP_X ? "Camera.x" : "Camera.y"))
            return JS_EXCEPTION;
        if (magic == PROP_X)
            cam->x = cam->goal_x = number;
        else
            cam->y = cam->goal_y = number;
        camera->placed = true;
        cam->pan_tween.active = false;
        break;
    case PROP_ZOOM:
    case PROP_ZOOM_X:
    case PROP_ZOOM_Y:
        if (camera_number(ctx, value, &number, 1.0e-3f, CAMERA_MAX_ZOOM, "Camera.zoom"))
            return JS_EXCEPTION;
        if (magic != PROP_ZOOM_Y)
            cam->zoom_x = number;
        if (magic != PROP_ZOOM_X)
            cam->zoom_y = number;
        cam->zoom_tween.active = false;
        break;
    case PROP_ROTATION:
        if (camera_number(ctx, value, &number, -CAMERA_MAX_ANGLE, CAMERA_MAX_ANGLE,
                "Camera.rotation"))
            return JS_EXCEPTION;
        cam->rotation = number;
        break;
    case PROP_PIXEL_SNAP:
        cam->pixel_snap = JS_ToBool(ctx, value) != 0;
        break;
    case PROP_BOUNDS_IGNORE_ROTATION:
        cam->bounds_ignore_rotation = JS_ToBool(ctx, value) != 0;
        athena_camera2d_clamp(cam);
        break;
    case PROP_REAL_TIME:
        camera->real_time = JS_ToBool(ctx, value) != 0;
        break;
    case PROP_DEBUG:
        cam->debug_draw = JS_ToBool(ctx, value) != 0;
        break;
    case PROP_ANCHOR:
        if (camera_pair(ctx, value, &number, &second, 0.0f, 1.0f, "Camera.anchor"))
            return JS_EXCEPTION;
        cam->anchor_x = number;
        cam->anchor_y = second;
        break;
    case PROP_VIEWPORT:
        if (JS_IsNull(value) || JS_IsUndefined(value)) {
            cam->viewport_w = cam->viewport_h = 0.0f;
            break;
        }
        if (camera_rect(ctx, value, rect, 1.0f, "Camera.viewport"))
            return JS_EXCEPTION;
        cam->viewport_x = rect[0];
        cam->viewport_y = rect[1];
        cam->viewport_w = rect[2];
        cam->viewport_h = rect[3];
        break;
    case PROP_BOUNDS:
        if (JS_IsNull(value) || JS_IsUndefined(value)) {
            athena_camera2d_clear_bounds(cam);
            break;
        }
        if (camera_rect(ctx, value, rect, 0.0f, "Camera.bounds"))
            return JS_EXCEPTION;
        athena_camera2d_set_bounds(cam, rect[0], rect[1], rect[2], rect[3]);
        athena_camera2d_clamp(cam);
        break;
    }
    return JS_UNDEFINED;
}

/* ---------------------------------------------------------------------- */
/* Methods                                                                */

static JSValue camera_apply_options(JSContext *ctx, CameraObject *camera,
    JSValueConst options) {
    AthenaCamera2D *cam = &camera->cam;
    JSValue value;
    float number, second;
    float x = 0.0f, y = 0.0f;
    bool flag, has_x, has_y;
    int found;

    if (camera_check_options(ctx, options, "Camera2D.Camera"))
        return JS_EXCEPTION;
    found = camera_option(ctx, options, "x", &value);
    if (found < 0)
        return JS_EXCEPTION;
    has_x = found > 0;
    if (has_x) {
        int bad = camera_number(ctx, value, &x, -ATHENA_CAMERA2D_MAX_COORD,
            ATHENA_CAMERA2D_MAX_COORD, "Camera x");
        JS_FreeValue(ctx, value);
        if (bad)
            return JS_EXCEPTION;
    }
    found = camera_option(ctx, options, "y", &value);
    if (found < 0)
        return JS_EXCEPTION;
    has_y = found > 0;
    if (has_y) {
        int bad = camera_number(ctx, value, &y, -ATHENA_CAMERA2D_MAX_COORD,
            ATHENA_CAMERA2D_MAX_COORD, "Camera y");
        JS_FreeValue(ctx, value);
        if (bad)
            return JS_EXCEPTION;
    }
    if (camera_option_number(ctx, options, "rotation", &cam->rotation,
            -CAMERA_MAX_ANGLE, CAMERA_MAX_ANGLE, "Camera rotation") < 0)
        return JS_EXCEPTION;
    found = camera_option(ctx, options, "zoom", &value);
    if (found < 0)
        return JS_EXCEPTION;
    if (found > 0) {
        int bad = camera_number(ctx, value, &number, 1.0e-3f, CAMERA_MAX_ZOOM,
            "Camera zoom");
        JS_FreeValue(ctx, value);
        if (bad)
            return JS_EXCEPTION;
        cam->zoom_x = cam->zoom_y = number;
    }
    if (camera_option_number(ctx, options, "zoomX", &cam->zoom_x, 1.0e-3f,
            CAMERA_MAX_ZOOM, "Camera zoomX") < 0 ||
        camera_option_number(ctx, options, "zoomY", &cam->zoom_y, 1.0e-3f,
            CAMERA_MAX_ZOOM, "Camera zoomY") < 0 ||
        camera_option_bool(ctx, options, "pixelSnap", &cam->pixel_snap) < 0 ||
        camera_option_bool(ctx, options, "boundsIgnoreRotation",
            &cam->bounds_ignore_rotation) < 0 ||
        camera_option_bool(ctx, options, "realTime", &camera->real_time) < 0 ||
        camera_option_bool(ctx, options, "debug", &cam->debug_draw) < 0)
        return JS_EXCEPTION;
    found = camera_option(ctx, options, "anchor", &value);
    if (found < 0)
        return JS_EXCEPTION;
    if (found > 0) {
        int bad = camera_pair(ctx, value, &number, &second, 0.0f, 1.0f,
            "Camera anchor");
        JS_FreeValue(ctx, value);
        if (bad)
            return JS_EXCEPTION;
        cam->anchor_x = number;
        cam->anchor_y = second;
    }
    found = camera_option(ctx, options, "viewport", &value);
    if (found < 0)
        return JS_EXCEPTION;
    if (found > 0) {
        JSValue r = camera_set(ctx, camera_value(camera), value, PROP_VIEWPORT);
        JS_FreeValue(ctx, value);
        if (JS_IsException(r))
            return r;
    }
    /*
     * Without a position, a camera shows its viewport's size of the world
     * from the origin: like the screen, at zoom 1 (see `placed`).
     */
    camera_sync_screen(camera);
    if (has_x || has_y) {
        if (has_x)
            cam->x = x;
        if (has_y)
            cam->y = y;
        camera->placed = true;
    }
    found = camera_option(ctx, options, "bounds", &value);
    if (found < 0)
        return JS_EXCEPTION;
    if (found > 0) {
        JSValue r = camera_set(ctx, camera_value(camera), value, PROP_BOUNDS);
        JS_FreeValue(ctx, value);
        if (JS_IsException(r))
            return r;
    }
    cam->goal_x = cam->x;
    cam->goal_y = cam->y;
    flag = false;
    if (camera_option_bool(ctx, options, "current", &flag) < 0)
        return JS_EXCEPTION;
    if (flag)
        camera_set_current(ctx, camera_value(camera));
    return JS_UNDEFINED;
}

static JSValue camera_set_position(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    float x, y;

    if (!camera ||
        camera_number(ctx, camera_arg(argc, argv, 0), &x, -ATHENA_CAMERA2D_MAX_COORD,
            ATHENA_CAMERA2D_MAX_COORD, "Camera.setPosition x") ||
        camera_number(ctx, camera_arg(argc, argv, 1), &y, -ATHENA_CAMERA2D_MAX_COORD,
            ATHENA_CAMERA2D_MAX_COORD, "Camera.setPosition y"))
        return JS_EXCEPTION;
    camera->cam.x = camera->cam.goal_x = x;
    camera->placed = true;
    camera->cam.y = camera->cam.goal_y = y;
    camera->cam.pan_tween.active = false;
    return JS_DupValue(ctx, this_val);
}

static JSValue camera_move(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    float dx, dy;

    if (!camera ||
        camera_number(ctx, camera_arg(argc, argv, 0), &dx, -ATHENA_CAMERA2D_MAX_COORD,
            ATHENA_CAMERA2D_MAX_COORD, "Camera.move dx") ||
        camera_number(ctx, camera_arg(argc, argv, 1), &dy, -ATHENA_CAMERA2D_MAX_COORD,
            ATHENA_CAMERA2D_MAX_COORD, "Camera.move dy"))
        return JS_EXCEPTION;
    camera->placed = true;
    camera->cam.x += dx;
    camera->cam.y += dy;
    camera->cam.goal_x = camera->cam.x;
    camera->cam.goal_y = camera->cam.y;
    athena_camera2d_clamp(&camera->cam);
    return JS_DupValue(ctx, this_val);
}

static JSValue camera_set_zoom(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    float zx, zy;

    if (!camera ||
        camera_number(ctx, camera_arg(argc, argv, 0), &zx, 1.0e-3f, CAMERA_MAX_ZOOM,
            "Camera.setZoom zoomX"))
        return JS_EXCEPTION;
    zy = zx;
    if (argc > 1 && !JS_IsUndefined(argv[1]) &&
        camera_number(ctx, argv[1], &zy, 1.0e-3f, CAMERA_MAX_ZOOM,
            "Camera.setZoom zoomY"))
        return JS_EXCEPTION;
    camera->cam.zoom_x = zx;
    camera->cam.zoom_y = zy;
    camera->cam.zoom_tween.active = false;
    athena_camera2d_clamp(&camera->cam);
    return JS_DupValue(ctx, this_val);
}

static JSValue camera_set_viewport(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    float rect[4];

    if (!camera)
        return JS_EXCEPTION;
    if (argc == 0 || JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) {
        camera->cam.viewport_w = camera->cam.viewport_h = 0.0f;
        return JS_DupValue(ctx, this_val);
    }
    if (camera_rect_args(ctx, argc, argv, rect, 1.0f, "Camera.setViewport"))
        return JS_EXCEPTION;
    camera->cam.viewport_x = rect[0];
    camera->cam.viewport_y = rect[1];
    camera->cam.viewport_w = rect[2];
    camera->cam.viewport_h = rect[3];
    return JS_DupValue(ctx, this_val);
}

static JSValue camera_set_bounds(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    float rect[4];

    if (!camera)
        return JS_EXCEPTION;
    if (argc == 0 || JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) {
        athena_camera2d_clear_bounds(&camera->cam);
        return JS_DupValue(ctx, this_val);
    }
    if (camera_rect_args(ctx, argc, argv, rect, 0.0f, "Camera.setBounds"))
        return JS_EXCEPTION;
    athena_camera2d_set_bounds(&camera->cam, rect[0], rect[1], rect[2], rect[3]);
    athena_camera2d_clamp(&camera->cam);
    return JS_DupValue(ctx, this_val);
}

/* A follow target: an object with a position, or a non-empty array of them. */
static int camera_check_target(JSContext *ctx, JSValueConst target) {
    int array = JS_IsArray(ctx, target);

    if (array < 0)
        return -1;
    if (array) {
        JSValue length_value = JS_GetProperty(ctx, target, state.atoms[ATOM_LENGTH]);
        uint32_t length = 0;
        int failed = JS_IsException(length_value) ||
            JS_ToUint32(ctx, &length, length_value);

        JS_FreeValue(ctx, length_value);
        if (failed)
            return -1;
        if (length == 0) {
            JS_ThrowRangeError(ctx, "Camera.follow needs at least one target");
            return -1;
        }
        for (uint32_t i = 0; i < length; i++) {
            JSValue item = JS_GetPropertyUint32(ctx, target, i);
            bool object = JS_IsObject(item);

            JS_FreeValue(ctx, item);
            if (!object) {
                JS_ThrowTypeError(ctx, "Camera.follow targets must be objects with x and y");
                return -1;
            }
        }
        return 0;
    }
    if (!JS_IsObject(target)) {
        JS_ThrowTypeError(ctx, "Camera.follow target must be an object with x and y, or an array of them");
        return -1;
    }
    /*
     * One target must have a position now: a typo (`{ X: 1 }`, a missing
     * .position) would otherwise leave the camera still, silently. Later
     * frames tolerate a missing value by keeping the last one.
     */
    for (int i = 0; i < 2; i++) {
        CameraAtom atom = i ? ATOM_Y : ATOM_X;
        JSValue value = JS_GetProperty(ctx, target, state.atoms[atom]);
        float number;
        int result = JS_IsException(value) ? -1 :
            camera_to_float(ctx, value, &number, ATHENA_CAMERA2D_MAX_COORD);

        JS_FreeValue(ctx, value);
        if (result < 0)
            return -1;
        if (result > 0) {
            JS_ThrowTypeError(ctx, "Camera.follow target.%s must be a finite number",
                camera_atom_names[atom]);
            return -1;
        }
    }
    return 0;
}

static JSValue camera_follow(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    JSValueConst target = camera_arg(argc, argv, 0);
    JSValueConst options = camera_arg(argc, argv, 1);
    AthenaCamera2D *cam;
    float lerp_x = 0.0f, lerp_y = 0.0f;
    float dead_w = 0.0f, dead_h = 0.0f;
    float look_x = 0.0f, look_y = 0.0f, look_lerp = 4.0f;
    float offset_x = 0.0f, offset_y = 0.0f;
    bool auto_zoom = false, snap = true, interpolate = false, speed_zoom = false;
    float zoom_min, zoom_max, zoom_margin, zoom_lerp;
    float speed_min, speed_max, speed_ref;
    JSValue value;
    int found;

    if (!camera || camera_check_target(ctx, target) ||
        camera_check_options(ctx, options, "Camera.follow"))
        return JS_EXCEPTION;
    cam = &camera->cam;
    zoom_min = cam->auto_zoom_min;
    zoom_max = cam->auto_zoom_max;
    zoom_margin = cam->auto_zoom_margin;
    zoom_lerp = cam->zoom_lerp;
    speed_min = cam->speed_zoom_min;
    speed_max = cam->speed_zoom_max;
    speed_ref = cam->speed_zoom_speed;

#define PAIR_OPTION(key, a, b, min, max, label) \
    found = camera_option(ctx, options, key, &value); \
    if (found < 0) \
        return JS_EXCEPTION; \
    if (found > 0) { \
        int bad = camera_pair(ctx, value, a, b, min, max, label); \
        JS_FreeValue(ctx, value); \
        if (bad) \
            return JS_EXCEPTION; \
    }
    PAIR_OPTION("lerp", &lerp_x, &lerp_y, 0.0f, CAMERA_MAX_RATE, "Camera.follow lerp")
    PAIR_OPTION("deadzone", &dead_w, &dead_h, 0.0f, CAMERA_MAX_SIZE, "Camera.follow deadzone")
    PAIR_OPTION("lookahead", &look_x, &look_y, 0.0f, CAMERA_MAX_SIZE, "Camera.follow lookahead")
    PAIR_OPTION("offset", &offset_x, &offset_y, -CAMERA_MAX_SIZE, CAMERA_MAX_SIZE,
        "Camera.follow offset")
#undef PAIR_OPTION
    if (camera_option_number(ctx, options, "lookaheadLerp", &look_lerp, 0.0f,
            CAMERA_MAX_RATE, "Camera.follow lookaheadLerp") < 0 ||
        camera_option_bool(ctx, options, "snap", &snap) < 0 ||
        camera_option_bool(ctx, options, "interpolate", &interpolate) < 0)
        return JS_EXCEPTION;

    found = camera_option(ctx, options, "zoomBySpeed", &value);
    if (found < 0)
        return JS_EXCEPTION;
    if (found > 0) {
        int bad = 0;

        if (JS_IsObject(value)) {
            speed_zoom = true;
            bad = camera_option_number(ctx, value, "min", &speed_min, 1.0e-3f,
                    CAMERA_MAX_ZOOM, "Camera.follow zoomBySpeed.min") < 0 ||
                camera_option_number(ctx, value, "max", &speed_max, 1.0e-3f,
                    CAMERA_MAX_ZOOM, "Camera.follow zoomBySpeed.max") < 0 ||
                camera_option_number(ctx, value, "speed", &speed_ref, 1.0e-3f,
                    CAMERA_MAX_SIZE, "Camera.follow zoomBySpeed.speed") < 0 ||
                camera_option_number(ctx, value, "lerp", &zoom_lerp, 0.0f,
                    CAMERA_MAX_RATE, "Camera.follow zoomBySpeed.lerp") < 0;
            if (!bad && speed_min > speed_max) {
                JS_ThrowRangeError(ctx, "Camera.follow zoomBySpeed.min must not exceed max");
                bad = 1;
            }
        } else {
            speed_zoom = JS_ToBool(ctx, value) != 0;
        }
        JS_FreeValue(ctx, value);
        if (bad)
            return JS_EXCEPTION;
    }

    found = camera_option(ctx, options, "autoZoom", &value);
    if (found < 0)
        return JS_EXCEPTION;
    if (found > 0) {
        int bad = 0;

        if (JS_IsObject(value)) {
            auto_zoom = true;
            bad = camera_option_number(ctx, value, "min", &zoom_min, 1.0e-3f,
                    CAMERA_MAX_ZOOM, "Camera.follow autoZoom.min") < 0 ||
                camera_option_number(ctx, value, "max", &zoom_max, 1.0e-3f,
                    CAMERA_MAX_ZOOM, "Camera.follow autoZoom.max") < 0 ||
                camera_option_number(ctx, value, "margin", &zoom_margin, 0.0f,
                    CAMERA_MAX_SIZE, "Camera.follow autoZoom.margin") < 0 ||
                camera_option_number(ctx, value, "lerp", &zoom_lerp, 0.0f,
                    CAMERA_MAX_RATE, "Camera.follow autoZoom.lerp") < 0;
            if (!bad && zoom_min > zoom_max) {
                JS_ThrowRangeError(ctx, "Camera.follow autoZoom.min must not exceed max");
                bad = 1;
            }
        } else {
            auto_zoom = JS_ToBool(ctx, value) != 0;
        }
        JS_FreeValue(ctx, value);
        if (bad)
            return JS_EXCEPTION;
    }

    athena_camera2d_follow(cam, lerp_x, lerp_y);
    cam->deadzone_w = dead_w;
    cam->deadzone_h = dead_h;
    cam->lookahead_x = look_x;
    cam->lookahead_y = look_y;
    cam->lookahead_lerp = look_lerp;
    cam->offset_x = offset_x;
    cam->offset_y = offset_y;
    cam->auto_zoom = auto_zoom;
    cam->auto_zoom_min = zoom_min;
    cam->auto_zoom_max = zoom_max;
    cam->auto_zoom_margin = zoom_margin;
    cam->zoom_lerp = zoom_lerp;
    cam->speed_zoom = speed_zoom;
    cam->speed_zoom_min = speed_min;
    cam->speed_zoom_max = speed_max;
    cam->speed_zoom_speed = speed_ref;
    cam->target_speed = 0.0f;
    cam->pan_tween.active = false;
    cam->has_target = false;

    camera->placed = true;
    camera->interpolate = interpolate;
    camera->has_samples = false;
    camera->pending = false;
    JS_FreeValue(ctx, camera->target);
    camera->target = JS_DupValue(ctx, target);
    /* Also the first interpolation sample: before and after are the same. */
    if (camera_sample(ctx, camera, true))
        return JS_EXCEPTION;
    if (snap)
        athena_camera2d_snap(cam);
    return JS_DupValue(ctx, this_val);
}

static JSValue camera_unfollow(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);

    if (!camera)
        return JS_EXCEPTION;
    athena_camera2d_unfollow(&camera->cam);
    camera->pending = false;
    camera->has_samples = false;
    JS_FreeValue(ctx, camera->target);
    camera->target = JS_UNDEFINED;
    return JS_DupValue(ctx, this_val);
}

static JSValue camera_snap(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);

    if (!camera)
        return JS_EXCEPTION;
    if (camera->cam.following && camera_read_target(ctx, camera))
        return JS_EXCEPTION;
    athena_camera2d_snap(&camera->cam);
    return JS_DupValue(ctx, this_val);
}

static JSValue camera_set_zones(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    JSValueConst zones = camera_arg(argc, argv, 0);
    JSValueConst options = camera_arg(argc, argv, 1);
    AthenaCamera2DZone list[ATHENA_CAMERA2D_MAX_ZONES];
    JSValue length_value, on_change = JS_UNDEFINED;
    uint32_t length = 0, i;
    float transition = 0.0f;

    if (!camera || camera_check_options(ctx, options, "Camera.setZones"))
        return JS_EXCEPTION;
    if (!JS_IsNull(zones) && !JS_IsUndefined(zones)) {
        if (JS_IsArray(ctx, zones) <= 0)
            return JS_ThrowTypeError(ctx, "Camera.setZones expects an array of {x, y, w, h, zoom?}");
        length_value = JS_GetProperty(ctx, zones, state.atoms[ATOM_LENGTH]);
        if (JS_IsException(length_value))
            return JS_EXCEPTION;
        if (JS_ToUint32(ctx, &length, length_value)) {
            JS_FreeValue(ctx, length_value);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, length_value);
        if (length > ATHENA_CAMERA2D_MAX_ZONES)
            return JS_ThrowRangeError(ctx, "Camera.setZones takes at most %d zones",
                ATHENA_CAMERA2D_MAX_ZONES);
        for (i = 0; i < length; i++) {
            JSValue item = JS_GetPropertyUint32(ctx, zones, i), zoom;
            float rect[4];
            int bad;

            if (JS_IsException(item))
                return JS_EXCEPTION;
            bad = camera_rect(ctx, item, rect, 1.0f, "Camera.setZones zone");
            memset(&list[i], 0, sizeof(list[i]));
            if (!bad) {
                int found = camera_option(ctx, item, "zoom", &zoom);
                if (found < 0) {
                    bad = 1;
                } else if (found > 0) {
                    bad = camera_number(ctx, zoom, &list[i].zoom, 1.0e-3f,
                        CAMERA_MAX_ZOOM, "Camera.setZones zone.zoom");
                    JS_FreeValue(ctx, zoom);
                }
            }
            /* A zone may bring its own follow smoothing and offset. */
            if (!bad) {
                int found = camera_option(ctx, item, "lerp", &zoom);
                if (found < 0) {
                    bad = 1;
                } else if (found > 0) {
                    bad = camera_pair(ctx, zoom, &list[i].lerp_x, &list[i].lerp_y,
                        0.0f, CAMERA_MAX_RATE, "Camera.setZones zone.lerp");
                    list[i].has_lerp = !bad;
                    JS_FreeValue(ctx, zoom);
                }
            }
            if (!bad) {
                int found = camera_option(ctx, item, "offset", &zoom);
                if (found < 0) {
                    bad = 1;
                } else if (found > 0) {
                    bad = camera_pair(ctx, zoom, &list[i].offset_x, &list[i].offset_y,
                        -CAMERA_MAX_SIZE, CAMERA_MAX_SIZE, "Camera.setZones zone.offset");
                    list[i].has_offset = !bad;
                    JS_FreeValue(ctx, zoom);
                }
            }
            JS_FreeValue(ctx, item);
            if (bad)
                return JS_EXCEPTION;
            list[i].x = rect[0];
            list[i].y = rect[1];
            list[i].w = rect[2];
            list[i].h = rect[3];
        }
    }
    if (camera_option_number(ctx, options, "transition", &transition, 0.0f,
            CAMERA_MAX_TIME, "Camera.setZones transition") < 0)
        return JS_EXCEPTION;
    if (camera_option(ctx, options, "onChange", &on_change) < 0)
        return JS_EXCEPTION;
    if (!JS_IsUndefined(on_change) && !JS_IsFunction(ctx, on_change)) {
        JS_FreeValue(ctx, on_change);
        return JS_ThrowTypeError(ctx, "Camera.setZones onChange must be a function");
    }
    athena_camera2d_set_zones(&camera->cam, list, (int)length, transition);
    JS_FreeValue(ctx, camera->on_zone);
    camera->on_zone = on_change;
    return JS_DupValue(ctx, this_val);
}

static JSValue camera_shake(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    JSValueConst options = camera_arg(argc, argv, 2);
    float intensity, duration, frequency = 25.0f, rotation = 0.0f;

    if (!camera ||
        camera_number(ctx, camera_arg(argc, argv, 0), &intensity, 0.0f,
            CAMERA_MAX_SIZE, "Camera.shake intensity") ||
        camera_number(ctx, camera_arg(argc, argv, 1), &duration, 0.0f,
            CAMERA_MAX_TIME, "Camera.shake duration") ||
        camera_check_options(ctx, options, "Camera.shake") ||
        camera_option_number(ctx, options, "frequency", &frequency, 0.01f,
            CAMERA_MAX_RATE, "Camera.shake frequency") < 0 ||
        camera_option_number(ctx, options, "rotation", &rotation, 0.0f,
            CAMERA_MAX_ANGLE, "Camera.shake rotation") < 0)
        return JS_EXCEPTION;
    athena_camera2d_shake(&camera->cam, intensity, duration, frequency, rotation);
    return JS_DupValue(ctx, this_val);
}

static JSValue camera_stop_shake(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);

    if (!camera)
        return JS_EXCEPTION;
    athena_camera2d_stop_shake(&camera->cam);
    return JS_DupValue(ctx, this_val);
}

/*
 * addTrauma(amount, options?): trauma (0..1) adds up between impacts and
 * shakes by its square. Options, kept for later calls: intensity (pixels
 * at full trauma, 16), rotation (radians, 0), decay (per second, 1),
 * frequency (25).
 */
static JSValue camera_add_trauma(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    JSValueConst options = camera_arg(argc, argv, 1);
    AthenaCamera2D *cam;
    float amount, intensity, rotation, decay, frequency;

    if (!camera)
        return JS_EXCEPTION;
    cam = &camera->cam;
    intensity = cam->trauma_intensity;
    rotation = cam->trauma_rotation;
    decay = cam->trauma_decay;
    frequency = cam->trauma_frequency;
    if (camera_number(ctx, camera_arg(argc, argv, 0), &amount, -1.0f, 1.0f,
            "Camera.addTrauma amount") ||
        camera_check_options(ctx, options, "Camera.addTrauma") ||
        camera_option_number(ctx, options, "intensity", &intensity, 0.0f,
            CAMERA_MAX_SIZE, "Camera.addTrauma intensity") < 0 ||
        camera_option_number(ctx, options, "rotation", &rotation, 0.0f,
            CAMERA_MAX_ANGLE, "Camera.addTrauma rotation") < 0 ||
        camera_option_number(ctx, options, "decay", &decay, 1.0e-3f,
            CAMERA_MAX_RATE, "Camera.addTrauma decay") < 0 ||
        camera_option_number(ctx, options, "frequency", &frequency, 0.01f,
            CAMERA_MAX_RATE, "Camera.addTrauma frequency") < 0)
        return JS_EXCEPTION;
    athena_camera2d_add_trauma(cam, amount, intensity, rotation, decay, frequency);
    return JS_DupValue(ctx, this_val);
}

/* kick(dx, dy, duration = 0.15): a push of the view that springs back. */
static JSValue camera_kick(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    float dx, dy, duration = 0.15f;

    if (!camera ||
        camera_number(ctx, camera_arg(argc, argv, 0), &dx, -CAMERA_MAX_SIZE,
            CAMERA_MAX_SIZE, "Camera.kick dx") ||
        camera_number(ctx, camera_arg(argc, argv, 1), &dy, -CAMERA_MAX_SIZE,
            CAMERA_MAX_SIZE, "Camera.kick dy") ||
        (argc > 2 && !JS_IsUndefined(argv[2]) &&
            camera_number(ctx, argv[2], &duration, 1.0e-3f, CAMERA_MAX_TIME,
                "Camera.kick duration")))
        return JS_EXCEPTION;
    athena_camera2d_kick(&camera->cam, dx, dy, duration);
    return JS_DupValue(ctx, this_val);
}

static int camera_duration(JSContext *ctx, JSValueConst value, float *out,
    const char *name) {
    if (JS_IsUndefined(value)) {
        *out = 0.0f;
        return 0;
    }
    return camera_number(ctx, value, out, 0.0f, CAMERA_MAX_TIME, name);
}

static JSValue camera_zoom_to(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    float zoom, duration;

    if (!camera ||
        camera_number(ctx, camera_arg(argc, argv, 0), &zoom, 1.0e-3f,
            CAMERA_MAX_ZOOM, "Camera.zoomTo zoom") ||
        camera_duration(ctx, camera_arg(argc, argv, 1), &duration,
            "Camera.zoomTo duration"))
        return JS_EXCEPTION;
    athena_camera2d_zoom_to(&camera->cam, zoom, duration);
    return camera_promise(ctx, camera, SETTLE_ZOOM, duration);
}

static JSValue camera_pan_to(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    float x, y, duration;

    if (!camera ||
        camera_number(ctx, camera_arg(argc, argv, 0), &x, -ATHENA_CAMERA2D_MAX_COORD,
            ATHENA_CAMERA2D_MAX_COORD, "Camera.panTo x") ||
        camera_number(ctx, camera_arg(argc, argv, 1), &y, -ATHENA_CAMERA2D_MAX_COORD,
            ATHENA_CAMERA2D_MAX_COORD, "Camera.panTo y") ||
        camera_duration(ctx, camera_arg(argc, argv, 2), &duration,
            "Camera.panTo duration"))
        return JS_EXCEPTION;
    camera->placed = true;
    athena_camera2d_pan_to(&camera->cam, x, y, duration);
    return camera_promise(ctx, camera, SETTLE_PAN, duration);
}

/* A Color (0xAABBGGRR, alpha 0..128) split into its RGB and alpha. */
static int camera_color(JSContext *ctx, JSValueConst value, uint32_t *rgb,
    float *alpha, const char *name) {
    uint32_t color;

    if (!JS_IsNumber(value)) {
        JS_ThrowTypeError(ctx, "%s must be a Color", name);
        return -1;
    }
    if (JS_ToUint32(ctx, &color, value))
        return -1;
    *rgb = color & 0xFFFFFFu;
    *alpha = (float)(color >> 24);
    return 0;
}

static JSValue camera_fade(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    uint32_t rgb;
    float alpha, duration;

    if (!camera ||
        camera_color(ctx, camera_arg(argc, argv, 0), &rgb, &alpha, "Camera.fade color") ||
        camera_duration(ctx, camera_arg(argc, argv, 1), &duration,
            "Camera.fade duration"))
        return JS_EXCEPTION;
    athena_camera2d_fade(&camera->cam, rgb, alpha, duration);
    return camera_promise(ctx, camera, SETTLE_FADE, duration);
}

static JSValue camera_flash(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    uint32_t rgb;
    float alpha, duration = 0.2f;

    if (!camera ||
        camera_color(ctx, camera_arg(argc, argv, 0), &rgb, &alpha, "Camera.flash color") ||
        (argc > 1 && !JS_IsUndefined(argv[1]) &&
            camera_number(ctx, argv[1], &duration, 0.0f, CAMERA_MAX_TIME,
                "Camera.flash duration")))
        return JS_EXCEPTION;
    athena_camera2d_flash(&camera->cam, rgb, alpha, duration);
    return camera_promise(ctx, camera, SETTLE_FLASH, duration);
}

static JSValue camera_letterbox(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    float amount, duration;

    if (!camera ||
        camera_number(ctx, camera_arg(argc, argv, 0), &amount, 0.0f, 0.5f,
            "Camera.letterbox amount") ||
        camera_duration(ctx, camera_arg(argc, argv, 1), &duration,
            "Camera.letterbox duration"))
        return JS_EXCEPTION;
    athena_camera2d_letterbox(&camera->cam, amount, duration);
    return camera_promise(ctx, camera, SETTLE_LETTERBOX, duration);
}

static int camera_xy_args(JSContext *ctx, int argc, JSValueConst *argv, float *x,
    float *y, const char *name) {
    char label[64];

    snprintf(label, sizeof(label), "%s x", name);
    if (camera_number(ctx, camera_arg(argc, argv, 0), x, -ATHENA_CAMERA2D_MAX_COORD,
            ATHENA_CAMERA2D_MAX_COORD, label))
        return -1;
    snprintf(label, sizeof(label), "%s y", name);
    return camera_number(ctx, camera_arg(argc, argv, 1), y, -ATHENA_CAMERA2D_MAX_COORD,
        ATHENA_CAMERA2D_MAX_COORD, label);
}

static JSValue camera_world_to_screen(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    float x, y;

    if (!camera || camera_xy_args(ctx, argc, argv, &x, &y, "Camera.worldToScreen"))
        return JS_EXCEPTION;
    athena_camera2d_world_to_screen(&camera->cam, x, y, &x, &y);
    return camera_new_point(ctx, x, y);
}

static JSValue camera_screen_to_world(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    float x, y;

    if (!camera || camera_xy_args(ctx, argc, argv, &x, &y, "Camera.screenToWorld"))
        return JS_EXCEPTION;
    athena_camera2d_screen_to_world(&camera->cam, x, y, &x, &y);
    return camera_new_point(ctx, x, y);
}

static JSValue camera_visible_rect(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    AthenaRect2D rect;

    if (!camera)
        return JS_EXCEPTION;
    athena_camera2d_visible_rect(&camera->cam, &rect);
    return camera_new_rect(ctx, &rect);
}

static JSValue camera_is_visible(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    float x, y, w = 0.0f, h = 0.0f;

    if (!camera || camera_xy_args(ctx, argc, argv, &x, &y, "Camera.isVisible"))
        return JS_EXCEPTION;
    if ((argc > 2 && !JS_IsUndefined(argv[2]) &&
            camera_number(ctx, argv[2], &w, 0.0f, CAMERA_MAX_SIZE * 10.0f,
                "Camera.isVisible w")) ||
        (argc > 3 && !JS_IsUndefined(argv[3]) &&
            camera_number(ctx, argv[3], &h, 0.0f, CAMERA_MAX_SIZE * 10.0f,
                "Camera.isVisible h")))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, athena_camera2d_rect_visible(&camera->cam, x, y, w, h));
}

/* The bytes of a typed array of `type`, or a thrown TypeError. */
static uint8_t *camera_typed_array(JSContext *ctx, JSValueConst value, int type,
    int alt_type, size_t *length, const char *name) {
    size_t offset = 0, byte_length = 0, element = 0, buffer_length = 0;
    int actual = JS_GetTypedArrayType(value);
    JSValue buffer;
    uint8_t *data;

    if (actual != type && actual != alt_type) {
        JS_ThrowTypeError(ctx, "%s", name);
        return NULL;
    }
    buffer = JS_GetTypedArrayBuffer(ctx, value, &offset, &byte_length, &element);
    if (JS_IsException(buffer))
        return NULL;
    data = JS_GetArrayBuffer(ctx, &buffer_length, buffer);
    JS_FreeValue(ctx, buffer);
    if (!data)
        return NULL;
    if (offset + byte_length > buffer_length) {
        JS_ThrowRangeError(ctx, "the typed array is out of bounds of its buffer");
        return NULL;
    }
    *length = byte_length;
    return data + offset;
}

/*
 * cull(rects, out?): rects is a Float32Array of (x, y, w, h) boxes; out, a
 * Uint8Array getting 1 for each visible box and 0 otherwise. Returns how
 * many are visible.
 */
static JSValue camera_cull(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    uint8_t *rects, *out = NULL;
    size_t rect_bytes, out_bytes = 0, count, i;
    AthenaRect2D visible;
    int32_t shown = 0;

    if (!camera)
        return JS_EXCEPTION;
    rects = camera_typed_array(ctx, camera_arg(argc, argv, 0), JS_TYPED_ARRAY_FLOAT32,
        JS_TYPED_ARRAY_FLOAT32, &rect_bytes,
        "Camera.cull expects a Float32Array of (x, y, w, h) boxes");
    if (!rects)
        return JS_EXCEPTION;
    count = rect_bytes / (4 * sizeof(float));
    if (argc > 1 && !JS_IsUndefined(argv[1])) {
        out = camera_typed_array(ctx, argv[1], JS_TYPED_ARRAY_UINT8,
            JS_TYPED_ARRAY_UINT8C, &out_bytes,
            "Camera.cull out must be a Uint8Array");
        if (!out)
            return JS_EXCEPTION;
        if (out_bytes < count)
            return JS_ThrowRangeError(ctx,
                "Camera.cull out holds %u entries, %u boxes were given",
                (unsigned)out_bytes, (unsigned)count);
    }
    athena_camera2d_visible_rect(&camera->cam, &visible);
    for (i = 0; i < count; i++) {
        float box[4];
        bool seen;

        /* Typed arrays may start anywhere: the EE faults on unaligned floats. */
        memcpy(box, rects + i * sizeof(box), sizeof(box));
        seen = box[0] + box[2] >= visible.x0 && box[0] <= visible.x1 &&
            box[1] + box[3] >= visible.y0 && box[1] <= visible.y1;
        if (out)
            out[i] = seen ? 1 : 0;
        shown += seen;
    }
    return JS_NewInt32(ctx, shown);
}

/* { parallax: number | [x, y] | {x, y} } */
static int camera_parallax(JSContext *ctx, JSValueConst options, float *px, float *py,
    const char *name) {
    JSValue value;
    int found, bad;

    *px = *py = 1.0f;
    if (camera_check_options(ctx, options, name))
        return -1;
    found = camera_option(ctx, options, "parallax", &value);
    if (found <= 0)
        return found;
    bad = camera_pair(ctx, value, px, py, -100.0f, 100.0f, "parallax");
    JS_FreeValue(ctx, value);
    return bad;
}

static JSValue camera_begin(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    float px, py;

    if (!camera ||
        camera_parallax(ctx, camera_arg(argc, argv, 0), &px, &py, "Camera.begin"))
        return JS_EXCEPTION;
    if (!athena_camera2d_begin(&camera->cam, px, py))
        return JS_ThrowRangeError(ctx,
            "Camera.begin: more than %d cameras are open; call end()",
            ATHENA_CAMERA2D_MAX_DEPTH);
    return JS_UNDEFINED;
}

static JSValue camera_end(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);

    if (!camera)
        return JS_EXCEPTION;
    if (athena_camera2d_depth() <= state.applied_depth)
        return JS_ThrowInternalError(ctx, "Camera.end without a matching begin()");
    athena_camera2d_end(&camera->cam);
    return JS_UNDEFINED;
}

static JSValue camera_call(JSContext *ctx, JSValueConst func) {
    JSValue callback = JS_DupValue(ctx, func);
    JSValue result = JS_Call(ctx, callback, JS_UNDEFINED, 0, NULL);

    JS_FreeValue(ctx, callback);
    return result;
}

/*
 * Ends the pair a draw()/screenSpace()/viewportSpace() opened at stack
 * depth `depth`, drawing `cam`'s effects (NULL: none). Pairs the callback
 * left open (a begin() without end(), a throw) are closed first; if it
 * closed ours already (Camera2D.reset()), nothing is left to do.
 */
static void camera_close(int depth, const AthenaCamera2D *cam) {
    while (athena_camera2d_depth() > depth + 1)
        athena_camera2d_end(NULL);
    if (athena_camera2d_depth() == depth + 1)
        athena_camera2d_end(cam);
}

static JSValue camera_draw(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    JSValueConst func = camera_arg(argc, argv, 0);
    JSValue result;
    float px, py;
    int depth;

    if (!camera)
        return JS_EXCEPTION;
    if (!JS_IsFunction(ctx, func))
        return JS_ThrowTypeError(ctx, "Camera.draw expects a function");
    if (camera_parallax(ctx, camera_arg(argc, argv, 1), &px, &py, "Camera.draw"))
        return JS_EXCEPTION;
    depth = athena_camera2d_depth();
    if (!athena_camera2d_begin(&camera->cam, px, py))
        return JS_ThrowRangeError(ctx, "Camera.draw: more than %d cameras are open",
            ATHENA_CAMERA2D_MAX_DEPTH);
    result = camera_call(ctx, func);
    camera_close(depth, &camera->cam);
    return result;
}

static JSValue camera_viewport_space(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    JSValueConst func = camera_arg(argc, argv, 0);
    AthenaAffine2D offset;
    AthenaRect2D vp;
    JSValue result;
    int depth = athena_camera2d_depth();

    if (!camera)
        return JS_EXCEPTION;
    if (!JS_IsFunction(ctx, func))
        return JS_ThrowTypeError(ctx, "Camera.viewportSpace expects a function");
    athena_camera2d_viewport(&camera->cam, &vp);
    if (!athena_camera2d_begin_screen(vp.x0, vp.y0, vp.x1 - vp.x0, vp.y1 - vp.y0))
        return JS_ThrowRangeError(ctx, "Camera.viewportSpace: more than %d cameras are open",
            ATHENA_CAMERA2D_MAX_DEPTH);
    athena_affine_identity(&offset);
    offset.tx = floorf(vp.x0 + 0.5f);
    offset.ty = floorf(vp.y0 + 0.5f);
    athena_view_set(&offset);
    result = camera_call(ctx, func);
    camera_close(depth, NULL);
    return result;
}

/* A positive number property of an object (an Image's width...). */
static int camera_size_of(JSContext *ctx, JSValueConst object, CameraAtom atom,
    float *out, const char *name) {
    JSValue value = JS_GetProperty(ctx, object, state.atoms[atom]);
    int result;

    if (JS_IsException(value))
        return -1;
    result = camera_number(ctx, value, out, 1.0e-3f, CAMERA_MAX_SIZE, name);
    JS_FreeValue(ctx, value);
    return result;
}

/*
 * drawRepeat(image, options?): the image repeated to cover what the camera
 * shows of a layer, for skies and far scenery. Options: parallax (as
 * draw()), x and y (where one copy sits, in the layer), repeatX and repeatY
 * (true). Returns how many copies were drawn.
 */
static JSValue camera_draw_repeat(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    JSValueConst image = camera_arg(argc, argv, 0);
    JSValueConst options = camera_arg(argc, argv, 1);
    float w, h, px, py, ox = 0.0f, oy = 0.0f;
    bool repeat_x = true, repeat_y = true;
    AthenaAffine2D m, inverse;
    AthenaRect2D clip, visible;
    int depth, i0, i1, j0, j1, drawn = 0;
    JSValue draw;

    if (!camera)
        return JS_EXCEPTION;
    if (!JS_IsObject(image))
        return JS_ThrowTypeError(ctx, "Camera.drawRepeat expects an Image");
    if (camera_size_of(ctx, image, ATOM_WIDTH, &w, "Camera.drawRepeat image.width") ||
        camera_size_of(ctx, image, ATOM_HEIGHT, &h, "Camera.drawRepeat image.height") ||
        camera_parallax(ctx, options, &px, &py, "Camera.drawRepeat") ||
        camera_option_number(ctx, options, "x", &ox, -ATHENA_CAMERA2D_MAX_COORD,
            ATHENA_CAMERA2D_MAX_COORD, "Camera.drawRepeat x") < 0 ||
        camera_option_number(ctx, options, "y", &oy, -ATHENA_CAMERA2D_MAX_COORD,
            ATHENA_CAMERA2D_MAX_COORD, "Camera.drawRepeat y") < 0 ||
        camera_option_bool(ctx, options, "repeatX", &repeat_x) < 0 ||
        camera_option_bool(ctx, options, "repeatY", &repeat_y) < 0)
        return JS_EXCEPTION;
    draw = JS_GetProperty(ctx, image, state.atoms[ATOM_DRAW]);
    if (JS_IsException(draw))
        return draw;
    if (!JS_IsFunction(ctx, draw)) {
        JS_FreeValue(ctx, draw);
        return JS_ThrowTypeError(ctx, "Camera.drawRepeat expects an Image (with draw())");
    }

    /* What the layer shows: the viewport through the inverse of its view. */
    athena_camera2d_matrix(&camera->cam, px, py, &m);
    athena_camera2d_viewport(&camera->cam, &clip);
    if (!athena_affine_invert(&m, &inverse)) {
        JS_FreeValue(ctx, draw);
        return JS_NewInt32(ctx, 0);
    }
    athena_affine_bounds(&inverse, &clip, &visible);
    i0 = repeat_x ? (int)floorf((visible.x0 - ox) / w) : 0;
    i1 = repeat_x ? (int)floorf((visible.x1 - ox) / w) : 0;
    j0 = repeat_y ? (int)floorf((visible.y0 - oy) / h) : 0;
    j1 = repeat_y ? (int)floorf((visible.y1 - oy) / h) : 0;
    if ((i1 - i0 + 1) * (j1 - j0 + 1) > CAMERA_REPEAT_MAX) {
        JS_FreeValue(ctx, draw);
        return JS_ThrowRangeError(ctx,
            "Camera.drawRepeat would draw %d copies (at most %d): use a larger image",
            (i1 - i0 + 1) * (j1 - j0 + 1), CAMERA_REPEAT_MAX);
    }

    depth = athena_camera2d_depth();
    if (!athena_camera2d_begin(&camera->cam, px, py)) {
        JS_FreeValue(ctx, draw);
        return JS_ThrowRangeError(ctx, "Camera.drawRepeat: more than %d cameras are open",
            ATHENA_CAMERA2D_MAX_DEPTH);
    }
    for (int j = j0; j <= j1; j++) {
        for (int i = i0; i <= i1; i++) {
            JSValue args[2] = {
                JS_NewFloat64(ctx, (double)(ox + (float)i * w)),
                JS_NewFloat64(ctx, (double)(oy + (float)j * h)),
            };
            JSValue result = JS_Call(ctx, draw, image, 2, args);

            if (JS_IsException(result)) {
                camera_close(depth, NULL);
                JS_FreeValue(ctx, draw);
                return result;
            }
            JS_FreeValue(ctx, result);
            drawn++;
        }
    }
    camera_close(depth, NULL);
    JS_FreeValue(ctx, draw);
    return JS_NewInt32(ctx, drawn);
}

/*
 * Settings and pose that state() saves and setState() restores, in the
 * order setState() applies them: what the bounds clamp depends on, then
 * the bounds, then the position (saved already clamped: set as is).
 */
static const struct {
    const char *name;
    int prop;
} camera_state_props[] = {
    { "zoomX", PROP_ZOOM_X }, { "zoomY", PROP_ZOOM_Y },
    { "rotation", PROP_ROTATION }, { "anchor", PROP_ANCHOR },
    { "boundsIgnoreRotation", PROP_BOUNDS_IGNORE_ROTATION },
    { "pixelSnap", PROP_PIXEL_SNAP }, { "realTime", PROP_REAL_TIME },
    { "bounds", PROP_BOUNDS }, { "x", PROP_X }, { "y", PROP_Y },
};

/*
 * state(): the pose and settings as a plain object (JSON-friendly), for
 * saves and replays. Targets, zones, callbacks and running effects are not
 * part of it. The viewport is null when it follows the screen.
 */
static JSValue camera_state(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    JSValue object, viewport;

    if (!camera)
        return JS_EXCEPTION;
    object = JS_NewObject(ctx);
    if (JS_IsException(object))
        return object;
    for (size_t i = 0; i < countof(camera_state_props); i++) {
        JSValue value = camera_get(ctx, this_val, camera_state_props[i].prop);

        if (JS_IsException(value) ||
            JS_SetPropertyStr(ctx, object, camera_state_props[i].name, value) < 0) {
            JS_FreeValue(ctx, object);
            return JS_EXCEPTION;
        }
    }
    viewport = camera->cam.viewport_w > 0.0f && camera->cam.viewport_h > 0.0f ?
        camera_get(ctx, this_val, PROP_VIEWPORT) : JS_NULL;
    if (JS_IsException(viewport) ||
        JS_SetPropertyStr(ctx, object, "viewport", viewport) < 0) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    return object;
}

/* setState(state): applies what state() returned; missing keys stay as they are. */
static JSValue camera_set_state(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    JSValueConst saved = camera_arg(argc, argv, 0);
    JSValue value;
    int found;

    if (!camera)
        return JS_EXCEPTION;
    if (!JS_IsObject(saved))
        return JS_ThrowTypeError(ctx, "Camera.setState expects what state() returned");
    /* The viewport first: the position may depend on it. */
    found = camera_option(ctx, saved, "viewport", &value);
    if (found < 0)
        return JS_EXCEPTION;
    if (found > 0) {
        JSValue result = camera_set(ctx, this_val, value, PROP_VIEWPORT);
        JS_FreeValue(ctx, value);
        if (JS_IsException(result))
            return result;
    }
    for (size_t i = 0; i < countof(camera_state_props); i++) {
        JSValue result;

        found = camera_option(ctx, saved, camera_state_props[i].name, &value);
        if (found < 0)
            return JS_EXCEPTION;
        if (found == 0)
            continue;
        result = camera_set(ctx, this_val, value, camera_state_props[i].prop);
        JS_FreeValue(ctx, value);
        if (JS_IsException(result))
            return result;
    }
    return JS_DupValue(ctx, this_val);
}

static JSValue camera_make_current(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);

    if (!camera)
        return JS_EXCEPTION;
    camera_set_current(ctx, this_val);
    return JS_DupValue(ctx, this_val);
}

static JSValue camera_update_method(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    float dt;

    if (!camera ||
        camera_number(ctx, camera_arg(argc, argv, 0), &dt, 0.0f, CAMERA_MAX_TIME,
            "Camera.update dt"))
        return JS_EXCEPTION;
    if (camera_update_one(ctx, camera, dt))
        return JS_EXCEPTION;
    return JS_DupValue(ctx, this_val);
}

static JSValue camera_get_matrix(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    CameraObject *camera = camera_this(ctx, this_val);
    static const char *const names[6] = { "xx", "xy", "yx", "yy", "tx", "ty" };
    AthenaAffine2D m;
    float values[6], px, py;
    JSValue object;
    int i;

    if (!camera ||
        camera_parallax(ctx, camera_arg(argc, argv, 0), &px, &py, "Camera.getMatrix"))
        return JS_EXCEPTION;
    athena_camera2d_matrix(&camera->cam, px, py, &m);
    values[0] = m.xx; values[1] = m.xy; values[2] = m.yx;
    values[3] = m.yy; values[4] = m.tx; values[5] = m.ty;
    object = JS_NewObject(ctx);
    if (JS_IsException(object))
        return object;
    for (i = 0; i < 6; i++) {
        if (JS_SetPropertyStr(ctx, object, names[i],
                JS_NewFloat64(ctx, (double)values[i])) < 0) {
            JS_FreeValue(ctx, object);
            return JS_EXCEPTION;
        }
    }
    return object;
}

static const JSCFunctionListEntry camera_proto_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Camera2D.Camera", JS_PROP_CONFIGURABLE),
    JS_CGETSET_MAGIC_DEF("x", camera_get, camera_set, PROP_X),
    JS_CGETSET_MAGIC_DEF("y", camera_get, camera_set, PROP_Y),
    JS_CGETSET_MAGIC_DEF("zoom", camera_get, camera_set, PROP_ZOOM),
    JS_CGETSET_MAGIC_DEF("zoomX", camera_get, camera_set, PROP_ZOOM_X),
    JS_CGETSET_MAGIC_DEF("zoomY", camera_get, camera_set, PROP_ZOOM_Y),
    JS_CGETSET_MAGIC_DEF("rotation", camera_get, camera_set, PROP_ROTATION),
    JS_CGETSET_MAGIC_DEF("pixelSnap", camera_get, camera_set, PROP_PIXEL_SNAP),
    JS_CGETSET_MAGIC_DEF("anchor", camera_get, camera_set, PROP_ANCHOR),
    JS_CGETSET_MAGIC_DEF("viewport", camera_get, camera_set, PROP_VIEWPORT),
    JS_CGETSET_MAGIC_DEF("bounds", camera_get, camera_set, PROP_BOUNDS),
    JS_CGETSET_MAGIC_DEF("boundsIgnoreRotation", camera_get, camera_set,
        PROP_BOUNDS_IGNORE_ROTATION),
    JS_CGETSET_MAGIC_DEF("realTime", camera_get, camera_set, PROP_REAL_TIME),
    JS_CGETSET_MAGIC_DEF("debug", camera_get, camera_set, PROP_DEBUG),
    JS_CGETSET_MAGIC_DEF("trauma", camera_get, NULL, PROP_TRAUMA),
    JS_CGETSET_MAGIC_DEF("zone", camera_get, NULL, PROP_ZONE),
    JS_CGETSET_MAGIC_DEF("following", camera_get, NULL, PROP_FOLLOWING),
    JS_CGETSET_MAGIC_DEF("shaking", camera_get, NULL, PROP_SHAKING),
    JS_CGETSET_MAGIC_DEF("letterboxAmount", camera_get, NULL, PROP_LETTERBOX),
    JS_CGETSET_MAGIC_DEF("fadeAlpha", camera_get, NULL, PROP_FADE),
    JS_CGETSET_MAGIC_DEF("isCurrent", camera_get, NULL, PROP_CURRENT),
    JS_CFUNC_DEF("setPosition", 2, camera_set_position),
    JS_CFUNC_DEF("move", 2, camera_move),
    JS_CFUNC_DEF("setZoom", 2, camera_set_zoom),
    JS_CFUNC_DEF("setViewport", 4, camera_set_viewport),
    JS_CFUNC_DEF("setBounds", 4, camera_set_bounds),
    JS_CFUNC_DEF("follow", 2, camera_follow),
    JS_CFUNC_DEF("unfollow", 0, camera_unfollow),
    JS_CFUNC_DEF("snap", 0, camera_snap),
    JS_CFUNC_DEF("setZones", 2, camera_set_zones),
    JS_CFUNC_DEF("shake", 3, camera_shake),
    JS_CFUNC_DEF("stopShake", 0, camera_stop_shake),
    JS_CFUNC_DEF("addTrauma", 2, camera_add_trauma),
    JS_CFUNC_DEF("kick", 3, camera_kick),
    JS_CFUNC_DEF("drawRepeat", 2, camera_draw_repeat),
    JS_CFUNC_DEF("state", 0, camera_state),
    JS_CFUNC_DEF("setState", 1, camera_set_state),
    JS_CFUNC_DEF("zoomTo", 2, camera_zoom_to),
    JS_CFUNC_DEF("panTo", 3, camera_pan_to),
    JS_CFUNC_DEF("fade", 2, camera_fade),
    JS_CFUNC_DEF("flash", 2, camera_flash),
    JS_CFUNC_DEF("letterbox", 2, camera_letterbox),
    JS_CFUNC_DEF("worldToScreen", 2, camera_world_to_screen),
    JS_CFUNC_DEF("screenToWorld", 2, camera_screen_to_world),
    JS_CFUNC_DEF("visibleRect", 0, camera_visible_rect),
    JS_CFUNC_DEF("isVisible", 4, camera_is_visible),
    JS_CFUNC_DEF("cull", 2, camera_cull),
    JS_CFUNC_DEF("begin", 1, camera_begin),
    JS_CFUNC_DEF("end", 0, camera_end),
    JS_CFUNC_DEF("draw", 2, camera_draw),
    JS_CFUNC_DEF("viewportSpace", 1, camera_viewport_space),
    JS_CFUNC_DEF("makeCurrent", 0, camera_make_current),
    JS_CFUNC_DEF("update", 1, camera_update_method),
    JS_CFUNC_DEF("getMatrix", 1, camera_get_matrix),
};

/* ---------------------------------------------------------------------- */
/* Module functions                                                       */

static JSValue camera2d_get_current(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    if (camera_ready(ctx))
        return JS_EXCEPTION;
    if (state.transition.active)
        return JS_DupValue(ctx, state.transition.to);
    return JS_DupValue(ctx, state.current);
}

static JSValue camera2d_set_current(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    JSValueConst camera = camera_arg(argc, argv, 0);

    if (camera_ready(ctx))
        return JS_EXCEPTION;
    if (JS_IsUndefined(camera) || JS_IsNull(camera)) {
        camera_set_current(ctx, JS_NULL);
        return JS_UNDEFINED;
    }
    if (!camera_of(camera))
        return JS_ThrowTypeError(ctx, "Camera2D.setCurrent expects a Camera2D.Camera or null");
    camera_set_current(ctx, camera);
    return JS_UNDEFINED;
}

static JSValue camera2d_screen_space(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    JSValueConst func = camera_arg(argc, argv, 0);
    JSValue result;
    int depth = athena_camera2d_depth();

    if (camera_ready(ctx))
        return JS_EXCEPTION;
    if (!JS_IsFunction(ctx, func))
        return JS_ThrowTypeError(ctx, "Camera2D.screenSpace expects a function");
    if (!athena_camera2d_begin_screen(0.0f, 0.0f, 0.0f, 0.0f))
        return JS_ThrowRangeError(ctx, "Camera2D.screenSpace: more than %d cameras are open",
            ATHENA_CAMERA2D_MAX_DEPTH);
    result = camera_call(ctx, func);
    camera_close(depth, NULL);
    return result;
}

/* { ease?, realTime? } of a transition; `ease` is left undefined or owned. */
static int camera_transition_options(JSContext *ctx, JSValueConst options,
    JSValue *ease, bool *real_time, const char *name) {
    *ease = JS_UNDEFINED;
    *real_time = false;
    if (camera_check_options(ctx, options, name) ||
        camera_option_bool(ctx, options, "realTime", real_time) < 0 ||
        camera_option(ctx, options, "ease", ease) < 0)
        return -1;
    if (!JS_IsUndefined(*ease) && !JS_IsFunction(ctx, *ease)) {
        JS_FreeValue(ctx, *ease);
        *ease = JS_UNDEFINED;
        JS_ThrowTypeError(ctx, "%s ease must be a function", name);
        return -1;
    }
    return 0;
}

/*
 * Makes `to` (a camera, or null) current, gliding from what is on screen
 * over `duration` seconds; takes `ease`. Returns the promise.
 */
static JSValue camera_start_transition(JSContext *ctx, JSValueConst to,
    float duration, JSValue ease, bool real_time) {
    JSValue funcs[2], promise, from;

    promise = JS_NewPromiseCapability(ctx, funcs);
    if (JS_IsException(promise)) {
        JS_FreeValue(ctx, ease);
        return promise;
    }
    JS_FreeValue(ctx, funcs[1]);

    /* The blend starts from what is on screen, even mid-transition. */
    from = state.transition.active ? JS_DupValue(ctx, state.transition.to) :
        JS_DupValue(ctx, state.current);
    if (state.transition.active)
        camera_transition_end(ctx, false);
    JS_FreeValue(ctx, state.current);
    state.current = JS_IsUndefined(to) ? JS_NULL : JS_DupValue(ctx, to);

    if (duration <= 0.0f || !camera_of(from) || !camera_of(to) ||
        camera_of(from) == camera_of(to)) {
        JSValue arg = JS_NewBool(ctx, true);
        JSValue result = JS_Call(ctx, funcs[0], JS_UNDEFINED, 1, &arg);

        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, funcs[0]);
        JS_FreeValue(ctx, from);
        JS_FreeValue(ctx, ease);
        return promise;
    }
    state.transition.active = true;
    state.transition.real_time = real_time;
    state.transition.from = from;
    state.transition.to = JS_DupValue(ctx, to);
    state.transition.ease = ease;
    state.transition.settle = funcs[0];
    state.transition.time = 0.0f;
    state.transition.duration = duration;
    state.transition.t = 0.0f;
    return promise;
}

static JSValue camera2d_transition(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    JSValueConst to = camera_arg(argc, argv, 0);
    JSValue ease;
    bool real_time;
    float duration;

    if (camera_ready(ctx))
        return JS_EXCEPTION;
    if (!camera_of(to))
        return JS_ThrowTypeError(ctx, "Camera2D.transition expects a Camera2D.Camera");
    if (camera_number(ctx, camera_arg(argc, argv, 1), &duration, 0.0f,
            CAMERA_MAX_TIME, "Camera2D.transition duration") ||
        camera_transition_options(ctx, camera_arg(argc, argv, 2), &ease, &real_time,
            "Camera2D.transition"))
        return JS_EXCEPTION;
    return camera_start_transition(ctx, to, duration, ease, real_time);
}

/* { duration?, ease?, realTime? } of push() and pop(). */
static int camera_stack_options(JSContext *ctx, JSValueConst options,
    float *duration, JSValue *ease, bool *real_time, const char *name) {
    *duration = 0.0f;
    if (camera_transition_options(ctx, options, ease, real_time, name))
        return -1;
    if (camera_option_number(ctx, options, "duration", duration, 0.0f,
            CAMERA_MAX_TIME, name) < 0) {
        JS_FreeValue(ctx, *ease);
        return -1;
    }
    return 0;
}

/*
 * push(camera, options?): makes `camera` current and remembers the one it
 * replaces (or null), for pop(). With a duration, a transition.
 */
static JSValue camera2d_push(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    JSValueConst to = camera_arg(argc, argv, 0);
    JSValue ease;
    bool real_time;
    float duration;

    if (camera_ready(ctx))
        return JS_EXCEPTION;
    if (!camera_of(to))
        return JS_ThrowTypeError(ctx, "Camera2D.push expects a Camera2D.Camera");
    if (state.stack_count >= CAMERA_STACK_MAX)
        return JS_ThrowRangeError(ctx, "Camera2D.push: more than %d cameras pushed",
            CAMERA_STACK_MAX);
    if (camera_stack_options(ctx, camera_arg(argc, argv, 1), &duration, &ease,
            &real_time, "Camera2D.push"))
        return JS_EXCEPTION;
    /* What is current for the game: the target of a running transition. */
    state.stack[state.stack_count++] = JS_DupValue(ctx, state.transition.active ?
        state.transition.to : state.current);
    return camera_start_transition(ctx, to, duration, ease, real_time);
}

/* pop(options?): goes back to the camera the last push() replaced. */
static JSValue camera2d_pop(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    JSValue ease, previous, promise;
    bool real_time;
    float duration;

    if (camera_ready(ctx))
        return JS_EXCEPTION;
    if (state.stack_count == 0)
        return JS_ThrowRangeError(ctx, "Camera2D.pop without a push()");
    if (camera_stack_options(ctx, camera_arg(argc, argv, 0), &duration, &ease,
            &real_time, "Camera2D.pop"))
        return JS_EXCEPTION;
    previous = state.stack[--state.stack_count];
    state.stack[state.stack_count] = JS_UNDEFINED;
    promise = camera_start_transition(ctx, previous, duration, ease, real_time);
    JS_FreeValue(ctx, previous);
    return promise;
}

/* getStats(): { culled, pushed } — culled: draws skipped last frame (Loop). */
static JSValue camera2d_get_stats(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    JSValue stats;

    if (camera_ready(ctx))
        return JS_EXCEPTION;
    stats = JS_NewObject(ctx);
    if (JS_IsException(stats))
        return stats;
    if (JS_SetPropertyStr(ctx, stats, "culled", JS_NewUint32(ctx, state.culled_last)) < 0 ||
        JS_SetPropertyStr(ctx, stats, "pushed", JS_NewInt32(ctx, state.stack_count)) < 0) {
        JS_FreeValue(ctx, stats);
        return JS_EXCEPTION;
    }
    return stats;
}

static JSValue camera2d_update(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    float dt;

    if (camera_ready(ctx) ||
        camera_number(ctx, camera_arg(argc, argv, 0), &dt, 0.0f, CAMERA_MAX_TIME,
            "Camera2D.update dt"))
        return JS_EXCEPTION;
    if (camera_update_all(ctx, dt))
        return JS_EXCEPTION;
    return JS_UNDEFINED;
}

static JSValue camera2d_reset(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    if (camera_ready(ctx))
        return JS_EXCEPTION;
    athena_camera2d_unwind();
    state.applied_depth = 0;
    athena_view_set(NULL);
    athena_view_set_clip(0, 0, 0, 0);
    athena_view_set_world(NULL);
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry camera2d_funcs[] = {
    JS_CFUNC_DEF("getCurrent", 0, camera2d_get_current),
    JS_CFUNC_DEF("setCurrent", 1, camera2d_set_current),
    JS_CFUNC_DEF("screenSpace", 1, camera2d_screen_space),
    JS_CFUNC_DEF("transition", 3, camera2d_transition),
    JS_CFUNC_DEF("push", 2, camera2d_push),
    JS_CFUNC_DEF("pop", 1, camera2d_pop),
    JS_CFUNC_DEF("getStats", 0, camera2d_get_stats),
    JS_CFUNC_DEF("update", 1, camera2d_update),
    JS_CFUNC_DEF("reset", 0, camera2d_reset),
};

static int camera2d_module_init(JSContext *ctx, JSModuleDef *module) {
    JSValue proto, constructor, main = JS_NULL;
    int i;

    if (athena_register_class(ctx, &camera_class_id, &camera_class) < 0)
        return -1;
    proto = JS_NewObject(ctx);
    if (JS_IsException(proto))
        return -1;
    JS_SetPropertyFunctionList(ctx, proto, camera_proto_funcs,
        countof(camera_proto_funcs));
    constructor = JS_NewCFunction2(ctx, camera_ctor, "Camera", 1,
        JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, constructor, proto);
    JS_SetClassProto(ctx, camera_class_id, proto);
    if (JS_SetModuleExport(ctx, module, "Camera", constructor) < 0)
        return -1;

    if (!state.ctx) {
        state.ctx = ctx;
        state.main = JS_UNDEFINED;
        state.current = JS_NULL;
        state.system_id = 0;
        state.applied_depth = 0;
        state.transition.active = false;
        state.transition.from = state.transition.to = JS_UNDEFINED;
        state.transition.ease = state.transition.settle = JS_UNDEFINED;
        state.real_system_id = 0;
        state.real_dt = 0.0f;
        state.stack_count = 0;
        state.culled_last = 0;
        for (i = 0; i < ATOM_COUNT; i++)
            state.atoms[i] = JS_NewAtom(ctx, camera_atom_names[i]);
        /* Camera2D.main: current from the start, at the identity view. */
        main = camera_create(ctx, JS_UNDEFINED, JS_UNDEFINED);
        if (JS_IsException(main))
            return -1;
        state.main = JS_DupValue(ctx, main);
        state.current = JS_DupValue(ctx, main);
    }
    if (JS_SetModuleExport(ctx, module, "main", main) < 0)
        return -1;
    return JS_SetModuleExportList(ctx, module, camera2d_funcs,
        countof(camera2d_funcs));
}

JSModuleDef *athena_camera2d_js_init(JSContext *ctx) {
    JSModuleDef *module = athena_push_module(ctx, camera2d_module_init,
        camera2d_funcs, countof(camera2d_funcs), "Camera2D");

    if (module) {
        JS_AddModuleExport(ctx, module, "Camera");
        JS_AddModuleExport(ctx, module, "main");
    }
    return module;
}

void athena_camera2d_js_cleanup(JSContext *ctx) {
    int i;

    if (ctx != state.ctx)
        return;
    if (state.system_id > 0)
        athena_loop_system_remove(state.system_id);
    state.system_id = 0;
    if (state.real_system_id > 0)
        athena_loop_system_remove(state.real_system_id);
    state.real_system_id = 0;
    while (state.stack_count > 0)
        JS_FreeValue(ctx, state.stack[--state.stack_count]);
    /* The next script starts from the identity view. */
    athena_camera2d_unwind();
    state.applied_depth = 0;
    athena_view_set(NULL);
    athena_view_set_world(NULL);
    /*
     * No JavaScript runs any more: pending promises are dropped, not
     * settled, and the cameras they pinned are let go. Unpinning may free
     * a camera, and through its target the next one: hold that one first.
     */
    {
        CameraObject *camera = camera_list;
        JSValue held = camera ? JS_DupValue(ctx, camera_value(camera)) : JS_UNDEFINED;

        while (camera) {
            CameraObject *next = camera->next;
            JSValue held_next = next ? JS_DupValue(ctx, camera_value(next)) :
                JS_UNDEFINED;

            if (camera->pinned) {
                camera->pinned = false;
                JS_FreeValue(ctx, camera_value(camera));
            }
            JS_FreeValue(ctx, held);
            held = held_next;
            camera = next;
        }
    }
    JS_FreeValue(ctx, state.transition.settle);
    state.transition.settle = JS_UNDEFINED;
    camera_transition_clear(ctx);
    JS_FreeValue(ctx, state.current);
    JS_FreeValue(ctx, state.main);
    state.current = JS_NULL;
    state.main = JS_UNDEFINED;
    for (i = 0; i < ATOM_COUNT; i++)
        JS_FreeAtom(ctx, state.atoms[i]);
    state.ctx = NULL;
}
