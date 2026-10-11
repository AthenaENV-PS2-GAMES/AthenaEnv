#include <athena_js_args.h>
#include <athena/js/lights.h>
#include "ath_lights.h"
static JSClassID lights_id;
AthenaLights *athena_lights_from_value(JSContext *ctx,JSValueConst value) {
    AthenaLights *l=JS_GetOpaque2(ctx,value,lights_id);
    if(!l) JS_ThrowTypeError(ctx,"Expected a live Lights.Set");
    return l;
}
static void finalizer(JSRuntime *rt,JSValue obj) { (void)rt; athena_lights_destroy(JS_GetOpaque(obj,lights_id)); }
static JSValue ctor(JSContext *ctx,JSValueConst target,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Lights.Set")) return JS_EXCEPTION;
    /* Read subclass prototype before acquiring any native resource. */
    JSValue proto=JS_GetPropertyStr(ctx,target,"prototype"); if(JS_IsException(proto)) return proto;
    JSValue obj=JS_NewObjectProtoClass(ctx,proto,lights_id); JS_FreeValue(ctx,proto);
    if(JS_IsException(obj)) return obj;
    AthenaLights *l=athena_lights_create();
    if(!l) { JS_FreeValue(ctx,obj); return JS_ThrowOutOfMemory(ctx); }
    JS_SetOpaque(obj,l); return obj;
}
static JSValue dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Lights.dispose")||!athena_js_class(ctx,self,lights_id)) return JS_EXCEPTION;
    AthenaLights *l=JS_GetOpaque(self,lights_id); JS_SetOpaque(self,NULL); athena_lights_destroy(l); return JS_UNDEFINED;
}
/* 0 setAmbient, 1 setDirectional, 2 disable, 3 setPoint, 4 disablePoint. */
static JSValue setter(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic) {
    int n=magic==0?3:magic==1?7:magic==3?8:1;
    if(!athena_js_argc(ctx,argc,n,n,"Lights setter")) return JS_EXCEPTION;
    float v[8]; for(int i=0;i<n;i++) if(!athena_js_float(ctx,argv[i],&v[i],"light")) return JS_EXCEPTION;
    uint32_t slots=magic>=3?ATHENA_LIGHTS_MAX_POINT:ATHENA_LIGHTS_MAX_DIRECTIONAL;
    if(magic!=0&&(v[0]<0||v[0]>=slots||floorf(v[0])!=v[0]))
        return JS_ThrowRangeError(ctx,"Light slot must be an integer in [0,%u]",(unsigned)slots-1);
    AthenaLights *l=athena_lights_from_value(ctx,self); if(!l) return JS_EXCEPTION;
    int ok=magic==0?athena_lights_set_ambient(l,v[0],v[1],v[2]):magic==1?
        athena_lights_set_directional(l,(uint32_t)v[0],v[1],v[2],v[3],v[4],v[5],v[6]):
        magic==3?athena_lights_set_point(l,(uint32_t)v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7]):
        magic==4?athena_lights_disable_point(l,(uint32_t)v[0]):athena_lights_disable(l,(uint32_t)v[0]);
    return ok?JS_DupValue(ctx,self):JS_ThrowRangeError(ctx,"Invalid light position, direction, range or RGB intensity");
}
/* setFog(start, end, r, g, b) and disableFog(). */
static JSValue set_fog(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,5,5,"Lights.setFog")) return JS_EXCEPTION;
    float v[5]; for(int i=0;i<5;i++) if(!athena_js_float(ctx,argv[i],&v[i],"fog")) return JS_EXCEPTION;
    AthenaLights *l=athena_lights_from_value(ctx,self); if(!l) return JS_EXCEPTION;
    if(!athena_lights_set_fog(l,v[0],v[1],v[2],v[3],v[4]))
        return JS_ThrowRangeError(ctx,"setFog: 0 <= start < end, RGB in [0,1]");
    return JS_DupValue(ctx,self);
}
static JSValue disable_fog(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Lights.disableFog")) return JS_EXCEPTION;
    AthenaLights *l=athena_lights_from_value(ctx,self); if(!l) return JS_EXCEPTION;
    athena_lights_disable_fog(l); return JS_DupValue(ctx,self);
}
static JSValue clear(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Lights.clear")) return JS_EXCEPTION;
    AthenaLights *l=athena_lights_from_value(ctx,self); if(!l) return JS_EXCEPTION;
    athena_lights_clear(l); return JS_DupValue(ctx,self);
}
static JSValue revision(JSContext *ctx,JSValueConst self) {
    AthenaLights *l=athena_lights_from_value(ctx,self); if(!l) return JS_EXCEPTION;
    AthenaLightsView view; athena_lights_view(l,&view); return JS_NewFloat64(ctx,(double)view.revision);
}
static JSClassDef class_def={"Lights.Set",.finalizer=finalizer};
static const JSCFunctionListEntry methods[]={
    JS_CFUNC_MAGIC_DEF("setAmbient",3,setter,0),JS_CFUNC_MAGIC_DEF("setDirectional",7,setter,1),
    JS_CFUNC_MAGIC_DEF("disable",1,setter,2),JS_CFUNC_MAGIC_DEF("setPoint",8,setter,3),
    JS_CFUNC_MAGIC_DEF("disablePoint",1,setter,4),JS_CFUNC_DEF("setFog",5,set_fog),
    JS_CFUNC_DEF("disableFog",0,disable_fog),JS_CFUNC_DEF("clear",0,clear),JS_CFUNC_DEF("dispose",0,dispose),
    JS_CGETSET_DEF("revision",revision,NULL)};
static const JSCFunctionListEntry exports[]={JS_PROP_INT32_DEF("MAX_DIRECTIONAL",ATHENA_LIGHTS_MAX_DIRECTIONAL,JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MAX_POINT",ATHENA_LIGHTS_MAX_POINT,JS_PROP_ENUMERABLE)};
static int init(JSContext *ctx,JSModuleDef *m) {
    if(athena_register_class(ctx,&lights_id,&class_def)<0) return -1;
    JSValue proto=JS_NewObject(ctx); if(JS_IsException(proto)) return -1;
    JS_SetPropertyFunctionList(ctx,proto,methods,countof(methods));
    JSValue cls=JS_NewCFunction2(ctx,ctor,"Set",0,JS_CFUNC_constructor,0);
    JS_SetConstructor(ctx,cls,proto); JS_SetClassProto(ctx,lights_id,proto);
    if(JS_SetModuleExport(ctx,m,"Set",cls)<0) return -1;
    return JS_SetModuleExportList(ctx,m,exports,countof(exports));
}
JSModuleDef *athena_lights_js_init(JSContext *ctx) {
    JSModuleDef *m=athena_push_module(ctx,init,exports,countof(exports),"Lights");
    if(m) JS_AddModuleExport(ctx,m,"Set");
    return m;
}
