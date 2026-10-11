#ifndef ATHENA_PARTICLES3D_BACKEND_H
#define ATHENA_PARTICLES3D_BACKEND_H
/* Private boundary between the portable emitter and the GS/VU1 submission
 * (particles3d_gs.c); host tests capture the records instead. */
#include <stdint.h>
#include <athena/particles3d.h>
/* Two quadwords per particle, as draw_3D_billboards.vcl reads them. */
typedef struct __attribute__((aligned(16))) AthenaParticle3DRecord {
    float x,y,z,half;
    int32_t r,g,b,a;
} AthenaParticle3DRecord;
#define ATHENA_PARTICLES3D_BATCH 44
/* rect: u1, v1, u2, v2 in texels. Returns 0, or NO_ZBUFFER / ENOMEM. */
int athena_particles3d_submit(AthenaCamera3D *camera,AthenaImage *image,const float rect[4],
    const AthenaParticle3DRecord *records,uint32_t count);
#endif
