#ifndef ATHENA_SKY_H
#define ATHENA_SKY_H
#include <stdint.h>
#include <athena/camera3d.h>
#include <athena/lights.h>
/* Sky and time of day. The sky is a vertical gradient (zenith, horizon,
 * ground) drawn in screen space before the 3D scene, as horizontal bands
 * colored by the elevation of the camera ray through each band (camera
 * roll is ignored), plus a sun disc and halo at the sun's projection. No
 * geometry around the camera, so nothing for the clipper. set_time() blends
 * built-in keyframes (night, dawn, day, dusk) into the colors, the sun and
 * the light colors that apply() writes into a Lights set (ambient,
 * directional slot 0 toward the sun or the moon, fog colour). Colors are
 * linear RGB in [0,1]. Main thread only. */
typedef struct {
    float zenith[3],horizon[3],ground[3];
    float sun_direction[3];   /* unit, toward the sun */
    float sun_color[3];       /* disc color; black hides it */
    float sun_size;           /* disc radius in pixels */
    float ambient[3],light[3],light_direction[3];
    float time;               /* hours, after set_time() */
} AthenaSkyState;
void athena_sky_get(AthenaSkyState *out);
int athena_sky_set_colors(const float zenith[3],const float horizon[3],const float ground[3]);
int athena_sky_set_sun(const float direction[3],const float color[3],float size);
/* Hours in [0, 24) (wrapped); sets colors, sun and light colors. */
int athena_sky_set_time(float hours);
/* Color of a ray at elevation (radians, -pi/2..pi/2). */
void athena_sky_color_at(float elevation,float out[3]);
/* Writes ambient, directional slot 0 and, when fog is enabled in lights
 * and fog is nonzero, the fog colour (the horizon). */
int athena_sky_apply(AthenaLights *lights,int fog);
/* Screen-space bands for a width x height viewport: band i spans rows
 * y[i]..y[i+1] with top color top[i] and bottom color bottom[i]. Returns the
 * band count (<= max). */
typedef struct { float y0,y1; float top[3],bottom[3]; } AthenaSkyBand;
int athena_sky_bands(AthenaCamera3D *camera,float width,float height,AthenaSkyBand *out,uint32_t max);
/* Sun position on screen: 1 with x, y when in front of the camera. */
int athena_sky_sun_screen(AthenaCamera3D *camera,float width,float height,float out[2]);
/* Draws the bands and the sun on the current framebuffer (sky_gs.c). */
int athena_sky_draw(AthenaCamera3D *camera,uint32_t bands);
void athena_sky_module_shutdown(void);
#endif
