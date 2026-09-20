#ifndef ATHENA_MATRIX_FACADE_H
#define ATHENA_MATRIX_FACADE_H

#include <stddef.h>
#include <stdbool.h>

#include <matrix.h>

#define ATHENA_MATRIX4_LENGTH 16

typedef struct AthenaMatrix4 {
    MATRIX m;
} AthenaMatrix4;

AthenaMatrix4 *athena_matrix4_create(void);
AthenaMatrix4 *athena_matrix4_create_from(const float values[ATHENA_MATRIX4_LENGTH]);
void athena_matrix4_destroy(AthenaMatrix4 *m);

float athena_matrix4_get(const AthenaMatrix4 *m, int index);
void athena_matrix4_set(AthenaMatrix4 *m, int index, float value);
void athena_matrix4_to_array(const AthenaMatrix4 *m, float out[ATHENA_MATRIX4_LENGTH]);
void athena_matrix4_from_array(AthenaMatrix4 *m, const float values[ATHENA_MATRIX4_LENGTH]);

AthenaMatrix4 *athena_matrix4_clone(const AthenaMatrix4 *m);
void athena_matrix4_copy(AthenaMatrix4 *dst, const AthenaMatrix4 *src);
void athena_matrix4_identity(AthenaMatrix4 *m);
void athena_matrix4_transpose(AthenaMatrix4 *m);
void athena_matrix4_invert(AthenaMatrix4 *m);
AthenaMatrix4 *athena_matrix4_multiply(const AthenaMatrix4 *a, const AthenaMatrix4 *b);
bool athena_matrix4_equals(const AthenaMatrix4 *a, const AthenaMatrix4 *b);

int athena_matrix4_tostring(const AthenaMatrix4 *m, char *buf, size_t buflen);

#endif /* ATHENA_MATRIX_FACADE_H */
