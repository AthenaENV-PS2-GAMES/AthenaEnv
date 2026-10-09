#include <athena_js_args.h>
#include <athena/triggers3d.h>
#include <athena/js/scene3d.h>
#include "ath_triggers3d.h"
/* Id-based binding; the Triggers3D JavaScript module builds the object API on it. */
static JSClassID world_id;
static AthenaTriggers3D *get_world(JSContext *ctx,JSValueConst v) {
    AthenaTriggers3D *t=JS_GetOpaque2(ctx,v,world_id);
    if(!t) JS_ThrowTypeError(ctx,"Expected a live Triggers3D world");
    return t;
}
static void finalizer(JSRuntime *rt,JSValue v) { (void)rt; athena_triggers3d_destroy(JS_GetOpaque(v,world_id)); }
static JSValue ctor(JSContext *ctx,JSValueConst target,int argc,JSValueConst *argv) {
    (void)argc; (void)argv;
    JSValue proto=JS_GetPropertyStr(ctx,target,"prototype"); if(JS_IsException(proto)) return proto;
    JSValue obj=JS_NewObjectProtoClass(ctx,proto,world_id); JS_FreeValue(ctx,proto);
    if(JS_IsException(obj)) return obj;
    AthenaTriggers3D *t=athena_triggers3d_create();
    if(!t) { JS_FreeValue(ctx,obj); return JS_ThrowOutOfMemory(ctx); }
    JS_SetOpaque(obj,t); return obj;
}
static JSValue dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argc; (void)argv;
    if(!athena_js_class(ctx,self,world_id)) return JS_EXCEPTION;
    AthenaTriggers3D *t=JS_GetOpaque(self,world_id); JS_SetOpaque(self,NULL); athena_triggers3d_destroy(t);
    return JS_UNDEFINED;
}
static int floats(JSContext *ctx,JSValueConst *argv,int n,float *out) {
    for(int i=0;i<n;i++) if(!athena_js_float(ctx,argv[i],&out[i],"coordinate")) return 0;
    return 1;
}
static int int_arg(JSContext *ctx,JSValueConst v,int32_t *out) { return JS_ToInt32(ctx,out,v)==0; }
static int u32_arg(JSContext *ctx,JSValueConst v,uint32_t *out) { return JS_ToUint32(ctx,out,v)==0; }
static JSValue code(JSContext *ctx,int c,const char *what) {
    if(c<0) return JS_ThrowRangeError(ctx,"%s: invalid arguments, unknown id or too many (%u)",what,ATHENA_TRIGGERS3D_MAX);
    return JS_NewInt32(ctx,c);
}
/* addZone(shape, ax, ay, az, bx, by, bz, mask) */
static JSValue add_zone(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,8,8,"addZone")) return JS_EXCEPTION;
    int32_t shape; float f[6]; uint32_t mask;
    if(!int_arg(ctx,argv[0],&shape)||!floats(ctx,argv+1,6,f)||!u32_arg(ctx,argv[7],&mask)) return JS_EXCEPTION;
    AthenaTriggers3D *t=get_world(ctx,self); if(!t) return JS_EXCEPTION;
    return code(ctx,athena_triggers3d_add_zone(t,(AthenaTrigger3DShape)shape,f,f+3,mask),"Triggers3D zone");
}
static JSValue set_zone(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,8,8,"setZone")) return JS_EXCEPTION;
    int32_t id,shape; float f[6];
    if(!int_arg(ctx,argv[0],&id)||!int_arg(ctx,argv[1],&shape)||!floats(ctx,argv+2,6,f)) return JS_EXCEPTION;
    AthenaTriggers3D *t=get_world(ctx,self); if(!t) return JS_EXCEPTION;
    return code(ctx,athena_triggers3d_set_zone(t,id,(AthenaTrigger3DShape)shape,f,f+3),"Triggers3D zone");
}
/* addBody(x, y, z, radius, layers) */
static JSValue add_body(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,5,5,"addBody")) return JS_EXCEPTION;
    float f[4]; uint32_t layers;
    if(!floats(ctx,argv,4,f)||!u32_arg(ctx,argv[4],&layers)) return JS_EXCEPTION;
    AthenaTriggers3D *t=get_world(ctx,self); if(!t) return JS_EXCEPTION;
    return code(ctx,athena_triggers3d_add_body(t,f,f[3],layers),"Triggers3D body");
}
static JSValue set_body(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,5,5,"setBody")) return JS_EXCEPTION;
    int32_t id; float f[4];
    if(!int_arg(ctx,argv[0],&id)||!floats(ctx,argv+1,4,f)) return JS_EXCEPTION;
    AthenaTriggers3D *t=get_world(ctx,self); if(!t) return JS_EXCEPTION;
    return code(ctx,athena_triggers3d_set_body(t,id,f,f[3]),"Triggers3D body");
}
/* Id operations by magic: 0 removeZone, 1 removeBody. */
static JSValue remove_fn(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic) {
    if(!athena_js_argc(ctx,argc,1,1,"remove")) return JS_EXCEPTION;
    int32_t id; if(!int_arg(ctx,argv[0],&id)) return JS_EXCEPTION;
    AthenaTriggers3D *t=get_world(ctx,self); if(!t) return JS_EXCEPTION;
    return code(ctx,magic?athena_triggers3d_remove_body(t,id):athena_triggers3d_remove_zone(t,id),"Triggers3D remove");
}
/* Id + number by magic: 0 setZoneEnabled, 1 setMask, 2 setLayers. */
static JSValue set_value(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic) {
    if(!athena_js_argc(ctx,argc,2,2,"set")) return JS_EXCEPTION;
    int32_t id; uint32_t v;
    if(!int_arg(ctx,argv[0],&id)) return JS_EXCEPTION;
    if(magic==0) v=(uint32_t)JS_ToBool(ctx,argv[1]); else if(!u32_arg(ctx,argv[1],&v)) return JS_EXCEPTION;
    AthenaTriggers3D *t=get_world(ctx,self); if(!t) return JS_EXCEPTION;
    int c=magic==0?athena_triggers3d_set_zone_enabled(t,id,(int)v):magic==1?athena_triggers3d_set_mask(t,id,v):athena_triggers3d_set_layers(t,id,v);
    return code(ctx,c,"Triggers3D set");
}
/* zoneFollow/bodyFollow(id, node | null, ox, oy, oz) by magic 0/1. */
static JSValue follow_fn(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic) {
    if(!athena_js_argc(ctx,argc,5,5,"follow")) return JS_EXCEPTION;
    int32_t id; float off[3]; AthenaNode3D *node=NULL;
    if(!int_arg(ctx,argv[0],&id)||!floats(ctx,argv+2,3,off)) return JS_EXCEPTION;
    if(!JS_IsNull(argv[1])&&!(node=athena_node3d_from_value(ctx,argv[1]))) return JS_EXCEPTION;
    AthenaTriggers3D *t=get_world(ctx,self); if(!t) return JS_EXCEPTION;
    return code(ctx,magic?athena_triggers3d_body_follow(t,id,node,off):athena_triggers3d_zone_follow(t,id,node,off),"Triggers3D follow");
}
/* update(out: Int32Array): events as type, zone, body triples (as many as
 * fit); returns the total count (grow out and call readEvents() for more). */
static JSValue copy_events(JSContext *ctx,AthenaTriggers3D *t,JSValueConst out) {
    AthenaJSArray a;
    if(!athena_js_array(ctx,out,JS_TYPED_ARRAY_INT32,&a,"out")) return JS_EXCEPTION;
    uint32_t n; const AthenaTrigger3DEvent *e=athena_triggers3d_events(t,&n);
    int32_t *d=a.data;
    for(uint32_t i=0;i<n&&(size_t)i*3+2<a.count;i++) { d[i*3]=e[i].type; d[i*3+1]=e[i].zone; d[i*3+2]=e[i].body; }
    JS_FreeValue(ctx,a.backing);
    return JS_NewUint32(ctx,n);
}
static JSValue update(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"update")) return JS_EXCEPTION;
    AthenaTriggers3D *t=get_world(ctx,self); if(!t) return JS_EXCEPTION;
    if(athena_triggers3d_update(t)<0) return JS_ThrowOutOfMemory(ctx);
    return copy_events(ctx,t,argv[0]);
}
static JSValue read_events(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"readEvents")) return JS_EXCEPTION;
    AthenaTriggers3D *t=get_world(ctx,self); if(!t) return JS_EXCEPTION;
    return copy_events(ctx,t,argv[0]);
}
static JSValue inside(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,2,2,"inside")) return JS_EXCEPTION;
    int32_t z,b; if(!int_arg(ctx,argv[0],&z)||!int_arg(ctx,argv[1],&b)) return JS_EXCEPTION;
    AthenaTriggers3D *t=get_world(ctx,self); if(!t) return JS_EXCEPTION;
    return JS_NewBool(ctx,athena_triggers3d_inside(t,z,b));
}
static JSValue occupants(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"occupants")) return JS_EXCEPTION;
    int32_t z; if(!int_arg(ctx,argv[0],&z)) return JS_EXCEPTION;
    AthenaTriggers3D *t=get_world(ctx,self); if(!t) return JS_EXCEPTION;
    uint16_t ids[ATHENA_TRIGGERS3D_MAX];
    int n=athena_triggers3d_occupants(t,z,ids,ATHENA_TRIGGERS3D_MAX);
    if(n<0) return JS_ThrowRangeError(ctx,"occupants: unknown zone");
    JSValue arr=JS_NewArray(ctx);
    for(int i=0;i<n&&!JS_IsException(arr);i++) JS_SetPropertyUint32(ctx,arr,(uint32_t)i,JS_NewInt32(ctx,ids[i]));
    return arr;
}
static JSClassDef class_def={"Triggers3DNative.World",.finalizer=finalizer};
static const JSCFunctionListEntry methods[]={
    JS_CFUNC_DEF("addZone",8,add_zone),JS_CFUNC_DEF("setZone",8,set_zone),JS_CFUNC_DEF("addBody",5,add_body),
    JS_CFUNC_DEF("setBody",5,set_body),JS_CFUNC_MAGIC_DEF("removeZone",1,remove_fn,0),JS_CFUNC_MAGIC_DEF("removeBody",1,remove_fn,1),
    JS_CFUNC_MAGIC_DEF("setZoneEnabled",2,set_value,0),JS_CFUNC_MAGIC_DEF("setMask",2,set_value,1),
    JS_CFUNC_MAGIC_DEF("setLayers",2,set_value,2),JS_CFUNC_MAGIC_DEF("zoneFollow",5,follow_fn,0),
    JS_CFUNC_MAGIC_DEF("bodyFollow",5,follow_fn,1),JS_CFUNC_DEF("update",1,update),JS_CFUNC_DEF("readEvents",1,read_events),
    JS_CFUNC_DEF("inside",2,inside),JS_CFUNC_DEF("occupants",1,occupants),JS_CFUNC_DEF("dispose",0,dispose)};
static const JSCFunctionListEntry exports[]={
    JS_PROP_INT32_DEF("MAX",ATHENA_TRIGGERS3D_MAX,JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("ENTER",ATHENA_TRIGGER3D_ENTER,JS_PROP_ENUMERABLE),JS_PROP_INT32_DEF("EXIT",ATHENA_TRIGGER3D_EXIT,JS_PROP_ENUMERABLE)};
static int init(JSContext *ctx,JSModuleDef *m) {
    if(athena_register_class(ctx,&world_id,&class_def)<0) return -1;
    JSValue proto=JS_NewObject(ctx); if(JS_IsException(proto)) return -1;
    JS_SetPropertyFunctionList(ctx,proto,methods,countof(methods));
    JSValue cls=JS_NewCFunction2(ctx,ctor,"World",0,JS_CFUNC_constructor,0);
    JS_SetConstructor(ctx,cls,proto); JS_SetClassProto(ctx,world_id,proto);
    if(JS_SetModuleExport(ctx,m,"World",cls)<0) return -1;
    return JS_SetModuleExportList(ctx,m,exports,countof(exports));
}
JSModuleDef *athena_triggers3d_js_init(JSContext *ctx) {
    JSModuleDef *m=athena_push_module(ctx,init,exports,countof(exports),"Triggers3DNative");
    if(m) JS_AddModuleExport(ctx,m,"World");
    return m;
}
