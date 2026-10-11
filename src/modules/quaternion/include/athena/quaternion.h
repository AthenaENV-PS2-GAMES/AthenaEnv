#ifndef ATHENA_QUATERNION_H
#define ATHENA_QUATERNION_H

#include <athena/matrix4.h>

/* xyzw, right-handed rotations in radians. Inputs may alias outputs.
 * Invalid/non-finite inputs return 0 and leave the output unchanged. */
typedef struct __attribute__((aligned(16))) { float x, y, z, w; } AthenaQuaternion;
void athena_quaternion_identity(AthenaQuaternion *out);
int athena_quaternion_normalize(AthenaQuaternion *out, const AthenaQuaternion *in);
int athena_quaternion_axis_angle(AthenaQuaternion *out, float x, float y, float z, float radians);
/* Euler radians composed as Rz * Ry * Rx (x applied first). */
int athena_quaternion_euler(AthenaQuaternion *out, float x, float y, float z);
int athena_quaternion_multiply(AthenaQuaternion *out, const AthenaQuaternion *a, const AthenaQuaternion *b);
int athena_quaternion_slerp(AthenaQuaternion *out, const AthenaQuaternion *a, const AthenaQuaternion *b, float t);
int athena_quaternion_trs(AthenaMatrix4 *out, const AthenaVector4 *position,
    const AthenaQuaternion *rotation, const AthenaVector4 *scale);

#endif
