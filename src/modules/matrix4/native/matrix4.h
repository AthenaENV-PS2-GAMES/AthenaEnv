#ifndef ATH_MATRIX4_H
#define ATH_MATRIX4_H

#include "../vector/native/vector_math.h"

typedef struct __attribute__((aligned(16))) {
    float value[16];
} AthenaMatrix4;

void ath_matrix4_identity(AthenaMatrix4 *matrix);
void ath_matrix4_copy(AthenaMatrix4 *out, const AthenaMatrix4 *in);
void ath_matrix4_multiply(AthenaMatrix4 *out, const AthenaMatrix4 *a,
    const AthenaMatrix4 *b);
void ath_matrix4_apply(AthenaVector4 *out, const AthenaMatrix4 *matrix,
    const AthenaVector4 *vector);
void ath_matrix4_transpose(AthenaMatrix4 *out, const AthenaMatrix4 *in);
int ath_matrix4_inverse(AthenaMatrix4 *out, const AthenaMatrix4 *in);
void ath_matrix4_translate(AthenaMatrix4 *out, const AthenaMatrix4 *in,
    const AthenaVector4 *translation);
void ath_matrix4_scale(AthenaMatrix4 *out, const AthenaMatrix4 *in,
    const AthenaVector4 *scale);
void ath_matrix4_rotate(AthenaMatrix4 *out, const AthenaMatrix4 *in,
    const AthenaVector4 *rotation);
int ath_matrix4_equals(const AthenaMatrix4 *a, const AthenaMatrix4 *b);
int ath_matrix4_equals_epsilon(const AthenaMatrix4 *a,
    const AthenaMatrix4 *b, float epsilon);

#endif
