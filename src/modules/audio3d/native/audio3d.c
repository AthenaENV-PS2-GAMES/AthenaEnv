#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <athena/audio3d.h>
#include <athena/float_bits.h>
void athena_audio3d_default_params(AthenaAudio3DParams *p) {
    p->min_distance=1; p->max_distance=30; p->rolloff=ATHENA_AUDIO3D_INVERSE; p->volume=100; p->pan_strength=.8f;
    p->priority=0; p->cull=ATHENA_AUDIO3D_CULL_DEFAULT;
}
int athena_audio3d_validate(const AthenaAudio3DParams *p) {
    return p&&athena_float_isfinite(p->min_distance)&&athena_float_isfinite(p->max_distance)&&p->min_distance>0&&
        p->max_distance>p->min_distance&&(p->rolloff==ATHENA_AUDIO3D_INVERSE||p->rolloff==ATHENA_AUDIO3D_LINEAR)&&
        athena_float_isfinite(p->volume)&&p->volume>=0&&p->volume<=100&&athena_float_isfinite(p->pan_strength)&&
        p->pan_strength>=0&&p->pan_strength<=1&&p->priority>=-1000&&p->priority<=1000&&
        p->cull>=ATHENA_AUDIO3D_CULL_DEFAULT&&p->cull<=ATHENA_AUDIO3D_CULL_VIRTUAL;
}
int athena_audio3d_listener_from_camera(AthenaCamera3D *c,AthenaAudio3DListener *out) {
    if(!c||!out) return -1;
    const float *v=c->view.value;
    out->position[0]=c->position.x; out->position[1]=c->position.y; out->position[2]=c->position.z;
    out->right[0]=v[0]; out->right[1]=v[4]; out->right[2]=v[8];
    return 0;
}
float athena_audio3d_gain(const AthenaAudio3DParams *p,float d) {
    if(d<=p->min_distance) return 1;
    if(d>=p->max_distance) return 0;
    if(p->rolloff==ATHENA_AUDIO3D_LINEAR) return (p->max_distance-d)/(p->max_distance-p->min_distance);
    float g=p->min_distance/d,fade_start=p->max_distance*.9f;
    if(d>fade_start) g*=(p->max_distance-d)/(p->max_distance-fade_start);
    return g;
}
float athena_audio3d_levels(const AthenaAudio3DListener *l,const float s[3],const AthenaAudio3DParams *p,int *volume,int *pan) {
    float dx=s[0]-l->position[0],dy=s[1]-l->position[1],dz=s[2]-l->position[2],d=sqrtf(dx*dx+dy*dy+dz*dz);
    float g=athena_audio3d_gain(p,d),v=g*p->volume;
    float side=d>1e-4f?(dx*l->right[0]+dy*l->right[1]+dz*l->right[2])/d:0;
    float pn=side*100*p->pan_strength;
    if(!athena_float_isfinite(v)) v=0;
    if(!athena_float_isfinite(pn)) pn=0;
    int iv=(int)(v+.5f),ip=(int)(pn<0?pn-.5f:pn+.5f);
    *volume=iv<0?0:iv>100?100:iv;
    *pan=ip<-100?-100:ip>100?100:ip;
    return d;
}

/* ---- Voices and occlusion ---- */
#define START_MARGIN .98f   /* a culled source restarts only a little inside its max distance: no thrash at the edge */
struct AthenaAudio3DSource {
    AthenaAudio3DResolve resolve; void *user;
    AthenaAudio3DParams params;
    float position[3]; AthenaNode3D *node;
    int channel; uint32_t token;
    uint8_t desired,force;
    int volume,pan;
    float distance,gain,loudness,occ,occ_target;
    uint32_t occ_age,id;
    AthenaSfx *sfx;   /* resolved for the current update */
    AthenaAudio3DSource *prev,*next;
};
static AthenaAudio3DSource *sources,*voices[ATHENA_AUDIO3D_MAX_VOICES];
static uint32_t voice_count,max_voices=ATHENA_AUDIO3D_MAX_VOICES,next_id=1;
static AthenaAudio3DListener the_listener={{0,0,0},{1,0,0}};
static struct { AthenaAudio3DOcclusionFn fn; void *user; AthenaAudio3DOcclusionOptions options; } occlusion;

void athena_audio3d_set_listener(const AthenaAudio3DListener *l) { if(l) the_listener=*l; }
void athena_audio3d_get_listener(AthenaAudio3DListener *out) { if(out) *out=the_listener; }
int athena_audio3d_set_max_voices(uint32_t count) {
    if(count<1||count>ATHENA_AUDIO3D_MAX_VOICES) return -1;
    max_voices=count; return 0;
}
uint32_t athena_audio3d_max_voices(void) { return max_voices; }
void athena_audio3d_occlusion_defaults(AthenaAudio3DOcclusionOptions *o) {
    if(o) *o=(AthenaAudio3DOcclusionOptions){4,6,.8f,.25f};
}
int athena_audio3d_set_occlusion(AthenaAudio3DOcclusionFn fn,void *user,const AthenaAudio3DOcclusionOptions *options) {
    AthenaAudio3DOcclusionOptions o; if(options) o=*options; else athena_audio3d_occlusion_defaults(&o);
    if(fn&&(o.max_queries<1||o.max_queries>256||o.interval<1||o.interval>3600||!athena_float_isfinite(o.strength)||o.strength<0||o.strength>1||
        !athena_float_isfinite(o.smoothing)||o.smoothing<=0||o.smoothing>1)) return -1;
    occlusion.fn=fn; occlusion.user=fn?user:NULL; occlusion.options=o;
    if(!fn) for(AthenaAudio3DSource *s=sources;s;s=s->next) s->occ=s->occ_target=0;
    return 0;
}
static const float *source_xyz(const AthenaAudio3DSource *s,float out[3]) {
    if(s->node) athena_node3d_last_world_position(s->node,out); else memcpy(out,s->position,sizeof(s->position));
    return out;
}
static AthenaAudio3DCull effective_cull(const AthenaAudio3DSource *s) {
    if(s->params.cull!=ATHENA_AUDIO3D_CULL_DEFAULT) return s->params.cull;
    return s->sfx&&athena_sfx_get_loop(s->sfx)?ATHENA_AUDIO3D_CULL_VIRTUAL:ATHENA_AUDIO3D_CULL_STOP;
}
AthenaAudio3DSource *athena_audio3d_source_create(AthenaAudio3DResolve resolve,void *user,const AthenaAudio3DParams *params) {
    if(!resolve||!athena_audio3d_validate(params)) return NULL;
    AthenaAudio3DSource *s=calloc(1,sizeof(*s)); if(!s) return NULL;
    s->resolve=resolve; s->user=user; s->params=*params; s->channel=-1; s->id=next_id++;
    s->next=sources; if(sources) sources->prev=s; sources=s;   /* newest first: ids decide ties, not list order */
    return s;
}
static void voice_remove(AthenaAudio3DSource *s) {
    for(uint32_t i=0;i<voice_count;i++) if(voices[i]==s) { voices[i]=voices[--voice_count]; break; }
    s->channel=-1; s->token=0;
}
/* Gives the voice back; with stop set silences it, but only if it is still the one this source started. */
static void voice_release(AthenaAudio3DSource *s,AthenaSfx *sfx,int stop) {
    if(s->channel<0) return;
    if(stop&&sfx&&athena_sfx_voice_token(sfx,s->channel)==s->token) athena_sfx_stop(sfx,s->channel);
    voice_remove(s);
}
void athena_audio3d_source_destroy(AthenaAudio3DSource *s) {
    if(!s) return;
    voice_release(s,s->resolve(s->user),1);
    if(s->prev) s->prev->next=s->next; else sources=s->next;
    if(s->next) s->next->prev=s->prev;
    athena_node3d_release(s->node); free(s);
}
int athena_audio3d_source_configure(AthenaAudio3DSource *s,const AthenaAudio3DParams *p) {
    if(!s||!athena_audio3d_validate(p)) return -1;
    s->params=*p; return 0;
}
void athena_audio3d_source_params(const AthenaAudio3DSource *s,AthenaAudio3DParams *out) { if(s&&out) *out=s->params; }
void athena_audio3d_source_set_position(AthenaAudio3DSource *s,const float p[3]) { if(s&&p) memcpy(s->position,p,sizeof(s->position)); }
void athena_audio3d_source_position(const AthenaAudio3DSource *s,float out[3]) { if(s&&out) source_xyz(s,out); }
void athena_audio3d_source_set_node(AthenaAudio3DSource *s,AthenaNode3D *n) {
    if(!s) return;
    athena_node3d_retain(n); athena_node3d_release(s->node); s->node=n;
}
int athena_audio3d_source_channel(const AthenaAudio3DSource *s) { return s?s->channel:-1; }
int athena_audio3d_source_pending(const AthenaAudio3DSource *s) { return s&&s->desired&&s->channel<0; }
int athena_audio3d_source_volume(const AthenaAudio3DSource *s) { return s?s->volume:0; }
int athena_audio3d_source_pan(const AthenaAudio3DSource *s) { return s?s->pan:0; }
float athena_audio3d_source_occlusion(const AthenaAudio3DSource *s) { return s?s->occ:0; }

static void refresh_levels(AthenaAudio3DSource *s) {
    float p[3]; source_xyz(s,p);
    int v,pn;
    s->distance=athena_audio3d_levels(&the_listener,p,&s->params,&v,&pn);
    s->gain=athena_audio3d_gain(&s->params,s->distance);
    float k=1-s->occ*occlusion.options.strength; if(!occlusion.fn||k<0) k=occlusion.fn?0:1;
    s->loudness=s->gain*s->params.volume*k;
    int iv=(int)(s->loudness+.5f); s->volume=iv<0?0:iv>100?100:iv; s->pan=pn;
}
/* Rank: priority, then loudness, then nearer, then older. */
static int better(const AthenaAudio3DSource *a,const AthenaAudio3DSource *b) {
    if(a->params.priority!=b->params.priority) return a->params.priority>b->params.priority;
    if(a->loudness!=b->loudness) return a->loudness>b->loudness;
    if(a->distance!=b->distance) return a->distance<b->distance;
    return a->id<b->id;
}
/* A source that cannot (or may no longer) sound: its policy decides if it waits. */
static void lose(AthenaAudio3DSource *s) {
    if(effective_cull(s)==ATHENA_AUDIO3D_CULL_STOP) s->desired=0;
}
/* Starts s on a free voice at its current levels: the channel, -1, or a Sound error. */
static int voice_start(AthenaAudio3DSource *s) {
    AthenaSfx *sfx=s->sfx;
    /* The sample's own volume/pan are its defaults for other plays: restore them. */
    int saved_volume=athena_sfx_get_volume(sfx),saved_pan=athena_sfx_get_pan(sfx);
    athena_sfx_set_volume(sfx,s->volume); athena_sfx_set_pan(sfx,s->pan);
    int channel=athena_sfx_play(sfx,-1);
    athena_sfx_set_volume(sfx,saved_volume); athena_sfx_set_pan(sfx,saved_pan);
    if(channel<0) return channel;
    s->channel=channel; s->token=athena_sfx_voice_token(sfx,channel);
    s->occ_age=0xffffffffu;   /* queried first */
    if(voice_count<ATHENA_AUDIO3D_MAX_VOICES) voices[voice_count++]=s;
    return channel;
}
int athena_audio3d_source_play(AthenaAudio3DSource *s,int force) {
    if(!s) return ATHENA_SOUND_ERR_ARGS;
    s->sfx=s->resolve(s->user); if(!s->sfx) return ATHENA_SOUND_ERR_ARGS;
    voice_release(s,s->sfx,1); s->desired=0; s->force=force!=0;
    refresh_levels(s);
    if(!s->force&&!(s->gain>0&&s->distance<s->params.max_distance)) return -1;
    s->desired=1;
    if(voice_count>=max_voices) {
        AthenaAudio3DSource *worst=NULL;
        for(uint32_t i=0;i<voice_count;i++) if(!worst||better(worst,voices[i])) worst=voices[i];
        if(!worst||!better(s,worst)) { lose(s); return -1; }
        AthenaSfx *ws=worst->resolve(worst->user);
        voice_release(worst,ws,1); worst->sfx=ws; lose(worst);
    }
    int r=voice_start(s);
    if(r>=0) return r;
    if(r==-1) { lose(s); return -1; }
    s->desired=0; return r;
}
void athena_audio3d_source_stop(AthenaAudio3DSource *s) {
    if(!s) return;
    voice_release(s,s->resolve(s->user),1); s->desired=0;
}
void athena_audio3d_update(AthenaAudio3DStats *stats) {
    AthenaAudio3DStats ignored; if(!stats) stats=&ignored;
    memset(stats,0,sizeof(*stats));
    AthenaAudio3DSource *top[ATHENA_AUDIO3D_MAX_VOICES]; uint32_t n=0;
    for(AthenaAudio3DSource *s=sources;s;s=s->next) {
        if(!s->desired) continue;
        s->sfx=s->resolve(s->user);
        if(!s->sfx) { voice_remove(s); s->desired=0; continue; }   /* freed: Sound already silenced the voice */
        if(s->channel>=0&&athena_sfx_voice_token(s->sfx,s->channel)!=s->token) {
            voice_remove(s);   /* ended, stopped elsewhere or replaced by another sample */
            if(!athena_sfx_get_loop(s->sfx)) { s->desired=0; continue; }
        }
        stats->sources++;
        refresh_levels(s);
        int audible=s->channel>=0?(s->force||s->gain>0):(s->force||(s->gain>0&&s->distance<s->params.max_distance*START_MARGIN));
        if(!audible) { stats->culled++; voice_release(s,s->sfx,1); lose(s); continue; }
        stats->candidates++;
        AthenaAudio3DSource *loser=s;
        if(n<max_voices) { uint32_t i=n++; while(i&&better(s,top[i-1])) { top[i]=top[i-1]; i--; } top[i]=s; loser=NULL; }
        else if(better(s,top[n-1])) { loser=top[n-1]; uint32_t i=n-1; while(i&&better(s,top[i-1])) { top[i]=top[i-1]; i--; } top[i]=s; }
        if(loser) { stats->culled++; if(loser->channel>=0) { stats->stolen++; voice_release(loser,loser->sfx,1); } lose(loser); }
    }
    /* Voices of sources that dropped out of the top (lowered max_voices) were released above; hold the rest. */
    for(uint32_t i=0;i<n;i++) {
        AthenaAudio3DSource *s=top[i];
        if(s->channel>=0) continue;
        int r=voice_start(s);
        if(r>=0) stats->started++;
        else { stats->culled++; if(r<-1) s->desired=0; else lose(s); }
    }
    /* Occlusion: the stalest answers of the voiced sources, a bounded number per update. */
    if(occlusion.fn) {
        AthenaAudio3DSource *due[256]; uint32_t nd=0,limit=occlusion.options.max_queries;
        for(uint32_t i=0;i<n;i++) {
            AthenaAudio3DSource *s=top[i];
            if(s->channel<0||!s->desired) continue;
            if(s->occ_age!=0xffffffffu) s->occ_age++;
            if(s->occ_age>stats->oldest_result&&s->occ_age!=0xffffffffu) stats->oldest_result=s->occ_age;
            if(s->occ_age<occlusion.options.interval&&s->occ_age!=0xffffffffu) continue;
            uint32_t j=nd<limit?nd++:limit; /* insert by age, oldest first, capped at limit */
            if(j==limit) { if(s->occ_age<=due[limit-1]->occ_age) continue; j=limit-1; }
            while(j&&s->occ_age>due[j-1]->occ_age) { due[j]=due[j-1]; j--; }
            due[j]=s;
        }
        for(uint32_t i=0;i<nd;i++) {
            AthenaAudio3DSource *s=due[i]; float to[3],blocked=0;
            source_xyz(s,to);
            int r=occlusion.fn(occlusion.user,the_listener.position,to,&blocked);
            stats->queries++; s->occ_age=0;
            if(r<0||!athena_float_isfinite(blocked)) { stats->query_errors++; continue; }
            s->occ_target=blocked<0?0:blocked>1?1:blocked;
        }
        for(uint32_t i=0;i<n;i++) { AthenaAudio3DSource *s=top[i]; if(s->channel>=0) s->occ+=(s->occ_target-s->occ)*occlusion.options.smoothing; }
    }
    /* Levels of every voice held. */
    for(uint32_t i=0;i<n;i++) {
        AthenaAudio3DSource *s=top[i];
        if(s->channel<0) continue;
        refresh_levels(s);
        if(athena_sfx_set_channel_levels(s->sfx,s->channel,s->volume,s->pan)<=0) {
            voice_remove(s); if(!athena_sfx_get_loop(s->sfx)) s->desired=0;
        }
    }
    stats->voices=voice_count;
}
/* Sources belong to their JS handles (finalizers free them): only the global state is reset. */
void athena_audio3d_module_shutdown(void) {
    for(AthenaAudio3DSource *s=sources;s;s=s->next) { s->channel=-1; s->token=0; s->desired=0; }
    voice_count=0; max_voices=ATHENA_AUDIO3D_MAX_VOICES; occlusion.fn=NULL; occlusion.user=NULL;
    the_listener=(AthenaAudio3DListener){{0,0,0},{1,0,0}};
}
