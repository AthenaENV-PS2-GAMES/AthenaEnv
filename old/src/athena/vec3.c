#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <athena/vec3.h>

AthenaVec3 *athena_vec3_create(float x, float y, float z) {
    AthenaVec3 *v = calloc(1, sizeof(*v));
    if (!v)
        return NULL;
    v->x = x;
    v->y = y;
    v->z = z;
    return v;
}

void athena_vec3_destroy(AthenaVec3 *v) {
    free(v);
}

float athena_vec3_get_x(const AthenaVec3 *v) {
    return v ? v->x : 0.0f;
}

float athena_vec3_get_y(const AthenaVec3 *v) {
    return v ? v->y : 0.0f;
}

float athena_vec3_get_z(const AthenaVec3 *v) {
    return v ? v->z : 0.0f;
}

void athena_vec3_set_x(AthenaVec3 *v, float x) {
    if (v)
        v->x = x;
}

void athena_vec3_set_y(AthenaVec3 *v, float y) {
    if (v)
        v->y = y;
}

void athena_vec3_set_z(AthenaVec3 *v, float z) {
    if (v)
        v->z = z;
}

float athena_vec3_norm(const AthenaVec3 *v) {
    if (!v)
        return 0.0f;
    return sqrtf(v->x * v->x + v->y * v->y + v->z * v->z);
}

float athena_vec3_dot(const AthenaVec3 *a, const AthenaVec3 *b) {
    if (!a || !b)
        return 0.0f;
    return a->x * b->x + a->y * b->y + a->z * b->z;
}

AthenaVec3 *athena_vec3_cross(const AthenaVec3 *a, const AthenaVec3 *b) {
    if (!a || !b)
        return NULL;
    AthenaVec3 *out = athena_vec3_create(0.0f, 0.0f, 0.0f);
    if (!out)
        return NULL;
    out->x = a->y * b->z - a->z * b->y;
    out->y = a->z * b->x - a->x * b->z;
    out->z = a->x * b->y - a->y * b->x;
    return out;
}

float athena_vec3_dist(const AthenaVec3 *a, const AthenaVec3 *b) {
    if (!a || !b)
        return 0.0f;
    float dx = b->x - a->x;
    float dy = b->y - a->y;
    float dz = b->z - a->z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

float athena_vec3_distsqr(const AthenaVec3 *a, const AthenaVec3 *b) {
    if (!a || !b)
        return 0.0f;
    float dx = b->x - a->x;
    float dy = b->y - a->y;
    float dz = b->z - a->z;
    return dx * dx + dy * dy + dz * dz;
}

static AthenaVec3 *athena_vec3_alloc(float x, float y, float z) {
    return athena_vec3_create(x, y, z);
}

AthenaVec3 *athena_vec3_add(const AthenaVec3 *a, const AthenaVec3 *b) {
    if (!a || !b)
        return NULL;
    return athena_vec3_alloc(a->x + b->x, a->y + b->y, a->z + b->z);
}

AthenaVec3 *athena_vec3_sub(const AthenaVec3 *a, const AthenaVec3 *b) {
    if (!a || !b)
        return NULL;
    return athena_vec3_alloc(a->x - b->x, a->y - b->y, a->z - b->z);
}

AthenaVec3 *athena_vec3_mul(const AthenaVec3 *a, const AthenaVec3 *b) {
    if (!a || !b)
        return NULL;
    return athena_vec3_alloc(a->x * b->x, a->y * b->y, a->z * b->z);
}

AthenaVec3 *athena_vec3_div(const AthenaVec3 *a, const AthenaVec3 *b) {
    if (!a || !b)
        return NULL;
    return athena_vec3_alloc(a->x / b->x, a->y / b->y, a->z / b->z);
}

int athena_vec3_tostring(const AthenaVec3 *v, char *buf, size_t buflen) {
    if (!v || !buf || buflen == 0)
        return 0;
    return snprintf(buf, buflen, "{x:%g, y:%g, z:%g}", v->x, v->y, v->z);
}
