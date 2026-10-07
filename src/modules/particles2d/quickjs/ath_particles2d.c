#include <athena_js_args.h>
#include <athena/particles2d.h>
#include <athena/js/image.h>
#include "ath_particles2d.h"
static JSClassID emitter_id;
/* The emitter borrows its Image at draw time, like Sprite: the handle keeps
 * the JS value and a freed image draws nothing. */
typedef struct { AthenaEmitter2D *emitter; JSValue image; } Emitter;
static Emitter *get_emitter(JSContext *ctx,JSValueConst v) {
    Emitter *e=JS_GetOpaque2(ctx,v,emitter_id);
    if(!e||!e->emitter) { JS_ThrowTypeError(ctx,"Expected a live Particles2D.Emitter"); return NULL; }
    return e;
}
static void finalizer(JSRuntime *rt,JSValue v) {
    Emitter *e=JS_GetOpaque(v,emitter_id); if(!e) return;
    athena_emitter2d_release(e->emitter); JS_FreeValueRT(rt,e->image); js_free_rt(rt,e);
}
static void mark(JSRuntime *rt,JSValueConst v,JS_MarkFunc *mark_func) {
    Emitter *e=JS_GetOpaque(v,emitter_id); if(e) JS_MarkValue(rt,e->image,mark_func);
}
/* options[key]: [count numbers], or one number for a [min, max] range when
 * single is set. 0 absent, 1 read, -1 error. */
static int numbers(JSContext *ctx,JSValueConst options,const char *key,float *out,int count,int single) {
    JSValue v=JS_GetPropertyStr(ctx,options,key); if(JS_IsException(v)) return -1;
    if(JS_IsUndefined(v)) return 0;
    int ok=1;
    if(JS_IsNumber(v)&&single) { ok=athena_js_float(ctx,v,&out[0],key); out[1]=out[0]; }
    else if(JS_IsObject(v)) for(int i=0;ok&&i<count;i++) {
        JSValue e=JS_GetPropertyUint32(ctx,v,i);
        ok=!JS_IsException(e)&&athena_js_float(ctx,e,&out[i],key);
        JS_FreeValue(ctx,e);
    } else { JS_ThrowTypeError(ctx,"options.%s must be a number or an array of %d numbers",key,count); ok=0; }
    JS_FreeValue(ctx,v);
    return ok?1:-1;
}
static int color_value(JSContext *ctx,JSValueConst v,uint32_t *out) {
    if(!JS_IsNumber(v)) { JS_ThrowTypeError(ctx,"options.color takes Color values"); return 0; }
    return JS_ToUint32(ctx,out,v)==0;
}
static int colors(JSContext *ctx,JSValueConst options,uint32_t *start,uint32_t *end) {
    JSValue v=JS_GetPropertyStr(ctx,options,"color"); if(JS_IsException(v)) return 0;
    int ok=1;
    if(JS_IsUndefined(v)) ok=1;
    else if(JS_IsNumber(v)) { ok=color_value(ctx,v,start); *end=*start; }
    else if(JS_IsObject(v)) {
        JSValue a=JS_GetPropertyUint32(ctx,v,0),b=JS_GetPropertyUint32(ctx,v,1);
        ok=!JS_IsException(a)&&!JS_IsException(b)&&color_value(ctx,a,start)&&color_value(ctx,b,end);
        JS_FreeValue(ctx,a); JS_FreeValue(ctx,b);
    } else { JS_ThrowTypeError(ctx,"options.color must be a Color or [start, end]"); ok=0; }
    JS_FreeValue(ctx,v); return ok;
}
static int read_options(JSContext *ctx,JSValueConst options,AthenaEmitter2DDesc *d) {
    if(JS_IsUndefined(options)) return 1;
    if(!JS_IsObject(options)) { JS_ThrowTypeError(ctx,"options must be an object"); return 0; }
    float capacity=d->capacity,seed=d->seed,pair[2],rect[4]={d->u1,d->v1,d->u2,d->v2};
    if(!athena_js_option_float(ctx,options,"capacity",&capacity)||!athena_js_option_float(ctx,options,"rate",&d->rate)||
        !athena_js_option_float(ctx,options,"angle",&d->angle)||!athena_js_option_float(ctx,options,"spread",&d->spread)||
        !athena_js_option_float(ctx,options,"drag",&d->drag)||!athena_js_option_float(ctx,options,"seed",&seed)) return 0;
    /* Ranges take one number; vectors (gravity, area) need both values. */
    struct { const char *key; float *a,*b; int single; } pairs[]={{"life",&d->life_min,&d->life_max,1},
        {"speed",&d->speed_min,&d->speed_max,1},{"size",&d->size_start,&d->size_end,1},
        {"rotation",&d->rotation_min,&d->rotation_max,1},{"spin",&d->spin_min,&d->spin_max,1},
        {"gravity",&d->gravity_x,&d->gravity_y,0},{"area",&d->area_width,&d->area_height,0}};
    for(unsigned i=0;i<countof(pairs);i++) {
        int got=numbers(ctx,options,pairs[i].key,pair,2,pairs[i].single);
        if(got<0) return 0;
        if(got) { *pairs[i].a=pair[0]; *pairs[i].b=pair[1]; }
    }
    int got=numbers(ctx,options,"rect",rect,4,0); if(got<0) return 0;
    d->u1=rect[0]; d->v1=rect[1]; d->u2=rect[2]; d->v2=rect[3];
    if(!colors(ctx,options,&d->color_start,&d->color_end)) return 0;
    if(capacity!=(float)(uint32_t)capacity||seed<0||seed!=(float)(uint32_t)seed) {
        JS_ThrowRangeError(ctx,"capacity and seed must be integers"); return 0;
    }
    d->capacity=(uint32_t)capacity; d->seed=(uint32_t)seed; return 1;
}
static JSValue invalid(JSContext *ctx) {
    return JS_ThrowRangeError(ctx,"Invalid emitter: capacity 1..%u, life > 0, min <= max, rate/drag/size/area >= 0",
        (unsigned)ATHENA_PARTICLES2D_MAX);
}
static JSValue ctor(JSContext *ctx,JSValueConst target,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,2,"Particles2D.Emitter")) return JS_EXCEPTION;
    if(!athena_image_from_value(ctx,argv[0])) return JS_EXCEPTION;
    AthenaEmitter2DDesc d; athena_emitter2d_defaults(&d);
    if(!read_options(ctx,argc>1?argv[1]:JS_UNDEFINED,&d)) return JS_EXCEPTION;
    AthenaEmitter2D *native=NULL; int code=athena_emitter2d_create(&d,&native);
    if(code==ATHENA_PARTICLES2D_ENOMEM) return JS_ThrowOutOfMemory(ctx);
    if(code<0) return invalid(ctx);
    Emitter *e=js_mallocz(ctx,sizeof(*e)); if(!e) { athena_emitter2d_release(native); return JS_EXCEPTION; }
    e->emitter=native; e->image=JS_DupValue(ctx,argv[0]);
    JSValue proto=JS_GetPropertyStr(ctx,target,"prototype");
    JSValue obj=JS_IsException(proto)?proto:JS_NewObjectProtoClass(ctx,proto,emitter_id);
    JS_FreeValue(ctx,proto);
    if(JS_IsException(obj)) { athena_emitter2d_release(native); JS_FreeValue(ctx,e->image); js_free(ctx,e); return obj; }
    JS_SetOpaque(obj,e); return obj;
}
static JSValue configure(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"Emitter.configure")) return JS_EXCEPTION;
    Emitter *e=get_emitter(ctx,self); if(!e) return JS_EXCEPTION;
    AthenaEmitter2DDesc d=*athena_emitter2d_desc(e->emitter);
    if(!read_options(ctx,argv[0],&d)) return JS_EXCEPTION;
    if(!(e=get_emitter(ctx,self))) return JS_EXCEPTION; /* getters may dispose it */
    int code=athena_emitter2d_configure(e->emitter,&d);
    if(code==ATHENA_PARTICLES2D_ENOMEM) return JS_ThrowOutOfMemory(ctx);
    return code<0?invalid(ctx):JS_DupValue(ctx,self);
}
static JSValue set_position(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    float x,y;
    if(!athena_js_argc(ctx,argc,2,2,"Emitter.setPosition")||!athena_js_float(ctx,argv[0],&x,"x")||
        !athena_js_float(ctx,argv[1],&y,"y")) return JS_EXCEPTION;
    Emitter *e=get_emitter(ctx,self); if(!e) return JS_EXCEPTION;
    athena_emitter2d_set_position(e->emitter,x,y); return JS_DupValue(ctx,self);
}
static JSValue emit(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    float n;
    if(!athena_js_argc(ctx,argc,1,1,"Emitter.emit")||!athena_js_float(ctx,argv[0],&n,"count")) return JS_EXCEPTION;
    if(n<0||n!=(float)(uint32_t)n) return JS_ThrowRangeError(ctx,"count must be a non-negative integer");
    Emitter *e=get_emitter(ctx,self); if(!e) return JS_EXCEPTION;
    return JS_NewUint32(ctx,athena_emitter2d_emit(e->emitter,(uint32_t)n));
}
static JSValue simple(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic) {
    Emitter *e;
    if(magic==1) { /* update(dt) */
        float dt;
        if(!athena_js_argc(ctx,argc,1,1,"Emitter.update")||!athena_js_float(ctx,argv[0],&dt,"dt")) return JS_EXCEPTION;
        if(dt<0) return JS_ThrowRangeError(ctx,"dt must not be negative");
        if(!(e=get_emitter(ctx,self))) return JS_EXCEPTION;
        athena_emitter2d_update(e->emitter,dt); return JS_DupValue(ctx,self);
    }
    if(!athena_js_argc(ctx,argc,0,0,magic?"Emitter.draw":"Emitter.clear")||!(e=get_emitter(ctx,self))) return JS_EXCEPTION;
    if(magic==0) athena_emitter2d_clear(e->emitter);
    else athena_emitter2d_draw(e->emitter,athena_image_peek(e->image)); /* NULL: freed image draws nothing */
    return JS_DupValue(ctx,self);
}
static JSValue get_count(JSContext *ctx,JSValueConst self) {
    Emitter *e=get_emitter(ctx,self); return e?JS_NewUint32(ctx,athena_emitter2d_count(e->emitter)):JS_EXCEPTION;
}
static JSValue get_active(JSContext *ctx,JSValueConst self) {
    Emitter *e=get_emitter(ctx,self); return e?JS_NewBool(ctx,athena_emitter2d_active(e->emitter)):JS_EXCEPTION;
}
static JSValue set_active(JSContext *ctx,JSValueConst self,JSValueConst value) {
    if(!JS_IsBool(value)) return JS_ThrowTypeError(ctx,"active must be a boolean");
    Emitter *e=get_emitter(ctx,self); if(!e) return JS_EXCEPTION;
    athena_emitter2d_set_active(e->emitter,JS_ToBool(ctx,value)); return JS_UNDEFINED;
}
static JSValue dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Emitter.dispose")||!athena_js_class(ctx,self,emitter_id)) return JS_EXCEPTION;
    Emitter *e=JS_GetOpaque(self,emitter_id);
    if(e&&e->emitter) { athena_emitter2d_release(e->emitter); e->emitter=NULL; JS_FreeValue(ctx,e->image); e->image=JS_UNDEFINED; }
    return JS_UNDEFINED;
}
static JSValue update_all(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; float dt;
    if(!athena_js_argc(ctx,argc,1,1,"Particles2D.update")||!athena_js_float(ctx,argv[0],&dt,"dt")) return JS_EXCEPTION;
    if(dt<0) return JS_ThrowRangeError(ctx,"dt must not be negative");
    athena_particles2d_update(dt); return JS_UNDEFINED;
}
static JSValue attach_loop(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,0,1,"Particles2D.attachLoop")) return JS_EXCEPTION;
    float priority=ATHENA_PARTICLES2D_LOOP_PRIORITY;
    if(argc==1&&!JS_IsUndefined(argv[0])&&!athena_js_float(ctx,argv[0],&priority,"priority")) return JS_EXCEPTION;
    if(priority!=(float)(int)priority) return JS_ThrowRangeError(ctx,"priority must be an integer");
    if(athena_particles2d_loop_system()) return JS_UNDEFINED;
    int id=athena_particles2d_attach_loop((int)priority,ctx);
    if(id==ATHENA_PARTICLES2D_ENOMEM) return JS_ThrowOutOfMemory(ctx);
    return id<0?JS_ThrowInternalError(ctx,"Particles2D could not join the Loop"):JS_UNDEFINED;
}
static JSValue detach_loop(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Particles2D.detachLoop")) return JS_EXCEPTION;
    return JS_NewBool(ctx,athena_particles2d_detach_loop());
}
static JSValue attached(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Particles2D.isAttached")) return JS_EXCEPTION;
    return JS_NewBool(ctx,athena_particles2d_loop_system()!=0);
}
static JSClassDef emitter_class={"Particles2D.Emitter",.finalizer=finalizer,.gc_mark=mark};
static const JSCFunctionListEntry methods[]={
    JS_CFUNC_DEF("configure",1,configure),JS_CFUNC_DEF("setPosition",2,set_position),JS_CFUNC_DEF("emit",1,emit),
    JS_CFUNC_MAGIC_DEF("clear",0,simple,0),JS_CFUNC_MAGIC_DEF("update",1,simple,1),JS_CFUNC_MAGIC_DEF("draw",0,simple,2),
    JS_CGETSET_DEF("count",get_count,NULL),JS_CGETSET_DEF("active",get_active,set_active),
    JS_CFUNC_DEF("dispose",0,dispose)};
static const JSCFunctionListEntry exports[]={
    JS_CFUNC_DEF("update",1,update_all),JS_CFUNC_DEF("attachLoop",0,attach_loop),
    JS_CFUNC_DEF("detachLoop",0,detach_loop),JS_CFUNC_DEF("isAttached",0,attached),
    JS_PROP_INT32_DEF("MAX_PARTICLES",ATHENA_PARTICLES2D_MAX,JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("LOOP_PRIORITY",ATHENA_PARTICLES2D_LOOP_PRIORITY,JS_PROP_ENUMERABLE)};
static int init(JSContext *ctx,JSModuleDef *m) {
    if(athena_register_class(ctx,&emitter_id,&emitter_class)<0) return -1;
    JSValue proto=JS_NewObject(ctx); if(JS_IsException(proto)) return -1;
    JS_SetPropertyFunctionList(ctx,proto,methods,countof(methods));
    JSValue cls=JS_NewCFunction2(ctx,ctor,"Emitter",2,JS_CFUNC_constructor,0);
    if(JS_IsException(cls)) { JS_FreeValue(ctx,proto); return -1; }
    JS_SetConstructor(ctx,cls,proto); JS_SetClassProto(ctx,emitter_id,proto);
    if(JS_SetModuleExport(ctx,m,"Emitter",cls)<0) return -1;
    return JS_SetModuleExportList(ctx,m,exports,countof(exports));
}
void athena_particles2d_js_cleanup(JSContext *ctx) { athena_particles2d_detach_owner(ctx); }
JSModuleDef *athena_particles2d_js_init(JSContext *ctx) {
    JSModuleDef *m=athena_push_module(ctx,init,exports,countof(exports),"Particles2D");
    if(m) JS_AddModuleExport(ctx,m,"Emitter");
    return m;
}
