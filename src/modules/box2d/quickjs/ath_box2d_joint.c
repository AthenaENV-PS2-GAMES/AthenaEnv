#include "ath_box2d_internal.h"

/* ------------------------------------------------------------------------ */
/* Creation                                                                  */
/* ------------------------------------------------------------------------ */

/*
 * Common part of World.create*Joint(bodyA, bodyB, options?): the world, both
 * bodies and options.collideConnected. *has_options tells whether argv[2] is
 * an options object.
 */
static B2JSWorld *joint_begin(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
    const char *where, b2JointDef *base, int *has_options) {
    B2JSWorld *world = b2js_this_world(ctx, this_val, where);
    B2JSHandle *a, *b;

    if (!world || b2js_argc(ctx, argc, 2, 3, where) < 0)
        return NULL;
    a = b2js_handle(ctx, argv[0], B2JS_BODY, where);
    if (!a)
        return NULL;
    b = b2js_handle(ctx, argv[1], B2JS_BODY, where);
    if (!b)
        return NULL;
    if (a->world != world || b->world != world) {
        JS_ThrowTypeError(ctx, "%s: both bodies must belong to this World", where);
        return NULL;
    }
    if (a == b) {
        JS_ThrowTypeError(ctx, "%s: bodyA and bodyB must be different bodies", where);
        return NULL;
    }
    base->bodyIdA = a->id.body;
    base->bodyIdB = b->id.body;

    *has_options = b2js_options(ctx, argc, argv, 2, where);
    if (*has_options < 0 ||
        (*has_options && (b2js_opt_bool(ctx, argv[2], "collideConnected", where, &base->collideConnected) < 0 ||
            b2js_opt_float(ctx, argv[2], "forceThreshold", where, B2JS_NONNEG, &base->forceThreshold) < 0 ||
            b2js_opt_float(ctx, argv[2], "torqueThreshold", where, B2JS_NONNEG, &base->torqueThreshold) < 0 ||
            b2js_opt_float(ctx, argv[2], "constraintHertz", where, B2JS_NONNEG, &base->constraintHertz) < 0 ||
            b2js_opt_float(ctx, argv[2], "constraintDampingRatio", where, B2JS_NONNEG,
                &base->constraintDampingRatio) < 0)))
        return NULL;
    return world;
}

/* Before using the bodies: option getters may have destroyed them (or the world). */
static int joint_bodies_alive(JSContext *ctx, const B2JSWorld *world, const b2JointDef *base, const char *where) {
    if (b2js_world_still_alive(ctx, world, where) < 0)
        return -1;
    if (b2Body_IsValid(base->bodyIdA) && b2Body_IsValid(base->bodyIdB))
        return 0;
    JS_ThrowTypeError(ctx, "%s: a body was destroyed while the arguments were read", where);
    return -1;
}

/*
 * options.anchor (world point) and options.axis (world direction, only when
 * default_axis is given) define both joint frames; see athena_box2d_joint_frames.
 */
static int joint_frames(JSContext *ctx, const B2JSWorld *world, int has_options, JSValueConst options, const char *where,
    const b2Vec2 *default_axis, b2JointDef *base) {
    b2Vec2 anchor, axis = default_axis ? *default_axis : (b2Vec2){ 1.0f, 0.0f };
    int has_anchor = 0;

    if (has_options) {
        has_anchor = b2js_opt_vec2(ctx, options, "anchor", where, &anchor);
        if (has_anchor < 0 || (default_axis && b2js_opt_vec2(ctx, options, "axis", where, &axis) < 0))
            return -1;
    }
    if (b2LengthSquared(axis) < 1e-12f) {
        JS_ThrowRangeError(ctx, "%s: axis must not be zero", where);
        return -1;
    }
    if (joint_bodies_alive(ctx, world, base, where) < 0)
        return -1;
    athena_box2d_joint_frames(base, has_anchor ? &anchor : NULL, b2MakeRotFromUnitVector(b2Normalize(axis)));
    return 0;
}

/* Script object for a new joint, taking user_data. Destroys the joint on failure. */
static JSValue joint_result(JSContext *ctx, B2JSWorld *world, JSValue user_data, b2JointId joint) {
    JSValue object = b2js_wrap_joint(ctx, world, joint);

    if (JS_IsException(object))
        b2DestroyJoint(joint, false);
    else if (!JS_IsUndefined(user_data))
        b2js_set_user_data(ctx, b2Joint_GetUserData(joint), user_data);
    JS_FreeValue(ctx, user_data);
    return object;
}

/* Reads options.userData, then evaluates `create` last. */
#define JOINT_RESULT(create)                                                     \
    do {                                                                         \
        JSValue user_data_ = has_options ?                                       \
            JS_GetPropertyStr(ctx, argv[2], "userData") : JS_UNDEFINED;          \
        if (JS_IsException(user_data_))                                          \
            return user_data_;                                                   \
        if (joint_bodies_alive(ctx, world, &def.base, where) < 0 ||              \
            b2js_check_memory(ctx, where) < 0) {                                 \
            JS_FreeValue(ctx, user_data_);                                       \
            return JS_EXCEPTION;                                                 \
        }                                                                        \
        return joint_result(ctx, world, user_data_, (create));                   \
    } while (0)

/* Spring options shared by distance, revolute, prismatic and wheel joints. */
static int spring_options(JSContext *ctx, JSValueConst options, const char *where,
    bool *enable, float *hertz, float *damping) {
    if (b2js_opt_bool(ctx, options, "enableSpring", where, enable) < 0 ||
        b2js_opt_float(ctx, options, "hertz", where, B2JS_NONNEG, hertz) < 0 ||
        b2js_opt_float(ctx, options, "dampingRatio", where, B2JS_NONNEG, damping) < 0)
        return -1;
    return 0;
}

static int translation_limits(JSContext *ctx, JSValueConst options, const char *where,
    bool *enable, float *lower, float *upper) {
    if (b2js_opt_bool(ctx, options, "enableLimit", where, enable) < 0 ||
        b2js_opt_float(ctx, options, "lowerTranslation", where, B2JS_ANY, lower) < 0 ||
        b2js_opt_float(ctx, options, "upperTranslation", where, B2JS_ANY, upper) < 0)
        return -1;
    if (*lower > *upper) {
        JS_ThrowRangeError(ctx, "%s: lowerTranslation must be <= upperTranslation", where);
        return -1;
    }
    return 0;
}

static JSValue world_create_distance_joint(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.createDistanceJoint";
    b2DistanceJointDef def = b2DefaultDistanceJointDef();
    int has_options, has_length = 0;
    B2JSWorld *world = joint_begin(ctx, this_val, argc, argv, where, &def.base, &has_options);

    if (!world)
        return JS_EXCEPTION;
    if (has_options) {
        JSValueConst options = argv[2];
        b2Vec2 anchor;
        int has_anchor = b2js_opt_vec2(ctx, options, "anchor", where, &anchor);

        if (has_anchor < 0)
            return JS_EXCEPTION;
        if (has_anchor) {
            if (joint_bodies_alive(ctx, world, &def.base, where) < 0)
                return JS_EXCEPTION;
            athena_box2d_joint_frames(&def.base, &anchor, b2Rot_identity);
        } else if (b2js_opt_vec2(ctx, options, "anchorA", where, &def.base.localFrameA.p) < 0 ||
            b2js_opt_vec2(ctx, options, "anchorB", where, &def.base.localFrameB.p) < 0) {
            return JS_EXCEPTION;
        }
        has_length = b2js_opt_float(ctx, options, "length", where, B2JS_POS, &def.length);
        if (has_length < 0 ||
            spring_options(ctx, options, where, &def.enableSpring, &def.hertz, &def.dampingRatio) < 0 ||
            b2js_opt_bool(ctx, options, "enableLimit", where, &def.enableLimit) < 0 ||
            b2js_opt_float(ctx, options, "minLength", where, B2JS_NONNEG, &def.minLength) < 0 ||
            b2js_opt_float(ctx, options, "maxLength", where, B2JS_NONNEG, &def.maxLength) < 0 ||
            b2js_opt_bool(ctx, options, "enableMotor", where, &def.enableMotor) < 0 ||
            b2js_opt_float(ctx, options, "maxMotorForce", where, B2JS_NONNEG, &def.maxMotorForce) < 0 ||
            b2js_opt_float(ctx, options, "motorSpeed", where, B2JS_ANY, &def.motorSpeed) < 0 ||
            b2js_opt_float(ctx, options, "lowerSpringForce", where, B2JS_ANY, &def.lowerSpringForce) < 0 ||
            b2js_opt_float(ctx, options, "upperSpringForce", where, B2JS_ANY, &def.upperSpringForce) < 0)
            return JS_EXCEPTION;
        /* Asserted by Box2D. */
        if (def.lowerSpringForce > def.upperSpringForce)
            return JS_ThrowRangeError(ctx, "%s: lowerSpringForce must be <= upperSpringForce", where);
    }
    if (!has_length) {
        if (joint_bodies_alive(ctx, world, &def.base, where) < 0)
            return JS_EXCEPTION;
        /* Rest length: the current distance between the anchors (Box2D requires > 0). */
        b2Vec2 a = b2Body_GetWorldPoint(def.base.bodyIdA, def.base.localFrameA.p);
        b2Vec2 b = b2Body_GetWorldPoint(def.base.bodyIdB, def.base.localFrameB.p);
        def.length = b2MaxFloat(b2Distance(a, b), athena_box2d_linear_slop());
    }
    JOINT_RESULT(b2CreateDistanceJoint(world->id, &def));
}

static JSValue world_create_revolute_joint(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.createRevoluteJoint";
    b2RevoluteJointDef def = b2DefaultRevoluteJointDef();
    int has_options;
    B2JSWorld *world = joint_begin(ctx, this_val, argc, argv, where, &def.base, &has_options);

    if (!world || joint_frames(ctx, world, has_options, argv[2], where, NULL, &def.base) < 0)
        return JS_EXCEPTION;
    if (has_options) {
        JSValueConst options = argv[2];
        if (spring_options(ctx, options, where, &def.enableSpring, &def.hertz, &def.dampingRatio) < 0 ||
            b2js_opt_bool(ctx, options, "enableLimit", where, &def.enableLimit) < 0 ||
            b2js_opt_float(ctx, options, "lowerAngle", where, B2JS_ANY, &def.lowerAngle) < 0 ||
            b2js_opt_float(ctx, options, "upperAngle", where, B2JS_ANY, &def.upperAngle) < 0 ||
            b2js_opt_bool(ctx, options, "enableMotor", where, &def.enableMotor) < 0 ||
            b2js_opt_float(ctx, options, "motorSpeed", where, B2JS_ANY, &def.motorSpeed) < 0 ||
            b2js_opt_float(ctx, options, "maxMotorTorque", where, B2JS_NONNEG, &def.maxMotorTorque) < 0 ||
            b2js_opt_float(ctx, options, "targetAngle", where, B2JS_ANY, &def.targetAngle) < 0)
            return JS_EXCEPTION;
    }
    /* Checked by Box2D even with the limit disabled. */
    if (!athena_box2d_revolute_limits_valid(def.lowerAngle, def.upperAngle))
        return JS_ThrowRangeError(ctx, "%s: lowerAngle must be <= upperAngle, both within +-0.99 * PI", where);
    JOINT_RESULT(b2CreateRevoluteJoint(world->id, &def));
}

static JSValue world_create_prismatic_joint(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.createPrismaticJoint";
    static const b2Vec2 default_axis = { 1.0f, 0.0f };
    b2PrismaticJointDef def = b2DefaultPrismaticJointDef();
    int has_options;
    B2JSWorld *world = joint_begin(ctx, this_val, argc, argv, where, &def.base, &has_options);

    if (!world || joint_frames(ctx, world, has_options, argv[2], where, &default_axis, &def.base) < 0)
        return JS_EXCEPTION;
    if (has_options) {
        JSValueConst options = argv[2];
        if (spring_options(ctx, options, where, &def.enableSpring, &def.hertz, &def.dampingRatio) < 0 ||
            translation_limits(ctx, options, where, &def.enableLimit, &def.lowerTranslation,
                &def.upperTranslation) < 0 ||
            b2js_opt_bool(ctx, options, "enableMotor", where, &def.enableMotor) < 0 ||
            b2js_opt_float(ctx, options, "motorSpeed", where, B2JS_ANY, &def.motorSpeed) < 0 ||
            b2js_opt_float(ctx, options, "maxMotorForce", where, B2JS_NONNEG, &def.maxMotorForce) < 0 ||
            b2js_opt_float(ctx, options, "targetTranslation", where, B2JS_COORD, &def.targetTranslation) < 0)
            return JS_EXCEPTION;
    }
    JOINT_RESULT(b2CreatePrismaticJoint(world->id, &def));
}

static JSValue world_create_weld_joint(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.createWeldJoint";
    b2WeldJointDef def = b2DefaultWeldJointDef();
    int has_options;
    B2JSWorld *world = joint_begin(ctx, this_val, argc, argv, where, &def.base, &has_options);

    /* Frames sharing a world rotation keep the current relative angle. */
    if (!world || joint_frames(ctx, world, has_options, argv[2], where, NULL, &def.base) < 0)
        return JS_EXCEPTION;
    if (has_options) {
        JSValueConst options = argv[2];
        if (b2js_opt_float(ctx, options, "linearHertz", where, B2JS_NONNEG, &def.linearHertz) < 0 ||
            b2js_opt_float(ctx, options, "angularHertz", where, B2JS_NONNEG, &def.angularHertz) < 0 ||
            b2js_opt_float(ctx, options, "linearDampingRatio", where, B2JS_NONNEG, &def.linearDampingRatio) < 0 ||
            b2js_opt_float(ctx, options, "angularDampingRatio", where, B2JS_NONNEG, &def.angularDampingRatio) < 0)
            return JS_EXCEPTION;
    }
    JOINT_RESULT(b2CreateWeldJoint(world->id, &def));
}

static JSValue world_create_wheel_joint(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.createWheelJoint";
    static const b2Vec2 default_axis = { 0.0f, 1.0f };
    b2WheelJointDef def = b2DefaultWheelJointDef();
    int has_options;
    B2JSWorld *world = joint_begin(ctx, this_val, argc, argv, where, &def.base, &has_options);

    if (!world || joint_frames(ctx, world, has_options, argv[2], where, &default_axis, &def.base) < 0)
        return JS_EXCEPTION;
    if (has_options) {
        JSValueConst options = argv[2];
        if (spring_options(ctx, options, where, &def.enableSpring, &def.hertz, &def.dampingRatio) < 0 ||
            translation_limits(ctx, options, where, &def.enableLimit, &def.lowerTranslation,
                &def.upperTranslation) < 0 ||
            b2js_opt_bool(ctx, options, "enableMotor", where, &def.enableMotor) < 0 ||
            b2js_opt_float(ctx, options, "motorSpeed", where, B2JS_ANY, &def.motorSpeed) < 0 ||
            b2js_opt_float(ctx, options, "maxMotorTorque", where, B2JS_NONNEG, &def.maxMotorTorque) < 0)
            return JS_EXCEPTION;
    }
    JOINT_RESULT(b2CreateWheelJoint(world->id, &def));
}

static JSValue world_create_motor_joint(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.createMotorJoint";
    b2MotorJointDef def = b2DefaultMotorJointDef();
    int has_options;
    B2JSWorld *world = joint_begin(ctx, this_val, argc, argv, where, &def.base, &has_options);

    if (!world)
        return JS_EXCEPTION;
    if (has_options) {
        JSValueConst options = argv[2];
        if (b2js_opt_vec2(ctx, options, "linearVelocity", where, &def.linearVelocity) < 0 ||
            b2js_opt_float(ctx, options, "angularVelocity", where, B2JS_ANY, &def.angularVelocity) < 0 ||
            b2js_opt_float(ctx, options, "maxVelocityForce", where, B2JS_NONNEG, &def.maxVelocityForce) < 0 ||
            b2js_opt_float(ctx, options, "maxVelocityTorque", where, B2JS_NONNEG, &def.maxVelocityTorque) < 0 ||
            b2js_opt_float(ctx, options, "linearHertz", where, B2JS_NONNEG, &def.linearHertz) < 0 ||
            b2js_opt_float(ctx, options, "linearDampingRatio", where, B2JS_NONNEG, &def.linearDampingRatio) < 0 ||
            b2js_opt_float(ctx, options, "maxSpringForce", where, B2JS_NONNEG, &def.maxSpringForce) < 0 ||
            b2js_opt_float(ctx, options, "angularHertz", where, B2JS_NONNEG, &def.angularHertz) < 0 ||
            b2js_opt_float(ctx, options, "angularDampingRatio", where, B2JS_NONNEG, &def.angularDampingRatio) < 0 ||
            b2js_opt_float(ctx, options, "maxSpringTorque", where, B2JS_NONNEG, &def.maxSpringTorque) < 0)
            return JS_EXCEPTION;
    }
    JOINT_RESULT(b2CreateMotorJoint(world->id, &def));
}

static JSValue world_create_filter_joint(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "World.createFilterJoint";
    b2FilterJointDef def = b2DefaultFilterJointDef();
    int has_options;
    B2JSWorld *world = joint_begin(ctx, this_val, argc, argv, where, &def.base, &has_options);

    if (!world)
        return JS_EXCEPTION;
    JOINT_RESULT(b2CreateFilterJoint(world->id, &def));
}

const JSCFunctionListEntry b2js_world_joint_funcs[] = {
    JS_CFUNC_DEF("createDistanceJoint", 3, world_create_distance_joint),
    JS_CFUNC_DEF("createRevoluteJoint", 3, world_create_revolute_joint),
    JS_CFUNC_DEF("createPrismaticJoint", 3, world_create_prismatic_joint),
    JS_CFUNC_DEF("createWeldJoint", 3, world_create_weld_joint),
    JS_CFUNC_DEF("createWheelJoint", 3, world_create_wheel_joint),
    JS_CFUNC_DEF("createMotorJoint", 3, world_create_motor_joint),
    JS_CFUNC_DEF("createFilterJoint", 3, world_create_filter_joint),
};
const int b2js_world_joint_funcs_count = countof(b2js_world_joint_funcs);

/* ------------------------------------------------------------------------ */
/* Joint                                                                     */
/* ------------------------------------------------------------------------ */

#define JOINT_THIS(where, min, max)                                              \
    B2JSHandle *handle = b2js_handle(ctx, this_val, B2JS_JOINT, where);          \
    if (!handle || b2js_argc(ctx, argc, min, max, where) < 0)                    \
        return JS_EXCEPTION;                                                     \
    b2JointId id = handle->id.joint

#define SPRING_JOINTS "distance, revolute, prismatic and wheel"

/* Accessors of the parameters dispatched by athena_box2d_joint_get/set. */
static const struct {
    const char *get;
    const char *set;   /* NULL: read-only */
    bool flag;         /* boolean parameter */
    B2JSRange range;   /* setter domain */
    const char *types; /* joint types that have it, for errors */
} joint_params[ATHENA_BOX2D_JOINT_PARAM_COUNT] = {
    [ATHENA_BOX2D_SPRING_ENABLED] = { "Joint.isSpringEnabled", "Joint.enableSpring", true, B2JS_ANY, SPRING_JOINTS },
    [ATHENA_BOX2D_SPRING_HERTZ] = { "Joint.getSpringHertz", "Joint.setSpringHertz", false, B2JS_NONNEG, SPRING_JOINTS },
    [ATHENA_BOX2D_SPRING_DAMPING_RATIO] = { "Joint.getSpringDampingRatio", "Joint.setSpringDampingRatio",
        false, B2JS_NONNEG, SPRING_JOINTS },
    [ATHENA_BOX2D_LIMIT_ENABLED] = { "Joint.isLimitEnabled", "Joint.enableLimit", true, B2JS_ANY, SPRING_JOINTS },
    [ATHENA_BOX2D_LOWER_LIMIT] = { NULL, NULL, false, B2JS_ANY, SPRING_JOINTS },
    [ATHENA_BOX2D_UPPER_LIMIT] = { NULL, NULL, false, B2JS_ANY, SPRING_JOINTS },
    [ATHENA_BOX2D_MOTOR_ENABLED] = { "Joint.isMotorEnabled", "Joint.enableMotor", true, B2JS_ANY, SPRING_JOINTS },
    [ATHENA_BOX2D_MOTOR_SPEED] = { "Joint.getMotorSpeed", "Joint.setMotorSpeed", false, B2JS_ANY, SPRING_JOINTS },
    [ATHENA_BOX2D_MAX_MOTOR_FORCE] = { "Joint.getMaxMotorForce", "Joint.setMaxMotorForce",
        false, B2JS_NONNEG, "distance and prismatic" },
    [ATHENA_BOX2D_MOTOR_FORCE] = { "Joint.getMotorForce", NULL, false, B2JS_ANY, "distance and prismatic" },
    [ATHENA_BOX2D_MAX_MOTOR_TORQUE] = { "Joint.getMaxMotorTorque", "Joint.setMaxMotorTorque",
        false, B2JS_NONNEG, "revolute and wheel" },
    [ATHENA_BOX2D_MOTOR_TORQUE] = { "Joint.getMotorTorque", NULL, false, B2JS_ANY, "revolute and wheel" },
    [ATHENA_BOX2D_LINEAR_HERTZ] = { "Joint.getLinearHertz", "Joint.setLinearHertz",
        false, B2JS_NONNEG, "weld and motor" },
    [ATHENA_BOX2D_LINEAR_DAMPING_RATIO] = { "Joint.getLinearDampingRatio", "Joint.setLinearDampingRatio",
        false, B2JS_NONNEG, "weld and motor" },
    [ATHENA_BOX2D_ANGULAR_HERTZ] = { "Joint.getAngularHertz", "Joint.setAngularHertz",
        false, B2JS_NONNEG, "weld and motor" },
    [ATHENA_BOX2D_ANGULAR_DAMPING_RATIO] = { "Joint.getAngularDampingRatio", "Joint.setAngularDampingRatio",
        false, B2JS_NONNEG, "weld and motor" },
    [ATHENA_BOX2D_ANGULAR_VELOCITY] = { "Joint.getAngularVelocity", "Joint.setAngularVelocity",
        false, B2JS_ANY, "motor" },
    [ATHENA_BOX2D_MAX_VELOCITY_FORCE] = { "Joint.getMaxVelocityForce", "Joint.setMaxVelocityForce",
        false, B2JS_NONNEG, "motor" },
    [ATHENA_BOX2D_MAX_VELOCITY_TORQUE] = { "Joint.getMaxVelocityTorque", "Joint.setMaxVelocityTorque",
        false, B2JS_NONNEG, "motor" },
    [ATHENA_BOX2D_MAX_SPRING_FORCE] = { "Joint.getMaxSpringForce", "Joint.setMaxSpringForce",
        false, B2JS_NONNEG, "motor" },
    [ATHENA_BOX2D_MAX_SPRING_TORQUE] = { "Joint.getMaxSpringTorque", "Joint.setMaxSpringTorque",
        false, B2JS_NONNEG, "motor" },
    [ATHENA_BOX2D_LENGTH] = { "Joint.getLength", "Joint.setLength", false, B2JS_POS, "distance" },
    [ATHENA_BOX2D_CURRENT_LENGTH] = { "Joint.getCurrentLength", NULL, false, B2JS_ANY, "distance" },
    [ATHENA_BOX2D_ANGLE] = { "Joint.getAngle", NULL, false, B2JS_ANY, "revolute" },
    [ATHENA_BOX2D_TRANSLATION] = { "Joint.getTranslation", NULL, false, B2JS_ANY, "prismatic" },
    [ATHENA_BOX2D_SPEED] = { "Joint.getSpeed", NULL, false, B2JS_ANY, "prismatic" },
    [ATHENA_BOX2D_TARGET_ANGLE] = { "Joint.getTargetAngle", "Joint.setTargetAngle", false, B2JS_ANY, "revolute" },
    [ATHENA_BOX2D_TARGET_TRANSLATION] = { "Joint.getTargetTranslation", "Joint.setTargetTranslation",
        false, B2JS_COORD, "prismatic" },
};

static JSValue joint_not_applicable(JSContext *ctx, b2JointId id, const char *where, const char *types) {
    return JS_ThrowTypeError(ctx, "%s does not apply to %s joints (only %s)", where,
        athena_box2d_joint_type_name(b2Joint_GetType(id)), types);
}

static JSValue joint_get_param(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    const char *where = joint_params[magic].get;
    JOINT_THIS(where, 0, 0);
    float value;

    if (!athena_box2d_joint_get(id, (AthenaBox2DJointParam)magic, &value))
        return joint_not_applicable(ctx, id, where, joint_params[magic].types);
    if (joint_params[magic].flag)
        return JS_NewBool(ctx, value != 0.0f);
    return JS_NewFloat32(ctx, value);
}

static JSValue joint_set_param(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    const char *where = joint_params[magic].set;
    JOINT_THIS(where, 1, 1);
    float value;
    bool flag;

    if (joint_params[magic].flag) {
        if (b2js_bool(ctx, argv[0], where, "flag", &flag) < 0)
            return JS_EXCEPTION;
        value = flag ? 1.0f : 0.0f;
    } else if (b2js_float(ctx, argv[0], where, "value", joint_params[magic].range, &value) < 0) {
        return JS_EXCEPTION;
    }
    if (!athena_box2d_joint_set(id, (AthenaBox2DJointParam)magic, value))
        return joint_not_applicable(ctx, id, where, joint_params[magic].types);
    return JS_UNDEFINED;
}

static JSValue joint_get_limits(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Joint.getLimits";
    JOINT_THIS(where, 0, 0);
    float lower, upper;
    JSValue object;

    if (!athena_box2d_joint_get(id, ATHENA_BOX2D_LOWER_LIMIT, &lower) ||
        !athena_box2d_joint_get(id, ATHENA_BOX2D_UPPER_LIMIT, &upper))
        return joint_not_applicable(ctx, id, where, SPRING_JOINTS);
    object = JS_NewObject(ctx);
    if (JS_IsException(object))
        return object;
    JS_SetPropertyStr(ctx, object, "lower", JS_NewFloat32(ctx, lower));
    JS_SetPropertyStr(ctx, object, "upper", JS_NewFloat32(ctx, upper));
    return object;
}

static JSValue joint_set_limits(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Joint.setLimits";
    JOINT_THIS(where, 2, 2);
    b2JointType type = b2Joint_GetType(id);
    float lower, upper;

    if (type != b2_distanceJoint && type != b2_revoluteJoint && type != b2_prismaticJoint && type != b2_wheelJoint)
        return joint_not_applicable(ctx, id, where, SPRING_JOINTS);
    if (b2js_float(ctx, argv[0], where, "lower", B2JS_ANY, &lower) < 0 ||
        b2js_float(ctx, argv[1], where, "upper", B2JS_ANY, &upper) < 0)
        return JS_EXCEPTION;
    if (!athena_box2d_joint_set_limits(id, lower, upper)) {
        if (type == b2_revoluteJoint)
            return JS_ThrowRangeError(ctx, "%s: lower must be <= upper, both within +-0.99 * PI", where);
        return JS_ThrowRangeError(ctx, "%s: lower must be <= upper", where);
    }
    return JS_UNDEFINED;
}

static JSValue joint_get_linear_velocity(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Joint.getLinearVelocity";
    JOINT_THIS(where, 0, 0);

    if (b2Joint_GetType(id) != b2_motorJoint)
        return joint_not_applicable(ctx, id, where, "motor");
    return b2js_new_vec2(ctx, b2MotorJoint_GetLinearVelocity(id));
}

static JSValue joint_set_linear_velocity(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Joint.setLinearVelocity";
    JOINT_THIS(where, 2, 2);
    b2Vec2 velocity;

    if (b2Joint_GetType(id) != b2_motorJoint)
        return joint_not_applicable(ctx, id, where, "motor");
    if (b2js_float(ctx, argv[0], where, "x", B2JS_COORD, &velocity.x) < 0 ||
        b2js_float(ctx, argv[1], where, "y", B2JS_COORD, &velocity.y) < 0)
        return JS_EXCEPTION;
    b2MotorJoint_SetLinearVelocity(id, velocity);
    return JS_UNDEFINED;
}

static JSValue joint_get_type(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JOINT_THIS("Joint.getType", 0, 0);

    return JS_NewString(ctx, athena_box2d_joint_type_name(b2Joint_GetType(id)));
}

enum {
    JOINT_BODY_A = 0,
    JOINT_BODY_B,
};

static JSValue joint_get_body(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    JOINT_THIS(magic == JOINT_BODY_A ? "Joint.getBodyA" : "Joint.getBodyB", 0, 0);

    return b2js_wrap_body(ctx, handle->world, magic == JOINT_BODY_A ? b2Joint_GetBodyA(id) : b2Joint_GetBodyB(id));
}

static JSValue joint_get_user_data(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JOINT_THIS("Joint.getUserData", 0, 0);

    (void)id;
    return JS_DupValue(ctx, handle->user_data);
}

static JSValue joint_set_user_data(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JOINT_THIS("Joint.setUserData", 1, 1);

    (void)id;
    b2js_set_user_data(ctx, handle, argv[0]);
    return JS_UNDEFINED;
}

static JSValue joint_get_collide_connected(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JOINT_THIS("Joint.getCollideConnected", 0, 0);

    return JS_NewBool(ctx, b2Joint_GetCollideConnected(id));
}

static JSValue joint_set_collide_connected(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Joint.setCollideConnected";
    JOINT_THIS(where, 1, 1);
    bool flag;

    if (b2js_bool(ctx, argv[0], where, "flag", &flag) < 0)
        return JS_EXCEPTION;
    b2Joint_SetCollideConnected(id, flag);
    return JS_UNDEFINED;
}

/* Joint events (World.getJointEvents) fire when the constraint force or torque exceeds these. */
enum {
    JOINT_FORCE_THRESHOLD = 0,
    JOINT_TORQUE_THRESHOLD,
};

static JSValue joint_get_threshold(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    JOINT_THIS(magic == JOINT_FORCE_THRESHOLD ? "Joint.getForceThreshold" : "Joint.getTorqueThreshold", 0, 0);

    return JS_NewFloat32(ctx, magic == JOINT_FORCE_THRESHOLD ?
        b2Joint_GetForceThreshold(id) : b2Joint_GetTorqueThreshold(id));
}

static JSValue joint_set_threshold(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    const char *where = magic == JOINT_FORCE_THRESHOLD ? "Joint.setForceThreshold" : "Joint.setTorqueThreshold";
    JOINT_THIS(where, 1, 1);
    float value;

    if (b2js_float(ctx, argv[0], where, "threshold", B2JS_NONNEG, &value) < 0)
        return JS_EXCEPTION;
    if (magic == JOINT_FORCE_THRESHOLD)
        b2Joint_SetForceThreshold(id, value);
    else
        b2Joint_SetTorqueThreshold(id, value);
    return JS_UNDEFINED;
}

static JSValue joint_wake_bodies(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JOINT_THIS("Joint.wakeBodies", 0, 0);

    b2Joint_WakeBodies(id);
    return JS_UNDEFINED;
}

static JSValue joint_get_constraint_force(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JOINT_THIS("Joint.getConstraintForce", 0, 0);

    return b2js_new_vec2(ctx, b2Joint_GetConstraintForce(id));
}

static JSValue joint_get_constraint_torque(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JOINT_THIS("Joint.getConstraintTorque", 0, 0);

    return JS_NewFloat32(ctx, b2Joint_GetConstraintTorque(id));
}

/* Joint.getSpringForceRange() -> { lower, upper } (distance). */
static JSValue joint_get_spring_force_range(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Joint.getSpringForceRange";
    JOINT_THIS(where, 0, 0);
    float lower, upper;
    JSValue object;

    if (b2Joint_GetType(id) != b2_distanceJoint)
        return joint_not_applicable(ctx, id, where, "distance");
    b2DistanceJoint_GetSpringForceRange(id, &lower, &upper);
    object = JS_NewObject(ctx);
    if (JS_IsException(object))
        return object;
    JS_SetPropertyStr(ctx, object, "lower", JS_NewFloat32(ctx, lower));
    JS_SetPropertyStr(ctx, object, "upper", JS_NewFloat32(ctx, upper));
    return object;
}

/* Joint.setSpringForceRange(lower, upper): force the spring may apply (distance; lower <= upper). */
static JSValue joint_set_spring_force_range(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Joint.setSpringForceRange";
    JOINT_THIS(where, 2, 2);
    float lower, upper;

    if (b2Joint_GetType(id) != b2_distanceJoint)
        return joint_not_applicable(ctx, id, where, "distance");
    if (b2js_float(ctx, argv[0], where, "lower", B2JS_ANY, &lower) < 0 ||
        b2js_float(ctx, argv[1], where, "upper", B2JS_ANY, &upper) < 0)
        return JS_EXCEPTION;
    if (lower > upper)
        return JS_ThrowRangeError(ctx, "%s: lower must be <= upper", where);
    b2DistanceJoint_SetSpringForceRange(id, lower, upper);
    return JS_UNDEFINED;
}

enum {
    JOINT_FRAME_A = 0,
    JOINT_FRAME_B,
};

/* Joint.getLocalFrameA/B() -> { x, y, angle }: the joint frame in the body's local space. */
static JSValue joint_get_local_frame(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    JOINT_THIS(magic == JOINT_FRAME_A ? "Joint.getLocalFrameA" : "Joint.getLocalFrameB", 0, 0);
    b2Transform frame = magic == JOINT_FRAME_A ? b2Joint_GetLocalFrameA(id) : b2Joint_GetLocalFrameB(id);
    JSValue object = JS_NewObject(ctx);

    if (JS_IsException(object))
        return object;
    b2js_set(ctx, object, b2js_atoms.x, JS_NewFloat32(ctx, frame.p.x));
    b2js_set(ctx, object, b2js_atoms.y, JS_NewFloat32(ctx, frame.p.y));
    b2js_set(ctx, object, b2js_atoms.angle, JS_NewFloat32(ctx, b2Rot_GetAngle(frame.q)));
    return object;
}

/* Joint.setLocalFrameA/B(x, y, angle): moves the joint anchor/axis on one body (local space). */
static JSValue joint_set_local_frame(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    const char *where = magic == JOINT_FRAME_A ? "Joint.setLocalFrameA" : "Joint.setLocalFrameB";
    JOINT_THIS(where, 3, 3);
    b2Transform frame;
    float angle;

    if (b2js_float(ctx, argv[0], where, "x", B2JS_COORD, &frame.p.x) < 0 ||
        b2js_float(ctx, argv[1], where, "y", B2JS_COORD, &frame.p.y) < 0 ||
        b2js_float(ctx, argv[2], where, "angle", B2JS_ANY, &angle) < 0)
        return JS_EXCEPTION;
    frame.q = athena_box2d_make_rot(angle);
    if (magic == JOINT_FRAME_A)
        b2Joint_SetLocalFrameA(id, frame);
    else
        b2Joint_SetLocalFrameB(id, frame);
    return JS_UNDEFINED;
}

enum {
    JOINT_LINEAR_SEPARATION = 0,
    JOINT_ANGULAR_SEPARATION,
};

/* Constraint error (meters / radians): how far the solver is from satisfying the joint. */
static JSValue joint_get_separation(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
    JOINT_THIS(magic == JOINT_LINEAR_SEPARATION ? "Joint.getLinearSeparation" : "Joint.getAngularSeparation", 0, 0);

    return JS_NewFloat32(ctx, magic == JOINT_LINEAR_SEPARATION ?
        b2Joint_GetLinearSeparation(id) : b2Joint_GetAngularSeparation(id));
}

static JSValue joint_get_constraint_tuning(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JOINT_THIS("Joint.getConstraintTuning", 0, 0);
    float hertz, damping;
    JSValue object;

    b2Joint_GetConstraintTuning(id, &hertz, &damping);
    object = JS_NewObject(ctx);
    if (JS_IsException(object))
        return object;
    JS_SetPropertyStr(ctx, object, "hertz", JS_NewFloat32(ctx, hertz));
    JS_SetPropertyStr(ctx, object, "dampingRatio", JS_NewFloat32(ctx, damping));
    return object;
}

/* Joint.setConstraintTuning(hertz, dampingRatio): stiffness of the joint constraint (advanced). */
static JSValue joint_set_constraint_tuning(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Joint.setConstraintTuning";
    JOINT_THIS(where, 2, 2);
    float hertz, damping;

    if (b2js_float(ctx, argv[0], where, "hertz", B2JS_NONNEG, &hertz) < 0 ||
        b2js_float(ctx, argv[1], where, "dampingRatio", B2JS_NONNEG, &damping) < 0)
        return JS_EXCEPTION;
    b2Joint_SetConstraintTuning(id, hertz, damping);
    return JS_UNDEFINED;
}

static JSValue joint_destroy(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    static const char where[] = "Joint.destroy";
    B2JSHandle *handle = b2js_handle_any(this_val, B2JS_JOINT);
    bool wake = true;
    b2JointId id;

    if (b2js_argc(ctx, argc, 0, 1, where) < 0)
        return JS_EXCEPTION;
    if (!handle)
        return JS_ThrowTypeError(ctx, "%s: expected a Box2D Joint", where);
    if (handle->dead)
        return JS_FALSE;
    if (b2js_has(argc, argv, 0) && b2js_bool(ctx, argv[0], where, "wakeBodies", &wake) < 0)
        return JS_EXCEPTION;

    id = handle->id.joint;
    b2js_kill(JS_GetRuntime(ctx), handle);
    b2DestroyJoint(id, wake);
    return JS_TRUE;
}

static JSValue joint_is_valid(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    B2JSHandle *handle = b2js_handle_any(this_val, B2JS_JOINT);

    if (b2js_argc(ctx, argc, 0, 0, "Joint.isValid") < 0)
        return JS_EXCEPTION;
    return JS_NewBool(ctx, handle && !handle->dead);
}

#define PARAM_GET(name, param) JS_CFUNC_MAGIC_DEF(name, 0, joint_get_param, param)
#define PARAM_SET(name, param) JS_CFUNC_MAGIC_DEF(name, 1, joint_set_param, param)

static const JSCFunctionListEntry joint_proto_funcs[] = {
    JS_CFUNC_DEF("isValid", 0, joint_is_valid),
    JS_CFUNC_DEF("destroy", 1, joint_destroy),
    JS_CFUNC_DEF("getType", 0, joint_get_type),
    JS_CFUNC_MAGIC_DEF("getBodyA", 0, joint_get_body, JOINT_BODY_A),
    JS_CFUNC_MAGIC_DEF("getBodyB", 0, joint_get_body, JOINT_BODY_B),
    JS_CFUNC_DEF("getUserData", 0, joint_get_user_data),
    JS_CFUNC_DEF("setUserData", 1, joint_set_user_data),
    JS_CFUNC_DEF("setCollideConnected", 1, joint_set_collide_connected),
    JS_CFUNC_DEF("getCollideConnected", 0, joint_get_collide_connected),
    JS_CFUNC_DEF("wakeBodies", 0, joint_wake_bodies),
    JS_CFUNC_MAGIC_DEF("getForceThreshold", 0, joint_get_threshold, JOINT_FORCE_THRESHOLD),
    JS_CFUNC_MAGIC_DEF("setForceThreshold", 1, joint_set_threshold, JOINT_FORCE_THRESHOLD),
    JS_CFUNC_MAGIC_DEF("getTorqueThreshold", 0, joint_get_threshold, JOINT_TORQUE_THRESHOLD),
    JS_CFUNC_MAGIC_DEF("setTorqueThreshold", 1, joint_set_threshold, JOINT_TORQUE_THRESHOLD),
    JS_CFUNC_DEF("getConstraintForce", 0, joint_get_constraint_force),
    JS_CFUNC_DEF("getConstraintTorque", 0, joint_get_constraint_torque),
    PARAM_SET("enableSpring", ATHENA_BOX2D_SPRING_ENABLED),
    PARAM_GET("isSpringEnabled", ATHENA_BOX2D_SPRING_ENABLED),
    PARAM_SET("setSpringHertz", ATHENA_BOX2D_SPRING_HERTZ),
    PARAM_GET("getSpringHertz", ATHENA_BOX2D_SPRING_HERTZ),
    PARAM_SET("setSpringDampingRatio", ATHENA_BOX2D_SPRING_DAMPING_RATIO),
    PARAM_GET("getSpringDampingRatio", ATHENA_BOX2D_SPRING_DAMPING_RATIO),
    PARAM_SET("enableLimit", ATHENA_BOX2D_LIMIT_ENABLED),
    PARAM_GET("isLimitEnabled", ATHENA_BOX2D_LIMIT_ENABLED),
    JS_CFUNC_DEF("setLimits", 2, joint_set_limits),
    JS_CFUNC_DEF("getLimits", 0, joint_get_limits),
    PARAM_SET("enableMotor", ATHENA_BOX2D_MOTOR_ENABLED),
    PARAM_GET("isMotorEnabled", ATHENA_BOX2D_MOTOR_ENABLED),
    PARAM_SET("setMotorSpeed", ATHENA_BOX2D_MOTOR_SPEED),
    PARAM_GET("getMotorSpeed", ATHENA_BOX2D_MOTOR_SPEED),
    PARAM_SET("setMaxMotorForce", ATHENA_BOX2D_MAX_MOTOR_FORCE),
    PARAM_GET("getMaxMotorForce", ATHENA_BOX2D_MAX_MOTOR_FORCE),
    PARAM_GET("getMotorForce", ATHENA_BOX2D_MOTOR_FORCE),
    PARAM_SET("setMaxMotorTorque", ATHENA_BOX2D_MAX_MOTOR_TORQUE),
    PARAM_GET("getMaxMotorTorque", ATHENA_BOX2D_MAX_MOTOR_TORQUE),
    PARAM_GET("getMotorTorque", ATHENA_BOX2D_MOTOR_TORQUE),
    PARAM_GET("getAngle", ATHENA_BOX2D_ANGLE),
    PARAM_GET("getTranslation", ATHENA_BOX2D_TRANSLATION),
    PARAM_GET("getSpeed", ATHENA_BOX2D_SPEED),
    PARAM_GET("getLength", ATHENA_BOX2D_LENGTH),
    PARAM_SET("setLength", ATHENA_BOX2D_LENGTH),
    PARAM_GET("getCurrentLength", ATHENA_BOX2D_CURRENT_LENGTH),
    PARAM_SET("setLinearHertz", ATHENA_BOX2D_LINEAR_HERTZ),
    PARAM_GET("getLinearHertz", ATHENA_BOX2D_LINEAR_HERTZ),
    PARAM_SET("setLinearDampingRatio", ATHENA_BOX2D_LINEAR_DAMPING_RATIO),
    PARAM_GET("getLinearDampingRatio", ATHENA_BOX2D_LINEAR_DAMPING_RATIO),
    PARAM_SET("setAngularHertz", ATHENA_BOX2D_ANGULAR_HERTZ),
    PARAM_GET("getAngularHertz", ATHENA_BOX2D_ANGULAR_HERTZ),
    PARAM_SET("setAngularDampingRatio", ATHENA_BOX2D_ANGULAR_DAMPING_RATIO),
    PARAM_GET("getAngularDampingRatio", ATHENA_BOX2D_ANGULAR_DAMPING_RATIO),
    JS_CFUNC_DEF("setLinearVelocity", 2, joint_set_linear_velocity),
    JS_CFUNC_DEF("getLinearVelocity", 0, joint_get_linear_velocity),
    PARAM_SET("setAngularVelocity", ATHENA_BOX2D_ANGULAR_VELOCITY),
    PARAM_GET("getAngularVelocity", ATHENA_BOX2D_ANGULAR_VELOCITY),
    PARAM_SET("setMaxVelocityForce", ATHENA_BOX2D_MAX_VELOCITY_FORCE),
    PARAM_GET("getMaxVelocityForce", ATHENA_BOX2D_MAX_VELOCITY_FORCE),
    PARAM_SET("setMaxVelocityTorque", ATHENA_BOX2D_MAX_VELOCITY_TORQUE),
    PARAM_GET("getMaxVelocityTorque", ATHENA_BOX2D_MAX_VELOCITY_TORQUE),
    PARAM_SET("setMaxSpringForce", ATHENA_BOX2D_MAX_SPRING_FORCE),
    PARAM_GET("getMaxSpringForce", ATHENA_BOX2D_MAX_SPRING_FORCE),
    PARAM_SET("setMaxSpringTorque", ATHENA_BOX2D_MAX_SPRING_TORQUE),
    PARAM_GET("getMaxSpringTorque", ATHENA_BOX2D_MAX_SPRING_TORQUE),
    PARAM_SET("setTargetAngle", ATHENA_BOX2D_TARGET_ANGLE),
    PARAM_GET("getTargetAngle", ATHENA_BOX2D_TARGET_ANGLE),
    PARAM_SET("setTargetTranslation", ATHENA_BOX2D_TARGET_TRANSLATION),
    PARAM_GET("getTargetTranslation", ATHENA_BOX2D_TARGET_TRANSLATION),
    JS_CFUNC_DEF("setSpringForceRange", 2, joint_set_spring_force_range),
    JS_CFUNC_DEF("getSpringForceRange", 0, joint_get_spring_force_range),
    JS_CFUNC_MAGIC_DEF("getLocalFrameA", 0, joint_get_local_frame, JOINT_FRAME_A),
    JS_CFUNC_MAGIC_DEF("getLocalFrameB", 0, joint_get_local_frame, JOINT_FRAME_B),
    JS_CFUNC_MAGIC_DEF("setLocalFrameA", 3, joint_set_local_frame, JOINT_FRAME_A),
    JS_CFUNC_MAGIC_DEF("setLocalFrameB", 3, joint_set_local_frame, JOINT_FRAME_B),
    JS_CFUNC_MAGIC_DEF("getLinearSeparation", 0, joint_get_separation, JOINT_LINEAR_SEPARATION),
    JS_CFUNC_MAGIC_DEF("getAngularSeparation", 0, joint_get_separation, JOINT_ANGULAR_SEPARATION),
    JS_CFUNC_DEF("getConstraintTuning", 0, joint_get_constraint_tuning),
    JS_CFUNC_DEF("setConstraintTuning", 2, joint_set_constraint_tuning),
};

int b2js_joint_init(JSContext *ctx) {
    return b2js_class_proto(ctx, b2js_class_ids[B2JS_JOINT], joint_proto_funcs, countof(joint_proto_funcs));
}
