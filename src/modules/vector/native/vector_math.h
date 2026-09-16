#ifndef ATH_VECTOR_MATH_H
#define ATH_VECTOR_MATH_H

typedef struct __attribute__((aligned(16))) {
    float x;
    float y;
    float z;
    float w;
} AthenaVector4;

void ath_vector_add(AthenaVector4 *out, const AthenaVector4 *a, const AthenaVector4 *b);
void ath_vector_sub(AthenaVector4 *out, const AthenaVector4 *a, const AthenaVector4 *b);
void ath_vector_mul(AthenaVector4 *out, const AthenaVector4 *a, const AthenaVector4 *b);
void ath_vector_div(AthenaVector4 *out, const AthenaVector4 *a, const AthenaVector4 *b);
float ath_vector_dot3(const AthenaVector4 *a, const AthenaVector4 *b);
void ath_vector_cross(AthenaVector4 *out, const AthenaVector4 *a, const AthenaVector4 *b);
float ath_vector_length3(const AthenaVector4 *v);
float ath_vector_length4(const AthenaVector4 *v);
void ath_vector_normalize3(AthenaVector4 *out, const AthenaVector4 *v);

#endif
