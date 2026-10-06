#ifndef ATHENA_LIGHTS_H
#define ATHENA_LIGHTS_H
#include <stdint.h>
#define ATHENA_LIGHTS_MAX_DIRECTIONAL 4u
typedef struct AthenaLights AthenaLights;
/* Independent world-space lights. Directions point TOWARD the light source.
 * RGB intensities are linear, finite and in [0,1]. No point/specular lights.
 * Main thread only; failed setters leave both state and revision unchanged. */
typedef struct {
    float ambient[4],direction[ATHENA_LIGHTS_MAX_DIRECTIONAL][4];
    float diffuse[ATHENA_LIGHTS_MAX_DIRECTIONAL][4];
    uint32_t count;
    uint64_t revision;
} AthenaLightsView;
AthenaLights *athena_lights_create(void);
void athena_lights_destroy(AthenaLights *lights);
int athena_lights_set_ambient(AthenaLights *lights,float r,float g,float b);
int athena_lights_set_directional(AthenaLights *lights,uint32_t slot,
    float x,float y,float z,float r,float g,float b);
int athena_lights_disable(AthenaLights *lights,uint32_t slot);
void athena_lights_clear(AthenaLights *lights);
void athena_lights_view(const AthenaLights *lights,AthenaLightsView *out);
#endif
