#ifndef ATHENA_VEC3_H
#define ATHENA_VEC3_H

#include <stddef.h>

typedef struct AthenaVec3 {
    float x;
    float y;
    float z;
} AthenaVec3;

AthenaVec3 *athena_vec3_create(float x, float y, float z);
void athena_vec3_destroy(AthenaVec3 *v);

float athena_vec3_get_x(const AthenaVec3 *v);
float athena_vec3_get_y(const AthenaVec3 *v);
float athena_vec3_get_z(const AthenaVec3 *v);
void athena_vec3_set_x(AthenaVec3 *v, float x);
void athena_vec3_set_y(AthenaVec3 *v, float y);
void athena_vec3_set_z(AthenaVec3 *v, float z);

float athena_vec3_norm(const AthenaVec3 *v);
float athena_vec3_dot(const AthenaVec3 *a, const AthenaVec3 *b);
AthenaVec3 *athena_vec3_cross(const AthenaVec3 *a, const AthenaVec3 *b);
float athena_vec3_dist(const AthenaVec3 *a, const AthenaVec3 *b);
float athena_vec3_distsqr(const AthenaVec3 *a, const AthenaVec3 *b);

AthenaVec3 *athena_vec3_add(const AthenaVec3 *a, const AthenaVec3 *b);
AthenaVec3 *athena_vec3_sub(const AthenaVec3 *a, const AthenaVec3 *b);
AthenaVec3 *athena_vec3_mul(const AthenaVec3 *a, const AthenaVec3 *b);
AthenaVec3 *athena_vec3_div(const AthenaVec3 *a, const AthenaVec3 *b);

int athena_vec3_tostring(const AthenaVec3 *v, char *buf, size_t buflen);

#endif /* ATHENA_VEC3_H */
