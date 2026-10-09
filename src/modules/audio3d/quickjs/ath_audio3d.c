#include <math.h>
#include <string.h>
#include <athena_js_args.h>
#include <athena/audio3d.h>
#include <athena/loop.h>
#include <athena/sound.h>
#include <athena/js/sound.h>
#include <athena/js/camera3d.h>
#include <athena/js/scene3d.h>
#include "ath_audio3d.h"
/* A source keeps its Sound.Sfx object (marked for the GC) and looks the
 * native sample up again every frame, so Sfx.free() stops it safely. A
 * Loop POST_UPDATE system (priority 1000: after Scene3D updates) moves the
 * listener with its camera and applies each playing source's levels. */
typedef struct Source {
    JSValue sfx;
    AthenaAudio3DParams params;
    float position[3];
    AthenaNode3D *node; float offset[3];
    int channel;           /* playing voice, -1 when not playing */
    int volume,pan;        /* last levels */
    struct Source *prev,*next;
} Source;
static JSClassID source_id;
static Source *sources;
static AthenaAudio3DListener listener={{0,0,0},{1,0,0}};
static AthenaCamera3D *listener_camera;
static int loop_system;

static void unlink_source(Source *s) {
    if(s->prev) s->prev->next=s->next; else if(sources==s) sources=s->next;
    if(s->next) s->next->prev=s->prev;
    s->prev=s->next=NULL;
}
static void source_position(const Source *s,float out[3]) {
    if(s->node) { athena_node3d_last_world_position(s->node,out); for(int i=0;i<3;i++) out[i]+=s->offset[i]; }
    else memcpy(out,s->position,sizeof(s->position));
}
static void refresh_listener(void) {
    if(listener_camera) athena_audio3d_listener_from_camera(listener_camera,&listener);
}
/* Applies the levels of every playing source; forgets voices that ended. */
static void update_sources(void) {
    refresh_listener();
    for(Source *s=sources;s;s=s->next) {
        if(s->channel<0) continue;
        AthenaSfx *sfx=athena_sfx_js_peek(s->sfx);
        if(!sfx) { s->channel=-1; continue; }
        float p[3]; source_position(s,p);
        athena_audio3d_levels(&listener,p,&s->params,&s->volume,&s->pan);
        if(athena_sfx_set_channel_levels(sfx,s->channel,s->volume,s->pan)<=0) s->channel=-1;
    }
}
static int loop_func(void *opaque,AthenaLoopPhase phase,float value) { (void)opaque; (void)phase; (void)value; update_sources(); return 0; }
static void loop_release(void *opaque) { (void)opaque; loop_system=0; }
static int ensure_system(JSContext *ctx) {
    if(loop_system) return 1;
    AthenaLoopSystemDesc desc={.name="audio3d",.priority=1000,.phases=ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_POST_UPDATE),
        .func=loop_func,.release=loop_release};
    int id=athena_loop_system_add(&desc);
    if(id<0) { JS_ThrowInternalError(ctx,"Audio3D: cannot add the Loop system"); return 0; }
    loop_system=id; return 1;
}
static Source *get_source(JSContext *ctx,JSValueConst v) {
    Source *s=JS_GetOpaque2(ctx,v,source_id); if(!s) JS_ThrowTypeError(ctx,"Expected a live Audio3D.Source"); return s;
}
static void source_free(JSRuntime *rt,Source *s) {
    if(!s) return;
    AthenaSfx *sfx=athena_sfx_js_peek(s->sfx);
    if(sfx&&s->channel>=0) athena_sfx_stop(sfx,s->channel);
    unlink_source(s); athena_node3d_release(s->node);
    JS_FreeValueRT(rt,s->sfx); js_free_rt(rt,s);
}
static void finalizer(JSRuntime *rt,JSValue v) { source_free(rt,JS_GetOpaque(v,source_id)); }
static void gc_mark(JSRuntime *rt,JSValueConst v,JS_MarkFunc *mark) {
    Source *s=JS_GetOpaque(v,source_id); if(s) JS_MarkValue(rt,s->sfx,mark);
}
static int read_options(JSContext *ctx,JSValueConst o,Source *s) {
    if(JS_IsUndefined(o)) return 1;
    if(!JS_IsObject(o)) { JS_ThrowTypeError(ctx,"Audio3D source options must be an object"); return 0; }
    AthenaAudio3DParams p=s->params; float pos[3]={s->position[0],s->position[1],s->position[2]};
    if(!athena_js_option_float(ctx,o,"minDistance",&p.min_distance)||!athena_js_option_float(ctx,o,"maxDistance",&p.max_distance)||
        !athena_js_option_float(ctx,o,"volume",&p.volume)||!athena_js_option_float(ctx,o,"panStrength",&p.pan_strength)||
        !athena_js_option_float(ctx,o,"x",&pos[0])||!athena_js_option_float(ctx,o,"y",&pos[1])||!athena_js_option_float(ctx,o,"z",&pos[2])) return 0;
    JSValue r=JS_GetPropertyStr(ctx,o,"rolloff"); if(JS_IsException(r)) return 0;
    if(!JS_IsUndefined(r)) {
        const char *name=JS_ToCString(ctx,r); JS_FreeValue(ctx,r); if(!name) return 0;
        int lin=!strcmp(name,"linear"),inv=!strcmp(name,"inverse"); JS_FreeCString(ctx,name);
        if(!lin&&!inv) { JS_ThrowRangeError(ctx,"rolloff must be \"inverse\" or \"linear\""); return 0; }
        p.rolloff=lin?ATHENA_AUDIO3D_LINEAR:ATHENA_AUDIO3D_INVERSE;
    } else JS_FreeValue(ctx,r);
    if(!athena_audio3d_validate(&p)) { JS_ThrowRangeError(ctx,"Audio3D: 0 < minDistance < maxDistance, volume 0-100, panStrength 0-1"); return 0; }
    JSValue node=JS_GetPropertyStr(ctx,o,"node"); if(JS_IsException(node)) return 0;
    if(!JS_IsUndefined(node)) {
        AthenaNode3D *n=NULL;
        if(!JS_IsNull(node)&&!(n=athena_node3d_from_value(ctx,node))) { JS_FreeValue(ctx,node); return 0; }
        athena_node3d_retain(n); athena_node3d_release(s->node); s->node=n;
    }
    JS_FreeValue(ctx,node);
    s->params=p; memcpy(s->position,pos,sizeof(pos));
    return 1;
}
/* new Audio3D.Source(sfx, { x, y, z, node, minDistance, maxDistance, rolloff, volume, panStrength }) */
static JSValue ctor(JSContext *ctx,JSValueConst target,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,2,"Audio3D.Source")) return JS_EXCEPTION;
    if(!athena_sfx_js_peek(argv[0])) return JS_ThrowTypeError(ctx,"Audio3D.Source expects a live Sound.Sfx");
    Source *s=js_mallocz(ctx,sizeof(*s)); if(!s) return JS_EXCEPTION;
    s->sfx=JS_UNDEFINED; s->channel=-1; athena_audio3d_default_params(&s->params);
    if(!read_options(ctx,argc==2?argv[1]:JS_UNDEFINED,s)) { athena_node3d_release(s->node); js_free(ctx,s); return JS_EXCEPTION; }
    JSValue proto=JS_GetPropertyStr(ctx,target,"prototype");
    JSValue obj=JS_IsException(proto)?proto:JS_NewObjectProtoClass(ctx,proto,source_id); JS_FreeValue(ctx,proto);
    if(JS_IsException(obj)) { athena_node3d_release(s->node); js_free(ctx,s); return obj; }
    s->sfx=JS_DupValue(ctx,argv[0]);
    s->next=sources; if(sources) sources->prev=s; sources=s;
    JS_SetOpaque(obj,s);
    if(!ensure_system(ctx)) { JS_FreeValue(ctx,obj); return JS_EXCEPTION; }
    return obj;
}
static JSValue configure(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"Source.configure")) return JS_EXCEPTION;
    Source *s=get_source(ctx,self); if(!s) return JS_EXCEPTION;
    /* Options can invoke arbitrary getters, including self.dispose().
     * Parse into a retained snapshot and reacquire the handle to commit. */
    Source pending={0}; pending.params=s->params;
    memcpy(pending.position,s->position,sizeof(pending.position));
    pending.node=s->node; athena_node3d_retain(pending.node);
    if(!read_options(ctx,argv[0],&pending)) { athena_node3d_release(pending.node); return JS_EXCEPTION; }
    s=get_source(ctx,self);
    if(!s) { athena_node3d_release(pending.node); return JS_EXCEPTION; }
    s->params=pending.params;
    memcpy(s->position,pending.position,sizeof(s->position));
    athena_node3d_release(s->node); s->node=pending.node;
    return JS_DupValue(ctx,self);
}
static JSValue set_position(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,3,3,"Source.setPosition")) return JS_EXCEPTION;
    float p[3]; for(int i=0;i<3;i++) if(!athena_js_float(ctx,argv[i],&p[i],"position")) return JS_EXCEPTION;
    Source *s=get_source(ctx,self); if(!s) return JS_EXCEPTION;
    memcpy(s->position,p,sizeof(p)); return JS_DupValue(ctx,self);
}
/* play({ force }): starts the sample at the source's current levels; returns
 * the channel, or -1 when out of range (unless force) or no channel is free. */
static JSValue play(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,0,1,"Source.play")) return JS_EXCEPTION;
    int force=0;
    if(argc==1&&JS_IsObject(argv[0])) {
        JSValue f=JS_GetPropertyStr(ctx,argv[0],"force"); if(JS_IsException(f)) return f;
        force=JS_ToBool(ctx,f); JS_FreeValue(ctx,f);
    }
    Source *s=get_source(ctx,self); if(!s) return JS_EXCEPTION;
    AthenaSfx *sfx=athena_sfx_js_peek(s->sfx); if(!sfx) return JS_ThrowTypeError(ctx,"Audio3D.Source: its Sfx was freed");
    refresh_listener();
    float p[3]; source_position(s,p);
    float d=athena_audio3d_levels(&listener,p,&s->params,&s->volume,&s->pan);
    if(!force&&d>=s->params.max_distance) return JS_NewInt32(ctx,-1);
    if(s->channel>=0) athena_sfx_stop(sfx,s->channel);
    /* The sample's own volume/pan are its defaults for other plays: restore them. */
    int saved_volume=athena_sfx_get_volume(sfx),saved_pan=athena_sfx_get_pan(sfx);
    athena_sfx_set_volume(sfx,s->volume); athena_sfx_set_pan(sfx,s->pan);
    int channel=athena_sfx_play(sfx,-1);
    athena_sfx_set_volume(sfx,saved_volume); athena_sfx_set_pan(sfx,saved_pan);
    if(channel<-1) return JS_ThrowInternalError(ctx,"Audio3D: %s",athena_sound_result_string(channel));
    s->channel=channel;
    return JS_NewInt32(ctx,channel);
}
static JSValue stop(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argc; (void)argv;
    Source *s=get_source(ctx,self); if(!s) return JS_EXCEPTION;
    AthenaSfx *sfx=athena_sfx_js_peek(s->sfx);
    if(sfx&&s->channel>=0) athena_sfx_stop(sfx,s->channel);
    s->channel=-1; return JS_DupValue(ctx,self);
}
static JSValue dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argc; (void)argv;
    if(!athena_js_class(ctx,self,source_id)) return JS_EXCEPTION;
    Source *s=JS_GetOpaque(self,source_id); JS_SetOpaque(self,NULL); source_free(JS_GetRuntime(ctx),s); return JS_UNDEFINED;
}
static JSValue source_prop(JSContext *ctx,JSValueConst self,int magic) {
    Source *s=get_source(ctx,self); if(!s) return JS_EXCEPTION;
    switch(magic) {
        case 0: return JS_NewBool(ctx,s->channel>=0);
        case 1: return JS_NewInt32(ctx,s->channel);
        case 2: return JS_NewInt32(ctx,s->volume);
        default: return JS_NewInt32(ctx,s->pan);
    }
}
/* setListener(camera | null) or setListener(x, y, z, rightX, rightY, rightZ). */
static JSValue set_listener(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(argc==1) {
        AthenaCamera3D *c=NULL;
        if(!JS_IsNull(argv[0])&&!(c=athena_camera3d_from_value(ctx,argv[0]))) return JS_EXCEPTION;
        if(c) athena_camera3d_js_retain(c);
        if(listener_camera) athena_camera3d_js_release(listener_camera);
        listener_camera=c; refresh_listener();
        return JS_UNDEFINED;
    }
    if(!athena_js_argc(ctx,argc,6,6,"Audio3D.setListener")) return JS_EXCEPTION;
    float f[6]; for(int i=0;i<6;i++) if(!athena_js_float(ctx,argv[i],&f[i],"listener")) return JS_EXCEPTION;
    float len=f[3]*f[3]+f[4]*f[4]+f[5]*f[5];
    if(!(len>1e-12f)) return JS_ThrowRangeError(ctx,"the right vector must not be zero");
    if(listener_camera) { athena_camera3d_js_release(listener_camera); listener_camera=NULL; }
    len=sqrtf(len);
    for(int i=0;i<3;i++) { listener.position[i]=f[i]; listener.right[i]=f[3+i]/len; }
    return JS_UNDEFINED;
}
/* levels(x, y, z, options?): { volume, pan, distance } for a source there, without playing. */
static JSValue levels(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,3,4,"Audio3D.levels")) return JS_EXCEPTION;
    float p[3]; for(int i=0;i<3;i++) if(!athena_js_float(ctx,argv[i],&p[i],"position")) return JS_EXCEPTION;
    Source tmp; memset(&tmp,0,sizeof(tmp)); athena_audio3d_default_params(&tmp.params);
    if(argc==4&&!read_options(ctx,argv[3],&tmp)) { athena_node3d_release(tmp.node); return JS_EXCEPTION; }
    athena_node3d_release(tmp.node);
    refresh_listener();
    int v,pn; float d=athena_audio3d_levels(&listener,p,&tmp.params,&v,&pn);
    JSValue obj=JS_NewObject(ctx);
    JS_SetPropertyStr(ctx,obj,"volume",JS_NewInt32(ctx,v)); JS_SetPropertyStr(ctx,obj,"pan",JS_NewInt32(ctx,pn));
    JS_SetPropertyStr(ctx,obj,"distance",JS_NewFloat64(ctx,d));
    return obj;
}
/* update(): what the Loop system does each frame, for games without Loop.run(). */
static JSValue update(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)ctx; (void)self; (void)argc; (void)argv; update_sources(); return JS_UNDEFINED;
}
static JSClassDef class_def={"Audio3D.Source",.finalizer=finalizer,.gc_mark=gc_mark};
static const JSCFunctionListEntry methods[]={
    JS_CFUNC_DEF("play",0,play),JS_CFUNC_DEF("stop",0,stop),JS_CFUNC_DEF("setPosition",3,set_position),
    JS_CFUNC_DEF("configure",1,configure),JS_CFUNC_DEF("dispose",0,dispose),
    JS_CGETSET_MAGIC_DEF("playing",source_prop,NULL,0),JS_CGETSET_MAGIC_DEF("channel",source_prop,NULL,1),
    JS_CGETSET_MAGIC_DEF("volume",source_prop,NULL,2),JS_CGETSET_MAGIC_DEF("pan",source_prop,NULL,3)};
static const JSCFunctionListEntry exports[]={
    JS_CFUNC_DEF("setListener",1,set_listener),JS_CFUNC_DEF("levels",3,levels),JS_CFUNC_DEF("update",0,update)};
static int init(JSContext *ctx,JSModuleDef *m) {
    if(athena_register_class(ctx,&source_id,&class_def)<0) return -1;
    JSValue proto=JS_NewObject(ctx); if(JS_IsException(proto)) return -1;
    JS_SetPropertyFunctionList(ctx,proto,methods,countof(methods));
    JSValue cls=JS_NewCFunction2(ctx,ctor,"Source",1,JS_CFUNC_constructor,0);
    JS_SetConstructor(ctx,cls,proto); JS_SetClassProto(ctx,source_id,proto);
    if(JS_SetModuleExport(ctx,m,"Source",cls)<0) return -1;
    return JS_SetModuleExportList(ctx,m,exports,countof(exports));
}
void athena_audio3d_js_cleanup(JSContext *ctx) {
    (void)ctx;
    if(loop_system) athena_loop_system_remove(loop_system);
    if(listener_camera) { athena_camera3d_js_release(listener_camera); listener_camera=NULL; }
    listener=(AthenaAudio3DListener){{0,0,0},{1,0,0}};
}
JSModuleDef *athena_audio3d_js_init(JSContext *ctx) {
    JSModuleDef *m=athena_push_module(ctx,init,exports,countof(exports),"Audio3D");
    if(m) JS_AddModuleExport(ctx,m,"Source");
    return m;
}
