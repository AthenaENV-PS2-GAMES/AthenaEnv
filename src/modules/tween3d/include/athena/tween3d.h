#ifndef ATHENA_TWEEN3D_H
#define ATHENA_TWEEN3D_H
#include <stdint.h>
#include <athena/ease_curves.h>
#include <athena/scene3d.h>
#include <athena/model3d.h>
#include <athena/camera3d.h>
/* Tweens of native targets advanced in C: no JS call per frame. Same
 * semantics as the JavaScript Tween: start values are read when the tween
 * starts (after its delay), the last frame sets the exact end values, repeat
 * adds cycles (-1 forever) and yoyo runs every other cycle backwards. */
typedef struct AthenaTween3D AthenaTween3D;
typedef enum { ATHENA_TWEEN3D_NODE, ATHENA_TWEEN3D_INSTANCE, ATHENA_TWEEN3D_CAMERA } AthenaTween3DTarget;
/* Node/Instance: POSITION, SCALE, ROTATION (Euler goal, slerp on the short
 * arc; overshooting curves extrapolate the arc). Camera: POSITION, LOOK. */
enum { ATHENA_TWEEN3D_POSITION=1, ATHENA_TWEEN3D_SCALE=2, ATHENA_TWEEN3D_ROTATION=4, ATHENA_TWEEN3D_LOOK=8 };
#define ATHENA_TWEEN3D_EINVAL (-1)
#define ATHENA_TWEEN3D_ENOMEM (-2)
#define ATHENA_TWEEN3D_LOOP_PRIORITY 0
typedef struct {
    uint32_t channels;
    float position[3],scale[3],rotation[3],look[3];
    float duration,delay;
    int32_t repeat;          /* extra cycles, -1 forever */
    int yoyo,overwrite;
    AthenaEaseCurve ease;
} AthenaTween3DDesc;
/* Called once when the tween ends: completed 1, killed 0. */
typedef void (*AthenaTween3DEnd)(void *opaque,int completed);
/* release_target (may be NULL) runs when the tween is freed; the JS binding
 * uses it for camera handles. Nodes and instances are retained here. */
int athena_tween3d_create(AthenaTween3DTarget kind,void *target,void (*release_target)(void *),
    const AthenaTween3DDesc *desc,AthenaTween3D **out);
void athena_tween3d_retain(AthenaTween3D *tween);
void athena_tween3d_release(AthenaTween3D *tween);
void athena_tween3d_set_end(AthenaTween3D *tween,AthenaTween3DEnd end,void *opaque);
void athena_tween3d_pause(AthenaTween3D *tween,int paused);
int athena_tween3d_paused(const AthenaTween3D *tween);
/* False once completed or killed. */
int athena_tween3d_active(const AthenaTween3D *tween);
/* Progress of the current cycle, 0..1 (0 while delayed). */
float athena_tween3d_progress(const AthenaTween3D *tween);
/* Stops it; with complete, applies the end values first. */
void athena_tween3d_kill(AthenaTween3D *tween,int complete);
/* Kills the active tweens of target; returns how many. */
int athena_tween3d_kill_target(const void *target,int complete);
int athena_tween3d_count_target(const void *target);
/* Advances every active tween; returns how many ended this step. */
int athena_tween3d_advance(float dt);
int athena_tween3d_attach_loop(int priority,void *owner);
int athena_tween3d_detach_loop(void);
void athena_tween3d_detach_owner(void *owner);
int athena_tween3d_loop_system(void);
/* Kills (without completing) every tween created with this owner tag. */
void athena_tween3d_kill_owner(void *owner);
void athena_tween3d_set_owner(AthenaTween3D *tween,void *owner);
#endif
