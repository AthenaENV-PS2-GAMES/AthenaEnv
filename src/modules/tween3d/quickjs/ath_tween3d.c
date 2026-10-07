#include <math.h>
#include <stdlib.h>
#include <athena_js_args.h>
#include <athena/tween3d.h>
#include <athena/js/scene3d.h>
#include <athena/js/model3d.h>
#include <athena/js/camera3d.h>
#include "ath_tween3d.h"
static JSClassID handle_id;
/* JS handle: one tween reference and the promise of its end. */
typedef struct { AthenaTween3D *tween; JSValue promise; } Handle;
/* Resolver kept by the native tween until it ends. */
typedef struct { JSContext *ctx; JSValue resolve; } Resolver;
static int cleaning; /* runtime teardown: end tweens without running JS */
static void on_end(void *opaque,int completed) {
    Resolver *r=opaque;
    if(!cleaning) {
        JSValue arg=JS_NewBool(r->ctx,completed);
        JSValue result=JS_Call(r->ctx,r->resolve,JS_UNDEFINED,1,&arg);
        JS_FreeValue(r->ctx,result);
    }
    JS_FreeValue(r->ctx,r->resolve); js_free(r->ctx,r);
}
static void release_camera(void *camera) { athena_camera3d_js_release(camera); }
static Handle *get_handle(JSContext *ctx,JSValueConst v) {
    Handle *h=JS_GetOpaque2(ctx,v,handle_id);
    if(!h) JS_ThrowTypeError(ctx,"Expected a Tween3D handle");
    return h;
}
static void handle_finalizer(JSRuntime *rt,JSValue v) {
    Handle *h=JS_GetOpaque(v,handle_id); if(!h) return;
    athena_tween3d_release(h->tween); JS_FreeValueRT(rt,h->promise); js_free_rt(rt,h);
}
static void handle_mark(JSRuntime *rt,JSValueConst v,JS_MarkFunc *mark) {
    Handle *h=JS_GetOpaque(v,handle_id); if(h) JS_MarkValue(rt,h->promise,mark);
}
/* Node, Instance or Camera, without leaving an exception for the misses. */
static void *resolve_target(JSContext *ctx,JSValueConst v,AthenaTween3DTarget *kind) {
    void *target;
    if((target=athena_node3d_from_value(ctx,v))) { *kind=ATHENA_TWEEN3D_NODE; return target; }
    JS_FreeValue(ctx,JS_GetException(ctx));
    if((target=athena_instance3d_from_value(ctx,v))) { *kind=ATHENA_TWEEN3D_INSTANCE; return target; }
    JS_FreeValue(ctx,JS_GetException(ctx));
    if((target=athena_camera3d_from_value(ctx,v))) { *kind=ATHENA_TWEEN3D_CAMERA; return target; }
    JS_FreeValue(ctx,JS_GetException(ctx));
    JS_ThrowTypeError(ctx,"Tween3D targets are Scene3D.Node, Model3D.Instance or Camera3D.Camera");
    return NULL;
}
/* props[key]: three numbers (Array or Float32Array). 0 absent, 1 read, -1 error. */
static int vector3(JSContext *ctx,JSValueConst props,const char *key,float out[3]) {
    JSValue v=JS_GetPropertyStr(ctx,props,key); if(JS_IsException(v)) return -1;
    if(JS_IsUndefined(v)) return 0;
    if(!JS_IsObject(v)) { JS_FreeValue(ctx,v); JS_ThrowTypeError(ctx,"props.%s must hold three numbers",key); return -1; }
    int ok=1;
    for(uint32_t i=0;ok&&i<3;i++) {
        JSValue e=JS_GetPropertyUint32(ctx,v,i); /* the exception, if any, stays pending */
        ok=!JS_IsException(e)&&athena_js_float(ctx,e,&out[i],key);
        JS_FreeValue(ctx,e);
    }
    JS_FreeValue(ctx,v);
    return ok?1:-1;
}
static int option_bool(JSContext *ctx,JSValueConst options,const char *key,int *out) {
    JSValue v=JS_GetPropertyStr(ctx,options,key); if(JS_IsException(v)) return 0;
    int ok=JS_IsUndefined(v)||JS_IsBool(v);
    if(JS_IsBool(v)) *out=JS_ToBool(ctx,v);
    JS_FreeValue(ctx,v);
    if(!ok) JS_ThrowTypeError(ctx,"options.%s must be a boolean",key);
    return ok;
}
static int read_options(JSContext *ctx,JSValueConst options,AthenaTween3DDesc *d) {
    d->ease=ATHENA_EASE_OUT_QUAD; /* Tween's default */
    if(JS_IsUndefined(options)) return 1;
    if(!JS_IsObject(options)) { JS_ThrowTypeError(ctx,"options must be an object"); return 0; }
    JSValue ease=JS_GetPropertyStr(ctx,options,"ease"); if(JS_IsException(ease)) return 0;
    if(!JS_IsUndefined(ease)) {
        const char *name=JS_IsString(ease)?JS_ToCString(ctx,ease):NULL;
        int curve=athena_ease_find(name);
        if(name) JS_FreeCString(ctx,name);
        JS_FreeValue(ctx,ease);
        if(curve<0) { JS_ThrowRangeError(ctx,"options.ease must be a curve name such as \"outBack\""); return 0; }
        d->ease=curve;
    }
    if(!athena_js_option_float(ctx,options,"delay",&d->delay)) return 0;
    JSValue repeat=JS_GetPropertyStr(ctx,options,"repeat"); if(JS_IsException(repeat)) return 0;
    if(!JS_IsUndefined(repeat)) {
        double r=-1; int number=JS_IsNumber(repeat);
        if(number) JS_ToFloat64(ctx,&r,repeat); /* numbers convert without side effects */
        JS_FreeValue(ctx,repeat);
        if(!number||!(r==INFINITY||(r>=0&&r<=2147483647.0&&r==floor(r)))) {
            JS_ThrowRangeError(ctx,"options.repeat must be a non-negative integer or Infinity"); return 0;
        }
        d->repeat=r==INFINITY?-1:(int32_t)r;
    }
    return option_bool(ctx,options,"yoyo",&d->yoyo)&&option_bool(ctx,options,"overwrite",&d->overwrite);
}
static JSValue tween_to(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,3,4,"Tween3D.to")) return JS_EXCEPTION;
    AthenaTween3DTarget kind; void *target=resolve_target(ctx,argv[0],&kind); if(!target) return JS_EXCEPTION;
    if(!JS_IsObject(argv[1])) return JS_ThrowTypeError(ctx,"props must be an object");
    AthenaTween3DDesc d={0};
    int got[4];
    if((got[0]=vector3(ctx,argv[1],"position",d.position))<0||(got[1]=vector3(ctx,argv[1],"scale",d.scale))<0||
        (got[2]=vector3(ctx,argv[1],"rotation",d.rotation))<0||(got[3]=vector3(ctx,argv[1],"target",d.look))<0)
        return JS_EXCEPTION;
    d.channels=(got[0]?ATHENA_TWEEN3D_POSITION:0)|(got[1]?ATHENA_TWEEN3D_SCALE:0)|
        (got[2]?ATHENA_TWEEN3D_ROTATION:0)|(got[3]?ATHENA_TWEEN3D_LOOK:0);
    if(!athena_js_float(ctx,argv[2],&d.duration,"duration")) return JS_EXCEPTION;
    if(!read_options(ctx,argc>3?argv[3]:JS_UNDEFINED,&d)) return JS_EXCEPTION;
    /* Getters above may have disposed the target: resolve it again. */
    AthenaTween3DTarget again; target=resolve_target(ctx,argv[0],&again); if(!target) return JS_EXCEPTION;
    Handle *h=js_mallocz(ctx,sizeof(*h)); Resolver *r=js_mallocz(ctx,sizeof(*r));
    JSValue funcs[2],obj=JS_UNDEFINED;
    JSValue promise=h&&r?JS_NewPromiseCapability(ctx,funcs):JS_ThrowOutOfMemory(ctx);
    if(JS_IsException(promise)) { js_free(ctx,h); js_free(ctx,r); return promise; }
    JS_FreeValue(ctx,funcs[1]);
    if(kind==ATHENA_TWEEN3D_CAMERA) athena_camera3d_js_retain(target);
    AthenaTween3D *t=NULL;
    int code=athena_tween3d_create(kind,target,kind==ATHENA_TWEEN3D_CAMERA?release_camera:NULL,&d,&t);
    if(code<0) {
        if(kind==ATHENA_TWEEN3D_CAMERA) athena_camera3d_js_release(target);
        JS_FreeValue(ctx,funcs[0]); JS_FreeValue(ctx,promise); js_free(ctx,h); js_free(ctx,r);
        if(code==ATHENA_TWEEN3D_ENOMEM) return JS_ThrowOutOfMemory(ctx);
        return JS_ThrowRangeError(ctx,d.channels&~(kind==ATHENA_TWEEN3D_CAMERA?
            (ATHENA_TWEEN3D_POSITION|ATHENA_TWEEN3D_LOOK):(ATHENA_TWEEN3D_POSITION|ATHENA_TWEEN3D_SCALE|ATHENA_TWEEN3D_ROTATION))?
            "Unsupported property for this target":"Tween3D needs finite values, duration >= 0 and delay >= 0");
    }
    r->ctx=ctx; r->resolve=funcs[0];
    athena_tween3d_set_end(t,on_end,r); athena_tween3d_set_owner(t,ctx);
    h->tween=t; h->promise=promise;
    obj=JS_NewObjectClass(ctx,handle_id);
    if(JS_IsException(obj)) { athena_tween3d_kill(t,0); athena_tween3d_release(t); JS_FreeValue(ctx,promise); js_free(ctx,h); return obj; }
    JS_SetOpaque(obj,h); return obj;
}
static JSValue handle_get(JSContext *ctx,JSValueConst self,int magic) {
    Handle *h=get_handle(ctx,self); if(!h) return JS_EXCEPTION;
    switch(magic) {
        case 0: return JS_NewBool(ctx,athena_tween3d_active(h->tween));
        case 1: return JS_NewBool(ctx,athena_tween3d_paused(h->tween));
        case 2: return JS_NewFloat64(ctx,athena_tween3d_progress(h->tween));
        default: return JS_DupValue(ctx,h->promise);
    }
}
static JSValue handle_control(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic) {
    Handle *h=get_handle(ctx,self); if(!h) return JS_EXCEPTION;
    if(magic==2) {
        if(!athena_js_argc(ctx,argc,0,1,"kill")) return JS_EXCEPTION;
        int complete=0;
        if(argc&&!JS_IsUndefined(argv[0])) {
            if(!JS_IsBool(argv[0])) return JS_ThrowTypeError(ctx,"kill(complete) takes a boolean");
            complete=JS_ToBool(ctx,argv[0]);
        }
        athena_tween3d_kill(h->tween,complete); return JS_UNDEFINED;
    }
    if(!athena_js_argc(ctx,argc,0,0,magic?"resume":"pause")) return JS_EXCEPTION;
    athena_tween3d_pause(h->tween,magic==0); return JS_DupValue(ctx,self);
}
/* Thenable: await handle resolves with true (completed) or false (killed). */
static JSValue handle_then(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    Handle *h=get_handle(ctx,self); if(!h) return JS_EXCEPTION;
    JSValue then=JS_GetPropertyStr(ctx,h->promise,"then"); if(JS_IsException(then)) return then;
    JSValue result=JS_Call(ctx,then,h->promise,argc,argv); JS_FreeValue(ctx,then); return result;
}
static JSValue kill_of(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,magic?1:2,magic?"Tween3D.isTweening":"Tween3D.killTweensOf")) return JS_EXCEPTION;
    AthenaTween3DTarget kind; void *target=resolve_target(ctx,argv[0],&kind); if(!target) return JS_EXCEPTION;
    if(magic) return JS_NewBool(ctx,athena_tween3d_count_target(target)>0);
    int complete=0;
    if(argc>1&&!JS_IsUndefined(argv[1])) {
        if(!JS_IsBool(argv[1])) return JS_ThrowTypeError(ctx,"complete must be a boolean");
        complete=JS_ToBool(ctx,argv[1]);
    }
    return JS_NewUint32(ctx,(uint32_t)athena_tween3d_kill_target(target,complete));
}
static JSValue advance(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,1,"Tween3D.advance")) return JS_EXCEPTION;
    float dt; if(!athena_js_float(ctx,argv[0],&dt,"dt")) return JS_EXCEPTION;
    if(dt<0) return JS_ThrowRangeError(ctx,"dt must not be negative");
    return JS_NewUint32(ctx,(uint32_t)athena_tween3d_advance(dt));
}
static JSValue attach_loop(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,0,1,"Tween3D.attachLoop")) return JS_EXCEPTION;
    float priority=ATHENA_TWEEN3D_LOOP_PRIORITY;
    if(argc==1&&!JS_IsUndefined(argv[0])&&!athena_js_float(ctx,argv[0],&priority,"priority")) return JS_EXCEPTION;
    if(priority!=(float)(int)priority) return JS_ThrowRangeError(ctx,"priority must be an integer");
    if(athena_tween3d_loop_system()) return JS_UNDEFINED;
    int id=athena_tween3d_attach_loop((int)priority,ctx);
    if(id==ATHENA_TWEEN3D_ENOMEM) return JS_ThrowOutOfMemory(ctx);
    return id<0?JS_ThrowInternalError(ctx,"Tween3D could not join the Loop"):JS_UNDEFINED;
}
static JSValue detach_loop(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Tween3D.detachLoop")) return JS_EXCEPTION;
    return JS_NewBool(ctx,athena_tween3d_detach_loop());
}
static JSValue attached(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Tween3D.isAttached")) return JS_EXCEPTION;
    return JS_NewBool(ctx,athena_tween3d_loop_system()!=0);
}
static JSClassDef handle_class={"Tween3D.Handle",.finalizer=handle_finalizer,.gc_mark=handle_mark};
static const JSCFunctionListEntry handle_methods[]={
    JS_CGETSET_MAGIC_DEF("active",handle_get,NULL,0),JS_CGETSET_MAGIC_DEF("paused",handle_get,NULL,1),
    JS_CGETSET_MAGIC_DEF("progress",handle_get,NULL,2),JS_CGETSET_MAGIC_DEF("finished",handle_get,NULL,3),
    JS_CFUNC_MAGIC_DEF("pause",0,handle_control,0),JS_CFUNC_MAGIC_DEF("resume",0,handle_control,1),
    JS_CFUNC_MAGIC_DEF("kill",0,handle_control,2),JS_CFUNC_DEF("then",2,handle_then)};
static const JSCFunctionListEntry exports[]={
    JS_CFUNC_DEF("to",3,tween_to),JS_CFUNC_MAGIC_DEF("killTweensOf",1,kill_of,0),
    JS_CFUNC_MAGIC_DEF("isTweening",1,kill_of,1),JS_CFUNC_DEF("advance",1,advance),
    JS_CFUNC_DEF("attachLoop",0,attach_loop),JS_CFUNC_DEF("detachLoop",0,detach_loop),
    JS_CFUNC_DEF("isAttached",0,attached),
    JS_PROP_INT32_DEF("LOOP_PRIORITY",ATHENA_TWEEN3D_LOOP_PRIORITY,JS_PROP_ENUMERABLE)};
static int init(JSContext *ctx,JSModuleDef *m) {
    if(athena_register_class(ctx,&handle_id,&handle_class)<0) return -1;
    JSValue proto=JS_NewObject(ctx); if(JS_IsException(proto)) return -1;
    JS_SetPropertyFunctionList(ctx,proto,handle_methods,countof(handle_methods));
    JS_SetClassProto(ctx,handle_id,proto);
    return JS_SetModuleExportList(ctx,m,exports,countof(exports));
}
void athena_tween3d_js_cleanup(JSContext *ctx) {
    athena_tween3d_detach_owner(ctx);
    cleaning=1; athena_tween3d_kill_owner(ctx); cleaning=0;
}
JSModuleDef *athena_tween3d_js_init(JSContext *ctx) {
    return athena_push_module(ctx,init,exports,countof(exports),"Tween3D");
}
