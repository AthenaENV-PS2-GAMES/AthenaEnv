#ifndef ATHENA_RENDER3D_SHADE_H
#define ATHENA_RENDER3D_SHADE_H
#include <athena/render3d.h>
typedef struct {
    AthenaMatrix4 normal_matrix;
    AthenaLightsView lights;
    int enabled;
} AthenaShade3D;
/* Builds an inverse-transpose 3x3, uniformly rescaled for VU1 normalization.
 * Rejects singular or excessively ill-conditioned diffuse transforms. */
int athena_render3d_shade_prepare(AthenaShade3D *out,const AthenaMesh3DView *mesh,
    const AthenaMatrix4 *model,const AthenaLights *lights);
void athena_render3d_shade_color(const AthenaShade3D *shade,const AthenaPosition3D *normal,
    const AthenaColor3D *color,float out[4]);
#endif
