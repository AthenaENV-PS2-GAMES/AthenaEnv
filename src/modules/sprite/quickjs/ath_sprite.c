#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ath_env.h>

#include <athena/image.h>
#include <athena/js/image.h>
#include <athena/js/job.h>
#include <athena/js/tilemap.h>
#include <athena/loop.h>
#include <athena/random.h>
#include <athena/sprite.h>
#include "ath_sprite.h"

/*
 * Sprite.Sheet wraps an AthenaSpriteSheet and the Image it draws from;
 * Sprite.Instance plays its clips and draws them; Sprite.Animator plays
 * clips on a range of TileMap sprites, writing their texture coordinates.
 *
 * A native Loop system (UPDATE phase, scaled delta) advances every playing
 * instance and every bound animator in C. Events are collected during that
 * pass, with the instances held, and only then dispatched to JavaScript, so
 * a callback that drops or stops sprites never touches the list being
 * walked. The system is added on first use: scripts without sprites run
 * exactly as before.
 */

#define SPRITE_SYSTEM_NAME "sprite"
/* After the cameras (-1000), before systems added from JavaScript (0). */
#define SPRITE_SYSTEM_PRIORITY (-500)
/* Largest coordinate accepted: keeps float math and the GS range sane. */
#define SPRITE_MAX_COORD 1.0e7f
#define SPRITE_MAX_SCALE 1.0e4f
#define SPRITE_MAX_SPEED 1.0e3f
#define SPRITE_MAX_FPS 1000.0f
#define SPRITE_DEFAULT_FPS 12.0f
/*
 * Sheet files larger than this are refused: the text, the parsed objects
 * and the sheet take several times its size of the console's 32 MB.
 */
#define SPRITE_MAX_JSON_BYTES (2u * 1024u * 1024u)
/*
 * Nested advances (a listener calling Sprite.update(), whose listeners call
 * it again...) before a RangeError, instead of exhausting the EE's stack.
 */
#define SPRITE_MAX_DEPTH 8
#define SPRITE_REALTIME_NAME "sprite.realtime"

static JSClassID sheet_class_id;
static JSClassID instance_class_id;
static JSClassID animator_class_id;

typedef struct {
    AthenaSpriteSheet sheet;
    /* Image or null. */
    JSValue image;
    /* fromGrid(): the grid, for clips given by row or column. */
    bool has_grid;
    uint32_t grid_columns, grid_rows, grid_first, grid_count;
} SheetObject;

typedef enum {
    LISTEN_FRAME,       /* "frame": every frame change */
    LISTEN_FRAME_AT,    /* "frame:N" or "clip:N": position N */
    LISTEN_LOOP,        /* "loop" or "loop:clip" */
    LISTEN_END          /* "end" or "end:clip" */
} ListenType;

#define MASK_FRAME (1u << 0)
#define MASK_LOOP  (1u << 1)
#define MASK_END   (1u << 2)

typedef struct {
    ListenType type;
    uint32_t position;
    /* Clip index the event must come from, or -1 for any clip. */
    int32_t clip;
    bool once;
    JSValue func;
} Listener;

typedef struct SpriteObject {
    /* The JSObject, to hold the instance while its events are pending. */
    void *object;
    JSValue sheet;
    SheetObject *data;
    AthenaSpriteAnim anim;
    AthenaSpriteDraw draw;
    bool auto_update;
    /* Advanced by the real (unscaled) delta: plays on while the game is paused. */
    bool real_time;
    /* Paused by pause() or stop(): resume() continues. */
    bool paused;
    /* Draws its outline, origin and slices over it. */
    bool debug;
    /* Counts play() calls, so events and promises know which play they belong to. */
    uint32_t play_id;
    /* playAsync(): resolve function (or undefined), its play and clip. */
    JSValue settle;
    uint32_t settle_play;
    int32_t settle_clip;
    Listener *listeners;
    uint32_t listener_count, listener_capacity;
    uint32_t mask;
    /* In the list of playing instances the system advances. */
    bool linked;
    struct SpriteObject *prev, *next;
} SpriteObject;

typedef struct AnimatorObject {
    void *object;
    JSValue instance;
    JSValue sheet;
    SheetObject *data;
    AthenaSpriteAnimatorEntry *entries;
    uint32_t first, count;
    float speed;
    bool paused;
    bool real_time;
    /* Bound animators are held by the module until unbind(). */
    bool bound;
    /* randomStart of this animator, seeded by bind()'s seed. */
    AthenaRandom rng;
    struct AnimatorObject *prev, *next;
} AnimatorObject;

typedef struct {
    JSValue sprite;
    AthenaSpriteEventType type;
    uint32_t play_id;
    int32_t clip;
    uint32_t position;
    uint32_t frame;
} PendingEvent;

/*
 * Module state, owned by the context that initialized the module first:
 * the main script (workers cannot draw). Sheets work in any context.
 */
static struct {
    JSContext *ctx;
    int system_id;
    /* Advances real-time instances and animators by the real delta. */
    int realtime_id;
    SpriteObject *playing;
    AnimatorObject *animators;
    PendingEvent *pending;
    uint32_t pending_count, pending_capacity;
    /* Nesting of advances and dispatches; see SPRITE_MAX_DEPTH. */
    int depth;
    /* The last Sprite.drawAll(): sprites sent and culled. */
    uint32_t drawn, culled;
    /* Main thread only, so one batch buffer serves every drawAll(). */
    AthenaSpriteBatch batch;
    /* Sprite.setDebug(): the debug overlay on every instance. */
    bool debug;
} state;

/* ---------------------------------------------------------------------- */
/* Values                                                                 */

static bool float_bits_finite(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

/*
 * A number in [min, max] into a float, or a thrown TypeError/RangeError
 * naming `name`. The EE has no infinities nor NaN: float32 values are
 * checked by their bits and other numbers as doubles (soft-float, IEEE).
 * `*out` is only written on success: a rejected setter changes nothing.
 */
static int sprite_number(JSContext *ctx, JSValueConst value, float *out,
    float min, float max, const char *name) {
    double number;
    float f;

    switch (JS_VALUE_GET_TAG(value)) {
    case JS_TAG_INT:
        f = (float)JS_VALUE_GET_INT(value);
        if (f >= min && f <= max) {
            *out = f;
            return 0;
        }
        break;
    case JS_CUSTOM_TAG_FLOAT32:
        f = JS_VALUE_GET_FLOAT32(value);
        if (float_bits_finite(f) && f >= min && f <= max) {
            *out = f;
            return 0;
        }
        break;
    default:
        if (!JS_IsNumber(value)) {
            JS_ThrowTypeError(ctx, "%s must be a number", name);
            return -1;
        }
        if (JS_ToFloat64(ctx, &number, value))
            return -1;
        if (number >= (double)min && number <= (double)max) {
            *out = (float)number;
            return 0;
        }
        break;
    }
    JS_ThrowRangeError(ctx, "%s must be a finite number from %g to %g", name,
        (double)min, (double)max);
    return -1;
}

/* An integer in [0, max]. */
static int sprite_uint(JSContext *ctx, JSValueConst value, uint32_t *out,
    uint32_t max, const char *name) {
    double number;

    if (JS_VALUE_GET_TAG(value) == JS_TAG_INT) {
        int32_t i = JS_VALUE_GET_INT(value);

        if (i >= 0 && (uint32_t)i <= max) {
            *out = (uint32_t)i;
            return 0;
        }
    } else {
        if (!JS_IsNumber(value)) {
            JS_ThrowTypeError(ctx, "%s must be a number", name);
            return -1;
        }
        if (JS_ToFloat64(ctx, &number, value))
            return -1;
        if (number >= 0.0 && number <= (double)max && number == floor(number)) {
            *out = (uint32_t)number;
            return 0;
        }
    }
    JS_ThrowRangeError(ctx, "%s must be an integer from 0 to %u", name,
        (unsigned)max);
    return -1;
}

static JSValueConst sprite_arg(int argc, JSValueConst *argv, int index) {
    return index < argc ? argv[index] : JS_UNDEFINED;
}

/* The length of an array. */
static int sprite_length(JSContext *ctx, JSValueConst array, uint32_t *length) {
    JSValue value = JS_GetPropertyStr(ctx, array, "length");
    int result = JS_IsException(value) ? -1 : JS_ToUint32(ctx, length, value);

    JS_FreeValue(ctx, value);
    return result ? -1 : 0;
}

/* Optional property: 0 when absent or undefined, 1 when read, -1 on error. */
static int sprite_option(JSContext *ctx, JSValueConst options, const char *key,
    JSValue *out) {
    JSValue value;

    if (!JS_IsObject(options))
        return 0;
    value = JS_GetPropertyStr(ctx, options, key);
    if (JS_IsException(value))
        return -1;
    if (JS_IsUndefined(value))
        return 0;
    *out = value;
    return 1;
}

static int sprite_check_options(JSContext *ctx, JSValueConst options,
    const char *name) {
    if (JS_IsUndefined(options) || (JS_IsObject(options) &&
        !JS_IsFunction(ctx, options) && !JS_IsArray(ctx, options)))
        return 0;
    JS_ThrowTypeError(ctx, "%s options must be an object", name);
    return -1;
}

/* A numeric option: 0 when absent (untouched), 1 when read, -1 on error. */
static int sprite_option_number(JSContext *ctx, JSValueConst options,
    const char *key, float *out, float min, float max, const char *name) {
    JSValue value;
    char label[96];
    int found = sprite_option(ctx, options, key, &value), result;

    if (found <= 0)
        return found;
    snprintf(label, sizeof(label), "%s.%s", name, key);
    result = sprite_number(ctx, value, out, min, max, label);
    JS_FreeValue(ctx, value);
    return result ? -1 : 1;
}

static int sprite_option_uint(JSContext *ctx, JSValueConst options,
    const char *key, uint32_t *out, uint32_t max, const char *name) {
    JSValue value;
    char label[96];
    int found = sprite_option(ctx, options, key, &value), result;

    if (found <= 0)
        return found;
    snprintf(label, sizeof(label), "%s.%s", name, key);
    result = sprite_uint(ctx, value, out, max, label);
    JS_FreeValue(ctx, value);
    return result ? -1 : 1;
}

static int sprite_option_bool(JSContext *ctx, JSValueConst options,
    const char *key, bool *out) {
    JSValue value;
    int found = sprite_option(ctx, options, key, &value);

    if (found <= 0)
        return found;
    *out = JS_ToBool(ctx, value) != 0;
    JS_FreeValue(ctx, value);
    return 0;
}

/* A required numeric property of an object. */
static int sprite_field(JSContext *ctx, JSValueConst object, const char *key,
    float *out, float min, float max, const char *name) {
    JSValue value = JS_GetPropertyStr(ctx, object, key);
    char label[96];
    int result;

    if (JS_IsException(value))
        return -1;
    snprintf(label, sizeof(label), "%s.%s", name, key);
    result = sprite_number(ctx, value, out, min, max, label);
    JS_FreeValue(ctx, value);
    return result;
}

/* A number for both, [a, b] or { x, y }. */
static int sprite_pair(JSContext *ctx, JSValueConst value, float *a, float *b,
    float min, float max, const char *name) {
    float x = 0.0f, y = 0.0f;
    int result;

    if (JS_IsNumber(value)) {
        result = sprite_number(ctx, value, &x, min, max, name);
        y = x;
    } else if (JS_IsArray(ctx, value)) {
        JSValue vx = JS_GetPropertyUint32(ctx, value, 0);
        JSValue vy = JS_GetPropertyUint32(ctx, value, 1);

        result = sprite_number(ctx, vx, &x, min, max, name) ||
            sprite_number(ctx, vy, &y, min, max, name) ? -1 : 0;
        JS_FreeValue(ctx, vx);
        JS_FreeValue(ctx, vy);
    } else if (JS_IsObject(value)) {
        result = sprite_field(ctx, value, "x", &x, min, max, name) ||
            sprite_field(ctx, value, "y", &y, min, max, name) ? -1 : 0;
    } else {
        JS_ThrowTypeError(ctx, "%s must be a number, [x, y] or { x, y }", name);
        return -1;
    }
    /* Both or neither: a rejected pair changes nothing. */
    if (!result) {
        *a = x;
        *b = y;
    }
    return result;
}

static int sprite_color(JSContext *ctx, JSValueConst value, uint32_t *out,
    const char *name) {
    if (!JS_IsNumber(value)) {
        JS_ThrowTypeError(ctx, "%s must be a color (Color.new())", name);
        return -1;
    }
    return JS_ToUint32(ctx, out, value) ? -1 : 0;
}

/* Throws the error of a negative athena_sprite_* result. */
static JSValue sprite_throw(JSContext *ctx, int code, const char *name) {
    switch (code) {
    case ATHENA_SPRITE_ENOMEM:
        return JS_ThrowOutOfMemory(ctx);
    case ATHENA_SPRITE_EEXIST:
        return JS_ThrowTypeError(ctx, "%s: the name is already used", name);
    case ATHENA_SPRITE_ERANGE:
        return JS_ThrowRangeError(ctx, "%s: frame index or count out of range", name);
    default:
        return JS_ThrowTypeError(ctx, "%s: invalid value", name);
    }
}

static JSValue sprite_new_object(JSContext *ctx, JSValueConst new_target,
    JSClassID class_id) {
    JSValue proto, object;

    if (JS_IsUndefined(new_target))
        return JS_NewObjectClass(ctx, class_id);
    proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    if (JS_IsException(proto))
        return proto;
    object = JS_NewObjectProtoClass(ctx, proto, class_id);
    JS_FreeValue(ctx, proto);
    return object;
}

static JSValue sprite_value_of(void *object) {
    return JS_MKPTR(JS_TAG_OBJECT, object);
}

/* ---------------------------------------------------------------------- */
/* Loop system                                                            */

static int sprite_system(void *opaque, AthenaLoopPhase phase, float value);

static int sprite_main(JSContext *ctx, const char *name) {
    if (ctx == state.ctx)
        return 0;
    JS_ThrowInternalError(ctx, "%s is only available on the main script", name);
    return -1;
}

/*
 * Adds the Loop systems on first use: one in UPDATE with the scaled delta,
 * and, once something plays in real time, one in PRE_UPDATE with the real
 * delta (it keeps playing while Loop.setTimeScale(0) pauses the game).
 */
static void sprite_ready(bool real_time) {
    if (state.system_id <= 0) {
        AthenaLoopSystemDesc desc = {
            .name = SPRITE_SYSTEM_NAME,
            .priority = SPRITE_SYSTEM_PRIORITY,
            .phases = ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_UPDATE),
            .real_time = false,
            .func = sprite_system,
        };
        int id = athena_loop_system_add(&desc);

        /* Without the system, Sprite.update() still works. */
        if (id > 0)
            state.system_id = id;
    }
    if (real_time && state.realtime_id <= 0) {
        AthenaLoopSystemDesc desc = {
            .name = SPRITE_REALTIME_NAME,
            .priority = SPRITE_SYSTEM_PRIORITY,
            .phases = ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_PRE_UPDATE),
            .real_time = true,
            .func = sprite_system,
        };
        int id = athena_loop_system_add(&desc);

        if (id > 0)
            state.realtime_id = id;
    }
}

static void sprite_unlink(SpriteObject *sprite) {
    if (!sprite->linked)
        return;
    if (sprite->prev)
        sprite->prev->next = sprite->next;
    else
        state.playing = sprite->next;
    if (sprite->next)
        sprite->next->prev = sprite->prev;
    sprite->prev = sprite->next = NULL;
    sprite->linked = false;
}

/* Keeps the instance in the playing list exactly while the system must advance it. */
static void sprite_sync_link(SpriteObject *sprite) {
    bool wanted = sprite->auto_update && sprite->anim.playing;

    if (wanted)
        sprite_ready(sprite->real_time);
    if (wanted == sprite->linked)
        return;
    if (!wanted) {
        sprite_unlink(sprite);
        return;
    }
    sprite->prev = NULL;
    sprite->next = state.playing;
    if (state.playing)
        state.playing->prev = sprite;
    state.playing = sprite;
    sprite->linked = true;
}

/* ---- playAsync() promises ---------------------------------------------- */

/*
 * An instance with a pending playAsync() holds a reference to itself, so a
 * script awaiting it without keeping the instance still gets its answer.
 * The pinned ones are listed for cleanup, which drops them unsettled.
 */
static SpriteObject **sprite_pinned;
static uint32_t sprite_pinned_count, sprite_pinned_capacity;

static int sprite_pin(JSContext *ctx, SpriteObject *sprite) {
    if (sprite_pinned_count == sprite_pinned_capacity) {
        uint32_t capacity = sprite_pinned_capacity ? sprite_pinned_capacity * 2 : 8;
        SpriteObject **pinned = realloc(sprite_pinned, capacity * sizeof(*pinned));

        if (!pinned) {
            JS_ThrowOutOfMemory(ctx);
            return -1;
        }
        sprite_pinned = pinned;
        sprite_pinned_capacity = capacity;
    }
    sprite_pinned[sprite_pinned_count++] = sprite;
    JS_DupValue(ctx, sprite_value_of(sprite->object));
    return 0;
}

/* Forgets the pin; the caller then drops the reference (last use of the sprite). */
static void sprite_unpin(SpriteObject *sprite) {
    uint32_t i;

    for (i = 0; i < sprite_pinned_count; i++) {
        if (sprite_pinned[i] == sprite) {
            sprite_pinned[i] = sprite_pinned[--sprite_pinned_count];
            return;
        }
    }
}

/*
 * Settles a pending playAsync() with `value` (true: the clip ended, false:
 * another play, stop() or a still frame replaced it). May free the sprite:
 * callers hold it, or use it no more.
 */
static int sprite_settle(JSContext *ctx, SpriteObject *sprite, bool value) {
    JSValue resolve = sprite->settle, arg = JS_NewBool(ctx, value), ret;
    JSValue self = sprite_value_of(sprite->object);

    if (JS_IsUndefined(resolve))
        return 0;
    sprite->settle = JS_UNDEFINED;
    ret = JS_Call(ctx, resolve, JS_UNDEFINED, 1, &arg);
    JS_FreeValue(ctx, resolve);
    sprite_unpin(sprite);
    JS_FreeValue(ctx, self);
    if (JS_IsException(ret))
        return -1;
    JS_FreeValue(ctx, ret);
    return 0;
}

/* ---- Events ------------------------------------------------------------- */

/* Events the instance must collect: its listeners' and a pending playAsync()'s end. */
static uint32_t sprite_mask(const SpriteObject *sprite) {
    return sprite->mask | (JS_IsUndefined(sprite->settle) ? 0u : MASK_END);
}

/* Collects an event of an instance with a listener for it. */
static void sprite_collect(void *opaque, AthenaSpriteEventType type,
    uint32_t position, uint32_t frame) {
    SpriteObject *sprite = opaque;
    uint32_t needed = type == ATHENA_SPRITE_EVENT_FRAME ? MASK_FRAME :
        type == ATHENA_SPRITE_EVENT_LOOP ? MASK_LOOP : MASK_END;
    PendingEvent *event;

    if (!(sprite_mask(sprite) & needed))
        return;
    if (state.pending_count == state.pending_capacity) {
        uint32_t capacity = state.pending_capacity ? state.pending_capacity * 2 : 32;
        PendingEvent *pending = realloc(state.pending, capacity * sizeof(*pending));

        /* Out of memory: the event is lost, the game goes on. */
        if (!pending)
            return;
        state.pending = pending;
        state.pending_capacity = capacity;
    }
    event = &state.pending[state.pending_count++];
    event->sprite = JS_DupValue(state.ctx, sprite_value_of(sprite->object));
    event->type = type;
    event->play_id = sprite->play_id;
    event->clip = sprite->anim.clip;
    event->position = position;
    event->frame = frame;
}

static void sprite_advance(SpriteObject *sprite, float dt) {
    athena_sprite_anim_advance(&sprite->anim, &sprite->data->sheet, dt,
        sprite_mask(sprite) ? sprite_collect : NULL, sprite);
}

static bool listener_matches(const Listener *listener, const PendingEvent *event) {
    if (listener->clip >= 0 && listener->clip != event->clip)
        return false;
    switch (event->type) {
    case ATHENA_SPRITE_EVENT_FRAME:
        return listener->type == LISTEN_FRAME ||
            (listener->type == LISTEN_FRAME_AT && listener->position == event->position);
    case ATHENA_SPRITE_EVENT_LOOP:
        return listener->type == LISTEN_LOOP;
    default:
        return listener->type == LISTEN_END;
    }
}

static void sprite_update_mask(SpriteObject *sprite) {
    uint32_t i, mask = 0;

    for (i = 0; i < sprite->listener_count; i++) {
        switch (sprite->listeners[i].type) {
        case LISTEN_FRAME:
        case LISTEN_FRAME_AT:
            mask |= MASK_FRAME;
            break;
        case LISTEN_LOOP:
            mask |= MASK_LOOP;
            break;
        case LISTEN_END:
            mask |= MASK_END;
            break;
        }
    }
    sprite->mask = mask;
}

static void sprite_remove_listener(JSContext *ctx, SpriteObject *sprite, uint32_t index) {
    JS_FreeValue(ctx, sprite->listeners[index].func);
    memmove(&sprite->listeners[index], &sprite->listeners[index + 1],
        (sprite->listener_count - index - 1) * sizeof(*sprite->listeners));
    sprite->listener_count--;
}

/*
 * Calls the listeners of one event, `this` being the instance, with
 * (clip, position, frame), and settles the playAsync() that the event ends.
 * The matching listeners are copied first: callbacks may add or remove
 * listeners. The event holds the instance, so it outlives the callbacks.
 */
static int sprite_dispatch_one(JSContext *ctx, PendingEvent *event) {
    SpriteObject *sprite = JS_GetOpaque(event->sprite, instance_class_id);
    JSValue *funcs, args[3];
    uint32_t i, count = 0;
    int result = 0;

    if (!sprite)
        return 0;
    if (event->type == ATHENA_SPRITE_EVENT_END && !JS_IsUndefined(sprite->settle) &&
        event->play_id == sprite->settle_play && event->clip == sprite->settle_clip &&
        sprite_settle(ctx, sprite, true))
        return -1;
    if (sprite->listener_count == 0)
        return 0;
    funcs = js_malloc(ctx, sprite->listener_count * sizeof(*funcs));
    if (!funcs)
        return -1;
    for (i = 0; i < sprite->listener_count;) {
        Listener *listener = &sprite->listeners[i];

        if (!listener_matches(listener, event)) {
            i++;
            continue;
        }
        funcs[count++] = JS_DupValue(ctx, listener->func);
        if (listener->once)
            sprite_remove_listener(ctx, sprite, i);
        else
            i++;
    }
    sprite_update_mask(sprite);
    if (event->clip >= 0 && (uint32_t)event->clip < sprite->data->sheet.clip_count)
        args[0] = JS_NewString(ctx, sprite->data->sheet.clips[event->clip].name);
    else
        args[0] = JS_NULL;
    args[1] = JS_NewInt32(ctx, (int32_t)event->position);
    args[2] = JS_NewInt32(ctx, (int32_t)event->frame);
    for (i = 0; i < count; i++) {
        if (!result) {
            JSValue ret = JS_Call(ctx, funcs[i], event->sprite, 3, args);

            if (JS_IsException(ret))
                result = -1;
            JS_FreeValue(ctx, ret);
        }
        JS_FreeValue(ctx, funcs[i]);
    }
    JS_FreeValue(ctx, args[0]);
    js_free(ctx, funcs);
    return result;
}

/*
 * Dispatches the collected events. The list is taken first: callbacks may
 * advance sprites themselves (Sprite.update(), instance.update()).
 */
static int sprite_flush_events(JSContext *ctx) {
    PendingEvent *events = state.pending;
    uint32_t count = state.pending_count, i;
    int result = 0;

    state.pending = NULL;
    state.pending_count = state.pending_capacity = 0;
    for (i = 0; i < count; i++) {
        /* After an exception the rest are dropped, as the Loop stops. */
        if (!result)
            result = sprite_dispatch_one(ctx, &events[i]);
        JS_FreeValue(ctx, events[i].sprite);
    }
    free(events);
    return result;
}

/* Guards nested advances: a listener advancing sprites whose listeners advance... */
static int sprite_enter(JSContext *ctx, const char *name) {
    if (state.depth >= SPRITE_MAX_DEPTH) {
        JS_ThrowRangeError(ctx, "%s: sprites advanced from their own listeners more "
            "than %d levels deep", name, SPRITE_MAX_DEPTH);
        return -1;
    }
    state.depth++;
    return 0;
}

static void animator_advance(JSContext *ctx, AnimatorObject *animator, float dt) {
    AthenaTileSprite *sprites;
    uint32_t count = 0;

    sprites = athena_tilemap_instance_sprites(ctx, animator->instance, &count);
    athena_sprite_animator_update(animator->entries, animator->count,
        &animator->data->sheet, animator->paused ? 0.0f : dt, sprites, count);
}

/*
 * Advances the playing instances and bound animators of one clock (scaled
 * or real time) in C, then dispatches the events. No JavaScript runs while
 * the lists are walked.
 */
static int sprite_advance_all(JSContext *ctx, float dt, bool real_time) {
    SpriteObject *sprite = state.playing;
    AnimatorObject *animator;
    int result;

    if (sprite_enter(ctx, "Sprite.update"))
        return -1;
    while (sprite) {
        SpriteObject *next = sprite->next;

        if (sprite->real_time == real_time) {
            sprite_advance(sprite, dt);
            sprite_sync_link(sprite);
        }
        sprite = next;
    }
    for (animator = state.animators; animator; animator = animator->next)
        if (animator->real_time == real_time)
            animator_advance(ctx, animator, dt);
    result = sprite_flush_events(ctx);
    state.depth--;
    return result;
}

static int sprite_system(void *opaque, AthenaLoopPhase phase, float value) {
    (void)opaque;
    if (!state.ctx)
        return 0;
    if (phase == ATHENA_LOOP_UPDATE)
        return sprite_advance_all(state.ctx, value, false) ? -1 : 0;
    if (phase == ATHENA_LOOP_PRE_UPDATE)
        return sprite_advance_all(state.ctx, value, true) ? -1 : 0;
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Sheet                                                                  */

static void sheet_finalizer(JSRuntime *rt, JSValue value) {
    SheetObject *sheet = JS_GetOpaque(value, sheet_class_id);

    if (!sheet)
        return;
    athena_sprite_sheet_clear(&sheet->sheet);
    JS_FreeValueRT(rt, sheet->image);
    js_free_rt(rt, sheet);
}

static void sheet_gc_mark(JSRuntime *rt, JSValueConst value, JS_MarkFunc *mark) {
    SheetObject *sheet = JS_GetOpaque(value, sheet_class_id);

    if (sheet)
        JS_MarkValue(rt, sheet->image, mark);
}

static JSClassDef sheet_class = {
    "Sheet",
    .finalizer = sheet_finalizer,
    .gc_mark = sheet_gc_mark,
};

static SheetObject *sheet_this(JSContext *ctx, JSValueConst value) {
    return JS_GetOpaque2(ctx, value, sheet_class_id);
}

/* Texture size of the sheet's image, or 0 x 0 while it is not loaded. */
static void sheet_texture_size(SheetObject *sheet, float *w, float *h) {
    AthenaImage *image = athena_image_peek(sheet->image);

    *w = *h = 0.0f;
    if (image && image->surface && image->loaded) {
        *w = (float)image->surface->Width;
        *h = (float)image->surface->Height;
    }
}

/* Creates an empty sheet drawing from `image` (Image, null or undefined). */
static JSValue sheet_create(JSContext *ctx, JSValueConst new_target,
    JSValueConst image, const char *name, SheetObject **out) {
    JSValue object;
    SheetObject *sheet;

    if (!JS_IsUndefined(image) && !JS_IsNull(image) && !athena_image_peek(image))
        return JS_ThrowTypeError(ctx, "%s image must be an Image or null", name);
    object = sprite_new_object(ctx, new_target, sheet_class_id);
    if (JS_IsException(object))
        return object;
    sheet = js_mallocz(ctx, sizeof(*sheet));
    if (!sheet) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    athena_sprite_sheet_init(&sheet->sheet);
    sheet->image = JS_IsUndefined(image) ? JS_NULL : JS_DupValue(ctx, image);
    JS_SetOpaque(object, sheet);
    *out = sheet;
    return object;
}

/* Frame of `sheet` by index or name, or a thrown error. */
static int sheet_frame_ref(JSContext *ctx, SheetObject *sheet, JSValueConst value,
    uint32_t *frame, const char *name) {
    if (JS_IsString(value)) {
        const char *text = JS_ToCString(ctx, value);
        int index;

        if (!text)
            return -1;
        index = athena_sprite_sheet_find_frame(&sheet->sheet, text);
        if (index < 0)
            JS_ThrowRangeError(ctx, "%s: no frame named '%s'", name, text);
        JS_FreeCString(ctx, text);
        if (index < 0)
            return -1;
        *frame = (uint32_t)index;
        return 0;
    }
    if (sheet->sheet.frame_count == 0) {
        JS_ThrowRangeError(ctx, "%s: the sheet has no frames", name);
        return -1;
    }
    return sprite_uint(ctx, value, frame, sheet->sheet.frame_count - 1, name);
}

/* Clip of `sheet` by name, or a thrown RangeError. */
static int sheet_clip_ref(JSContext *ctx, SheetObject *sheet, JSValueConst value,
    int32_t *clip, const char *name) {
    const char *text;
    int index;

    if (!JS_IsString(value)) {
        JS_ThrowTypeError(ctx, "%s clip must be a clip name", name);
        return -1;
    }
    text = JS_ToCString(ctx, value);
    if (!text)
        return -1;
    index = athena_sprite_sheet_find_clip(&sheet->sheet, text);
    if (index < 0)
        JS_ThrowRangeError(ctx, "%s: the sheet has no clip named '%s'", name, text);
    JS_FreeCString(ctx, text);
    if (index < 0)
        return -1;
    *clip = index;
    return 0;
}

/* Adds a frame described by { x, y, w, h, offsetX, offsetY, sourceWidth, sourceHeight, duration, name }. */
static int sheet_add_frame_value(JSContext *ctx, SheetObject *sheet,
    JSValueConst value, const char *name) {
    const float max = ATHENA_SPRITE_MAX_TEXELS;
    AthenaSpriteFrame frame;
    JSValue label = JS_UNDEFINED;
    const char *text = NULL;
    float ms = 0.0f;
    int result;

    if (!JS_IsObject(value)) {
        JS_ThrowTypeError(ctx, "%s must be an object { x, y, w, h }", name);
        return -1;
    }
    memset(&frame, 0, sizeof(frame));
    if (sprite_field(ctx, value, "x", &frame.x, 0.0f, max, name) ||
        sprite_field(ctx, value, "y", &frame.y, 0.0f, max, name) ||
        sprite_field(ctx, value, "w", &frame.w, 1e-3f, max, name) ||
        sprite_field(ctx, value, "h", &frame.h, 1e-3f, max, name) ||
        sprite_option_number(ctx, value, "offsetX", &frame.offset_x, 0.0f, max, name) < 0 ||
        sprite_option_number(ctx, value, "offsetY", &frame.offset_y, 0.0f, max, name) < 0 ||
        sprite_option_number(ctx, value, "sourceWidth", &frame.source_w, 0.0f, max, name) < 0 ||
        sprite_option_number(ctx, value, "sourceHeight", &frame.source_h, 0.0f, max, name) < 0 ||
        sprite_option_number(ctx, value, "duration", &ms, 0.0f,
            ATHENA_SPRITE_MAX_DURATION * 1000.0f, name) < 0)
        return -1;
    frame.duration = ms / 1000.0f;
    if (sprite_option(ctx, value, "name", &label) < 0)
        return -1;
    if (!JS_IsUndefined(label)) {
        if (!JS_IsString(label)) {
            JS_FreeValue(ctx, label);
            JS_ThrowTypeError(ctx, "%s.name must be a string", name);
            return -1;
        }
        text = JS_ToCString(ctx, label);
        JS_FreeValue(ctx, label);
        if (!text)
            return -1;
    }
    result = athena_sprite_sheet_add_frame(&sheet->sheet, &frame, text);
    if (result == ATHENA_SPRITE_EINVAL)
        JS_ThrowRangeError(ctx, "%s: the trim (offsetX/offsetY) must fit in the "
            "source size, and a name must be 1 to %d characters", name,
            ATHENA_SPRITE_NAME_MAX - 1);
    else if (result < 0)
        sprite_throw(ctx, result, name);
    JS_FreeCString(ctx, text);
    return result < 0 ? -1 : result;
}

/*
 * Frames of a grid row or column: { row, from, to } takes columns from..to
 * (default the whole row; to < from plays backwards), { column, from, to }
 * rows from..to. Only for sheets made by fromGrid().
 */
static int sheet_grid_frames(JSContext *ctx, SheetObject *sheet, JSValueConst value,
    uint16_t **out, uint32_t *count, const char *name) {
    uint32_t line = 0, from = 0, to, cells, i;
    bool by_row;
    JSValue probe;
    int has_row, has_column;

    has_row = sprite_option(ctx, value, "row", &probe);
    if (has_row > 0)
        JS_FreeValue(ctx, probe);
    has_column = has_row ? 0 : sprite_option(ctx, value, "column", &probe);
    if (has_column > 0)
        JS_FreeValue(ctx, probe);
    if (has_row < 0 || has_column < 0)
        return -1;
    if (!has_row && !has_column) {
        JS_ThrowTypeError(ctx, "%s must have frames, a row or a column", name);
        return -1;
    }
    if (!sheet->has_grid) {
        JS_ThrowTypeError(ctx, "%s: { row } and { column } need a sheet made by fromGrid()",
            name);
        return -1;
    }
    if (has_row ? sprite_option_uint(ctx, value, "row", &line, sheet->grid_rows - 1, name) < 0 :
        sprite_option_uint(ctx, value, "column", &line, sheet->grid_columns - 1, name) < 0)
        return -1;
    by_row = has_row > 0;
    cells = by_row ? sheet->grid_columns : sheet->grid_rows;
    to = cells - 1;
    if (sprite_option_uint(ctx, value, "from", &from, cells - 1, name) < 0 ||
        sprite_option_uint(ctx, value, "to", &to, cells - 1, name) < 0)
        return -1;
    *count = (from <= to ? to - from : from - to) + 1;
    *out = js_malloc(ctx, *count * sizeof(**out));
    if (!*out)
        return -1;
    for (i = 0; i < *count; i++) {
        uint32_t step = from <= to ? from + i : from - i;
        uint32_t cell = by_row ? line * sheet->grid_columns + step :
            step * sheet->grid_columns + line;

        /* Cells before `first` or past `count` are not frames of the sheet. */
        if (cell < sheet->grid_first || cell - sheet->grid_first >= sheet->grid_count) {
            js_free(ctx, *out);
            *out = NULL;
            JS_ThrowRangeError(ctx, "%s: cell %u of the grid is not a frame of the sheet",
                name, (unsigned)cell);
            return -1;
        }
        (*out)[i] = (uint16_t)(cell - sheet->grid_first);
    }
    return 0;
}

/* Frame list of a clip: "0-3,5", [0, 1, 2], ["a.png", "b.png"] or { row | column }. */
static int sheet_clip_frames(JSContext *ctx, SheetObject *sheet, JSValueConst value,
    uint16_t **out, uint32_t *count, const char *name) {
    if (JS_IsString(value)) {
        const char *text = JS_ToCString(ctx, value);
        int total;

        if (!text)
            return -1;
        total = athena_sprite_parse_frames(text, sheet->sheet.frame_count, NULL, 0);
        if (total > 0) {
            *out = js_malloc(ctx, (size_t)total * sizeof(**out));
            if (!*out) {
                JS_FreeCString(ctx, text);
                return -1;
            }
            athena_sprite_parse_frames(text, sheet->sheet.frame_count, *out, (uint32_t)total);
        }
        JS_FreeCString(ctx, text);
        if (total == ATHENA_SPRITE_ERANGE) {
            JS_ThrowRangeError(ctx, "%s: frame index past the sheet's %u frames", name,
                (unsigned)sheet->sheet.frame_count);
            return -1;
        }
        if (total <= 0) {
            JS_ThrowSyntaxError(ctx, "%s must list indices and ranges, such as \"0-3,5\"",
                name);
            return -1;
        }
        *count = (uint32_t)total;
        return 0;
    }
    if (JS_IsArray(ctx, value)) {
        JSValue length_value = JS_GetPropertyStr(ctx, value, "length");
        uint32_t length, i, frame;

        if (JS_IsException(length_value))
            return -1;
        if (JS_ToUint32(ctx, &length, length_value)) {
            JS_FreeValue(ctx, length_value);
            return -1;
        }
        JS_FreeValue(ctx, length_value);
        if (length == 0 || length > ATHENA_SPRITE_MAX_FRAMES) {
            JS_ThrowRangeError(ctx, "%s must hold 1 to %u frames", name,
                ATHENA_SPRITE_MAX_FRAMES);
            return -1;
        }
        *out = js_malloc(ctx, length * sizeof(**out));
        if (!*out)
            return -1;
        for (i = 0; i < length; i++) {
            JSValue item = JS_GetPropertyUint32(ctx, value, i);
            int failed = JS_IsException(item) ||
                sheet_frame_ref(ctx, sheet, item, &frame, name);

            JS_FreeValue(ctx, item);
            if (failed) {
                js_free(ctx, *out);
                *out = NULL;
                return -1;
            }
            (*out)[i] = (uint16_t)frame;
        }
        *count = length;
        return 0;
    }
    if (JS_IsObject(value))
        return sheet_grid_frames(ctx, sheet, value, out, count, name);
    JS_ThrowTypeError(ctx, "%s must be a range string, an array of frames or "
        "{ row | column, from, to }", name);
    return -1;
}

/*
 * Adds clip `clip_name` from a definition: frames only (a range string or
 * an array), or { frames, fps | durations (ms), mode, loops, reverse }.
 * Durations default to the frames' own, else to `default_fps`.
 */
static int sheet_add_clip_value(JSContext *ctx, SheetObject *sheet,
    const char *clip_name, JSValueConst def, float default_fps, const char *name) {
    uint16_t *frames = NULL;
    const char *next = NULL;
    float *durations = NULL, fps = 0.0f, one;
    uint32_t count = 0, duration_count = 0, loops = 0, i;
    bool reverse = false;
    AthenaSpriteMode mode = ATHENA_SPRITE_LOOP;
    JSValue value, frames_value;
    int found, result = -1;

    if (JS_IsString(def) || JS_IsArray(ctx, def)) {
        frames_value = JS_DupValue(ctx, def);
        def = JS_UNDEFINED;
    } else if (JS_IsObject(def)) {
        frames_value = JS_GetPropertyStr(ctx, def, "frames");
        if (JS_IsException(frames_value))
            return -1;
        /* { row: 2, fps: 8 }: the definition names its grid line itself. */
        if (JS_IsUndefined(frames_value))
            frames_value = JS_DupValue(ctx, def);
    } else {
        JS_ThrowTypeError(ctx, "%s must be a frame list or { frames, fps, mode }", name);
        return -1;
    }
    if (sheet_clip_frames(ctx, sheet, frames_value, &frames, &count, name)) {
        JS_FreeValue(ctx, frames_value);
        return -1;
    }
    JS_FreeValue(ctx, frames_value);

    /* Mode, loops, reverse. */
    found = sprite_option(ctx, def, "mode", &value);
    if (found < 0)
        goto done;
    if (found) {
        const char *text = JS_IsString(value) ? JS_ToCString(ctx, value) : NULL;

        JS_FreeValue(ctx, value);
        if (text && strcmp(text, "loop") == 0) {
            mode = ATHENA_SPRITE_LOOP;
        } else if (text && strcmp(text, "once") == 0) {
            mode = ATHENA_SPRITE_LOOP;
            loops = 1;
        } else if (text && strcmp(text, "pingpong") == 0) {
            mode = ATHENA_SPRITE_PINGPONG;
        } else {
            JS_FreeCString(ctx, text);
            JS_ThrowTypeError(ctx, "%s.mode must be \"loop\", \"once\" or \"pingpong\"", name);
            goto done;
        }
        JS_FreeCString(ctx, text);
    }
    found = sprite_option_uint(ctx, def, "loops", &loops, UINT32_MAX / 2, name);
    if (found < 0)
        goto done;
    if (sprite_option_bool(ctx, def, "reverse", &reverse) < 0)
        goto done;

    /* Durations. */
    found = sprite_option(ctx, def, "durations", &value);
    if (found < 0)
        goto done;
    if (found) {
        uint32_t length = 0;
        int failed = !JS_IsArray(ctx, value) ? 1 :
            sprite_length(ctx, value, &length) ? -1 : length != count;

        if (failed) {
            JS_FreeValue(ctx, value);
            if (failed > 0)
                JS_ThrowRangeError(ctx, "%s.durations must be an array with one "
                    "duration (ms) per frame (%u)", name, (unsigned)count);
            goto done;
        }
        durations = js_malloc(ctx, count * sizeof(*durations));
        if (!durations) {
            JS_FreeValue(ctx, value);
            goto done;
        }
        for (i = 0; i < count; i++) {
            JSValue item = JS_GetPropertyUint32(ctx, value, i);
            float ms;
            int failed = JS_IsException(item) || sprite_number(ctx, item, &ms,
                ATHENA_SPRITE_MIN_DURATION * 1000.0f,
                ATHENA_SPRITE_MAX_DURATION * 1000.0f, name);

            JS_FreeValue(ctx, item);
            if (failed) {
                JS_FreeValue(ctx, value);
                goto done;
            }
            durations[i] = ms / 1000.0f;
        }
        duration_count = count;
        JS_FreeValue(ctx, value);
    } else {
        if (sprite_option_number(ctx, def, "fps", &fps, 1e-3f, SPRITE_MAX_FPS, name) < 0)
            goto done;
        if (fps == 0.0f) {
            /* The frames' own durations (sheet files), when they all have one. */
            for (i = 0; i < count; i++)
                if (!(sheet->sheet.frames[frames[i]].duration > 0.0f))
                    break;
            if (i < count)
                fps = default_fps;
        }
        if (fps > 0.0f) {
            one = 1.0f / fps;
            durations = &one;
            duration_count = 1;
        }
    }

    if (reverse) {
        for (i = 0; i < count / 2; i++) {
            uint16_t f = frames[i];

            frames[i] = frames[count - 1 - i];
            frames[count - 1 - i] = f;
            if (duration_count == count) {
                float d = durations[i];

                durations[i] = durations[count - 1 - i];
                durations[count - 1 - i] = d;
            }
        }
    }
    /* The next clip is read before adding, so a bad one adds nothing. */
    found = sprite_option(ctx, def, "next", &value);
    if (found < 0)
        goto done;
    if (found) {
        if (!JS_IsString(value) && !JS_IsNull(value)) {
            JS_FreeValue(ctx, value);
            JS_ThrowTypeError(ctx, "%s.next must be a clip name", name);
            goto done;
        }
        bool is_string = JS_IsString(value);

        next = is_string ? JS_ToCString(ctx, value) : NULL;
        JS_FreeValue(ctx, value);
        if (is_string && !next)
            goto done;
        if (next && (!next[0] || strlen(next) >= ATHENA_SPRITE_NAME_MAX)) {
            JS_ThrowTypeError(ctx, "%s.next: clip names must be 1 to %d characters", name,
                ATHENA_SPRITE_NAME_MAX - 1);
            goto done;
        }
    }
    result = athena_sprite_sheet_add_clip(&sheet->sheet, clip_name, frames, count,
        durations, duration_count, mode, loops);
    if (result == ATHENA_SPRITE_EINVAL)
        JS_ThrowTypeError(ctx, "%s: clip names must be 1 to %d characters", name,
            ATHENA_SPRITE_NAME_MAX - 1);
    else if (result < 0)
        sprite_throw(ctx, result, name);
    else
        athena_sprite_sheet_set_next(&sheet->sheet, result, next);
    result = result < 0 ? -1 : 0;
done:
    JS_FreeCString(ctx, next);
    js_free(ctx, frames);
    if (durations != &one)
        js_free(ctx, durations);
    return result;
}

/* Adds every clip of { name: definition }. */
static int sheet_add_clips(JSContext *ctx, SheetObject *sheet, JSValueConst clips,
    float default_fps, const char *name) {
    JSPropertyEnum *props = NULL;
    uint32_t count = 0, i;
    int result = 0;

    if (!JS_IsObject(clips) || JS_IsArray(ctx, clips)) {
        JS_ThrowTypeError(ctx, "%s.clips must be an object { name: clip }", name);
        return -1;
    }
    if (JS_GetOwnPropertyNames(ctx, &props, &count, clips,
            JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY))
        return -1;
    for (i = 0; i < count; i++) {
        const char *clip_name;
        JSValue def;
        char label[ATHENA_SPRITE_NAME_MAX + 64];

        if (result)
            continue;
        clip_name = JS_AtomToCString(ctx, props[i].atom);
        def = JS_GetProperty(ctx, clips, props[i].atom);
        if (!clip_name || JS_IsException(def)) {
            result = -1;
        } else {
            snprintf(label, sizeof(label), "%s.clips.%.*s", name,
                ATHENA_SPRITE_NAME_MAX, clip_name);
            result = sheet_add_clip_value(ctx, sheet, clip_name, def, default_fps, label);
        }
        JS_FreeCString(ctx, clip_name);
        JS_FreeValue(ctx, def);
    }
    for (i = 0; i < count; i++)
        JS_FreeAtom(ctx, props[i].atom);
    js_free(ctx, props);
    return result;
}

/* The default fps of the sheet's clips: options.fps, else 12. */
static int sheet_default_fps(JSContext *ctx, JSValueConst options, float *fps,
    const char *name) {
    *fps = SPRITE_DEFAULT_FPS;
    return sprite_option_number(ctx, options, "fps", fps, 1e-3f, SPRITE_MAX_FPS, name) < 0 ?
        -1 : 0;
}

/* Largest inset: half a texel is what filtering needs; more crops the frames. */
#define SPRITE_MAX_INSET 16.0f

/* Options every way of making a sheet takes: inset, clips (at fps). */
static int sheet_apply_options(JSContext *ctx, SheetObject *sheet, JSValueConst options,
    const char *name) {
    JSValue clips;
    float fps;
    int found, result;

    if (sheet_default_fps(ctx, options, &fps, name) ||
        sprite_option_number(ctx, options, "inset", &sheet->sheet.inset, 0.0f,
            SPRITE_MAX_INSET, name) < 0)
        return -1;
    found = sprite_option(ctx, options, "clips", &clips);
    if (found <= 0)
        return found;
    result = sheet_add_clips(ctx, sheet, clips, fps, name);
    JS_FreeValue(ctx, clips);
    return result;
}

/* new Sprite.Sheet(image, { frames: [...], clips, fps }) */
static JSValue sheet_ctor(JSContext *ctx, JSValueConst new_target, int argc,
    JSValueConst *argv) {
    const char *name = "Sprite.Sheet";
    JSValueConst options = sprite_arg(argc, argv, 1);
    SheetObject *sheet;
    JSValue object, frames;
    int found;

    if (sprite_check_options(ctx, options, name))
        return JS_EXCEPTION;
    object = sheet_create(ctx, new_target, sprite_arg(argc, argv, 0), name, &sheet);
    if (JS_IsException(object))
        return object;
    found = sprite_option(ctx, options, "frames", &frames);
    if (found < 0)
        goto fail;
    if (found) {
        uint32_t length = 0, i;

        if (!JS_IsArray(ctx, frames)) {
            JS_FreeValue(ctx, frames);
            JS_ThrowTypeError(ctx, "%s options.frames must be an array", name);
            goto fail;
        }
        if (sprite_length(ctx, frames, &length)) {
            JS_FreeValue(ctx, frames);
            goto fail;
        }
        for (i = 0; i < length; i++) {
            JSValue item = JS_GetPropertyUint32(ctx, frames, i);
            int failed = JS_IsException(item) ||
                sheet_add_frame_value(ctx, sheet, item, "Sprite.Sheet frame") < 0;

            JS_FreeValue(ctx, item);
            if (failed) {
                JS_FreeValue(ctx, frames);
                goto fail;
            }
        }
        JS_FreeValue(ctx, frames);
    }
    if (sheet_apply_options(ctx, sheet, options, name))
        goto fail;
    return object;
fail:
    JS_FreeValue(ctx, object);
    return JS_EXCEPTION;
}

/*
 * The frames of a grid on `image` from { frameWidth, frameHeight, margin,
 * spacing, columns, rows, first, count, textureWidth, textureHeight, clips,
 * fps, inset }. The grid is kept, for clips given by row or column.
 */
static JSValue sheet_grid_build(JSContext *ctx, JSValueConst image, JSValueConst options,
    const char *name) {
    const float max = ATHENA_SPRITE_MAX_TEXELS;
    AthenaSpriteGrid grid;
    SheetObject *sheet;
    JSValue object;
    int result;

    if (!JS_IsObject(options) || JS_IsArray(ctx, options))
        return JS_ThrowTypeError(ctx, "%s options must be an object "
            "{ frameWidth, frameHeight }", name);
    object = sheet_create(ctx, JS_UNDEFINED, image, name, &sheet);
    if (JS_IsException(object))
        return object;
    memset(&grid, 0, sizeof(grid));
    sheet_texture_size(sheet, &grid.texture_w, &grid.texture_h);
    if (sprite_field(ctx, options, "frameWidth", &grid.frame_w, 1.0f, max, name) ||
        sprite_field(ctx, options, "frameHeight", &grid.frame_h, 1.0f, max, name) ||
        sprite_option_number(ctx, options, "margin", &grid.margin, 0.0f, max, name) < 0 ||
        sprite_option_number(ctx, options, "spacing", &grid.spacing, 0.0f, max, name) < 0 ||
        sprite_option_number(ctx, options, "textureWidth", &grid.texture_w, 0.0f, max, name) < 0 ||
        sprite_option_number(ctx, options, "textureHeight", &grid.texture_h, 0.0f, max, name) < 0 ||
        sprite_option_uint(ctx, options, "columns", &grid.columns, ATHENA_SPRITE_MAX_FRAMES, name) < 0 ||
        sprite_option_uint(ctx, options, "rows", &grid.rows, ATHENA_SPRITE_MAX_FRAMES, name) < 0 ||
        sprite_option_uint(ctx, options, "first", &grid.first, ATHENA_SPRITE_MAX_FRAMES, name) < 0 ||
        sprite_option_uint(ctx, options, "count", &grid.count, ATHENA_SPRITE_MAX_FRAMES, name) < 0)
        goto fail;
    result = athena_sprite_sheet_add_grid(&sheet->sheet, &grid);
    if (result == ATHENA_SPRITE_EINVAL) {
        JS_ThrowRangeError(ctx, "%s: no whole frame fits; give a loaded image, "
            "textureWidth/textureHeight or columns/rows (or use fromGridAsync)", name);
        goto fail;
    }
    if (result < 0) {
        sprite_throw(ctx, result, name);
        goto fail;
    }
    sheet->has_grid = true;
    sheet->grid_columns = grid.columns;
    sheet->grid_rows = grid.rows;
    sheet->grid_first = grid.first;
    sheet->grid_count = (uint32_t)result;
    if (sheet_apply_options(ctx, sheet, options, name))
        goto fail;
    return object;
fail:
    JS_FreeValue(ctx, object);
    return JS_EXCEPTION;
}

/*
 * Sprite.Sheet.fromGrid(image, { frameWidth, frameHeight, margin, spacing,
 * columns, rows, first, count, textureWidth, textureHeight, clips, fps, inset })
 */
static JSValue sheet_from_grid(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    return sheet_grid_build(ctx, sprite_arg(argc, argv, 0), sprite_arg(argc, argv, 1),
        "Sprite.Sheet.fromGrid");
}

/* ---- Sheet files (Aseprite, TexturePacker) ---------------------------- */

static char *sprite_read_file(JSContext *ctx, const char *path, size_t *length,
    const char *name) {
    FILE *file = fopen(path, "rb");
    long size;
    char *data;

    if (!file) {
        JS_ThrowReferenceError(ctx, "%s: cannot open '%s'", name, path);
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
        (unsigned long)size > SPRITE_MAX_JSON_BYTES || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        JS_ThrowRangeError(ctx, "%s: '%s' cannot be read or is larger than %u bytes",
            name, path, SPRITE_MAX_JSON_BYTES);
        return NULL;
    }
    data = js_malloc(ctx, (size_t)size + 1);
    if (!data) {
        fclose(file);
        return NULL;
    }
    if (fread(data, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        js_free(ctx, data);
        JS_ThrowReferenceError(ctx, "%s: cannot read '%s'", name, path);
        return NULL;
    }
    fclose(file);
    data[size] = '\0';
    *length = (size_t)size;
    return data;
}

/* A { k1: number, k2: number } sub-object. */
static int json_rect(JSContext *ctx, JSValueConst object, const char *key,
    const char *k1, const char *k2, float *a, float *b, bool required, const char *name) {
    JSValue value = JS_GetPropertyStr(ctx, object, key);
    char label[96];
    int result;

    if (JS_IsException(value))
        return -1;
    if (JS_IsUndefined(value) && !required)
        return 0;
    snprintf(label, sizeof(label), "%s.%s", name, key);
    if (!JS_IsObject(value)) {
        JS_FreeValue(ctx, value);
        JS_ThrowTypeError(ctx, "%s must be an object", label);
        return -1;
    }
    result = sprite_field(ctx, value, k1, a, 0.0f, ATHENA_SPRITE_MAX_TEXELS, label) ||
        sprite_field(ctx, value, k2, b, 0.0f, ATHENA_SPRITE_MAX_TEXELS, label) ? -1 : 0;
    JS_FreeValue(ctx, value);
    return result;
}

/* One frame entry of a packer file: { frame, rotated, spriteSourceSize, sourceSize, duration }. */
static int json_frame(JSContext *ctx, SheetObject *sheet, JSValueConst entry,
    const char *frame_name, const char *name) {
    AthenaSpriteFrame frame;
    float ms = 0.0f;
    bool rotated = false;
    int result;

    if (!JS_IsObject(entry)) {
        JS_ThrowTypeError(ctx, "%s: frame '%s' must be an object", name,
            frame_name ? frame_name : "?");
        return -1;
    }
    memset(&frame, 0, sizeof(frame));
    /* Turned 90 degrees clockwise in the atlas (TexturePacker): drawn turned back. */
    if (sprite_option_bool(ctx, entry, "rotated", &rotated) < 0)
        return -1;
    frame.rotated = rotated;
    if (json_rect(ctx, entry, "frame", "x", "y", &frame.x, &frame.y, true, name) ||
        json_rect(ctx, entry, "frame", "w", "h", &frame.w, &frame.h, true, name) ||
        json_rect(ctx, entry, "spriteSourceSize", "x", "y", &frame.offset_x,
            &frame.offset_y, false, name) ||
        json_rect(ctx, entry, "sourceSize", "w", "h", &frame.source_w, &frame.source_h,
            false, name) ||
        sprite_option_number(ctx, entry, "duration", &ms, 0.0f,
            ATHENA_SPRITE_MAX_DURATION * 1000.0f, name) < 0)
        return -1;
    frame.duration = ms / 1000.0f;
    result = athena_sprite_sheet_add_frame(&sheet->sheet, &frame, frame_name);
    if (result == ATHENA_SPRITE_EINVAL) {
        JS_ThrowRangeError(ctx, "%s: frame '%s' has an empty rectangle, a trim past "
            "its source size or a name longer than %d characters", name,
            frame_name ? frame_name : "?", ATHENA_SPRITE_NAME_MAX - 1);
        return -1;
    }
    if (result == ATHENA_SPRITE_EEXIST) {
        JS_ThrowTypeError(ctx, "%s: frame name '%s' appears twice", name, frame_name);
        return -1;
    }
    if (result < 0) {
        sprite_throw(ctx, result, name);
        return -1;
    }
    return 0;
}

static int json_frames(JSContext *ctx, SheetObject *sheet, JSValueConst data,
    const char *name) {
    JSValue frames = JS_GetPropertyStr(ctx, data, "frames");
    int result = 0;

    if (JS_IsException(frames))
        return -1;
    if (JS_IsArray(ctx, frames)) {
        /* JSON Array: [{ filename, frame, ... }] */
        JSValue length_value = JS_GetPropertyStr(ctx, frames, "length");
        uint32_t length = 0, i;

        if (JS_ToUint32(ctx, &length, length_value))
            result = -1;
        JS_FreeValue(ctx, length_value);
        for (i = 0; !result && i < length; i++) {
            JSValue entry = JS_GetPropertyUint32(ctx, frames, i);
            JSValue filename = JS_IsObject(entry) ?
                JS_GetPropertyStr(ctx, entry, "filename") : JS_UNDEFINED;
            const char *frame_name = JS_IsString(filename) ? JS_ToCString(ctx, filename) : NULL;

            result = JS_IsException(entry) || JS_IsException(filename) ||
                json_frame(ctx, sheet, entry, frame_name, name) ? -1 : 0;
            JS_FreeCString(ctx, frame_name);
            JS_FreeValue(ctx, filename);
            JS_FreeValue(ctx, entry);
        }
    } else if (JS_IsObject(frames)) {
        /* JSON Hash: { "name": { frame, ... } }, in file order. */
        JSPropertyEnum *props = NULL;
        uint32_t count = 0, i;

        if (JS_GetOwnPropertyNames(ctx, &props, &count, frames,
                JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) {
            result = -1;
        } else {
            for (i = 0; i < count; i++) {
                if (!result) {
                    const char *frame_name = JS_AtomToCString(ctx, props[i].atom);
                    JSValue entry = JS_GetProperty(ctx, frames, props[i].atom);

                    result = !frame_name || JS_IsException(entry) ||
                        json_frame(ctx, sheet, entry, frame_name, name) ? -1 : 0;
                    JS_FreeCString(ctx, frame_name);
                    JS_FreeValue(ctx, entry);
                }
                JS_FreeAtom(ctx, props[i].atom);
            }
            js_free(ctx, props);
        }
    } else {
        JS_ThrowTypeError(ctx, "%s: the file has no \"frames\" (Aseprite or "
            "TexturePacker JSON Hash/Array)", name);
        result = -1;
    }
    JS_FreeValue(ctx, frames);
    if (!result && sheet->sheet.frame_count == 0) {
        JS_ThrowRangeError(ctx, "%s: the file has no frames", name);
        result = -1;
    }
    return result;
}

/* Aseprite tags: meta.frameTags [{ name, from, to, direction, repeat }]. */
static int json_tags(JSContext *ctx, SheetObject *sheet, JSValueConst meta,
    float default_fps, const char *name) {
    JSValue tags = JS_IsObject(meta) ? JS_GetPropertyStr(ctx, meta, "frameTags") : JS_UNDEFINED;
    JSValue length_value;
    uint32_t length = 0, i;
    int result = 0;

    if (JS_IsException(tags))
        return -1;
    if (!JS_IsArray(ctx, tags)) {
        JS_FreeValue(ctx, tags);
        return 0;
    }
    length_value = JS_GetPropertyStr(ctx, tags, "length");
    if (JS_ToUint32(ctx, &length, length_value))
        result = -1;
    JS_FreeValue(ctx, length_value);
    for (i = 0; !result && i < length; i++) {
        JSValue tag = JS_GetPropertyUint32(ctx, tags, i);
        JSValue tag_name = JS_UNDEFINED, direction = JS_UNDEFINED, repeat = JS_UNDEFINED;
        const char *text = NULL, *dir = NULL;
        uint32_t from = 0, to = 0, loops = 0, count, k;
        uint16_t *frames = NULL;
        float one = 1.0f / default_fps;
        bool reverse = false, own = true;
        AthenaSpriteMode mode = ATHENA_SPRITE_LOOP;
        int added;

        if (JS_IsException(tag) || !JS_IsObject(tag)) {
            if (!JS_IsException(tag))
                JS_ThrowTypeError(ctx, "%s: meta.frameTags entries must be objects", name);
            result = -1;
            goto next;
        }
        tag_name = JS_GetPropertyStr(ctx, tag, "name");
        direction = JS_GetPropertyStr(ctx, tag, "direction");
        repeat = JS_GetPropertyStr(ctx, tag, "repeat");
        if (!JS_IsString(tag_name)) {
            JS_ThrowTypeError(ctx, "%s: a frame tag has no name", name);
            result = -1;
            goto next;
        }
        text = JS_ToCString(ctx, tag_name);
        dir = JS_IsString(direction) ? JS_ToCString(ctx, direction) : NULL;
        if (!text) {
            result = -1;
            goto next;
        }
        {
            JSValue v_from = JS_GetPropertyStr(ctx, tag, "from");
            JSValue v_to = JS_GetPropertyStr(ctx, tag, "to");

            result = sprite_uint(ctx, v_from, &from, sheet->sheet.frame_count - 1, name) ||
                sprite_uint(ctx, v_to, &to, sheet->sheet.frame_count - 1, name) ? -1 : 0;
            JS_FreeValue(ctx, v_from);
            JS_FreeValue(ctx, v_to);
            if (result)
                goto next;
        }
        /* Aseprite writes repeat as a string ("3"); 0 or none plays forever. */
        if (JS_IsString(repeat) || JS_IsNumber(repeat)) {
            double number;

            if (JS_ToFloat64(ctx, &number, repeat)) {
                result = -1;
                goto next;
            }
            if (number >= 1.0 && number <= 1.0e9)
                loops = (uint32_t)number;
        }
        if (dir && strcmp(dir, "reverse") == 0) {
            reverse = true;
        } else if (dir && strcmp(dir, "pingpong") == 0) {
            mode = ATHENA_SPRITE_PINGPONG;
        } else if (dir && strcmp(dir, "pingpong_reverse") == 0) {
            mode = ATHENA_SPRITE_PINGPONG;
            reverse = true;
        }
        if (to < from) {
            uint32_t swap = from;

            from = to;
            to = swap;
        }
        count = to - from + 1;
        frames = js_malloc(ctx, count * sizeof(*frames));
        if (!frames) {
            result = -1;
            goto next;
        }
        for (k = 0; k < count; k++) {
            frames[k] = (uint16_t)(reverse ? to - k : from + k);
            if (!(sheet->sheet.frames[frames[k]].duration > 0.0f))
                own = false;
        }
        added = athena_sprite_sheet_add_clip(&sheet->sheet, text, frames, count,
            own ? NULL : &one, own ? 0 : 1, mode, loops);
        js_free(ctx, frames);
        if (added < 0) {
            if (added == ATHENA_SPRITE_EINVAL)
                JS_ThrowTypeError(ctx, "%s: tag names must be 1 to %d characters", name,
                    ATHENA_SPRITE_NAME_MAX - 1);
            else
                sprite_throw(ctx, added, name);
            result = -1;
        }
    next:
        JS_FreeCString(ctx, text);
        JS_FreeCString(ctx, dir);
        JS_FreeValue(ctx, tag_name);
        JS_FreeValue(ctx, direction);
        JS_FreeValue(ctx, repeat);
        JS_FreeValue(ctx, tag);
    }
    JS_FreeValue(ctx, tags);
    return result;
}

/* Pixi/TexturePacker animations: { name: ["frame.png", ...] }. */
static int json_animations(JSContext *ctx, SheetObject *sheet, JSValueConst data,
    float default_fps, const char *name) {
    JSValue animations = JS_GetPropertyStr(ctx, data, "animations");
    int result = 0;

    if (JS_IsException(animations))
        return -1;
    if (JS_IsObject(animations) && !JS_IsArray(ctx, animations))
        result = sheet_add_clips(ctx, sheet, animations, default_fps, name);
    JS_FreeValue(ctx, animations);
    return result;
}

/*
 * meta.image relative to the file's directory (malloc'd), or NULL with an
 * exception pending.
 */
static char *json_image_path(JSContext *ctx, JSValueConst meta, const char *path,
    const char *name) {
    JSValue image_name = JS_IsObject(meta) ? JS_GetPropertyStr(ctx, meta, "image") :
        JS_UNDEFINED;
    const char *file;
    char *full;

    if (JS_IsException(image_name))
        return NULL;
    if (!JS_IsString(image_name)) {
        JS_FreeValue(ctx, image_name);
        JS_ThrowTypeError(ctx, "%s: the file has no meta.image; pass options.image", name);
        return NULL;
    }
    file = JS_ToCString(ctx, image_name);
    JS_FreeValue(ctx, image_name);
    if (!file)
        return NULL;
    full = athena_sprite_path_join(path, file);
    JS_FreeCString(ctx, file);
    if (!full)
        JS_ThrowOutOfMemory(ctx);
    return full;
}

/* new Image(path), through the global the Image module installs. */
static JSValue sprite_new_image(JSContext *ctx, const char *path, const char *name) {
    JSValue global, ctor, arg, image;

    arg = JS_NewString(ctx, path);
    if (JS_IsException(arg))
        return arg;
    global = JS_GetGlobalObject(ctx);
    ctor = JS_GetPropertyStr(ctx, global, "Image");
    JS_FreeValue(ctx, global);
    if (!JS_IsFunction(ctx, ctor)) {
        JS_FreeValue(ctx, arg);
        if (JS_IsException(ctor))
            return ctor;
        JS_FreeValue(ctx, ctor);
        return JS_ThrowReferenceError(ctx, "%s: the Image module is not available; "
            "pass options.image", name);
    }
    image = JS_CallConstructor(ctx, ctor, 1, (JSValueConst *)&arg);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, arg);
    return image;
}

/* meta.image, relative to the file's directory, loaded now. */
static JSValue json_image(JSContext *ctx, JSValueConst meta, const char *path,
    const char *name) {
    char *full = json_image_path(ctx, meta, path, name);
    JSValue image;

    if (!full)
        return JS_EXCEPTION;
    image = sprite_new_image(ctx, full, name);
    free(full);
    return image;
}

static int json_slices(JSContext *ctx, SheetObject *sheet, JSValueConst meta,
    const char *name);

/*
 * A sheet from parsed Aseprite or TexturePacker data. The texture is
 * options.image when given, else `decoded` (an Image made by a loading job,
 * owned; used when it is the file's meta.image, as found from `path`), else
 * meta.image loaded now.
 */
static JSValue sheet_json_build(JSContext *ctx, JSValueConst data, const char *path,
    JSValueConst options, JSValue decoded, const char *decoded_path, const char *name) {
    JSValue meta = JS_UNDEFINED, image = JS_UNDEFINED, object = JS_EXCEPTION;
    SheetObject *sheet;
    float fps;
    int found;

    if (!JS_IsObject(data) || JS_IsArray(ctx, data)) {
        JS_ThrowTypeError(ctx, "%s: the sheet data must be an object", name);
        goto done;
    }
    if (sheet_default_fps(ctx, options, &fps, name))
        goto done;
    found = sprite_option(ctx, options, "image", &image);
    if (found < 0)
        goto done;
    meta = JS_GetPropertyStr(ctx, data, "meta");
    if (JS_IsException(meta))
        goto done;
    if (!found) {
        char *wanted = NULL;

        if (!JS_IsUndefined(decoded)) {
            wanted = json_image_path(ctx, meta, path, name);
            if (!wanted)
                goto done;
        }
        if (wanted && decoded_path && strcmp(wanted, decoded_path) == 0) {
            image = decoded;
            decoded = JS_UNDEFINED;
        } else {
            /* The scan on the worker found another name: load what the parser found. */
            image = wanted ? sprite_new_image(ctx, wanted, name) :
                json_image(ctx, meta, path, name);
        }
        free(wanted);
        if (JS_IsException(image))
            goto done;
    }
    object = sheet_create(ctx, JS_UNDEFINED, image, name, &sheet);
    if (JS_IsException(object))
        goto done;
    if (json_frames(ctx, sheet, data, name) || json_tags(ctx, sheet, meta, fps, name) ||
        json_slices(ctx, sheet, meta, name) ||
        json_animations(ctx, sheet, data, fps, name) ||
        sheet_apply_options(ctx, sheet, options, name)) {
        JS_FreeValue(ctx, object);
        object = JS_EXCEPTION;
    }
done:
    JS_FreeValue(ctx, meta);
    JS_FreeValue(ctx, image);
    JS_FreeValue(ctx, decoded);
    return object;
}

/*
 * Sprite.Sheet.fromJSON(pathOrData, { image, clips, fps, inset }): Aseprite
 * and TexturePacker files (JSON Hash or Array), with Aseprite tags and
 * TexturePacker/Pixi "animations" as clips.
 */
static JSValue sheet_from_json(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    const char *name = "Sprite.Sheet.fromJSON";
    JSValueConst source = sprite_arg(argc, argv, 0), options = sprite_arg(argc, argv, 1);
    const char *path = NULL;
    JSValue data, object;

    if (sprite_check_options(ctx, options, name))
        return JS_EXCEPTION;
    if (JS_IsString(source)) {
        size_t length;
        char *text;

        path = JS_ToCString(ctx, source);
        if (!path)
            return JS_EXCEPTION;
        text = sprite_read_file(ctx, path, &length, name);
        if (!text) {
            JS_FreeCString(ctx, path);
            return JS_EXCEPTION;
        }
        data = JS_ParseJSON(ctx, text, length, path);
        js_free(ctx, text);
    } else if (JS_IsObject(source) && !JS_IsArray(ctx, source)) {
        data = JS_DupValue(ctx, source);
    } else {
        return JS_ThrowTypeError(ctx, "%s expects a file path or the parsed data", name);
    }
    object = JS_IsException(data) ? JS_EXCEPTION :
        sheet_json_build(ctx, data, path, options, JS_UNDEFINED, NULL, name);
    JS_FreeCString(ctx, path);
    JS_FreeValue(ctx, data);
    return object;
}

/* ---- Loading in the background (Job) ------------------------------------ */

/* What a loading Job object keeps next to the native job. */
typedef struct {
    bool json;
    JSValue options;
    const char *name;
} SheetLoadUser;

static const char *sheet_load_message(int result) {
    switch (result) {
    case ATHENA_SPRITE_LOAD_OPEN:
        return "cannot open the sheet file";
    case ATHENA_SPRITE_LOAD_TOO_LARGE:
        return "the sheet file is larger than 2 MB";
    case ATHENA_SPRITE_LOAD_READ:
        return "cannot read the sheet file";
    case ATHENA_SPRITE_LOAD_IMAGE:
        return "cannot load the texture";
    case ATHENA_SPRITE_LOAD_NOMEM:
        return "out of memory";
    default:
        return "cancelled";
    }
}

/* The Image of a decoded buffer, which it takes; undefined when none. */
static JSValue sheet_load_image(JSContext *ctx, AthenaSpriteLoad *load, const char *name) {
    AthenaImage *image;
    JSValue value;

    if (!load->has_image)
        return JS_UNDEFINED;
    load->has_image = false;
    image = athena_image_create_empty(true);
    if (!image) {
        athena_image_buffer_release(&load->image);
        return JS_ThrowOutOfMemory(ctx);
    }
    if (athena_image_apply_buffer(image, load->image_path, &load->image) < 0) {
        athena_image_destroy(image);
        return JS_ThrowInternalError(ctx, "%s: cannot create the texture of '%s'", name,
            load->image_path);
    }
    value = athena_image_to_value(ctx, image);
    if (JS_IsException(value))
        athena_image_destroy(image);
    return value;
}

/* Builds the sheet on the script thread, once the worker read and decoded. */
static int sheet_load_settle(JSContext *ctx, AthenaJob *job, AthenaJobState state,
    int result, void *user, JSValue *outcome, bool *failed) {
    SheetLoadUser *load_user = user;
    AthenaSpriteLoad *load = athena_job_data(job);
    JSValue image, data;

    if (state != ATHENA_JOB_DONE) {
        const char *what = state == ATHENA_JOB_CANCELLED ? "cancelled" :
            sheet_load_message(result);
        const char *path = result == ATHENA_SPRITE_LOAD_IMAGE ? load->image_path :
            load->json_path;
        char message[256];

        snprintf(message, sizeof(message), "%s: %s%s%s%s", load_user->name, what,
            path ? " '" : "", path ? path : "", path ? "'" : "");
        *failed = true;
        *outcome = JS_NewError(ctx);
        if (JS_IsException(*outcome))
            return -1;
        JS_DefinePropertyValueStr(ctx, *outcome, "message", JS_NewString(ctx, message),
            JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
        return 0;
    }
    image = sheet_load_image(ctx, load, load_user->name);
    if (JS_IsException(image))
        return -1;
    if (!load_user->json) {
        *outcome = sheet_grid_build(ctx, image, load_user->options, load_user->name);
        JS_FreeValue(ctx, image);
        return JS_IsException(*outcome) ? -1 : 0;
    }
    data = JS_ParseJSON(ctx, load->text, load->length, load->json_path);
    free(load->text);
    load->text = NULL;
    if (JS_IsException(data)) {
        JS_FreeValue(ctx, image);
        return -1;
    }
    *outcome = sheet_json_build(ctx, data, load->json_path, load_user->options, image,
        load->image_path, load_user->name);
    JS_FreeValue(ctx, data);
    return JS_IsException(*outcome) ? -1 : 0;
}

static void sheet_load_free_user(JSRuntime *rt, void *user) {
    SheetLoadUser *load_user = user;

    JS_FreeValueRT(rt, load_user->options);
    free(load_user);
}

/* The options held in `user` must be visible to the cycle collector. */
static void sheet_load_mark_user(JSRuntime *rt, void *user, JS_MarkFunc *mark) {
    SheetLoadUser *load_user = user;

    JS_MarkValue(rt, load_user->options, mark);
}

static const AthenaJsJobKind sheet_load_kind = {
    .owner = "Sprite",
    .settle = sheet_load_settle,
    .free_user = sheet_load_free_user,
    .mark_user = sheet_load_mark_user,
};

/*
 * Sprite.Sheet.fromJSONAsync(path, options) and fromGridAsync(imagePath,
 * options): a Job (awaitable) that reads the file and decodes the texture
 * on a worker, then builds the Sheet on the script thread.
 */
static JSValue sheet_load_async(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int json) {
    const char *name = json ? "Sprite.Sheet.fromJSONAsync" : "Sprite.Sheet.fromGridAsync";
    JSValueConst options = sprite_arg(argc, argv, 1);
    SheetLoadUser *user;
    const char *path;
    bool has_image = false;
    JSValue image;
    AthenaJob *job;
    int found;

    if (!JS_IsString(sprite_arg(argc, argv, 0)))
        return JS_ThrowTypeError(ctx, "%s expects a file path", name);
    if (json ? sprite_check_options(ctx, options, name) :
        (!JS_IsObject(options) || JS_IsArray(ctx, options))) {
        if (!json)
            JS_ThrowTypeError(ctx, "%s options must be an object { frameWidth, frameHeight }",
                name);
        return JS_EXCEPTION;
    }
    /* options.image given: only the file is read in the background. */
    found = json ? sprite_option(ctx, options, "image", &image) : 0;
    if (found < 0)
        return JS_EXCEPTION;
    if (found) {
        has_image = true;
        JS_FreeValue(ctx, image);
    }
    path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_EXCEPTION;
    user = malloc(sizeof(*user));
    if (!user) {
        JS_FreeCString(ctx, path);
        return JS_ThrowOutOfMemory(ctx);
    }
    user->json = json != 0;
    user->options = JS_IsUndefined(options) ? JS_NewObject(ctx) : JS_DupValue(ctx, options);
    user->name = name;
    job = json ? athena_sprite_load_submit(path, NULL, !has_image) :
        athena_sprite_load_submit(NULL, path, false);
    JS_FreeCString(ctx, path);
    return athena_js_job_new(ctx, &sheet_load_kind, job, user, name);
}

/* sheet.addFrame({ x, y, w, h, ... }): the new frame's index. */
static JSValue sheet_add_frame(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    SheetObject *sheet = sheet_this(ctx, this_val);
    int index;

    if (!sheet)
        return JS_EXCEPTION;
    index = sheet_add_frame_value(ctx, sheet, sprite_arg(argc, argv, 0),
        "Sprite.Sheet.addFrame frame");
    return index < 0 ? JS_EXCEPTION : JS_NewInt32(ctx, index);
}

/* sheet.addClip(name, definition): adds or replaces a clip. */
static JSValue sheet_add_clip(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    const char *name = "Sprite.Sheet.addClip";
    SheetObject *sheet = sheet_this(ctx, this_val);
    const char *clip_name;
    int result;

    if (!sheet)
        return JS_EXCEPTION;
    if (!JS_IsString(sprite_arg(argc, argv, 0)))
        return JS_ThrowTypeError(ctx, "%s expects a clip name", name);
    clip_name = JS_ToCString(ctx, argv[0]);
    if (!clip_name)
        return JS_EXCEPTION;
    result = sheet_add_clip_value(ctx, sheet, clip_name, sprite_arg(argc, argv, 1),
        SPRITE_DEFAULT_FPS, name);
    JS_FreeCString(ctx, clip_name);
    return result ? JS_EXCEPTION : JS_DupValue(ctx, this_val);
}

static JSValue sheet_has_clip(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    SheetObject *sheet = sheet_this(ctx, this_val);
    const char *clip_name;
    int index;

    if (!sheet)
        return JS_EXCEPTION;
    if (!JS_IsString(sprite_arg(argc, argv, 0)))
        return JS_FALSE;
    clip_name = JS_ToCString(ctx, argv[0]);
    if (!clip_name)
        return JS_EXCEPTION;
    index = athena_sprite_sheet_find_clip(&sheet->sheet, clip_name);
    JS_FreeCString(ctx, clip_name);
    return JS_NewBool(ctx, index >= 0);
}

static JSValue sheet_find_frame(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    SheetObject *sheet = sheet_this(ctx, this_val);
    const char *frame_name;
    int index;

    if (!sheet)
        return JS_EXCEPTION;
    if (!JS_IsString(sprite_arg(argc, argv, 0)))
        return JS_ThrowTypeError(ctx, "Sprite.Sheet.findFrame expects a frame name");
    frame_name = JS_ToCString(ctx, argv[0]);
    if (!frame_name)
        return JS_EXCEPTION;
    index = athena_sprite_sheet_find_frame(&sheet->sheet, frame_name);
    JS_FreeCString(ctx, frame_name);
    return JS_NewInt32(ctx, index);
}

static void sprite_set_float(JSContext *ctx, JSValue object, const char *key, float value) {
    JS_SetPropertyStr(ctx, object, key, JS_NewFloat64(ctx, (double)value));
}

/* sheet.getFrame(indexOrName): { x, y, w, h, offsetX, offsetY, sourceWidth, sourceHeight, duration, name }. */
static JSValue sheet_get_frame(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    SheetObject *sheet = sheet_this(ctx, this_val);
    const AthenaSpriteFrame *frame;
    uint32_t index;
    JSValue object;

    if (!sheet || sheet_frame_ref(ctx, sheet, sprite_arg(argc, argv, 0), &index,
            "Sprite.Sheet.getFrame"))
        return JS_EXCEPTION;
    frame = &sheet->sheet.frames[index];
    object = JS_NewObject(ctx);
    if (JS_IsException(object))
        return object;
    sprite_set_float(ctx, object, "x", frame->x);
    sprite_set_float(ctx, object, "y", frame->y);
    sprite_set_float(ctx, object, "w", frame->w);
    sprite_set_float(ctx, object, "h", frame->h);
    sprite_set_float(ctx, object, "offsetX", frame->offset_x);
    sprite_set_float(ctx, object, "offsetY", frame->offset_y);
    sprite_set_float(ctx, object, "sourceWidth", frame->source_w);
    sprite_set_float(ctx, object, "sourceHeight", frame->source_h);
    sprite_set_float(ctx, object, "duration", frame->duration * 1000.0f);
    JS_SetPropertyStr(ctx, object, "rotated", JS_NewBool(ctx, frame->rotated));
    JS_SetPropertyStr(ctx, object, "name",
        sheet->sheet.frame_names && sheet->sheet.frame_names[index] ?
            JS_NewString(ctx, sheet->sheet.frame_names[index]) : JS_NULL);
    return object;
}

/* sheet.getClip(name): { name, frames, durations (ms), mode, loops }, or null. */
static JSValue sheet_get_clip(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    SheetObject *sheet = sheet_this(ctx, this_val);
    const AthenaSpriteClip *clip;
    JSValue object, frames, durations;
    const char *clip_name;
    uint32_t i;
    int index;

    if (!sheet)
        return JS_EXCEPTION;
    if (!JS_IsString(sprite_arg(argc, argv, 0)))
        return JS_ThrowTypeError(ctx, "Sprite.Sheet.getClip expects a clip name");
    clip_name = JS_ToCString(ctx, argv[0]);
    if (!clip_name)
        return JS_EXCEPTION;
    index = athena_sprite_sheet_find_clip(&sheet->sheet, clip_name);
    JS_FreeCString(ctx, clip_name);
    if (index < 0)
        return JS_NULL;
    clip = &sheet->sheet.clips[index];
    object = JS_NewObject(ctx);
    frames = JS_NewArray(ctx);
    durations = JS_NewArray(ctx);
    for (i = 0; i < clip->count; i++) {
        JS_SetPropertyUint32(ctx, frames, i, JS_NewInt32(ctx, clip->frames[i]));
        JS_SetPropertyUint32(ctx, durations, i,
            JS_NewFloat64(ctx, (double)clip->durations[i] * 1000.0));
    }
    JS_SetPropertyStr(ctx, object, "name", JS_NewString(ctx, clip->name));
    JS_SetPropertyStr(ctx, object, "frames", frames);
    JS_SetPropertyStr(ctx, object, "durations", durations);
    JS_SetPropertyStr(ctx, object, "mode", JS_NewString(ctx,
        clip->mode == ATHENA_SPRITE_PINGPONG ? "pingpong" :
        clip->loops == 1 ? "once" : "loop"));
    JS_SetPropertyStr(ctx, object, "loops", JS_NewInt64(ctx, clip->loops));
    sprite_set_float(ctx, object, "length", clip->length * 1000.0f);
    JS_SetPropertyStr(ctx, object, "next", clip->next[0] ? JS_NewString(ctx, clip->next) : JS_NULL);
    return object;
}

/* ---- Slices (hitboxes) ----------------------------------------------------- */

/* A { x, y, w, h } rectangle, or a thrown error naming `name`. */
static int sprite_rect_value(JSContext *ctx, JSValueConst value, AthenaSpriteRect *rect,
    const char *name) {
    const float max = ATHENA_SPRITE_MAX_TEXELS;

    if (!JS_IsObject(value)) {
        JS_ThrowTypeError(ctx, "%s must be an object { x, y, w, h }", name);
        return -1;
    }
    return sprite_field(ctx, value, "x", &rect->x, -max, max, name) ||
        sprite_field(ctx, value, "y", &rect->y, -max, max, name) ||
        sprite_field(ctx, value, "w", &rect->w, 0.0f, max, name) ||
        sprite_field(ctx, value, "h", &rect->h, 0.0f, max, name) ? -1 : 0;
}

static JSValue sprite_rect_object(JSContext *ctx, const AthenaSpriteRect *rect) {
    JSValue object = JS_NewObject(ctx);

    if (JS_IsException(object))
        return object;
    sprite_set_float(ctx, object, "x", rect->x);
    sprite_set_float(ctx, object, "y", rect->y);
    sprite_set_float(ctx, object, "w", rect->w);
    sprite_set_float(ctx, object, "h", rect->h);
    return object;
}

static int sheet_set_slice_range(JSContext *ctx, SheetObject *sheet, const char *slice_name,
    uint32_t first, uint32_t count, const AthenaSpriteRect *rect, const char *name) {
    int result = athena_sprite_sheet_set_slice(&sheet->sheet, slice_name, first, count, rect);

    if (result == ATHENA_SPRITE_EINVAL)
        JS_ThrowTypeError(ctx, "%s: slice names must be 1 to %d characters and "
            "rectangles finite", name, ATHENA_SPRITE_NAME_MAX - 1);
    else if (result < 0)
        sprite_throw(ctx, result, name);
    return result < 0 ? -1 : 0;
}

/*
 * Aseprite slices: meta.slices [{ name, keys: [{ frame, bounds }] }]. A key
 * holds from its frame until the next key's, or to the last frame.
 */
static int json_slices(JSContext *ctx, SheetObject *sheet, JSValueConst meta,
    const char *name) {
    JSValue slices = JS_IsObject(meta) ? JS_GetPropertyStr(ctx, meta, "slices") : JS_UNDEFINED;
    uint32_t length = 0, i;
    int result = 0;

    if (JS_IsException(slices))
        return -1;
    if (!JS_IsArray(ctx, slices)) {
        JS_FreeValue(ctx, slices);
        return 0;
    }
    if (sprite_length(ctx, slices, &length))
        result = -1;
    for (i = 0; !result && i < length; i++) {
        JSValue slice = JS_GetPropertyUint32(ctx, slices, i);
        JSValue slice_name = JS_UNDEFINED, keys = JS_UNDEFINED;
        const char *text = NULL;
        uint32_t key_count = 0, k;

        if (JS_IsException(slice) || !JS_IsObject(slice)) {
            if (!JS_IsException(slice))
                JS_ThrowTypeError(ctx, "%s: meta.slices entries must be objects", name);
            result = -1;
            goto next;
        }
        slice_name = JS_GetPropertyStr(ctx, slice, "name");
        keys = JS_GetPropertyStr(ctx, slice, "keys");
        if (!JS_IsString(slice_name) || !JS_IsArray(ctx, keys)) {
            if (!JS_IsException(slice_name) && !JS_IsException(keys))
                JS_ThrowTypeError(ctx, "%s: a slice needs a name and keys", name);
            result = -1;
            goto next;
        }
        text = JS_ToCString(ctx, slice_name);
        if (!text || sprite_length(ctx, keys, &key_count)) {
            result = -1;
            goto next;
        }
        for (k = 0; !result && k < key_count; k++) {
            JSValue key = JS_GetPropertyUint32(ctx, keys, k);
            JSValue following = k + 1 < key_count ?
                JS_GetPropertyUint32(ctx, keys, k + 1) : JS_UNDEFINED;
            JSValue frame_value = JS_UNDEFINED, next_value = JS_UNDEFINED, bounds = JS_UNDEFINED;
            uint32_t from = 0, to = sheet->sheet.frame_count;
            AthenaSpriteRect rect;

            if (!JS_IsObject(key)) {
                if (!JS_IsException(key))
                    JS_ThrowTypeError(ctx, "%s: slice keys must be objects", name);
                result = -1;
            } else {
                frame_value = JS_GetPropertyStr(ctx, key, "frame");
                bounds = JS_GetPropertyStr(ctx, key, "bounds");
                if (JS_IsObject(following))
                    next_value = JS_GetPropertyStr(ctx, following, "frame");
                if (sprite_uint(ctx, frame_value, &from, sheet->sheet.frame_count - 1, name) ||
                    (JS_IsNumber(next_value) &&
                        sprite_uint(ctx, next_value, &to, sheet->sheet.frame_count, name)) ||
                    sprite_rect_value(ctx, bounds, &rect, name))
                    result = -1;
                else if (to > from)
                    result = sheet_set_slice_range(ctx, sheet, text, from, to - from, &rect, name);
            }
            JS_FreeValue(ctx, frame_value);
            JS_FreeValue(ctx, next_value);
            JS_FreeValue(ctx, bounds);
            JS_FreeValue(ctx, following);
            JS_FreeValue(ctx, key);
        }
    next:
        JS_FreeCString(ctx, text);
        JS_FreeValue(ctx, slice_name);
        JS_FreeValue(ctx, keys);
        JS_FreeValue(ctx, slice);
    }
    JS_FreeValue(ctx, slices);
    return result;
}

/* sheet.setSlice(name, rect | null, frames?): the rectangle on those frames (default all). */
static JSValue sheet_set_slice(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    const char *name = "Sprite.Sheet.setSlice";
    SheetObject *sheet = sheet_this(ctx, this_val);
    JSValueConst rect_value = sprite_arg(argc, argv, 1), frames_value = sprite_arg(argc, argv, 2);
    AthenaSpriteRect rect;
    const AthenaSpriteRect *rect_ptr = NULL;
    const char *slice_name;
    uint16_t *frames = NULL;
    uint32_t count, frame, i;
    int result = 0;

    if (!sheet)
        return JS_EXCEPTION;
    if (!JS_IsString(sprite_arg(argc, argv, 0)))
        return JS_ThrowTypeError(ctx, "%s expects a slice name", name);
    if (!JS_IsNull(rect_value)) {
        if (sprite_rect_value(ctx, rect_value, &rect, "Sprite.Sheet.setSlice rect"))
            return JS_EXCEPTION;
        rect_ptr = &rect;
    }
    slice_name = JS_ToCString(ctx, argv[0]);
    if (!slice_name)
        return JS_EXCEPTION;
    if (JS_IsUndefined(frames_value)) {
        result = sheet_set_slice_range(ctx, sheet, slice_name, 0, sheet->sheet.frame_count,
            rect_ptr, name);
    } else if (JS_IsNumber(frames_value)) {
        result = sheet->sheet.frame_count == 0 ? -1 :
            sprite_uint(ctx, frames_value, &frame, sheet->sheet.frame_count - 1, name);
        if (sheet->sheet.frame_count == 0)
            JS_ThrowRangeError(ctx, "%s: the sheet has no frames", name);
        if (!result)
            result = sheet_set_slice_range(ctx, sheet, slice_name, frame, 1, rect_ptr, name);
    } else {
        result = sheet_clip_frames(ctx, sheet, frames_value, &frames, &count, name);
        for (i = 0; !result && i < count; i++)
            result = sheet_set_slice_range(ctx, sheet, slice_name, frames[i], 1, rect_ptr, name);
        js_free(ctx, frames);
    }
    JS_FreeCString(ctx, slice_name);
    return result ? JS_EXCEPTION : JS_DupValue(ctx, this_val);
}

/* sheet.getSlice(name, frame): { x, y, w, h } in untrimmed frame texels, or null. */
static JSValue sheet_get_slice(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    SheetObject *sheet = sheet_this(ctx, this_val);
    AthenaSpriteRect rect;
    const char *slice_name;
    uint32_t frame;
    int slice;

    if (!sheet)
        return JS_EXCEPTION;
    if (!JS_IsString(sprite_arg(argc, argv, 0)))
        return JS_ThrowTypeError(ctx, "Sprite.Sheet.getSlice expects a slice name");
    if (sheet_frame_ref(ctx, sheet, sprite_arg(argc, argv, 1), &frame, "Sprite.Sheet.getSlice"))
        return JS_EXCEPTION;
    slice_name = JS_ToCString(ctx, argv[0]);
    if (!slice_name)
        return JS_EXCEPTION;
    slice = athena_sprite_sheet_find_slice(&sheet->sheet, slice_name);
    JS_FreeCString(ctx, slice_name);
    if (!athena_sprite_sheet_slice(&sheet->sheet, slice, frame, &rect))
        return JS_NULL;
    return sprite_rect_object(ctx, &rect);
}

enum { SHEET_IMAGE, SHEET_FRAME_COUNT, SHEET_CLIP_NAMES, SHEET_SLICE_NAMES, SHEET_INSET };

static JSValue sheet_get(JSContext *ctx, JSValueConst this_val, int magic) {
    SheetObject *sheet = sheet_this(ctx, this_val);
    JSValue names;
    uint32_t i;

    if (!sheet)
        return JS_EXCEPTION;
    switch (magic) {
    case SHEET_IMAGE:
        return JS_DupValue(ctx, sheet->image);
    case SHEET_FRAME_COUNT:
        return JS_NewInt32(ctx, (int32_t)sheet->sheet.frame_count);
    case SHEET_INSET:
        return JS_NewFloat64(ctx, sheet->sheet.inset);
    case SHEET_SLICE_NAMES:
        names = JS_NewArray(ctx);
        for (i = 0; i < sheet->sheet.slice_count; i++)
            JS_SetPropertyUint32(ctx, names, i, JS_NewString(ctx, sheet->sheet.slices[i].name));
        return names;
    default:
        names = JS_NewArray(ctx);
        for (i = 0; i < sheet->sheet.clip_count; i++)
            JS_SetPropertyUint32(ctx, names, i, JS_NewString(ctx, sheet->sheet.clips[i].name));
        return names;
    }
}

static void animator_invalidate_sheet(SheetObject *sheet);

static JSValue sheet_set(JSContext *ctx, JSValueConst this_val, JSValueConst value,
    int magic) {
    SheetObject *sheet = sheet_this(ctx, this_val);

    if (!sheet || sprite_number(ctx, value, &sheet->sheet.inset, 0.0f, SPRITE_MAX_INSET,
            "Sprite.Sheet.inset"))
        return JS_EXCEPTION;
    /* Animated TileMap sprites of this sheet get their texture rectangles again. */
    animator_invalidate_sheet(sheet);
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry sheet_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("image", sheet_get, NULL, SHEET_IMAGE),
    JS_CGETSET_MAGIC_DEF("frameCount", sheet_get, NULL, SHEET_FRAME_COUNT),
    JS_CGETSET_MAGIC_DEF("clipNames", sheet_get, NULL, SHEET_CLIP_NAMES),
    JS_CGETSET_MAGIC_DEF("sliceNames", sheet_get, NULL, SHEET_SLICE_NAMES),
    JS_CGETSET_MAGIC_DEF("inset", sheet_get, sheet_set, SHEET_INSET),
    JS_CFUNC_DEF("setSlice", 3, sheet_set_slice),
    JS_CFUNC_DEF("getSlice", 2, sheet_get_slice),
    JS_CFUNC_DEF("addFrame", 1, sheet_add_frame),
    JS_CFUNC_DEF("addClip", 2, sheet_add_clip),
    JS_CFUNC_DEF("hasClip", 1, sheet_has_clip),
    JS_CFUNC_DEF("findFrame", 1, sheet_find_frame),
    JS_CFUNC_DEF("getFrame", 1, sheet_get_frame),
    JS_CFUNC_DEF("getClip", 1, sheet_get_clip),
};

static const JSCFunctionListEntry sheet_static_funcs[] = {
    JS_CFUNC_DEF("fromGrid", 2, sheet_from_grid),
    JS_CFUNC_DEF("fromJSON", 2, sheet_from_json),
    JS_CFUNC_MAGIC_DEF("fromJSONAsync", 2, sheet_load_async, 1),
    JS_CFUNC_MAGIC_DEF("fromGridAsync", 2, sheet_load_async, 0),
};

/* ---------------------------------------------------------------------- */
/* Instance                                                               */

static void instance_finalizer(JSRuntime *rt, JSValue value) {
    SpriteObject *sprite = JS_GetOpaque(value, instance_class_id);
    uint32_t i;

    if (!sprite)
        return;
    sprite_unlink(sprite);
    for (i = 0; i < sprite->listener_count; i++)
        JS_FreeValueRT(rt, sprite->listeners[i].func);
    js_free_rt(rt, sprite->listeners);
    JS_FreeValueRT(rt, sprite->settle);
    JS_FreeValueRT(rt, sprite->sheet);
    js_free_rt(rt, sprite);
}

/* The sheet and the listeners live in C memory. */
static void instance_gc_mark(JSRuntime *rt, JSValueConst value, JS_MarkFunc *mark) {
    SpriteObject *sprite = JS_GetOpaque(value, instance_class_id);
    uint32_t i;

    if (!sprite)
        return;
    JS_MarkValue(rt, sprite->sheet, mark);
    JS_MarkValue(rt, sprite->settle, mark);
    for (i = 0; i < sprite->listener_count; i++)
        JS_MarkValue(rt, sprite->listeners[i].func, mark);
}

static JSClassDef instance_class = {
    "Instance",
    .finalizer = instance_finalizer,
    .gc_mark = instance_gc_mark,
};

static SpriteObject *instance_this(JSContext *ctx, JSValueConst value) {
    return JS_GetOpaque2(ctx, value, instance_class_id);
}

static int instance_play(JSContext *ctx, SpriteObject *sprite, JSValueConst clip_value,
    JSValueConst options, const char *name) {
    bool restart = false;
    uint32_t position = 0;
    int32_t clip;
    int has_position;

    if (sprite_check_options(ctx, options, name) ||
        sheet_clip_ref(ctx, sprite->data, clip_value, &clip, name) ||
        sprite_option_bool(ctx, options, "restart", &restart) < 0 ||
        sprite_option_number(ctx, options, "speed", &sprite->anim.speed, 0.0f,
            SPRITE_MAX_SPEED, name) < 0)
        return -1;
    has_position = sprite_option_uint(ctx, options, "position", &position,
        sprite->data->sheet.clips[clip].count - 1, name);
    if (has_position < 0)
        return -1;
    /* Already playing it: nothing to do, as games call play() every frame. */
    if (!restart && !has_position && sprite->anim.clip == clip && sprite->anim.playing)
        return 0;
    /* A new play: a playAsync() waiting for the previous one gets false. */
    if (sprite_settle(ctx, sprite, false))
        return -1;
    sprite->play_id++;
    athena_sprite_anim_play(&sprite->anim, &sprite->data->sheet, clip);
    if (has_position)
        athena_sprite_anim_seek(&sprite->anim, &sprite->data->sheet, position);
    sprite->paused = false;
    sprite_sync_link(sprite);
    return 0;
}

/* Instance options: clip, frame, x, y, origin, scale, rotation, flip, color, speed, autoUpdate, realTime. */
static int instance_apply_options(JSContext *ctx, SpriteObject *sprite,
    JSValueConst options, const char *name) {
    AthenaSpriteDraw *draw = &sprite->draw;
    JSValue value;
    uint32_t frame;
    int found;

    if (sprite_check_options(ctx, options, name))
        return -1;
    found = sprite_option(ctx, options, "origin", &value);
    if (found < 0)
        return -1;
    if (found) {
        found = sprite_pair(ctx, value, &draw->origin_x, &draw->origin_y, -SPRITE_MAX_SCALE,
            SPRITE_MAX_SCALE, "Sprite.Instance origin");
        JS_FreeValue(ctx, value);
        if (found)
            return -1;
    }
    found = sprite_option(ctx, options, "scale", &value);
    if (found < 0)
        return -1;
    if (found) {
        found = sprite_pair(ctx, value, &draw->scale_x, &draw->scale_y, 0.0f,
            SPRITE_MAX_SCALE, "Sprite.Instance scale");
        JS_FreeValue(ctx, value);
        if (found)
            return -1;
    }
    if (sprite_option_number(ctx, options, "rotation", &draw->rotation, -SPRITE_MAX_COORD,
            SPRITE_MAX_COORD, name) < 0 ||
        sprite_option_bool(ctx, options, "flipX", &draw->flip_x) < 0 ||
        sprite_option_bool(ctx, options, "flipY", &draw->flip_y) < 0 ||
        sprite_option_number(ctx, options, "speed", &sprite->anim.speed, 0.0f,
            SPRITE_MAX_SPEED, name) < 0 ||
        sprite_option_bool(ctx, options, "autoUpdate", &sprite->auto_update) < 0 ||
        sprite_option_bool(ctx, options, "realTime", &sprite->real_time) < 0 ||
        sprite_option_bool(ctx, options, "debug", &sprite->debug) < 0 ||
        sprite_option_number(ctx, options, "x", &draw->x, -SPRITE_MAX_COORD,
            SPRITE_MAX_COORD, name) < 0 ||
        sprite_option_number(ctx, options, "y", &draw->y, -SPRITE_MAX_COORD,
            SPRITE_MAX_COORD, name) < 0)
        return -1;
    found = sprite_option(ctx, options, "color", &value);
    if (found < 0)
        return -1;
    if (found) {
        found = sprite_color(ctx, value, &draw->color, "Sprite.Instance color");
        JS_FreeValue(ctx, value);
        if (found)
            return -1;
    }
    found = sprite_option(ctx, options, "frame", &value);
    if (found < 0)
        return -1;
    if (found) {
        found = sheet_frame_ref(ctx, sprite->data, value, &frame, "Sprite.Instance frame");
        JS_FreeValue(ctx, value);
        if (found)
            return -1;
        athena_sprite_anim_show(&sprite->anim, &sprite->data->sheet, frame);
    }
    found = sprite_option(ctx, options, "clip", &value);
    if (found < 0)
        return -1;
    if (found) {
        found = instance_play(ctx, sprite, value, JS_UNDEFINED, "Sprite.Instance clip");
        JS_FreeValue(ctx, value);
        if (found)
            return -1;
    }
    return 0;
}

/* new Sprite.Instance(sheet, options) */
static JSValue instance_ctor(JSContext *ctx, JSValueConst new_target, int argc,
    JSValueConst *argv) {
    const char *name = "Sprite.Instance";
    JSValueConst sheet_value = sprite_arg(argc, argv, 0);
    SheetObject *sheet = JS_GetOpaque(sheet_value, sheet_class_id);
    SpriteObject *sprite;
    JSValue object;

    if (sprite_main(ctx, name))
        return JS_EXCEPTION;
    if (!sheet)
        return JS_ThrowTypeError(ctx, "%s expects a Sprite.Sheet", name);
    if (sheet->sheet.frame_count == 0)
        return JS_ThrowRangeError(ctx, "%s: the sheet has no frames", name);
    object = sprite_new_object(ctx, new_target, instance_class_id);
    if (JS_IsException(object))
        return object;
    sprite = js_mallocz(ctx, sizeof(*sprite));
    if (!sprite) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    sprite->object = JS_VALUE_GET_PTR(object);
    sprite->sheet = JS_DupValue(ctx, sheet_value);
    sprite->data = sheet;
    sprite->auto_update = true;
    sprite->settle = JS_UNDEFINED;
    athena_sprite_anim_init(&sprite->anim);
    athena_sprite_draw_init(&sprite->draw);
    JS_SetOpaque(object, sprite);
    if (instance_apply_options(ctx, sprite, sprite_arg(argc, argv, 1), name)) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    return object;
}

static JSValue instance_play_method(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    SpriteObject *sprite = instance_this(ctx, this_val);

    if (!sprite || instance_play(ctx, sprite, sprite_arg(argc, argv, 0),
            sprite_arg(argc, argv, 1), "Sprite.Instance.play"))
        return JS_EXCEPTION;
    return JS_DupValue(ctx, this_val);
}

enum { CONTROL_PAUSE, CONTROL_RESUME, CONTROL_STOP };

static JSValue instance_control(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int magic) {
    SpriteObject *sprite = instance_this(ctx, this_val);

    if (!sprite)
        return JS_EXCEPTION;
    switch (magic) {
    case CONTROL_PAUSE:
        if (sprite->anim.playing) {
            sprite->anim.playing = false;
            sprite->paused = true;
        }
        break;
    case CONTROL_RESUME:
        if (sprite->paused && sprite->anim.clip >= 0 && !sprite->anim.finished)
            sprite->anim.playing = true;
        sprite->paused = false;
        break;
    default:
        /*
         * Back to the clip's first frame, paused, without an end event:
         * resume() plays it from there. A playAsync() waiting gets false.
         */
        if (sprite_settle(ctx, sprite, false))
            return JS_EXCEPTION;
        if (sprite->anim.clip >= 0) {
            sprite->play_id++;
            athena_sprite_anim_play(&sprite->anim, &sprite->data->sheet, sprite->anim.clip);
            sprite->anim.playing = false;
            sprite->paused = true;
        }
        break;
    }
    sprite_sync_link(sprite);
    return JS_DupValue(ctx, this_val);
}

/* instance.update(dt): advances this instance now and dispatches its events. */
static JSValue instance_update(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    SpriteObject *sprite = instance_this(ctx, this_val);
    float dt;
    int result;

    if (!sprite || sprite_number(ctx, sprite_arg(argc, argv, 0), &dt, 0.0f,
            SPRITE_MAX_COORD, "Sprite.Instance.update dt"))
        return JS_EXCEPTION;
    if (sprite_enter(ctx, "Sprite.Instance.update"))
        return JS_EXCEPTION;
    sprite_advance(sprite, dt);
    sprite_sync_link(sprite);
    result = sprite_flush_events(ctx);
    state.depth--;
    return result ? JS_EXCEPTION : JS_DupValue(ctx, this_val);
}

/*
 * Optional (x, y) arguments: the instance's position when absent. draw()
 * runs once per sprite per frame: the error labels are constants, never
 * formatted (snprintf per call cost more than the drawing on the EE).
 */
static int instance_xy(JSContext *ctx, SpriteObject *sprite, int argc, JSValueConst *argv,
    float *x, float *y, const char *label_x, const char *label_y) {
    *x = sprite->draw.x;
    *y = sprite->draw.y;
    if (argc == 0 || JS_IsUndefined(argv[0]))
        return 0;
    if (sprite_number(ctx, argv[0], x, -SPRITE_MAX_COORD, SPRITE_MAX_COORD, label_x))
        return -1;
    return sprite_number(ctx, sprite_arg(argc, argv, 1), y, -SPRITE_MAX_COORD,
        SPRITE_MAX_COORD, label_y);
}

/* The sheet's image to draw with, NULL while it is loading, or a thrown error without one. */
static int instance_image(JSContext *ctx, SpriteObject *sprite, AthenaImage **image,
    const char *name) {
    AthenaImage *peeked;

    if (JS_IsNull(sprite->data->image)) {
        JS_ThrowTypeError(ctx, "%s: the sheet has no image", name);
        return -1;
    }
    peeked = athena_image_peek(sprite->data->image);
    /* An image still loading (ImageList) or freed draws nothing, like TileMap. */
    *image = peeked && athena_image_is_loaded(peeked) ? peeked : NULL;
    return 0;
}

/* instance.draw(x?, y?): the current frame at (x, y), or at its x and y, through the camera. */
static JSValue instance_draw(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    SpriteObject *sprite = instance_this(ctx, this_val);
    AthenaImage *image;

    if (!sprite ||
        instance_xy(ctx, sprite, argc, argv, &sprite->draw.x, &sprite->draw.y,
            "Sprite.Instance.draw x", "Sprite.Instance.draw y") ||
        instance_image(ctx, sprite, &image, "Sprite.Instance.draw"))
        return JS_EXCEPTION;
    if (image)
        athena_sprite_draw(image, &sprite->data->sheet,
            athena_sprite_anim_frame(&sprite->anim, &sprite->data->sheet), &sprite->draw);
    if (sprite->debug || state.debug)
        athena_sprite_draw_debug(&sprite->data->sheet,
            athena_sprite_anim_frame(&sprite->anim, &sprite->data->sheet), &sprite->draw);
    return JS_UNDEFINED;
}

/* instance.getBounds(x?, y?): { x, y, w, h } of the frame drawn there, without rotation. */
static JSValue instance_get_bounds(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    SpriteObject *sprite = instance_this(ctx, this_val);
    AthenaSpriteDraw draw;
    AthenaSpriteQuad quad;
    AthenaSpriteRect rect;

    if (!sprite)
        return JS_EXCEPTION;
    draw = sprite->draw;
    draw.rotation = 0.0f;
    if (instance_xy(ctx, sprite, argc, argv, &draw.x, &draw.y, "Sprite.Instance.getBounds x",
            "Sprite.Instance.getBounds y"))
        return JS_EXCEPTION;
    if (!athena_sprite_quad(&sprite->data->sheet.frames[athena_sprite_anim_frame(
            &sprite->anim, &sprite->data->sheet)], &draw, 0.0f, &quad))
        memset(&quad, 0, sizeof(quad));
    rect.x = quad.x;
    rect.y = quad.y;
    rect.w = quad.w;
    rect.h = quad.h;
    return sprite_rect_object(ctx, &rect);
}

/*
 * instance.getSlice(name, x?, y?): world { x, y, w, h } of the slice in the
 * current frame, flipped, placed by the origin and scaled (not rotated), or
 * null when the frame has none.
 */
static JSValue instance_get_slice(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    SpriteObject *sprite = instance_this(ctx, this_val);
    AthenaSpriteRect rect, placed;
    AthenaSpriteDraw draw;
    const char *slice_name;
    uint32_t frame;
    int slice;

    if (!sprite)
        return JS_EXCEPTION;
    if (!JS_IsString(sprite_arg(argc, argv, 0)))
        return JS_ThrowTypeError(ctx, "Sprite.Instance.getSlice expects a slice name");
    draw = sprite->draw;
    if (instance_xy(ctx, sprite, argc - 1, argv + 1, &draw.x, &draw.y,
            "Sprite.Instance.getSlice x", "Sprite.Instance.getSlice y"))
        return JS_EXCEPTION;
    slice_name = JS_ToCString(ctx, argv[0]);
    if (!slice_name)
        return JS_EXCEPTION;
    slice = athena_sprite_sheet_find_slice(&sprite->data->sheet, slice_name);
    JS_FreeCString(ctx, slice_name);
    frame = athena_sprite_anim_frame(&sprite->anim, &sprite->data->sheet);
    if (!athena_sprite_sheet_slice(&sprite->data->sheet, slice, frame, &rect) ||
        !athena_sprite_place_rect(&sprite->data->sheet.frames[frame], &draw, &rect, &placed))
        return JS_NULL;
    return sprite_rect_object(ctx, &placed);
}

/*
 * instance.playAsync(clip, options): play(), and a promise of true when the
 * clip ends, or false when another play, stop() or a still frame replaces
 * it first. A clip that loops forever only settles by being replaced.
 */
static JSValue instance_play_async(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    SpriteObject *sprite = instance_this(ctx, this_val);
    JSValue promise, funcs[2];

    if (!sprite || instance_play(ctx, sprite, sprite_arg(argc, argv, 0),
            sprite_arg(argc, argv, 1), "Sprite.Instance.playAsync"))
        return JS_EXCEPTION;
    /* Still playing the same clip (play() did nothing): a second wait replaces the first. */
    if (sprite_settle(ctx, sprite, false))
        return JS_EXCEPTION;
    promise = JS_NewPromiseCapability(ctx, funcs);
    if (JS_IsException(promise))
        return promise;
    JS_FreeValue(ctx, funcs[1]);
    if (sprite_pin(ctx, sprite)) {
        JS_FreeValue(ctx, funcs[0]);
        JS_FreeValue(ctx, promise);
        return JS_EXCEPTION;
    }
    sprite->settle = funcs[0];
    sprite->settle_play = sprite->play_id;
    sprite->settle_clip = sprite->anim.clip;
    return promise;
}

static JSValue instance_set_pair(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int magic) {
    SpriteObject *sprite = instance_this(ctx, this_val);
    const char *name = magic ? "Sprite.Instance.setScale" : "Sprite.Instance.setOrigin";
    float min = magic ? 0.0f : -SPRITE_MAX_SCALE;
    float x, y;

    if (!sprite)
        return JS_EXCEPTION;
    if (sprite_number(ctx, sprite_arg(argc, argv, 0), &x, min, SPRITE_MAX_SCALE, name))
        return JS_EXCEPTION;
    y = x;
    if (argc > 1 && !JS_IsUndefined(argv[1]) &&
        sprite_number(ctx, argv[1], &y, min, SPRITE_MAX_SCALE, name))
        return JS_EXCEPTION;
    if (magic) {
        sprite->draw.scale_x = x;
        sprite->draw.scale_y = y;
    } else {
        sprite->draw.origin_x = x;
        sprite->draw.origin_y = y;
    }
    return JS_DupValue(ctx, this_val);
}

/* A position (the digits of "N"), or -1. */
static int32_t listener_position(const char *text) {
    char *end;
    unsigned long n;

    if (text[0] < '0' || text[0] > '9' || strlen(text) > 6)
        return -1;
    n = strtoul(text, &end, 10);
    return *end == '\0' && n <= ATHENA_SPRITE_MAX_FRAMES ? (int32_t)n : -1;
}

typedef struct {
    ListenType type;
    uint32_t position;
    int32_t clip;
} ListenSpec;

/*
 * An event name: "frame", "loop", "end"; "frame:N" (position N of any
 * clip); "clip:N" (position N of that clip); "loop:clip" and "end:clip".
 * Clip names are resolved now, so a typo throws at on() and not never fires.
 */
static int listener_parse(JSContext *ctx, SheetObject *sheet, JSValueConst value,
    ListenSpec *spec, const char *name) {
    const char *text, *colon;
    int result = 0;

    if (!JS_IsString(value)) {
        JS_ThrowTypeError(ctx, "%s event must be a string such as \"end\" or \"run:3\"", name);
        return -1;
    }
    text = JS_ToCString(ctx, value);
    if (!text)
        return -1;
    spec->position = 0;
    spec->clip = -1;
    colon = strchr(text, ':');
    if (!colon) {
        if (strcmp(text, "frame") == 0)
            spec->type = LISTEN_FRAME;
        else if (strcmp(text, "loop") == 0)
            spec->type = LISTEN_LOOP;
        else if (strcmp(text, "end") == 0)
            spec->type = LISTEN_END;
        else
            result = -1;
    } else {
        size_t prefix = (size_t)(colon - text);
        const char *rest = colon + 1;

        if ((prefix == 4 && strncmp(text, "loop", 4) == 0) ||
            (prefix == 3 && strncmp(text, "end", 3) == 0)) {
            spec->type = prefix == 4 ? LISTEN_LOOP : LISTEN_END;
            spec->clip = athena_sprite_sheet_find_clip(&sheet->sheet, rest);
            if (spec->clip < 0) {
                JS_ThrowRangeError(ctx, "%s: the sheet has no clip named '%s'", name, rest);
                JS_FreeCString(ctx, text);
                return -1;
            }
        } else {
            int32_t position = listener_position(rest);

            spec->type = LISTEN_FRAME_AT;
            if (position < 0) {
                result = -1;
            } else if (prefix == 5 && strncmp(text, "frame", 5) == 0) {
                spec->position = (uint32_t)position;
            } else if (prefix > 0 && prefix < ATHENA_SPRITE_NAME_MAX) {
                char clip_name[ATHENA_SPRITE_NAME_MAX];

                memcpy(clip_name, text, prefix);
                clip_name[prefix] = 0;
                spec->position = (uint32_t)position;
                spec->clip = athena_sprite_sheet_find_clip(&sheet->sheet, clip_name);
                if (spec->clip < 0) {
                    JS_ThrowRangeError(ctx, "%s: the sheet has no clip named '%s'", name,
                        clip_name);
                    JS_FreeCString(ctx, text);
                    return -1;
                }
            } else {
                result = -1;
            }
        }
    }
    if (result)
        JS_ThrowTypeError(ctx, "%s: unknown event '%s' (\"frame\", \"loop\", \"end\", "
            "\"frame:N\", \"clip:N\", \"loop:clip\" or \"end:clip\")", name, text);
    JS_FreeCString(ctx, text);
    return result;
}

static JSValue instance_on(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int once) {
    const char *name = once ? "Sprite.Instance.once" : "Sprite.Instance.on";
    SpriteObject *sprite = instance_this(ctx, this_val);
    Listener *listener;
    ListenSpec spec;

    if (!sprite || listener_parse(ctx, sprite->data, sprite_arg(argc, argv, 0), &spec, name))
        return JS_EXCEPTION;
    if (!JS_IsFunction(ctx, sprite_arg(argc, argv, 1)))
        return JS_ThrowTypeError(ctx, "%s expects a function", name);
    if (sprite->listener_count == sprite->listener_capacity) {
        uint32_t capacity = sprite->listener_capacity ? sprite->listener_capacity * 2 : 4;
        Listener *listeners = js_realloc(ctx, sprite->listeners,
            capacity * sizeof(*listeners));

        if (!listeners)
            return JS_EXCEPTION;
        sprite->listeners = listeners;
        sprite->listener_capacity = capacity;
    }
    listener = &sprite->listeners[sprite->listener_count++];
    listener->type = spec.type;
    listener->position = spec.position;
    listener->clip = spec.clip;
    listener->once = once != 0;
    listener->func = JS_DupValue(ctx, argv[1]);
    sprite_update_mask(sprite);
    return JS_DupValue(ctx, this_val);
}

/* off(): every listener; off(event): those of the event; off(event, fn): that one. */
static JSValue instance_off(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    const char *name = "Sprite.Instance.off";
    SpriteObject *sprite = instance_this(ctx, this_val);
    JSValueConst func = sprite_arg(argc, argv, 1);
    bool any_event = argc == 0 || JS_IsUndefined(argv[0]);
    ListenSpec spec = { LISTEN_FRAME, 0, -1 };
    uint32_t i;

    if (!sprite || (!any_event && listener_parse(ctx, sprite->data, argv[0], &spec, name)))
        return JS_EXCEPTION;
    for (i = 0; i < sprite->listener_count;) {
        Listener *listener = &sprite->listeners[i];
        bool matches = any_event || (listener->type == spec.type &&
            listener->clip == spec.clip &&
            (spec.type != LISTEN_FRAME_AT || listener->position == spec.position));

        if (matches && JS_IsFunction(ctx, func) &&
            JS_VALUE_GET_PTR(listener->func) != JS_VALUE_GET_PTR(func))
            matches = false;
        if (matches)
            sprite_remove_listener(ctx, sprite, i);
        else
            i++;
    }
    sprite_update_mask(sprite);
    return JS_DupValue(ctx, this_val);
}

enum {
    INST_SHEET, INST_CLIP, INST_FRAME, INST_POSITION, INST_PLAYING, INST_PAUSED,
    INST_FINISHED, INST_PROGRESS, INST_CYCLES, INST_SPEED, INST_FLIP_X, INST_FLIP_Y,
    INST_ORIGIN_X, INST_ORIGIN_Y, INST_SCALE_X, INST_SCALE_Y, INST_ROTATION, INST_COLOR,
    INST_AUTO_UPDATE, INST_WIDTH, INST_HEIGHT, INST_X, INST_Y, INST_REAL_TIME, INST_DEBUG
};

static JSValue instance_get(JSContext *ctx, JSValueConst this_val, int magic) {
    SpriteObject *sprite = instance_this(ctx, this_val);
    const AthenaSpriteSheet *sheet;
    const AthenaSpriteFrame *frame;

    if (!sprite)
        return JS_EXCEPTION;
    sheet = &sprite->data->sheet;
    switch (magic) {
    case INST_SHEET:
        return JS_DupValue(ctx, sprite->sheet);
    case INST_CLIP:
        if (sprite->anim.clip < 0 || (uint32_t)sprite->anim.clip >= sheet->clip_count)
            return JS_NULL;
        return JS_NewString(ctx, sheet->clips[sprite->anim.clip].name);
    case INST_FRAME:
        return JS_NewInt32(ctx, (int32_t)athena_sprite_anim_frame(&sprite->anim, sheet));
    case INST_POSITION:
        return JS_NewInt32(ctx, sprite->anim.clip >= 0 ? (int32_t)sprite->anim.position : 0);
    case INST_PLAYING:
        return JS_NewBool(ctx, sprite->anim.playing);
    case INST_PAUSED:
        return JS_NewBool(ctx, sprite->paused);
    case INST_FINISHED:
        return JS_NewBool(ctx, sprite->anim.finished);
    case INST_PROGRESS:
        return JS_NewFloat64(ctx, athena_sprite_anim_progress(&sprite->anim, sheet));
    case INST_CYCLES:
        return JS_NewInt64(ctx, sprite->anim.cycles);
    case INST_SPEED:
        return JS_NewFloat64(ctx, sprite->anim.speed);
    case INST_FLIP_X:
        return JS_NewBool(ctx, sprite->draw.flip_x);
    case INST_FLIP_Y:
        return JS_NewBool(ctx, sprite->draw.flip_y);
    case INST_ORIGIN_X:
        return JS_NewFloat64(ctx, sprite->draw.origin_x);
    case INST_ORIGIN_Y:
        return JS_NewFloat64(ctx, sprite->draw.origin_y);
    case INST_SCALE_X:
        return JS_NewFloat64(ctx, sprite->draw.scale_x);
    case INST_SCALE_Y:
        return JS_NewFloat64(ctx, sprite->draw.scale_y);
    case INST_ROTATION:
        return JS_NewFloat64(ctx, sprite->draw.rotation);
    case INST_COLOR:
        return JS_NewUint32(ctx, sprite->draw.color);
    case INST_AUTO_UPDATE:
        return JS_NewBool(ctx, sprite->auto_update);
    case INST_REAL_TIME:
        return JS_NewBool(ctx, sprite->real_time);
    case INST_DEBUG:
        return JS_NewBool(ctx, sprite->debug);
    case INST_X:
        return JS_NewFloat64(ctx, sprite->draw.x);
    case INST_Y:
        return JS_NewFloat64(ctx, sprite->draw.y);
    case INST_WIDTH:
    case INST_HEIGHT:
        frame = &sheet->frames[athena_sprite_anim_frame(&sprite->anim, sheet)];
        return JS_NewFloat64(ctx, magic == INST_WIDTH ?
            frame->source_w * sprite->draw.scale_x : frame->source_h * sprite->draw.scale_y);
    default:
        return JS_UNDEFINED;
    }
}

static JSValue instance_set(JSContext *ctx, JSValueConst this_val, JSValueConst value,
    int magic) {
    SpriteObject *sprite = instance_this(ctx, this_val);
    AthenaSpriteDraw *draw;
    uint32_t index;
    int result = 0;

    if (!sprite)
        return JS_EXCEPTION;
    draw = &sprite->draw;
    switch (magic) {
    case INST_FRAME:
        result = sheet_frame_ref(ctx, sprite->data, value, &index, "Sprite.Instance.frame");
        if (!result)
            result = sprite_settle(ctx, sprite, false);
        if (!result) {
            sprite->play_id++;
            athena_sprite_anim_show(&sprite->anim, &sprite->data->sheet, index);
            sprite->paused = false;
        }
        break;
    case INST_POSITION:
        if (sprite->anim.clip < 0) {
            JS_ThrowTypeError(ctx, "Sprite.Instance.position needs a clip; play() one first");
            return JS_EXCEPTION;
        }
        result = sprite_uint(ctx, value, &index,
            sprite->data->sheet.clips[sprite->anim.clip].count - 1, "Sprite.Instance.position");
        if (!result) {
            athena_sprite_anim_seek(&sprite->anim, &sprite->data->sheet, index);
            sprite->paused = !sprite->anim.playing;
        }
        break;
    case INST_SPEED:
        result = sprite_number(ctx, value, &sprite->anim.speed, 0.0f, SPRITE_MAX_SPEED,
            "Sprite.Instance.speed");
        break;
    case INST_FLIP_X:
        draw->flip_x = JS_ToBool(ctx, value) != 0;
        break;
    case INST_FLIP_Y:
        draw->flip_y = JS_ToBool(ctx, value) != 0;
        break;
    case INST_ORIGIN_X:
    case INST_ORIGIN_Y:
        result = sprite_number(ctx, value, magic == INST_ORIGIN_X ? &draw->origin_x :
            &draw->origin_y, -SPRITE_MAX_SCALE, SPRITE_MAX_SCALE, "Sprite.Instance origin");
        break;
    case INST_SCALE_X:
    case INST_SCALE_Y:
        result = sprite_number(ctx, value, magic == INST_SCALE_X ? &draw->scale_x :
            &draw->scale_y, 0.0f, SPRITE_MAX_SCALE, "Sprite.Instance scale");
        break;
    case INST_ROTATION:
        result = sprite_number(ctx, value, &draw->rotation, -SPRITE_MAX_COORD,
            SPRITE_MAX_COORD, "Sprite.Instance.rotation");
        break;
    case INST_COLOR:
        result = sprite_color(ctx, value, &draw->color, "Sprite.Instance.color");
        break;
    case INST_AUTO_UPDATE:
        sprite->auto_update = JS_ToBool(ctx, value) != 0;
        break;
    case INST_REAL_TIME:
        sprite->real_time = JS_ToBool(ctx, value) != 0;
        break;
    case INST_DEBUG:
        sprite->debug = JS_ToBool(ctx, value) != 0;
        break;
    case INST_X:
    case INST_Y:
        result = sprite_number(ctx, value, magic == INST_X ? &draw->x : &draw->y,
            -SPRITE_MAX_COORD, SPRITE_MAX_COORD, "Sprite.Instance position");
        break;
    }
    if (result)
        return JS_EXCEPTION;
    sprite_sync_link(sprite);
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry instance_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("sheet", instance_get, NULL, INST_SHEET),
    JS_CGETSET_MAGIC_DEF("clip", instance_get, NULL, INST_CLIP),
    JS_CGETSET_MAGIC_DEF("frame", instance_get, instance_set, INST_FRAME),
    JS_CGETSET_MAGIC_DEF("position", instance_get, instance_set, INST_POSITION),
    JS_CGETSET_MAGIC_DEF("playing", instance_get, NULL, INST_PLAYING),
    JS_CGETSET_MAGIC_DEF("paused", instance_get, NULL, INST_PAUSED),
    JS_CGETSET_MAGIC_DEF("finished", instance_get, NULL, INST_FINISHED),
    JS_CGETSET_MAGIC_DEF("progress", instance_get, NULL, INST_PROGRESS),
    JS_CGETSET_MAGIC_DEF("cycles", instance_get, NULL, INST_CYCLES),
    JS_CGETSET_MAGIC_DEF("speed", instance_get, instance_set, INST_SPEED),
    JS_CGETSET_MAGIC_DEF("x", instance_get, instance_set, INST_X),
    JS_CGETSET_MAGIC_DEF("y", instance_get, instance_set, INST_Y),
    JS_CGETSET_MAGIC_DEF("flipX", instance_get, instance_set, INST_FLIP_X),
    JS_CGETSET_MAGIC_DEF("flipY", instance_get, instance_set, INST_FLIP_Y),
    JS_CGETSET_MAGIC_DEF("originX", instance_get, instance_set, INST_ORIGIN_X),
    JS_CGETSET_MAGIC_DEF("originY", instance_get, instance_set, INST_ORIGIN_Y),
    JS_CGETSET_MAGIC_DEF("scaleX", instance_get, instance_set, INST_SCALE_X),
    JS_CGETSET_MAGIC_DEF("scaleY", instance_get, instance_set, INST_SCALE_Y),
    JS_CGETSET_MAGIC_DEF("rotation", instance_get, instance_set, INST_ROTATION),
    JS_CGETSET_MAGIC_DEF("color", instance_get, instance_set, INST_COLOR),
    JS_CGETSET_MAGIC_DEF("autoUpdate", instance_get, instance_set, INST_AUTO_UPDATE),
    JS_CGETSET_MAGIC_DEF("realTime", instance_get, instance_set, INST_REAL_TIME),
    JS_CGETSET_MAGIC_DEF("debug", instance_get, instance_set, INST_DEBUG),
    JS_CGETSET_MAGIC_DEF("width", instance_get, NULL, INST_WIDTH),
    JS_CGETSET_MAGIC_DEF("height", instance_get, NULL, INST_HEIGHT),
    JS_CFUNC_DEF("play", 2, instance_play_method),
    JS_CFUNC_DEF("playAsync", 2, instance_play_async),
    JS_CFUNC_MAGIC_DEF("pause", 0, instance_control, CONTROL_PAUSE),
    JS_CFUNC_MAGIC_DEF("resume", 0, instance_control, CONTROL_RESUME),
    JS_CFUNC_MAGIC_DEF("stop", 0, instance_control, CONTROL_STOP),
    JS_CFUNC_DEF("update", 1, instance_update),
    JS_CFUNC_DEF("draw", 2, instance_draw),
    JS_CFUNC_DEF("getBounds", 2, instance_get_bounds),
    JS_CFUNC_DEF("getSlice", 3, instance_get_slice),
    JS_CFUNC_MAGIC_DEF("setOrigin", 2, instance_set_pair, 0),
    JS_CFUNC_MAGIC_DEF("setScale", 2, instance_set_pair, 1),
    JS_CFUNC_MAGIC_DEF("on", 2, instance_on, 0),
    JS_CFUNC_MAGIC_DEF("once", 2, instance_on, 1),
    JS_CFUNC_DEF("off", 2, instance_off),
};

/* ---------------------------------------------------------------------- */
/* Animator                                                               */

static void animator_unbind(JSContext *ctx, AnimatorObject *animator);

static void animator_finalizer(JSRuntime *rt, JSValue value) {
    AnimatorObject *animator = JS_GetOpaque(value, animator_class_id);

    if (!animator)
        return;
    JS_FreeValueRT(rt, animator->instance);
    JS_FreeValueRT(rt, animator->sheet);
    js_free_rt(rt, animator->entries);
    js_free_rt(rt, animator);
}

static void animator_gc_mark(JSRuntime *rt, JSValueConst value, JS_MarkFunc *mark) {
    AnimatorObject *animator = JS_GetOpaque(value, animator_class_id);

    if (!animator)
        return;
    JS_MarkValue(rt, animator->instance, mark);
    JS_MarkValue(rt, animator->sheet, mark);
}

static JSClassDef animator_class = {
    "Animator",
    .finalizer = animator_finalizer,
    .gc_mark = animator_gc_mark,
};

static AnimatorObject *animator_this(JSContext *ctx, JSValueConst value) {
    return JS_GetOpaque2(ctx, value, animator_class_id);
}

/*
 * TileMap sprites have no turned texture rectangle: a clip with a frame
 * turned in the atlas cannot play on an Animator. 0, or a thrown TypeError.
 */
static int animator_check_clip(JSContext *ctx, SheetObject *sheet, int32_t clip,
    const char *name) {
    const AthenaSpriteClip *def = &sheet->sheet.clips[clip];
    uint32_t i;

    for (i = 0; i < def->count; i++) {
        if (sheet->sheet.frames[def->frames[i]].rotated) {
            JS_ThrowTypeError(ctx, "%s: clip '%s' has frames turned in the atlas, which "
                "TileMap sprites cannot show; export them without rotation", name, def->name);
            return -1;
        }
    }
    return 0;
}

/* Makes animated TileMap sprites of `sheet` write their texture rectangles again. */
static void animator_invalidate_sheet(SheetObject *sheet) {
    AnimatorObject *animator;
    uint32_t i;

    for (animator = state.animators; animator; animator = animator->next)
        if (animator->data == sheet)
            for (i = 0; i < animator->count; i++)
                animator->entries[i].shown = ATHENA_SPRITE_NOT_SHOWN;
}

/* Starts `clip` on entries [first, first + count), from a random point when asked. */
static void animator_play_range(AnimatorObject *animator, int32_t clip, uint32_t first,
    uint32_t count, bool restart, bool random_start) {
    const AthenaSpriteSheet *sheet = &animator->data->sheet;
    const AthenaSpriteClip *def = &sheet->clips[clip];
    uint32_t i;

    for (i = first; i < first + count; i++) {
        AthenaSpriteAnim *anim = &animator->entries[i].anim;
        float speed = anim->speed;

        if (!restart && !random_start && anim->clip == clip && anim->playing)
            continue;
        athena_sprite_anim_play(anim, sheet, clip);
        anim->speed = speed;
        anim->pending_frame = false;
        if (random_start) {
            uint32_t position = (uint32_t)(athena_random_float(&animator->rng) * (float)def->count);

            if (position >= def->count)
                position = def->count - 1;
            anim->position = position;
            anim->time = athena_random_float(&animator->rng) * def->durations[position];
            if (def->mode == ATHENA_SPRITE_PINGPONG && def->count > 1 &&
                position > 0 && position < def->count - 1 && athena_random_float(&animator->rng) < 0.5f)
                anim->direction = -1;
            if (position == def->count - 1 && def->count > 1 &&
                def->mode == ATHENA_SPRITE_PINGPONG)
                anim->direction = -1;
        }
        animator->entries[i].shown = ATHENA_SPRITE_NOT_SHOWN;
    }
}

/* Reads { first, count } relative to the animator, defaulting to all entries. */
static int animator_range(JSContext *ctx, AnimatorObject *animator, JSValueConst options,
    uint32_t *first, uint32_t *count, const char *name) {
    bool has_count;
    int found;

    *first = 0;
    if (sprite_option_uint(ctx, options, "first", first, animator->count, name) < 0)
        return -1;
    *count = animator->count - *first;
    found = sprite_option_uint(ctx, options, "count", count, animator->count, name);
    if (found < 0)
        return -1;
    has_count = found > 0;
    if (has_count && *count > animator->count - *first) {
        JS_ThrowRangeError(ctx, "%s: first + count exceeds the animator's %u sprites",
            name, (unsigned)animator->count);
        return -1;
    }
    return 0;
}

/*
 * Sprite.Animator.bind(instance, sheet, clip, { first, count, randomStart,
 * speed, flipX, flipY, seed }): plays `clip` on TileMap sprites
 * [first, first + count) until unbind().
 */
static JSValue animator_bind(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    const char *name = "Sprite.Animator.bind";
    JSValueConst instance = sprite_arg(argc, argv, 0), sheet_value = sprite_arg(argc, argv, 1);
    JSValueConst options = sprite_arg(argc, argv, 3);
    SheetObject *sheet = JS_GetOpaque(sheet_value, sheet_class_id);
    AnimatorObject *animator;
    uint32_t first = 0, count = 0, sprite_count = 0, i;
    bool random_start = false, flip_x = false, flip_y = false, real_time = false, has_count;
    float speed = 1.0f, seed = 0.0f;
    int32_t clip;
    int found;
    JSValue object;

    if (sprite_main(ctx, name))
        return JS_EXCEPTION;
    if (!athena_tilemap_is_instance(instance))
        return JS_ThrowTypeError(ctx, "%s expects a TileMap.Instance", name);
    if (!sheet)
        return JS_ThrowTypeError(ctx, "%s expects a Sprite.Sheet as its second argument", name);
    if (sprite_check_options(ctx, options, name) ||
        sheet_clip_ref(ctx, sheet, sprite_arg(argc, argv, 2), &clip, name) ||
        animator_check_clip(ctx, sheet, clip, name))
        return JS_EXCEPTION;
    if (!athena_tilemap_instance_sprites(ctx, instance, &sprite_count))
        return JS_ThrowTypeError(ctx, "%s: the TileMap.Instance has no usable sprite buffer",
            name);
    if (sprite_option_uint(ctx, options, "first", &first, sprite_count, name) < 0)
        return JS_EXCEPTION;
    count = sprite_count - first;
    found = sprite_option_uint(ctx, options, "count", &count, sprite_count, name);
    if (found < 0)
        return JS_EXCEPTION;
    has_count = found > 0;
    if ((has_count && count > sprite_count - first) || count == 0)
        return JS_ThrowRangeError(ctx, "%s: sprites [first, first + count) must be a "
            "non-empty range of the buffer's %u sprites", name, (unsigned)sprite_count);
    if (sprite_option_bool(ctx, options, "randomStart", &random_start) < 0 ||
        sprite_option_bool(ctx, options, "flipX", &flip_x) < 0 ||
        sprite_option_bool(ctx, options, "flipY", &flip_y) < 0 ||
        sprite_option_bool(ctx, options, "realTime", &real_time) < 0 ||
        sprite_option_number(ctx, options, "speed", &speed, 0.0f, SPRITE_MAX_SPEED, name) < 0)
        return JS_EXCEPTION;
    found = sprite_option_number(ctx, options, "seed", &seed, 0.0f, 4294967295.0f, name);
    if (found < 0)
        return JS_EXCEPTION;

    object = sprite_new_object(ctx, JS_UNDEFINED, animator_class_id);
    if (JS_IsException(object))
        return object;
    animator = js_mallocz(ctx, sizeof(*animator));
    if (!animator) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    animator->entries = js_mallocz(ctx, count * sizeof(*animator->entries));
    if (!animator->entries) {
        js_free(ctx, animator);
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    animator->object = JS_VALUE_GET_PTR(object);
    animator->instance = JS_DupValue(ctx, instance);
    animator->sheet = JS_DupValue(ctx, sheet_value);
    animator->data = sheet;
    animator->first = first;
    animator->count = count;
    animator->speed = speed;
    animator->real_time = real_time;
    /* Its own generator: a seed gives the same start whatever else is bound. */
    athena_random_seed(&animator->rng, found ? (uint64_t)seed : athena_random_entropy());
    for (i = 0; i < count; i++) {
        AthenaSpriteAnimatorEntry *entry = &animator->entries[i];

        athena_sprite_anim_init(&entry->anim);
        entry->anim.speed = speed;
        entry->sprite = first + i;
        entry->shown = ATHENA_SPRITE_NOT_SHOWN;
        entry->flip_x = flip_x;
        entry->flip_y = flip_y;
    }
    JS_SetOpaque(object, animator);
    animator_play_range(animator, clip, 0, count, true, random_start);

    /* Held by the module until unbind(): binding is fire and forget. */
    animator->bound = true;
    animator->next = state.animators;
    if (state.animators)
        state.animators->prev = animator;
    state.animators = animator;
    JS_DupValue(ctx, object);
    sprite_ready(real_time);
    /* The first frames show at once, not one update later. */
    animator_advance(ctx, animator, 0.0f);
    return object;
}

static void animator_unbind(JSContext *ctx, AnimatorObject *animator) {
    if (!animator->bound)
        return;
    if (animator->prev)
        animator->prev->next = animator->next;
    else
        state.animators = animator->next;
    if (animator->next)
        animator->next->prev = animator->prev;
    animator->prev = animator->next = NULL;
    animator->bound = false;
    /* May finalize the animator: the last thing done with it. */
    JS_FreeValue(ctx, sprite_value_of(animator->object));
}

/* animator.play(clip, { first, count, restart, randomStart }) */
static JSValue animator_play(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    const char *name = "Sprite.Animator.play";
    AnimatorObject *animator = animator_this(ctx, this_val);
    JSValueConst options = sprite_arg(argc, argv, 1);
    bool restart = false, random_start = false;
    uint32_t first, count;
    int32_t clip;

    if (!animator || sprite_check_options(ctx, options, name) ||
        sheet_clip_ref(ctx, animator->data, sprite_arg(argc, argv, 0), &clip, name) ||
        animator_check_clip(ctx, animator->data, clip, name) ||
        animator_range(ctx, animator, options, &first, &count, name) ||
        sprite_option_bool(ctx, options, "restart", &restart) < 0 ||
        sprite_option_bool(ctx, options, "randomStart", &random_start) < 0)
        return JS_EXCEPTION;
    animator_play_range(animator, clip, first, count, restart, random_start);
    return JS_DupValue(ctx, this_val);
}

/* animator.setFlip(flipX, flipY, { first, count }) */
static JSValue animator_set_flip(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    const char *name = "Sprite.Animator.setFlip";
    AnimatorObject *animator = animator_this(ctx, this_val);
    bool flip_x, flip_y;
    uint32_t first, count, i;

    if (!animator || sprite_check_options(ctx, sprite_arg(argc, argv, 2), name) ||
        animator_range(ctx, animator, sprite_arg(argc, argv, 2), &first, &count, name))
        return JS_EXCEPTION;
    flip_x = JS_ToBool(ctx, sprite_arg(argc, argv, 0)) != 0;
    flip_y = JS_ToBool(ctx, sprite_arg(argc, argv, 1)) != 0;
    for (i = first; i < first + count; i++) {
        AthenaSpriteAnimatorEntry *entry = &animator->entries[i];

        if (entry->flip_x != flip_x || entry->flip_y != flip_y) {
            entry->flip_x = flip_x;
            entry->flip_y = flip_y;
            entry->shown = ATHENA_SPRITE_NOT_SHOWN;
        }
    }
    return JS_DupValue(ctx, this_val);
}

enum { ANIM_PAUSE, ANIM_RESUME, ANIM_UNBIND };

static JSValue animator_control(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int magic) {
    AnimatorObject *animator = animator_this(ctx, this_val);

    if (!animator)
        return JS_EXCEPTION;
    switch (magic) {
    case ANIM_PAUSE:
        animator->paused = true;
        break;
    case ANIM_RESUME:
        animator->paused = false;
        break;
    default:
        animator_unbind(ctx, animator);
        return JS_UNDEFINED;
    }
    return JS_DupValue(ctx, this_val);
}

/* animator.frameAt(index): the sheet frame sprite `first + index` shows. */
static JSValue animator_frame_at(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    AnimatorObject *animator = animator_this(ctx, this_val);
    uint32_t index;

    if (!animator || sprite_uint(ctx, sprite_arg(argc, argv, 0), &index,
            animator->count - 1, "Sprite.Animator.frameAt index"))
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, (int32_t)athena_sprite_anim_frame(
        &animator->entries[index].anim, &animator->data->sheet));
}

/*
 * animator.isPlaying(index) / isFinished(index): the state of sprite
 * `first + index`, as a game polls it (animators send no events).
 */
static JSValue animator_state_at(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int finished) {
    AnimatorObject *animator = animator_this(ctx, this_val);
    uint32_t index;

    if (!animator || sprite_uint(ctx, sprite_arg(argc, argv, 0), &index,
            animator->count - 1, finished ? "Sprite.Animator.isFinished index" :
            "Sprite.Animator.isPlaying index"))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, finished ? animator->entries[index].anim.finished :
        animator->entries[index].anim.playing);
}

enum { ANIMATOR_INSTANCE, ANIMATOR_SHEET, ANIMATOR_FIRST, ANIMATOR_COUNT,
    ANIMATOR_SPEED, ANIMATOR_PAUSED, ANIMATOR_BOUND, ANIMATOR_FINISHED,
    ANIMATOR_REAL_TIME };

static JSValue animator_get(JSContext *ctx, JSValueConst this_val, int magic) {
    AnimatorObject *animator = animator_this(ctx, this_val);

    if (!animator)
        return JS_EXCEPTION;
    switch (magic) {
    case ANIMATOR_INSTANCE:
        return JS_DupValue(ctx, animator->instance);
    case ANIMATOR_SHEET:
        return JS_DupValue(ctx, animator->sheet);
    case ANIMATOR_FIRST:
        return JS_NewInt64(ctx, animator->first);
    case ANIMATOR_COUNT:
        return JS_NewInt64(ctx, animator->count);
    case ANIMATOR_SPEED:
        return JS_NewFloat64(ctx, animator->speed);
    case ANIMATOR_PAUSED:
        return JS_NewBool(ctx, animator->paused);
    case ANIMATOR_REAL_TIME:
        return JS_NewBool(ctx, animator->real_time);
    case ANIMATOR_FINISHED: {
        /* One pass in C: cheaper than isFinished() per sprite from JavaScript. */
        uint32_t i, finished = 0;

        for (i = 0; i < animator->count; i++)
            finished += animator->entries[i].anim.finished ? 1u : 0u;
        return JS_NewInt64(ctx, finished);
    }
    default:
        return JS_NewBool(ctx, animator->bound);
    }
}

static JSValue animator_set_speed(JSContext *ctx, JSValueConst this_val, JSValueConst value,
    int magic) {
    AnimatorObject *animator = animator_this(ctx, this_val);
    uint32_t i;

    if (!animator || sprite_number(ctx, value, &animator->speed, 0.0f, SPRITE_MAX_SPEED,
            "Sprite.Animator.speed"))
        return JS_EXCEPTION;
    for (i = 0; i < animator->count; i++)
        animator->entries[i].anim.speed = animator->speed;
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry animator_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("instance", animator_get, NULL, ANIMATOR_INSTANCE),
    JS_CGETSET_MAGIC_DEF("sheet", animator_get, NULL, ANIMATOR_SHEET),
    JS_CGETSET_MAGIC_DEF("first", animator_get, NULL, ANIMATOR_FIRST),
    JS_CGETSET_MAGIC_DEF("count", animator_get, NULL, ANIMATOR_COUNT),
    JS_CGETSET_MAGIC_DEF("paused", animator_get, NULL, ANIMATOR_PAUSED),
    JS_CGETSET_MAGIC_DEF("bound", animator_get, NULL, ANIMATOR_BOUND),
    JS_CGETSET_MAGIC_DEF("finishedCount", animator_get, NULL, ANIMATOR_FINISHED),
    JS_CGETSET_MAGIC_DEF("realTime", animator_get, NULL, ANIMATOR_REAL_TIME),
    JS_CGETSET_MAGIC_DEF("speed", animator_get, animator_set_speed, ANIMATOR_SPEED),
    JS_CFUNC_DEF("play", 2, animator_play),
    JS_CFUNC_DEF("setFlip", 3, animator_set_flip),
    JS_CFUNC_MAGIC_DEF("pause", 0, animator_control, ANIM_PAUSE),
    JS_CFUNC_MAGIC_DEF("resume", 0, animator_control, ANIM_RESUME),
    JS_CFUNC_MAGIC_DEF("unbind", 0, animator_control, ANIM_UNBIND),
    JS_CFUNC_DEF("frameAt", 1, animator_frame_at),
    JS_CFUNC_MAGIC_DEF("isPlaying", 1, animator_state_at, 0),
    JS_CFUNC_MAGIC_DEF("isFinished", 1, animator_state_at, 1),
};

static const JSCFunctionListEntry animator_static_funcs[] = {
    JS_CFUNC_DEF("bind", 4, animator_bind),
};

/* ---------------------------------------------------------------------- */
/* Module                                                                 */

/* Sprite.update(dt): advances every playing instance and animator, for games without Loop.run(). */
static JSValue sprite_update_all(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    float dt;

    if (sprite_main(ctx, "Sprite.update") ||
        sprite_number(ctx, sprite_arg(argc, argv, 0), &dt, 0.0f, SPRITE_MAX_COORD,
            "Sprite.update dt"))
        return JS_EXCEPTION;
    /* Without the Loop there is one clock: real-time sprites take the same dt. */
    if (sprite_advance_all(ctx, dt, false) || sprite_advance_all(ctx, dt, true))
        return JS_EXCEPTION;
    return JS_UNDEFINED;
}

/* A Float32Array (read in place) or an array of numbers, as positions. */
typedef struct {
    const float *floats;
    JSValueConst array;
    uint32_t length;
} SpritePositions;

static int sprite_positions(JSContext *ctx, JSValueConst value, SpritePositions *out,
    const char *name) {
    size_t offset = 0, length = 0, element = 0, size = 0;
    JSValue buffer;
    uint8_t *data;

    out->floats = NULL;
    out->array = JS_UNDEFINED;
    out->length = 0;
    if (JS_IsArray(ctx, value)) {
        out->array = value;
        return sprite_length(ctx, value, &out->length);
    }
    if (!JS_IsObject(value)) {
        JS_ThrowTypeError(ctx, "%s positions must be a Float32Array or an array "
            "[x0, y0, x1, y1, ...]", name);
        return -1;
    }
    buffer = JS_GetTypedArrayBuffer(ctx, value, &offset, &length, &element);
    if (JS_IsException(buffer)) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        JS_ThrowTypeError(ctx, "%s positions must be a Float32Array or an array "
            "[x0, y0, x1, y1, ...]", name);
        return -1;
    }
    data = JS_GetArrayBuffer(ctx, &size, buffer);
    JS_FreeValue(ctx, buffer);
    if (!data || element != sizeof(float) || offset + length > size) {
        if (!data)
            JS_FreeValue(ctx, JS_GetException(ctx));
        JS_ThrowTypeError(ctx, "%s positions must be a Float32Array that is not detached",
            name);
        return -1;
    }
    /* A Float32Array view is always 4-byte aligned: float loads are safe. */
    out->floats = (const float *)(data + offset);
    out->length = (uint32_t)(length / sizeof(float));
    return 0;
}

static int sprite_position_at(JSContext *ctx, const SpritePositions *positions,
    uint32_t index, float *out, const char *name) {
    JSValue value;
    float f;
    int result;

    if (positions->floats) {
        f = positions->floats[index];
        /* Checked like any number: NaN and huge values would reach the GS. */
        if (float_bits_finite(f) && f >= -SPRITE_MAX_COORD && f <= SPRITE_MAX_COORD) {
            *out = f;
            return 0;
        }
        JS_ThrowRangeError(ctx, "%s positions[%u] must be a finite coordinate", name,
            (unsigned)index);
        return -1;
    }
    value = JS_GetPropertyUint32(ctx, positions->array, index);
    if (JS_IsException(value))
        return -1;
    result = sprite_number(ctx, value, out, -SPRITE_MAX_COORD, SPRITE_MAX_COORD, name);
    JS_FreeValue(ctx, value);
    return result;
}

/*
 * Sprite.drawAll(instances, positions?, options?): draws every instance in
 * order, in few GS packets: instances of one texture in a row share a packet
 * (128 per chunk) and those outside the camera are skipped. positions place
 * instance i at [stride * i] and [stride * i + 1] (and become its x and y);
 * with options.stride 3 the third value becomes its rotation, which is the
 * layout of Box2D's world.readTransforms(). Without positions each draws at
 * its own x and y. Rotated instances go alone.
 */
static JSValue sprite_draw_all(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    const char *name = "Sprite.drawAll";
    JSValueConst list = sprite_arg(argc, argv, 0), positions_value = sprite_arg(argc, argv, 1);
    JSValueConst options = sprite_arg(argc, argv, 2);
    SpritePositions positions;
    bool has_positions = !JS_IsUndefined(positions_value), any_debug = state.debug;
    uint32_t count, i, stride = 2;
    int result = 0;

    if (sprite_main(ctx, name))
        return JS_EXCEPTION;
    if (!JS_IsArray(ctx, list))
        return JS_ThrowTypeError(ctx, "%s expects an array of Sprite.Instance", name);
    if (!JS_IsUndefined(options)) {
        JSValue value;
        int32_t requested = 0;

        if (!JS_IsObject(options))
            return JS_ThrowTypeError(ctx, "%s options must be an object", name);
        value = JS_GetPropertyStr(ctx, options, "stride");
        if (JS_IsException(value))
            return JS_EXCEPTION;
        if (!JS_IsUndefined(value)) {
            int bad = !JS_IsNumber(value) || JS_ToInt32(ctx, &requested, value) ||
                (requested != 2 && requested != 3);
            JS_FreeValue(ctx, value);
            if (bad)
                return JS_ThrowRangeError(ctx, "%s options.stride must be 2 (x, y) or 3 (x, y, rotation)", name);
            stride = (uint32_t)requested;
        }
    }
    if (sprite_length(ctx, list, &count) ||
        (has_positions && sprite_positions(ctx, positions_value, &positions, name)))
        return JS_EXCEPTION;
    if (has_positions && positions.length / stride < count)
        return JS_ThrowRangeError(ctx, "%s: %u instances need %u values, got %u", name,
            (unsigned)count, (unsigned)count * stride, (unsigned)positions.length);
    athena_sprite_batch_begin(&state.batch);
    for (i = 0; i < count; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, list, i);
        SpriteObject *sprite = JS_GetOpaque(item, instance_class_id);
        AthenaImage *image = NULL;

        if (JS_IsException(item)) {
            result = -1;
            break;
        }
        /* The array holds the instance: it outlives this iteration. */
        JS_FreeValue(ctx, item);
        if (!sprite) {
            JS_ThrowTypeError(ctx, "%s: element %u is not a Sprite.Instance", name,
                (unsigned)i);
            result = -1;
            break;
        }
        if ((has_positions &&
                (sprite_position_at(ctx, &positions, stride * i, &sprite->draw.x, name) ||
                sprite_position_at(ctx, &positions, stride * i + 1, &sprite->draw.y, name) ||
                (stride == 3 &&
                    sprite_position_at(ctx, &positions, stride * i + 2, &sprite->draw.rotation, name)))) ||
            instance_image(ctx, sprite, &image, name)) {
            result = -1;
            break;
        }
        if (image)
            athena_sprite_batch_add(&state.batch, image, &sprite->data->sheet,
                athena_sprite_anim_frame(&sprite->anim, &sprite->data->sheet), &sprite->draw);
        any_debug |= sprite->debug;
    }
    /* What was queued before an error is still drawn, in order. */
    athena_sprite_batch_end(&state.batch);
    /* Overlays over every sprite of the call, as the list is read again. */
    for (i = 0; any_debug && !result && i < count; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, list, i);
        SpriteObject *sprite = JS_GetOpaque(item, instance_class_id);

        JS_FreeValue(ctx, item);
        if (sprite && (sprite->debug || state.debug))
            athena_sprite_draw_debug(&sprite->data->sheet,
                athena_sprite_anim_frame(&sprite->anim, &sprite->data->sheet), &sprite->draw);
    }
    state.drawn = state.batch.drawn;
    state.culled = state.batch.culled;
    return result ? JS_EXCEPTION : JS_UNDEFINED;
}

/* Sprite.getStats(): { playing, animators, animatedSprites, drawn, culled }. */
static JSValue sprite_get_stats(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    SpriteObject *sprite;
    AnimatorObject *animator;
    int64_t playing = 0, animators = 0, entries = 0;
    JSValue object;

    for (sprite = state.playing; sprite; sprite = sprite->next)
        playing++;
    for (animator = state.animators; animator; animator = animator->next) {
        animators++;
        entries += animator->count;
    }
    object = JS_NewObject(ctx);
    if (JS_IsException(object))
        return object;
    JS_SetPropertyStr(ctx, object, "playing", JS_NewInt64(ctx, playing));
    JS_SetPropertyStr(ctx, object, "animators", JS_NewInt64(ctx, animators));
    JS_SetPropertyStr(ctx, object, "animatedSprites", JS_NewInt64(ctx, entries));
    JS_SetPropertyStr(ctx, object, "drawn", JS_NewInt64(ctx, state.drawn));
    JS_SetPropertyStr(ctx, object, "culled", JS_NewInt64(ctx, state.culled));
    return object;
}

/* Sprite.setDebug(on): the debug overlay on every instance drawn; returns the previous state. */
static JSValue sprite_set_debug(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    bool previous = state.debug;

    state.debug = JS_ToBool(ctx, sprite_arg(argc, argv, 0)) != 0;
    return JS_NewBool(ctx, previous);
}

static const JSCFunctionListEntry sprite_funcs[] = {
    JS_CFUNC_DEF("setDebug", 1, sprite_set_debug),
    JS_CFUNC_DEF("update", 1, sprite_update_all),
    JS_CFUNC_DEF("getStats", 0, sprite_get_stats),
    JS_CFUNC_DEF("drawAll", 3, sprite_draw_all),
};

static JSValue sprite_class(JSContext *ctx, JSClassID *class_id, const JSClassDef *def,
    JSCFunction *ctor, const char *name, const JSCFunctionListEntry *proto_funcs,
    int proto_count, const JSCFunctionListEntry *static_funcs, int static_count) {
    JSValue proto, constructor;

    if (athena_register_class(ctx, class_id, def) < 0)
        return JS_EXCEPTION;
    proto = JS_NewObject(ctx);
    if (JS_IsException(proto))
        return proto;
    JS_SetPropertyFunctionList(ctx, proto, proto_funcs, proto_count);
    constructor = JS_NewCFunction2(ctx, ctor, name, 2, JS_CFUNC_constructor, 0);
    if (JS_IsException(constructor)) {
        JS_FreeValue(ctx, proto);
        return constructor;
    }
    JS_SetConstructor(ctx, constructor, proto);
    JS_SetClassProto(ctx, *class_id, proto);
    if (static_funcs)
        JS_SetPropertyFunctionList(ctx, constructor, static_funcs, static_count);
    return constructor;
}

/* Animators are made by Animator.bind() only. */
static JSValue animator_ctor(JSContext *ctx, JSValueConst new_target, int argc,
    JSValueConst *argv) {
    return JS_ThrowTypeError(ctx, "use Sprite.Animator.bind() to create an Animator");
}

static int sprite_module_init(JSContext *ctx, JSModuleDef *module) {
    JSValue sheet, instance, animator;

    sheet = sprite_class(ctx, &sheet_class_id, &sheet_class, sheet_ctor, "Sheet",
        sheet_proto_funcs, countof(sheet_proto_funcs), sheet_static_funcs,
        countof(sheet_static_funcs));
    if (JS_IsException(sheet) || JS_SetModuleExport(ctx, module, "Sheet", sheet) < 0)
        return -1;
    instance = sprite_class(ctx, &instance_class_id, &instance_class, instance_ctor,
        "Instance", instance_proto_funcs, countof(instance_proto_funcs), NULL, 0);
    if (JS_IsException(instance) || JS_SetModuleExport(ctx, module, "Instance", instance) < 0)
        return -1;
    animator = sprite_class(ctx, &animator_class_id, &animator_class, animator_ctor,
        "Animator", animator_proto_funcs, countof(animator_proto_funcs),
        animator_static_funcs, countof(animator_static_funcs));
    if (JS_IsException(animator) || JS_SetModuleExport(ctx, module, "Animator", animator) < 0)
        return -1;
    if (!state.ctx) {
        state.ctx = ctx;
        state.system_id = 0;
        state.playing = NULL;
        state.animators = NULL;
        state.pending = NULL;
        state.pending_count = state.pending_capacity = 0;
        state.realtime_id = 0;
        state.depth = 0;
        state.debug = false;
        state.drawn = state.culled = 0;
    }
    return JS_SetModuleExportList(ctx, module, sprite_funcs, countof(sprite_funcs));
}

JSModuleDef *athena_sprite_js_init(JSContext *ctx) {
    JSModuleDef *module = athena_push_module(ctx, sprite_module_init, sprite_funcs,
        countof(sprite_funcs), "Sprite");

    if (module) {
        JS_AddModuleExport(ctx, module, "Sheet");
        JS_AddModuleExport(ctx, module, "Instance");
        JS_AddModuleExport(ctx, module, "Animator");
    }
    return module;
}

void athena_sprite_js_cleanup(JSContext *ctx) {
    uint32_t i;

    if (ctx != state.ctx)
        return;
    if (state.system_id > 0)
        athena_loop_system_remove(state.system_id);
    state.system_id = 0;
    if (state.realtime_id > 0)
        athena_loop_system_remove(state.realtime_id);
    state.realtime_id = 0;
    /* Instances stay alive until the runtime frees them: forget the list only. */
    while (state.playing)
        sprite_unlink(state.playing);
    /*
     * No JavaScript runs any more: pending playAsync() promises are dropped,
     * not settled, and the instances they pinned are let go (which may
     * finalize them: the resolve function is taken out first).
     */
    while (sprite_pinned_count > 0) {
        SpriteObject *sprite = sprite_pinned[--sprite_pinned_count];
        JSValue settle = sprite->settle;

        sprite->settle = JS_UNDEFINED;
        JS_FreeValue(ctx, settle);
        JS_FreeValue(ctx, sprite_value_of(sprite->object));
    }
    free(sprite_pinned);
    sprite_pinned = NULL;
    sprite_pinned_capacity = 0;
    /* Bound animators are held by the module: let them go. */
    while (state.animators)
        animator_unbind(ctx, state.animators);
    for (i = 0; i < state.pending_count; i++)
        JS_FreeValue(ctx, state.pending[i].sprite);
    free(state.pending);
    state.pending = NULL;
    state.pending_count = state.pending_capacity = 0;
    state.ctx = NULL;
}
