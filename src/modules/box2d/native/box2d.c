#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <athena/box2d.h>

/* Internal header of the vendored library (b2IsValidSnapshotImage). */
#include "box2d/world_snapshot.h"

#if defined(PS2) || defined(_EE)
#include <kernel.h>
#include <malloc.h>
#include <athena/boot.h>
#include <athena/exceptions.h>
#include <athena/memory.h>
#define ATHENA_BOX2D_EE 1
#endif

/* ------------------------------------------------------------------------ */
/* Module hook                                                               */
/* ------------------------------------------------------------------------ */

static size_t memory_limit;

#ifdef ATHENA_BOX2D_EE
/* Stops on the crash screen: Box2D cannot continue from either failure. */
static void fatal(const char *title, const char *message) {
    printf("[Box2D] %s: %s\n", title, message);
    athena_display_crash_screen(title, message, dark_mode);
    for (;;)
        SleepThread();
}

static void *box2d_alloc(size_t size, int alignment) {
    void *memory = memalign((size_t)alignment, size);
    char message[160];

    if (!memory) {
        snprintf(message, sizeof(message), "Out of memory allocating %u bytes (Box2D holds %u bytes).",
            (unsigned)size, (unsigned)athena_box2d_memory_used());
        fatal("Box2D out of memory", message);
    }
    return memory;
}

static void box2d_free(void *memory, size_t size) {
    (void)size;
    free(memory);
}

static int box2d_assert(const char *condition, const char *file, int line) {
    char message[256];

    snprintf(message, sizeof(message), "Assertion failed: %s\n  at %s:%d", condition, file, line);
    fatal("Box2D assertion", message);
    return 1;
}
#endif

int athena_box2d_module_init(void) {
#ifdef ATHENA_BOX2D_EE
    b2SetAllocator(box2d_alloc, box2d_free);
    b2SetAssertFcn(box2d_assert);
#endif
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Values and memory                                                         */
/* ------------------------------------------------------------------------ */

float athena_box2d_max_coordinate(void) {
    return B2_HUGE;
}

void athena_box2d_set_memory_limit(size_t bytes) {
    memory_limit = bytes;
}

size_t athena_box2d_memory_limit(void) {
    return memory_limit;
}

size_t athena_box2d_memory_used(void) {
    int64_t bytes = b2GetByteCount();
    return bytes > 0 ? (size_t)bytes : 0;
}

bool athena_box2d_memory_available(void) {
    if (memory_limit && athena_box2d_memory_used() >= memory_limit)
        return false;
#ifdef ATHENA_BOX2D_EE
    {
        size_t total = GetMemorySize(), used = get_used_memory();
        if (used >= total || total - used < ATHENA_BOX2D_RESERVE)
            return false;
    }
#endif
    return true;
}

/* ------------------------------------------------------------------------ */
/* Snapshots                                                                 */
/* ------------------------------------------------------------------------ */


#define SNAPSHOT_TRAILER_MAGIC 0x53324241u /* "AB2S" */
#define SNAPSHOT_TRAILER_SIZE 8

/* FNV-1a: integer only, cheap on the EE. */
static uint32_t snapshot_checksum(const uint8_t *data, int size) {
    uint32_t hash = 2166136261u;

    for (int i = 0; i < size; i++)
        hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

static void put_u32(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}


uint8_t *athena_box2d_snapshot(b2WorldId world, int *size) {
    int needed = b2World_Snapshot(world, NULL, 0);
    uint8_t *image;

    if (needed <= 0)
        return NULL;
    image = malloc((size_t)needed + SNAPSHOT_TRAILER_SIZE);
    if (!image)
        return NULL;
    if (b2World_Snapshot(world, image, needed) != needed) {
        free(image);
        return NULL;
    }
    put_u32(image + needed, SNAPSHOT_TRAILER_MAGIC);
    put_u32(image + needed + 4, snapshot_checksum(image, needed));
    *size = needed + SNAPSHOT_TRAILER_SIZE;
    return image;
}

int athena_box2d_restore(b2WorldId world, const uint8_t *image, int size) {
    int payload = size - SNAPSHOT_TRAILER_SIZE;
    uint8_t expected[SNAPSHOT_TRAILER_SIZE];

    if (!image || payload <= 0)
        return ATHENA_BOX2D_RESTORE_REJECTED;
    /*
     * The trailer is compared as bytes. Compared as uint32_t, GCC loads the
     * stored word with lwl/lwr and compares 64-bit registers, assuming both
     * sides are sign-extended; on the EE that failed for checksums with bit
     * 31 set. Byte stores and memcmp never see the upper register bits.
     */
    put_u32(expected, SNAPSHOT_TRAILER_MAGIC);
    put_u32(expected + 4, snapshot_checksum(image, payload));
    if (memcmp(expected, image + payload, 4) != 0)
        return ATHENA_BOX2D_RESTORE_REJECTED;
    if (memcmp(expected + 4, image + payload + 4, 4) != 0)
        return ATHENA_BOX2D_RESTORE_DAMAGED;
    if (!b2IsValidSnapshotImage(image, payload))
        return ATHENA_BOX2D_RESTORE_REJECTED;
    return b2World_Restore(world, image, payload) ? ATHENA_BOX2D_RESTORED : ATHENA_BOX2D_RESTORE_FAILED;
}

const char *athena_box2d_shape_type_name(b2ShapeType type) {
    switch (type) {
        case b2_circleShape: return "circle";
        case b2_capsuleShape: return "capsule";
        case b2_segmentShape: return "segment";
        case b2_polygonShape: return "polygon";
        case b2_chainSegmentShape: return "chainSegment";
        default: return "unknown";
    }
}

const char *athena_box2d_joint_type_name(b2JointType type) {
    switch (type) {
        case b2_distanceJoint: return "distance";
        case b2_filterJoint: return "filter";
        case b2_motorJoint: return "motor";
        case b2_prismaticJoint: return "prismatic";
        case b2_revoluteJoint: return "revolute";
        case b2_weldJoint: return "weld";
        case b2_wheelJoint: return "wheel";
        default: return "unknown";
    }
}

/* ------------------------------------------------------------------------ */
/* Geometry                                                                  */
/* ------------------------------------------------------------------------ */

float athena_box2d_linear_slop(void) {
    return B2_LINEAR_SLOP;
}

b2Rot athena_box2d_make_rot(float radians) {
    return (b2Rot){ cosf(radians), sinf(radians) };
}

static bool point_finite(b2Vec2 p) {
    return athena_box2d_valid_float(p.x) && athena_box2d_valid_float(p.y);
}

/* Farther apart than the distance at which Box2D welds points. */
static bool points_apart(b2Vec2 a, b2Vec2 b) {
    return b2DistanceSquared(a, b) > B2_LINEAR_SLOP * B2_LINEAR_SLOP;
}

bool athena_box2d_make_box(float hw, float hh, b2Vec2 center, float angle, b2Polygon *out) {
    /* The polygon mass asserts area > FLT_EPSILON; the area is 4 * hw * hh. */
    if (!(athena_box2d_valid_float(hw) && athena_box2d_valid_float(hh) && hw > 0.0f && hh > 0.0f) || !(hw * hh > FLT_EPSILON))
        return false;
    if (!point_finite(center) || !athena_box2d_valid_float(angle))
        return false;
    *out = b2MakeOffsetBox(hw, hh, center, athena_box2d_make_rot(angle));
    return true;
}

bool athena_box2d_make_polygon(const b2Vec2 *points, int count, float radius, b2Polygon *out) {
    b2Hull hull;

    if (count < 3 || count > B2_MAX_POLYGON_VERTICES || !athena_box2d_valid_float(radius) || radius < 0.0f)
        return false;
    for (int i = 0; i < count; i++) {
        if (!point_finite(points[i]))
            return false;
    }
    /* Welds close points and drops collinear ones; count 0 when degenerate. */
    hull = b2ComputeHull(points, count);
    if (hull.count < 3 || !b2ValidateHull(&hull))
        return false;
    *out = b2MakePolygon(&hull, radius);
    return true;
}

bool athena_box2d_segment_valid(b2Vec2 p1, b2Vec2 p2) {
    return point_finite(p1) && point_finite(p2) && points_apart(p1, p2);
}

bool athena_box2d_chain_valid(const b2Vec2 *points, int count, bool loop) {
    if (count < 4)
        return false;
    for (int i = 0; i < count; i++) {
        if (!point_finite(points[i]))
            return false;
        if (i > 0 && !points_apart(points[i - 1], points[i]))
            return false;
    }
    return !loop || points_apart(points[count - 1], points[0]);
}

/* ------------------------------------------------------------------------ */
/* Joints                                                                    */
/* ------------------------------------------------------------------------ */

void athena_box2d_joint_frames(b2JointDef *base, const b2Vec2 *anchor, b2Rot axis) {
    b2Transform a = b2Body_GetTransform(base->bodyIdA);
    b2Transform b = b2Body_GetTransform(base->bodyIdB);

    if (anchor) {
        b2Transform frame = { *anchor, axis };
        base->localFrameA = b2InvMulTransforms(a, frame);
        base->localFrameB = b2InvMulTransforms(b, frame);
    } else {
        base->localFrameA = (b2Transform){ b2Vec2_zero, b2InvMulRot(a.q, axis) };
        base->localFrameB = (b2Transform){ b2Vec2_zero, b2InvMulRot(b.q, axis) };
    }
}

bool athena_box2d_revolute_limits_valid(float lower, float upper) {
    return athena_box2d_valid_float(lower) && athena_box2d_valid_float(upper) && lower <= upper &&
        lower >= -0.99f * B2_PI && upper <= 0.99f * B2_PI;
}

bool athena_box2d_joint_get(b2JointId joint, AthenaBox2DJointParam param, float *out) {
    b2JointType type = b2Joint_GetType(joint);
    float value;

    switch (param) {
    case ATHENA_BOX2D_SPRING_ENABLED:
        switch (type) {
            case b2_distanceJoint: value = b2DistanceJoint_IsSpringEnabled(joint); break;
            case b2_revoluteJoint: value = b2RevoluteJoint_IsSpringEnabled(joint); break;
            case b2_prismaticJoint: value = b2PrismaticJoint_IsSpringEnabled(joint); break;
            case b2_wheelJoint: value = b2WheelJoint_IsSpringEnabled(joint); break;
            default: return false;
        }
        break;
    case ATHENA_BOX2D_SPRING_HERTZ:
        switch (type) {
            case b2_distanceJoint: value = b2DistanceJoint_GetSpringHertz(joint); break;
            case b2_revoluteJoint: value = b2RevoluteJoint_GetSpringHertz(joint); break;
            case b2_prismaticJoint: value = b2PrismaticJoint_GetSpringHertz(joint); break;
            case b2_wheelJoint: value = b2WheelJoint_GetSpringHertz(joint); break;
            default: return false;
        }
        break;
    case ATHENA_BOX2D_SPRING_DAMPING_RATIO:
        switch (type) {
            case b2_distanceJoint: value = b2DistanceJoint_GetSpringDampingRatio(joint); break;
            case b2_revoluteJoint: value = b2RevoluteJoint_GetSpringDampingRatio(joint); break;
            case b2_prismaticJoint: value = b2PrismaticJoint_GetSpringDampingRatio(joint); break;
            case b2_wheelJoint: value = b2WheelJoint_GetSpringDampingRatio(joint); break;
            default: return false;
        }
        break;
    case ATHENA_BOX2D_LIMIT_ENABLED:
        switch (type) {
            case b2_distanceJoint: value = b2DistanceJoint_IsLimitEnabled(joint); break;
            case b2_revoluteJoint: value = b2RevoluteJoint_IsLimitEnabled(joint); break;
            case b2_prismaticJoint: value = b2PrismaticJoint_IsLimitEnabled(joint); break;
            case b2_wheelJoint: value = b2WheelJoint_IsLimitEnabled(joint); break;
            default: return false;
        }
        break;
    case ATHENA_BOX2D_LOWER_LIMIT:
        switch (type) {
            case b2_distanceJoint: value = b2DistanceJoint_GetMinLength(joint); break;
            case b2_revoluteJoint: value = b2RevoluteJoint_GetLowerLimit(joint); break;
            case b2_prismaticJoint: value = b2PrismaticJoint_GetLowerLimit(joint); break;
            case b2_wheelJoint: value = b2WheelJoint_GetLowerLimit(joint); break;
            default: return false;
        }
        break;
    case ATHENA_BOX2D_UPPER_LIMIT:
        switch (type) {
            case b2_distanceJoint: value = b2DistanceJoint_GetMaxLength(joint); break;
            case b2_revoluteJoint: value = b2RevoluteJoint_GetUpperLimit(joint); break;
            case b2_prismaticJoint: value = b2PrismaticJoint_GetUpperLimit(joint); break;
            case b2_wheelJoint: value = b2WheelJoint_GetUpperLimit(joint); break;
            default: return false;
        }
        break;
    case ATHENA_BOX2D_MOTOR_ENABLED:
        switch (type) {
            case b2_distanceJoint: value = b2DistanceJoint_IsMotorEnabled(joint); break;
            case b2_revoluteJoint: value = b2RevoluteJoint_IsMotorEnabled(joint); break;
            case b2_prismaticJoint: value = b2PrismaticJoint_IsMotorEnabled(joint); break;
            case b2_wheelJoint: value = b2WheelJoint_IsMotorEnabled(joint); break;
            default: return false;
        }
        break;
    case ATHENA_BOX2D_MOTOR_SPEED:
        switch (type) {
            case b2_distanceJoint: value = b2DistanceJoint_GetMotorSpeed(joint); break;
            case b2_revoluteJoint: value = b2RevoluteJoint_GetMotorSpeed(joint); break;
            case b2_prismaticJoint: value = b2PrismaticJoint_GetMotorSpeed(joint); break;
            case b2_wheelJoint: value = b2WheelJoint_GetMotorSpeed(joint); break;
            default: return false;
        }
        break;
    case ATHENA_BOX2D_MAX_MOTOR_FORCE:
        switch (type) {
            case b2_distanceJoint: value = b2DistanceJoint_GetMaxMotorForce(joint); break;
            case b2_prismaticJoint: value = b2PrismaticJoint_GetMaxMotorForce(joint); break;
            default: return false;
        }
        break;
    case ATHENA_BOX2D_MOTOR_FORCE:
        switch (type) {
            case b2_distanceJoint: value = b2DistanceJoint_GetMotorForce(joint); break;
            case b2_prismaticJoint: value = b2PrismaticJoint_GetMotorForce(joint); break;
            default: return false;
        }
        break;
    case ATHENA_BOX2D_MAX_MOTOR_TORQUE:
        switch (type) {
            case b2_revoluteJoint: value = b2RevoluteJoint_GetMaxMotorTorque(joint); break;
            case b2_wheelJoint: value = b2WheelJoint_GetMaxMotorTorque(joint); break;
            default: return false;
        }
        break;
    case ATHENA_BOX2D_MOTOR_TORQUE:
        switch (type) {
            case b2_revoluteJoint: value = b2RevoluteJoint_GetMotorTorque(joint); break;
            case b2_wheelJoint: value = b2WheelJoint_GetMotorTorque(joint); break;
            default: return false;
        }
        break;
    case ATHENA_BOX2D_LINEAR_HERTZ:
        switch (type) {
            case b2_weldJoint: value = b2WeldJoint_GetLinearHertz(joint); break;
            case b2_motorJoint: value = b2MotorJoint_GetLinearHertz(joint); break;
            default: return false;
        }
        break;
    case ATHENA_BOX2D_LINEAR_DAMPING_RATIO:
        switch (type) {
            case b2_weldJoint: value = b2WeldJoint_GetLinearDampingRatio(joint); break;
            case b2_motorJoint: value = b2MotorJoint_GetLinearDampingRatio(joint); break;
            default: return false;
        }
        break;
    case ATHENA_BOX2D_ANGULAR_HERTZ:
        switch (type) {
            case b2_weldJoint: value = b2WeldJoint_GetAngularHertz(joint); break;
            case b2_motorJoint: value = b2MotorJoint_GetAngularHertz(joint); break;
            default: return false;
        }
        break;
    case ATHENA_BOX2D_ANGULAR_DAMPING_RATIO:
        switch (type) {
            case b2_weldJoint: value = b2WeldJoint_GetAngularDampingRatio(joint); break;
            case b2_motorJoint: value = b2MotorJoint_GetAngularDampingRatio(joint); break;
            default: return false;
        }
        break;
    case ATHENA_BOX2D_ANGULAR_VELOCITY:
        if (type != b2_motorJoint) return false;
        value = b2MotorJoint_GetAngularVelocity(joint);
        break;
    case ATHENA_BOX2D_MAX_VELOCITY_FORCE:
        if (type != b2_motorJoint) return false;
        value = b2MotorJoint_GetMaxVelocityForce(joint);
        break;
    case ATHENA_BOX2D_MAX_VELOCITY_TORQUE:
        if (type != b2_motorJoint) return false;
        value = b2MotorJoint_GetMaxVelocityTorque(joint);
        break;
    case ATHENA_BOX2D_MAX_SPRING_FORCE:
        if (type != b2_motorJoint) return false;
        value = b2MotorJoint_GetMaxSpringForce(joint);
        break;
    case ATHENA_BOX2D_MAX_SPRING_TORQUE:
        if (type != b2_motorJoint) return false;
        value = b2MotorJoint_GetMaxSpringTorque(joint);
        break;
    case ATHENA_BOX2D_LENGTH:
        if (type != b2_distanceJoint) return false;
        value = b2DistanceJoint_GetLength(joint);
        break;
    case ATHENA_BOX2D_CURRENT_LENGTH:
        if (type != b2_distanceJoint) return false;
        value = b2DistanceJoint_GetCurrentLength(joint);
        break;
    case ATHENA_BOX2D_ANGLE:
        if (type != b2_revoluteJoint) return false;
        value = b2RevoluteJoint_GetAngle(joint);
        break;
    case ATHENA_BOX2D_TRANSLATION:
        if (type != b2_prismaticJoint) return false;
        value = b2PrismaticJoint_GetTranslation(joint);
        break;
    case ATHENA_BOX2D_SPEED:
        if (type != b2_prismaticJoint) return false;
        value = b2PrismaticJoint_GetSpeed(joint);
        break;
    case ATHENA_BOX2D_TARGET_ANGLE:
        if (type != b2_revoluteJoint) return false;
        value = b2RevoluteJoint_GetTargetAngle(joint);
        break;
    case ATHENA_BOX2D_TARGET_TRANSLATION:
        if (type != b2_prismaticJoint) return false;
        value = b2PrismaticJoint_GetTargetTranslation(joint);
        break;
    default:
        return false;
    }

    *out = value;
    return true;
}

bool athena_box2d_joint_set(b2JointId joint, AthenaBox2DJointParam param, float value) {
    b2JointType type = b2Joint_GetType(joint);
    bool flag = value != 0.0f;

    switch (param) {
    case ATHENA_BOX2D_SPRING_ENABLED:
        switch (type) {
            case b2_distanceJoint: b2DistanceJoint_EnableSpring(joint, flag); return true;
            case b2_revoluteJoint: b2RevoluteJoint_EnableSpring(joint, flag); return true;
            case b2_prismaticJoint: b2PrismaticJoint_EnableSpring(joint, flag); return true;
            case b2_wheelJoint: b2WheelJoint_EnableSpring(joint, flag); return true;
            default: return false;
        }
    case ATHENA_BOX2D_SPRING_HERTZ:
        switch (type) {
            case b2_distanceJoint: b2DistanceJoint_SetSpringHertz(joint, value); return true;
            case b2_revoluteJoint: b2RevoluteJoint_SetSpringHertz(joint, value); return true;
            case b2_prismaticJoint: b2PrismaticJoint_SetSpringHertz(joint, value); return true;
            case b2_wheelJoint: b2WheelJoint_SetSpringHertz(joint, value); return true;
            default: return false;
        }
    case ATHENA_BOX2D_SPRING_DAMPING_RATIO:
        switch (type) {
            case b2_distanceJoint: b2DistanceJoint_SetSpringDampingRatio(joint, value); return true;
            case b2_revoluteJoint: b2RevoluteJoint_SetSpringDampingRatio(joint, value); return true;
            case b2_prismaticJoint: b2PrismaticJoint_SetSpringDampingRatio(joint, value); return true;
            case b2_wheelJoint: b2WheelJoint_SetSpringDampingRatio(joint, value); return true;
            default: return false;
        }
    case ATHENA_BOX2D_LIMIT_ENABLED:
        switch (type) {
            case b2_distanceJoint: b2DistanceJoint_EnableLimit(joint, flag); return true;
            case b2_revoluteJoint: b2RevoluteJoint_EnableLimit(joint, flag); return true;
            case b2_prismaticJoint: b2PrismaticJoint_EnableLimit(joint, flag); return true;
            case b2_wheelJoint: b2WheelJoint_EnableLimit(joint, flag); return true;
            default: return false;
        }
    case ATHENA_BOX2D_MOTOR_ENABLED:
        switch (type) {
            case b2_distanceJoint: b2DistanceJoint_EnableMotor(joint, flag); return true;
            case b2_revoluteJoint: b2RevoluteJoint_EnableMotor(joint, flag); return true;
            case b2_prismaticJoint: b2PrismaticJoint_EnableMotor(joint, flag); return true;
            case b2_wheelJoint: b2WheelJoint_EnableMotor(joint, flag); return true;
            default: return false;
        }
    case ATHENA_BOX2D_MOTOR_SPEED:
        switch (type) {
            case b2_distanceJoint: b2DistanceJoint_SetMotorSpeed(joint, value); return true;
            case b2_revoluteJoint: b2RevoluteJoint_SetMotorSpeed(joint, value); return true;
            case b2_prismaticJoint: b2PrismaticJoint_SetMotorSpeed(joint, value); return true;
            case b2_wheelJoint: b2WheelJoint_SetMotorSpeed(joint, value); return true;
            default: return false;
        }
    case ATHENA_BOX2D_MAX_MOTOR_FORCE:
        switch (type) {
            case b2_distanceJoint: b2DistanceJoint_SetMaxMotorForce(joint, value); return true;
            case b2_prismaticJoint: b2PrismaticJoint_SetMaxMotorForce(joint, value); return true;
            default: return false;
        }
    case ATHENA_BOX2D_MAX_MOTOR_TORQUE:
        switch (type) {
            case b2_revoluteJoint: b2RevoluteJoint_SetMaxMotorTorque(joint, value); return true;
            case b2_wheelJoint: b2WheelJoint_SetMaxMotorTorque(joint, value); return true;
            default: return false;
        }
    case ATHENA_BOX2D_LINEAR_HERTZ:
        switch (type) {
            case b2_weldJoint: b2WeldJoint_SetLinearHertz(joint, value); return true;
            case b2_motorJoint: b2MotorJoint_SetLinearHertz(joint, value); return true;
            default: return false;
        }
    case ATHENA_BOX2D_LINEAR_DAMPING_RATIO:
        switch (type) {
            case b2_weldJoint: b2WeldJoint_SetLinearDampingRatio(joint, value); return true;
            case b2_motorJoint: b2MotorJoint_SetLinearDampingRatio(joint, value); return true;
            default: return false;
        }
    case ATHENA_BOX2D_ANGULAR_HERTZ:
        switch (type) {
            case b2_weldJoint: b2WeldJoint_SetAngularHertz(joint, value); return true;
            case b2_motorJoint: b2MotorJoint_SetAngularHertz(joint, value); return true;
            default: return false;
        }
    case ATHENA_BOX2D_ANGULAR_DAMPING_RATIO:
        switch (type) {
            case b2_weldJoint: b2WeldJoint_SetAngularDampingRatio(joint, value); return true;
            case b2_motorJoint: b2MotorJoint_SetAngularDampingRatio(joint, value); return true;
            default: return false;
        }
    case ATHENA_BOX2D_ANGULAR_VELOCITY:
        if (type != b2_motorJoint) return false;
        b2MotorJoint_SetAngularVelocity(joint, value);
        return true;
    case ATHENA_BOX2D_MAX_VELOCITY_FORCE:
        if (type != b2_motorJoint) return false;
        b2MotorJoint_SetMaxVelocityForce(joint, value);
        return true;
    case ATHENA_BOX2D_MAX_VELOCITY_TORQUE:
        if (type != b2_motorJoint) return false;
        b2MotorJoint_SetMaxVelocityTorque(joint, value);
        return true;
    case ATHENA_BOX2D_MAX_SPRING_FORCE:
        if (type != b2_motorJoint) return false;
        b2MotorJoint_SetMaxSpringForce(joint, value);
        return true;
    case ATHENA_BOX2D_MAX_SPRING_TORQUE:
        if (type != b2_motorJoint) return false;
        b2MotorJoint_SetMaxSpringTorque(joint, value);
        return true;
    case ATHENA_BOX2D_LENGTH:
        if (type != b2_distanceJoint) return false;
        b2DistanceJoint_SetLength(joint, value);
        return true;
    case ATHENA_BOX2D_TARGET_ANGLE:
        if (type != b2_revoluteJoint) return false;
        b2RevoluteJoint_SetTargetAngle(joint, value);
        return true;
    case ATHENA_BOX2D_TARGET_TRANSLATION:
        if (type != b2_prismaticJoint) return false;
        b2PrismaticJoint_SetTargetTranslation(joint, value);
        return true;
    default:
        /* Read-only: limits (see athena_box2d_joint_set_limits), forces, angle, ... */
        return false;
    }
}

bool athena_box2d_joint_set_limits(b2JointId joint, float lower, float upper) {
    if (!(athena_box2d_valid_float(lower) && athena_box2d_valid_float(upper) && lower <= upper))
        return false;

    switch (b2Joint_GetType(joint)) {
    case b2_distanceJoint:
        b2DistanceJoint_SetLengthRange(joint, lower, upper);
        return true;
    case b2_revoluteJoint:
        if (!athena_box2d_revolute_limits_valid(lower, upper))
            return false;
        b2RevoluteJoint_SetLimits(joint, lower, upper);
        return true;
    case b2_prismaticJoint:
        b2PrismaticJoint_SetLimits(joint, lower, upper);
        return true;
    case b2_wheelJoint:
        b2WheelJoint_SetLimits(joint, lower, upper);
        return true;
    default:
        return false;
    }
}
