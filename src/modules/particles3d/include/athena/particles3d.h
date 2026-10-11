#ifndef ATHENA_PARTICLES3D_H
#define ATHENA_PARTICLES3D_H
#include <stdint.h>
#include <athena/image.h>
#include <athena/camera3d.h>
/* Native 3D particles drawn as camera-facing quads (billboards) by VU1
 * (vu1/draw_3D_billboards.vcl): emission and integration in C, depth tested
 * against the scene's z-buffer without writing it. World units, right-handed
 * as Camera3D. */
typedef struct AthenaEmitter3D AthenaEmitter3D;
#define ATHENA_PARTICLES3D_EINVAL (-1)
#define ATHENA_PARTICLES3D_ENOMEM (-2)
#define ATHENA_PARTICLES3D_NO_ZBUFFER (-3)
#define ATHENA_PARTICLES3D_MAX 16384u
#define ATHENA_PARTICLES3D_LOOP_PRIORITY 0
typedef struct {
    uint32_t capacity;              /* pool size, 1..MAX */
    float rate;                     /* particles per second while active */
    float life_min,life_max;        /* seconds, > 0 */
    float speed_min,speed_max;      /* units per second */
    float direction[3];             /* cone axis, normalized on configure */
    float spread;                   /* cone half angle, radians, 0..pi */
    float gravity[3];               /* units per second squared */
    float drag;                     /* >= 0: velocity *= 1 / (1 + drag * dt) */
    float size_start,size_end;      /* quad side in units, >= 0 */
    uint32_t color_start,color_end; /* packed RGBA (Color.new), alpha 0..128 */
    float area[3];                  /* spawn box centred on the position */
    float u1,v1,u2,v2;              /* texture rectangle in texels; all 0: whole image */
    uint32_t seed;                  /* 0 picks a fixed default */
} AthenaEmitter3DDesc;
/* Defaults: 256 particles, 1 s life, white, 0.5 units, upward cone of 0. */
void athena_emitter3d_defaults(AthenaEmitter3DDesc *desc);
int athena_emitter3d_create(const AthenaEmitter3DDesc *desc,AthenaEmitter3D **out);
int athena_emitter3d_configure(AthenaEmitter3D *emitter,const AthenaEmitter3DDesc *desc);
const AthenaEmitter3DDesc *athena_emitter3d_desc(const AthenaEmitter3D *emitter);
void athena_emitter3d_retain(AthenaEmitter3D *emitter);
void athena_emitter3d_release(AthenaEmitter3D *emitter);
int athena_emitter3d_set_position(AthenaEmitter3D *emitter,float x,float y,float z);
void athena_emitter3d_set_active(AthenaEmitter3D *emitter,int active);
int athena_emitter3d_active(const AthenaEmitter3D *emitter);
uint32_t athena_emitter3d_emit(AthenaEmitter3D *emitter,uint32_t count);
void athena_emitter3d_clear(AthenaEmitter3D *emitter);
uint32_t athena_emitter3d_count(const AthenaEmitter3D *emitter);
int athena_emitter3d_update(AthenaEmitter3D *emitter,float dt);
/* Draws the live particles seen by camera, oldest first; particles outside
 * the near/far range are skipped on the EE. Returns how many were sent, or
 * EINVAL for an invalid camera and NO_ZBUFFER without a depth buffer. */
int athena_emitter3d_draw(AthenaEmitter3D *emitter,AthenaCamera3D *camera,AthenaImage *image);
int athena_particles3d_update(float dt);
int athena_particles3d_attach_loop(int priority,void *owner);
int athena_particles3d_detach_loop(void);
void athena_particles3d_detach_owner(void *owner);
int athena_particles3d_loop_system(void);
#endif
