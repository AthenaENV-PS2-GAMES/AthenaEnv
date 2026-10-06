#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <athena/float_bits.h>
#include <athena/lights.h>
struct AthenaLights { AthenaLightsView state,cached; uint32_t enabled; int dirty; };
static int rgb(float r,float g,float b) {
    return athena_float_isfinite(r)&&athena_float_isfinite(g)&&athena_float_isfinite(b)&&
        r>=0&&r<=1&&g>=0&&g<=1&&b>=0&&b<=1;
}
AthenaLights *athena_lights_create(void) {
    AthenaLights *l=calloc(1,sizeof(*l));
    if(l) { l->state.ambient[3]=1; l->state.revision=1; l->dirty=1; }
    return l;
}
void athena_lights_destroy(AthenaLights *l) { free(l); }
int athena_lights_set_ambient(AthenaLights *l,float r,float g,float b) {
    if(!l||!rgb(r,g,b)) return 0;
    float value[4]={r,g,b,1};
    if(memcmp(value,l->state.ambient,sizeof(value))) {
        memcpy(l->state.ambient,value,sizeof(value)); l->state.revision++; l->dirty=1;
    }
    return 1;
}
int athena_lights_set_directional(AthenaLights *l,uint32_t slot,
    float x,float y,float z,float r,float g,float b) {
    if(!l||slot>=ATHENA_LIGHTS_MAX_DIRECTIONAL||!rgb(r,g,b)||
        !athena_float_isfinite(x)||!athena_float_isfinite(y)||!athena_float_isfinite(z)) return 0;
    double length=sqrt((double)x*x+(double)y*y+(double)z*z);
    if(length==0) return 0;
    float direction[4]={x/length,y/length,z/length,0},diffuse[4]={r,g,b,0};
    if(!(l->enabled&(1u<<slot))||memcmp(direction,l->state.direction[slot],sizeof(direction))||
        memcmp(diffuse,l->state.diffuse[slot],sizeof(diffuse))) {
        memcpy(l->state.direction[slot],direction,sizeof(direction));
        memcpy(l->state.diffuse[slot],diffuse,sizeof(diffuse));
        l->enabled|=1u<<slot; l->state.revision++; l->dirty=1;
    }
    return 1;
}
int athena_lights_disable(AthenaLights *l,uint32_t slot) {
    if(!l||slot>=ATHENA_LIGHTS_MAX_DIRECTIONAL) return 0;
    if(l->enabled&(1u<<slot)) { l->enabled&=~(1u<<slot); l->state.revision++; l->dirty=1; }
    return 1;
}
void athena_lights_clear(AthenaLights *l) {
    if(!l) return;
    if(l->enabled||l->state.ambient[0]!=0||l->state.ambient[1]!=0||l->state.ambient[2]!=0) {
        l->enabled=0; l->state.ambient[0]=l->state.ambient[1]=l->state.ambient[2]=0;
        l->state.revision++; l->dirty=1;
    }
}
void athena_lights_view(const AthenaLights *l,AthenaLightsView *out) {
    memset(out,0,sizeof(*out)); out->ambient[3]=1;
    if(!l) return;
    /* Logical const: only a private derived cache changes. Main thread only.
     * Compact active slots once per effective update, not once per instance. */
    AthenaLights *cache=(AthenaLights *)l;
    if(cache->dirty) {
        AthenaLightsView *v=&cache->cached; memset(v,0,sizeof(*v));
        memcpy(v->ambient,l->state.ambient,sizeof(v->ambient)); v->revision=l->state.revision;
        for(uint32_t slot=0;slot<ATHENA_LIGHTS_MAX_DIRECTIONAL;slot++) if(l->enabled&(1u<<slot)) {
            memcpy(v->direction[v->count],l->state.direction[slot],sizeof(v->direction[0]));
            memcpy(v->diffuse[v->count],l->state.diffuse[slot],sizeof(v->diffuse[0])); v->count++;
        }
        cache->dirty=0;
    }
    *out=cache->cached;
}
