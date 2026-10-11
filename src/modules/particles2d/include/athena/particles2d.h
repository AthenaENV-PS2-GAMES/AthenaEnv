#ifndef ATHENA_PARTICLES2D_H
#define ATHENA_PARTICLES2D_H
#include <stdint.h>
#include <athena/image.h>
/* Native 2D particles: emission, integration and drawing in C. The script
 * configures an emitter once, moves it and calls draw; nothing per particle
 * runs in JavaScript. Coordinates are 2D world units drawn through the 2D
 * view (Camera2D), +Y down. */
typedef struct AthenaEmitter2D AthenaEmitter2D;
#define ATHENA_PARTICLES2D_EINVAL (-1)
#define ATHENA_PARTICLES2D_ENOMEM (-2)
#define ATHENA_PARTICLES2D_MAX 16384u
#define ATHENA_PARTICLES2D_LOOP_PRIORITY 0
typedef struct {
    uint32_t capacity;              /* pool size, 1..MAX */
    float rate;                     /* particles per second while active */
    float life_min,life_max;        /* seconds, > 0 */
    float speed_min,speed_max;      /* units per second */
    float angle,spread;             /* direction in radians (0 = +X) and total spread */
    float gravity_x,gravity_y;      /* units per second squared */
    float drag;                     /* >= 0: velocity *= 1 / (1 + drag * dt) */
    float size_start,size_end;      /* square side in units, >= 0 */
    uint32_t color_start,color_end; /* packed RGBA (Color.new), alpha 0..128 */
    float rotation_min,rotation_max;/* initial angle, radians */
    float spin_min,spin_max;        /* radians per second */
    float area_width,area_height;   /* spawn rectangle centred on the position */
    float u1,v1,u2,v2;              /* texture rectangle in texels; all 0: whole image */
    uint32_t seed;                  /* 0 picks a fixed default */
} AthenaEmitter2DDesc;
/* Defaults: 256 particles, 1 s life, white, 8 units, no motion. */
void athena_emitter2d_defaults(AthenaEmitter2DDesc *desc);
int athena_emitter2d_create(const AthenaEmitter2DDesc *desc,AthenaEmitter2D **out);
/* Changes the configuration; live particles keep their state. A smaller
 * capacity drops the newest particles. */
int athena_emitter2d_configure(AthenaEmitter2D *emitter,const AthenaEmitter2DDesc *desc);
/* Current configuration, for partial changes. */
const AthenaEmitter2DDesc *athena_emitter2d_desc(const AthenaEmitter2D *emitter);
void athena_emitter2d_retain(AthenaEmitter2D *emitter);
void athena_emitter2d_release(AthenaEmitter2D *emitter);
int athena_emitter2d_set_position(AthenaEmitter2D *emitter,float x,float y);
void athena_emitter2d_set_active(AthenaEmitter2D *emitter,int active);
int athena_emitter2d_active(const AthenaEmitter2D *emitter);
/* Spawns up to count particles now; returns how many fit in the pool. */
uint32_t athena_emitter2d_emit(AthenaEmitter2D *emitter,uint32_t count);
void athena_emitter2d_clear(AthenaEmitter2D *emitter);
uint32_t athena_emitter2d_count(const AthenaEmitter2D *emitter);
/* Ages, integrates and spawns (rate * dt). */
int athena_emitter2d_update(AthenaEmitter2D *emitter,float dt);
/* Draws live particles with image, oldest first: two quadwords per particle
 * go to VU1, which builds the rotated quad (vu1/draw_2D_particles.vcl). */
void athena_emitter2d_draw(AthenaEmitter2D *emitter,AthenaImage *image);
/* Updates every live emitter. */
int athena_particles2d_update(float dt);
int athena_particles2d_attach_loop(int priority,void *owner);
int athena_particles2d_detach_loop(void);
void athena_particles2d_detach_owner(void *owner);
int athena_particles2d_loop_system(void);
#endif
