#ifndef ATHENA_ANIMATION3D_H
#define ATHENA_ANIMATION3D_H
#include <stdint.h>
#include <athena/scene3d.h>
/* Keyframe clips sampled in C and applied to Scene3D nodes: the script starts
 * and stops players, it does not compute poses every frame. */
typedef struct AthenaClip3D AthenaClip3D;
typedef struct AthenaPlayer3D AthenaPlayer3D;
typedef enum { ATHENA_ANIM3D_POSITION=0, ATHENA_ANIM3D_ROTATION=1, ATHENA_ANIM3D_SCALE=2,
    ATHENA_ANIM3D_WEIGHTS=3 } AthenaAnim3DPath;
typedef enum { ATHENA_ANIM3D_LINEAR=0, ATHENA_ANIM3D_STEP=1 } AthenaAnim3DInterpolation;
#define ATHENA_ANIM3D_EINVAL (-1)
#define ATHENA_ANIM3D_ENOMEM (-2)
#define ATHENA_ANIM3D_MAX_KEYS 65536u
#define ATHENA_ANIM3D_MAX_TRACKS 1024u
/* Default Loop priority: before Scene3D systems (0) update the transforms. */
#define ATHENA_ANIM3D_LOOP_PRIORITY (-100)
/* One channel of one target. times: key_count seconds, >= 0 and strictly
 * increasing. values: 3 floats per key (position, scale), 4 (rotation,
 * xyzw, normalized on copy; consecutive keys are made to share a
 * hemisphere so slerp takes the short path) or weight_count (morph target
 * weights, 1..ATHENA_MODEL3D_MAX_TARGETS, applied with
 * athena_node3d_set_weights()). Inputs are copied. */
typedef struct {
    uint32_t target; AthenaAnim3DPath path; AthenaAnim3DInterpolation interpolation;
    const float *times,*values; uint32_t key_count;
    uint32_t weight_count; /* WEIGHTS tracks only */
} AthenaTrack3DDesc;
int athena_clip3d_create(const AthenaTrack3DDesc *tracks,uint32_t count,AthenaClip3D **out);
void athena_clip3d_retain(AthenaClip3D *clip);
void athena_clip3d_release(AthenaClip3D *clip);
/* Last key time of all tracks. */
float athena_clip3d_duration(const AthenaClip3D *clip);
/* Highest track target + 1: the node count a player needs. */
uint32_t athena_clip3d_target_count(const AthenaClip3D *clip);
/* Retains the clip and nodes[0..count-1] (count >= target_count, no NULL).
 * New players are stopped at time 0, speed 1, not looping. */
AthenaPlayer3D *athena_player3d_create(AthenaClip3D *clip,AthenaNode3D *const *nodes,uint32_t count);
void athena_player3d_retain(AthenaPlayer3D *player);
void athena_player3d_release(AthenaPlayer3D *player);
void athena_player3d_play(AthenaPlayer3D *player);
void athena_player3d_pause(AthenaPlayer3D *player);
/* Pauses and rewinds to 0 (or to the end for a negative speed); no pose. */
void athena_player3d_stop(AthenaPlayer3D *player);
int athena_player3d_playing(const AthenaPlayer3D *player);
/* Seeks and applies the pose at time (clamped, or wrapped when looping). */
int athena_player3d_set_time(AthenaPlayer3D *player,float time);
float athena_player3d_time(const AthenaPlayer3D *player);
/* Any finite speed; negative plays backwards. */
int athena_player3d_set_speed(AthenaPlayer3D *player,float speed);
float athena_player3d_speed(const AthenaPlayer3D *player);
void athena_player3d_set_loop(AthenaPlayer3D *player,int loop);
int athena_player3d_loop(const AthenaPlayer3D *player);
/* Advances a playing player by dt*speed seconds and applies the pose. A
 * non-looping player stops at the end (returns 1 that step), else 0. */
int athena_player3d_advance(AthenaPlayer3D *player,float dt);
/* Advances every live playing player; returns how many finished. */
int athena_animation3d_advance(float dt);
/* One Loop POST_UPDATE system advancing every player (see LOOP_PRIORITY).
 * Returns the system id; attached at most once (EINVAL). owner tags it for
 * athena_animation3d_detach_owner(). */
int athena_animation3d_attach_loop(int priority,void *owner);
int athena_animation3d_detach_loop(void);
void athena_animation3d_detach_owner(void *owner);
int athena_animation3d_loop_system(void);
#endif
