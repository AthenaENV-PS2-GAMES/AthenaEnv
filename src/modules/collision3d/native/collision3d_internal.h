#ifndef ATHENA_COLLISION3D_INTERNAL_H
#define ATHENA_COLLISION3D_INTERNAL_H
/* Private: the swept ellipsoid shared by sphere casts and characters. */
#include <athena/collision3d.h>
/* Ellipsoid space: world coordinates divided by radius, where the swept
 * shape is the unit sphere (Fauerby, "Improved Collision detection and
 * Response"). base and velocity are in that space. */
typedef struct {
    float radius[3];
    float base[3],velocity[3];
    uint32_t mask;
    /* Result: the first front face touched along velocity. */
    int found;
    float distance;   /* in ellipsoid space, along velocity */
    float point[3];   /* contact point, ellipsoid space */
    int shape; uint32_t triangle;
    int edge;         /* touched an edge or vertex, not the inside of a face */
} AthenaSweep3D;
/* 0 on success (found tells whether something was hit), negative error. */
int athena_collision3d_sweep(AthenaCollision3DWorld *world,AthenaSweep3D *sweep);
#endif
