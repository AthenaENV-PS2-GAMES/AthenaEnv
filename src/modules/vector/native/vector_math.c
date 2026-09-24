#include <math.h>
#include <string.h>

#include <athena/vector.h>

void ath_vector_add(AthenaVector4 *out, const AthenaVector4 *a, const AthenaVector4 *b) {
    __asm__ __volatile__(
        "lqc2 $vf4, 0(%1)\n"
        "lqc2 $vf5, 0(%2)\n"
        "vadd.xyzw $vf6, $vf4, $vf5\n"
        "sqc2 $vf6, 0(%0)\n"
        : : "r"(out), "r"(a), "r"(b) : "memory");
}

void ath_vector_sub(AthenaVector4 *out, const AthenaVector4 *a, const AthenaVector4 *b) {
    __asm__ __volatile__(
        "lqc2 $vf4, 0(%1)\n"
        "lqc2 $vf5, 0(%2)\n"
        "vsub.xyzw $vf6, $vf4, $vf5\n"
        "sqc2 $vf6, 0(%0)\n"
        : : "r"(out), "r"(a), "r"(b) : "memory");
}

void ath_vector_mul(AthenaVector4 *out, const AthenaVector4 *a, const AthenaVector4 *b) {
    __asm__ __volatile__(
        "lqc2 $vf4, 0(%1)\n"
        "lqc2 $vf5, 0(%2)\n"
        "vmul.xyzw $vf6, $vf4, $vf5\n"
        "sqc2 $vf6, 0(%0)\n"
        : : "r"(out), "r"(a), "r"(b) : "memory");
}

void ath_vector_div(AthenaVector4 *out, const AthenaVector4 *a, const AthenaVector4 *b) {
    out->x = a->x / b->x;
    out->y = a->y / b->y;
    out->z = a->z / b->z;
    out->w = a->w / b->w;
}

float ath_vector_dot3(const AthenaVector4 *a, const AthenaVector4 *b) {
    float result;
    __asm__ __volatile__(
        "lqc2 $vf4, 0(%1)\n"
        "lqc2 $vf5, 0(%2)\n"
        "vmul.xyz $vf5, $vf4, $vf5\n"
        "vaddy.x $vf5, $vf5, $vf5\n"
        "vaddz.x $vf5, $vf5, $vf5\n"
        "qmfc2 $2, $vf5\n"
        "mtc1 $2, %0\n"
        : "=f"(result) : "r"(a), "r"(b) : "$2", "memory");
    return result;
}

void ath_vector_cross(AthenaVector4 *out, const AthenaVector4 *a, const AthenaVector4 *b) {
    __asm__ __volatile__(
        "lqc2 $vf4, 0(%1)\n"
        "lqc2 $vf5, 0(%2)\n"
        "vopmula.xyz $ACC, $vf4, $vf5\n"
        "vopmsub.xyz $vf6, $vf5, $vf4\n"
        "vsub.w $vf6, $vf6, $vf6\n"
        "sqc2 $vf6, 0(%0)\n"
        : : "r"(out), "r"(a), "r"(b) : "memory");
}

float ath_vector_length3(const AthenaVector4 *v) {
    return sqrtf(ath_vector_dot3(v, v));
}

float ath_vector_length4(const AthenaVector4 *v) {
    return sqrtf(v->x * v->x + v->y * v->y + v->z * v->z + v->w * v->w);
}

void ath_vector_normalize3(AthenaVector4 *out, const AthenaVector4 *v) {
    float length = ath_vector_length3(v);
    if (length == 0.0f) {
        memset(out, 0, sizeof(*out));
        return;
    }
    out->x = v->x / length;
    out->y = v->y / length;
    out->z = v->z / length;
    out->w = v->w;
}
