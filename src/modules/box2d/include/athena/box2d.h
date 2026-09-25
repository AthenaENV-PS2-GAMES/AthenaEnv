#ifndef ATH_NATIVE_BOX2D_H
#define ATH_NATIVE_BOX2D_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <box2d/box2d.h>
#include <box2d/constants.h>

/*
 * AthenaEnv helpers on top of Box2D 3.2 (<box2d/box2d.h>, vendored in this
 * module). C applications use the Box2D API directly; these functions hold
 * the rules the script bindings share with them:
 *
 * - the Box2D validity rules for input that would otherwise hit a B2_ASSERT
 *   (a trap on the EE), so callers can reject it first;
 * - joint frames built from a world anchor and axis;
 * - one entry point for the spring/limit/motor parameters that several joint
 *   types share under different function names.
 *
 * Box2D worlds are not thread-safe: use a world from one thread at a time.
 * On the EE a world always runs with one worker.
 */

/*
 * Module hook: installs the allocator (out of memory stops on the crash
 * screen instead of a NULL dereference) and the assertion handler (the
 * failed condition on the crash screen instead of a bare trap).
 * Assertions are compiled in debug builds only (see <box2d/base.h>).
 */
int athena_box2d_module_init(void);

/* ------------------------------------------------------------------------ */
/* Values and memory                                                         */
/* ------------------------------------------------------------------------ */

/*
 * A float Box2D can use: finite and below 2^127 in magnitude. Tested on the
 * bits: the EE FPU has no infinity or NaN (isfinite() is always true there)
 * and treats the all-ones exponent as a large number.
 */
static inline bool athena_box2d_valid_float(float value) {
    union { float f; uint32_t u; } bits = { value };
    return ((bits.u >> 23) & 0xFF) < 0xFE;
}

/* Largest coordinate Box2D is designed for (B2_HUGE, 100 km). */
float athena_box2d_max_coordinate(void);

/*
 * Memory budget. The limit (0 = none, the default) caps the bytes Box2D
 * holds; below ATHENA_BOX2D_RESERVE of free EE RAM nothing more is created
 * either. Box2D itself cannot recover from a failed allocation, so callers
 * check athena_box2d_memory_available() before creating objects.
 */
#define ATHENA_BOX2D_RESERVE (256u * 1024u)
void athena_box2d_set_memory_limit(size_t bytes);
size_t athena_box2d_memory_limit(void);
size_t athena_box2d_memory_used(void);
bool athena_box2d_memory_available(void);

/*
 * World snapshots (b2World_Snapshot) followed by a checksum, so a damaged
 * image (e.g. a save file) is rejected instead of deserialized. Call between
 * steps. The image is malloc'd: free() it. NULL when out of memory.
 */
uint8_t *athena_box2d_snapshot(b2WorldId world, int *size);

#define ATHENA_BOX2D_RESTORED 0
#define ATHENA_BOX2D_RESTORE_REJECTED -1 /* not a snapshot of this build; world unchanged */
#define ATHENA_BOX2D_RESTORE_FAILED -2   /* failed midway: the world is unusable, destroy it */
#define ATHENA_BOX2D_RESTORE_DAMAGED -3  /* checksum mismatch; world unchanged */
int athena_box2d_restore(b2WorldId world, const uint8_t *image, int size);

/* Script-facing names: "circle", "capsule", "segment", "polygon", "chainSegment". */
const char *athena_box2d_shape_type_name(b2ShapeType type);
/* "distance", "filter", "motor", "prismatic", "revolute", "weld", "wheel". */
const char *athena_box2d_joint_type_name(b2JointType type);

/* ------------------------------------------------------------------------ */
/* Geometry                                                                  */
/* ------------------------------------------------------------------------ */

/* Distance below which Box2D welds points together (B2_LINEAR_SLOP). */
float athena_box2d_linear_slop(void);

/*
 * Rotation of an angle in radians, exact to float precision. b2MakeRot uses
 * a polynomial approximation that is off by up to ~0.0017 rad, so an angle
 * set by a script would not read back.
 */
b2Rot athena_box2d_make_rot(float radians);

/*
 * Box with half extents hw/hh, centered at center and rotated by angle.
 * False when an extent is not positive or the area is too small for Box2D
 * to compute a mass.
 */
bool athena_box2d_make_box(float hw, float hh, b2Vec2 center, float angle, b2Polygon *out);

/*
 * Convex hull of 3..B2_MAX_POLYGON_VERTICES points, rounded by radius (>= 0).
 * False when the points are collinear, closer than the linear slop or out of
 * range.
 */
bool athena_box2d_make_polygon(const b2Vec2 *points, int count, float radius, b2Polygon *out);

/* A segment (or capsule axis) Box2D accepts: longer than the linear slop. */
bool athena_box2d_segment_valid(b2Vec2 p1, b2Vec2 p2);

/*
 * Chain points: at least 4, consecutive points (and the closing pair of a
 * loop) farther apart than the linear slop. An open chain uses its first and
 * last points as ghost vertices only: n points make n - 3 segments.
 */
bool athena_box2d_chain_valid(const b2Vec2 *points, int count, bool loop);

/* ------------------------------------------------------------------------ */
/* Joints                                                                    */
/* ------------------------------------------------------------------------ */

/*
 * Sets base->localFrameA/B from the current transforms of bodyIdA and
 * bodyIdB so both frames share the world axis rotation: the joint starts
 * relaxed (zero angle, zero translation along the axis).
 * anchor: world point of both frames; NULL puts each frame at its body origin.
 */
void athena_box2d_joint_frames(b2JointDef *base, const b2Vec2 *anchor, b2Rot axis);

/* Revolute limits Box2D accepts: lower <= upper, both within +-0.99 pi. */
bool athena_box2d_revolute_limits_valid(float lower, float upper);

/*
 * Joint parameters shared by several joint types. Booleans are 0 or 1.
 * athena_box2d_joint_get/set return false, and do nothing, when the joint
 * type does not have the parameter or the parameter is read-only.
 */
typedef enum AthenaBox2DJointParam {
    ATHENA_BOX2D_SPRING_ENABLED = 0,   /* distance, revolute, prismatic, wheel */
    ATHENA_BOX2D_SPRING_HERTZ,         /* distance, revolute, prismatic, wheel */
    ATHENA_BOX2D_SPRING_DAMPING_RATIO, /* distance, revolute, prismatic, wheel */
    ATHENA_BOX2D_LIMIT_ENABLED,        /* distance, revolute, prismatic, wheel */
    ATHENA_BOX2D_LOWER_LIMIT,          /* read-only, see athena_box2d_joint_set_limits */
    ATHENA_BOX2D_UPPER_LIMIT,          /* read-only, see athena_box2d_joint_set_limits */
    ATHENA_BOX2D_MOTOR_ENABLED,        /* distance, revolute, prismatic, wheel */
    ATHENA_BOX2D_MOTOR_SPEED,          /* distance, revolute, prismatic, wheel */
    ATHENA_BOX2D_MAX_MOTOR_FORCE,      /* distance, prismatic */
    ATHENA_BOX2D_MOTOR_FORCE,          /* distance, prismatic (read-only) */
    ATHENA_BOX2D_MAX_MOTOR_TORQUE,     /* revolute, wheel */
    ATHENA_BOX2D_MOTOR_TORQUE,         /* revolute, wheel (read-only) */
    ATHENA_BOX2D_LINEAR_HERTZ,         /* weld, motor */
    ATHENA_BOX2D_LINEAR_DAMPING_RATIO, /* weld, motor */
    ATHENA_BOX2D_ANGULAR_HERTZ,        /* weld, motor */
    ATHENA_BOX2D_ANGULAR_DAMPING_RATIO,/* weld, motor */
    ATHENA_BOX2D_ANGULAR_VELOCITY,     /* motor */
    ATHENA_BOX2D_MAX_VELOCITY_FORCE,   /* motor */
    ATHENA_BOX2D_MAX_VELOCITY_TORQUE,  /* motor */
    ATHENA_BOX2D_MAX_SPRING_FORCE,     /* motor */
    ATHENA_BOX2D_MAX_SPRING_TORQUE,    /* motor */
    ATHENA_BOX2D_LENGTH,               /* distance (rest length) */
    ATHENA_BOX2D_CURRENT_LENGTH,       /* distance (read-only) */
    ATHENA_BOX2D_ANGLE,                /* revolute (read-only) */
    ATHENA_BOX2D_TRANSLATION,          /* prismatic (read-only) */
    ATHENA_BOX2D_SPEED,                /* prismatic (read-only) */
    ATHENA_BOX2D_JOINT_PARAM_COUNT
} AthenaBox2DJointParam;

bool athena_box2d_joint_get(b2JointId joint, AthenaBox2DJointParam param, float *out);
bool athena_box2d_joint_set(b2JointId joint, AthenaBox2DJointParam param, float value);

/*
 * Distance joints: length range; revolute: angle limits (see
 * athena_box2d_revolute_limits_valid); prismatic and wheel: translation
 * limits. False for other joint types or lower > upper.
 */
bool athena_box2d_joint_set_limits(b2JointId joint, float lower, float upper);

#endif /* ATH_NATIVE_BOX2D_H */
