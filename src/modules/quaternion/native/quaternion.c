#include <athena/float_bits.h>
#include <math.h>
#include <athena/quaternion.h>

void athena_quaternion_identity(AthenaQuaternion *out) {
    *out = (AthenaQuaternion){0, 0, 0, 1};
}

/* The R5900 FPU is single precision; double is emulated in software at
 * ~1.4 us per operation. Scaling by the largest component keeps the float
 * sum of squares in [1,4], so large or tiny inputs neither overflow nor
 * underflow. Lengths at or below 1e-20 are rejected as before. */
static float scaled_length(float x,float y,float z,float w,float *largest) {
    float m=fabsf(x);
    if(fabsf(y)>m) m=fabsf(y);
    if(fabsf(z)>m) m=fabsf(z);
    if(fabsf(w)>m) m=fabsf(w);
    *largest=m;
    if(m<=1e-20f) return 0;
    x/=m; y/=m; z/=m; w/=m;
    return sqrtf(x*x+y*y+z*z+w*w);
}

int athena_quaternion_normalize(AthenaQuaternion *out, const AthenaQuaternion *in) {
    if(!athena_float_isfinite(in->x)||!athena_float_isfinite(in->y)||
        !athena_float_isfinite(in->z)||!athena_float_isfinite(in->w)) return 0;
    float m, n = scaled_length(in->x, in->y, in->z, in->w, &m);
    if (n == 0) return 0;
    float inverse = 1/n;
    *out = (AthenaQuaternion){in->x/m*inverse, in->y/m*inverse, in->z/m*inverse, in->w/m*inverse};
    return 1;
}

int athena_quaternion_axis_angle(AthenaQuaternion *out, float x, float y, float z, float radians) {
    if(!athena_float_isfinite(x)||!athena_float_isfinite(y)||!athena_float_isfinite(z)||
        !athena_float_isfinite(radians)) return 0;
    float m, n = scaled_length(x, y, z, 0, &m);
    if (n == 0) return 0;
    float s = sinf(radians/2)/n;
    *out = (AthenaQuaternion){x/m*s, y/m*s, z/m*s, cosf(radians/2)};
    return 1;
}

/* Closed form of Rz * Ry * Rx: six sinf/cosf and one normalization, instead
 * of three axis-angle quaternions and two multiplications. */
int athena_quaternion_euler(AthenaQuaternion *out, float x, float y, float z) {
    if(!athena_float_isfinite(x)||!athena_float_isfinite(y)||!athena_float_isfinite(z)) return 0;
    float sx=sinf(x/2), cx=cosf(x/2), sy=sinf(y/2), cy=cosf(y/2), sz=sinf(z/2), cz=cosf(z/2);
    AthenaQuaternion q={cz*cy*sx - sz*sy*cx, cz*sy*cx + sz*cy*sx,
        sz*cy*cx - cz*sy*sx, cz*cy*cx + sz*sy*sx};
    return athena_quaternion_normalize(out, &q);
}

int athena_quaternion_multiply(AthenaQuaternion *out, const AthenaQuaternion *a, const AthenaQuaternion *b) {
    AthenaQuaternion p, q, r;
    if (!athena_quaternion_normalize(&p, a) || !athena_quaternion_normalize(&q, b)) return 0;
    r = (AthenaQuaternion){
        p.w*q.x + p.x*q.w + p.y*q.z - p.z*q.y,
        p.w*q.y - p.x*q.z + p.y*q.w + p.z*q.x,
        p.w*q.z + p.x*q.y - p.y*q.x + p.z*q.w,
        p.w*q.w - p.x*q.x - p.y*q.y - p.z*q.z};
    return athena_quaternion_normalize(out, &r);
}

int athena_quaternion_slerp(AthenaQuaternion *out, const AthenaQuaternion *a, const AthenaQuaternion *b, float t) {
    AthenaQuaternion p, q, r;
    if (!athena_float_isfinite(t) || t < 0 || t > 1 || !athena_quaternion_normalize(&p, a) ||
        !athena_quaternion_normalize(&q, b)) return 0;
    /* Float: unit operands keep dot in [-1,1]; the nlerp branch avoids
     * dividing by a tiny sinf(angle). */
    float dot = p.x*q.x + p.y*q.y + p.z*q.z + p.w*q.w;
    if (dot < 0) { q.x=-q.x; q.y=-q.y; q.z=-q.z; q.w=-q.w; dot=-dot; }
    if (dot > 1) dot=1;
    float s=1-t, u=t;
    if (dot < 0.9995f) {
        float angle=acosf(dot), denominator=sinf(angle);
        s=sinf((1-t)*angle)/denominator; u=sinf(t*angle)/denominator;
    }
    r=(AthenaQuaternion){p.x*s+q.x*u, p.y*s+q.y*u, p.z*s+q.z*u, p.w*s+q.w*u};
    return athena_quaternion_normalize(out, &r);
}

int athena_quaternion_trs(AthenaMatrix4 *out, const AthenaVector4 *p,
    const AthenaQuaternion *rotation, const AthenaVector4 *s) {
    AthenaQuaternion q;
    if (!athena_float_isfinite(p->x) || !athena_float_isfinite(p->y) || !athena_float_isfinite(p->z) ||
        !athena_float_isfinite(s->x) || !athena_float_isfinite(s->y) || !athena_float_isfinite(s->z) ||
        !athena_quaternion_normalize(&q, rotation)) return 0;
    float x=q.x, y=q.y, z=q.z, w=q.w;
    AthenaMatrix4 m = {{
        (1-2*(y*y+z*z))*s->x, 2*(x*y+z*w)*s->x, 2*(x*z-y*w)*s->x, 0,
        2*(x*y-z*w)*s->y, (1-2*(x*x+z*z))*s->y, 2*(y*z+x*w)*s->y, 0,
        2*(x*z+y*w)*s->z, 2*(y*z-x*w)*s->z, (1-2*(x*x+y*y))*s->z, 0,
        p->x, p->y, p->z, 1}};
    for (int i=0; i<16; ++i) if (!athena_float_isfinite(m.value[i])) return 0;
    *out=m; return 1;
}
