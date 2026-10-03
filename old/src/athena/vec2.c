#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <athena/vec2.h>

AthenaVec2 *athena_vec2_create(float x, float y) {
    AthenaVec2 *v = calloc(1, sizeof(*v));
    if (!v)
        return NULL;
    v->x = x;
    v->y = y;
    return v;
}

void athena_vec2_destroy(AthenaVec2 *v) {
    free(v);
}

float athena_vec2_get_x(const AthenaVec2 *v) {
    return v ? v->x : 0.0f;
}

float athena_vec2_get_y(const AthenaVec2 *v) {
    return v ? v->y : 0.0f;
}

void athena_vec2_set_x(AthenaVec2 *v, float x) {
    if (v)
        v->x = x;
}

void athena_vec2_set_y(AthenaVec2 *v, float y) {
    if (v)
        v->y = y;
}

float athena_vec2_norm(const AthenaVec2 *v) {
    if (!v)
        return 0.0f;
    return sqrtf(v->x * v->x + v->y * v->y);
}

float athena_vec2_dot(const AthenaVec2 *a, const AthenaVec2 *b) {
    if (!a || !b)
        return 0.0f;
    return a->x * b->x + a->y * b->y;
}

float athena_vec2_cross(const AthenaVec2 *a, const AthenaVec2 *b) {
    if (!a || !b)
        return 0.0f;
    return a->x * b->y - a->y * b->x;
}

float athena_vec2_dist(const AthenaVec2 *a, const AthenaVec2 *b) {
    if (!a || !b)
        return 0.0f;
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    return sqrtf(dx * dx + dy * dy);
}

float athena_vec2_distsqr(const AthenaVec2 *a, const AthenaVec2 *b) {
    if (!a || !b)
        return 0.0f;
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    return dx * dx + dy * dy;
}

static AthenaVec2 *athena_vec2_alloc(float x, float y) {
    return athena_vec2_create(x, y);
}

AthenaVec2 *athena_vec2_add(const AthenaVec2 *a, const AthenaVec2 *b) {
    if (!a || !b)
        return NULL;
    return athena_vec2_alloc(a->x + b->x, a->y + b->y);
}

AthenaVec2 *athena_vec2_sub(const AthenaVec2 *a, const AthenaVec2 *b) {
    if (!a || !b)
        return NULL;
    return athena_vec2_alloc(a->x - b->x, a->y - b->y);
}

AthenaVec2 *athena_vec2_mul(const AthenaVec2 *a, const AthenaVec2 *b) {
    if (!a || !b)
        return NULL;
    return athena_vec2_alloc(a->x * b->x, a->y * b->y);
}

AthenaVec2 *athena_vec2_div(const AthenaVec2 *a, const AthenaVec2 *b) {
    if (!a || !b)
        return NULL;
    return athena_vec2_alloc(a->x / b->x, a->y / b->y);
}

int athena_vec2_tostring(const AthenaVec2 *v, char *buf, size_t buflen) {
    if (!v || !buf || buflen == 0)
        return 0;
    return snprintf(buf, buflen, "{x:%g, y:%g}", v->x, v->y);
}
