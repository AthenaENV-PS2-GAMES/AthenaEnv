#ifndef ATHENA_VEC2_H
#define ATHENA_VEC2_H

#include <stddef.h>

typedef struct AthenaVec2 {
    float x;
    float y;
} AthenaVec2;

AthenaVec2 *athena_vec2_create(float x, float y);
void athena_vec2_destroy(AthenaVec2 *v);

float athena_vec2_get_x(const AthenaVec2 *v);
float athena_vec2_get_y(const AthenaVec2 *v);
void athena_vec2_set_x(AthenaVec2 *v, float x);
void athena_vec2_set_y(AthenaVec2 *v, float y);

float athena_vec2_norm(const AthenaVec2 *v);
float athena_vec2_dot(const AthenaVec2 *a, const AthenaVec2 *b);
float athena_vec2_cross(const AthenaVec2 *a, const AthenaVec2 *b);
float athena_vec2_dist(const AthenaVec2 *a, const AthenaVec2 *b);
float athena_vec2_distsqr(const AthenaVec2 *a, const AthenaVec2 *b);

AthenaVec2 *athena_vec2_add(const AthenaVec2 *a, const AthenaVec2 *b);
AthenaVec2 *athena_vec2_sub(const AthenaVec2 *a, const AthenaVec2 *b);
AthenaVec2 *athena_vec2_mul(const AthenaVec2 *a, const AthenaVec2 *b);
AthenaVec2 *athena_vec2_div(const AthenaVec2 *a, const AthenaVec2 *b);

int athena_vec2_tostring(const AthenaVec2 *v, char *buf, size_t buflen);

#endif /* ATHENA_VEC2_H */
