#ifndef ATHENA_VEC4_H
#define ATHENA_VEC4_H

#include <stddef.h>
#include <stdbool.h>

#include <render.h>

typedef struct AthenaVec4 {
    VUVECTOR v;
} AthenaVec4;

AthenaVec4 *athena_vec4_create(float x, float y, float z, float w);
void athena_vec4_destroy(AthenaVec4 *v);

float athena_vec4_get_x(const AthenaVec4 *v);
float athena_vec4_get_y(const AthenaVec4 *v);
float athena_vec4_get_z(const AthenaVec4 *v);
float athena_vec4_get_w(const AthenaVec4 *v);
void athena_vec4_set_x(AthenaVec4 *v, float x);
void athena_vec4_set_y(AthenaVec4 *v, float y);
void athena_vec4_set_z(AthenaVec4 *v, float z);
void athena_vec4_set_w(AthenaVec4 *v, float w);

float athena_vec4_norm(const AthenaVec4 *v);
float athena_vec4_dot(const AthenaVec4 *a, const AthenaVec4 *b);
AthenaVec4 *athena_vec4_cross(const AthenaVec4 *a, const AthenaVec4 *b);
float athena_vec4_distance(const AthenaVec4 *a, const AthenaVec4 *b);
float athena_vec4_distance2(const AthenaVec4 *a, const AthenaVec4 *b);
bool athena_vec4_equals(const AthenaVec4 *a, const AthenaVec4 *b);

AthenaVec4 *athena_vec4_add(const AthenaVec4 *a, const AthenaVec4 *b);
AthenaVec4 *athena_vec4_sub(const AthenaVec4 *a, const AthenaVec4 *b);
AthenaVec4 *athena_vec4_mul(const AthenaVec4 *a, const AthenaVec4 *b);
AthenaVec4 *athena_vec4_div(const AthenaVec4 *a, const AthenaVec4 *b);

int athena_vec4_tostring(const AthenaVec4 *v, char *buf, size_t buflen);

#endif /* ATHENA_VEC4_H */
