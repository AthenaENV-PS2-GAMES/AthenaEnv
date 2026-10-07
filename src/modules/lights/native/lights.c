#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include <athena/float_bits.h>
#include <athena/lights.h>
struct AthenaLights { AthenaLightsView state,cached; uint32_t enabled,points; int dirty; };
/* Stamps are unique across every Lights object (revisions are per object and
 * start at 1), so a renderer can key cached uploads by stamp alone. */
static uint64_t next_stamp;
static void touch(AthenaLights *l) { l->state.revision++; l->state.stamp=++next_stamp; l->dirty=1; }
static int rgb(float r,float g,float b) {
    return athena_float_isfinite(r)&&athena_float_isfinite(g)&&athena_float_isfinite(b)&&
        r>=0&&r<=1&&g>=0&&g<=1&&b>=0&&b<=1;
}
AthenaLights *athena_lights_create(void) {
    /* AthenaLightsView is quadword aligned: calloc only guarantees 8 bytes. */
    AthenaLights *l=memalign(16,sizeof(*l));
    if(l) { memset(l,0,sizeof(*l)); l->state.ambient[3]=1; l->state.revision=1; l->state.stamp=++next_stamp; l->dirty=1; }
    return l;
}
void athena_lights_destroy(AthenaLights *l) { free(l); }
int athena_lights_set_ambient(AthenaLights *l,float r,float g,float b) {
    if(!l||!rgb(r,g,b)) return 0;
    float value[4]={r,g,b,1};
    if(memcmp(value,l->state.ambient,sizeof(value))) {
        memcpy(l->state.ambient,value,sizeof(value)); touch(l);
    }
    return 1;
}
int athena_lights_set_directional(AthenaLights *l,uint32_t slot,
    float x,float y,float z,float r,float g,float b) {
    if(!l||slot>=ATHENA_LIGHTS_MAX_DIRECTIONAL||!rgb(r,g,b)||
        !athena_float_isfinite(x)||!athena_float_isfinite(y)||!athena_float_isfinite(z)) return 0;
    /* Float, rescaled by the largest component: no software double. */
    float m=fabsf(x); if(fabsf(y)>m) m=fabsf(y); if(fabsf(z)>m) m=fabsf(z);
    if(m==0) return 0;
    float nx=x/m,ny=y/m,nz=z/m,length=sqrtf(nx*nx+ny*ny+nz*nz);
    float direction[4]={nx/length,ny/length,nz/length,0},diffuse[4]={r,g,b,0};
    if(!(l->enabled&(1u<<slot))||memcmp(direction,l->state.direction[slot],sizeof(direction))||
        memcmp(diffuse,l->state.diffuse[slot],sizeof(diffuse))) {
        memcpy(l->state.direction[slot],direction,sizeof(direction));
        memcpy(l->state.diffuse[slot],diffuse,sizeof(diffuse));
        l->enabled|=1u<<slot; touch(l);
    }
    return 1;
}
int athena_lights_disable(AthenaLights *l,uint32_t slot) {
    if(!l||slot>=ATHENA_LIGHTS_MAX_DIRECTIONAL) return 0;
    if(l->enabled&(1u<<slot)) { l->enabled&=~(1u<<slot); touch(l); }
    return 1;
}
int athena_lights_set_point(AthenaLights *l,uint32_t slot,
    float x,float y,float z,float r,float g,float b,float range) {
    if(!l||slot>=ATHENA_LIGHTS_MAX_POINT||!rgb(r,g,b)||!athena_float_isfinite(x)||!athena_float_isfinite(y)||
        !athena_float_isfinite(z)||!athena_float_isfinite(range)||!(range>0)) return 0;
    float inverse=1/(range*range);
    if(!athena_float_isfinite(inverse)||!(inverse>0)) return 0;
    float position[4]={x,y,z,inverse},color[4]={r,g,b,0};
    if(!(l->points&(1u<<slot))||memcmp(position,l->state.point_position[slot],sizeof(position))||
        memcmp(color,l->state.point_color[slot],sizeof(color))) {
        memcpy(l->state.point_position[slot],position,sizeof(position));
        memcpy(l->state.point_color[slot],color,sizeof(color));
        l->points|=1u<<slot; touch(l);
    }
    return 1;
}
int athena_lights_disable_point(AthenaLights *l,uint32_t slot) {
    if(!l||slot>=ATHENA_LIGHTS_MAX_POINT) return 0;
    if(l->points&(1u<<slot)) { l->points&=~(1u<<slot); touch(l); }
    return 1;
}
int athena_lights_set_fog(AthenaLights *l,float start,float end,float r,float g,float b) {
    if(!l||!rgb(r,g,b)||!athena_float_isfinite(start)||!athena_float_isfinite(end)||start<0||!(end>start)) return 0;
    AthenaLightsView *v=&l->state;
    if(!v->fog_enabled||v->fog_start!=start||v->fog_end!=end||v->fog_color[0]!=r||v->fog_color[1]!=g||v->fog_color[2]!=b) {
        v->fog_enabled=1; v->fog_start=start; v->fog_end=end;
        v->fog_color[0]=r; v->fog_color[1]=g; v->fog_color[2]=b;
        touch(l);
    }
    return 1;
}
void athena_lights_disable_fog(AthenaLights *l) {
    if(l&&l->state.fog_enabled) { l->state.fog_enabled=0; touch(l); }
}
void athena_lights_clear(AthenaLights *l) {
    if(!l) return;
    if(l->enabled||l->points||l->state.ambient[0]!=0||l->state.ambient[1]!=0||l->state.ambient[2]!=0) {
        l->enabled=0; l->points=0; l->state.ambient[0]=l->state.ambient[1]=l->state.ambient[2]=0;
        touch(l);
    }
}
void athena_lights_view(const AthenaLights *l,AthenaLightsView *out) {
    if(!l) { memset(out,0,sizeof(*out)); out->ambient[3]=1; return; }
    *out=*athena_lights_peek(l);
}
const AthenaLightsView *athena_lights_peek(const AthenaLights *l) {
    if(!l) return NULL;
    /* Logical const: only a private derived cache changes. Main thread only.
     * Compact active slots once per effective update, not once per instance. */
    AthenaLights *cache=(AthenaLights *)l;
    if(cache->dirty) {
        AthenaLightsView *v=&cache->cached; memset(v,0,sizeof(*v));
        memcpy(v->ambient,l->state.ambient,sizeof(v->ambient)); v->revision=l->state.revision; v->stamp=l->state.stamp;
        v->fog_enabled=l->state.fog_enabled; v->fog_start=l->state.fog_start; v->fog_end=l->state.fog_end;
        memcpy(v->fog_color,l->state.fog_color,sizeof(v->fog_color));
        for(uint32_t slot=0;slot<ATHENA_LIGHTS_MAX_DIRECTIONAL;slot++) if(l->enabled&(1u<<slot)) {
            memcpy(v->direction[v->count],l->state.direction[slot],sizeof(v->direction[0]));
            memcpy(v->diffuse[v->count],l->state.diffuse[slot],sizeof(v->diffuse[0])); v->count++;
        }
        for(uint32_t slot=0;slot<ATHENA_LIGHTS_MAX_POINT;slot++) if(l->points&(1u<<slot)) {
            memcpy(v->point_position[v->point_count],l->state.point_position[slot],sizeof(v->point_position[0]));
            memcpy(v->point_color[v->point_count],l->state.point_color[slot],sizeof(v->point_color[0])); v->point_count++;
        }
        cache->dirty=0;
    }
    return &cache->cached;
}
