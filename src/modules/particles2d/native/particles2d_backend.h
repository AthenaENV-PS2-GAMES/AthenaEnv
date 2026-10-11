#ifndef ATHENA_PARTICLES2D_BACKEND_H
#define ATHENA_PARTICLES2D_BACKEND_H
/* Private boundary between the portable emitter and the GS/VU1 submission
 * (particles2d_gs.c); host tests capture the records instead. */
#include <stdint.h>
#include <athena/particles2d.h>
/* Two quadwords per particle, as draw_2D_particles.vcl reads them: centre,
 * half extent along the rotated X axis (half * cos, half * sin), then the
 * integer RGBA the GS RGBAQ register takes. */
typedef struct __attribute__((aligned(16))) AthenaParticle2DRecord {
    float x,y,hc,hs;
    int32_t r,g,b,a;
} AthenaParticle2DRecord;
/* Particles per VU1 batch (vu1/draw_2D_particles.vcl INBUF_SIZE). */
#define ATHENA_PARTICLES2D_BATCH 44
/* rect: u1, v1, u2, v2 in texels. count <= ATHENA_PARTICLES2D_BATCH * 8 per call. */
void athena_particles2d_submit(AthenaImage *image,const float rect[4],
    const AthenaParticle2DRecord *records,uint32_t count);
#endif
