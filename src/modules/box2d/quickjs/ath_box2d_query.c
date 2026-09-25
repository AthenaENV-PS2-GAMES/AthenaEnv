#include <stdlib.h>

#include "ath_box2d_internal.h"

/* ------------------------------------------------------------------------ */
/* Result collection                                                         */
/* ------------------------------------------------------------------------ */

/*
 * Box2D callbacks only record ids and numbers; script objects are built after
 * the query returns, so an allocation failure never happens mid-query.
 */
typedef struct CastHit {
    b2ShapeId shape;
    b2Vec2 point;
    b2Vec2 normal;
    float fraction;
} CastHit;

typedef struct Hits {
    void *items;
    int count;
    int capacity;
    size_t size;
    bool out_of_memory;
} Hits;

static void *hits_push(Hits *hits) {
    if (hits->count == hits->capacity) {
        int capacity = hits->capacity ? hits->capacity * 2 : 16;
        void *items = realloc(hits->items, hits->size * (size_t)capacity);
        if (!items) {
            hits->out_of_memory = true;
            return NULL;
        }
        hits->items = items;
        hits->capacity = capacity;
    }
    return (char *)hits->items + hits->size * (size_t)hits->count++;
}

static bool overlap_collect(b2ShapeId shape, void *context) {
    b2ShapeId *slot = hits_push(context);

    if (!slot)
        return false;
    *slot = shape;
    return true;
}

static float cast_collect(b2ShapeId shape, b2Vec2 point, b2Vec2 normal, float fraction, void *context) {
    CastHit *hit = hits_push(context);

    if (!hit)
        return 0.0f; /* terminates the cast */
    hit->shape = shape;
    hit->point = point;
    hit->normal = normal;
    hit->fraction = fraction;
    return 1.0f; /* keep the full length: every hit is reported */
}

static int compare_fraction(const void *a, const void *b) {
    float fa = ((const CastHit *)a)->fraction, fb = ((const CastHit *)b)->fraction;
    return (fa > fb) - (fa < fb);
}

/* Shape[] from collected ids; frees the collection. */
static JSValue overlap_result(JSContext *ctx, B2JSWorld *world, Hits *hits) {
    const b2ShapeId *shapes = hits->items;
    JSValue array;

    if (hits->out_of_memory) {
        free(hits->items);
        return JS_ThrowOutOfMemory(ctx);
    }
    array = JS_NewArray(ctx);
    for (int i = 0; i < hits->count && !JS_IsException(array); i++) {
        JSValue shape = b2js_wrap_shape(ctx, world, shapes[i]);
        if (JS_IsException(shape)) {
            JS_FreeValue(ctx, array);
            array = JS_EXCEPTION;
        } else {
            JS_SetPropertyUint32(ctx, array, (uint32_t)i, shape);
        }
    }
    free(hits->items);
    return array;
}

static JSValue cast_hit_object(JSContext *ctx, B2JSWorld *world, b2ShapeId id, b2Vec2 point, b2Vec2 normal,
    float fraction) {
    JSValue shape = b2js_wrap_shape(ctx, world, id);
    JSValue object;

    if (JS_IsException(shape))
        return shape;
    object = JS_NewObject(ctx);
    if (JS_IsException(object)) {
        JS_FreeValue(ctx, shape);
        return object;
    }
    b2js_set(ctx, object, b2js_atoms.shape, shape);
    b2js_set(ctx, object, b2js_atoms.point, b2js_new_vec2(ctx, point));
    b2js_set(ctx, object, b2js_atoms.normal, b2js_new_vec2(ctx, normal));
    b2js_set(ctx, object, b2js_atoms.fraction, JS_NewFloat32(ctx, fraction));
    return object;
}

/* Hits sorted nearest first; frees the collection. */
static JSValue cast_result(JSContext *ctx, B2JSWorld *world, Hits *hits) {
    CastHit *items = hits->items;
    JSValue array;

    if (hits->out_of_memory) {
        free(hits->items);
        return JS_ThrowOutOfMemory(ctx);
    }
    if (hits->count > 1)
        qsort(items, (size_t)hits->count, sizeof(*items), compare_fraction);
    array = JS_NewArray(ctx);
    for (int i = 0; i < hits->count && !JS_IsException(array); i++) {
        JSValue hit = cast_hit_object(ctx, world, items[i].shape, items[i].point, items[i].normal,
            items[i].fraction);
        if (JS_IsException(hit)) {
            JS_FreeValue(ctx, array);
            array = JS_EXCEPTION;
        } else {
            JS_SetPropertyUint32(ctx, array, (uint32_t)i, hit);
        }
    }
    free(hits->items);
    return array;
}

/* Reads `count` coordinates (or translations) from argv[first...]. */
static int float_args(JSContext *ctx, JSValueConst *argv, int first, int count, const char *where,
    const char *const *names, float *out) {
    for (int i = 0; i < count; i++) {
        if (b2js_float(ctx, argv[first + i], where, names[i], B2JS_COORD, &out[i]) < 0)
            return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Rays and AABB                                                             */
/* ------------------------------------------------------------------------ */

enum {
    RAY_CLOSEST = 0,
    RAY_ALL,
};

static JSValue world_cast_ray(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    static const char *const names[] = { "originX", "originY", "translationX", "translationY" };
    const char *where = magic == RAY_CLOSEST ? "World.castRay" : "World.raycastAll";
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);
    b2QueryFilter filter;
    float v[4];

    if (!world || b2js_argc(ctx, argc, 4, 5, where) < 0 ||
        float_args(ctx, argv, 0, 4, where, names, v) < 0 ||
        b2js_query_filter(ctx, argc, argv, 4, where, &filter) < 0 ||
        b2js_world_still_alive(ctx, world, where) < 0)
        return JS_EXCEPTION;

    if (magic == RAY_CLOSEST) {
        b2RayResult result = b2World_CastRayClosest(world->id, (b2Vec2){ v[0], v[1] }, (b2Vec2){ v[2], v[3] }, filter);
        if (!result.hit)
            return JS_NULL;
        return cast_hit_object(ctx, world, result.shapeId, result.point, result.normal, result.fraction);
    } else {
        Hits hits = { .size = sizeof(CastHit) };
        b2World_CastRay(world->id, (b2Vec2){ v[0], v[1] }, (b2Vec2){ v[2], v[3] }, filter, cast_collect, &hits);
        return cast_result(ctx, world, &hits);
    }
}

static JSValue world_query_aabb(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.queryAABB";
    static const char *const names[] = { "lowerX", "lowerY", "upperX", "upperY" };
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);
    Hits hits = { .size = sizeof(b2ShapeId) };
    b2QueryFilter filter;
    b2AABB aabb;
    float v[4];

    if (!world || b2js_argc(ctx, argc, 4, 5, where) < 0 ||
        float_args(ctx, argv, 0, 4, where, names, v) < 0 ||
        b2js_query_filter(ctx, argc, argv, 4, where, &filter) < 0 ||
        b2js_world_still_alive(ctx, world, where) < 0)
        return JS_EXCEPTION;
    if (v[0] > v[2] || v[1] > v[3])
        return JS_ThrowRangeError(ctx, "%s: lower bounds must be <= upper bounds", where);

    aabb.lowerBound = (b2Vec2){ v[0], v[1] };
    aabb.upperBound = (b2Vec2){ v[2], v[3] };
    b2World_OverlapAABB(world->id, b2Pos_zero, aabb, filter, overlap_collect, &hits);
    return overlap_result(ctx, world, &hits);
}

/* ------------------------------------------------------------------------ */
/* Shape overlaps and casts                                                  */
/* ------------------------------------------------------------------------ */

/*
 * The proxy (convex hull of 1..8 points plus a radius) described by the
 * leading arguments of each overlap/cast method. Returns the index of the
 * first argument after the proxy, or -1 on error.
 */
enum {
    PROXY_POINTS = 0,  /* (points, radius?, ...): radius only when a number */
    PROXY_POLYGON,     /* (vertices, radius?, ...): same, 3+ vertices */
    PROXY_POINTS_R,    /* (points, radius, ...) */
    PROXY_POLYGON_R,   /* (vertices, radius, ...) */
    PROXY_CIRCLE,      /* (x, y, radius, ...) */
    PROXY_CAPSULE,     /* (x1, y1, x2, y2, radius, ...) */
};

static int proxy_args(JSContext *ctx, int argc, JSValueConst *argv, int kind, const char *where,
    b2ShapeProxy *proxy) {
    static const char *const circle_names[] = { "x", "y" };
    static const char *const capsule_names[] = { "x1", "y1", "x2", "y2" };
    b2Vec2 points[B2_MAX_POLYGON_VERTICES];
    float radius = 0.0f;
    int count, next;

    switch (kind) {
    case PROXY_CIRCLE: {
        float v[2];
        if (float_args(ctx, argv, 0, 2, where, circle_names, v) < 0 ||
            b2js_float(ctx, argv[2], where, "radius", B2JS_NONNEG, &radius) < 0)
            return -1;
        points[0] = (b2Vec2){ v[0], v[1] };
        *proxy = b2MakeProxy(points, 1, radius);
        return 3;
    }
    case PROXY_CAPSULE: {
        float v[4];
        if (float_args(ctx, argv, 0, 4, where, capsule_names, v) < 0 ||
            b2js_float(ctx, argv[4], where, "radius", B2JS_NONNEG, &radius) < 0)
            return -1;
        points[0] = (b2Vec2){ v[0], v[1] };
        points[1] = (b2Vec2){ v[2], v[3] };
        *proxy = b2MakeProxy(points, 2, radius);
        return 5;
    }
    default: {
        bool polygon = kind == PROXY_POLYGON || kind == PROXY_POLYGON_R;
        const char *what = polygon ? "vertices" : "points";

        if (b2js_points(ctx, argv[0], where, what, polygon ? 3 : 1, B2_MAX_POLYGON_VERTICES, points, &count) < 0)
            return -1;
        next = 1;
        if (kind == PROXY_POINTS_R || kind == PROXY_POLYGON_R) {
            if (b2js_float(ctx, argv[1], where, "radius", B2JS_NONNEG, &radius) < 0)
                return -1;
            next = 2;
        } else if (argc > 1 && (JS_IsNumber(argv[1]) || (JS_IsUndefined(argv[1]) && argc > 2))) {
            /* Optional radius: a number, or undefined followed by a filter. */
            if (!JS_IsUndefined(argv[1]) && b2js_float(ctx, argv[1], where, "radius", B2JS_NONNEG, &radius) < 0)
                return -1;
            next = 2;
        }
        *proxy = b2MakeProxy(points, count, radius);
        return next;
    }
    }
}

/* magic: proxy kind. Arguments: proxy, filter? */
static JSValue world_overlap(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    static const char *const names[] = {
        [PROXY_POINTS] = "World.overlapShape",
        [PROXY_POLYGON] = "World.overlapPolygon",
        [PROXY_CIRCLE] = "World.overlapCircle",
        [PROXY_CAPSULE] = "World.overlapCapsule",
    };
    static const int min_args[] = { [PROXY_POINTS] = 1, [PROXY_POLYGON] = 1, [PROXY_CIRCLE] = 3, [PROXY_CAPSULE] = 5 };
    const char *where = names[magic];
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);
    Hits hits = { .size = sizeof(b2ShapeId) };
    b2QueryFilter filter;
    b2ShapeProxy proxy;
    int next;

    /* The optional radius of point/polygon proxies adds one argument. */
    if (!world || b2js_argc(ctx, argc, min_args[magic], min_args[magic] + (magic <= PROXY_POLYGON ? 2 : 1), where) < 0)
        return JS_EXCEPTION;
    next = proxy_args(ctx, argc, argv, magic, where, &proxy);
    if (next < 0 || b2js_argc(ctx, argc, 0, next + 1, where) < 0 ||
        b2js_query_filter(ctx, argc, argv, next, where, &filter) < 0 ||
        b2js_world_still_alive(ctx, world, where) < 0)
        return JS_EXCEPTION;

    b2World_OverlapShape(world->id, b2Pos_zero, &proxy, filter, overlap_collect, &hits);
    return overlap_result(ctx, world, &hits);
}

/* magic: proxy kind. Arguments: proxy, translationX, translationY, filter? */
static JSValue world_cast_shape(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    static const char *const names[] = {
        [PROXY_POINTS_R] = "World.castShape",
        [PROXY_POLYGON_R] = "World.castPolygon",
        [PROXY_CIRCLE] = "World.castCircle",
        [PROXY_CAPSULE] = "World.castCapsule",
    };
    static const int proxy_argc[] = { [PROXY_POINTS_R] = 2, [PROXY_POLYGON_R] = 2, [PROXY_CIRCLE] = 3, [PROXY_CAPSULE] = 5 };
    static const char *const translation_names[] = { "translationX", "translationY" };
    const char *where = names[magic];
    int first = proxy_argc[magic];
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);
    Hits hits = { .size = sizeof(CastHit) };
    b2QueryFilter filter;
    b2ShapeProxy proxy;
    float t[2];

    if (!world || b2js_argc(ctx, argc, first + 2, first + 3, where) < 0 ||
        proxy_args(ctx, argc, argv, magic, where, &proxy) < 0 ||
        float_args(ctx, argv, first, 2, where, translation_names, t) < 0 ||
        b2js_query_filter(ctx, argc, argv, first + 2, where, &filter) < 0 ||
        b2js_world_still_alive(ctx, world, where) < 0)
        return JS_EXCEPTION;

    b2World_CastShape(world->id, b2Pos_zero, &proxy, (b2Vec2){ t[0], t[1] }, filter, cast_collect, &hits);
    return cast_result(ctx, world, &hits);
}

/* ------------------------------------------------------------------------ */
/* Character movers                                                          */
/* ------------------------------------------------------------------------ */

/* (x, y, x1, y1, x2, y2, radius): origin and capsule relative to it. */
static int mover_args(JSContext *ctx, JSValueConst *argv, const char *where, float min_radius,
    b2Vec2 *origin, b2Capsule *mover) {
    static const char *const names[] = { "x", "y", "x1", "y1", "x2", "y2" };
    float v[6];

    if (float_args(ctx, argv, 0, 6, where, names, v) < 0 ||
        b2js_float(ctx, argv[6], where, "radius", B2JS_POS, &mover->radius) < 0)
        return -1;
    if (!(mover->radius > min_radius)) {
        JS_ThrowRangeError(ctx, "%s: radius must be > %g", where, (double)min_radius);
        return -1;
    }
    *origin = (b2Vec2){ v[0], v[1] };
    mover->center1 = (b2Vec2){ v[2], v[3] };
    mover->center2 = (b2Vec2){ v[4], v[5] };
    return 0;
}

static JSValue world_cast_mover(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.castMover";
    static const char *const names[] = { "translationX", "translationY" };
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);
    b2QueryFilter filter;
    b2Capsule mover;
    b2Vec2 origin;
    float t[2];

    /* Box2D asserts radius > 2 * linear slop. */
    if (!world || b2js_argc(ctx, argc, 9, 10, where) < 0 ||
        mover_args(ctx, argv, where, 2.0f * athena_box2d_linear_slop(), &origin, &mover) < 0 ||
        float_args(ctx, argv, 7, 2, where, names, t) < 0 ||
        b2js_query_filter(ctx, argc, argv, 9, where, &filter) < 0 ||
        b2js_world_still_alive(ctx, world, where) < 0)
        return JS_EXCEPTION;
    return JS_NewFloat32(ctx, b2World_CastMover(world->id, origin, &mover, (b2Vec2){ t[0], t[1] }, filter));
}

typedef struct PlaneHit {
    b2ShapeId shape;
    b2PlaneResult result;
} PlaneHit;

static bool plane_collect(b2ShapeId shape, const b2PlaneResult *result, void *context) {
    PlaneHit *hit;

    /* Planes without a hit are to be ignored (Box2D). */
    if (!result->hit)
        return true;
    hit = hits_push(context);
    if (!hit)
        return false;
    hit->shape = shape;
    hit->result = *result;
    return true;
}

static JSValue world_collide_mover(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.collideMover";
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);
    Hits hits = { .size = sizeof(PlaneHit) };
    b2QueryFilter filter;
    b2Capsule mover;
    b2Vec2 origin;
    PlaneHit *items;
    JSValue array;

    if (!world || b2js_argc(ctx, argc, 7, 8, where) < 0 ||
        mover_args(ctx, argv, where, 0.0f, &origin, &mover) < 0 ||
        b2js_query_filter(ctx, argc, argv, 7, where, &filter) < 0 ||
        b2js_world_still_alive(ctx, world, where) < 0)
        return JS_EXCEPTION;

    b2World_CollideMover(world->id, origin, &mover, filter, plane_collect, &hits);
    if (hits.out_of_memory) {
        free(hits.items);
        return JS_ThrowOutOfMemory(ctx);
    }
    items = hits.items;
    array = JS_NewArray(ctx);
    for (int i = 0; i < hits.count && !JS_IsException(array); i++) {
        JSValue shape = b2js_wrap_shape(ctx, world, items[i].shape);
        JSValue plane = JS_IsException(shape) ? JS_EXCEPTION : JS_NewObject(ctx);

        if (JS_IsException(plane)) {
            JS_FreeValue(ctx, shape);
            JS_FreeValue(ctx, array);
            array = JS_EXCEPTION;
            break;
        }
        b2js_set(ctx, plane, b2js_atoms.shape, shape);
        b2js_set(ctx, plane, b2js_atoms.normal, b2js_new_vec2(ctx, items[i].result.plane.normal));
        b2js_set(ctx, plane, b2js_atoms.offset, JS_NewFloat32(ctx, items[i].result.plane.offset));
        b2js_set(ctx, plane, b2js_atoms.point, b2js_new_vec2(ctx, items[i].result.point));
        JS_SetPropertyUint32(ctx, array, (uint32_t)i, plane);
    }
    free(hits.items);
    return array;
}

#define B2JS_MAX_PLANES 64

/*
 * Planes from a JS array of { normal, offset, pushLimit?, clipVelocity? }
 * (collideMover results work as is). Returns the count or -1.
 */
static int plane_args(JSContext *ctx, JSValueConst value, const char *where, b2CollisionPlane *planes) {
    char name[48];
    int64_t length;
    int is_array = JS_IsArray(ctx, value);
    JSValue item;

    if (is_array < 0)
        return -1;
    if (!is_array) {
        JS_ThrowTypeError(ctx, "%s: planes must be an array of { normal, offset }", where);
        return -1;
    }
    item = JS_GetPropertyStr(ctx, value, "length");
    if (JS_IsException(item) || JS_ToInt64(ctx, &length, item) < 0) {
        JS_FreeValue(ctx, item);
        return -1;
    }
    JS_FreeValue(ctx, item);
    if (length > B2JS_MAX_PLANES) {
        JS_ThrowRangeError(ctx, "%s: at most %d planes", where, B2JS_MAX_PLANES);
        return -1;
    }

    for (int i = 0; i < (int)length; i++) {
        b2CollisionPlane *plane = &planes[i];
        int ret;

        plane->plane = (b2Plane){ { 0.0f, 0.0f }, 0.0f };
        plane->pushLimit = FLT_MAX;
        plane->push = 0.0f;
        plane->clipVelocity = true;
        item = JS_GetPropertyUint32(ctx, value, (uint32_t)i);
        if (JS_IsException(item))
            return -1;
        snprintf(name, sizeof(name), "%s planes[%d]", where, i);
        if (!JS_IsObject(item)) {
            JS_FreeValue(ctx, item);
            JS_ThrowTypeError(ctx, "%s must be an object { normal, offset }", name);
            return -1;
        }
        ret = b2js_opt_vec2(ctx, item, "normal", name, &plane->plane.normal);
        if (ret == 0)
            JS_ThrowTypeError(ctx, "%s: normal is required", name);
        if (ret > 0) {
            ret = b2js_opt_float(ctx, item, "offset", name, B2JS_COORD, &plane->plane.offset);
            if (ret == 0)
                JS_ThrowTypeError(ctx, "%s: offset is required", name);
        }
        /* push is written by solvePlanes and read by clipVector. */
        if (ret > 0 && (b2js_opt_float(ctx, item, "pushLimit", name, B2JS_NONNEG, &plane->pushLimit) < 0 ||
            b2js_opt_float(ctx, item, "push", name, B2JS_ANY, &plane->push) < 0 ||
            b2js_opt_bool(ctx, item, "clipVelocity", name, &plane->clipVelocity) < 0))
            ret = -1;
        JS_FreeValue(ctx, item);
        if (ret <= 0)
            return -1;
    }
    return (int)length;
}

/* Box2D.solvePlanes(dx, dy, planes): the mover translation that respects the planes. */
JSValue b2js_solve_planes(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Box2D.solvePlanes";
    b2CollisionPlane planes[B2JS_MAX_PLANES];
    b2PlaneSolverResult result;
    b2Vec2 delta;
    JSValue object;
    int count;

    if (b2js_argc(ctx, argc, 3, 3, where) < 0 ||
        b2js_float(ctx, argv[0], where, "dx", B2JS_COORD, &delta.x) < 0 ||
        b2js_float(ctx, argv[1], where, "dy", B2JS_COORD, &delta.y) < 0 ||
        (count = plane_args(ctx, argv[2], where, planes)) < 0)
        return JS_EXCEPTION;
    result = b2SolvePlanes(delta, planes, count);
    /* As in C, the planes carry the push that clipVector needs afterwards. */
    for (int i = 0; i < count; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, argv[2], (uint32_t)i);
        int ret = JS_IsObject(item) ? JS_SetPropertyStr(ctx, item, "push", JS_NewFloat32(ctx, planes[i].push)) : 0;
        JS_FreeValue(ctx, item);
        if (JS_IsException(item) || ret < 0)
            return JS_EXCEPTION;
    }
    object = JS_NewObject(ctx);
    if (JS_IsException(object))
        return object;
    b2js_set(ctx, object, b2js_atoms.x, JS_NewFloat32(ctx, result.translation.x));
    b2js_set(ctx, object, b2js_atoms.y, JS_NewFloat32(ctx, result.translation.y));
    JS_SetPropertyStr(ctx, object, "iterations", JS_NewInt32(ctx, result.iterationCount));
    return object;
}

/* Box2D.clipVector(vx, vy, planes): a velocity with the parts into the planes removed. */
JSValue b2js_clip_vector(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Box2D.clipVector";
    b2CollisionPlane planes[B2JS_MAX_PLANES];
    b2Vec2 vector;
    int count;

    if (b2js_argc(ctx, argc, 3, 3, where) < 0 ||
        b2js_float(ctx, argv[0], where, "vx", B2JS_ANY, &vector.x) < 0 ||
        b2js_float(ctx, argv[1], where, "vy", B2JS_ANY, &vector.y) < 0 ||
        (count = plane_args(ctx, argv[2], where, planes)) < 0)
        return JS_EXCEPTION;
    return b2js_new_vec2(ctx, b2ClipVector(vector, planes, count));
}

/* ------------------------------------------------------------------------ */
/* Events                                                                    */
/* ------------------------------------------------------------------------ */

static int array_push(JSContext *ctx, JSValue *array, uint32_t index, JSValue item);

static JSValue world_get_joint_events(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.getJointEvents";
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);
    b2JointEvents events;
    JSValue array;

    if (!world || b2js_argc(ctx, argc, 0, 0, where) < 0)
        return JS_EXCEPTION;
    events = b2World_GetJointEvents(world->id);
    array = JS_NewArray(ctx);
    for (int i = 0, count = 0; i < events.count && !JS_IsException(array); i++) {
        JSValue joint, item;

        /* Joints destroyed since the step are left out (their user data may be freed). */
        if (!b2Joint_IsValid(events.jointEvents[i].jointId))
            continue;
        joint = b2js_wrap_joint(ctx, world, events.jointEvents[i].jointId);
        item = JS_IsException(joint) ? JS_EXCEPTION : JS_NewObject(ctx);
        if (JS_IsException(item))
            JS_FreeValue(ctx, joint);
        else
            b2js_set(ctx, item, b2js_atoms.joint, joint);
        array_push(ctx, &array, (uint32_t)count++, item);
    }
    return array;
}

/* { keyA: shapeA, keyB: shapeB }; shapes destroyed since the step are null. */
static JSValue shape_pair(JSContext *ctx, B2JSWorld *world, JSAtom key_a, b2ShapeId a,
    JSAtom key_b, b2ShapeId b) {
    JSValue object = JS_NewObject(ctx);
    JSValue shape_a, shape_b;

    if (JS_IsException(object))
        return object;
    shape_a = b2js_wrap_shape(ctx, world, a);
    shape_b = JS_IsException(shape_a) ? JS_UNDEFINED : b2js_wrap_shape(ctx, world, b);
    if (JS_IsException(shape_a) || JS_IsException(shape_b)) {
        JS_FreeValue(ctx, shape_a);
        JS_FreeValue(ctx, object);
        return JS_EXCEPTION;
    }
    b2js_set(ctx, object, key_a, shape_a);
    b2js_set(ctx, object, key_b, shape_b);
    return object;
}

/* Appends `item` (taken) to `array`; frees the array and returns -1 on exception. */
static int array_push(JSContext *ctx, JSValue *array, uint32_t index, JSValue item) {
    if (JS_IsException(item)) {
        JS_FreeValue(ctx, *array);
        *array = JS_EXCEPTION;
        return -1;
    }
    JS_SetPropertyUint32(ctx, *array, index, item);
    return 0;
}

static JSValue world_get_contact_events(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.getContactEvents";
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);
    b2ContactEvents events;
    JSValue begin, end, hit, result;

    if (!world || b2js_argc(ctx, argc, 0, 0, where) < 0)
        return JS_EXCEPTION;
    events = b2World_GetContactEvents(world->id);

    begin = JS_NewArray(ctx);
    for (int i = 0; i < events.beginCount && !JS_IsException(begin); i++) {
        const b2ContactBeginTouchEvent *e = &events.beginEvents[i];
        array_push(ctx, &begin, (uint32_t)i, shape_pair(ctx, world, b2js_atoms.shapeA, e->shapeIdA, b2js_atoms.shapeB, e->shapeIdB));
    }
    end = JS_NewArray(ctx);
    for (int i = 0; i < events.endCount && !JS_IsException(end); i++) {
        const b2ContactEndTouchEvent *e = &events.endEvents[i];
        array_push(ctx, &end, (uint32_t)i, shape_pair(ctx, world, b2js_atoms.shapeA, e->shapeIdA, b2js_atoms.shapeB, e->shapeIdB));
    }
    hit = JS_NewArray(ctx);
    for (int i = 0; i < events.hitCount && !JS_IsException(hit); i++) {
        const b2ContactHitEvent *e = &events.hitEvents[i];
        JSValue item = shape_pair(ctx, world, b2js_atoms.shapeA, e->shapeIdA, b2js_atoms.shapeB, e->shapeIdB);
        if (!JS_IsException(item)) {
            b2js_set(ctx, item, b2js_atoms.point, b2js_new_vec2(ctx, e->point));
            b2js_set(ctx, item, b2js_atoms.normal, b2js_new_vec2(ctx, e->normal));
            b2js_set(ctx, item, b2js_atoms.approachSpeed, JS_NewFloat32(ctx, e->approachSpeed));
        }
        array_push(ctx, &hit, (uint32_t)i, item);
    }

    result = JS_NewObject(ctx);
    if (JS_IsException(begin) || JS_IsException(end) || JS_IsException(hit) || JS_IsException(result)) {
        JS_FreeValue(ctx, begin);
        JS_FreeValue(ctx, end);
        JS_FreeValue(ctx, hit);
        JS_FreeValue(ctx, result);
        return JS_EXCEPTION;
    }
    JS_SetPropertyStr(ctx, result, "begin", begin);
    JS_SetPropertyStr(ctx, result, "end", end);
    JS_SetPropertyStr(ctx, result, "hit", hit);
    return result;
}

static JSValue world_get_sensor_events(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.getSensorEvents";
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);
    b2SensorEvents events;
    JSValue begin, end, result;

    if (!world || b2js_argc(ctx, argc, 0, 0, where) < 0)
        return JS_EXCEPTION;
    events = b2World_GetSensorEvents(world->id);

    begin = JS_NewArray(ctx);
    for (int i = 0; i < events.beginCount && !JS_IsException(begin); i++) {
        const b2SensorBeginTouchEvent *e = &events.beginEvents[i];
        array_push(ctx, &begin, (uint32_t)i,
            shape_pair(ctx, world, b2js_atoms.sensor, e->sensorShapeId, b2js_atoms.visitor, e->visitorShapeId));
    }
    end = JS_NewArray(ctx);
    for (int i = 0; i < events.endCount && !JS_IsException(end); i++) {
        const b2SensorEndTouchEvent *e = &events.endEvents[i];
        array_push(ctx, &end, (uint32_t)i,
            shape_pair(ctx, world, b2js_atoms.sensor, e->sensorShapeId, b2js_atoms.visitor, e->visitorShapeId));
    }

    result = JS_NewObject(ctx);
    if (JS_IsException(begin) || JS_IsException(end) || JS_IsException(result)) {
        JS_FreeValue(ctx, begin);
        JS_FreeValue(ctx, end);
        JS_FreeValue(ctx, result);
        return JS_EXCEPTION;
    }
    JS_SetPropertyStr(ctx, result, "begin", begin);
    JS_SetPropertyStr(ctx, result, "end", end);
    return result;
}

static JSValue world_get_body_events(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.getBodyEvents";
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);
    b2BodyEvents events;
    JSValue array;

    if (!world || b2js_argc(ctx, argc, 0, 0, where) < 0)
        return JS_EXCEPTION;
    events = b2World_GetBodyEvents(world->id);

    array = JS_NewArray(ctx);
    for (int i = 0, count = 0; i < events.moveCount && !JS_IsException(array); i++) {
        const b2BodyMoveEvent *e = &events.moveEvents[i];
        const B2JSHandle *handle;
        JSValue body, item;

        /* Bodies destroyed since the step are still listed: skip them. */
        if (!b2Body_IsValid(e->bodyId))
            continue;
        /* The event's userData was recorded at the step and may be stale
         * (e.g. after World.restore); the body's current user data is its
         * live handle. */
        handle = b2Body_GetUserData(e->bodyId);
        body = handle ? JS_DupValue(ctx, handle->object) : b2js_wrap_body(ctx, world, e->bodyId);
        item = JS_IsException(body) ? JS_EXCEPTION : JS_NewObject(ctx);

        if (JS_IsException(item)) {
            JS_FreeValue(ctx, body);
        } else {
            b2js_set(ctx, item, b2js_atoms.body, body);
            b2js_set(ctx, item, b2js_atoms.x, JS_NewFloat32(ctx, e->transform.p.x));
            b2js_set(ctx, item, b2js_atoms.y, JS_NewFloat32(ctx, e->transform.p.y));
            b2js_set(ctx, item, b2js_atoms.angle, JS_NewFloat32(ctx, b2Rot_GetAngle(e->transform.q)));
            b2js_set(ctx, item, b2js_atoms.fellAsleep, JS_NewBool(ctx, e->fellAsleep));
        }
        array_push(ctx, &array, (uint32_t)count++, item);
    }
    return array;
}

static JSValue world_explode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.explode";
    static const char *const names[] = { "x", "y" };
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);
    b2ExplosionDef def = b2DefaultExplosionDef();
    float position[2];

    if (!world || b2js_argc(ctx, argc, 5, 6, where) < 0 ||
        float_args(ctx, argv, 0, 2, where, names, position) < 0 ||
        b2js_float(ctx, argv[2], where, "radius", B2JS_NONNEG, &def.radius) < 0 ||
        b2js_float(ctx, argv[3], where, "falloff", B2JS_NONNEG, &def.falloff) < 0 ||
        b2js_float(ctx, argv[4], where, "impulsePerLength", B2JS_ANY, &def.impulsePerLength) < 0 ||
        (b2js_has(argc, argv, 5) && b2js_bits(ctx, argv[5], where, "maskBits", &def.maskBits) < 0))
        return JS_EXCEPTION;
    def.position = (b2Vec2){ position[0], position[1] };
    b2World_Explode(world->id, &def);
    return JS_UNDEFINED;
}

const JSCFunctionListEntry b2js_world_query_funcs[] = {
    JS_CFUNC_MAGIC_DEF("castRay", 5, world_cast_ray, RAY_CLOSEST),
    JS_CFUNC_MAGIC_DEF("raycastAll", 5, world_cast_ray, RAY_ALL),
    JS_CFUNC_DEF("queryAABB", 5, world_query_aabb),
    JS_CFUNC_MAGIC_DEF("overlapShape", 3, world_overlap, PROXY_POINTS),
    JS_CFUNC_MAGIC_DEF("overlapPolygon", 3, world_overlap, PROXY_POLYGON),
    JS_CFUNC_MAGIC_DEF("overlapCircle", 4, world_overlap, PROXY_CIRCLE),
    JS_CFUNC_MAGIC_DEF("overlapCapsule", 6, world_overlap, PROXY_CAPSULE),
    JS_CFUNC_MAGIC_DEF("castShape", 5, world_cast_shape, PROXY_POINTS_R),
    JS_CFUNC_MAGIC_DEF("castPolygon", 5, world_cast_shape, PROXY_POLYGON_R),
    JS_CFUNC_MAGIC_DEF("castCircle", 6, world_cast_shape, PROXY_CIRCLE),
    JS_CFUNC_MAGIC_DEF("castCapsule", 8, world_cast_shape, PROXY_CAPSULE),
    JS_CFUNC_DEF("castMover", 10, world_cast_mover),
    JS_CFUNC_DEF("collideMover", 8, world_collide_mover),
    JS_CFUNC_DEF("getContactEvents", 0, world_get_contact_events),
    JS_CFUNC_DEF("getSensorEvents", 0, world_get_sensor_events),
    JS_CFUNC_DEF("getBodyEvents", 0, world_get_body_events),
    JS_CFUNC_DEF("getJointEvents", 0, world_get_joint_events),
    JS_CFUNC_DEF("explode", 6, world_explode),
};
const int b2js_world_query_funcs_count = countof(b2js_world_query_funcs);
