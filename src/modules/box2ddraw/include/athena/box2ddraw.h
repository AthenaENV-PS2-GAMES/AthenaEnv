#ifndef ATH_NATIVE_BOX2DDRAW_H
#define ATH_NATIVE_BOX2DDRAW_H

#include <stdbool.h>

#include <box2d/box2d.h>

/*
 * Debug drawing of a Box2D world with the graphics module: shape outlines,
 * joints, bounds, contacts and centers of mass, in Box2D's colors (static
 * bodies green, sleeping gray, ...). Lines are batched into one GS packet
 * stream per call, and what lies outside the screen is skipped by Box2D.
 * Call between the frame clear and flip, from the thread that renders.
 */

typedef struct AthenaBox2DDrawOptions {
    float scale;    /* pixels per meter */
    float offsetX;  /* screen position of the world origin */
    float offsetY;
    bool flipY;     /* world y up, screen y down */
    bool fill;      /* translucent shape interiors */
    bool shapes;
    bool joints;
    bool jointExtras;
    bool bounds;    /* shape AABBs */
    bool contacts;  /* contact points and normals */
    bool mass;      /* centers of mass */
} AthenaBox2DDrawOptions;

/* Scale 32, origin at the screen center, y up, shapes and joints. */
void athena_box2d_draw_defaults(AthenaBox2DDrawOptions *options);

/* Returns false when the graphics service is not initialized. */
bool athena_box2d_draw(b2WorldId world, const AthenaBox2DDrawOptions *options);

#endif /* ATH_NATIVE_BOX2DDRAW_H */
