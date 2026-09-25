#ifndef ATH_BOX2D_INTERNAL_H
#define ATH_BOX2D_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include <ath_env.h>
#include <athena/box2d.h>

/*
 * Ownership
 *
 * A World object owns a B2JSWorld: the Box2D world and one handle per
 * Body/Shape/Joint/Chain object handed to scripts. While its Box2D object
 * exists, a handle is linked in the world, is the Box2D user data (bodies,
 * shapes, joints), and holds one reference to its script object and to the
 * script's user data. So each Box2D object has a single script object, found
 * in O(1) from its id, and `shape.getBody() === body`.
 *
 * Each script object holds its World in a read-only `world` property and the
 * world marks its handles (gc_mark): the cycle keeps the world alive while a
 * script holds any of its objects, and lets the GC collect everything once
 * none is reachable.
 *
 * Destroying a Box2D object (or its world) marks the handle dead and drops its
 * references. The script object keeps the dead handle, so later calls report
 * "destroyed", and frees it in its finalizer.
 */

typedef enum B2JSKind {
    B2JS_BODY = 0,
    B2JS_SHAPE,
    B2JS_JOINT,
    B2JS_CHAIN,
    B2JS_KIND_COUNT
} B2JSKind;

typedef struct B2JSWorld B2JSWorld;

typedef struct B2JSHandle {
    B2JSKind kind;
    bool dead;
    union {
        b2BodyId body;
        b2ShapeId shape;
        b2JointId joint;
        b2ChainId chain;
    } id;
    B2JSWorld *world;
    JSValue object;    /* owned while alive */
    JSValue user_data; /* owned */
    struct B2JSHandle *prev;
    struct B2JSHandle *next;
} B2JSHandle;

struct B2JSWorld {
    b2WorldId id;         /* b2_nullWorldId once destroyed */
    JSValue object;       /* the World object, not a reference */
    JSValue user_data;    /* owned */
    B2JSHandle *handles;  /* live body, shape and joint handles */
    B2JSHandle *chains;   /* live chain handles, searched by id (Box2D has no chain user data) */
};

extern JSClassID b2js_world_class_id;
extern JSClassID b2js_class_ids[B2JS_KIND_COUNT];

/*
 * Property names of the result objects, created at module initialization
 * (atoms belong to the runtime) instead of being looked up from a C string
 * on every set.
 */
#define B2JS_ATOM_LIST(X) \
    X(x) X(y) X(angle) X(body) X(shape) X(point) X(normal) X(fraction) X(fellAsleep) \
    X(shapeA) X(shapeB) X(sensor) X(visitor) X(approachSpeed) X(joint) X(separation) \
    X(normalImpulse) X(points) X(offset)

typedef struct B2JSAtoms {
#define B2JS_ATOM_FIELD(name) JSAtom name;
    B2JS_ATOM_LIST(B2JS_ATOM_FIELD)
#undef B2JS_ATOM_FIELD
} B2JSAtoms;

extern B2JSAtoms b2js_atoms;

/* Adds a property to a new result object; takes `value`. */
static inline void b2js_set(JSContext *ctx, JSValueConst object, JSAtom name, JSValue value) {
    JS_DefinePropertyValue(ctx, object, name, value, JS_PROP_C_W_E);
}

/* Throws a RangeError unless Box2D may allocate more (see athena_box2d_memory_available). */
int b2js_check_memory(JSContext *ctx, const char *where);

/* ------------------------------------------------------------------------ */
/* Arguments (ath_box2d.c). Return 0 on success, -1 with an exception set.   */
/* ------------------------------------------------------------------------ */

/* Every number is finite; scalars within +-1e10, coordinates within B2_HUGE. */
typedef enum B2JSRange {
    B2JS_ANY = 0,  /* any scalar */
    B2JS_NONNEG,   /* >= 0 */
    B2JS_POS,      /* > 0 */
    B2JS_COORD,    /* a coordinate or distance in meters, within +-B2_HUGE */
} B2JSRange;

int b2js_argc(JSContext *ctx, int argc, int min, int max, const char *where);
/* The argument exists and is not undefined. */
bool b2js_has(int argc, JSValueConst *argv, int index);

int b2js_float(JSContext *ctx, JSValueConst value, const char *where, const char *what,
    B2JSRange range, float *out);
int b2js_int(JSContext *ctx, JSValueConst value, const char *where, const char *what,
    int min, int max, int *out);
/* Booleans; numbers are accepted as 0 / non-zero. */
int b2js_bool(JSContext *ctx, JSValueConst value, const char *where, const char *what, bool *out);
/* { x, y } with finite numbers. */
int b2js_vec2(JSContext *ctx, JSValueConst value, const char *where, const char *what, b2Vec2 *out);
/* 64-bit masks: an integer number (negative = two's complement) or a BigInt. */
int b2js_bits(JSContext *ctx, JSValueConst value, const char *where, const char *what, uint64_t *out);
/* Array of { x, y }: count in [min, max], into out[max]. */
int b2js_points(JSContext *ctx, JSValueConst value, const char *where, const char *what,
    int min, int max, b2Vec2 *out, int *count);
/* Same, any length >= min, allocated with malloc (caller frees). */
b2Vec2 *b2js_points_alloc(JSContext *ctx, JSValueConst value, const char *where, const char *what,
    int min, int *count);

/* argv[index] as an options object: 1 = object, 0 = absent or undefined, -1 = error. */
int b2js_options(JSContext *ctx, int argc, JSValueConst *argv, int index, const char *where);

/* options[key]: 1 = read into *out, 0 = absent (out unchanged), -1 = error. */
int b2js_opt_float(JSContext *ctx, JSValueConst options, const char *key, const char *where,
    B2JSRange range, float *out);
int b2js_opt_int(JSContext *ctx, JSValueConst options, const char *key, const char *where,
    int min, int max, int *out);
int b2js_opt_bool(JSContext *ctx, JSValueConst options, const char *key, const char *where, bool *out);
int b2js_opt_vec2(JSContext *ctx, JSValueConst options, const char *key, const char *where, b2Vec2 *out);
/* Returns the value (owned) or JS_UNDEFINED; JS_EXCEPTION on error. */
JSValue b2js_opt_value(JSContext *ctx, JSValueConst options, const char *key);

/* { categoryBits?, maskBits?, groupIndex? } merged into *filter. */
int b2js_filter(JSContext *ctx, JSValueConst value, const char *where, b2Filter *filter);
/* Optional query filter at argv[index] ({ categoryBits?, maskBits? }). */
int b2js_query_filter(JSContext *ctx, int argc, JSValueConst *argv, int index, const char *where,
    b2QueryFilter *filter);
/* Shape options shared by every create*Shape (density, material, events, filter). */
int b2js_shape_def(JSContext *ctx, JSValueConst options, const char *where, b2ShapeDef *def);
/*
 * Surface material fields present in options (friction, restitution,
 * rollingResistance, tangentSpeed, userMaterialId, customColor) into *material.
 */
int b2js_material(JSContext *ctx, JSValueConst options, const char *where, b2SurfaceMaterial *material);
JSValue b2js_new_material(JSContext *ctx, b2SurfaceMaterial material);
/* A body name: a string, cut to B2_NAME_LENGTH bytes at a UTF-8 character boundary. */
int b2js_name(JSContext *ctx, JSValueConst value, const char *where, char out[B2_NAME_LENGTH + 1]);

/* ------------------------------------------------------------------------ */
/* Values                                                                    */
/* ------------------------------------------------------------------------ */

JSValue b2js_new_vec2(JSContext *ctx, b2Vec2 v);
JSValue b2js_new_aabb(JSContext *ctx, b2AABB aabb);
/* A number when exact (two's complement), a BigInt otherwise. */
JSValue b2js_new_bits(JSContext *ctx, uint64_t bits);
JSValue b2js_new_filter(JSContext *ctx, b2Filter filter);

/* ------------------------------------------------------------------------ */
/* Handles                                                                   */
/* ------------------------------------------------------------------------ */

/* Live world behind `this`, or NULL with an exception set. */
B2JSWorld *b2js_this_world(JSContext *ctx, JSValueConst this_val, const char *where);
/* Live handle of the given kind, or NULL with a TypeError set. */
B2JSHandle *b2js_handle(JSContext *ctx, JSValueConst value, B2JSKind kind, const char *where);
/* The handle (live or dead) behind value, NULL when it is not of this kind. */
B2JSHandle *b2js_handle_any(JSValueConst value, B2JSKind kind);

/* The script object for a Box2D object (created on first use); null when the id is invalid. */
JSValue b2js_wrap_body(JSContext *ctx, B2JSWorld *world, b2BodyId id);
JSValue b2js_wrap_shape(JSContext *ctx, B2JSWorld *world, b2ShapeId id);
JSValue b2js_wrap_joint(JSContext *ctx, B2JSWorld *world, b2JointId id);
JSValue b2js_wrap_chain(JSContext *ctx, B2JSWorld *world, b2ChainId id);

/*
 * Reading arguments can run script code (getters of option objects, array
 * elements), which may destroy the objects a call works on. These re-check,
 * right before the Box2D call, what the call validated at its start.
 */
static inline int b2js_still_alive(JSContext *ctx, const B2JSHandle *handle, const char *where) {
    if (!handle->dead)
        return 0;
    JS_ThrowTypeError(ctx, "%s: the object was destroyed while its arguments were read", where);
    return -1;
}

static inline int b2js_world_still_alive(JSContext *ctx, const B2JSWorld *world, const char *where) {
    if (!B2_IS_NULL(world->id))
        return 0;
    JS_ThrowTypeError(ctx, "%s: the World was destroyed while its arguments were read", where);
    return -1;
}

/* Replaces the user data held by a live handle. */
void b2js_set_user_data(JSContext *ctx, B2JSHandle *handle, JSValueConst value);

/*
 * Marks a handle dead and releases its references. Call before the Box2D
 * object is destroyed. The handle may be freed by this call.
 */
void b2js_kill(JSRuntime *rt, B2JSHandle *handle);
/*
 * Kills the handles of a body's shapes (chain segments included) and joints,
 * which b2DestroyBody destroys with it. -1 with an exception when out of memory.
 */
int b2js_kill_body_attachments(JSContext *ctx, b2BodyId body);
/* Kills chain handles whose Box2D chain no longer exists. */
void b2js_kill_stale_chains(JSRuntime *rt, B2JSWorld *world);

/* ------------------------------------------------------------------------ */
/* Classes and world methods implemented in the other files                  */
/* ------------------------------------------------------------------------ */

/* Sets a class prototype with the given methods. */
int b2js_class_proto(JSContext *ctx, JSClassID class_id, const JSCFunctionListEntry *funcs, int count);

int b2js_body_init(JSContext *ctx);
int b2js_shape_init(JSContext *ctx);
int b2js_joint_init(JSContext *ctx);

extern const JSCFunctionListEntry b2js_world_joint_funcs[];
extern const int b2js_world_joint_funcs_count;
extern const JSCFunctionListEntry b2js_world_query_funcs[];
extern const int b2js_world_query_funcs_count;

/*
 * Shape geometry from an options object, validated as Box2D requires
 * (ath_box2d_body.c): { radius, center? }, { halfWidth, halfHeight, center?,
 * angle? }, { vertices, radius? }, { point1, point2, radius }, { point1, point2 }.
 */
int b2js_circle(JSContext *ctx, JSValueConst options, const char *where, b2Circle *circle);
int b2js_box(JSContext *ctx, JSValueConst options, const char *where, b2Polygon *box);
int b2js_polygon(JSContext *ctx, JSValueConst options, const char *where, b2Polygon *polygon);
int b2js_capsule(JSContext *ctx, JSValueConst options, const char *where, b2Capsule *capsule);
int b2js_segment(JSContext *ctx, JSValueConst options, const char *where, b2Segment *segment);

/* { mass, center, rotationalInertia } (ath_box2d_body.c). */
JSValue b2js_new_mass_data(JSContext *ctx, b2MassData data);

/* [{ shapeA, shapeB, normal, points: [{ point, separation, normalImpulse }] }] (ath_box2d_body.c). */
JSValue b2js_contact_array(JSContext *ctx, B2JSWorld *world, const b2ContactData *contacts, int count);

/* Module functions of the character mover (ath_box2d_query.c). */
JSValue b2js_solve_planes(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue b2js_clip_vector(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

#endif /* ATH_BOX2D_INTERNAL_H */
