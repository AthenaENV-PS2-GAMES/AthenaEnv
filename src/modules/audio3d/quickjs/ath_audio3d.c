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
/* A source keeps its Sound.Sfx object (marked for the GC) and the native source looks the sample up
 * again every update, so Sfx.free() ends it safely. The voice policy (priority, stealing, virtual
 * sources, occlusion) lives in native/audio3d.c; a Loop POST_UPDATE system (priority 1000: after
 * Scene3D updates) moves the listener with its camera and runs it. */
typedef struct { JSValue sfx; AthenaAudio3DSource *src; } Source;
static JSClassID source_id;
static AthenaCamera3D *listener_camera;
static int loop_system;
static AthenaAudio3DStats last_stats;

static void refresh_listener(void) {
    AthenaAudio3DListener l;
    if(listener_camera&&athena_audio3d_listener_from_camera(listener_camera,&l)==0) athena_audio3d_set_listener(&l);
}
static void run_update(void) { refresh_listener(); athena_audio3d_update(&last_stats); }
static int loop_func(void *opaque,AthenaLoopPhase phase,float value) { (void)opaque; (void)phase; (void)value; run_update(); return 0; }
static void loop_release(void *opaque) { (void)opaque; loop_system=0; }
static int ensure_system(JSContext *ctx) {
    if(loop_system) return 1;
    AthenaLoopSystemDesc desc={.name="audio3d",.priority=1000,.phases=ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_POST_UPDATE),
        .func=loop_func,.release=loop_release};
    int id=athena_loop_system_add(&desc);
    if(id<0) { JS_ThrowInternalError(ctx,"Audio3D: cannot add the Loop system"); return 0; }
    loop_system=id; return 1;
}
static AthenaSfx *resolve_sfx(void *user) { return athena_sfx_js_peek(((Source *)user)->sfx); }
static Source *get_source(JSContext *ctx,JSValueConst v) {
    Source *s=JS_GetOpaque2(ctx,v,source_id); if(!s) JS_ThrowTypeError(ctx,"Expected a live Audio3D.Source"); return s;
}
static void source_free(JSRuntime *rt,Source *s) {
    if(!s) return;
    athena_audio3d_source_destroy(s->src);
    JS_FreeValueRT(rt,s->sfx); js_free_rt(rt,s);
}
static void finalizer(JSRuntime *rt,JSValue v) { source_free(rt,JS_GetOpaque(v,source_id)); }
static void gc_mark(JSRuntime *rt,JSValueConst v,JS_MarkFunc *mark) {
    Source *s=JS_GetOpaque(v,source_id); if(s) JS_MarkValue(rt,s->sfx,mark);
}
/* Options parsed into a snapshot: getters may run script, so nothing is committed until all are read. */
typedef struct { AthenaAudio3DParams params; float position[3]; AthenaNode3D *node; int node_set; } Options;
static int read_options(JSContext *ctx,JSValueConst o,Options *out) {
    if(JS_IsUndefined(o)) return 1;
    if(!JS_IsObject(o)) { JS_ThrowTypeError(ctx,"Audio3D source options must be an object"); return 0; }
    AthenaAudio3DParams p=out->params; float pos[3]={out->position[0],out->position[1],out->position[2]},priority=(float)p.priority;
    if(!athena_js_option_float(ctx,o,"minDistance",&p.min_distance)||!athena_js_option_float(ctx,o,"maxDistance",&p.max_distance)||
        !athena_js_option_float(ctx,o,"volume",&p.volume)||!athena_js_option_float(ctx,o,"panStrength",&p.pan_strength)||
        !athena_js_option_float(ctx,o,"priority",&priority)||
        !athena_js_option_float(ctx,o,"x",&pos[0])||!athena_js_option_float(ctx,o,"y",&pos[1])||!athena_js_option_float(ctx,o,"z",&pos[2])) return 0;
    if(!(priority>=-1000&&priority<=1000)) { JS_ThrowRangeError(ctx,"Audio3D: priority must be -1000 to 1000"); return 0; }
    p.priority=(int)priority;
    JSValue r=JS_GetPropertyStr(ctx,o,"rolloff"); if(JS_IsException(r)) return 0;
    if(!JS_IsUndefined(r)) {
        const char *name=JS_ToCString(ctx,r); JS_FreeValue(ctx,r); if(!name) return 0;
        int lin=!strcmp(name,"linear"),inv=!strcmp(name,"inverse"); JS_FreeCString(ctx,name);
        if(!lin&&!inv) { JS_ThrowRangeError(ctx,"rolloff must be \"inverse\" or \"linear\""); return 0; }
        p.rolloff=lin?ATHENA_AUDIO3D_LINEAR:ATHENA_AUDIO3D_INVERSE;
    } else JS_FreeValue(ctx,r);
    JSValue c=JS_GetPropertyStr(ctx,o,"cull"); if(JS_IsException(c)) return 0;
    if(!JS_IsUndefined(c)) {
        const char *name=JS_ToCString(ctx,c); JS_FreeValue(ctx,c); if(!name) return 0;
        int stop=!strcmp(name,"stop"),virt=!strcmp(name,"virtual"),def=!strcmp(name,"default"); JS_FreeCString(ctx,name);
        if(!stop&&!virt&&!def) { JS_ThrowRangeError(ctx,"cull must be \"default\", \"stop\" or \"virtual\""); return 0; }
        p.cull=stop?ATHENA_AUDIO3D_CULL_STOP:virt?ATHENA_AUDIO3D_CULL_VIRTUAL:ATHENA_AUDIO3D_CULL_DEFAULT;
    } else JS_FreeValue(ctx,c);
    if(!athena_audio3d_validate(&p)) { JS_ThrowRangeError(ctx,"Audio3D: 0 < minDistance < maxDistance, volume 0-100, panStrength 0-1"); return 0; }
    JSValue node=JS_GetPropertyStr(ctx,o,"node"); if(JS_IsException(node)) return 0;
    if(!JS_IsUndefined(node)) {
        AthenaNode3D *n=NULL;
        if(!JS_IsNull(node)&&!(n=athena_node3d_from_value(ctx,node))) { JS_FreeValue(ctx,node); return 0; }
        athena_node3d_retain(n); athena_node3d_release(out->node); out->node=n; out->node_set=1;
    }
    JS_FreeValue(ctx,node);
    out->params=p; memcpy(out->position,pos,sizeof(pos));
    return 1;
}
/* new Audio3D.Source(sfx, { x, y, z, node, minDistance, maxDistance, rolloff, volume, panStrength, priority, cull }) */
static JSValue ctor(JSContext *ctx,JSValueConst target,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,2,"Audio3D.Source")) return JS_EXCEPTION;
    if(!athena_sfx_js_peek(argv[0])) return JS_ThrowTypeError(ctx,"Audio3D.Source expects a live Sound.Sfx");
    Options o={0}; athena_audio3d_default_params(&o.params);
    if(!read_options(ctx,argc==2?argv[1]:JS_UNDEFINED,&o)) { athena_node3d_release(o.node); return JS_EXCEPTION; }
    Source *s=js_mallocz(ctx,sizeof(*s)); if(!s) { athena_node3d_release(o.node); return JS_EXCEPTION; }
    s->sfx=JS_DupValue(ctx,argv[0]);
    s->src=athena_audio3d_source_create(resolve_sfx,s,&o.params);
    if(!s->src) { JS_FreeValue(ctx,s->sfx); js_free(ctx,s); athena_node3d_release(o.node); return JS_ThrowOutOfMemory(ctx); }
    athena_audio3d_source_set_position(s->src,o.position); athena_audio3d_source_set_node(s->src,o.node); athena_node3d_release(o.node);
    JSValue proto=JS_GetPropertyStr(ctx,target,"prototype");
    JSValue obj=JS_IsException(proto)?proto:JS_NewObjectProtoClass(ctx,proto,source_id); JS_FreeValue(ctx,proto);
    if(JS_IsException(obj)) { source_free(JS_GetRuntime(ctx),s); return obj; }
    JS_SetOpaque(obj,s);
    if(!ensure_system(ctx)) { JS_FreeValue(ctx,obj); return JS_EXCEPTION; }
    return obj;
}
static JSValue configure(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"Source.configure")) return JS_EXCEPTION;
    Source *s=get_source(ctx,self); if(!s) return JS_EXCEPTION;
    /* Options can invoke arbitrary getters, including self.dispose(): parse into a snapshot and reacquire the handle to commit. */
    Options o={0}; athena_audio3d_source_params(s->src,&o.params); athena_audio3d_source_position(s->src,o.position);
    if(!read_options(ctx,argv[0],&o)) { athena_node3d_release(o.node); return JS_EXCEPTION; }
    s=get_source(ctx,self);
    if(!s) { athena_node3d_release(o.node); return JS_EXCEPTION; }
    if(athena_audio3d_source_configure(s->src,&o.params)<0) { athena_node3d_release(o.node); return JS_ThrowRangeError(ctx,"Audio3D: invalid source options"); }
    athena_audio3d_source_set_position(s->src,o.position);
    if(o.node_set) athena_audio3d_source_set_node(s->src,o.node);
    athena_node3d_release(o.node);
    return JS_DupValue(ctx,self);
}
static JSValue set_position(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,3,3,"Source.setPosition")) return JS_EXCEPTION;
    float p[3]; for(int i=0;i<3;i++) if(!athena_js_float(ctx,argv[i],&p[i],"position")) return JS_EXCEPTION;
    Source *s=get_source(ctx,self); if(!s) return JS_EXCEPTION;
    athena_audio3d_source_set_position(s->src,p); return JS_DupValue(ctx,self);
}
/* play({ force }): starts the sample at the source's current levels; returns the channel, or -1 when out of
 * range (unless force), no voice is free or it was outranked (a "virtual" source then waits). */
static JSValue play(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,0,1,"Source.play")) return JS_EXCEPTION;
    int force=0;
    if(argc==1&&JS_IsObject(argv[0])) {
        JSValue f=JS_GetPropertyStr(ctx,argv[0],"force"); if(JS_IsException(f)) return f;
        force=JS_ToBool(ctx,f); JS_FreeValue(ctx,f);
    }
    Source *s=get_source(ctx,self); if(!s) return JS_EXCEPTION;
    if(!athena_sfx_js_peek(s->sfx)) return JS_ThrowTypeError(ctx,"Audio3D.Source: its Sfx was freed");
    refresh_listener();
    int channel=athena_audio3d_source_play(s->src,force);
    if(channel<-1) return JS_ThrowInternalError(ctx,"Audio3D: %s",athena_sound_result_string(channel));
    return JS_NewInt32(ctx,channel);
}
static JSValue stop(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argc; (void)argv;
    Source *s=get_source(ctx,self); if(!s) return JS_EXCEPTION;
    athena_audio3d_source_stop(s->src); return JS_DupValue(ctx,self);
}
static JSValue dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argc; (void)argv;
    if(!athena_js_class(ctx,self,source_id)) return JS_EXCEPTION;
    Source *s=JS_GetOpaque(self,source_id); JS_SetOpaque(self,NULL); source_free(JS_GetRuntime(ctx),s); return JS_UNDEFINED;
}
static JSValue source_prop(JSContext *ctx,JSValueConst self,int magic) {
    Source *s=get_source(ctx,self); if(!s) return JS_EXCEPTION;
    switch(magic) {
        case 0: return JS_NewBool(ctx,athena_audio3d_source_channel(s->src)>=0);
        case 1: return JS_NewInt32(ctx,athena_audio3d_source_channel(s->src));
        case 2: return JS_NewInt32(ctx,athena_audio3d_source_volume(s->src));
        case 3: return JS_NewInt32(ctx,athena_audio3d_source_pan(s->src));
        case 4: return JS_NewBool(ctx,athena_audio3d_source_pending(s->src));
        default: return JS_NewFloat64(ctx,athena_audio3d_source_occlusion(s->src));
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
    AthenaAudio3DListener l;
    for(int i=0;i<3;i++) { l.position[i]=f[i]; l.right[i]=f[3+i]/len; }
    athena_audio3d_set_listener(&l);
    return JS_UNDEFINED;
}
static JSValue levels(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,3,4,"Audio3D.levels")) return JS_EXCEPTION;
    float p[3]; for(int i=0;i<3;i++) if(!athena_js_float(ctx,argv[i],&p[i],"position")) return JS_EXCEPTION;
    Options o={0}; athena_audio3d_default_params(&o.params);
    if(argc==4&&!read_options(ctx,argv[3],&o)) { athena_node3d_release(o.node); return JS_EXCEPTION; }
    athena_node3d_release(o.node);
    refresh_listener();
    AthenaAudio3DListener listener; athena_audio3d_get_listener(&listener);
    int v,pn; float d=athena_audio3d_levels(&listener,p,&o.params,&v,&pn);
    JSValue obj=JS_NewObject(ctx);
    JS_SetPropertyStr(ctx,obj,"volume",JS_NewInt32(ctx,v)); JS_SetPropertyStr(ctx,obj,"pan",JS_NewInt32(ctx,pn));
    JS_SetPropertyStr(ctx,obj,"distance",JS_NewFloat64(ctx,d));
    return obj;
}
static JSValue put_stats(JSContext *ctx) {
    JSValue o=JS_NewObject(ctx); if(JS_IsException(o)) return o;
    const struct { const char *name; uint32_t value; } f[]={
        {"sources",last_stats.sources},{"candidates",last_stats.candidates},{"voices",last_stats.voices},{"stolen",last_stats.stolen},
        {"culled",last_stats.culled},{"started",last_stats.started},{"queries",last_stats.queries},
        {"queryErrors",last_stats.query_errors},{"oldestResult",last_stats.oldest_result}};
    for(unsigned i=0;i<countof(f);i++) JS_SetPropertyStr(ctx,o,f[i].name,JS_NewUint32(ctx,f[i].value));
    return o;
}
/* update(): what the Loop system does each frame, for games without Loop.run(); returns the stats. */
static JSValue update(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Audio3D.update")) return JS_EXCEPTION;
    run_update(); return put_stats(ctx);
}
static JSValue js_stats(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Audio3D.stats")) return JS_EXCEPTION;
    return put_stats(ctx);
}
static JSValue set_max_voices(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,1,"Audio3D.setMaxVoices")) return JS_EXCEPTION;
    float n; if(!athena_js_float(ctx,argv[0],&n,"count")) return JS_EXCEPTION;
    if(!(n>=1&&n<=ATHENA_AUDIO3D_MAX_VOICES)||n!=floorf(n)) return JS_ThrowRangeError(ctx,"Audio3D.setMaxVoices: 1 to %u",ATHENA_AUDIO3D_MAX_VOICES);
    athena_audio3d_set_max_voices((uint32_t)n); return JS_UNDEFINED;
}
/* ---- Occlusion provider: a Collision3D.World (needs that module) or a function (x0,y0,z0,x1,y1,z1) -> 0..1 ---- */
extern int athena_collision3d_js_segment_blocked(JSContext *ctx,JSValueConst world,const float from[3],const float to[3],uint32_t mask,float *blocked) __attribute__((weak));
static JSContext *occ_ctx;
static JSValue occ_provider=JS_UNDEFINED;
static int occ_is_world;
static uint32_t occ_mask=0xffffffffu;
static int occ_query(void *user,const float from[3],const float to[3],float *blocked) {
    (void)user;
    if(!occ_ctx) return -1;
    if(occ_is_world) return athena_collision3d_js_segment_blocked(occ_ctx,occ_provider,from,to,occ_mask,blocked);
    JSValue args[6]; for(int i=0;i<3;i++) { args[i]=JS_NewFloat64(occ_ctx,from[i]); args[3+i]=JS_NewFloat64(occ_ctx,to[i]); }
    JSValue r=JS_Call(occ_ctx,occ_provider,JS_UNDEFINED,6,args);
    for(int i=0;i<6;i++) JS_FreeValue(occ_ctx,args[i]);
    if(JS_IsException(r)) { JS_FreeValue(occ_ctx,JS_GetException(occ_ctx)); return -1; }
    double v=0; int ok=JS_ToFloat64(occ_ctx,&v,r)==0; JS_FreeValue(occ_ctx,r);
    if(!ok) { JS_FreeValue(occ_ctx,JS_GetException(occ_ctx)); return -1; }
    *blocked=(float)v; return 0;
}
static void occ_clear(JSContext *ctx) {
    athena_audio3d_set_occlusion(NULL,NULL,NULL);
    JS_FreeValue(ctx,occ_provider); occ_provider=JS_UNDEFINED; occ_ctx=NULL; occ_is_world=0;
}
/* setOcclusion(provider | null, { maxQueries, interval, strength, smoothing, mask }) */
static JSValue set_occlusion(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,2,"Audio3D.setOcclusion")) return JS_EXCEPTION;
    if(JS_IsNull(argv[0])) { occ_clear(ctx); return JS_UNDEFINED; }
    AthenaAudio3DOcclusionOptions o; athena_audio3d_occlusion_defaults(&o);
    float max_queries=(float)o.max_queries,interval=(float)o.interval,mask=-1;
    if(argc==2&&!JS_IsUndefined(argv[1])) {
        if(!JS_IsObject(argv[1])) return JS_ThrowTypeError(ctx,"Audio3D.setOcclusion options must be an object");
        if(!athena_js_option_float(ctx,argv[1],"maxQueries",&max_queries)||!athena_js_option_float(ctx,argv[1],"interval",&interval)||
            !athena_js_option_float(ctx,argv[1],"strength",&o.strength)||!athena_js_option_float(ctx,argv[1],"smoothing",&o.smoothing)||
            !athena_js_option_float(ctx,argv[1],"mask",&mask)) return JS_EXCEPTION;
    }
    if(!(max_queries>=1&&max_queries<=256&&interval>=1&&interval<=3600)) return JS_ThrowRangeError(ctx,"Audio3D.setOcclusion: maxQueries 1-256, interval 1-3600");
    o.max_queries=(uint32_t)max_queries; o.interval=(uint32_t)interval;
    int world=!JS_IsFunction(ctx,argv[0]);
    if(world&&!athena_collision3d_js_segment_blocked) return JS_ThrowTypeError(ctx,"Audio3D.setOcclusion: Collision3D is not in this build; pass a function");
    if(world) { /* validate now: a world that is not live is a TypeError, not a silent provider error */
        float a[3]={0,0,0},b[3]={0,0,1},blocked; if(athena_collision3d_js_segment_blocked(ctx,argv[0],a,b,1,&blocked)<0) return JS_ThrowTypeError(ctx,"Audio3D.setOcclusion expects a function or a live Collision3D.World");
    }
    if(athena_audio3d_set_occlusion(occ_query,NULL,&o)<0) return JS_ThrowRangeError(ctx,"Audio3D.setOcclusion: strength 0-1, smoothing in (0, 1]");
    JS_FreeValue(ctx,occ_provider); occ_provider=JS_DupValue(ctx,argv[0]); occ_ctx=ctx; occ_is_world=world;
    occ_mask=mask<0?0xffffffffu:(uint32_t)mask;
    return JS_UNDEFINED;
}
static JSClassDef class_def={"Audio3D.Source",.finalizer=finalizer,.gc_mark=gc_mark};
static const JSCFunctionListEntry methods[]={
    JS_CFUNC_DEF("play",0,play),JS_CFUNC_DEF("stop",0,stop),JS_CFUNC_DEF("setPosition",3,set_position),
    JS_CFUNC_DEF("configure",1,configure),JS_CFUNC_DEF("dispose",0,dispose),
    JS_CGETSET_MAGIC_DEF("playing",source_prop,NULL,0),JS_CGETSET_MAGIC_DEF("channel",source_prop,NULL,1),
    JS_CGETSET_MAGIC_DEF("volume",source_prop,NULL,2),JS_CGETSET_MAGIC_DEF("pan",source_prop,NULL,3),
    JS_CGETSET_MAGIC_DEF("pending",source_prop,NULL,4),JS_CGETSET_MAGIC_DEF("occlusion",source_prop,NULL,5)};
static const JSCFunctionListEntry exports[]={
    JS_CFUNC_DEF("setListener",1,set_listener),JS_CFUNC_DEF("levels",3,levels),JS_CFUNC_DEF("update",0,update),
    JS_CFUNC_DEF("stats",0,js_stats),JS_CFUNC_DEF("setMaxVoices",1,set_max_voices),JS_CFUNC_DEF("setOcclusion",1,set_occlusion),
    JS_PROP_INT32_DEF("MAX_VOICES",ATHENA_AUDIO3D_MAX_VOICES,JS_PROP_ENUMERABLE)};
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
    if(loop_system) athena_loop_system_remove(loop_system);
    if(listener_camera) { athena_camera3d_js_release(listener_camera); listener_camera=NULL; }
    occ_clear(ctx);
    athena_audio3d_module_shutdown();   /* settings belong to the runtime; sources die with their handles */
}
JSModuleDef *athena_audio3d_js_init(JSContext *ctx) {
    JSModuleDef *m=athena_push_module(ctx,init,exports,countof(exports),"Audio3D");
    if(m) JS_AddModuleExport(ctx,m,"Source");
    return m;
}
