#include <stdlib.h>

#include "ath_box2d_internal.h"

/* The live shape behind `this` and its id; returns on error. */
#define SHAPE_THIS(where, min, max)                                              \
    B2JSHandle *handle = b2js_handle(ctx, this_val, B2JS_SHAPE, where);          \
    if (!handle || b2js_argc(ctx, argc, min, max, where) < 0)                    \
        return JS_EXCEPTION;                                                     \
    b2ShapeId id = handle->id.shape

#define CHAIN_THIS(where, min, max)                                              \
    B2JSHandle *handle = b2js_handle(ctx, this_val, B2JS_CHAIN, where);          \
    if (!handle || b2js_argc(ctx, argc, min, max, where) < 0)                    \
        return JS_EXCEPTION;                                                     \
    b2ChainId id = handle->id.chain

/* ------------------------------------------------------------------------ */
/* Shape scalars                                                             */
/* ------------------------------------------------------------------------ */

enum {
    SHAPE_FRICTION = 0,
    SHAPE_RESTITUTION,
    SHAPE_DENSITY,
    SHAPE_SENSOR,
    SHAPE_SENSOR_EVENTS,
    SHAPE_CONTACT_EVENTS,
    SHAPE_HIT_EVENTS,
};

static const struct {
    const char *get;
    const char *set;
} shape_scalars[] = {
    [SHAPE_FRICTION] = { "Shape.getFriction", "Shape.setFriction" },
    [SHAPE_RESTITUTION] = { "Shape.getRestitution", "Shape.setRestitution" },
    [SHAPE_DENSITY] = { "Shape.getDensity", "Shape.setDensity" },
    [SHAPE_SENSOR] = { "Shape.isSensor", NULL },
    [SHAPE_SENSOR_EVENTS] = { "Shape.areSensorEventsEnabled", "Shape.enableSensorEvents" },
    [SHAPE_CONTACT_EVENTS] = { "Shape.areContactEventsEnabled", "Shape.enableContactEvents" },
    [SHAPE_HIT_EVENTS] = { "Shape.areHitEventsEnabled", "Shape.enableHitEvents" },
};

static JSValue shape_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    SHAPE_THIS(shape_scalars[magic].get, 0, 0);

    switch (magic) {
        case SHAPE_FRICTION: return JS_NewFloat32(ctx, b2Shape_GetFriction(id));
        case SHAPE_RESTITUTION: return JS_NewFloat32(ctx, b2Shape_GetRestitution(id));
        case SHAPE_DENSITY: return JS_NewFloat32(ctx, b2Shape_GetDensity(id));
        case SHAPE_SENSOR: return JS_NewBool(ctx, b2Shape_IsSensor(id));
        case SHAPE_SENSOR_EVENTS: return JS_NewBool(ctx, b2Shape_AreSensorEventsEnabled(id));
        case SHAPE_CONTACT_EVENTS: return JS_NewBool(ctx, b2Shape_AreContactEventsEnabled(id));
        case SHAPE_HIT_EVENTS: return JS_NewBool(ctx, b2Shape_AreHitEventsEnabled(id));
        default: return JS_UNDEFINED;
    }
}

static JSValue shape_set(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    const char *where = shape_scalars[magic].set;
    /* setDensity(density, updateBodyMass = true) */
    SHAPE_THIS(where, 1, magic == SHAPE_DENSITY ? 2 : 1);
    bool flag, update_mass = true;
    float value;

    switch (magic) {
    case SHAPE_SENSOR_EVENTS:
    case SHAPE_CONTACT_EVENTS:
    case SHAPE_HIT_EVENTS:
        if (b2js_bool(ctx, argv[0], where, "flag", &flag) < 0)
            return JS_EXCEPTION;
        if (magic == SHAPE_SENSOR_EVENTS)
            b2Shape_EnableSensorEvents(id, flag);
        else if (magic == SHAPE_CONTACT_EVENTS)
            b2Shape_EnableContactEvents(id, flag);
        else
            b2Shape_EnableHitEvents(id, flag);
        return JS_UNDEFINED;
    default:
        if (b2js_float(ctx, argv[0], where, "value", B2JS_NONNEG, &value) < 0)
            return JS_EXCEPTION;
        if (magic == SHAPE_FRICTION) {
            b2Shape_SetFriction(id, value);
        } else if (magic == SHAPE_RESTITUTION) {
            b2Shape_SetRestitution(id, value);
        } else {
            if (b2js_has(argc, argv, 1) && b2js_bool(ctx, argv[1], where, "updateBodyMass", &update_mass) < 0)
                return JS_EXCEPTION;
            b2Shape_SetDensity(id, value, update_mass);
        }
        return JS_UNDEFINED;
    }
}

/* ------------------------------------------------------------------------ */
/* Shape queries and geometry                                                */
/* ------------------------------------------------------------------------ */

static JSValue shape_get_type(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    SHAPE_THIS("Shape.getType", 0, 0);

    return JS_NewString(ctx, athena_box2d_shape_type_name(b2Shape_GetType(id)));
}

static JSValue shape_get_body(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    SHAPE_THIS("Shape.getBody", 0, 0);

    return b2js_wrap_body(ctx, handle->world, b2Shape_GetBody(id));
}

static JSValue shape_get_filter(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    SHAPE_THIS("Shape.getFilter", 0, 0);

    return b2js_new_filter(ctx, b2Shape_GetFilter(id));
}

static JSValue shape_set_filter(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Shape.setFilter";
    SHAPE_THIS(where, 1, 1);
    /* Fields left out keep their current value. */
    b2Filter filter = b2Shape_GetFilter(id);

    if (b2js_filter(ctx, argv[0], where, &filter) < 0 || b2js_still_alive(ctx, handle, where) < 0)
        return JS_EXCEPTION;
    b2Shape_SetFilter(id, filter);
    return JS_UNDEFINED;
}

enum {
    SHAPE_TEST_POINT = 0,
    SHAPE_CLOSEST_POINT,
};

static JSValue shape_point_query(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    const char *where = magic == SHAPE_TEST_POINT ? "Shape.testPoint" : "Shape.getClosestPoint";
    SHAPE_THIS(where, 2, 2);
    b2Vec2 point;

    if (b2js_float(ctx, argv[0], where, "x", B2JS_COORD, &point.x) < 0 ||
        b2js_float(ctx, argv[1], where, "y", B2JS_COORD, &point.y) < 0)
        return JS_EXCEPTION;
    if (magic == SHAPE_TEST_POINT)
        return JS_NewBool(ctx, b2Shape_TestPoint(id, point));
    return b2js_new_vec2(ctx, b2Shape_GetClosestPoint(id, point));
}

static JSValue shape_get_aabb(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    SHAPE_THIS("Shape.getAABB", 0, 0);

    return b2js_new_aabb(ctx, b2Shape_GetAABB(id));
}

/* Throws unless the shape has the expected type. */
static int shape_require(JSContext *ctx, b2ShapeId id, b2ShapeType type, const char *where) {
    b2ShapeType actual = b2Shape_GetType(id);

    if (actual == type)
        return 0;
    JS_ThrowTypeError(ctx, "%s: the shape is a %s, not a %s", where,
        athena_box2d_shape_type_name(actual), athena_box2d_shape_type_name(type));
    return -1;
}

static JSValue segment_object(JSContext *ctx, b2Segment segment) {
    JSValue object = JS_NewObject(ctx);

    if (JS_IsException(object))
        return object;
    JS_SetPropertyStr(ctx, object, "point1", b2js_new_vec2(ctx, segment.point1));
    JS_SetPropertyStr(ctx, object, "point2", b2js_new_vec2(ctx, segment.point2));
    return object;
}

static JSValue shape_get_circle(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Shape.getCircle";
    SHAPE_THIS(where, 0, 0);
    b2Circle circle;
    JSValue object;

    if (shape_require(ctx, id, b2_circleShape, where) < 0)
        return JS_EXCEPTION;
    circle = b2Shape_GetCircle(id);
    object = JS_NewObject(ctx);
    if (JS_IsException(object))
        return object;
    JS_SetPropertyStr(ctx, object, "center", b2js_new_vec2(ctx, circle.center));
    JS_SetPropertyStr(ctx, object, "radius", JS_NewFloat32(ctx, circle.radius));
    return object;
}

static JSValue shape_get_capsule(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Shape.getCapsule";
    SHAPE_THIS(where, 0, 0);
    b2Capsule capsule;
    JSValue object;

    if (shape_require(ctx, id, b2_capsuleShape, where) < 0)
        return JS_EXCEPTION;
    capsule = b2Shape_GetCapsule(id);
    object = JS_NewObject(ctx);
    if (JS_IsException(object))
        return object;
    JS_SetPropertyStr(ctx, object, "center1", b2js_new_vec2(ctx, capsule.center1));
    JS_SetPropertyStr(ctx, object, "center2", b2js_new_vec2(ctx, capsule.center2));
    JS_SetPropertyStr(ctx, object, "radius", JS_NewFloat32(ctx, capsule.radius));
    return object;
}

static JSValue shape_get_polygon(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Shape.getPolygon";
    SHAPE_THIS(where, 0, 0);
    b2Polygon polygon;
    JSValue object, vertices, normals;

    if (shape_require(ctx, id, b2_polygonShape, where) < 0)
        return JS_EXCEPTION;
    polygon = b2Shape_GetPolygon(id);
    object = JS_NewObject(ctx);
    if (JS_IsException(object))
        return object;
    vertices = JS_NewArray(ctx);
    normals = JS_NewArray(ctx);
    for (int i = 0; i < polygon.count; i++) {
        JS_SetPropertyUint32(ctx, vertices, (uint32_t)i, b2js_new_vec2(ctx, polygon.vertices[i]));
        JS_SetPropertyUint32(ctx, normals, (uint32_t)i, b2js_new_vec2(ctx, polygon.normals[i]));
    }
    JS_SetPropertyStr(ctx, object, "vertices", vertices);
    JS_SetPropertyStr(ctx, object, "normals", normals);
    JS_SetPropertyStr(ctx, object, "centroid", b2js_new_vec2(ctx, polygon.centroid));
    JS_SetPropertyStr(ctx, object, "radius", JS_NewFloat32(ctx, polygon.radius));
    JS_SetPropertyStr(ctx, object, "count", JS_NewInt32(ctx, polygon.count));
    return object;
}

static JSValue shape_get_segment(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Shape.getSegment";
    SHAPE_THIS(where, 0, 0);

    if (shape_require(ctx, id, b2_segmentShape, where) < 0)
        return JS_EXCEPTION;
    return segment_object(ctx, b2Shape_GetSegment(id));
}

static JSValue shape_get_chain_segment(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Shape.getChainSegment";
    SHAPE_THIS(where, 0, 0);
    b2ChainSegment segment;
    JSValue object, chain;

    if (shape_require(ctx, id, b2_chainSegmentShape, where) < 0)
        return JS_EXCEPTION;
    segment = b2Shape_GetChainSegment(id);
    chain = b2js_wrap_chain(ctx, handle->world, b2Shape_GetParentChain(id));
    if (JS_IsException(chain))
        return chain;
    object = JS_NewObject(ctx);
    if (JS_IsException(object)) {
        JS_FreeValue(ctx, chain);
        return object;
    }
    JS_SetPropertyStr(ctx, object, "ghost1", b2js_new_vec2(ctx, segment.ghost1));
    JS_SetPropertyStr(ctx, object, "segment", segment_object(ctx, segment.segment));
    JS_SetPropertyStr(ctx, object, "ghost2", b2js_new_vec2(ctx, segment.ghost2));
    JS_SetPropertyStr(ctx, object, "chain", chain);
    return object;
}

/* Shape.getContacts(): touching contacts of this shape. */
static JSValue shape_get_contacts(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    SHAPE_THIS("Shape.getContacts", 0, 0);
    int capacity = b2Shape_GetContactCapacity(id);
    b2ContactData *contacts;
    JSValue array;
    int count;

    if (capacity <= 0)
        return JS_NewArray(ctx);
    contacts = malloc(sizeof(*contacts) * (size_t)capacity);
    if (!contacts)
        return JS_ThrowOutOfMemory(ctx);
    count = b2Shape_GetContactData(id, contacts, capacity);
    array = b2js_contact_array(ctx, handle->world, contacts, count);
    free(contacts);
    return array;
}

/* Shape.getSensorOverlaps(): shapes inside this sensor (as of the last step). */
static JSValue shape_get_sensor_overlaps(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Shape.getSensorOverlaps";
    SHAPE_THIS(where, 0, 0);
    b2ShapeId *visitors;
    JSValue array;
    int capacity, count;

    if (!b2Shape_IsSensor(id))
        return JS_ThrowTypeError(ctx, "%s: the shape is not a sensor", where);
    capacity = b2Shape_GetSensorCapacity(id);
    if (capacity <= 0)
        return JS_NewArray(ctx);
    visitors = malloc(sizeof(*visitors) * (size_t)capacity);
    if (!visitors)
        return JS_ThrowOutOfMemory(ctx);
    count = b2Shape_GetSensorData(id, visitors, capacity);
    array = JS_NewArray(ctx);
    for (int i = 0, n = 0; i < count && !JS_IsException(array); i++) {
        JSValue shape;
        /* Visitors destroyed since the step are left out. */
        if (!b2Shape_IsValid(visitors[i]))
            continue;
        shape = b2js_wrap_shape(ctx, handle->world, visitors[i]);
        if (JS_IsException(shape)) {
            JS_FreeValue(ctx, array);
            array = JS_EXCEPTION;
        } else {
            JS_SetPropertyUint32(ctx, array, (uint32_t)n++, shape);
        }
    }
    free(visitors);
    return array;
}

/* ------------------------------------------------------------------------ */
/* Geometry changes, material, mass, ray cast, wind                         */
/* ------------------------------------------------------------------------ */

enum {
    GEOMETRY_CIRCLE = 0,
    GEOMETRY_BOX,
    GEOMETRY_POLYGON,
    GEOMETRY_CAPSULE,
    GEOMETRY_SEGMENT,
};

/*
 * Shape.setCircle/setBox/setPolygon/setCapsule/setSegment(options): replaces
 * the geometry (the shape type may change), with the options of the matching
 * create*Shape. The body mass is kept: call body.applyMassFromShapes().
 */
static JSValue shape_set_geometry(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    static const char *const names[] = {
        [GEOMETRY_CIRCLE] = "Shape.setCircle",
        [GEOMETRY_BOX] = "Shape.setBox",
        [GEOMETRY_POLYGON] = "Shape.setPolygon",
        [GEOMETRY_CAPSULE] = "Shape.setCapsule",
        [GEOMETRY_SEGMENT] = "Shape.setSegment",
    };
    const char *where = names[magic];
    SHAPE_THIS(where, 1, 1);
    union {
        b2Circle circle;
        b2Polygon polygon;
        b2Capsule capsule;
        b2Segment segment;
    } geometry;
    int ret;

    if (!JS_IsObject(argv[0]) || JS_IsFunction(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "%s: expected an options object", where);
    /* Box2D keeps a chain's segments in the chain: changing one would corrupt it. */
    if (b2Shape_GetType(id) == b2_chainSegmentShape)
        return JS_ThrowTypeError(ctx, "%s: chain segments keep their geometry; recreate the Chain", where);

    switch (magic) {
        case GEOMETRY_CIRCLE: ret = b2js_circle(ctx, argv[0], where, &geometry.circle); break;
        case GEOMETRY_BOX: ret = b2js_box(ctx, argv[0], where, &geometry.polygon); break;
        case GEOMETRY_POLYGON: ret = b2js_polygon(ctx, argv[0], where, &geometry.polygon); break;
        case GEOMETRY_CAPSULE: ret = b2js_capsule(ctx, argv[0], where, &geometry.capsule); break;
        default: ret = b2js_segment(ctx, argv[0], where, &geometry.segment); break;
    }
    if (ret < 0 || b2js_still_alive(ctx, handle, where) < 0)
        return JS_EXCEPTION;

    switch (magic) {
        case GEOMETRY_CIRCLE: b2Shape_SetCircle(id, &geometry.circle); break;
        case GEOMETRY_BOX:
        case GEOMETRY_POLYGON: b2Shape_SetPolygon(id, &geometry.polygon); break;
        case GEOMETRY_CAPSULE: b2Shape_SetCapsule(id, &geometry.capsule); break;
        default: b2Shape_SetSegment(id, &geometry.segment); break;
    }
    return JS_UNDEFINED;
}

static JSValue shape_get_surface_material(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    SHAPE_THIS("Shape.getSurfaceMaterial", 0, 0);

    return b2js_new_material(ctx, b2Shape_GetSurfaceMaterial(id));
}

/* Shape.setSurfaceMaterial(material): fields left out keep their value. */
static JSValue shape_set_surface_material(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Shape.setSurfaceMaterial";
    SHAPE_THIS(where, 1, 1);
    b2SurfaceMaterial material = b2Shape_GetSurfaceMaterial(id);

    if (!JS_IsObject(argv[0]))
        return JS_ThrowTypeError(ctx, "%s: expected { friction?, restitution?, rollingResistance?, "
            "tangentSpeed?, userMaterialId?, customColor? }", where);
    if (b2js_material(ctx, argv[0], where, &material) < 0 || b2js_still_alive(ctx, handle, where) < 0)
        return JS_EXCEPTION;
    b2Shape_SetSurfaceMaterial(id, &material);
    return JS_UNDEFINED;
}

/* Shape.computeMassData(): mass of this shape alone, from its density. */
static JSValue shape_compute_mass_data(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    SHAPE_THIS("Shape.computeMassData", 0, 0);

    return b2js_new_mass_data(ctx, b2Shape_ComputeMassData(id));
}

/* Shape.rayCast(originX, originY, translationX, translationY): this shape only; null when missed. */
static JSValue shape_ray_cast(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Shape.rayCast";
    static const char *const names[] = { "originX", "originY", "translationX", "translationY" };
    SHAPE_THIS(where, 4, 4);
    b2WorldCastOutput output;
    JSValue object;
    float v[4];

    for (int i = 0; i < 4; i++) {
        if (b2js_float(ctx, argv[i], where, names[i], B2JS_COORD, &v[i]) < 0)
            return JS_EXCEPTION;
    }
    output = b2Shape_RayCast(id, (b2Pos){ v[0], v[1] }, (b2Vec2){ v[2], v[3] });
    if (!output.hit)
        return JS_NULL;
    object = JS_NewObject(ctx);
    if (JS_IsException(object))
        return object;
    b2js_set(ctx, object, b2js_atoms.shape, JS_DupValue(ctx, this_val));
    b2js_set(ctx, object, b2js_atoms.point, b2js_new_vec2(ctx, output.point));
    b2js_set(ctx, object, b2js_atoms.normal, b2js_new_vec2(ctx, output.normal));
    b2js_set(ctx, object, b2js_atoms.fraction, JS_NewFloat32(ctx, output.fraction));
    return object;
}

/*
 * Shape.applyWind(windX, windY, drag, lift, wake = true): air force on a
 * circle, capsule or polygon of a dynamic body (other shapes are ignored).
 */
static JSValue shape_apply_wind(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Shape.applyWind";
    SHAPE_THIS(where, 4, 5);
    b2Vec2 wind;
    float drag, lift;
    bool wake = true;

    if (b2js_float(ctx, argv[0], where, "windX", B2JS_COORD, &wind.x) < 0 ||
        b2js_float(ctx, argv[1], where, "windY", B2JS_COORD, &wind.y) < 0 ||
        b2js_float(ctx, argv[2], where, "drag", B2JS_NONNEG, &drag) < 0 ||
        b2js_float(ctx, argv[3], where, "lift", B2JS_ANY, &lift) < 0 ||
        (b2js_has(argc, argv, 4) && b2js_bool(ctx, argv[4], where, "wake", &wake) < 0))
        return JS_EXCEPTION;
    b2Shape_ApplyWind(id, wind, drag, lift, wake);
    return JS_UNDEFINED;
}

/* ------------------------------------------------------------------------ */
/* Shape lifetime and user data                                              */
/* ------------------------------------------------------------------------ */

static JSValue shape_get_user_data(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    SHAPE_THIS("Shape.getUserData", 0, 0);

    (void)id;
    return JS_DupValue(ctx, handle->user_data);
}

static JSValue shape_set_user_data(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    SHAPE_THIS("Shape.setUserData", 1, 1);

    (void)id;
    b2js_set_user_data(ctx, handle, argv[0]);
    return JS_UNDEFINED;
}

static JSValue shape_destroy(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Shape.destroy";
    B2JSHandle *handle = b2js_handle_any(this_val, B2JS_SHAPE);
    bool update_mass = true;
    b2ShapeId id;

    if (b2js_argc(ctx, argc, 0, 1, where) < 0)
        return JS_EXCEPTION;
    if (!handle)
        return JS_ThrowTypeError(ctx, "%s: expected a Box2D Shape", where);
    if (handle->dead)
        return JS_FALSE;
    if (b2js_has(argc, argv, 0) && b2js_bool(ctx, argv[0], where, "updateBodyMass", &update_mass) < 0)
        return JS_EXCEPTION;

    id = handle->id.shape;
    /* Box2D asserts on it: segments of a chain go away with the chain. */
    if (b2Shape_GetType(id) == b2_chainSegmentShape && b2Chain_IsValid(b2Shape_GetParentChain(id)))
        return JS_ThrowTypeError(ctx, "%s: this segment belongs to a Chain; destroy the Chain instead", where);

    b2js_kill(JS_GetRuntime(ctx), handle);
    b2DestroyShape(id, update_mass);
    return JS_TRUE;
}

static JSValue shape_is_valid(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    B2JSHandle *handle = b2js_handle_any(this_val, B2JS_SHAPE);

    if (b2js_argc(ctx, argc, 0, 0, "Shape.isValid") < 0)
        return JS_EXCEPTION;
    return JS_NewBool(ctx, handle && !handle->dead);
}

static const JSCFunctionListEntry shape_proto_funcs[] = {
    JS_CFUNC_DEF("isValid", 0, shape_is_valid),
    JS_CFUNC_DEF("destroy", 1, shape_destroy),
    JS_CFUNC_DEF("getType", 0, shape_get_type),
    JS_CFUNC_DEF("getBody", 0, shape_get_body),
    JS_CFUNC_MAGIC_DEF("getFriction", 0, shape_get, SHAPE_FRICTION),
    JS_CFUNC_MAGIC_DEF("setFriction", 1, shape_set, SHAPE_FRICTION),
    JS_CFUNC_MAGIC_DEF("getRestitution", 0, shape_get, SHAPE_RESTITUTION),
    JS_CFUNC_MAGIC_DEF("setRestitution", 1, shape_set, SHAPE_RESTITUTION),
    JS_CFUNC_MAGIC_DEF("getDensity", 0, shape_get, SHAPE_DENSITY),
    JS_CFUNC_MAGIC_DEF("setDensity", 2, shape_set, SHAPE_DENSITY),
    JS_CFUNC_MAGIC_DEF("isSensor", 0, shape_get, SHAPE_SENSOR),
    JS_CFUNC_MAGIC_DEF("enableSensorEvents", 1, shape_set, SHAPE_SENSOR_EVENTS),
    JS_CFUNC_MAGIC_DEF("areSensorEventsEnabled", 0, shape_get, SHAPE_SENSOR_EVENTS),
    JS_CFUNC_MAGIC_DEF("enableContactEvents", 1, shape_set, SHAPE_CONTACT_EVENTS),
    JS_CFUNC_MAGIC_DEF("areContactEventsEnabled", 0, shape_get, SHAPE_CONTACT_EVENTS),
    JS_CFUNC_MAGIC_DEF("enableHitEvents", 1, shape_set, SHAPE_HIT_EVENTS),
    JS_CFUNC_MAGIC_DEF("areHitEventsEnabled", 0, shape_get, SHAPE_HIT_EVENTS),
    JS_CFUNC_DEF("getFilter", 0, shape_get_filter),
    JS_CFUNC_DEF("setFilter", 1, shape_set_filter),
    JS_CFUNC_DEF("getUserData", 0, shape_get_user_data),
    JS_CFUNC_DEF("setUserData", 1, shape_set_user_data),
    JS_CFUNC_MAGIC_DEF("testPoint", 2, shape_point_query, SHAPE_TEST_POINT),
    JS_CFUNC_MAGIC_DEF("getClosestPoint", 2, shape_point_query, SHAPE_CLOSEST_POINT),
    JS_CFUNC_DEF("getAABB", 0, shape_get_aabb),
    JS_CFUNC_DEF("getCircle", 0, shape_get_circle),
    JS_CFUNC_DEF("getCapsule", 0, shape_get_capsule),
    JS_CFUNC_DEF("getPolygon", 0, shape_get_polygon),
    JS_CFUNC_DEF("getSegment", 0, shape_get_segment),
    JS_CFUNC_DEF("getChainSegment", 0, shape_get_chain_segment),
    JS_CFUNC_DEF("getContacts", 0, shape_get_contacts),
    JS_CFUNC_DEF("getSensorOverlaps", 0, shape_get_sensor_overlaps),
    JS_CFUNC_MAGIC_DEF("setCircle", 1, shape_set_geometry, GEOMETRY_CIRCLE),
    JS_CFUNC_MAGIC_DEF("setBox", 1, shape_set_geometry, GEOMETRY_BOX),
    JS_CFUNC_MAGIC_DEF("setPolygon", 1, shape_set_geometry, GEOMETRY_POLYGON),
    JS_CFUNC_MAGIC_DEF("setCapsule", 1, shape_set_geometry, GEOMETRY_CAPSULE),
    JS_CFUNC_MAGIC_DEF("setSegment", 1, shape_set_geometry, GEOMETRY_SEGMENT),
    JS_CFUNC_DEF("getSurfaceMaterial", 0, shape_get_surface_material),
    JS_CFUNC_DEF("setSurfaceMaterial", 1, shape_set_surface_material),
    JS_CFUNC_DEF("computeMassData", 0, shape_compute_mass_data),
    JS_CFUNC_DEF("rayCast", 4, shape_ray_cast),
    JS_CFUNC_DEF("applyWind", 5, shape_apply_wind),
};

/* ------------------------------------------------------------------------ */
/* Chain                                                                     */
/* ------------------------------------------------------------------------ */

/* Segments of a chain (malloc'd, caller frees); NULL with an exception set. */
static b2ShapeId *chain_segments(JSContext *ctx, b2ChainId id, int *count) {
    int capacity = b2Chain_GetSegmentCount(id);
    b2ShapeId *segments = malloc(sizeof(*segments) * (size_t)(capacity > 0 ? capacity : 1));

    if (!segments) {
        JS_ThrowOutOfMemory(ctx);
        return NULL;
    }
    *count = b2Chain_GetSegments(id, segments, capacity);
    return segments;
}

static JSValue chain_get_shapes(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    CHAIN_THIS("Chain.getShapes", 0, 0);
    JSValue array;
    b2ShapeId *segments;
    int count;

    segments = chain_segments(ctx, id, &count);
    if (!segments)
        return JS_EXCEPTION;
    array = JS_NewArray(ctx);
    for (int i = 0; i < count && !JS_IsException(array); i++) {
        JSValue shape = b2js_wrap_shape(ctx, handle->world, segments[i]);
        if (JS_IsException(shape)) {
            JS_FreeValue(ctx, array);
            array = JS_EXCEPTION;
        } else {
            JS_SetPropertyUint32(ctx, array, (uint32_t)i, shape);
        }
    }
    free(segments);
    return array;
}

static JSValue chain_get_body(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    CHAIN_THIS("Chain.getBody", 0, 0);
    b2ShapeId segment;

    /* Box2D has no chain-to-body query; every chain has at least one segment. */
    if (b2Chain_GetSegments(id, &segment, 1) < 1)
        return JS_ThrowInternalError(ctx, "Chain.getBody: the chain has no segments");
    return b2js_wrap_body(ctx, handle->world, b2Shape_GetBody(segment));
}

static JSValue chain_get_user_data(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    CHAIN_THIS("Chain.getUserData", 0, 0);

    (void)id;
    return JS_DupValue(ctx, handle->user_data);
}

static JSValue chain_set_user_data(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    CHAIN_THIS("Chain.setUserData", 1, 1);

    (void)id;
    b2js_set_user_data(ctx, handle, argv[0]);
    return JS_UNDEFINED;
}

static JSValue chain_destroy(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JSRuntime *rt = JS_GetRuntime(ctx);
    B2JSHandle *handle = b2js_handle_any(this_val, B2JS_CHAIN);
    b2ShapeId *segments;
    b2ChainId id;
    int count;

    if (b2js_argc(ctx, argc, 0, 0, "Chain.destroy") < 0)
        return JS_EXCEPTION;
    if (!handle)
        return JS_ThrowTypeError(ctx, "Chain.destroy: expected a Box2D Chain");
    if (handle->dead)
        return JS_FALSE;

    id = handle->id.chain;
    segments = chain_segments(ctx, id, &count);
    if (!segments)
        return JS_EXCEPTION;
    for (int i = 0; i < count; i++) {
        B2JSHandle *segment = b2Shape_GetUserData(segments[i]);
        if (segment)
            b2js_kill(rt, segment);
    }
    free(segments);
    b2js_kill(rt, handle);
    b2DestroyChain(id);
    return JS_TRUE;
}

static JSValue chain_is_valid(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    B2JSHandle *handle = b2js_handle_any(this_val, B2JS_CHAIN);

    if (b2js_argc(ctx, argc, 0, 0, "Chain.isValid") < 0)
        return JS_EXCEPTION;
    return JS_NewBool(ctx, handle && !handle->dead);
}

static const JSCFunctionListEntry chain_proto_funcs[] = {
    JS_CFUNC_DEF("isValid", 0, chain_is_valid),
    JS_CFUNC_DEF("destroy", 0, chain_destroy),
    JS_CFUNC_DEF("getBody", 0, chain_get_body),
    JS_CFUNC_DEF("getShapes", 0, chain_get_shapes),
    JS_CFUNC_DEF("getUserData", 0, chain_get_user_data),
    JS_CFUNC_DEF("setUserData", 1, chain_set_user_data),
};

int b2js_shape_init(JSContext *ctx) {
    if (b2js_class_proto(ctx, b2js_class_ids[B2JS_SHAPE], shape_proto_funcs, countof(shape_proto_funcs)) < 0 ||
        b2js_class_proto(ctx, b2js_class_ids[B2JS_CHAIN], chain_proto_funcs, countof(chain_proto_funcs)) < 0)
        return -1;
    return 0;
}
