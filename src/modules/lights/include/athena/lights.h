#ifndef ATHENA_LIGHTS_H
#define ATHENA_LIGHTS_H
#include <stdint.h>
#define ATHENA_LIGHTS_MAX_DIRECTIONAL 4u
#define ATHENA_LIGHTS_MAX_POINT 4u
typedef struct AthenaLights AthenaLights;
/* Independent world-space lights. Directions point TOWARD the light source.
 * RGB intensities are linear, finite and in [0,1]. Point lights fade with
 * distance as (1 - d^2 / range^2)^2, reaching 0 at range. No specular.
 * Main thread only; failed setters leave both state and revision unchanged. */
typedef struct {
    float ambient[4],direction[ATHENA_LIGHTS_MAX_DIRECTIONAL][4];
    float diffuse[ATHENA_LIGHTS_MAX_DIRECTIONAL][4];
    uint32_t count;
    /* Active point lights, compacted: position xyz and 1 / range^2 in w.
     * Aligned: Render3D copies these quadwords into VU1 packets with 128-bit
     * loads, which ignore the low address bits (after count they would sit
     * 4 bytes off and arrive shifted by one lane). */
    float point_position[ATHENA_LIGHTS_MAX_POINT][4] __attribute__((aligned(16)));
    float point_color[ATHENA_LIGHTS_MAX_POINT][4] __attribute__((aligned(16)));
    uint32_t point_count;
    /* Distance fog: full scene colour up to fog_start, fog_color from
     * fog_end on (view depth), applied by the GS. */
    uint32_t fog_enabled;
    float fog_start,fog_end,fog_color[3];
    uint64_t revision;
} AthenaLightsView;
AthenaLights *athena_lights_create(void);
void athena_lights_destroy(AthenaLights *lights);
int athena_lights_set_ambient(AthenaLights *lights,float r,float g,float b);
int athena_lights_set_directional(AthenaLights *lights,uint32_t slot,
    float x,float y,float z,float r,float g,float b);
int athena_lights_disable(AthenaLights *lights,uint32_t slot);
/* Slots 0..3; range > 0. */
int athena_lights_set_point(AthenaLights *lights,uint32_t slot,
    float x,float y,float z,float r,float g,float b,float range);
int athena_lights_disable_point(AthenaLights *lights,uint32_t slot);
/* 0 <= start < end; RGB in [0,1]. */
int athena_lights_set_fog(AthenaLights *lights,float start,float end,float r,float g,float b);
void athena_lights_disable_fog(AthenaLights *lights);
void athena_lights_clear(AthenaLights *lights);
void athena_lights_view(const AthenaLights *lights,AthenaLightsView *out);
/* The same view without a copy, valid until the next setter (NULL lights:
 * NULL). For per-draw checks such as fog. */
const AthenaLightsView *athena_lights_peek(const AthenaLights *lights);
#endif
