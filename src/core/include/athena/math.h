#ifndef ATHENA_MATH_H
#define ATHENA_MATH_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Largest finite IEEE single (bits 0x7F7FFFFF), the same on every platform.
 * Do not use FLT_MAX to bound floats that cross to or from doubles: the EE
 * has no infinities, so its FLT_MAX is 6.8e38 (bits 0x7FFFFFFF, exponent
 * 255), which the soft-float double conversion reads as a NaN.
 */
#define ATHENA_FLOAT_MAX 3.40282346e38f

float athena_cosf(float x);
float athena_atan2f(float y, float x);
float athena_randomf(float min, float max);
int athena_randomi(int min, int max);
float athena_asinf(float x);
float athena_acosf(float x);
float athena_sinf(float x);
float athena_tanf(float x);

#ifdef __cplusplus
}
#endif

#endif