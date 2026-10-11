#ifndef ATHENA_AUDIO3D_H
#define ATHENA_AUDIO3D_H
#include <stdint.h>
#include <athena/camera3d.h>
#include <athena/scene3d.h>
#include <athena/sound.h>
/* Positional audio math: a listener (position and right vector, usually a
 * camera) and a source give a channel volume (0..100) and pan (-100 left ..
 * 100 right). Attenuation is 1 up to min_distance and 0 from max_distance:
 * linear in between, or inverse (min / d, faded to 0 over the last 10% so it
 * reaches max_distance silent). Pan is the source direction projected on
 * the listener's right axis, times pan_strength. No sound calls here: the
 * binding applies the levels to the SPU2 voices. */
typedef struct { float position[3],right[3]; } AthenaAudio3DListener;
typedef enum { ATHENA_AUDIO3D_INVERSE=0, ATHENA_AUDIO3D_LINEAR=1 } AthenaAudio3DRolloff;
/* What happens to a source that loses its voice (out of range, or outranked when voices run out):
 * STOP ends it; VIRTUAL keeps it pending, and it restarts from the beginning when it ranks again
 * (the SPU2 cannot seek). */
typedef enum { ATHENA_AUDIO3D_CULL_DEFAULT=0, ATHENA_AUDIO3D_CULL_STOP=1, ATHENA_AUDIO3D_CULL_VIRTUAL=2 } AthenaAudio3DCull;
typedef struct {
    float min_distance,max_distance;
    AthenaAudio3DRolloff rolloff;
    float volume;        /* 0..100 at full gain */
    float pan_strength;  /* 0..1 */
    int priority;        /* -1000..1000, higher wins a voice; default 0 */
    AthenaAudio3DCull cull;   /* DEFAULT: virtual for looping samples, stop for one-shots */
} AthenaAudio3DParams;
void athena_audio3d_default_params(AthenaAudio3DParams *p);
int athena_audio3d_validate(const AthenaAudio3DParams *p);
/* Listener at the camera: its position and the right axis of its view. */
int athena_audio3d_listener_from_camera(AthenaCamera3D *camera,AthenaAudio3DListener *out);
/* Gain in [0,1] at distance d. */
float athena_audio3d_gain(const AthenaAudio3DParams *p,float d);
/* Levels for a source at position; returns the distance. */
float athena_audio3d_levels(const AthenaAudio3DListener *l,const float position[3],const AthenaAudio3DParams *p,int *volume,int *pan);

/* ---- Voices ----
 * Sources share at most `max_voices` SPU2 channels (default 24, i.e. all of them; other Sound users
 * keep the channels Audio3D does not hold). Every update the sources that want to sound are ranked by
 * priority, then predicted loudness (gain * volume, after occlusion), then distance, then creation
 * order, so the choice is deterministic; the best ones hold voices, the others are culled by their
 * policy. A source's voice is tied to a Sound voice token, so a channel that ended or was reused by
 * another sample is never touched. Main thread only. */
#define ATHENA_AUDIO3D_MAX_VOICES 24u
typedef struct AthenaAudio3DSource AthenaAudio3DSource;
/* The source's current sample (NULL once it was freed); called every update. */
typedef AthenaSfx *(*AthenaAudio3DResolve)(void *user);
AthenaAudio3DSource *athena_audio3d_source_create(AthenaAudio3DResolve resolve,void *user,const AthenaAudio3DParams *params);
/* Stops its voice and forgets it. */
void athena_audio3d_source_destroy(AthenaAudio3DSource *source);
/* 0, or -1 for invalid parameters (the source is unchanged). */
int athena_audio3d_source_configure(AthenaAudio3DSource *source,const AthenaAudio3DParams *params);
void athena_audio3d_source_params(const AthenaAudio3DSource *source,AthenaAudio3DParams *out);
void athena_audio3d_source_set_position(AthenaAudio3DSource *source,const float position[3]);
void athena_audio3d_source_position(const AthenaAudio3DSource *source,float out[3]);
/* Follows a node's world position of the last scene update (retained); NULL stops. */
void athena_audio3d_source_set_node(AthenaAudio3DSource *source,AthenaNode3D *node);
/* Starts the sample at its current levels. Returns the channel; -1 when it is out of range (unless
 * force), no voice is free, or it was outranked (a VIRTUAL source then stays pending); a negative
 * AthenaSoundResult below -1 on a Sound error. It may take the voice of a lower-ranked source. */
int athena_audio3d_source_play(AthenaAudio3DSource *source,int force);
void athena_audio3d_source_stop(AthenaAudio3DSource *source);
int athena_audio3d_source_channel(const AthenaAudio3DSource *source);   /* -1 without a voice */
int athena_audio3d_source_pending(const AthenaAudio3DSource *source);   /* wants to sound, has no voice */
int athena_audio3d_source_volume(const AthenaAudio3DSource *source);
int athena_audio3d_source_pan(const AthenaAudio3DSource *source);
float athena_audio3d_source_occlusion(const AthenaAudio3DSource *source);
int athena_audio3d_set_max_voices(uint32_t count);   /* 1..24; excess voices are culled at the next update */
uint32_t athena_audio3d_max_voices(void);
void athena_audio3d_set_listener(const AthenaAudio3DListener *listener);
void athena_audio3d_get_listener(AthenaAudio3DListener *out);

/* ---- Occlusion (opt-in) ----
 * A provider answers how blocked the straight path from the listener to a source is, 0 (clear) to 1
 * (fully blocked); it returns 0, or negative on failure (counted, the previous value is kept).
 * At most `max_queries` sources are asked per update, the stalest first, and a source is re-asked
 * after `interval` updates; the result is eased toward by `smoothing` per update and lowers the gain
 * by `strength` (1: a blocked source is silent). Sources that hold no voice are never queried. */
typedef int (*AthenaAudio3DOcclusionFn)(void *user,const float from[3],const float to[3],float *blocked);
typedef struct { uint32_t max_queries,interval; float strength,smoothing; } AthenaAudio3DOcclusionOptions;
void athena_audio3d_occlusion_defaults(AthenaAudio3DOcclusionOptions *out);
/* fn NULL turns occlusion off. -1 for invalid options. */
int athena_audio3d_set_occlusion(AthenaAudio3DOcclusionFn fn,void *user,const AthenaAudio3DOcclusionOptions *options);
typedef struct {
    uint32_t sources;       /* sources that want to sound (playing or pending) */
    uint32_t candidates;    /* of those, audible ones competing for voices */
    uint32_t voices;        /* voices held after the update */
    uint32_t stolen;        /* voices taken from a lower-ranked source */
    uint32_t culled;        /* sources that lost or could not get a voice (any reason) */
    uint32_t started;       /* voices started by the update */
    uint32_t queries,query_errors;
    uint32_t oldest_result; /* updates since the stalest occlusion answer of a voiced source */
} AthenaAudio3DStats;
/* Applies every source's levels, moves voices by rank, runs the occlusion budget. */
void athena_audio3d_update(AthenaAudio3DStats *stats);
void athena_audio3d_module_shutdown(void);
#endif
