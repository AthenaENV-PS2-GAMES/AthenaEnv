#include <athena/float_bits.h>
#include <math.h>
#include <athena/quaternion.h>

void athena_quaternion_identity(AthenaQuaternion *out) {
    *out = (AthenaQuaternion){0, 0, 0, 1};
}

int athena_quaternion_normalize(AthenaQuaternion *out, const AthenaQuaternion *in) {
    if(!athena_float_isfinite(in->x)||!athena_float_isfinite(in->y)||
        !athena_float_isfinite(in->z)||!athena_float_isfinite(in->w)) return 0;
    double n = sqrt((double)in->x*in->x + (double)in->y*in->y +
        (double)in->z*in->z + (double)in->w*in->w);
    if (!athena_double_isfinite(n) || n <= 1e-20) return 0;
    *out = (AthenaQuaternion){in->x/n, in->y/n, in->z/n, in->w/n};
    return 1;
}

int athena_quaternion_axis_angle(AthenaQuaternion *out, float x, float y, float z, float radians) {
    if(!athena_float_isfinite(x)||!athena_float_isfinite(y)||!athena_float_isfinite(z)||
        !athena_float_isfinite(radians)) return 0;
    double n = sqrt((double)x*x + (double)y*y + (double)z*z);
    if (!athena_double_isfinite(n) || n <= 1e-20) return 0;
    double s = sin((double)radians/2)/n;
    *out = (AthenaQuaternion){x*s, y*s, z*s, cos((double)radians/2)};
    return 1;
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
    double dot = (double)p.x*q.x + (double)p.y*q.y + (double)p.z*q.z + (double)p.w*q.w;
    if (dot < 0) { q.x=-q.x; q.y=-q.y; q.z=-q.z; q.w=-q.w; dot=-dot; }
    if (dot > 1) dot=1;
    double s=1-t, u=t;
    if (dot < 0.9995) {
        double angle=acos(dot), denominator=sin(angle);
        s=sin((1-t)*angle)/denominator; u=sin(t*angle)/denominator;
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
