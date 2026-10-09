#include <math.h>
#include <string.h>
#include <athena/sky.h>
#include <athena/float_bits.h>
typedef struct { float hour,zenith[3],horizon[3],ground[3],sun[3],ambient[3],light[3]; } Key;
/* Built-in day: night, dawn, morning, noon, afternoon, dusk, night. */
static const Key KEYS[]={
    {0.0f, {.02f,.03f,.08f},{.07f,.09f,.16f},{.03f,.03f,.04f},{0,0,0},     {.10f,.11f,.16f},{.12f,.14f,.24f}},
    {4.5f, {.02f,.03f,.08f},{.07f,.09f,.16f},{.03f,.03f,.04f},{0,0,0},     {.10f,.11f,.16f},{.12f,.14f,.24f}},
    {6.0f, {.25f,.30f,.50f},{.95f,.55f,.35f},{.20f,.15f,.12f},{1,.60f,.35f},{.35f,.30f,.30f},{.80f,.50f,.35f}},
    {8.0f, {.30f,.50f,.85f},{.70f,.80f,.95f},{.30f,.28f,.25f},{1,.95f,.85f},{.45f,.45f,.50f},{.90f,.88f,.80f}},
    {12.0f,{.25f,.45f,.85f},{.65f,.78f,.95f},{.32f,.30f,.27f},{1,1,.95f},   {.50f,.50f,.55f},{1,1,.95f}},
    {16.0f,{.30f,.50f,.85f},{.70f,.80f,.95f},{.30f,.28f,.25f},{1,.95f,.85f},{.45f,.45f,.50f},{.90f,.88f,.80f}},
    {18.0f,{.25f,.28f,.48f},{.95f,.50f,.30f},{.20f,.15f,.12f},{1,.55f,.30f},{.35f,.28f,.28f},{.80f,.45f,.30f}},
    {19.5f,{.02f,.03f,.08f},{.07f,.09f,.16f},{.03f,.03f,.04f},{0,0,0},     {.10f,.11f,.16f},{.12f,.14f,.24f}},
    {24.0f,{.02f,.03f,.08f},{.07f,.09f,.16f},{.03f,.03f,.04f},{0,0,0},     {.10f,.11f,.16f},{.12f,.14f,.24f}}};
static AthenaSkyState state;
static int initialized;
static void init_state(void) {
    if(initialized) return;
    initialized=1;
    athena_sky_set_time(12);
    state.sun_size=18;
}
void athena_sky_get(AthenaSkyState *out) { init_state(); *out=state; }
static int color_ok(const float c[3]) {
    for(int i=0;i<3;i++) if(!athena_float_isfinite(c[i])||c[i]<0||c[i]>1) return 0;
    return 1;
}
int athena_sky_set_colors(const float z[3],const float h[3],const float g[3]) {
    init_state();
    if(!z||!h||!g||!color_ok(z)||!color_ok(h)||!color_ok(g)) return -1;
    memcpy(state.zenith,z,12); memcpy(state.horizon,h,12); memcpy(state.ground,g,12); return 0;
}
static int normalize(float v[3]) {
    float l=sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    if(!(l>1e-6f)||!athena_float_isfinite(l)) return 0;
    v[0]/=l; v[1]/=l; v[2]/=l; return 1;
}
int athena_sky_set_sun(const float d[3],const float c[3],float size) {
    init_state();
    float dir[3]={d?d[0]:0,d?d[1]:0,d?d[2]:0};
    if(!d||!normalize(dir)||(c&&!color_ok(c))||!athena_float_isfinite(size)||size<0||size>256) return -1;
    memcpy(state.sun_direction,dir,12); if(c) memcpy(state.sun_color,c,12); state.sun_size=size; return 0;
}
static void mix3(float out[3],const float a[3],const float b[3],float t) { for(int i=0;i<3;i++) out[i]=a[i]+(b[i]-a[i])*t; }
int athena_sky_set_time(float hours) {
    if(!athena_float_isfinite(hours)) return -1;
    /* setTime may be the first API called after boot or std.reload(). */
    if(!initialized) { initialized=1; state.sun_size=18; }
    hours=fmodf(hours,24); if(hours<0) hours+=24;
    unsigned k=0; while(k+2<sizeof(KEYS)/sizeof(KEYS[0])&&KEYS[k+1].hour<=hours) k++;
    const Key *a=&KEYS[k],*b=&KEYS[k+1];
    float t=(hours-a->hour)/(b->hour-a->hour);
    t=t*t*(3-2*t);   /* smoothstep: no kinks at the keys */
    mix3(state.zenith,a->zenith,b->zenith,t); mix3(state.horizon,a->horizon,b->horizon,t);
    mix3(state.ground,a->ground,b->ground,t); mix3(state.sun_color,a->sun,b->sun,t);
    mix3(state.ambient,a->ambient,b->ambient,t); mix3(state.light,a->light,b->light,t);
    /* The sun rises in +X at 6:00, culminates at 12:00 and sets in -X at
     * 18:00, tilted toward +Z; at night the light comes from the moon,
     * opposite. */
    float angle=(hours-6)/12*3.14159265f;
    float sun[3]={cosf(angle),sinf(angle),.3f}; normalize(sun);
    memcpy(state.sun_direction,sun,12);
    if(sun[1]>=-.05f) memcpy(state.light_direction,sun,12);
    else { state.light_direction[0]=-sun[0]; state.light_direction[1]=-sun[1]; state.light_direction[2]=sun[2]; normalize(state.light_direction); }
    state.time=hours;
    return 0;
}
void athena_sky_color_at(float e,float out[3]) {
    init_state();
    if(e>=0) {
        float t=sqrtf(e/1.5707963f); if(t>1) t=1;
        mix3(out,state.horizon,state.zenith,t);
    } else {
        float t=-e/.15f; if(t>1) t=1;   /* the ground takes over quickly below the horizon */
        mix3(out,state.horizon,state.ground,t);
    }
}
int athena_sky_apply(AthenaLights *l,int fog) {
    init_state();
    if(!l) return -1;
    athena_lights_set_ambient(l,state.ambient[0],state.ambient[1],state.ambient[2]);
    athena_lights_set_directional(l,0,state.light_direction[0],state.light_direction[1],state.light_direction[2],
        state.light[0],state.light[1],state.light[2]);
    if(fog) {
        AthenaLightsView v; athena_lights_view(l,&v);
        if(v.fog_enabled) athena_lights_set_fog(l,v.fog_start,v.fog_end,state.horizon[0],state.horizon[1],state.horizon[2]);
    }
    return 0;
}
/* Elevation of the camera ray through screen row y (at the centre column). */
static float row_elevation(AthenaCamera3D *c,float width,float height,float y) {
    float o[3],d[3];
    if(athena_camera3d_screen_to_ray(c,width*.5f,y,width,height,o,d)<0) return 0;
    float e=asinf(d[1]<-1?-1:d[1]>1?1:d[1]);
    return e;
}
int athena_sky_bands(AthenaCamera3D *c,float width,float height,AthenaSkyBand *out,uint32_t max) {
    init_state();
    if(!c||!out||!max||!athena_float_isfinite(width)||!athena_float_isfinite(height)||width<=0||height<=0) return -1;
    if(!athena_camera3d_update(c)) return -1;
    float prev=row_elevation(c,width,height,0);
    float top[3]; athena_sky_color_at(prev,top);
    for(uint32_t i=0;i<max;i++) {
        float y1=height*(float)(i+1)/(float)max,e=row_elevation(c,width,height,y1),bottom[3];
        athena_sky_color_at(e,bottom);
        out[i].y0=height*(float)i/(float)max; out[i].y1=y1;
        memcpy(out[i].top,top,12); memcpy(out[i].bottom,bottom,12); memcpy(top,bottom,12);
    }
    return (int)max;
}
int athena_sky_sun_screen(AthenaCamera3D *c,float width,float height,float out[2]) {
    init_state();
    if(!c||!out) return -1;
    float p[3]={c->position.x+state.sun_direction[0]*c->far_clip*.5f,c->position.y+state.sun_direction[1]*c->far_clip*.5f,
        c->position.z+state.sun_direction[2]*c->far_clip*.5f},s[3];
    int r=athena_camera3d_world_to_screen(c,p,width,height,s);
    if(r<=0) return r<0?-1:0;
    out[0]=s[0]; out[1]=s[1]; return 1;
}
void athena_sky_module_shutdown(void) { initialized=0; }
