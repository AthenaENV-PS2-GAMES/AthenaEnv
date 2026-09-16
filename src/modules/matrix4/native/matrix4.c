#include <math.h>
#include <string.h>

#include "matrix4.h"

void ath_matrix4_identity(AthenaMatrix4 *matrix) {
    memset(matrix, 0, sizeof(*matrix));
    matrix->value[0] = 1.0f;
    matrix->value[5] = 1.0f;
    matrix->value[10] = 1.0f;
    matrix->value[15] = 1.0f;
}

void ath_matrix4_copy(AthenaMatrix4 *out, const AthenaMatrix4 *in) {
    memcpy(out, in, sizeof(*out));
}

void ath_matrix4_multiply(AthenaMatrix4 *out, const AthenaMatrix4 *a,
    const AthenaMatrix4 *b) {
    AthenaMatrix4 result;
    __asm__ __volatile__(
        "lqc2 $vf1, 0x00(%1)\n"
        "lqc2 $vf2, 0x10(%1)\n"
        "lqc2 $vf3, 0x20(%1)\n"
        "lqc2 $vf4, 0x30(%1)\n"
        "lqc2 $vf5, 0x00(%2)\n"
        "lqc2 $vf6, 0x10(%2)\n"
        "lqc2 $vf7, 0x20(%2)\n"
        "lqc2 $vf8, 0x30(%2)\n"
        "vmulax.xyzw $ACC, $vf5, $vf1\n"
        "vmadday.xyzw $ACC, $vf6, $vf1\n"
        "vmaddaz.xyzw $ACC, $vf7, $vf1\n"
        "vmaddw.xyzw $vf1, $vf8, $vf1\n"
        "vmulax.xyzw $ACC, $vf5, $vf2\n"
        "vmadday.xyzw $ACC, $vf6, $vf2\n"
        "vmaddaz.xyzw $ACC, $vf7, $vf2\n"
        "vmaddw.xyzw $vf2, $vf8, $vf2\n"
        "vmulax.xyzw $ACC, $vf5, $vf3\n"
        "vmadday.xyzw $ACC, $vf6, $vf3\n"
        "vmaddaz.xyzw $ACC, $vf7, $vf3\n"
        "vmaddw.xyzw $vf3, $vf8, $vf3\n"
        "vmulax.xyzw $ACC, $vf5, $vf4\n"
        "vmadday.xyzw $ACC, $vf6, $vf4\n"
        "vmaddaz.xyzw $ACC, $vf7, $vf4\n"
        "vmaddw.xyzw $vf4, $vf8, $vf4\n"
        "sqc2 $vf1, 0x00(%0)\n"
        "sqc2 $vf2, 0x10(%0)\n"
        "sqc2 $vf3, 0x20(%0)\n"
        "sqc2 $vf4, 0x30(%0)\n"
        : : "r"(&result), "r"(a), "r"(b) : "memory");
    *out = result;
}

void ath_matrix4_apply(AthenaVector4 *out, const AthenaMatrix4 *matrix,
    const AthenaVector4 *vector) {
    __asm__ __volatile__(
        "lqc2 $vf4, 0x00(%1)\n"
        "lqc2 $vf5, 0x10(%1)\n"
        "lqc2 $vf6, 0x20(%1)\n"
        "lqc2 $vf7, 0x30(%1)\n"
        "lqc2 $vf8, 0x00(%2)\n"
        "vmulax.xyzw $ACC, $vf4, $vf8\n"
        "vmadday.xyzw $ACC, $vf5, $vf8\n"
        "vmaddaz.xyzw $ACC, $vf6, $vf8\n"
        "vmaddw.xyzw $vf9, $vf7, $vf8\n"
        "sqc2 $vf9, 0x00(%0)\n"
        : : "r"(out), "r"(matrix), "r"(vector) : "memory");
}

void ath_matrix4_transpose(AthenaMatrix4 *out, const AthenaMatrix4 *in) {
    AthenaMatrix4 result;
    for (int row = 0; row < 4; row++)
        for (int column = 0; column < 4; column++)
            result.value[row * 4 + column] = in->value[column * 4 + row];
    *out = result;
}

int ath_matrix4_inverse(AthenaMatrix4 *out, const AthenaMatrix4 *in) {
    AthenaMatrix4 result;
    float augmented[4][8];
    int row;
    int column;

    for (row = 0; row < 4; row++) {
        for (column = 0; column < 4; column++)
            augmented[row][column] = in->value[column * 4 + row];
        for (column = 0; column < 4; column++)
            augmented[row][column + 4] = row == column ? 1.0f : 0.0f;
    }

    for (column = 0; column < 4; column++) {
        int pivot = column;
        float pivot_abs = fabsf(augmented[pivot][column]);
        for (row = column + 1; row < 4; row++) {
            float candidate = fabsf(augmented[row][column]);
            if (candidate > pivot_abs) {
                pivot = row;
                pivot_abs = candidate;
            }
        }
        if (pivot_abs < 1.0e-7f) return 0;
        if (pivot != column) {
            for (int i = 0; i < 8; i++) {
                float temp = augmented[column][i];
                augmented[column][i] = augmented[pivot][i];
                augmented[pivot][i] = temp;
            }
        }
        float divisor = augmented[column][column];
        for (int i = 0; i < 8; i++)
            augmented[column][i] /= divisor;
        for (row = 0; row < 4; row++) {
            if (row == column) continue;
            float factor = augmented[row][column];
            for (int i = 0; i < 8; i++)
                augmented[row][i] -= factor * augmented[column][i];
        }
    }

    for (row = 0; row < 4; row++)
        for (column = 0; column < 4; column++)
            result.value[column * 4 + row] = augmented[row][column + 4];
    *out = result;
    return 1;
}

void ath_matrix4_translate(AthenaMatrix4 *out, const AthenaMatrix4 *in,
    const AthenaVector4 *translation) {
    AthenaMatrix4 transform;
    ath_matrix4_identity(&transform);
    transform.value[12] = translation->x;
    transform.value[13] = translation->y;
    transform.value[14] = translation->z;
    ath_matrix4_multiply(out, in, &transform);
}

void ath_matrix4_scale(AthenaMatrix4 *out, const AthenaMatrix4 *in,
    const AthenaVector4 *scale) {
    AthenaMatrix4 transform;
    ath_matrix4_identity(&transform);
    transform.value[0] = scale->x;
    transform.value[5] = scale->y;
    transform.value[10] = scale->z;
    ath_matrix4_multiply(out, in, &transform);
}

void ath_matrix4_rotate(AthenaMatrix4 *out, const AthenaMatrix4 *in,
    const AthenaVector4 *rotation) {
    AthenaMatrix4 work;
    ath_matrix4_identity(&work);
    work.value[0] = cosf(rotation->z);
    work.value[1] = sinf(rotation->z);
    work.value[4] = -sinf(rotation->z);
    work.value[5] = cosf(rotation->z);
    ath_matrix4_multiply(out, in, &work);

    ath_matrix4_identity(&work);
    work.value[0] = cosf(rotation->y);
    work.value[2] = -sinf(rotation->y);
    work.value[8] = sinf(rotation->y);
    work.value[10] = cosf(rotation->y);
    ath_matrix4_multiply(out, out, &work);

    ath_matrix4_identity(&work);
    work.value[5] = cosf(rotation->x);
    work.value[6] = sinf(rotation->x);
    work.value[9] = -sinf(rotation->x);
    work.value[10] = cosf(rotation->x);
    ath_matrix4_multiply(out, out, &work);
}

int ath_matrix4_equals(const AthenaMatrix4 *a, const AthenaMatrix4 *b) {
    for (int i = 0; i < 16; i++)
        if (a->value[i] != b->value[i]) return 0;
    return 1;
}

int ath_matrix4_equals_epsilon(const AthenaMatrix4 *a,
    const AthenaMatrix4 *b, float epsilon) {
    if (epsilon < 0.0f) return 0;
    for (int i = 0; i < 16; i++)
        if (fabsf(a->value[i] - b->value[i]) > epsilon) return 0;
    return 1;
}
