#ifndef ATHENA_AUDIO3D_H
#define ATHENA_AUDIO3D_H
#include <stdint.h>
#include <athena/camera3d.h>
/* Positional audio math: a listener (position and right vector, usually a
 * camera) and a source give a channel volume (0..100) and pan (-100 left ..
 * 100 right). Attenuation is 1 up to min_distance and 0 from max_distance:
 * linear in between, or inverse (min / d, faded to 0 over the last 10% so it
 * reaches max_distance silent). Pan is the source direction projected on
 * the listener's right axis, times pan_strength. No sound calls here: the
 * binding applies the levels to the SPU2 voices. */
typedef struct { float position[3],right[3]; } AthenaAudio3DListener;
typedef enum { ATHENA_AUDIO3D_INVERSE=0, ATHENA_AUDIO3D_LINEAR=1 } AthenaAudio3DRolloff;
typedef struct {
    float min_distance,max_distance;
    AthenaAudio3DRolloff rolloff;
    float volume;        /* 0..100 at full gain */
    float pan_strength;  /* 0..1 */
} AthenaAudio3DParams;
void athena_audio3d_default_params(AthenaAudio3DParams *p);
int athena_audio3d_validate(const AthenaAudio3DParams *p);
/* Listener at the camera: its position and the right axis of its view. */
int athena_audio3d_listener_from_camera(AthenaCamera3D *camera,AthenaAudio3DListener *out);
/* Gain in [0,1] at distance d. */
float athena_audio3d_gain(const AthenaAudio3DParams *p,float d);
/* Levels for a source at position; returns the distance. */
float athena_audio3d_levels(const AthenaAudio3DListener *l,const float position[3],const AthenaAudio3DParams *p,int *volume,int *pan);
#endif
