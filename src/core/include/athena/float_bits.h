#ifndef ATHENA_FLOAT_BITS_H
#define ATHENA_FLOAT_BITS_H
#include <stdint.h>
#include <string.h>
/* R5900 GCC may fold isfinite(float) to true. External buffers can still
 * contain IEEE NaN/Inf bits, so input/output validation uses integer bits. */
static inline int athena_float_isfinite(float value) {
    uint32_t bits; memcpy(&bits,&value,sizeof(bits));
    return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000);
}
static inline int athena_double_isfinite(double value) {
    uint64_t bits; memcpy(&bits,&value,sizeof(bits));
    return (bits & UINT64_C(0x7ff0000000000000)) != UINT64_C(0x7ff0000000000000);
}
#endif
