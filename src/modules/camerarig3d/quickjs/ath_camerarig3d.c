#include <athena_js_args.h>
#include <athena/camerarig3d.h>
#include <athena/js/camera3d.h>
#include <athena/js/scene3d.h>
#include "ath_camerarig3d.h"
/* One class per rig kind; both share the native type and most methods. */
static JSClassID follow_id,orbit_id;
/* Rigs move their camera by themselves: kept until dispose(). */
static AthenaJSKept *kept_rigs;
static AthenaCameraRig3D *get_rig(JSContext *ctx,JSValueConst v) {
    JSClassID id=JS_GetClassID(v);
    AthenaCameraRig3D *r=(id==follow_id||id==orbit_id)?JS_GetOpaque(v,id):NULL;
    if(!r) JS_ThrowTypeError(ctx,"Expected a live CameraRig3D rig");
    return r;
}
static AthenaCameraRig3D *get_orbit(JSContext *ctx,JSValueConst v) {
    AthenaCameraRig3D *r=JS_GetOpaque2(ctx,v,orbit_id);
    if(!r) JS_ThrowTypeError(ctx,"Expected a live CameraRig3D.Orbit");
    return r;
}
static void finalizer(JSRuntime *rt,JSValue v) {
    (void)rt; JSClassID id=JS_GetClassID(v); athena_rig3d_release(JS_GetOpaque(v,id));
}
/* null or undefined: no target. */
static int optional_node(JSContext *ctx,JSValueConst v,AthenaNode3D **out) {
    *out=NULL;
    if(JS_IsUndefined(v)||JS_IsNull(v)) return 1;
    return (*out=athena_node3d_from_value(ctx,v))!=NULL;
}
static JSValue ctor(JSContext *ctx,JSValueConst target,int argc,JSValueConst *argv,int magic) {
    const char *name=magic?"CameraRig3D.Orbit":"CameraRig3D.Follow";
    if(!athena_js_argc(ctx,argc,magic?1:2,2,name)) return JS_EXCEPTION;
    AthenaCamera3D *camera=athena_camera3d_from_value(ctx,argv[0]); if(!camera) return JS_EXCEPTION;
    AthenaNode3D *node; if(!optional_node(ctx,argc>1?argv[1]:JS_UNDEFINED,&node)) return JS_EXCEPTION;
    if(!magic&&!node) return JS_ThrowTypeError(ctx,"CameraRig3D.Follow needs a Scene3D.Node target");
    JSValue proto=JS_GetPropertyStr(ctx,target,"prototype"); if(JS_IsException(proto)) return proto;
    JSValue obj=JS_NewObjectProtoClass(ctx,proto,magic?orbit_id:follow_id); JS_FreeValue(ctx,proto);
    if(JS_IsException(obj)) return obj;
    /* The rig keeps the camera after its handle is disposed. */
    athena_camera3d_js_retain(camera);
    AthenaCameraRig3D *r=magic?athena_rig3d_orbit_create(camera,athena_camera3d_js_release,node):
        athena_rig3d_follow_create(camera,athena_camera3d_js_release,node);
    if(!r) { athena_camera3d_js_release(camera); JS_FreeValue(ctx,obj); return JS_ThrowOutOfMemory(ctx); }
    JS_SetOpaque(obj,r);
    if(!athena_js_keep(ctx,&kept_rigs,obj)) { JS_FreeValue(ctx,obj); return JS_EXCEPTION; }
    return obj;
}
static int floats(JSContext *ctx,int argc,JSValueConst *argv,int count,float *v,const char *name) {
    if(!athena_js_argc(ctx,argc,count,count,name)) return 0;
    for(int i=0;i<count;i++) if(!athena_js_float(ctx,argv[i],&v[i],name)) return 0;
    return 1;
}
/* Shared numeric setters; magic selects the operation. */
static JSValue set_numbers(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic) {
    static const char *const names[]={"setOffset","setLookOffset","setSharpness","setCenter","setAngles",
        "rotate","zoom","setLimits"};
    static const int counts[]={3,3,2,3,2,2,1,4};
    float v[4];
    int count=counts[magic];
    /* setOffset(x, y, z, local = true) takes an optional fourth argument. */
    int local=1;
    if(magic==0&&argc==4) {
        if(!JS_IsBool(argv[3])) return JS_ThrowTypeError(ctx,"setOffset: local must be a boolean");
        local=JS_ToBool(ctx,argv[3]); argc=3;
    }
    if(!floats(ctx,argc,argv,count,v,names[magic])) return JS_EXCEPTION;
    AthenaCameraRig3D *r=magic>=3?get_orbit(ctx,self):get_rig(ctx,self); if(!r) return JS_EXCEPTION;
    int code;
    switch(magic) {
        case 0: code=athena_rig3d_set_offset(r,v[0],v[1],v[2],local); break;
        case 1: code=athena_rig3d_set_look_offset(r,v[0],v[1],v[2]); break;
        case 2: code=athena_rig3d_set_sharpness(r,v[0],v[1]); break;
        case 3: code=athena_rig3d_set_center(r,v[0],v[1],v[2]); break;
        case 4: code=athena_rig3d_set_angles(r,v[0],v[1]); break;
        case 5: code=athena_rig3d_rotate(r,v[0],v[1]); break;
        case 6: code=athena_rig3d_zoom(r,v[0]); break;
        default: code=athena_rig3d_set_limits(r,v[0],v[1],v[2],v[3]); break;
    }
    if(code<0) return JS_ThrowRangeError(ctx,"%s: invalid values",names[magic]);
    return JS_DupValue(ctx,self);
}
static JSValue set_target(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"setTarget")) return JS_EXCEPTION;
    AthenaNode3D *node; if(!optional_node(ctx,argv[0],&node)) return JS_EXCEPTION;
    AthenaCameraRig3D *r=get_rig(ctx,self); if(!r) return JS_EXCEPTION;
    athena_rig3d_set_target(r,node); return JS_DupValue(ctx,self);
}
static JSValue snap(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"snap")) return JS_EXCEPTION;
    AthenaCameraRig3D *r=get_rig(ctx,self); if(!r) return JS_EXCEPTION;
    athena_rig3d_snap(r); return JS_DupValue(ctx,self);
}
static JSValue update(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    float dt; if(!floats(ctx,argc,argv,1,&dt,"update")) return JS_EXCEPTION;
    AthenaCameraRig3D *r=get_rig(ctx,self); if(!r) return JS_EXCEPTION;
    if(dt<0) return JS_ThrowRangeError(ctx,"dt must not be negative");
    return JS_NewBool(ctx,athena_rig3d_update(r,dt)==1);
}
static JSValue get_enabled(JSContext *ctx,JSValueConst self) {
    AthenaCameraRig3D *r=get_rig(ctx,self); return r?JS_NewBool(ctx,athena_rig3d_enabled(r)):JS_EXCEPTION;
}
static JSValue set_enabled(JSContext *ctx,JSValueConst self,JSValueConst value) {
    if(!JS_IsBool(value)) return JS_ThrowTypeError(ctx,"enabled must be a boolean");
    AthenaCameraRig3D *r=get_rig(ctx,self); if(!r) return JS_EXCEPTION;
    athena_rig3d_set_enabled(r,JS_ToBool(ctx,value)); return JS_UNDEFINED;
}
/* Orbit getters/setters: 0 yaw, 1 pitch, 2 distance, 3 autoRotate. */
static JSValue orbit_get(JSContext *ctx,JSValueConst self,int magic) {
    AthenaCameraRig3D *r=get_orbit(ctx,self); if(!r) return JS_EXCEPTION;
    float v[3]; athena_rig3d_get_angles(r,&v[0],&v[1],&v[2]);
    if(magic==3) return JS_ThrowTypeError(ctx,"autoRotate is write-only");
    return JS_NewFloat64(ctx,v[magic]);
}
static JSValue orbit_set(JSContext *ctx,JSValueConst self,JSValueConst value,int magic) {
    float f; if(!athena_js_float(ctx,value,&f,magic==2?"distance":"autoRotate")) return JS_EXCEPTION;
    AthenaCameraRig3D *r=get_orbit(ctx,self); if(!r) return JS_EXCEPTION;
    int code=magic==2?athena_rig3d_set_distance(r,f):athena_rig3d_set_auto_rotate(r,f);
    return code<0?JS_ThrowRangeError(ctx,"Invalid value"):JS_UNDEFINED;
}
static JSValue dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"dispose")) return JS_EXCEPTION;
    JSClassID id=JS_GetClassID(self);
    if(id!=follow_id&&id!=orbit_id) return JS_ThrowTypeError(ctx,"Invalid object class");
    AthenaCameraRig3D *r=JS_GetOpaque(self,id);
    if(r) athena_js_unkeep(ctx,&kept_rigs,r,id);
    JS_SetOpaque(self,NULL); athena_rig3d_release(r);
    return JS_UNDEFINED;
}
static JSValue update_all(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; float dt; if(!floats(ctx,argc,argv,1,&dt,"CameraRig3D.update")) return JS_EXCEPTION;
    if(dt<0) return JS_ThrowRangeError(ctx,"dt must not be negative");
    return JS_NewUint32(ctx,(uint32_t)athena_rig3d_update_all(dt));
}
static JSValue attach_loop(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,0,1,"CameraRig3D.attachLoop")) return JS_EXCEPTION;
    float priority=ATHENA_RIG3D_LOOP_PRIORITY;
    if(argc==1&&!JS_IsUndefined(argv[0])&&!athena_js_float(ctx,argv[0],&priority,"priority")) return JS_EXCEPTION;
    if(priority!=(float)(int)priority) return JS_ThrowRangeError(ctx,"priority must be an integer");
    if(athena_rig3d_loop_system()) return JS_UNDEFINED;
    int id=athena_rig3d_attach_loop((int)priority,ctx);
    if(id==ATHENA_RIG3D_ENOMEM) return JS_ThrowOutOfMemory(ctx);
    return id<0?JS_ThrowInternalError(ctx,"CameraRig3D could not join the Loop"):JS_UNDEFINED;
}
static JSValue detach_loop(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"CameraRig3D.detachLoop")) return JS_EXCEPTION;
    return JS_NewBool(ctx,athena_rig3d_detach_loop());
}
static JSValue attached(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"CameraRig3D.isAttached")) return JS_EXCEPTION;
    return JS_NewBool(ctx,athena_rig3d_loop_system()!=0);
}
static JSClassDef follow_class={"CameraRig3D.Follow",.finalizer=finalizer};
static JSClassDef orbit_class={"CameraRig3D.Orbit",.finalizer=finalizer};
#define COMMON_METHODS \
    JS_CFUNC_DEF("setTarget",1,set_target),JS_CFUNC_MAGIC_DEF("setSharpness",2,set_numbers,2), \
    JS_CFUNC_DEF("snap",0,snap),JS_CFUNC_DEF("update",1,update), \
    JS_CGETSET_DEF("enabled",get_enabled,set_enabled),JS_CFUNC_DEF("dispose",0,dispose)
static const JSCFunctionListEntry follow_methods[]={COMMON_METHODS,
    JS_CFUNC_MAGIC_DEF("setOffset",3,set_numbers,0),JS_CFUNC_MAGIC_DEF("setLookOffset",3,set_numbers,1)};
static const JSCFunctionListEntry orbit_methods[]={COMMON_METHODS,
    JS_CFUNC_MAGIC_DEF("setCenter",3,set_numbers,3),JS_CFUNC_MAGIC_DEF("setAngles",2,set_numbers,4),
    JS_CFUNC_MAGIC_DEF("rotate",2,set_numbers,5),JS_CFUNC_MAGIC_DEF("zoom",1,set_numbers,6),
    JS_CFUNC_MAGIC_DEF("setLimits",4,set_numbers,7),
    JS_CGETSET_MAGIC_DEF("yaw",orbit_get,NULL,0),JS_CGETSET_MAGIC_DEF("pitch",orbit_get,NULL,1),
    JS_CGETSET_MAGIC_DEF("distance",orbit_get,orbit_set,2),JS_CGETSET_MAGIC_DEF("autoRotate",NULL,orbit_set,3)};
static const JSCFunctionListEntry exports[]={
    JS_CFUNC_DEF("update",1,update_all),JS_CFUNC_DEF("attachLoop",0,attach_loop),
    JS_CFUNC_DEF("detachLoop",0,detach_loop),JS_CFUNC_DEF("isAttached",0,attached),
    JS_PROP_INT32_DEF("LOOP_PRIORITY",ATHENA_RIG3D_LOOP_PRIORITY,JS_PROP_ENUMERABLE)};
static int define_class(JSContext *ctx,JSModuleDef *m,JSClassID id,const char *name,int magic,
    const JSCFunctionListEntry *methods,int count) {
    JSValue proto=JS_NewObject(ctx); if(JS_IsException(proto)) return -1;
    JS_SetPropertyFunctionList(ctx,proto,methods,count);
    JSValue cls=JS_NewCFunctionMagic(ctx,ctor,name,2,JS_CFUNC_constructor_magic,magic);
    if(JS_IsException(cls)) { JS_FreeValue(ctx,proto); return -1; }
    JS_SetConstructor(ctx,cls,proto); JS_SetClassProto(ctx,id,proto);
    return JS_SetModuleExport(ctx,m,name,cls);
}
static int init(JSContext *ctx,JSModuleDef *m) {
    if(athena_register_class(ctx,&follow_id,&follow_class)<0||athena_register_class(ctx,&orbit_id,&orbit_class)<0) return -1;
    if(define_class(ctx,m,follow_id,"Follow",0,follow_methods,countof(follow_methods))<0||
        define_class(ctx,m,orbit_id,"Orbit",1,orbit_methods,countof(orbit_methods))<0) return -1;
    return JS_SetModuleExportList(ctx,m,exports,countof(exports));
}
void athena_camerarig3d_js_cleanup(JSContext *ctx) {
    athena_rig3d_detach_owner(ctx); athena_js_keep_free(ctx,&kept_rigs);
}
JSModuleDef *athena_camerarig3d_js_init(JSContext *ctx) {
    JSModuleDef *m=athena_push_module(ctx,init,exports,countof(exports),"CameraRig3D");
    if(m) { JS_AddModuleExport(ctx,m,"Follow"); JS_AddModuleExport(ctx,m,"Orbit"); }
    return m;
}
