#include <float.h>
#include <stdlib.h>

#include "ath_box2d_internal.h"

/* The live body behind `this` and its id; returns on error. */
#define BODY_THIS(where, min, max)                                               \
    B2JSHandle *handle = b2js_handle(ctx, this_val, B2JS_BODY, where);           \
    if (!handle || b2js_argc(ctx, argc, min, max, where) < 0)                    \
        return JS_EXCEPTION;                                                     \
    b2BodyId id = handle->id.body

/* ------------------------------------------------------------------------ */
/* Scalars                                                                   */
/* ------------------------------------------------------------------------ */

enum {
    BODY_TYPE = 0,
    BODY_ANGLE,
    BODY_ANGULAR_VELOCITY,
    BODY_LINEAR_DAMPING,
    BODY_ANGULAR_DAMPING,
    BODY_GRAVITY_SCALE,
    BODY_MASS,
    BODY_AWAKE,
    BODY_ENABLED,
    BODY_FIXED_ROTATION,
    BODY_BULLET,
};

static const struct {
    const char *get;
    const char *set;
    B2JSRange range;
} body_scalars[] = {
    [BODY_TYPE] = { "Body.getType", "Body.setType", B2JS_ANY },
    [BODY_ANGLE] = { "Body.getAngle", NULL, B2JS_ANY },
    [BODY_ANGULAR_VELOCITY] = { "Body.getAngularVelocity", "Body.setAngularVelocity", B2JS_ANY },
    [BODY_LINEAR_DAMPING] = { "Body.getLinearDamping", "Body.setLinearDamping", B2JS_NONNEG },
    [BODY_ANGULAR_DAMPING] = { "Body.getAngularDamping", "Body.setAngularDamping", B2JS_NONNEG },
    [BODY_GRAVITY_SCALE] = { "Body.getGravityScale", "Body.setGravityScale", B2JS_ANY },
    [BODY_MASS] = { "Body.getMass", NULL, B2JS_ANY },
    [BODY_AWAKE] = { "Body.isAwake", "Body.setAwake", B2JS_ANY },
    [BODY_ENABLED] = { "Body.isEnabled", "Body.setEnabled", B2JS_ANY },
    [BODY_FIXED_ROTATION] = { "Body.isFixedRotation", "Body.setFixedRotation", B2JS_ANY },
    [BODY_BULLET] = { "Body.isBullet", "Body.setBullet", B2JS_ANY },
};

static JSValue body_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    BODY_THIS(body_scalars[magic].get, 0, 0);

    switch (magic) {
        case BODY_TYPE: return JS_NewInt32(ctx, b2Body_GetType(id));
        case BODY_ANGLE: return JS_NewFloat32(ctx, b2Rot_GetAngle(b2Body_GetRotation(id)));
        case BODY_ANGULAR_VELOCITY: return JS_NewFloat32(ctx, b2Body_GetAngularVelocity(id));
        case BODY_LINEAR_DAMPING: return JS_NewFloat32(ctx, b2Body_GetLinearDamping(id));
        case BODY_ANGULAR_DAMPING: return JS_NewFloat32(ctx, b2Body_GetAngularDamping(id));
        case BODY_GRAVITY_SCALE: return JS_NewFloat32(ctx, b2Body_GetGravityScale(id));
        case BODY_MASS: return JS_NewFloat32(ctx, b2Body_GetMass(id));
        case BODY_AWAKE: return JS_NewBool(ctx, b2Body_IsAwake(id));
        case BODY_ENABLED: return JS_NewBool(ctx, b2Body_IsEnabled(id));
        case BODY_FIXED_ROTATION: return JS_NewBool(ctx, b2Body_GetMotionLocks(id).angularZ);
        case BODY_BULLET: return JS_NewBool(ctx, b2Body_IsBullet(id));
        default: return JS_UNDEFINED;
    }
}

static JSValue body_set(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    const char *where = body_scalars[magic].set;
    BODY_THIS(where, 1, 1);
    float value;
    bool flag;
    int type;

    switch (magic) {
    case BODY_TYPE:
        if (b2js_int(ctx, argv[0], where, "type", b2_staticBody, b2_dynamicBody, &type) < 0)
            return JS_EXCEPTION;
        b2Body_SetType(id, (b2BodyType)type);
        return JS_UNDEFINED;
    case BODY_AWAKE:
    case BODY_ENABLED:
    case BODY_FIXED_ROTATION:
    case BODY_BULLET:
        if (b2js_bool(ctx, argv[0], where, "flag", &flag) < 0)
            return JS_EXCEPTION;
        if (magic == BODY_AWAKE) {
            b2Body_SetAwake(id, flag);
        } else if (magic == BODY_ENABLED) {
            if (flag)
                b2Body_Enable(id);
            else
                b2Body_Disable(id);
        } else if (magic == BODY_FIXED_ROTATION) {
            b2MotionLocks locks = b2Body_GetMotionLocks(id);
            locks.angularZ = flag;
            b2Body_SetMotionLocks(id, locks);
        } else {
            b2Body_SetBullet(id, flag);
        }
        return JS_UNDEFINED;
    default:
        if (b2js_float(ctx, argv[0], where, "value", body_scalars[magic].range, &value) < 0)
            return JS_EXCEPTION;
        switch (magic) {
            case BODY_ANGULAR_VELOCITY: b2Body_SetAngularVelocity(id, value); break;
            case BODY_LINEAR_DAMPING: b2Body_SetLinearDamping(id, value); break;
            case BODY_ANGULAR_DAMPING: b2Body_SetAngularDamping(id, value); break;
            case BODY_GRAVITY_SCALE: b2Body_SetGravityScale(id, value); break;
        }
        return JS_UNDEFINED;
    }
}

/* ------------------------------------------------------------------------ */
/* Transform and velocity                                                    */
/* ------------------------------------------------------------------------ */

enum {
    BODY_POSITION = 0,
    BODY_LINEAR_VELOCITY,
    BODY_WORLD_CENTER,
};

static JSValue body_get_vec2(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    static const char *const names[] = { "Body.getPosition", "Body.getLinearVelocity", "Body.getWorldCenter" };
    BODY_THIS(names[magic], 0, 0);

    switch (magic) {
        case BODY_POSITION: return b2js_new_vec2(ctx, b2Body_GetPosition(id));
        case BODY_LINEAR_VELOCITY: return b2js_new_vec2(ctx, b2Body_GetLinearVelocity(id));
        default: return b2js_new_vec2(ctx, b2Body_GetWorldCenter(id));
    }
}

/* Reads (x, y) from argv[index], argv[index + 1]: a point or velocity, within B2_HUGE. */
static int xy_args(JSContext *ctx, JSValueConst *argv, int index, const char *where,
    const char *x_name, const char *y_name, b2Vec2 *out) {
    if (b2js_float(ctx, argv[index], where, x_name, B2JS_COORD, &out->x) < 0 ||
        b2js_float(ctx, argv[index + 1], where, y_name, B2JS_COORD, &out->y) < 0)
        return -1;
    return 0;
}

static JSValue body_set_position(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Body.setPosition";
    BODY_THIS(where, 2, 2);
    b2Vec2 position;

    if (xy_args(ctx, argv, 0, where, "x", "y", &position) < 0)
        return JS_EXCEPTION;
    b2Body_SetTransform(id, position, b2Body_GetRotation(id));
    return JS_UNDEFINED;
}

static JSValue body_set_transform(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Body.setTransform";
    BODY_THIS(where, 3, 3);
    b2Vec2 position;
    float angle;

    if (xy_args(ctx, argv, 0, where, "x", "y", &position) < 0 ||
        b2js_float(ctx, argv[2], where, "angle", B2JS_ANY, &angle) < 0)
        return JS_EXCEPTION;
    b2Body_SetTransform(id, position, athena_box2d_make_rot(angle));
    return JS_UNDEFINED;
}

static JSValue body_get_transform(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    BODY_THIS("Body.getTransform", 0, 0);
    b2Transform transform = b2Body_GetTransform(id);
    JSValue object = JS_NewObject(ctx);

    if (JS_IsException(object))
        return object;
    b2js_set(ctx, object, b2js_atoms.x, JS_NewFloat32(ctx, transform.p.x));
    b2js_set(ctx, object, b2js_atoms.y, JS_NewFloat32(ctx, transform.p.y));
    b2js_set(ctx, object, b2js_atoms.angle, JS_NewFloat32(ctx, b2Rot_GetAngle(transform.q)));
    return object;
}

static JSValue body_set_target_transform(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Body.setTargetTransform";
    BODY_THIS(where, 4, 4);
    b2Transform target;
    float angle, duration;

    if (xy_args(ctx, argv, 0, where, "x", "y", &target.p) < 0 ||
        b2js_float(ctx, argv[2], where, "angle", B2JS_ANY, &angle) < 0 ||
        b2js_float(ctx, argv[3], where, "duration", B2JS_POS, &duration) < 0)
        return JS_EXCEPTION;
    target.q = athena_box2d_make_rot(angle);
    b2Body_SetTargetTransform(id, target, duration, true);
    return JS_UNDEFINED;
}

static JSValue body_set_linear_velocity(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Body.setLinearVelocity";
    BODY_THIS(where, 2, 2);
    b2Vec2 velocity;

    if (xy_args(ctx, argv, 0, where, "vx", "vy", &velocity) < 0)
        return JS_EXCEPTION;
    b2Body_SetLinearVelocity(id, velocity);
    return JS_UNDEFINED;
}

enum {
    BODY_WORLD_POINT = 0,
    BODY_LOCAL_POINT,
};

static JSValue body_transform_point(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
    int magic) {
    const char *where = magic == BODY_WORLD_POINT ? "Body.getWorldPoint" : "Body.getLocalPoint";
    BODY_THIS(where, 2, 2);
    b2Vec2 point;

    if (xy_args(ctx, argv, 0, where, "x", "y", &point) < 0)
        return JS_EXCEPTION;
    if (magic == BODY_WORLD_POINT)
        return b2js_new_vec2(ctx, b2Body_GetWorldPoint(id, point));
    return b2js_new_vec2(ctx, b2Body_GetLocalPoint(id, point));
}

/* ------------------------------------------------------------------------ */
/* Forces                                                                    */
/* ------------------------------------------------------------------------ */

enum {
    FORCE_AT_POINT = 0,
    FORCE_TO_CENTER,
    FORCE_TORQUE,
    IMPULSE_AT_POINT,
    IMPULSE_TO_CENTER,
    IMPULSE_ANGULAR,
};

static const struct {
    const char *name;
    int vectors; /* number arguments before the optional wake flag */
} forces[] = {
    [FORCE_AT_POINT] = { "Body.applyForce", 4 },
    [FORCE_TO_CENTER] = { "Body.applyForceToCenter", 2 },
    [FORCE_TORQUE] = { "Body.applyTorque", 1 },
    [IMPULSE_AT_POINT] = { "Body.applyLinearImpulse", 4 },
    [IMPULSE_TO_CENTER] = { "Body.applyLinearImpulseToCenter", 2 },
    [IMPULSE_ANGULAR] = { "Body.applyAngularImpulse", 1 },
};

static JSValue body_apply(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    const char *where = forces[magic].name;
    int count = forces[magic].vectors;
    BODY_THIS(where, count, count + 1);
    b2Vec2 vector = b2Vec2_zero, point = b2Vec2_zero;
    float scalar = 0.0f;
    bool wake = true;

    if (count == 1) {
        if (b2js_float(ctx, argv[0], where, "value", B2JS_ANY, &scalar) < 0)
            return JS_EXCEPTION;
    } else if (b2js_float(ctx, argv[0], where, "x", B2JS_ANY, &vector.x) < 0 ||
        b2js_float(ctx, argv[1], where, "y", B2JS_ANY, &vector.y) < 0 ||
        (count == 4 && xy_args(ctx, argv, 2, where, "pointX", "pointY", &point) < 0)) {
        return JS_EXCEPTION;
    }
    if (b2js_has(argc, argv, count) && b2js_bool(ctx, argv[count], where, "wake", &wake) < 0)
        return JS_EXCEPTION;

    switch (magic) {
        case FORCE_AT_POINT: b2Body_ApplyForce(id, vector, point, wake); break;
        case FORCE_TO_CENTER: b2Body_ApplyForceToCenter(id, vector, wake); break;
        case FORCE_TORQUE: b2Body_ApplyTorque(id, scalar, wake); break;
        case IMPULSE_AT_POINT: b2Body_ApplyLinearImpulse(id, vector, point, wake); break;
        case IMPULSE_TO_CENTER: b2Body_ApplyLinearImpulseToCenter(id, vector, wake); break;
        case IMPULSE_ANGULAR: b2Body_ApplyAngularImpulse(id, scalar, wake); break;
    }
    return JS_UNDEFINED;
}

static JSValue body_apply_mass_from_shapes(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    BODY_THIS("Body.applyMassFromShapes", 0, 0);

    b2Body_ApplyMassFromShapes(id);
    return JS_UNDEFINED;
}

static JSValue body_compute_aabb(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    BODY_THIS("Body.computeAABB", 0, 0);

    return b2js_new_aabb(ctx, b2Body_ComputeAABB(id));
}

/* ------------------------------------------------------------------------ */
/* Shapes and chains                                                         */
/* ------------------------------------------------------------------------ */

/* Script object for a new shape, taking user_data. Destroys the shape on failure. */
static JSValue shape_result(JSContext *ctx, B2JSHandle *body, JSValue user_data, b2ShapeId shape) {
    JSValue object = b2js_wrap_shape(ctx, body->world, shape);

    if (JS_IsException(object))
        b2DestroyShape(shape, true);
    else if (!JS_IsUndefined(user_data))
        b2js_set_user_data(ctx, b2Shape_GetUserData(shape), user_data);
    JS_FreeValue(ctx, user_data);
    return object;
}

/* Reads options.userData, then evaluates `create` (the Box2D call) last, so
 * no failure can leave a shape behind. */
#define SHAPE_RESULT(create)                                                     \
    do {                                                                         \
        JSValue user_data_ = JS_GetPropertyStr(ctx, argv[0], "userData");        \
        if (JS_IsException(user_data_))                                          \
            return user_data_;                                                   \
        if (b2js_still_alive(ctx, handle, where) < 0 ||                          \
            b2js_check_memory(ctx, where) < 0) {                                 \
            JS_FreeValue(ctx, user_data_);                                       \
            return JS_EXCEPTION;                                                 \
        }                                                                        \
        return shape_result(ctx, handle, user_data_, (create));                  \
    } while (0)

/* The options object every create*Shape takes, and the shape definition from it. */
static int shape_options(JSContext *ctx, int argc, JSValueConst *argv, const char *where,
    const char *usage, b2ShapeDef *def) {
    if (b2js_argc(ctx, argc, 1, 1, where) < 0)
        return -1;
    if (!JS_IsObject(argv[0]) || JS_IsFunction(ctx, argv[0])) {
        JS_ThrowTypeError(ctx, "%s: expected an options object %s", where, usage);
        return -1;
    }
    *def = b2DefaultShapeDef();
    return b2js_shape_def(ctx, argv[0], where, def);
}

static int required_float(JSContext *ctx, JSValueConst options, const char *key, const char *where,
    B2JSRange range, float *out) {
    int ret = b2js_opt_float(ctx, options, key, where, range, out);

    if (ret == 0)
        JS_ThrowTypeError(ctx, "%s: %s is required", where, key);
    return ret > 0 ? 0 : -1;
}

static int required_vec2(JSContext *ctx, JSValueConst options, const char *key, const char *where, b2Vec2 *out) {
    int ret = b2js_opt_vec2(ctx, options, key, where, out);

    if (ret == 0)
        JS_ThrowTypeError(ctx, "%s: %s is required", where, key);
    return ret > 0 ? 0 : -1;
}

static JSValue body_create_circle(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Body.createCircleShape";
    B2JSHandle *handle = b2js_handle(ctx, this_val, B2JS_BODY, where);
    b2Circle circle = { { 0.0f, 0.0f }, 0.0f };
    b2ShapeDef def;

    if (!handle || shape_options(ctx, argc, argv, where, "{ radius, center?, ... }", &def) < 0 ||
        required_float(ctx, argv[0], "radius", where, B2JS_POS, &circle.radius) < 0 ||
        b2js_opt_vec2(ctx, argv[0], "center", where, &circle.center) < 0)
        return JS_EXCEPTION;
    SHAPE_RESULT(b2CreateCircleShape(handle->id.body, &def, &circle));
}

static JSValue body_create_box(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Body.createBoxShape";
    B2JSHandle *handle = b2js_handle(ctx, this_val, B2JS_BODY, where);
    b2Vec2 center = b2Vec2_zero;
    float hw, hh, angle = 0.0f;
    b2Polygon box;
    b2ShapeDef def;

    if (!handle || shape_options(ctx, argc, argv, where, "{ halfWidth, halfHeight, center?, angle?, ... }", &def) < 0 ||
        required_float(ctx, argv[0], "halfWidth", where, B2JS_POS, &hw) < 0 ||
        required_float(ctx, argv[0], "halfHeight", where, B2JS_POS, &hh) < 0 ||
        b2js_opt_vec2(ctx, argv[0], "center", where, &center) < 0 ||
        b2js_opt_float(ctx, argv[0], "angle", where, B2JS_ANY, &angle) < 0)
        return JS_EXCEPTION;
    if (!athena_box2d_make_box(hw, hh, center, angle, &box))
        return JS_ThrowRangeError(ctx, "%s: the box is too small (halfWidth * halfHeight must exceed %g)",
            where, (double)FLT_EPSILON);
    SHAPE_RESULT(b2CreatePolygonShape(handle->id.body, &def, &box));
}

static JSValue body_create_polygon(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Body.createPolygonShape";
    B2JSHandle *handle = b2js_handle(ctx, this_val, B2JS_BODY, where);
    b2Vec2 points[B2_MAX_POLYGON_VERTICES];
    float radius = 0.0f;
    b2Polygon polygon;
    b2ShapeDef def;
    JSValue vertices;
    int count, ret;

    if (!handle || shape_options(ctx, argc, argv, where, "{ vertices, radius?, ... }", &def) < 0 ||
        b2js_opt_float(ctx, argv[0], "radius", where, B2JS_NONNEG, &radius) < 0)
        return JS_EXCEPTION;

    vertices = JS_GetPropertyStr(ctx, argv[0], "vertices");
    if (JS_IsException(vertices))
        return JS_EXCEPTION;
    ret = b2js_points(ctx, vertices, where, "vertices", 3, B2_MAX_POLYGON_VERTICES, points, &count);
    JS_FreeValue(ctx, vertices);
    if (ret < 0)
        return JS_EXCEPTION;
    if (!athena_box2d_make_polygon(points, count, radius, &polygon))
        return JS_ThrowRangeError(ctx, "%s: the vertices do not form a convex polygon "
            "(collinear, or points closer than %g)", where, (double)athena_box2d_linear_slop());
    SHAPE_RESULT(b2CreatePolygonShape(handle->id.body, &def, &polygon));
}

static JSValue body_create_capsule(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Body.createCapsuleShape";
    B2JSHandle *handle = b2js_handle(ctx, this_val, B2JS_BODY, where);
    b2Capsule capsule;
    b2ShapeDef def;

    if (!handle || shape_options(ctx, argc, argv, where, "{ point1, point2, radius, ... }", &def) < 0 ||
        required_vec2(ctx, argv[0], "point1", where, &capsule.center1) < 0 ||
        required_vec2(ctx, argv[0], "point2", where, &capsule.center2) < 0 ||
        required_float(ctx, argv[0], "radius", where, B2JS_POS, &capsule.radius) < 0)
        return JS_EXCEPTION;
    /* Box2D returns no shape for a capsule shorter than the linear slop. */
    if (!athena_box2d_segment_valid(capsule.center1, capsule.center2))
        return JS_ThrowRangeError(ctx, "%s: point1 and point2 must be more than %g apart (use a circle)",
            where, (double)athena_box2d_linear_slop());
    SHAPE_RESULT(b2CreateCapsuleShape(handle->id.body, &def, &capsule));
}

static JSValue body_create_segment(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Body.createSegmentShape";
    B2JSHandle *handle = b2js_handle(ctx, this_val, B2JS_BODY, where);
    b2Segment segment;
    b2ShapeDef def;

    if (!handle || shape_options(ctx, argc, argv, where, "{ point1, point2, ... }", &def) < 0 ||
        required_vec2(ctx, argv[0], "point1", where, &segment.point1) < 0 ||
        required_vec2(ctx, argv[0], "point2", where, &segment.point2) < 0)
        return JS_EXCEPTION;
    if (!athena_box2d_segment_valid(segment.point1, segment.point2))
        return JS_ThrowRangeError(ctx, "%s: point1 and point2 must be more than %g apart",
            where, (double)athena_box2d_linear_slop());
    SHAPE_RESULT(b2CreateSegmentShape(handle->id.body, &def, &segment));
}

static int chain_def(JSContext *ctx, JSValueConst options, const char *where, b2ChainDef *def,
    b2SurfaceMaterial *material, JSValue *user_data) {
    JSValue filter;
    int ret = 0;

    if (b2js_opt_bool(ctx, options, "isLoop", where, &def->isLoop) < 0 ||
        b2js_opt_bool(ctx, options, "enableSensorEvents", where, &def->enableSensorEvents) < 0 ||
        b2js_opt_float(ctx, options, "friction", where, B2JS_NONNEG, &material->friction) < 0 ||
        b2js_opt_float(ctx, options, "restitution", where, B2JS_NONNEG, &material->restitution) < 0 ||
        b2js_opt_float(ctx, options, "rollingResistance", where, B2JS_NONNEG, &material->rollingResistance) < 0 ||
        b2js_opt_float(ctx, options, "tangentSpeed", where, B2JS_ANY, &material->tangentSpeed) < 0)
        return -1;

    filter = JS_GetPropertyStr(ctx, options, "filter");
    if (JS_IsException(filter))
        return -1;
    if (!JS_IsUndefined(filter))
        ret = b2js_filter(ctx, filter, where, &def->filter);
    JS_FreeValue(ctx, filter);
    if (ret < 0)
        return -1;

    *user_data = JS_GetPropertyStr(ctx, options, "userData");
    return JS_IsException(*user_data) ? -1 : 0;
}

static JSValue body_create_chain(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Body.createChain";
    BODY_THIS(where, 1, 2);
    b2ChainDef def = b2DefaultChainDef();
    b2SurfaceMaterial material = b2DefaultSurfaceMaterial();
    JSValue user_data = JS_UNDEFINED;
    JSValue chain;
    b2Vec2 *points;
    b2ChainId chain_id;
    int count, has_options;

    has_options = b2js_options(ctx, argc, argv, 1, where);
    if (has_options < 0 || (has_options && chain_def(ctx, argv[1], where, &def, &material, &user_data) < 0))
        return JS_EXCEPTION;
    points = b2js_points_alloc(ctx, argv[0], where, "points", 4, &count);
    if (!points) {
        JS_FreeValue(ctx, user_data);
        return JS_EXCEPTION;
    }
    if (!athena_box2d_chain_valid(points, count, def.isLoop)) {
        free(points);
        JS_FreeValue(ctx, user_data);
        return JS_ThrowRangeError(ctx, "%s: consecutive points must be more than %g apart",
            where, (double)athena_box2d_linear_slop());
    }
    if (b2js_still_alive(ctx, handle, where) < 0 || b2js_check_memory(ctx, where) < 0) {
        free(points);
        JS_FreeValue(ctx, user_data);
        return JS_EXCEPTION;
    }

    def.points = points;
    def.count = count;
    def.materials = &material;
    def.materialCount = 1;
    chain_id = b2CreateChain(id, &def);
    free(points);

    chain = b2js_wrap_chain(ctx, handle->world, chain_id);
    if (JS_IsException(chain)) {
        b2DestroyChain(chain_id);
    } else if (!JS_IsUndefined(user_data)) {
        b2js_set_user_data(ctx, b2js_handle_any(chain, B2JS_CHAIN), user_data);
    }
    JS_FreeValue(ctx, user_data);
    return chain;
}

static JSValue body_get_shapes(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    BODY_THIS("Body.getShapes", 0, 0);
    int count = b2Body_GetShapeCount(id);
    b2ShapeId *shapes;
    JSValue array = JS_NewArray(ctx);

    if (JS_IsException(array) || count <= 0)
        return array;
    shapes = malloc(sizeof(*shapes) * (size_t)count);
    if (!shapes) {
        JS_FreeValue(ctx, array);
        return JS_ThrowOutOfMemory(ctx);
    }
    count = b2Body_GetShapes(id, shapes, count);
    for (int i = 0; i < count; i++) {
        JSValue shape = b2js_wrap_shape(ctx, handle->world, shapes[i]);
        if (JS_IsException(shape)) {
            free(shapes);
            JS_FreeValue(ctx, array);
            return shape;
        }
        JS_SetPropertyUint32(ctx, array, (uint32_t)i, shape);
    }
    free(shapes);
    return array;
}

static JSValue body_get_joints(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    BODY_THIS("Body.getJoints", 0, 0);
    int count = b2Body_GetJointCount(id);
    b2JointId *joints;
    JSValue array = JS_NewArray(ctx);

    if (JS_IsException(array) || count <= 0)
        return array;
    joints = malloc(sizeof(*joints) * (size_t)count);
    if (!joints) {
        JS_FreeValue(ctx, array);
        return JS_ThrowOutOfMemory(ctx);
    }
    count = b2Body_GetJoints(id, joints, count);
    for (int i = 0; i < count; i++) {
        JSValue joint = b2js_wrap_joint(ctx, handle->world, joints[i]);
        if (JS_IsException(joint)) {
            free(joints);
            JS_FreeValue(ctx, array);
            return joint;
        }
        JS_SetPropertyUint32(ctx, array, (uint32_t)i, joint);
    }
    free(joints);
    return array;
}

static JSValue body_get_motion_locks(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    BODY_THIS("Body.getMotionLocks", 0, 0);
    b2MotionLocks locks = b2Body_GetMotionLocks(id);
    JSValue object = JS_NewObject(ctx);

    if (JS_IsException(object))
        return object;
    JS_SetPropertyStr(ctx, object, "linearX", JS_NewBool(ctx, locks.linearX));
    JS_SetPropertyStr(ctx, object, "linearY", JS_NewBool(ctx, locks.linearY));
    JS_SetPropertyStr(ctx, object, "angularZ", JS_NewBool(ctx, locks.angularZ));
    return object;
}

/* Body.setMotionLocks({ linearX?, linearY?, angularZ? }): fields left out keep their value. */
static JSValue body_set_motion_locks(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Body.setMotionLocks";
    BODY_THIS(where, 1, 1);
    b2MotionLocks locks = b2Body_GetMotionLocks(id);

    if (!JS_IsObject(argv[0]))
        return JS_ThrowTypeError(ctx, "%s: expected { linearX?, linearY?, angularZ? }", where);
    if (b2js_opt_bool(ctx, argv[0], "linearX", where, &locks.linearX) < 0 ||
        b2js_opt_bool(ctx, argv[0], "linearY", where, &locks.linearY) < 0 ||
        b2js_opt_bool(ctx, argv[0], "angularZ", where, &locks.angularZ) < 0 ||
        b2js_still_alive(ctx, handle, where) < 0)
        return JS_EXCEPTION;
    b2Body_SetMotionLocks(id, locks);
    return JS_UNDEFINED;
}

JSValue b2js_contact_array(JSContext *ctx, B2JSWorld *world, const b2ContactData *contacts, int count) {
    JSValue array = JS_NewArray(ctx);

    for (int i = 0; i < count && !JS_IsException(array); i++) {
        const b2ContactData *contact = &contacts[i];
        /* Manifold anchors are relative to the center of mass of shape A's body. */
        b2Vec2 center = b2Body_GetWorldCenter(b2Shape_GetBody(contact->shapeIdA));
        JSValue shape_a = b2js_wrap_shape(ctx, world, contact->shapeIdA);
        JSValue shape_b = JS_IsException(shape_a) ? JS_EXCEPTION : b2js_wrap_shape(ctx, world, contact->shapeIdB);
        JSValue item = JS_IsException(shape_b) ? JS_EXCEPTION : JS_NewObject(ctx);
        JSValue points;

        if (JS_IsException(item)) {
            JS_FreeValue(ctx, shape_a);
            JS_FreeValue(ctx, shape_b);
            JS_FreeValue(ctx, array);
            return JS_EXCEPTION;
        }
        b2js_set(ctx, item, b2js_atoms.shapeA, shape_a);
        b2js_set(ctx, item, b2js_atoms.shapeB, shape_b);
        b2js_set(ctx, item, b2js_atoms.normal, b2js_new_vec2(ctx, contact->manifold.normal));
        points = JS_NewArray(ctx);
        for (int p = 0; p < contact->manifold.pointCount && !JS_IsException(points); p++) {
            const b2ManifoldPoint *mp = &contact->manifold.points[p];
            JSValue point = JS_NewObject(ctx);
            if (JS_IsException(point)) {
                JS_FreeValue(ctx, points);
                points = JS_EXCEPTION;
                break;
            }
            b2js_set(ctx, point, b2js_atoms.point, b2js_new_vec2(ctx, b2Add(center, mp->anchorA)));
            b2js_set(ctx, point, b2js_atoms.separation, JS_NewFloat32(ctx, mp->separation));
            b2js_set(ctx, point, b2js_atoms.normalImpulse, JS_NewFloat32(ctx, mp->normalImpulse));
            JS_SetPropertyUint32(ctx, points, (uint32_t)p, point);
        }
        b2js_set(ctx, item, b2js_atoms.points, points);
        JS_SetPropertyUint32(ctx, array, (uint32_t)i, item);
    }
    return array;
}

/* Body.getContacts(): the touching contacts of the body's shapes. */
static JSValue body_get_contacts(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    BODY_THIS("Body.getContacts", 0, 0);
    int capacity = b2Body_GetContactCapacity(id);
    b2ContactData *contacts;
    JSValue array;
    int count;

    if (capacity <= 0)
        return JS_NewArray(ctx);
    contacts = malloc(sizeof(*contacts) * (size_t)capacity);
    if (!contacts)
        return JS_ThrowOutOfMemory(ctx);
    count = b2Body_GetContactData(id, contacts, capacity);
    array = b2js_contact_array(ctx, handle->world, contacts, count);
    free(contacts);
    return array;
}

/* ------------------------------------------------------------------------ */
/* Lifetime and user data                                                    */
/* ------------------------------------------------------------------------ */

static JSValue body_get_user_data(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    BODY_THIS("Body.getUserData", 0, 0);

    (void)id;
    return JS_DupValue(ctx, handle->user_data);
}

static JSValue body_set_user_data(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    BODY_THIS("Body.setUserData", 1, 1);

    (void)id;
    b2js_set_user_data(ctx, handle, argv[0]);
    return JS_UNDEFINED;
}

static JSValue body_destroy(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    B2JSHandle *handle = b2js_handle_any(this_val, B2JS_BODY);
    B2JSWorld *world;
    b2BodyId id;

    if (b2js_argc(ctx, argc, 0, 0, "Body.destroy") < 0)
        return JS_EXCEPTION;
    if (!handle)
        return JS_ThrowTypeError(ctx, "Body.destroy: expected a Box2D Body");
    if (handle->dead)
        return JS_FALSE;

    world = handle->world;
    id = handle->id.body;
    if (b2js_kill_body_attachments(ctx, id) < 0)
        return JS_EXCEPTION;
    b2js_kill(JS_GetRuntime(ctx), handle);
    /* Also destroys the body's shapes, chains and joints. */
    b2DestroyBody(id);
    b2js_kill_stale_chains(JS_GetRuntime(ctx), world);
    return JS_TRUE;
}

static JSValue body_is_valid(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    B2JSHandle *handle = b2js_handle_any(this_val, B2JS_BODY);

    if (b2js_argc(ctx, argc, 0, 0, "Body.isValid") < 0)
        return JS_EXCEPTION;
    return JS_NewBool(ctx, handle && !handle->dead);
}

static const JSCFunctionListEntry body_proto_funcs[] = {
    JS_CFUNC_MAGIC_DEF("getType", 0, body_get, BODY_TYPE),
    JS_CFUNC_MAGIC_DEF("setType", 1, body_set, BODY_TYPE),
    JS_CFUNC_MAGIC_DEF("getPosition", 0, body_get_vec2, BODY_POSITION),
    JS_CFUNC_DEF("setPosition", 2, body_set_position),
    JS_CFUNC_MAGIC_DEF("getAngle", 0, body_get, BODY_ANGLE),
    JS_CFUNC_DEF("getTransform", 0, body_get_transform),
    JS_CFUNC_DEF("setTransform", 3, body_set_transform),
    JS_CFUNC_DEF("setTargetTransform", 4, body_set_target_transform),
    JS_CFUNC_MAGIC_DEF("getLinearVelocity", 0, body_get_vec2, BODY_LINEAR_VELOCITY),
    JS_CFUNC_DEF("setLinearVelocity", 2, body_set_linear_velocity),
    JS_CFUNC_MAGIC_DEF("getAngularVelocity", 0, body_get, BODY_ANGULAR_VELOCITY),
    JS_CFUNC_MAGIC_DEF("setAngularVelocity", 1, body_set, BODY_ANGULAR_VELOCITY),
    JS_CFUNC_MAGIC_DEF("applyForce", 5, body_apply, FORCE_AT_POINT),
    JS_CFUNC_MAGIC_DEF("applyForceToCenter", 3, body_apply, FORCE_TO_CENTER),
    JS_CFUNC_MAGIC_DEF("applyTorque", 2, body_apply, FORCE_TORQUE),
    JS_CFUNC_MAGIC_DEF("applyLinearImpulse", 5, body_apply, IMPULSE_AT_POINT),
    JS_CFUNC_MAGIC_DEF("applyLinearImpulseToCenter", 3, body_apply, IMPULSE_TO_CENTER),
    JS_CFUNC_MAGIC_DEF("applyAngularImpulse", 2, body_apply, IMPULSE_ANGULAR),
    JS_CFUNC_MAGIC_DEF("getMass", 0, body_get, BODY_MASS),
    JS_CFUNC_MAGIC_DEF("getLinearDamping", 0, body_get, BODY_LINEAR_DAMPING),
    JS_CFUNC_MAGIC_DEF("setLinearDamping", 1, body_set, BODY_LINEAR_DAMPING),
    JS_CFUNC_MAGIC_DEF("getAngularDamping", 0, body_get, BODY_ANGULAR_DAMPING),
    JS_CFUNC_MAGIC_DEF("setAngularDamping", 1, body_set, BODY_ANGULAR_DAMPING),
    JS_CFUNC_MAGIC_DEF("getGravityScale", 0, body_get, BODY_GRAVITY_SCALE),
    JS_CFUNC_MAGIC_DEF("setGravityScale", 1, body_set, BODY_GRAVITY_SCALE),
    JS_CFUNC_MAGIC_DEF("isAwake", 0, body_get, BODY_AWAKE),
    JS_CFUNC_MAGIC_DEF("setAwake", 1, body_set, BODY_AWAKE),
    JS_CFUNC_MAGIC_DEF("isEnabled", 0, body_get, BODY_ENABLED),
    JS_CFUNC_MAGIC_DEF("setEnabled", 1, body_set, BODY_ENABLED),
    JS_CFUNC_MAGIC_DEF("isFixedRotation", 0, body_get, BODY_FIXED_ROTATION),
    JS_CFUNC_MAGIC_DEF("setFixedRotation", 1, body_set, BODY_FIXED_ROTATION),
    JS_CFUNC_MAGIC_DEF("isBullet", 0, body_get, BODY_BULLET),
    JS_CFUNC_MAGIC_DEF("setBullet", 1, body_set, BODY_BULLET),
    JS_CFUNC_MAGIC_DEF("getWorldPoint", 2, body_transform_point, BODY_WORLD_POINT),
    JS_CFUNC_MAGIC_DEF("getLocalPoint", 2, body_transform_point, BODY_LOCAL_POINT),
    JS_CFUNC_MAGIC_DEF("getWorldCenter", 0, body_get_vec2, BODY_WORLD_CENTER),
    JS_CFUNC_DEF("applyMassFromShapes", 0, body_apply_mass_from_shapes),
    JS_CFUNC_DEF("computeAABB", 0, body_compute_aabb),
    JS_CFUNC_DEF("createCircleShape", 1, body_create_circle),
    JS_CFUNC_DEF("createBoxShape", 1, body_create_box),
    JS_CFUNC_DEF("createPolygonShape", 1, body_create_polygon),
    JS_CFUNC_DEF("createCapsuleShape", 1, body_create_capsule),
    JS_CFUNC_DEF("createSegmentShape", 1, body_create_segment),
    JS_CFUNC_DEF("createChain", 2, body_create_chain),
    JS_CFUNC_DEF("getShapes", 0, body_get_shapes),
    JS_CFUNC_DEF("getJoints", 0, body_get_joints),
    JS_CFUNC_DEF("getContacts", 0, body_get_contacts),
    JS_CFUNC_DEF("getMotionLocks", 0, body_get_motion_locks),
    JS_CFUNC_DEF("setMotionLocks", 1, body_set_motion_locks),
    JS_CFUNC_DEF("getUserData", 0, body_get_user_data),
    JS_CFUNC_DEF("setUserData", 1, body_set_user_data),
    JS_CFUNC_DEF("destroy", 0, body_destroy),
    JS_CFUNC_DEF("isValid", 0, body_is_valid),
};

int b2js_body_init(JSContext *ctx) {
    return b2js_class_proto(ctx, b2js_class_ids[B2JS_BODY], body_proto_funcs, countof(body_proto_funcs));
}
