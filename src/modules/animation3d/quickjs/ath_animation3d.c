#include <string.h>
#include <athena_js_args.h>
#include <athena/animation3d.h>
#include <athena/js/scene3d.h>
#include <athena/js/animation3d.h>
#include "ath_animation3d.h"
static JSClassID clip_id,player_id;
/* Players act in the Loop system by themselves: kept until dispose(). */
static AthenaJSKept *kept_players;
static AthenaClip3D *get_clip(JSContext *ctx,JSValueConst v) {
    AthenaClip3D *c=JS_GetOpaque2(ctx,v,clip_id);
    if(!c) JS_ThrowTypeError(ctx,"Expected a live Animation3D.Clip");
    return c;
}
static AthenaPlayer3D *get_player(JSContext *ctx,JSValueConst v) {
    AthenaPlayer3D *p=JS_GetOpaque2(ctx,v,player_id);
    if(!p) JS_ThrowTypeError(ctx,"Expected a live Animation3D.Player");
    return p;
}
static void release_clip(void *c) { athena_clip3d_release(c); }
static void release_player(void *p) { athena_player3d_release(p); }
static void clip_finalizer(JSRuntime *rt,JSValue v) { (void)rt; athena_clip3d_release(JS_GetOpaque(v,clip_id)); }
static void player_finalizer(JSRuntime *rt,JSValue v) { (void)rt; athena_player3d_release(JS_GetOpaque(v,player_id)); }
static JSValue new_instance(JSContext *ctx,JSValueConst target,JSClassID id,void *opaque,void (*release)(void *)) {
    JSValue proto=JS_GetPropertyStr(ctx,target,"prototype");
    if(JS_IsException(proto)) { release(opaque); return proto; }
    JSValue obj=JS_NewObjectProtoClass(ctx,proto,id); JS_FreeValue(ctx,proto);
    if(JS_IsException(obj)) { release(opaque); return obj; }
    JS_SetOpaque(obj,opaque); return obj;
}
static int string_choice(JSContext *ctx,JSValueConst options,const char *key,const char *const *names,
    int count,int fallback,int *out) {
    JSValue value=JS_GetPropertyStr(ctx,options,key);
    if(JS_IsException(value)) return 0;
    if(JS_IsUndefined(value)&&fallback>=0) { *out=fallback; return 1; }
    const char *text=JS_IsString(value)?JS_ToCString(ctx,value):NULL;
    JS_FreeValue(ctx,value);
    if(text) for(int i=0;i<count;i++) if(!strcmp(text,names[i])) { JS_FreeCString(ctx,text); *out=i; return 1; }
    if(text) JS_FreeCString(ctx,text);
    JS_ThrowTypeError(ctx,"track.%s is not a valid name",key); return 0;
}
/* A cubic track's tangents: a Float32Array as long as values, copied. */
static int read_tangents(JSContext *ctx,JSValueConst track,const char *key,uint32_t count,float **out) {
    JSValue value=JS_GetPropertyStr(ctx,track,key); if(JS_IsException(value)) return 0;
    AthenaJSArray a; int ok=0;
    if(athena_js_array(ctx,value,JS_TYPED_ARRAY_FLOAT32,&a,key)) {
        if(a.count!=count) JS_ThrowRangeError(ctx,"track.%s needs as many floats as track.values",key);
        else if((*out=js_malloc(ctx,count*sizeof(float)))) { memcpy(*out,a.data,count*sizeof(float)); ok=1; }
        JS_FreeValue(ctx,a.backing);
    }
    JS_FreeValue(ctx,value); return ok;
}
/* Reads one track and copies its arrays at once: later getters may detach
 * the buffers of tracks already read. */
static int read_track(JSContext *ctx,JSValueConst track,AthenaTrack3DDesc *out) {
    static const char *const paths[]={"position","rotation","scale","weights"},*const modes[]={"linear","step","cubic"};
    if(!JS_IsObject(track)) { JS_ThrowTypeError(ctx,"Each track must be an object"); return 0; }
    int path,mode; float target=0;
    JSValue value=JS_GetPropertyStr(ctx,track,"target"); if(JS_IsException(value)) return 0;
    int ok=JS_IsUndefined(value)||athena_js_float(ctx,value,&target,"track.target");
    JS_FreeValue(ctx,value);
    if(!ok) return 0;
    if(target<0||target>=ATHENA_ANIM3D_MAX_TRACKS||target!=(float)(uint32_t)target) {
        JS_ThrowRangeError(ctx,"track.target must be an integer below %u",(unsigned)ATHENA_ANIM3D_MAX_TRACKS); return 0;
    }
    if(!string_choice(ctx,track,"path",paths,4,-1,&path)||!string_choice(ctx,track,"interpolation",modes,3,0,&mode)) return 0;
    JSValue times=JS_GetPropertyStr(ctx,track,"times"); if(JS_IsException(times)) return 0;
    JSValue values=JS_GetPropertyStr(ctx,track,"values");
    if(JS_IsException(values)) { JS_FreeValue(ctx,times); return 0; }
    AthenaJSArray t,v; ok=0;
    if(athena_js_array(ctx,times,JS_TYPED_ARRAY_FLOAT32,&t,"track.times")) {
        if(athena_js_array(ctx,values,JS_TYPED_ARRAY_FLOAT32,&v,"track.values")) {
            /* Weights: as many floats per key as morph targets (1..8). */
            uint32_t width=path==ATHENA_ANIM3D_WEIGHTS?(t.count?(uint32_t)(v.count/t.count):0):
                path==ATHENA_ANIM3D_ROTATION?4:3;
            if(!t.count||t.count>ATHENA_ANIM3D_MAX_KEYS) JS_ThrowRangeError(ctx,"track.times needs 1..%u keys",(unsigned)ATHENA_ANIM3D_MAX_KEYS);
            else if(path==ATHENA_ANIM3D_WEIGHTS&&(v.count%t.count||!width||width>ATHENA_MODEL3D_MAX_TARGETS))
                JS_ThrowRangeError(ctx,"weights track.values needs 1..%u floats per key",(unsigned)ATHENA_MODEL3D_MAX_TARGETS);
            else if(v.count!=t.count*width) JS_ThrowRangeError(ctx,"track.values needs %u floats per key",(unsigned)width);
            else {
                float *ct=js_malloc(ctx,t.count*sizeof(float)),*cv=js_malloc(ctx,v.count*sizeof(float));
                float *ci=NULL,*co=NULL;
                if(ct&&cv) {
                    memcpy(ct,t.data,t.count*sizeof(float)); memcpy(cv,v.data,v.count*sizeof(float));
                    ok=mode!=ATHENA_ANIM3D_CUBIC||(read_tangents(ctx,track,"inTangents",v.count,&ci)&&
                        read_tangents(ctx,track,"outTangents",v.count,&co));
                    if(ok) *out=(AthenaTrack3DDesc){(uint32_t)target,path,mode,ct,cv,(uint32_t)t.count,
                        path==ATHENA_ANIM3D_WEIGHTS?width:0,ci,co};
                }
                if(!ok) { js_free(ctx,ct); js_free(ctx,cv); js_free(ctx,ci); js_free(ctx,co); }
            }
            JS_FreeValue(ctx,v.backing);
        }
        JS_FreeValue(ctx,t.backing);
    }
    JS_FreeValue(ctx,times); JS_FreeValue(ctx,values); return ok;
}
static JSValue clip_ctor(JSContext *ctx,JSValueConst target,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"Animation3D.Clip")) return JS_EXCEPTION;
    int is_array=JS_IsArray(ctx,argv[0]); if(is_array<0) return JS_EXCEPTION;
    if(!is_array) return JS_ThrowTypeError(ctx,"Animation3D.Clip expects an array of tracks");
    JSValue length_value=JS_GetPropertyStr(ctx,argv[0],"length"); int64_t length=0;
    int failed=JS_IsException(length_value)||JS_ToInt64(ctx,&length,length_value)<0;
    JS_FreeValue(ctx,length_value); if(failed) return JS_EXCEPTION;
    if(length<1||length>ATHENA_ANIM3D_MAX_TRACKS) return JS_ThrowRangeError(ctx,"A clip needs 1..%u tracks",(unsigned)ATHENA_ANIM3D_MAX_TRACKS);
    AthenaTrack3DDesc *tracks=js_mallocz(ctx,(size_t)length*sizeof(*tracks)); if(!tracks) return JS_EXCEPTION;
    uint32_t read=0; JSValue result=JS_UNDEFINED;
    for(;read<length;read++) {
        JSValue track=JS_GetPropertyUint32(ctx,argv[0],read);
        int ok=!JS_IsException(track)&&read_track(ctx,track,&tracks[read]);
        JS_FreeValue(ctx,track);
        if(!ok) { result=JS_EXCEPTION; break; }
    }
    if(!JS_IsException(result)) {
        AthenaClip3D *clip=NULL; int code=athena_clip3d_create(tracks,read,&clip);
        if(code==ATHENA_ANIM3D_ENOMEM) result=JS_ThrowOutOfMemory(ctx);
        else if(code<0) result=JS_ThrowRangeError(ctx,"Invalid track: times must be >= 0 and increasing, values and tangents finite, rotations nonzero");
        else result=new_instance(ctx,target,clip_id,clip,release_clip);
    }
    for(uint32_t i=0;i<read;i++) {
        js_free(ctx,(void *)tracks[i].times); js_free(ctx,(void *)tracks[i].values);
        js_free(ctx,(void *)tracks[i].in_tangents); js_free(ctx,(void *)tracks[i].out_tangents);
    }
    js_free(ctx,tracks); return result;
}
JSValue athena_clip3d_to_value(JSContext *ctx,AthenaClip3D *clip) {
    if(!clip_id) return JS_ThrowInternalError(ctx,"Animation3D is not initialized");
    JSValue obj=JS_NewObjectClass(ctx,clip_id); if(JS_IsException(obj)) return obj;
    athena_clip3d_retain(clip); JS_SetOpaque(obj,clip); return obj;
}
static JSValue clip_duration(JSContext *ctx,JSValueConst self) {
    AthenaClip3D *c=get_clip(ctx,self); return c?JS_NewFloat64(ctx,athena_clip3d_duration(c)):JS_EXCEPTION;
}
static JSValue clip_targets(JSContext *ctx,JSValueConst self) {
    AthenaClip3D *c=get_clip(ctx,self); return c?JS_NewUint32(ctx,athena_clip3d_target_count(c)):JS_EXCEPTION;
}
static JSValue clip_dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Clip.dispose")||!athena_js_class(ctx,self,clip_id)) return JS_EXCEPTION;
    AthenaClip3D *c=JS_GetOpaque(self,clip_id); JS_SetOpaque(self,NULL); athena_clip3d_release(c); return JS_UNDEFINED;
}
static void *node_item(JSContext *ctx,JSValueConst v) { return athena_node3d_from_value(ctx,v); }
static void node_retain(void *n) { athena_node3d_retain(n); }
static void node_release(void *n) { athena_node3d_release(n); }
static JSValue player_ctor(JSContext *ctx,JSValueConst target,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,2,2,"Animation3D.Player")) return JS_EXCEPTION;
    AthenaClip3D *clip=get_clip(ctx,argv[0]); if(!clip) return JS_EXCEPTION;
    athena_clip3d_retain(clip); /* node getters may dispose the clip handle */
    AthenaJSHandles h;
    if(!athena_js_handles(ctx,argv[1],&h,node_item,node_retain,node_release,"nodes")) {
        athena_clip3d_release(clip); return JS_EXCEPTION;
    }
    JSValue result;
    if(h.count<athena_clip3d_target_count(clip))
        result=JS_ThrowRangeError(ctx,"The clip animates %u nodes",(unsigned)athena_clip3d_target_count(clip));
    else {
        AthenaPlayer3D *p=athena_player3d_create(clip,(AthenaNode3D *const *)h.items,h.count);
        result=p?new_instance(ctx,target,player_id,p,release_player):JS_ThrowOutOfMemory(ctx);
        if(!JS_IsException(result)&&!athena_js_keep(ctx,&kept_players,result)) {
            JS_FreeValue(ctx,result); result=JS_EXCEPTION;
        }
    }
    athena_js_handles_free(&h); athena_clip3d_release(clip); return result;
}
static JSValue player_control(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Player control")) return JS_EXCEPTION;
    AthenaPlayer3D *p=get_player(ctx,self); if(!p) return JS_EXCEPTION;
    if(magic==0) athena_player3d_play(p); else if(magic==1) athena_player3d_pause(p); else athena_player3d_stop(p);
    return JS_DupValue(ctx,self);
}
static JSValue player_advance(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"Player.advance")) return JS_EXCEPTION;
    float dt; if(!athena_js_float(ctx,argv[0],&dt,"dt")) return JS_EXCEPTION;
    AthenaPlayer3D *p=get_player(ctx,self); if(!p) return JS_EXCEPTION;
    if(dt<0) return JS_ThrowRangeError(ctx,"dt must not be negative");
    int code=athena_player3d_advance(p,dt);
    if(code<0) return JS_ThrowRangeError(ctx,"Animation produced an invalid transform");
    return JS_NewBool(ctx,code==1);
}
static JSValue player_get(JSContext *ctx,JSValueConst self,int magic) {
    AthenaPlayer3D *p=get_player(ctx,self); if(!p) return JS_EXCEPTION;
    switch(magic) {
        case 0: return JS_NewFloat64(ctx,athena_player3d_time(p));
        case 1: return JS_NewFloat64(ctx,athena_player3d_speed(p));
        case 2: return JS_NewBool(ctx,athena_player3d_loop(p));
        default: return JS_NewBool(ctx,athena_player3d_playing(p));
    }
}
static JSValue player_set(JSContext *ctx,JSValueConst self,JSValueConst value,int magic) {
    AthenaPlayer3D *p=get_player(ctx,self); if(!p) return JS_EXCEPTION;
    if(magic==2) {
        if(!JS_IsBool(value)) return JS_ThrowTypeError(ctx,"loop must be a boolean");
        athena_player3d_set_loop(p,JS_ToBool(ctx,value)); return JS_UNDEFINED;
    }
    float f; if(!athena_js_float(ctx,value,&f,magic?"speed":"time")) return JS_EXCEPTION;
    int code=magic?athena_player3d_set_speed(p,f):athena_player3d_set_time(p,f);
    return code<0?JS_ThrowRangeError(ctx,"Invalid %s",magic?"speed":"time"):JS_UNDEFINED;
}
static JSValue player_dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Player.dispose")||!athena_js_class(ctx,self,player_id)) return JS_EXCEPTION;
    AthenaPlayer3D *p=JS_GetOpaque(self,player_id);
    if(p) athena_js_unkeep(ctx,&kept_players,p,player_id);
    JS_SetOpaque(self,NULL); athena_player3d_release(p); return JS_UNDEFINED;
}
static JSValue advance_all(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,1,"Animation3D.advance")) return JS_EXCEPTION;
    float dt; if(!athena_js_float(ctx,argv[0],&dt,"dt")) return JS_EXCEPTION;
    if(dt<0) return JS_ThrowRangeError(ctx,"dt must not be negative");
    int finished=athena_animation3d_advance(dt);
    return finished<0?JS_ThrowRangeError(ctx,"Animation produced an invalid transform"):JS_NewUint32(ctx,(uint32_t)finished);
}
static JSValue attach_loop(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,0,1,"Animation3D.attachLoop")) return JS_EXCEPTION;
    float priority=ATHENA_ANIM3D_LOOP_PRIORITY;
    if(argc==1&&!JS_IsUndefined(argv[0])&&!athena_js_float(ctx,argv[0],&priority,"priority")) return JS_EXCEPTION;
    if(priority!=(float)(int)priority) return JS_ThrowRangeError(ctx,"priority must be an integer");
    if(athena_animation3d_loop_system()) return JS_UNDEFINED; /* already attached */
    int id=athena_animation3d_attach_loop((int)priority,ctx);
    if(id==ATHENA_ANIM3D_ENOMEM) return JS_ThrowOutOfMemory(ctx);
    return id<0?JS_ThrowInternalError(ctx,"Animation3D could not join the Loop"):JS_UNDEFINED;
}
static JSValue detach_loop(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Animation3D.detachLoop")) return JS_EXCEPTION;
    return JS_NewBool(ctx,athena_animation3d_detach_loop());
}
static JSValue attached(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Animation3D.isAttached")) return JS_EXCEPTION;
    return JS_NewBool(ctx,athena_animation3d_loop_system()!=0);
}
static JSClassDef clip_class={"Animation3D.Clip",.finalizer=clip_finalizer};
static JSClassDef player_class={"Animation3D.Player",.finalizer=player_finalizer};
static const JSCFunctionListEntry clip_methods[]={
    JS_CGETSET_DEF("duration",clip_duration,NULL),JS_CGETSET_DEF("targetCount",clip_targets,NULL),
    JS_CFUNC_DEF("dispose",0,clip_dispose)};
static const JSCFunctionListEntry player_methods[]={
    JS_CFUNC_MAGIC_DEF("play",0,player_control,0),JS_CFUNC_MAGIC_DEF("pause",0,player_control,1),
    JS_CFUNC_MAGIC_DEF("stop",0,player_control,2),JS_CFUNC_DEF("advance",1,player_advance),
    JS_CGETSET_MAGIC_DEF("time",player_get,player_set,0),JS_CGETSET_MAGIC_DEF("speed",player_get,player_set,1),
    JS_CGETSET_MAGIC_DEF("loop",player_get,player_set,2),JS_CGETSET_MAGIC_DEF("playing",player_get,NULL,3),
    JS_CFUNC_DEF("dispose",0,player_dispose)};
static const JSCFunctionListEntry exports[]={
    JS_CFUNC_DEF("advance",1,advance_all),JS_CFUNC_DEF("attachLoop",0,attach_loop),
    JS_CFUNC_DEF("detachLoop",0,detach_loop),JS_CFUNC_DEF("isAttached",0,attached),
    JS_PROP_INT32_DEF("LOOP_PRIORITY",ATHENA_ANIM3D_LOOP_PRIORITY,JS_PROP_ENUMERABLE)};
static int define_class(JSContext *ctx,JSModuleDef *m,JSClassID id,const char *name,JSCFunction *ctor,
    const JSCFunctionListEntry *methods,int count) {
    JSValue proto=JS_NewObject(ctx); if(JS_IsException(proto)) return -1;
    JS_SetPropertyFunctionList(ctx,proto,methods,count);
    JSValue cls=JS_NewCFunction2(ctx,ctor,name,2,JS_CFUNC_constructor,0);
    if(JS_IsException(cls)) { JS_FreeValue(ctx,proto); return -1; }
    JS_SetConstructor(ctx,cls,proto); JS_SetClassProto(ctx,id,proto);
    return JS_SetModuleExport(ctx,m,name,cls);
}
static int init(JSContext *ctx,JSModuleDef *m) {
    if(athena_register_class(ctx,&clip_id,&clip_class)<0||athena_register_class(ctx,&player_id,&player_class)<0) return -1;
    if(define_class(ctx,m,clip_id,"Clip",clip_ctor,clip_methods,countof(clip_methods))<0||
        define_class(ctx,m,player_id,"Player",player_ctor,player_methods,countof(player_methods))<0) return -1;
    return JS_SetModuleExportList(ctx,m,exports,countof(exports));
}
void athena_animation3d_js_cleanup(JSContext *ctx) {
    athena_animation3d_detach_owner(ctx); athena_js_keep_free(ctx,&kept_players);
}
JSModuleDef *athena_animation3d_js_init(JSContext *ctx) {
    JSModuleDef *m=athena_push_module(ctx,init,exports,countof(exports),"Animation3D");
    if(m) { JS_AddModuleExport(ctx,m,"Clip"); JS_AddModuleExport(ctx,m,"Player"); }
    return m;
}
