#include <stdlib.h>
#include <stdio.h>

#include <athena/matrix.h>

AthenaMatrix4 *athena_matrix4_create(void) {
    AthenaMatrix4 *m = calloc(1, sizeof(*m));
    if (!m)
        return NULL;
    matrix_functions->identity(m->m);
    return m;
}

AthenaMatrix4 *athena_matrix4_create_from(const float values[ATHENA_MATRIX4_LENGTH]) {
    AthenaMatrix4 *m = calloc(1, sizeof(*m));
    if (!m)
        return NULL;
    if (values) {
        for (int i = 0; i < ATHENA_MATRIX4_LENGTH; i++)
            ((float *)m->m)[i] = values[i];
    } else {
        matrix_functions->identity(m->m);
    }
    return m;
}

void athena_matrix4_destroy(AthenaMatrix4 *m) {
    free(m);
}

float athena_matrix4_get(const AthenaMatrix4 *m, int index) {
    if (!m || index < 0 || index >= ATHENA_MATRIX4_LENGTH)
        return 0.0f;
    return ((float *)m->m)[index];
}

void athena_matrix4_set(AthenaMatrix4 *m, int index, float value) {
    if (!m || index < 0 || index >= ATHENA_MATRIX4_LENGTH)
        return;
    ((float *)m->m)[index] = value;
}

void athena_matrix4_to_array(const AthenaMatrix4 *m, float out[ATHENA_MATRIX4_LENGTH]) {
    if (!m || !out)
        return;
    for (int i = 0; i < ATHENA_MATRIX4_LENGTH; i++)
        out[i] = ((float *)m->m)[i];
}

void athena_matrix4_from_array(AthenaMatrix4 *m, const float values[ATHENA_MATRIX4_LENGTH]) {
    if (!m || !values)
        return;
    for (int i = 0; i < ATHENA_MATRIX4_LENGTH; i++)
        ((float *)m->m)[i] = values[i];
}

AthenaMatrix4 *athena_matrix4_clone(const AthenaMatrix4 *m) {
    if (!m)
        return NULL;
    AthenaMatrix4 *out = calloc(1, sizeof(*out));
    if (!out)
        return NULL;
    matrix_functions->copy(out->m, m->m);
    return out;
}

void athena_matrix4_copy(AthenaMatrix4 *dst, const AthenaMatrix4 *src) {
    if (!dst || !src)
        return;
    matrix_functions->copy(dst->m, src->m);
}

void athena_matrix4_identity(AthenaMatrix4 *m) {
    if (!m)
        return;
    matrix_functions->identity(m->m);
}

void athena_matrix4_transpose(AthenaMatrix4 *m) {
    if (!m)
        return;
    matrix_functions->transpose(m->m, m->m);
}

void athena_matrix4_invert(AthenaMatrix4 *m) {
    if (!m)
        return;
    matrix_functions->inverse(m->m, m->m);
}

AthenaMatrix4 *athena_matrix4_multiply(const AthenaMatrix4 *a, const AthenaMatrix4 *b) {
    if (!a || !b)
        return NULL;
    AthenaMatrix4 *out = calloc(1, sizeof(*out));
    if (!out)
        return NULL;
    matrix_functions->multiply(out->m, a->m, b->m);
    return out;
}

bool athena_matrix4_equals(const AthenaMatrix4 *a, const AthenaMatrix4 *b) {
    if (!a || !b)
        return false;
    return matrix_functions->equals(a->m, b->m) != 0;
}

int athena_matrix4_tostring(const AthenaMatrix4 *m, char *buf, size_t buflen) {
    if (!m || !buf || buflen == 0)
        return 0;
    float values[ATHENA_MATRIX4_LENGTH];
    athena_matrix4_to_array(m, values);
    return snprintf(buf, buflen,
        "[%f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f]",
        values[0], values[1], values[2], values[3],
        values[4], values[5], values[6], values[7],
        values[8], values[9], values[10], values[11],
        values[12], values[13], values[14], values[15]);
}
