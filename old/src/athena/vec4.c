#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <vector.h>
#include <athena/vec4.h>

AthenaVec4 *athena_vec4_create(float x, float y, float z, float w) {
    AthenaVec4 *v = calloc(1, sizeof(*v));
    if (!v)
        return NULL;
    v->v.x = x;
    v->v.y = y;
    v->v.z = z;
    v->v.w = w;
    return v;
}

void athena_vec4_destroy(AthenaVec4 *v) {
    free(v);
}

float athena_vec4_get_x(const AthenaVec4 *v) {
    return v ? v->v.x : 0.0f;
}

float athena_vec4_get_y(const AthenaVec4 *v) {
    return v ? v->v.y : 0.0f;
}

float athena_vec4_get_z(const AthenaVec4 *v) {
    return v ? v->v.z : 0.0f;
}

float athena_vec4_get_w(const AthenaVec4 *v) {
    return v ? v->v.w : 0.0f;
}

void athena_vec4_set_x(AthenaVec4 *v, float x) {
    if (v)
        v->v.x = x;
}

void athena_vec4_set_y(AthenaVec4 *v, float y) {
    if (v)
        v->v.y = y;
}

void athena_vec4_set_z(AthenaVec4 *v, float z) {
    if (v)
        v->v.z = z;
}

void athena_vec4_set_w(AthenaVec4 *v, float w) {
    if (v)
        v->v.w = w;
}

float athena_vec4_norm(const AthenaVec4 *v) {
    if (!v)
        return 0.0f;
    return vector_functions->get_length(v->v.f);
}

float athena_vec4_dot(const AthenaVec4 *a, const AthenaVec4 *b) {
    if (!a || !b)
        return 0.0f;
    return vector_functions->dot(a->v.f, b->v.f);
}

AthenaVec4 *athena_vec4_cross(const AthenaVec4 *a, const AthenaVec4 *b) {
    if (!a || !b)
        return NULL;
    AthenaVec4 *out = athena_vec4_create(0.0f, 0.0f, 0.0f, 0.0f);
    if (!out)
        return NULL;
    vector_functions->cross(out->v.f, a->v.f, b->v.f);
    return out;
}

float athena_vec4_distance(const AthenaVec4 *a, const AthenaVec4 *b) {
    if (!a || !b)
        return 0.0f;
    float dx = b->v.x - a->v.x;
    float dy = b->v.y - a->v.y;
    float dz = b->v.z - a->v.z;
    float dw = b->v.w - a->v.w;
    return sqrtf(dx * dx + dy * dy + dz * dz + dw * dw);
}

float athena_vec4_distance2(const AthenaVec4 *a, const AthenaVec4 *b) {
    if (!a || !b)
        return 0.0f;
    float dx = b->v.x - a->v.x;
    float dy = b->v.y - a->v.y;
    float dz = b->v.z - a->v.z;
    float dw = b->v.w - a->v.w;
    return dx * dx + dy * dy + dz * dz + dw * dw;
}

bool athena_vec4_equals(const AthenaVec4 *a, const AthenaVec4 *b) {
    if (!a || !b)
        return false;
    return vector_functions->equals(a->v.f, b->v.f) != 0;
}

static AthenaVec4 *athena_vec4_alloc(void) {
    return calloc(1, sizeof(AthenaVec4));
}

AthenaVec4 *athena_vec4_add(const AthenaVec4 *a, const AthenaVec4 *b) {
    if (!a || !b)
        return NULL;
    AthenaVec4 *out = athena_vec4_alloc();
    if (!out)
        return NULL;
    vector_functions->add(out->v.f, a->v.f, b->v.f);
    return out;
}

AthenaVec4 *athena_vec4_sub(const AthenaVec4 *a, const AthenaVec4 *b) {
    if (!a || !b)
        return NULL;
    AthenaVec4 *out = athena_vec4_alloc();
    if (!out)
        return NULL;
    vector_functions->sub(out->v.f, a->v.f, b->v.f);
    return out;
}

AthenaVec4 *athena_vec4_mul(const AthenaVec4 *a, const AthenaVec4 *b) {
    if (!a || !b)
        return NULL;
    AthenaVec4 *out = athena_vec4_alloc();
    if (!out)
        return NULL;
    vector_functions->mul(out->v.f, a->v.f, b->v.f);
    return out;
}

AthenaVec4 *athena_vec4_div(const AthenaVec4 *a, const AthenaVec4 *b) {
    if (!a || !b)
        return NULL;
    AthenaVec4 *out = athena_vec4_alloc();
    if (!out)
        return NULL;
    vector_functions->div(out->v.f, a->v.f, b->v.f);
    return out;
}

int athena_vec4_tostring(const AthenaVec4 *v, char *buf, size_t buflen) {
    if (!v || !buf || buflen == 0)
        return 0;
    return snprintf(buf, buflen, "{x:%g, y:%g, z:%g, w:%g}", v->v.x, v->v.y, v->v.z, v->v.w);
}
