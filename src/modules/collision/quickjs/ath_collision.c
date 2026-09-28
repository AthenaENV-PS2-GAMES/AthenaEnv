#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ath_env.h>

#include <athena/collision.h>
#include <athena/loop.h>
#include "ath_collision.h"

/*
 * Collision.World wraps an AthenaCollisionWorld; Collision.Body names one of
 * its bodies. The world holds a reference to each body object (its C body's
 * `user` pointer) and each body holds its world, so a script can keep
 * either; the cycle is visible to the GC through gc_mark. Removing a body
 * drops both references and leaves the object detached.
 *
 * Worlds with autoStep are stepped by a native Loop system (UPDATE phase,
 * scaled delta), added on first use: scripts without collision worlds run
 * exactly as before. Contacts are collected during the C step and only then
 * dispatched to onContact, so a callback that removes bodies never touches
 * the world while it moves them.
 */

#define COLLISION_SYSTEM_NAME "collision"
/* After the cameras (-1000), before Sprite (-500): animations see the moved bodies. */
#define COLLISION_SYSTEM_PRIORITY (-600)
#define MAX_COORD ATHENA_COLLISION_MAX_COORD
#define MAX_RATE 1.0e4f
#define MAX_TILE_ID 65535u
#define MAX_GRID_SIDE ATHENA_COLLISION_MAX_GRID_SIDE
/* Most pairs pairs() returns: 512 KB of ids, and as many JavaScript arrays. */
#define MAX_PAIRS 65536u
/* Auto-step worlds held on the stack by the Loop system; more are allocated. */
#define HELD_ON_STACK 8

enum { EVENT_CONTACT, EVENT_ENTER, EVENT_EXIT };

/*
 * Property names of the objects built each frame (move results, contacts,
 * raycast hits), as atoms: setting a property by a C string looks the atom
 * up in the runtime's table on every call.
 */
#define COL_ATOMS(X) \
    X(x) X(y) X(w) X(h) X(dx) X(dy) X(body) X(tile) X(column) X(row) X(normalX) \
    X(normalY) X(fraction) X(distance) X(contacts) X(length) X(onGround) \
    X(hitCeiling) X(hitWall)
#define COL_ATOM_ENUM(name) ATOM_##name,
#define COL_ATOM_NAME(name) #name,

enum { COL_ATOMS(COL_ATOM_ENUM) ATOM_COUNT };

static const char *const col_atom_names[ATOM_COUNT] = { COL_ATOMS(COL_ATOM_NAME) };

/* An event of a step, dispatched after it: a contact, or a sensor's enter or exit. */
typedef struct {
    uint8_t type;
    /* The body of a contact, or the sensor. */
    AthenaBodyId body;
    /* For sensor events, only `other` is used. */
    AthenaCollisionContact contact;
} PendingContact;

typedef struct WorldObject {
    AthenaCollisionWorld *world;
    /* The JSObject of the world, for `this` in callbacks and the Loop system. */
    void *object;
    JSValue on_contact, on_enter, on_exit;
    PendingContact *pending;
    uint32_t pending_count, pending_capacity;
    /* Query results. */
    AthenaBodyId *ids;
    uint32_t id_capacity;
    AthenaBodyPair *pairs;
    uint32_t pair_capacity;
    /* step() or its callbacks are running. */
    bool stepping;
    bool auto_step;
    /* In the module's auto-step list, which holds no reference. */
    bool linked;
    struct WorldObject *prev, *next;
} WorldObject;

typedef struct {
    /* The world, held; JS_UNDEFINED once the body was removed. */
    JSValue world;
    AthenaBodyId id;
} BodyObject;

static JSClassID world_class_id;
static JSClassID body_class_id;

/*
 * Module state, owned by the context that initialized the module first:
 * the main script. Worlds work in any context, but only the main one's
 * are stepped by the Loop.
 */
static struct {
    JSContext *ctx;
    int system_id;
    WorldObject *auto_worlds;
    /* The atoms belong to the main script's runtime; Workers have their own. */
    JSRuntime *rt;
    JSAtom atoms[ATOM_COUNT];
} state;

/* ---------------------------------------------------------------------- */
/* Values                                                                 */

static bool float_bits_finite(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

/*
 * Errors name the value as "name" or "name.key": both are constant strings,
 * joined only when an error is thrown. Getters and setters run every frame,
 * and formatting a label on each call is measurable on the EE (it made
 * Sprite's draw() 60% slower).
 */
#define COL_LABEL "%s%s%s"
#define COL_LABEL_ARGS(name, key) (name), (key) ? "." : "", (key) ? (key) : ""

/*
 * A number in [min, max] into a float, or a thrown TypeError/RangeError
 * naming `name` (and `key`, when not NULL). The EE has no infinities nor
 * NaN: float32 values are checked by their bits and other numbers as
 * doubles (soft-float, IEEE). `*out` is only written on success: a rejected
 * setter changes nothing.
 */
static int col_number_at(JSContext *ctx, JSValueConst value, float *out, float min,
    float max, const char *name, const char *key) {
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
            JS_ThrowTypeError(ctx, COL_LABEL " must be a number", COL_LABEL_ARGS(name, key));
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
    JS_ThrowRangeError(ctx, COL_LABEL " must be a finite number from %g to %g",
        COL_LABEL_ARGS(name, key), (double)min, (double)max);
    return -1;
}

static int col_number(JSContext *ctx, JSValueConst value, float *out, float min,
    float max, const char *name) {
    return col_number_at(ctx, value, out, min, max, name, NULL);
}

/* An integer in [min, max]. */
static int col_int_at(JSContext *ctx, JSValueConst value, int32_t *out, int32_t min,
    int32_t max, const char *name, const char *key) {
    double number;

    if (JS_VALUE_GET_TAG(value) == JS_TAG_INT) {
        int32_t i = JS_VALUE_GET_INT(value);

        if (i >= min && i <= max) {
            *out = i;
            return 0;
        }
    } else {
        if (!JS_IsNumber(value)) {
            JS_ThrowTypeError(ctx, COL_LABEL " must be a number", COL_LABEL_ARGS(name, key));
            return -1;
        }
        if (JS_ToFloat64(ctx, &number, value))
            return -1;
        if (number >= (double)min && number <= (double)max && number == floor(number)) {
            *out = (int32_t)number;
            return 0;
        }
    }
    JS_ThrowRangeError(ctx, COL_LABEL " must be an integer from %d to %d",
        COL_LABEL_ARGS(name, key), (int)min, (int)max);
    return -1;
}

static int col_int(JSContext *ctx, JSValueConst value, int32_t *out, int32_t min,
    int32_t max, const char *name) {
    return col_int_at(ctx, value, out, min, max, name, NULL);
}

/* A layer or mask: any number, as 32 bits (like the result of | and <<). */
static int col_bits_at(JSContext *ctx, JSValueConst value, uint32_t *out, const char *name,
    const char *key) {
    int32_t bits;

    if (!JS_IsNumber(value)) {
        JS_ThrowTypeError(ctx, COL_LABEL " must be a number (bit flags)",
            COL_LABEL_ARGS(name, key));
        return -1;
    }
    if (JS_ToInt32(ctx, &bits, value))
        return -1;
    *out = (uint32_t)bits;
    return 0;
}

static int col_bits(JSContext *ctx, JSValueConst value, uint32_t *out, const char *name) {
    return col_bits_at(ctx, value, out, name, NULL);
}

static JSValueConst col_arg(int argc, JSValueConst *argv, int index) {
    return index < argc ? argv[index] : JS_UNDEFINED;
}

/* Optional property: 0 when absent or undefined, 1 when read, -1 on error. */
static int col_option(JSContext *ctx, JSValueConst options, const char *key, JSValue *out) {
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

static int col_check_options(JSContext *ctx, JSValueConst options, bool required,
    const char *name) {
    if ((!required && JS_IsUndefined(options)) || (JS_IsObject(options) &&
            !JS_IsFunction(ctx, options) && !JS_IsArray(ctx, options)))
        return 0;
    JS_ThrowTypeError(ctx, "%s expects an options object", name);
    return -1;
}

/* A numeric option: 0 when absent (untouched), 1 when read, -1 on error. */
static int col_option_number(JSContext *ctx, JSValueConst options, const char *key,
    float *out, float min, float max, const char *name) {
    JSValue value;
    int found = col_option(ctx, options, key, &value), result;

    if (found <= 0)
        return found;
    result = col_number_at(ctx, value, out, min, max, name, key);
    JS_FreeValue(ctx, value);
    return result ? -1 : 1;
}

static int col_option_int(JSContext *ctx, JSValueConst options, const char *key,
    int32_t *out, int32_t min, int32_t max, const char *name) {
    JSValue value;
    int found = col_option(ctx, options, key, &value), result;

    if (found <= 0)
        return found;
    result = col_int_at(ctx, value, out, min, max, name, key);
    JS_FreeValue(ctx, value);
    return result ? -1 : 1;
}

static int col_option_bits(JSContext *ctx, JSValueConst options, const char *key,
    uint32_t *out, const char *name) {
    JSValue value;
    int found = col_option(ctx, options, key, &value), result;

    if (found <= 0)
        return found;
    result = col_bits_at(ctx, value, out, name, key);
    JS_FreeValue(ctx, value);
    return result ? -1 : 1;
}

static int col_option_bool(JSContext *ctx, JSValueConst options, const char *key, bool *out) {
    JSValue value;
    int found = col_option(ctx, options, key, &value);

    if (found <= 0)
        return found;
    *out = JS_ToBool(ctx, value) != 0;
    JS_FreeValue(ctx, value);
    return 1;
}

static JSValue col_get(JSContext *ctx, JSValueConst object, int atom);

/* A required numeric property of an object, by its cached name. */
static int col_field(JSContext *ctx, JSValueConst object, int atom, float *out,
    float min, float max, const char *name) {
    JSValue value = col_get(ctx, object, atom);
    int result;

    if (JS_IsException(value))
        return -1;
    result = col_number_at(ctx, value, out, min, max, name, col_atom_names[atom]);
    JS_FreeValue(ctx, value);
    return result;
}

/* [x, y] or { x, y }. */
static int col_pair(JSContext *ctx, JSValueConst value, float *a, float *b, float min,
    float max, const char *name) {
    float x = 0.0f, y = 0.0f;
    int result;

    if (JS_IsArray(ctx, value)) {
        JSValue vx = JS_GetPropertyUint32(ctx, value, 0);
        JSValue vy = JS_GetPropertyUint32(ctx, value, 1);

        result = col_number(ctx, vx, &x, min, max, name) ||
            col_number(ctx, vy, &y, min, max, name) ? -1 : 0;
        JS_FreeValue(ctx, vx);
        JS_FreeValue(ctx, vy);
    } else if (JS_IsObject(value)) {
        result = col_field(ctx, value, ATOM_x, &x, min, max, name) ||
            col_field(ctx, value, ATOM_y, &y, min, max, name) ? -1 : 0;
    } else {
        JS_ThrowTypeError(ctx, "%s must be [x, y] or { x, y }", name);
        return -1;
    }
    if (!result) {
        *a = x;
        *b = y;
    }
    return result;
}

static int col_length(JSContext *ctx, JSValueConst array, uint32_t *length) {
    JSValue value = JS_GetPropertyStr(ctx, array, "length");
    int result = JS_IsException(value) ? -1 : JS_ToUint32(ctx, length, value);

    JS_FreeValue(ctx, value);
    return result ? -1 : 0;
}

static int col_set(JSContext *ctx, JSValue object, const char *key, JSValue value) {
    return JS_SetPropertyStr(ctx, object, key, value) < 0 ? -1 : 0;
}

/* The atom of a name, as a new reference: the cached one on the main runtime. */
static JSAtom col_atom(JSContext *ctx, int atom) {
    if (state.rt == JS_GetRuntime(ctx))
        return JS_DupAtom(ctx, state.atoms[atom]);
    return JS_NewAtom(ctx, col_atom_names[atom]);
}

/* col_set() by a cached name. Takes `value`. */
static int col_put(JSContext *ctx, JSValue object, int atom, JSValue value) {
    JSAtom name;
    int result;

    if (state.rt == JS_GetRuntime(ctx))
        return JS_SetProperty(ctx, object, state.atoms[atom], value) < 0 ? -1 : 0;
    name = JS_NewAtom(ctx, col_atom_names[atom]);
    if (name == JS_ATOM_NULL) {
        JS_FreeValue(ctx, value);
        return -1;
    }
    result = JS_SetProperty(ctx, object, name, value);
    JS_FreeAtom(ctx, name);
    return result < 0 ? -1 : 0;
}

static JSValue col_get(JSContext *ctx, JSValueConst object, int atom) {
    JSAtom name = col_atom(ctx, atom);
    JSValue value;

    if (name == JS_ATOM_NULL)
        return JS_EXCEPTION;
    value = JS_GetProperty(ctx, object, name);
    JS_FreeAtom(ctx, name);
    return value;
}

static JSValue col_object_value(void *object) {
    return JS_MKPTR(JS_TAG_OBJECT, object);
}

static const char *const body_type_names[] = { "static", "kinematic", "dynamic" };

static int col_body_type(JSContext *ctx, JSValueConst value, AthenaBodyType *out,
    const char *name) {
    const char *text = JS_IsString(value) ? JS_ToCString(ctx, value) : NULL;
    int i;

    if (!text) {
        if (!JS_IsString(value))
            JS_ThrowTypeError(ctx, "%s must be \"static\", \"kinematic\" or \"dynamic\"", name);
        return -1;
    }
    for (i = 0; i < 3; i++) {
        if (strcmp(text, body_type_names[i]) == 0) {
            JS_FreeCString(ctx, text);
            *out = (AthenaBodyType)i;
            return 0;
        }
    }
    JS_FreeCString(ctx, text);
    JS_ThrowTypeError(ctx, "%s must be \"static\", \"kinematic\" or \"dynamic\"", name);
    return -1;
}

static bool col_main(JSContext *ctx) {
    return ctx == state.ctx;
}

/* ---------------------------------------------------------------------- */
/* Loop system                                                            */

static int collision_system(void *opaque, AthenaLoopPhase phase, float value);

static void collision_ready(void) {
    AthenaLoopSystemDesc desc = {
        .name = COLLISION_SYSTEM_NAME,
        .priority = COLLISION_SYSTEM_PRIORITY,
        .phases = ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_UPDATE),
        .real_time = false,
        .func = collision_system,
    };
    int id;

    if (state.system_id > 0)
        return;
    /* Without the system, world.step() still works. */
    id = athena_loop_system_add(&desc);
    if (id > 0)
        state.system_id = id;
}

static void world_unlink(WorldObject *wo) {
    if (!wo->linked)
        return;
    if (wo->prev)
        wo->prev->next = wo->next;
    else
        state.auto_worlds = wo->next;
    if (wo->next)
        wo->next->prev = wo->prev;
    wo->prev = wo->next = NULL;
    wo->linked = false;
}

static void world_link(WorldObject *wo) {
    if (wo->linked)
        return;
    collision_ready();
    wo->prev = NULL;
    wo->next = state.auto_worlds;
    if (state.auto_worlds)
        state.auto_worlds->prev = wo;
    state.auto_worlds = wo;
    wo->linked = true;
}

/* ---------------------------------------------------------------------- */
/* Bodies as values                                                       */

/* The body object of an id, as a new reference, or null. */
static JSValue body_value(JSContext *ctx, WorldObject *wo, AthenaBodyId id) {
    AthenaBody *body = id ? athena_collision_get(wo->world, id) : NULL;

    return body ? JS_DupValue(ctx, col_object_value(body->user)) : JS_NULL;
}

static JSValue contact_object(JSContext *ctx, WorldObject *wo,
    const AthenaCollisionContact *contact) {
    JSValue object = JS_NewObject(ctx);

    if (JS_IsException(object))
        return object;
    if (col_put(ctx, object, ATOM_body, body_value(ctx, wo, contact->other)) ||
        col_put(ctx, object, ATOM_tile, JS_NewInt32(ctx, contact->tile)) ||
        col_put(ctx, object, ATOM_column, JS_NewInt32(ctx, contact->column)) ||
        col_put(ctx, object, ATOM_row, JS_NewInt32(ctx, contact->row)) ||
        col_put(ctx, object, ATOM_normalX, JS_NewFloat32(ctx, contact->normal_x)) ||
        col_put(ctx, object, ATOM_normalY, JS_NewFloat32(ctx, contact->normal_y))) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    return object;
}

/* The C body of a Body, or NULL with an exception (not one, or removed). */
static AthenaBody *body_resolve(JSContext *ctx, JSValueConst value, WorldObject **world,
    const char *name) {
    BodyObject *bo = JS_GetOpaque(value, body_class_id);
    WorldObject *wo;

    if (!bo) {
        JS_ThrowTypeError(ctx, "%s expects a Collision.Body", name);
        return NULL;
    }
    if (JS_IsUndefined(bo->world)) {
        JS_ThrowTypeError(ctx, "%s: the body was removed from its world", name);
        return NULL;
    }
    wo = JS_GetOpaque(bo->world, world_class_id);
    if (world)
        *world = wo;
    return athena_collision_get(wo->world, bo->id);
}

/* A body of this world, or NULL with an exception. */
static AthenaBody *body_of_world(JSContext *ctx, WorldObject *wo, JSValueConst value,
    AthenaBodyId *id, const char *name) {
    WorldObject *owner;
    AthenaBody *body = body_resolve(ctx, value, &owner, name);

    if (!body)
        return NULL;
    if (owner != wo) {
        JS_ThrowTypeError(ctx, "%s: the body belongs to another world", name);
        return NULL;
    }
    *id = ((BodyObject *)JS_GetOpaque(value, body_class_id))->id;
    return body;
}

/* ---------------------------------------------------------------------- */
/* Stepping and contacts                                                  */

static PendingContact *world_event(WorldObject *wo, uint8_t type, AthenaBodyId body) {
    PendingContact *event;

    if (wo->pending_count == wo->pending_capacity) {
        uint32_t capacity = wo->pending_capacity ? wo->pending_capacity * 2u : 16u;
        PendingContact *grown = realloc(wo->pending, capacity * sizeof(*grown));

        /* Out of memory: the event is lost, the step goes on. */
        if (!grown)
            return NULL;
        wo->pending = grown;
        wo->pending_capacity = capacity;
    }
    event = &wo->pending[wo->pending_count++];
    memset(event, 0, sizeof(*event));
    event->type = type;
    event->body = body;
    return event;
}

static void world_contact(void *opaque, AthenaBodyId body,
    const AthenaCollisionContact *contact) {
    PendingContact *event = world_event(opaque, EVENT_CONTACT, body);

    if (event)
        event->contact = *contact;
}

static void world_sensor(void *opaque, AthenaBodyId sensor, AthenaBodyId other,
    bool entered) {
    WorldObject *wo = opaque;
    PendingContact *event;

    /* Only the events someone listens to. */
    if (JS_IsUndefined(entered ? wo->on_enter : wo->on_exit))
        return;
    event = world_event(wo, entered ? EVENT_ENTER : EVENT_EXIT, sensor);
    if (event)
        event->contact.other = other;
}

/* The C callbacks are set only while a JavaScript one is: no work otherwise. */
static void world_sync_funcs(WorldObject *wo) {
    athena_collision_set_contact_func(wo->world,
        JS_IsUndefined(wo->on_contact) ? NULL : world_contact, wo);
    athena_collision_set_sensor_func(wo->world,
        JS_IsUndefined(wo->on_enter) && JS_IsUndefined(wo->on_exit) ? NULL : world_sensor, wo);
}

/*
 * Calls onContact(body, contact), onEnter(sensor, other) and
 * onExit(sensor, other) for the events of the last step, in order.
 */
static int world_dispatch(JSContext *ctx, WorldObject *wo) {
    JSValue this_val = col_object_value(wo->object);
    uint32_t i;
    int result = 0;

    for (i = 0; i < wo->pending_count && !result; i++) {
        PendingContact pending = wo->pending[i];
        JSValueConst handler = pending.type == EVENT_CONTACT ? wo->on_contact :
            pending.type == EVENT_ENTER ? wo->on_enter : wo->on_exit;
        JSValue args[2], func, ret;

        if (JS_IsUndefined(handler))
            continue;
        args[0] = body_value(ctx, wo, pending.body);
        if (JS_IsNull(args[0]))
            continue;
        args[1] = pending.type == EVENT_CONTACT ? contact_object(ctx, wo, &pending.contact) :
            body_value(ctx, wo, pending.contact.other);
        if (JS_IsException(args[1])) {
            JS_FreeValue(ctx, args[0]);
            result = -1;
            break;
        }
        /* The other body of a sensor event was removed by an earlier callback. */
        if (JS_IsNull(args[1]) && pending.type != EVENT_CONTACT) {
            JS_FreeValue(ctx, args[0]);
            continue;
        }
        func = JS_DupValue(ctx, handler);
        ret = JS_Call(ctx, func, this_val, 2, (JSValueConst *)args);
        JS_FreeValue(ctx, func);
        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, args[1]);
        if (JS_IsException(ret))
            result = -1;
        JS_FreeValue(ctx, ret);
    }
    wo->pending_count = 0;
    return result;
}

static int world_step(JSContext *ctx, WorldObject *wo, float dt) {
    int result;

    if (wo->stepping) {
        JS_ThrowInternalError(ctx,
            "Collision.World: step() cannot run inside onContact, onEnter or onExit");
        return -1;
    }
    wo->stepping = true;
    wo->pending_count = 0;
    athena_collision_step(wo->world, dt);
    result = world_dispatch(ctx, wo);
    wo->stepping = false;
    return result;
}

/*
 * Steps every auto-step world of the main context. The list holds no
 * references: each world is held while it steps, since a callback may drop
 * the last reference to it or to another world of the list.
 */
static int collision_system(void *opaque, AthenaLoopPhase phase, float value) {
    JSContext *ctx = state.ctx;
    WorldObject *wo;
    /* Games have a world or two: no allocation each frame. */
    JSValue stack[HELD_ON_STACK], *held = stack;
    uint32_t count = 0, i;
    int result = 0;

    (void)opaque;
    if (!ctx || phase != ATHENA_LOOP_UPDATE || !state.auto_worlds)
        return 0;
    for (wo = state.auto_worlds; wo; wo = wo->next)
        count++;
    if (count > HELD_ON_STACK) {
        held = malloc(count * sizeof(*held));
        if (!held) {
            JS_ThrowOutOfMemory(ctx);
            return -1;
        }
    }
    i = 0;
    for (wo = state.auto_worlds; wo; wo = wo->next)
        held[i++] = JS_DupValue(ctx, col_object_value(wo->object));
    for (i = 0; i < count; i++) {
        wo = JS_GetOpaque(held[i], world_class_id);
        if (!result && wo && wo->auto_step && !wo->stepping)
            result = world_step(ctx, wo, value);
    }
    for (i = 0; i < count; i++)
        JS_FreeValue(ctx, held[i]);
    if (held != stack)
        free(held);
    return result;
}

/* ---------------------------------------------------------------------- */
/* World                                                                  */

static void world_finalizer(JSRuntime *rt, JSValue value) {
    WorldObject *wo = JS_GetOpaque(value, world_class_id);
    uint32_t i, capacity;

    if (!wo)
        return;
    world_unlink(wo);
    capacity = athena_collision_body_capacity(wo->world);
    for (i = 0; i < capacity; i++) {
        AthenaBody *body = athena_collision_slot(wo->world, i);

        if (body && body->user)
            JS_FreeValueRT(rt, col_object_value(body->user));
    }
    JS_FreeValueRT(rt, wo->on_contact);
    JS_FreeValueRT(rt, wo->on_enter);
    JS_FreeValueRT(rt, wo->on_exit);
    athena_collision_world_destroy(wo->world);
    free(wo->pending);
    js_free_rt(rt, wo->ids);
    js_free_rt(rt, wo->pairs);
    js_free_rt(rt, wo);
}

static void world_gc_mark(JSRuntime *rt, JSValueConst value, JS_MarkFunc *mark) {
    WorldObject *wo = JS_GetOpaque(value, world_class_id);
    uint32_t i, capacity;

    if (!wo)
        return;
    capacity = athena_collision_body_capacity(wo->world);
    for (i = 0; i < capacity; i++) {
        AthenaBody *body = athena_collision_slot(wo->world, i);

        if (body && body->user)
            JS_MarkValue(rt, col_object_value(body->user), mark);
    }
    JS_MarkValue(rt, wo->on_contact, mark);
    JS_MarkValue(rt, wo->on_enter, mark);
    JS_MarkValue(rt, wo->on_exit, mark);
}

static JSClassDef world_class = {
    "World",
    .finalizer = world_finalizer,
    .gc_mark = world_gc_mark,
};

static WorldObject *world_this(JSContext *ctx, JSValueConst this_val) {
    return JS_GetOpaque2(ctx, this_val, world_class_id);
}

static JSValue col_new_object(JSContext *ctx, JSValueConst new_target, JSClassID class_id) {
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

static JSValue world_ctor(JSContext *ctx, JSValueConst new_target, int argc,
    JSValueConst *argv) {
    const char *name = "Collision.World";
    JSValueConst options = col_arg(argc, argv, 0);
    AthenaCollisionWorldDesc desc;
    bool auto_step = col_main(ctx);
    JSValue gravity, object;
    WorldObject *wo;
    int found;

    if (JS_IsUndefined(new_target))
        return JS_ThrowTypeError(ctx, "Collision.World must be called with new");
    if (col_check_options(ctx, options, false, name))
        return JS_EXCEPTION;
    athena_collision_world_desc_init(&desc);
    if (col_option_number(ctx, options, "cellSize", &desc.cell_size, 1.0f, MAX_COORD, name) < 0)
        return JS_EXCEPTION;
    found = col_option(ctx, options, "gravity", &gravity);
    if (found < 0)
        return JS_EXCEPTION;
    if (found) {
        int failed = col_pair(ctx, gravity, &desc.gravity_x, &desc.gravity_y, -MAX_COORD,
            MAX_COORD, "Collision.World gravity");

        JS_FreeValue(ctx, gravity);
        if (failed)
            return JS_EXCEPTION;
    }
    if (col_option_bool(ctx, options, "autoStep", &auto_step) < 0)
        return JS_EXCEPTION;
    if (auto_step && !col_main(ctx))
        return JS_ThrowInternalError(ctx, "%s: autoStep is only available on the main script",
            name);

    object = col_new_object(ctx, new_target, world_class_id);
    if (JS_IsException(object))
        return object;
    wo = js_mallocz(ctx, sizeof(*wo));
    if (!wo) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    wo->world = athena_collision_world_create(&desc);
    if (!wo->world) {
        js_free(ctx, wo);
        JS_FreeValue(ctx, object);
        return JS_ThrowOutOfMemory(ctx);
    }
    wo->on_contact = wo->on_enter = wo->on_exit = JS_UNDEFINED;
    wo->object = JS_VALUE_GET_PTR(object);
    wo->auto_step = auto_step;
    JS_SetOpaque(object, wo);
    if (auto_step)
        world_link(wo);
    return object;
}

/* Reads the body options of add() into `desc`. */
static int world_body_desc(JSContext *ctx, JSValueConst options,
    AthenaCollisionBodyDesc *desc) {
    const char *name = "Collision.World.add";
    JSValue value;
    int found;

    athena_collision_body_desc_init(desc);
    if (col_check_options(ctx, options, true, name))
        return -1;
    if (col_option_number(ctx, options, "x", &desc->x, -MAX_COORD, MAX_COORD, name) < 0 ||
        col_option_number(ctx, options, "y", &desc->y, -MAX_COORD, MAX_COORD, name) < 0)
        return -1;
    found = col_option_number(ctx, options, "r", &desc->r, 0.0f, MAX_COORD, name);
    if (found < 0)
        return -1;
    if (found) {
        desc->shape = ATHENA_SHAPE_CIRCLE;
        if (!(desc->r > 0.0f)) {
            JS_ThrowRangeError(ctx, "%s: r must be greater than 0", name);
            return -1;
        }
    } else {
        int has_w = col_option_number(ctx, options, "w", &desc->w, 0.0f, MAX_COORD, name);
        int has_h = has_w < 0 ? -1 :
            col_option_number(ctx, options, "h", &desc->h, 0.0f, MAX_COORD, name);

        if (has_w < 0 || has_h < 0)
            return -1;
        if (!has_w || !has_h) {
            JS_ThrowTypeError(ctx, "%s needs w and h (a rectangle) or r (a circle)", name);
            return -1;
        }
        if (!(desc->w > 0.0f && desc->h > 0.0f)) {
            JS_ThrowRangeError(ctx, "%s: w and h must be greater than 0", name);
            return -1;
        }
    }
    found = col_option(ctx, options, "type", &value);
    if (found < 0)
        return -1;
    if (found) {
        int failed = col_body_type(ctx, value, &desc->type, "Collision.World.add type");

        JS_FreeValue(ctx, value);
        if (failed)
            return -1;
    }
    if (col_option_number(ctx, options, "vx", &desc->vx, -MAX_COORD, MAX_COORD, name) < 0 ||
        col_option_number(ctx, options, "vy", &desc->vy, -MAX_COORD, MAX_COORD, name) < 0 ||
        col_option_bits(ctx, options, "layer", &desc->layer, name) < 0 ||
        col_option_bits(ctx, options, "mask", &desc->mask, name) < 0 ||
        col_option_bool(ctx, options, "sensor", &desc->sensor) < 0 ||
        col_option_bool(ctx, options, "oneWay", &desc->one_way) < 0 ||
        col_option_number(ctx, options, "gravityScale", &desc->gravity_scale, -MAX_RATE,
            MAX_RATE, name) < 0 ||
        col_option_number(ctx, options, "damping", &desc->damping, 0.0f, MAX_RATE, name) < 0 ||
        col_option_number(ctx, options, "bounce", &desc->bounce, 0.0f, 1.0f, name) < 0 ||
        col_option_number(ctx, options, "maxSpeedX", &desc->max_speed_x, 0.0f, MAX_COORD,
            name) < 0 ||
        col_option_number(ctx, options, "maxSpeedY", &desc->max_speed_y, 0.0f, MAX_COORD,
            name) < 0)
        return -1;
    return 0;
}

static JSValue world_add(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    WorldObject *wo = world_this(ctx, this_val);
    AthenaCollisionBodyDesc desc;
    AthenaBodyId id;
    BodyObject *bo;
    JSValue object;
    int error;

    if (!wo || world_body_desc(ctx, col_arg(argc, argv, 0), &desc))
        return JS_EXCEPTION;
    object = JS_NewObjectClass(ctx, body_class_id);
    if (JS_IsException(object))
        return object;
    bo = js_mallocz(ctx, sizeof(*bo));
    if (!bo) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    bo->world = JS_UNDEFINED;
    JS_SetOpaque(object, bo);
    desc.user = JS_VALUE_GET_PTR(object);
    error = athena_collision_add(wo->world, &desc, &id);
    if (error) {
        JS_FreeValue(ctx, object);
        if (error == ATHENA_COLLISION_ENOMEM)
            return JS_ThrowOutOfMemory(ctx);
        return JS_ThrowTypeError(ctx, "Collision.World.add: invalid body");
    }
    bo->world = JS_DupValue(ctx, this_val);
    bo->id = id;
    /* The world's reference, released by remove() or the world's finalizer. */
    JS_DupValue(ctx, object);
    return object;
}

/* Takes a body out of its world; the object stays, detached. */
static void body_detach(JSContext *ctx, BodyObject *bo) {
    WorldObject *wo = JS_GetOpaque(bo->world, world_class_id);
    JSValue world = bo->world, held = JS_UNDEFINED;
    AthenaBody *body = athena_collision_get(wo->world, bo->id);

    if (body)
        held = col_object_value(body->user);
    athena_collision_remove(wo->world, bo->id);
    bo->world = JS_UNDEFINED;
    bo->id = ATHENA_BODY_NONE;
    /* The caller holds the body object: neither release finalizes it. */
    JS_FreeValue(ctx, held);
    JS_FreeValue(ctx, world);
}

static JSValue world_remove(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    WorldObject *wo = world_this(ctx, this_val);
    JSValueConst value = col_arg(argc, argv, 0);
    BodyObject *bo;

    if (!wo)
        return JS_EXCEPTION;
    bo = JS_GetOpaque(value, body_class_id);
    if (!bo)
        return JS_ThrowTypeError(ctx, "Collision.World.remove expects a Collision.Body");
    if (JS_IsUndefined(bo->world) || JS_GetOpaque(bo->world, world_class_id) != wo)
        return JS_FALSE;
    body_detach(ctx, bo);
    return JS_TRUE;
}

static JSValue world_clear(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    WorldObject *wo = world_this(ctx, this_val);
    uint32_t i;

    if (!wo)
        return JS_EXCEPTION;
    for (i = 0; i < athena_collision_body_capacity(wo->world); i++) {
        AthenaBody *body = athena_collision_slot(wo->world, i);
        JSValue object;

        if (!body)
            continue;
        /* Held while detaching: the world may hold the only reference. */
        object = JS_DupValue(ctx, col_object_value(body->user));
        body_detach(ctx, JS_GetOpaque(object, body_class_id));
        JS_FreeValue(ctx, object);
    }
    return JS_UNDEFINED;
}

static JSValue world_bodies(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    WorldObject *wo = world_this(ctx, this_val);
    uint32_t i, n = 0;
    JSValue array;

    if (!wo)
        return JS_EXCEPTION;
    array = JS_NewArray(ctx);
    for (i = 0; i < athena_collision_body_capacity(wo->world) && !JS_IsException(array); i++) {
        AthenaBody *body = athena_collision_slot(wo->world, i);

        if (body && JS_SetPropertyUint32(ctx, array, n++,
                JS_DupValue(ctx, col_object_value(body->user))) < 0) {
            JS_FreeValue(ctx, array);
            array = JS_EXCEPTION;
        }
    }
    return array;
}

/*
 * The result of a move, in `out` when it is an object (its `contacts` array
 * emptied and reused): moving many bodies each frame then allocates nothing.
 */
static JSValue move_object(JSContext *ctx, WorldObject *wo, const AthenaCollisionMove *move,
    JSValueConst out) {
    JSValue object, contacts = JS_UNDEFINED;
    uint32_t i;

    if (JS_IsObject(out)) {
        object = JS_DupValue(ctx, out);
        contacts = col_get(ctx, object, ATOM_contacts);
        if (JS_IsException(contacts))
            goto fail;
        if (!JS_IsArray(ctx, contacts)) {
            JS_FreeValue(ctx, contacts);
            contacts = JS_UNDEFINED;
        } else if (col_put(ctx, contacts, ATOM_length, JS_NewInt32(ctx, 0))) {
            goto fail;
        }
    } else if (!JS_IsUndefined(out)) {
        return JS_ThrowTypeError(ctx, "Collision: the move result must be an object");
    } else {
        object = JS_NewObject(ctx);
        if (JS_IsException(object))
            return object;
    }
    if (JS_IsUndefined(contacts)) {
        contacts = JS_NewArray(ctx);
        if (JS_IsException(contacts) ||
            col_put(ctx, object, ATOM_contacts, JS_DupValue(ctx, contacts)))
            goto fail;
    }
    if (col_put(ctx, object, ATOM_dx, JS_NewFloat32(ctx, move->dx)) ||
        col_put(ctx, object, ATOM_dy, JS_NewFloat32(ctx, move->dy)) ||
        col_put(ctx, object, ATOM_onGround, JS_NewBool(ctx, move->on_ground)) ||
        col_put(ctx, object, ATOM_hitCeiling, JS_NewBool(ctx, move->hit_ceiling)) ||
        col_put(ctx, object, ATOM_hitWall, JS_NewInt32(ctx, move->hit_wall)))
        goto fail;
    for (i = 0; i < move->contact_count; i++) {
        JSValue contact = contact_object(ctx, wo, &move->contacts[i]);

        if (JS_IsException(contact) || JS_SetPropertyUint32(ctx, contacts, i, contact) < 0)
            goto fail;
    }
    JS_FreeValue(ctx, contacts);
    return object;
fail:
    JS_FreeValue(ctx, contacts);
    JS_FreeValue(ctx, object);
    return JS_EXCEPTION;
}

static JSValue world_move_body(JSContext *ctx, WorldObject *wo, AthenaBodyId id,
    JSValueConst vdx, JSValueConst vdy, JSValueConst out, const char *name) {
    AthenaCollisionMove move;
    float dx = 0.0f, dy = 0.0f;

    if (col_number_at(ctx, vdx, &dx, -MAX_COORD, MAX_COORD, name, "dx") ||
        col_number_at(ctx, vdy, &dy, -MAX_COORD, MAX_COORD, name, "dy"))
        return JS_EXCEPTION;
    /* By id: a valueOf() run by the conversions may have removed the body (then nothing moves). */
    athena_collision_move(wo->world, id, dx, dy, &move);
    return move_object(ctx, wo, &move, out);
}

static JSValue world_move(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    WorldObject *wo = world_this(ctx, this_val);
    AthenaBodyId id;

    if (!wo || !body_of_world(ctx, wo, col_arg(argc, argv, 0), &id, "Collision.World.move"))
        return JS_EXCEPTION;
    return world_move_body(ctx, wo, id, col_arg(argc, argv, 1), col_arg(argc, argv, 2),
        col_arg(argc, argv, 3), "Collision.World.move");
}

static JSValue world_step_js(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    WorldObject *wo = world_this(ctx, this_val);
    float dt;

    if (!wo || col_number(ctx, col_arg(argc, argv, 0), &dt, 0.0f, 3600.0f,
            "Collision.World.step dt"))
        return JS_EXCEPTION;
    return world_step(ctx, wo, dt) ? JS_EXCEPTION : JS_UNDEFINED;
}

/* ---- Tiles ------------------------------------------------------------- */

/* Tile kinds by id, grown up to the largest id named; new ones are empty. */
typedef struct {
    AthenaTileKind *kinds;
    uint32_t count, capacity;
} KindTable;

static AthenaTileKind *kind_at(JSContext *ctx, KindTable *table, uint32_t id) {
    if (id >= table->capacity) {
        uint32_t capacity = table->capacity ? table->capacity : 16u;
        AthenaTileKind *grown;

        while (capacity <= id)
            capacity *= 2u;
        grown = js_realloc(ctx, table->kinds, capacity * sizeof(*grown));
        if (!grown)
            return NULL;
        table->kinds = grown;
        table->capacity = capacity;
    }
    if (id >= table->count) {
        memset(table->kinds + table->count, 0,
            (id + 1u - table->count) * sizeof(AthenaTileKind));
        table->count = id + 1u;
    }
    return &table->kinds[id];
}

/* Marks the ids of an array (solid, oneWay) as `type` in the kinds table. */
static int grid_kind_list(JSContext *ctx, JSValueConst options, const char *key,
    AthenaTileKindType type, KindTable *table) {
    JSValue list;
    uint32_t length, i;
    int found = col_option(ctx, options, key, &list), result = 0;
    char label[64];

    if (found <= 0)
        return found;
    snprintf(label, sizeof(label), "Collision.World.setGrid %s", key);
    if (!JS_IsArray(ctx, list)) {
        JS_FreeValue(ctx, list);
        JS_ThrowTypeError(ctx, "%s must be an array of tile ids", label);
        return -1;
    }
    result = col_length(ctx, list, &length);
    for (i = 0; i < length && !result; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, list, i);
        int32_t id;

        result = JS_IsException(item) || col_int(ctx, item, &id, 0, MAX_TILE_ID, label) ? -1 : 0;
        JS_FreeValue(ctx, item);
        if (!result) {
            AthenaTileKind *kind = kind_at(ctx, table, (uint32_t)id);

            if (kind)
                kind->type = type;
            else
                result = -1;
        }
    }
    JS_FreeValue(ctx, list);
    return result;
}

/* A slope: [left, right] floor heights, or a preset name. */
static int grid_slope(JSContext *ctx, JSValueConst value, AthenaTileKind *kind,
    const char *label) {
    static const struct {
        const char *name;
        float left, right;
    } presets[] = {
        { "45r", 0.0f, 1.0f }, { "45l", 1.0f, 0.0f },
        { "22r1", 0.0f, 0.5f }, { "22r2", 0.5f, 1.0f },
        { "22l1", 1.0f, 0.5f }, { "22l2", 0.5f, 0.0f },
    };
    float left, right;

    if (JS_IsString(value)) {
        const char *text = JS_ToCString(ctx, value);
        size_t i;

        if (!text)
            return -1;
        for (i = 0; i < countof(presets); i++) {
            if (strcmp(text, presets[i].name) == 0) {
                kind->type = ATHENA_TILE_SLOPE;
                kind->left = presets[i].left;
                kind->right = presets[i].right;
                JS_FreeCString(ctx, text);
                return 0;
            }
        }
        JS_FreeCString(ctx, text);
        JS_ThrowTypeError(ctx, "%s: unknown slope (use [left, right], \"45r\", \"45l\", "
            "\"22r1\", \"22r2\", \"22l1\" or \"22l2\")", label);
        return -1;
    }
    if (!JS_IsArray(ctx, value)) {
        JS_ThrowTypeError(ctx, "%s must be [left, right] or a preset name", label);
        return -1;
    }
    {
        JSValue a = JS_GetPropertyUint32(ctx, value, 0), b = JS_GetPropertyUint32(ctx, value, 1);
        int failed = col_number(ctx, a, &left, 0.0f, 1.0f, label) ||
            col_number(ctx, b, &right, 0.0f, 1.0f, label);

        JS_FreeValue(ctx, a);
        JS_FreeValue(ctx, b);
        if (failed)
            return -1;
    }
    if (left == 0.0f && right == 0.0f) {
        JS_ThrowRangeError(ctx, "%s: a slope needs a height above 0 at one side", label);
        return -1;
    }
    kind->type = ATHENA_TILE_SLOPE;
    kind->left = left;
    kind->right = right;
    return 0;
}

static int grid_slopes(JSContext *ctx, JSValueConst options, KindTable *table) {
    JSPropertyEnum *props = NULL;
    uint32_t count = 0, i;
    JSValue slopes;
    int found = col_option(ctx, options, "slopes", &slopes), result = 0;

    if (found <= 0)
        return found;
    if (!JS_IsObject(slopes) || JS_IsArray(ctx, slopes)) {
        JS_FreeValue(ctx, slopes);
        JS_ThrowTypeError(ctx, "Collision.World.setGrid slopes must be an object { id: slope }");
        return -1;
    }
    if (JS_GetOwnPropertyNames(ctx, &props, &count, slopes,
            JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) {
        JS_FreeValue(ctx, slopes);
        return -1;
    }
    for (i = 0; i < count && !result; i++) {
        const char *key = JS_AtomToCString(ctx, props[i].atom);
        char *end = NULL, label[64];
        long id = key ? strtol(key, &end, 10) : -1;
        JSValue value;

        if (!key) {
            result = -1;
            break;
        }
        if (!*key || *end || id < 0 || id > (long)MAX_TILE_ID) {
            JS_ThrowRangeError(ctx, "Collision.World.setGrid slopes: '%s' is not a tile id", key);
            JS_FreeCString(ctx, key);
            result = -1;
            break;
        }
        JS_FreeCString(ctx, key);
        snprintf(label, sizeof(label), "Collision.World.setGrid slopes[%ld]", id);
        value = JS_GetProperty(ctx, slopes, props[i].atom);
        if (JS_IsException(value)) {
            result = -1;
        } else {
            AthenaTileKind *kind = kind_at(ctx, table, (uint32_t)id);

            result = !kind || grid_slope(ctx, value, kind, label) ? -1 : 0;
        }
        JS_FreeValue(ctx, value);
    }
    for (i = 0; i < count; i++)
        JS_FreeAtom(ctx, props[i].atom);
    js_free(ctx, props);
    JS_FreeValue(ctx, slopes);
    return result;
}

/*
 * The tiles, as `count` ids: a Uint16Array is used in place (NULL `*owned`),
 * an array is converted into `*owned`. Called last, after every option was
 * read: no script runs between taking the typed array's pointer and the copy
 * into the world.
 */
static int grid_tiles(JSContext *ctx, JSValueConst tiles, uint32_t count,
    const uint16_t **out, uint16_t **owned) {
    const char *label = "Collision.World.setGrid tiles";
    uint32_t length, i;

    *out = NULL;
    *owned = NULL;
    if (JS_IsUndefined(tiles))
        return 0;
    if (JS_GetTypedArrayType(tiles) == JS_TYPED_ARRAY_UINT16) {
        size_t offset, byte_length, element_size, buffer_length;
        JSValue buffer = JS_GetTypedArrayBuffer(ctx, tiles, &offset, &byte_length, &element_size);
        uint8_t *data;

        if (JS_IsException(buffer))
            return -1;
        data = JS_GetArrayBuffer(ctx, &buffer_length, buffer);
        JS_FreeValue(ctx, buffer);
        if (!data)
            return -1;
        if (byte_length / 2u != count || offset + byte_length > buffer_length) {
            JS_ThrowRangeError(ctx, "%s must hold columns * rows (%u) ids", label, (unsigned)count);
            return -1;
        }
        /* Uint16Array data is 2-byte aligned. */
        *out = (const uint16_t *)(data + offset);
        return 0;
    }
    if (!JS_IsArray(ctx, tiles)) {
        JS_ThrowTypeError(ctx, "%s must be a Uint16Array or an array of tile ids", label);
        return -1;
    }
    if (col_length(ctx, tiles, &length))
        return -1;
    if (length != count) {
        JS_ThrowRangeError(ctx, "%s must hold columns * rows (%u) ids", label, (unsigned)count);
        return -1;
    }
    *owned = js_malloc(ctx, (size_t)count * sizeof(uint16_t));
    if (!*owned)
        return -1;
    for (i = 0; i < count; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, tiles, i);
        int32_t id;
        int failed = JS_IsException(item) || col_int(ctx, item, &id, 0, MAX_TILE_ID, label);

        JS_FreeValue(ctx, item);
        if (failed) {
            js_free(ctx, *owned);
            *owned = NULL;
            return -1;
        }
        (*owned)[i] = (uint16_t)id;
    }
    *out = *owned;
    return 0;
}

static JSValue world_set_grid(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    const char *name = "Collision.World.setGrid";
    WorldObject *wo = world_this(ctx, this_val);
    JSValueConst options = col_arg(argc, argv, 0);
    AthenaCollisionGridDesc desc;
    KindTable table = { NULL, 0, 0 };
    const uint16_t *tile_ids;
    uint16_t *owned = NULL;
    int32_t columns = 0, rows = 0;
    JSValue tiles = JS_UNDEFINED;
    int error;

    if (!wo || col_check_options(ctx, options, true, name))
        return JS_EXCEPTION;
    memset(&desc, 0, sizeof(desc));
    desc.layer = 1u;
    {
        int found[4];

        if ((found[0] = col_option_int(ctx, options, "columns", &columns, 1, MAX_GRID_SIDE,
                name)) < 0 ||
            (found[1] = col_option_int(ctx, options, "rows", &rows, 1, MAX_GRID_SIDE, name)) < 0 ||
            (found[2] = col_option_number(ctx, options, "tileWidth", &desc.tile_width, 1e-3f,
                MAX_COORD, name)) < 0 ||
            (found[3] = col_option_number(ctx, options, "tileHeight", &desc.tile_height, 1e-3f,
                MAX_COORD, name)) < 0)
            return JS_EXCEPTION;
        if (!found[0] || !found[1] || !found[2] || !found[3])
            return JS_ThrowTypeError(ctx, "%s needs columns, rows, tileWidth and tileHeight",
                name);
    }
    desc.columns = (uint32_t)columns;
    desc.rows = (uint32_t)rows;
    /* Refused before any copy: a grid this large would not fit a PS2 anyway. */
    if ((uint64_t)desc.columns * desc.rows > ATHENA_COLLISION_MAX_TILES)
        return JS_ThrowRangeError(ctx, "%s: columns * rows must be at most %u tiles", name,
            (unsigned)ATHENA_COLLISION_MAX_TILES);
    if (col_option_number(ctx, options, "x", &desc.x, -MAX_COORD, MAX_COORD, name) < 0 ||
        col_option_number(ctx, options, "y", &desc.y, -MAX_COORD, MAX_COORD, name) < 0 ||
        col_option_bits(ctx, options, "layer", &desc.layer, name) < 0)
        return JS_EXCEPTION;
    if (grid_kind_list(ctx, options, "solid", ATHENA_TILE_SOLID, &table) < 0 ||
        grid_kind_list(ctx, options, "oneWay", ATHENA_TILE_ONE_WAY, &table) < 0 ||
        grid_slopes(ctx, options, &table) < 0 ||
        col_option(ctx, options, "tiles", &tiles) < 0 ||
        grid_tiles(ctx, tiles, desc.columns * desc.rows, &tile_ids, &owned) < 0) {
        JS_FreeValue(ctx, tiles);
        js_free(ctx, table.kinds);
        return JS_EXCEPTION;
    }
    desc.tiles = tile_ids;
    desc.kinds = table.kinds;
    desc.kind_count = table.count;
    error = athena_collision_set_grid(wo->world, &desc);
    js_free(ctx, owned);
    js_free(ctx, table.kinds);
    JS_FreeValue(ctx, tiles);
    if (error == ATHENA_COLLISION_ENOMEM)
        return JS_ThrowOutOfMemory(ctx);
    if (error)
        return JS_ThrowRangeError(ctx, "%s: invalid grid", name);
    return JS_UNDEFINED;
}

static JSValue world_clear_grid(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    WorldObject *wo = world_this(ctx, this_val);

    if (!wo)
        return JS_EXCEPTION;
    athena_collision_clear_grid(wo->world);
    return JS_UNDEFINED;
}

static int cell_args(JSContext *ctx, int argc, JSValueConst *argv, int32_t *column,
    int32_t *row, const char *name) {
    if (col_int_at(ctx, col_arg(argc, argv, 0), column, INT32_MIN, INT32_MAX, name, "column"))
        return -1;
    return col_int_at(ctx, col_arg(argc, argv, 1), row, INT32_MIN, INT32_MAX, name, "row");
}

static JSValue world_get_tile(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    WorldObject *wo = world_this(ctx, this_val);
    int32_t column, row;

    if (!wo || cell_args(ctx, argc, argv, &column, &row, "Collision.World.getTile"))
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, athena_collision_get_tile(wo->world, column, row));
}

static JSValue world_set_tile(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    WorldObject *wo = world_this(ctx, this_val);
    int32_t column, row, id;

    if (!wo || cell_args(ctx, argc, argv, &column, &row, "Collision.World.setTile") ||
        col_int(ctx, col_arg(argc, argv, 2), &id, 0, MAX_TILE_ID, "Collision.World.setTile id"))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, athena_collision_set_tile(wo->world, column, row, (uint16_t)id));
}

static JSValue world_cell_at(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    WorldObject *wo = world_this(ctx, this_val);
    float x, y;
    int32_t column, row;
    JSValue object;

    if (!wo || col_number(ctx, col_arg(argc, argv, 0), &x, -MAX_COORD, MAX_COORD,
            "Collision.World.cellAt x") ||
        col_number(ctx, col_arg(argc, argv, 1), &y, -MAX_COORD, MAX_COORD,
            "Collision.World.cellAt y"))
        return JS_EXCEPTION;
    if (!athena_collision_cell_at(wo->world, x, y, &column, &row))
        return JS_NULL;
    object = JS_NewObject(ctx);
    if (JS_IsException(object) ||
        col_set(ctx, object, "column", JS_NewInt32(ctx, column)) ||
        col_set(ctx, object, "row", JS_NewInt32(ctx, row)) ||
        col_set(ctx, object, "tile", JS_NewInt32(ctx,
            athena_collision_get_tile(wo->world, column, row)))) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    return object;
}

/* solidAt(x, y, mask?): a solid tile, a slope below its surface or a solid body. */
static JSValue world_solid_at(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    const char *name = "Collision.World.solidAt";
    WorldObject *wo = world_this(ctx, this_val);
    uint32_t mask = 0xFFFFFFFFu;
    float x, y;

    if (!wo || col_number_at(ctx, col_arg(argc, argv, 0), &x, -MAX_COORD, MAX_COORD, name, "x") ||
        col_number_at(ctx, col_arg(argc, argv, 1), &y, -MAX_COORD, MAX_COORD, name, "y") ||
        (!JS_IsUndefined(col_arg(argc, argv, 2)) && col_bits_at(ctx, argv[2], &mask, name, "mask")))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, athena_collision_point_solid(wo->world, x, y, mask));
}

/* ---- Queries ----------------------------------------------------------- */

static int world_reserve_ids(JSContext *ctx, WorldObject *wo, uint32_t count) {
    AthenaBodyId *grown;

    if (count <= wo->id_capacity)
        return 0;
    grown = js_realloc(ctx, wo->ids, count * sizeof(*grown));
    if (!grown)
        return -1;
    wo->ids = grown;
    wo->id_capacity = count;
    return 0;
}

static JSValue ids_array(JSContext *ctx, WorldObject *wo, uint32_t count) {
    JSValue array = JS_NewArray(ctx);
    uint32_t i;

    for (i = 0; i < count && !JS_IsException(array); i++) {
        if (JS_SetPropertyUint32(ctx, array, i, body_value(ctx, wo, wo->ids[i])) < 0) {
            JS_FreeValue(ctx, array);
            array = JS_EXCEPTION;
        }
    }
    return array;
}

/* The query `kind` with its numbers, into wo->ids; the count or -1. */
enum { QUERY_RECT, QUERY_CIRCLE, QUERY_POINT, QUERY_OVERLAPPING };

static int64_t world_run_query(WorldObject *wo, int kind, const float *n, uint32_t mask,
    AthenaBodyId id, uint32_t max) {
    switch (kind) {
    case QUERY_RECT:
        return athena_collision_query_rect(wo->world, n[0], n[1], n[2], n[3], mask, wo->ids, max);
    case QUERY_CIRCLE:
        return athena_collision_query_circle(wo->world, n[0], n[1], n[2], mask, wo->ids, max);
    case QUERY_POINT:
        return athena_collision_query_point(wo->world, n[0], n[1], mask, wo->ids, max);
    default:
        return athena_collision_overlapping(wo->world, id, wo->ids, max);
    }
}

static JSValue world_query_common(JSContext *ctx, WorldObject *wo, int kind,
    const float *numbers, uint32_t mask, AthenaBodyId id) {
    uint32_t count = (uint32_t)world_run_query(wo, kind, numbers, mask, id, wo->id_capacity);

    if (count > wo->id_capacity) {
        if (world_reserve_ids(ctx, wo, count))
            return JS_EXCEPTION;
        count = (uint32_t)world_run_query(wo, kind, numbers, mask, id, wo->id_capacity);
    }
    return ids_array(ctx, wo, count);
}

/* query(x, y, w, h, mask?), queryCircle(x, y, r, mask?), queryPoint(x, y, mask?). */
static JSValue world_query(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int magic) {
    static const char *const names[] = { "Collision.World.query",
        "Collision.World.queryCircle", "Collision.World.queryPoint" };
    static const int counts[] = { 4, 3, 2 };
    WorldObject *wo = world_this(ctx, this_val);
    float numbers[4];
    uint32_t mask = 0xFFFFFFFFu;
    int i;

    if (!wo)
        return JS_EXCEPTION;
    for (i = 0; i < counts[magic]; i++) {
        float min = (magic == QUERY_RECT && i >= 2) || (magic == QUERY_CIRCLE && i == 2) ?
            0.0f : -MAX_COORD;

        if (col_number(ctx, col_arg(argc, argv, i), &numbers[i], min, MAX_COORD, names[magic]))
            return JS_EXCEPTION;
    }
    if (!JS_IsUndefined(col_arg(argc, argv, counts[magic])) &&
        col_bits(ctx, argv[counts[magic]], &mask, names[magic]))
        return JS_EXCEPTION;
    return world_query_common(ctx, wo, magic, numbers, mask, ATHENA_BODY_NONE);
}

static JSValue world_overlapping(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    WorldObject *wo = world_this(ctx, this_val);
    AthenaBodyId id;

    if (!wo || !body_of_world(ctx, wo, col_arg(argc, argv, 0), &id,
            "Collision.World.overlapping"))
        return JS_EXCEPTION;
    return world_query_common(ctx, wo, QUERY_OVERLAPPING, NULL, 0, id);
}

static JSValue world_pairs(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    const char *name = "Collision.World.pairs";
    WorldObject *wo = world_this(ctx, this_val);
    JSValueConst callback = col_arg(argc, argv, 2);
    uint32_t layer_a, layer_b, count, i;
    JSValue array;

    if (!wo || col_bits_at(ctx, col_arg(argc, argv, 0), &layer_a, name, "layerA") ||
        col_bits_at(ctx, col_arg(argc, argv, 1), &layer_b, name, "layerB"))
        return JS_EXCEPTION;
    if (!JS_IsUndefined(callback) && !JS_IsFunction(ctx, callback))
        return JS_ThrowTypeError(ctx, "%s: the callback must be a function", name);
    count = athena_collision_pairs(wo->world, layer_a, layer_b, wo->pairs, wo->pair_capacity);
    if (count > MAX_PAIRS)
        return JS_ThrowRangeError(ctx, "%s: %u pairs, more than %u (use narrower layers)",
            name, (unsigned)count, (unsigned)MAX_PAIRS);
    if (count > wo->pair_capacity) {
        AthenaBodyPair *grown = js_realloc(ctx, wo->pairs, count * sizeof(*grown));

        if (!grown)
            return JS_EXCEPTION;
        wo->pairs = grown;
        wo->pair_capacity = count;
        count = athena_collision_pairs(wo->world, layer_a, layer_b, wo->pairs, count);
    }
    if (!JS_IsUndefined(callback)) {
        /* The callback may call pairs() again: it gets a buffer of its own. */
        AthenaBodyPair *list = wo->pairs;
        uint32_t capacity = wo->pair_capacity;
        int failed = 0;

        wo->pairs = NULL;
        wo->pair_capacity = 0;
        for (i = 0; i < count && !failed; i++) {
            JSValue args[2] = { body_value(ctx, wo, list[i].a), body_value(ctx, wo, list[i].b) };

            /* A body removed by an earlier call: its pairs are skipped. */
            if (!JS_IsNull(args[0]) && !JS_IsNull(args[1])) {
                JSValue ret = JS_Call(ctx, callback, JS_UNDEFINED, 2, (JSValueConst *)args);

                failed = JS_IsException(ret);
                JS_FreeValue(ctx, ret);
            }
            JS_FreeValue(ctx, args[0]);
            JS_FreeValue(ctx, args[1]);
        }
        if (wo->pairs) {
            js_free(ctx, list);
        } else {
            wo->pairs = list;
            wo->pair_capacity = capacity;
        }
        return failed ? JS_EXCEPTION : JS_NewUint32(ctx, count);
    }
    array = JS_NewArray(ctx);
    for (i = 0; i < count && !JS_IsException(array); i++) {
        JSValue pair = JS_NewArray(ctx);

        if (JS_IsException(pair) ||
            JS_SetPropertyUint32(ctx, pair, 0, body_value(ctx, wo, wo->pairs[i].a)) < 0 ||
            JS_SetPropertyUint32(ctx, pair, 1, body_value(ctx, wo, wo->pairs[i].b)) < 0 ||
            JS_SetPropertyUint32(ctx, array, i, JS_DupValue(ctx, pair)) < 0) {
            JS_FreeValue(ctx, array);
            array = JS_EXCEPTION;
        }
        JS_FreeValue(ctx, pair);
    }
    return array;
}

static JSValue world_raycast(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    const char *name = "Collision.World.raycast";
    WorldObject *wo = world_this(ctx, this_val);
    JSValueConst options = col_arg(argc, argv, 4);
    AthenaRaycastDesc desc = { 0xFFFFFFFFu, ATHENA_BODY_NONE, false };
    AthenaRayHit hit;
    float p[4], length;
    JSValue ignore, object;
    int i, found;

    if (!wo)
        return JS_EXCEPTION;
    for (i = 0; i < 4; i++)
        if (col_number(ctx, col_arg(argc, argv, i), &p[i], -MAX_COORD, MAX_COORD, name))
            return JS_EXCEPTION;
    if (col_check_options(ctx, options, false, name) ||
        col_option_bits(ctx, options, "mask", &desc.mask, name) < 0 ||
        col_option_bool(ctx, options, "sensors", &desc.sensors) < 0)
        return JS_EXCEPTION;
    found = col_option(ctx, options, "ignore", &ignore);
    if (found < 0)
        return JS_EXCEPTION;
    if (found) {
        AthenaBodyId id = ATHENA_BODY_NONE;
        bool ok = JS_IsNull(ignore) || body_of_world(ctx, wo, ignore, &id, name) != NULL;

        JS_FreeValue(ctx, ignore);
        if (!ok)
            return JS_EXCEPTION;
        desc.ignore = id;
    }
    if (!athena_collision_raycast(wo->world, p[0], p[1], p[2], p[3], &desc, &hit))
        return JS_NULL;
    length = sqrtf((p[2] - p[0]) * (p[2] - p[0]) + (p[3] - p[1]) * (p[3] - p[1]));
    object = JS_NewObject(ctx);
    if (JS_IsException(object) ||
        col_put(ctx, object, ATOM_x, JS_NewFloat32(ctx, hit.x)) ||
        col_put(ctx, object, ATOM_y, JS_NewFloat32(ctx, hit.y)) ||
        col_put(ctx, object, ATOM_normalX, JS_NewFloat32(ctx, hit.normal_x)) ||
        col_put(ctx, object, ATOM_normalY, JS_NewFloat32(ctx, hit.normal_y)) ||
        col_put(ctx, object, ATOM_fraction, JS_NewFloat32(ctx, hit.fraction)) ||
        col_put(ctx, object, ATOM_distance, JS_NewFloat32(ctx, hit.fraction * length)) ||
        col_put(ctx, object, ATOM_body, body_value(ctx, wo, hit.body)) ||
        col_put(ctx, object, ATOM_tile, JS_NewInt32(ctx, hit.tile)) ||
        col_put(ctx, object, ATOM_column, JS_NewInt32(ctx, hit.column)) ||
        col_put(ctx, object, ATOM_row, JS_NewInt32(ctx, hit.row))) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    return object;
}

static JSValue world_draw_debug(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    const char *name = "Collision.World.drawDebug";
    WorldObject *wo = world_this(ctx, this_val);
    JSValueConst options = col_arg(argc, argv, 0);
    AthenaCollisionDebug debug = { true, true };

    if (!wo)
        return JS_EXCEPTION;
    if (!col_main(ctx))
        return JS_ThrowInternalError(ctx, "%s is only available on the main script", name);
    if (col_check_options(ctx, options, false, name) ||
        col_option_bool(ctx, options, "bodies", &debug.bodies) < 0 ||
        col_option_bool(ctx, options, "tiles", &debug.tiles) < 0)
        return JS_EXCEPTION;
    athena_collision_draw_debug(wo->world, &debug);
    return JS_UNDEFINED;
}

enum {
    WORLD_GRAVITY_X,
    WORLD_GRAVITY_Y,
    WORLD_CELL_SIZE,
    WORLD_BODY_COUNT,
    WORLD_AUTO_STEP,
    WORLD_ON_CONTACT,
    WORLD_ON_ENTER,
    WORLD_ON_EXIT,
    WORLD_GRID,
};

static JSValue *world_handler(WorldObject *wo, int magic) {
    return magic == WORLD_ON_CONTACT ? &wo->on_contact :
        magic == WORLD_ON_ENTER ? &wo->on_enter : &wo->on_exit;
}

static JSValue world_get(JSContext *ctx, JSValueConst this_val, int magic) {
    WorldObject *wo = world_this(ctx, this_val);
    AthenaCollisionGridDesc grid;
    float gx, gy;

    if (!wo)
        return JS_EXCEPTION;
    athena_collision_get_gravity(wo->world, &gx, &gy);
    switch (magic) {
    case WORLD_GRAVITY_X:
        return JS_NewFloat32(ctx, gx);
    case WORLD_GRAVITY_Y:
        return JS_NewFloat32(ctx, gy);
    case WORLD_CELL_SIZE:
        return JS_NewFloat32(ctx, athena_collision_cell_size(wo->world));
    case WORLD_BODY_COUNT:
        return JS_NewUint32(ctx, athena_collision_body_count(wo->world));
    case WORLD_AUTO_STEP:
        return JS_NewBool(ctx, wo->auto_step);
    case WORLD_ON_CONTACT:
    case WORLD_ON_ENTER:
    case WORLD_ON_EXIT: {
        JSValue handler = *world_handler(wo, magic);

        return JS_IsUndefined(handler) ? JS_NULL : JS_DupValue(ctx, handler);
    }
    case WORLD_GRID: {
        JSValue object;

        if (!athena_collision_get_grid(wo->world, &grid))
            return JS_UNDEFINED;
        object = JS_NewObject(ctx);
        if (JS_IsException(object) ||
            col_set(ctx, object, "columns", JS_NewUint32(ctx, grid.columns)) ||
            col_set(ctx, object, "rows", JS_NewUint32(ctx, grid.rows)) ||
            col_set(ctx, object, "tileWidth", JS_NewFloat32(ctx, grid.tile_width)) ||
            col_set(ctx, object, "tileHeight", JS_NewFloat32(ctx, grid.tile_height)) ||
            col_set(ctx, object, "x", JS_NewFloat32(ctx, grid.x)) ||
            col_set(ctx, object, "y", JS_NewFloat32(ctx, grid.y)) ||
            col_set(ctx, object, "layer", JS_NewInt32(ctx, (int32_t)grid.layer))) {
            JS_FreeValue(ctx, object);
            return JS_EXCEPTION;
        }
        return object;
    }
    }
    return JS_UNDEFINED;
}

static JSValue world_set(JSContext *ctx, JSValueConst this_val, JSValueConst value,
    int magic) {
    WorldObject *wo = world_this(ctx, this_val);
    float gx, gy;

    if (!wo)
        return JS_EXCEPTION;
    athena_collision_get_gravity(wo->world, &gx, &gy);
    switch (magic) {
    case WORLD_GRAVITY_X:
        if (col_number(ctx, value, &gx, -MAX_COORD, MAX_COORD, "Collision.World.gravityX"))
            return JS_EXCEPTION;
        athena_collision_set_gravity(wo->world, gx, gy);
        break;
    case WORLD_GRAVITY_Y:
        if (col_number(ctx, value, &gy, -MAX_COORD, MAX_COORD, "Collision.World.gravityY"))
            return JS_EXCEPTION;
        athena_collision_set_gravity(wo->world, gx, gy);
        break;
    case WORLD_AUTO_STEP: {
        bool on = JS_ToBool(ctx, value) != 0;

        if (on && !col_main(ctx))
            return JS_ThrowInternalError(ctx,
                "Collision.World: autoStep is only available on the main script");
        wo->auto_step = on;
        if (on)
            world_link(wo);
        else
            world_unlink(wo);
        break;
    }
    case WORLD_ON_CONTACT:
    case WORLD_ON_ENTER:
    case WORLD_ON_EXIT: {
        JSValue *slot = world_handler(wo, magic), old = *slot;

        if (JS_IsNull(value) || JS_IsUndefined(value)) {
            *slot = JS_UNDEFINED;
        } else if (JS_IsFunction(ctx, value)) {
            *slot = JS_DupValue(ctx, value);
        } else {
            return JS_ThrowTypeError(ctx, "Collision.World.%s must be a function or null",
                magic == WORLD_ON_CONTACT ? "onContact" : magic == WORLD_ON_ENTER ? "onEnter" :
                "onExit");
        }
        JS_FreeValue(ctx, old);
        world_sync_funcs(wo);
        break;
    }
    }
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry world_proto_funcs[] = {
    JS_CFUNC_DEF("add", 1, world_add),
    JS_CFUNC_DEF("remove", 1, world_remove),
    JS_CFUNC_DEF("clear", 0, world_clear),
    JS_CFUNC_DEF("bodies", 0, world_bodies),
    JS_CFUNC_DEF("move", 4, world_move),
    JS_CFUNC_DEF("step", 1, world_step_js),
    JS_CFUNC_DEF("setGrid", 1, world_set_grid),
    JS_CFUNC_DEF("clearGrid", 0, world_clear_grid),
    JS_CFUNC_DEF("getTile", 2, world_get_tile),
    JS_CFUNC_DEF("setTile", 3, world_set_tile),
    JS_CFUNC_DEF("cellAt", 2, world_cell_at),
    JS_CFUNC_DEF("solidAt", 3, world_solid_at),
    JS_CFUNC_MAGIC_DEF("query", 5, world_query, QUERY_RECT),
    JS_CFUNC_MAGIC_DEF("queryCircle", 4, world_query, QUERY_CIRCLE),
    JS_CFUNC_MAGIC_DEF("queryPoint", 3, world_query, QUERY_POINT),
    JS_CFUNC_DEF("overlapping", 1, world_overlapping),
    JS_CFUNC_DEF("pairs", 3, world_pairs),
    JS_CFUNC_DEF("raycast", 5, world_raycast),
    JS_CFUNC_DEF("drawDebug", 1, world_draw_debug),
    JS_CGETSET_MAGIC_DEF("gravityX", world_get, world_set, WORLD_GRAVITY_X),
    JS_CGETSET_MAGIC_DEF("gravityY", world_get, world_set, WORLD_GRAVITY_Y),
    JS_CGETSET_MAGIC_DEF("cellSize", world_get, NULL, WORLD_CELL_SIZE),
    JS_CGETSET_MAGIC_DEF("bodyCount", world_get, NULL, WORLD_BODY_COUNT),
    JS_CGETSET_MAGIC_DEF("autoStep", world_get, world_set, WORLD_AUTO_STEP),
    JS_CGETSET_MAGIC_DEF("onContact", world_get, world_set, WORLD_ON_CONTACT),
    JS_CGETSET_MAGIC_DEF("onEnter", world_get, world_set, WORLD_ON_ENTER),
    JS_CGETSET_MAGIC_DEF("onExit", world_get, world_set, WORLD_ON_EXIT),
    JS_CGETSET_MAGIC_DEF("grid", world_get, NULL, WORLD_GRID),
};

/* ---------------------------------------------------------------------- */
/* Body                                                                   */

static void body_finalizer(JSRuntime *rt, JSValue value) {
    BodyObject *bo = JS_GetOpaque(value, body_class_id);

    if (!bo)
        return;
    JS_FreeValueRT(rt, bo->world);
    js_free_rt(rt, bo);
}

static void body_gc_mark(JSRuntime *rt, JSValueConst value, JS_MarkFunc *mark) {
    BodyObject *bo = JS_GetOpaque(value, body_class_id);

    if (bo)
        JS_MarkValue(rt, bo->world, mark);
}

static JSClassDef body_class = {
    "Body",
    .finalizer = body_finalizer,
    .gc_mark = body_gc_mark,
};

static JSValue body_ctor(JSContext *ctx, JSValueConst new_target, int argc,
    JSValueConst *argv) {
    return JS_ThrowTypeError(ctx, "use world.add() to create a Collision.Body");
}

/* Body properties: magic number and name. Error labels are constants, never formatted. */
#define BODY_PROPS(X) \
    X(BODY_WORLD, "world") X(BODY_VALID, "valid") X(BODY_SHAPE, "shape") \
    X(BODY_TYPE, "type") X(BODY_X, "x") X(BODY_Y, "y") X(BODY_W, "w") X(BODY_H, "h") \
    X(BODY_R, "r") X(BODY_VX, "vx") X(BODY_VY, "vy") X(BODY_LAYER, "layer") \
    X(BODY_MASK, "mask") X(BODY_SENSOR, "sensor") X(BODY_ONE_WAY, "oneWay") \
    X(BODY_DROP_THROUGH, "dropThrough") X(BODY_GRAVITY_SCALE, "gravityScale") \
    X(BODY_DAMPING, "damping") X(BODY_BOUNCE, "bounce") X(BODY_MAX_SPEED_X, "maxSpeedX") \
    X(BODY_MAX_SPEED_Y, "maxSpeedY") X(BODY_ON_GROUND, "onGround") \
    X(BODY_ON_CEILING, "onCeiling") X(BODY_ON_WALL, "onWall") X(BODY_GROUND, "ground") \
    X(BODY_CRUSHED, "crushed") X(BODY_CENTER_X, "centerX") X(BODY_CENTER_Y, "centerY") \
    X(BODY_RIGHT, "right") X(BODY_BOTTOM, "bottom")

#define BODY_ENUM(id, name) id,
#define BODY_LABEL(id, name) "Collision.Body." name,

enum { BODY_PROPS(BODY_ENUM) BODY_PROP_COUNT };

static const char *const body_labels[BODY_PROP_COUNT] = { BODY_PROPS(BODY_LABEL) };

static JSValue body_get(JSContext *ctx, JSValueConst this_val, int magic) {
    BodyObject *bo = JS_GetOpaque2(ctx, this_val, body_class_id);
    WorldObject *wo;
    AthenaBody *body;

    if (!bo)
        return JS_EXCEPTION;
    if (magic == BODY_WORLD)
        return JS_IsUndefined(bo->world) ? JS_NULL : JS_DupValue(ctx, bo->world);
    if (magic == BODY_VALID)
        return JS_NewBool(ctx, !JS_IsUndefined(bo->world));
    body = body_resolve(ctx, this_val, &wo, body_labels[magic]);
    if (!body)
        return JS_EXCEPTION;
    switch (magic) {
    case BODY_CRUSHED: return JS_NewBool(ctx, body->crushed);
    case BODY_CENTER_X:
        return JS_NewFloat32(ctx, body->shape == ATHENA_SHAPE_CIRCLE ? body->x : body->x + body->w * 0.5f);
    case BODY_CENTER_Y:
        return JS_NewFloat32(ctx, body->shape == ATHENA_SHAPE_CIRCLE ? body->y : body->y + body->h * 0.5f);
    case BODY_RIGHT:
        return JS_NewFloat32(ctx, body->shape == ATHENA_SHAPE_CIRCLE ? body->x + body->r : body->x + body->w);
    case BODY_BOTTOM:
        return JS_NewFloat32(ctx, body->shape == ATHENA_SHAPE_CIRCLE ? body->y + body->r : body->y + body->h);
    case BODY_SHAPE:
        return JS_NewString(ctx, body->shape == ATHENA_SHAPE_CIRCLE ? "circle" : "rect");
    case BODY_TYPE:
        return JS_NewString(ctx, body_type_names[body->type]);
    case BODY_X: return JS_NewFloat32(ctx, body->x);
    case BODY_Y: return JS_NewFloat32(ctx, body->y);
    case BODY_W: return JS_NewFloat32(ctx, body->w);
    case BODY_H: return JS_NewFloat32(ctx, body->h);
    case BODY_R:
        return body->shape == ATHENA_SHAPE_CIRCLE ? JS_NewFloat32(ctx, body->r) : JS_UNDEFINED;
    case BODY_VX: return JS_NewFloat32(ctx, body->vx);
    case BODY_VY: return JS_NewFloat32(ctx, body->vy);
    case BODY_LAYER: return JS_NewInt32(ctx, (int32_t)body->layer);
    case BODY_MASK: return JS_NewInt32(ctx, (int32_t)body->mask);
    case BODY_SENSOR: return JS_NewBool(ctx, body->sensor);
    case BODY_ONE_WAY: return JS_NewBool(ctx, body->one_way);
    case BODY_DROP_THROUGH: return JS_NewBool(ctx, body->drop_through);
    case BODY_GRAVITY_SCALE: return JS_NewFloat32(ctx, body->gravity_scale);
    case BODY_DAMPING: return JS_NewFloat32(ctx, body->damping);
    case BODY_BOUNCE: return JS_NewFloat32(ctx, body->bounce);
    case BODY_MAX_SPEED_X: return JS_NewFloat32(ctx, body->max_speed_x);
    case BODY_MAX_SPEED_Y: return JS_NewFloat32(ctx, body->max_speed_y);
    case BODY_ON_GROUND: return JS_NewBool(ctx, body->on_ground);
    case BODY_ON_CEILING: return JS_NewBool(ctx, body->on_ceiling);
    case BODY_ON_WALL: return JS_NewInt32(ctx, body->on_wall);
    case BODY_GROUND: return body_value(ctx, wo, body->ground);
    }
    return JS_UNDEFINED;
}

static JSValue body_set(JSContext *ctx, JSValueConst this_val, JSValueConst value, int magic) {
    BodyObject *bo = JS_GetOpaque2(ctx, this_val, body_class_id);
    WorldObject *wo;
    AthenaBody *body;
    AthenaBodyId id;
    const char *label = body_labels[magic];
    float f;
    bool refresh = false;

    if (!bo)
        return JS_EXCEPTION;
    if (!body_resolve(ctx, this_val, &wo, label))
        return JS_EXCEPTION;
    id = bo->id;
    /* Converting the value may run a script (valueOf): read it first, then the body again. */
    switch (magic) {
    case BODY_TYPE: {
        AthenaBodyType type;

        if (col_body_type(ctx, value, &type, label))
            return JS_EXCEPTION;
        if (!(body = body_resolve(ctx, this_val, &wo, label)))
            return JS_EXCEPTION;
        body->type = type;
        return JS_UNDEFINED;
    }
    case BODY_LAYER:
    case BODY_MASK: {
        uint32_t bits;

        if (col_bits(ctx, value, &bits, label))
            return JS_EXCEPTION;
        if (!(body = body_resolve(ctx, this_val, &wo, label)))
            return JS_EXCEPTION;
        if (magic == BODY_LAYER)
            body->layer = bits;
        else
            body->mask = bits;
        return JS_UNDEFINED;
    }
    case BODY_SENSOR:
    case BODY_ONE_WAY:
    case BODY_DROP_THROUGH: {
        bool on = JS_ToBool(ctx, value) != 0;

        body = athena_collision_get(wo->world, id);
        if (magic == BODY_SENSOR)
            body->sensor = on;
        else if (magic == BODY_ONE_WAY)
            body->one_way = on;
        else
            body->drop_through = on;
        return JS_UNDEFINED;
    }
    }
    {
        float min = -MAX_COORD, max = MAX_COORD;

        switch (magic) {
        case BODY_W: case BODY_H: case BODY_R: min = 1e-3f; break;
        case BODY_DAMPING: min = 0.0f; max = MAX_RATE; break;
        case BODY_GRAVITY_SCALE: min = -MAX_RATE; max = MAX_RATE; break;
        case BODY_BOUNCE: min = 0.0f; max = 1.0f; break;
        case BODY_MAX_SPEED_X: case BODY_MAX_SPEED_Y: min = 0.0f; break;
        case BODY_X: case BODY_Y: case BODY_VX: case BODY_VY: break;
        default:
            return JS_ThrowTypeError(ctx, "%s is read-only", label);
        }
        if (col_number(ctx, value, &f, min, max, label))
            return JS_EXCEPTION;
    }
    if (!(body = body_resolve(ctx, this_val, &wo, label)))
        return JS_EXCEPTION;
    switch (magic) {
    /*
     * A kinematic body moves by the difference, carrying its riders and
     * pushing what is in its way: a platform animated with Tween just works.
     * Other bodies teleport.
     */
    case BODY_X:
        if (body->type == ATHENA_BODY_KINEMATIC)
            athena_collision_translate(wo->world, bo->id, f - body->x, 0.0f);
        else
            athena_collision_set_position(wo->world, bo->id, f, body->y);
        break;
    case BODY_Y:
        if (body->type == ATHENA_BODY_KINEMATIC)
            athena_collision_translate(wo->world, bo->id, 0.0f, f - body->y);
        else
            athena_collision_set_position(wo->world, bo->id, body->x, f);
        break;
    case BODY_W:
    case BODY_H:
        if (body->shape == ATHENA_SHAPE_CIRCLE)
            return JS_ThrowTypeError(ctx, "%s: a circle is sized by r", label);
        if (magic == BODY_W)
            body->w = f;
        else
            body->h = f;
        refresh = true;
        break;
    case BODY_R:
        if (body->shape != ATHENA_SHAPE_CIRCLE)
            return JS_ThrowTypeError(ctx, "%s: a rectangle is sized by w and h", label);
        body->r = f;
        refresh = true;
        break;
    case BODY_VX: body->vx = f; break;
    case BODY_VY: body->vy = f; break;
    case BODY_GRAVITY_SCALE: body->gravity_scale = f; break;
    case BODY_DAMPING: body->damping = f; break;
    case BODY_BOUNCE: body->bounce = f; break;
    case BODY_MAX_SPEED_X: body->max_speed_x = f; break;
    case BODY_MAX_SPEED_Y: body->max_speed_y = f; break;
    }
    if (refresh)
        athena_collision_refresh(wo->world, bo->id);
    return JS_UNDEFINED;
}

static JSValue body_move(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    WorldObject *wo;
    BodyObject *bo = JS_GetOpaque2(ctx, this_val, body_class_id);

    if (!bo || !body_resolve(ctx, this_val, &wo, "Collision.Body.move"))
        return JS_EXCEPTION;
    return world_move_body(ctx, wo, bo->id, col_arg(argc, argv, 0), col_arg(argc, argv, 1),
        col_arg(argc, argv, 2), "Collision.Body.move");
}

static JSValue body_set_position(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    const char *name = "Collision.Body.setPosition";
    BodyObject *bo = JS_GetOpaque2(ctx, this_val, body_class_id);
    WorldObject *wo;
    float x, y;

    if (!bo || col_number(ctx, col_arg(argc, argv, 0), &x, -MAX_COORD, MAX_COORD, name) ||
        col_number(ctx, col_arg(argc, argv, 1), &y, -MAX_COORD, MAX_COORD, name) ||
        !body_resolve(ctx, this_val, &wo, name))
        return JS_EXCEPTION;
    athena_collision_set_position(wo->world, bo->id, x, y);
    return JS_UNDEFINED;
}

static JSValue body_get_bounds(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    AthenaBody *body = body_resolve(ctx, this_val, NULL, "Collision.Body.getBounds");
    float x0, y0, x1, y1;
    JSValue object;

    if (!body)
        return JS_EXCEPTION;
    athena_collision_body_bounds(body, &x0, &y0, &x1, &y1);
    object = JS_NewObject(ctx);
    if (JS_IsException(object) ||
        col_put(ctx, object, ATOM_x, JS_NewFloat32(ctx, x0)) ||
        col_put(ctx, object, ATOM_y, JS_NewFloat32(ctx, y0)) ||
        col_put(ctx, object, ATOM_w, JS_NewFloat32(ctx, x1 - x0)) ||
        col_put(ctx, object, ATOM_h, JS_NewFloat32(ctx, y1 - y0))) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    return object;
}

static JSValue body_remove(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    BodyObject *bo = JS_GetOpaque2(ctx, this_val, body_class_id);

    if (!bo)
        return JS_EXCEPTION;
    if (JS_IsUndefined(bo->world))
        return JS_FALSE;
    body_detach(ctx, bo);
    return JS_TRUE;
}

#define BODY_PROP(name, magic) JS_CGETSET_MAGIC_DEF(name, body_get, body_set, magic)
#define BODY_READ(name, magic) JS_CGETSET_MAGIC_DEF(name, body_get, NULL, magic)

static const JSCFunctionListEntry body_proto_funcs[] = {
    JS_CFUNC_DEF("move", 3, body_move),
    JS_CFUNC_DEF("setPosition", 2, body_set_position),
    JS_CFUNC_DEF("getBounds", 0, body_get_bounds),
    JS_CFUNC_DEF("remove", 0, body_remove),
    BODY_READ("world", BODY_WORLD),
    BODY_READ("valid", BODY_VALID),
    BODY_READ("shape", BODY_SHAPE),
    BODY_PROP("type", BODY_TYPE),
    BODY_PROP("x", BODY_X),
    BODY_PROP("y", BODY_Y),
    BODY_PROP("w", BODY_W),
    BODY_PROP("h", BODY_H),
    BODY_PROP("r", BODY_R),
    BODY_PROP("vx", BODY_VX),
    BODY_PROP("vy", BODY_VY),
    BODY_PROP("layer", BODY_LAYER),
    BODY_PROP("mask", BODY_MASK),
    BODY_PROP("sensor", BODY_SENSOR),
    BODY_PROP("oneWay", BODY_ONE_WAY),
    BODY_PROP("dropThrough", BODY_DROP_THROUGH),
    BODY_PROP("gravityScale", BODY_GRAVITY_SCALE),
    BODY_PROP("damping", BODY_DAMPING),
    BODY_PROP("bounce", BODY_BOUNCE),
    BODY_PROP("maxSpeedX", BODY_MAX_SPEED_X),
    BODY_PROP("maxSpeedY", BODY_MAX_SPEED_Y),
    BODY_READ("onGround", BODY_ON_GROUND),
    BODY_READ("onCeiling", BODY_ON_CEILING),
    BODY_READ("onWall", BODY_ON_WALL),
    BODY_READ("ground", BODY_GROUND),
    BODY_READ("crushed", BODY_CRUSHED),
    BODY_READ("centerX", BODY_CENTER_X),
    BODY_READ("centerY", BODY_CENTER_Y),
    BODY_READ("right", BODY_RIGHT),
    BODY_READ("bottom", BODY_BOTTOM),
};

/* ---------------------------------------------------------------------- */
/* Shapes outside a world                                                 */

/* A Body, { x, y, w, h } or { x, y, r }. */
static int col_shape(JSContext *ctx, JSValueConst value, AthenaShape *shape,
    const char *name) {
    JSValue r;
    int found;

    if (JS_GetOpaque(value, body_class_id)) {
        AthenaBody *body = body_resolve(ctx, value, NULL, name);

        if (!body)
            return -1;
        shape->type = body->shape;
        shape->x = body->x;
        shape->y = body->y;
        shape->w = body->w;
        shape->h = body->h;
        shape->r = body->r;
        return 0;
    }
    if (!JS_IsObject(value)) {
        JS_ThrowTypeError(ctx, "%s expects a Body, { x, y, w, h } or { x, y, r }", name);
        return -1;
    }
    memset(shape, 0, sizeof(*shape));
    if (col_field(ctx, value, ATOM_x, &shape->x, -MAX_COORD, MAX_COORD, name) ||
        col_field(ctx, value, ATOM_y, &shape->y, -MAX_COORD, MAX_COORD, name))
        return -1;
    found = col_option(ctx, value, "r", &r);
    if (found < 0)
        return -1;
    if (found) {
        int failed = col_number(ctx, r, &shape->r, 0.0f, MAX_COORD, name);

        JS_FreeValue(ctx, r);
        shape->type = ATHENA_SHAPE_CIRCLE;
        return failed;
    }
    shape->type = ATHENA_SHAPE_RECT;
    return col_field(ctx, value, ATOM_w, &shape->w, 0.0f, MAX_COORD, name) ||
        col_field(ctx, value, ATOM_h, &shape->h, 0.0f, MAX_COORD, name) ? -1 : 0;
}

static JSValue collision_overlaps(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    AthenaShape a, b;

    if (col_shape(ctx, col_arg(argc, argv, 0), &a, "Collision.overlaps") ||
        col_shape(ctx, col_arg(argc, argv, 1), &b, "Collision.overlaps"))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, athena_collision_shape_overlap(&a, &b));
}

static JSValue collision_resolve(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    AthenaShape a, b;
    float dx, dy;
    JSValue object;

    if (col_shape(ctx, col_arg(argc, argv, 0), &a, "Collision.resolve") ||
        col_shape(ctx, col_arg(argc, argv, 1), &b, "Collision.resolve"))
        return JS_EXCEPTION;
    if (!athena_collision_shape_resolve(&a, &b, &dx, &dy))
        return JS_NULL;
    object = JS_NewObject(ctx);
    if (JS_IsException(object) || col_put(ctx, object, ATOM_x, JS_NewFloat32(ctx, dx)) ||
        col_put(ctx, object, ATOM_y, JS_NewFloat32(ctx, dy))) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    return object;
}

static JSValue collision_segment(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv) {
    const char *name = "Collision.segment";
    AthenaShape shape;
    float p[4], t, nx, ny;
    JSValue object;
    int i;

    for (i = 0; i < 4; i++)
        if (col_number(ctx, col_arg(argc, argv, i), &p[i], -MAX_COORD, MAX_COORD, name))
            return JS_EXCEPTION;
    if (col_shape(ctx, col_arg(argc, argv, 4), &shape, name))
        return JS_EXCEPTION;
    if (!athena_collision_segment_shape(p[0], p[1], p[2], p[3], &shape, &t, &nx, &ny))
        return JS_NULL;
    object = JS_NewObject(ctx);
    if (JS_IsException(object) ||
        col_put(ctx, object, ATOM_fraction, JS_NewFloat32(ctx, t)) ||
        col_put(ctx, object, ATOM_x, JS_NewFloat32(ctx, p[0] + (p[2] - p[0]) * t)) ||
        col_put(ctx, object, ATOM_y, JS_NewFloat32(ctx, p[1] + (p[3] - p[1]) * t)) ||
        col_put(ctx, object, ATOM_normalX, JS_NewFloat32(ctx, nx)) ||
        col_put(ctx, object, ATOM_normalY, JS_NewFloat32(ctx, ny))) {
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    return object;
}

static const JSCFunctionListEntry collision_funcs[] = {
    JS_CFUNC_DEF("overlaps", 2, collision_overlaps),
    JS_CFUNC_DEF("resolve", 2, collision_resolve),
    JS_CFUNC_DEF("segment", 5, collision_segment),
};

/* ---------------------------------------------------------------------- */
/* Module                                                                 */

static JSValue collision_class(JSContext *ctx, JSClassID *class_id, const JSClassDef *def,
    JSCFunction *ctor, const char *name, const JSCFunctionListEntry *proto_funcs,
    int proto_count) {
    JSValue proto, constructor;

    if (athena_register_class(ctx, class_id, def) < 0)
        return JS_EXCEPTION;
    proto = JS_NewObject(ctx);
    if (JS_IsException(proto))
        return proto;
    JS_SetPropertyFunctionList(ctx, proto, proto_funcs, proto_count);
    constructor = JS_NewCFunction2(ctx, ctor, name, 1, JS_CFUNC_constructor, 0);
    if (JS_IsException(constructor)) {
        JS_FreeValue(ctx, proto);
        return constructor;
    }
    JS_SetConstructor(ctx, constructor, proto);
    JS_SetClassProto(ctx, *class_id, proto);
    return constructor;
}

static int collision_module_init(JSContext *ctx, JSModuleDef *module) {
    JSValue world, body;

    world = collision_class(ctx, &world_class_id, &world_class, world_ctor, "World",
        world_proto_funcs, countof(world_proto_funcs));
    if (JS_IsException(world) || JS_SetModuleExport(ctx, module, "World", world) < 0)
        return -1;
    body = collision_class(ctx, &body_class_id, &body_class, body_ctor, "Body",
        body_proto_funcs, countof(body_proto_funcs));
    if (JS_IsException(body) || JS_SetModuleExport(ctx, module, "Body", body) < 0)
        return -1;
    if (!state.ctx) {
        int i;

        for (i = 0; i < ATOM_COUNT; i++) {
            state.atoms[i] = JS_NewAtom(ctx, col_atom_names[i]);
            if (state.atoms[i] == JS_ATOM_NULL) {
                while (i-- > 0)
                    JS_FreeAtom(ctx, state.atoms[i]);
                return -1;
            }
        }
        state.rt = JS_GetRuntime(ctx);
        state.ctx = ctx;
        state.system_id = 0;
        state.auto_worlds = NULL;
    }
    return JS_SetModuleExportList(ctx, module, collision_funcs, countof(collision_funcs));
}

JSModuleDef *athena_collision_js_init(JSContext *ctx) {
    JSModuleDef *module = athena_push_module(ctx, collision_module_init, collision_funcs,
        countof(collision_funcs), "Collision");

    if (module) {
        JS_AddModuleExport(ctx, module, "World");
        JS_AddModuleExport(ctx, module, "Body");
    }
    return module;
}

void athena_collision_js_cleanup(JSContext *ctx) {
    int i;

    if (ctx != state.ctx)
        return;
    /* Scripts that still run (pending jobs) name properties by their strings. */
    for (i = 0; i < ATOM_COUNT; i++)
        JS_FreeAtom(ctx, state.atoms[i]);
    state.rt = NULL;
    if (state.system_id > 0)
        athena_loop_system_remove(state.system_id);
    state.system_id = 0;
    /* Worlds stay alive until the runtime frees them: forget the list only. */
    while (state.auto_worlds)
        world_unlink(state.auto_worlds);
    state.ctx = NULL;
}
