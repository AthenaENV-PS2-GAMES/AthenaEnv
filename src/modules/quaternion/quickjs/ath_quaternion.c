#include <malloc.h>
#include <athena_js_args.h>
#include <athena/quaternion.h>
#include "ath_quaternion.h"
static JSClassID quaternion_id;
static AthenaQuaternion *get(JSContext *ctx,JSValueConst value) {
    AthenaQuaternion *q=JS_GetOpaque2(ctx,value,quaternion_id);
    if(!q) JS_ThrowTypeError(ctx,"Expected a live Quaternion");
    return q;
}
static void finalizer(JSRuntime *rt,JSValue value) { (void)rt; free(JS_GetOpaque(value,quaternion_id)); }
static JSValue ctor(JSContext *ctx,JSValueConst target,int argc,JSValueConst *argv) {
    if(argc!=0 && argc!=4) return JS_ThrowTypeError(ctx,"Quaternion expects zero or four xyzw numbers");
    AthenaQuaternion q;
    athena_quaternion_identity(&q);
    if(argc && (!athena_js_float(ctx,argv[0],&q.x,"x") || !athena_js_float(ctx,argv[1],&q.y,"y") ||
        !athena_js_float(ctx,argv[2],&q.z,"z") || !athena_js_float(ctx,argv[3],&q.w,"w"))) return JS_EXCEPTION;
    if(!athena_quaternion_normalize(&q,&q)) return JS_ThrowRangeError(ctx,"Quaternion cannot be zero");
    AthenaQuaternion *owned=memalign(16,sizeof(*owned));
    if(!owned) return JS_ThrowOutOfMemory(ctx);
    *owned=q;
    JSValue proto=JS_GetPropertyStr(ctx,target,"prototype");
    if(JS_IsException(proto)) { free(owned); return proto; }
    JSValue obj=JS_NewObjectProtoClass(ctx,proto,quaternion_id); JS_FreeValue(ctx,proto);
    if(JS_IsException(obj)) { free(owned); return obj; }
    JS_SetOpaque(obj,owned); return obj;
}
static JSValue dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Quaternion.dispose")||!athena_js_class(ctx,self,quaternion_id)) return JS_EXCEPTION;
    AthenaQuaternion *q=JS_GetOpaque(self,quaternion_id); JS_SetOpaque(self,NULL); free(q); return JS_UNDEFINED;
}
static JSValue to_array(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Quaternion.toArray")) return JS_EXCEPTION;
    AthenaQuaternion *q=get(ctx,self); if(!q) return JS_EXCEPTION;
    float data[4]={q->x,q->y,q->z,q->w};
    JSValue arr=JS_NewArray(ctx); if(JS_IsException(arr)) return arr;
    for(int i=0;i<4;i++) if(JS_SetPropertyUint32(ctx,arr,i,JS_NewFloat32(ctx,data[i]))<0) { JS_FreeValue(ctx,arr); return JS_EXCEPTION; }
    return arr;
}
static JSValue axis_angle(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,4,4,"Quaternion.setAxisAngle")) return JS_EXCEPTION;
    AthenaQuaternion *q=get(ctx,self); float v[4]; if(!q) return JS_EXCEPTION;
    for(int i=0;i<4;i++) if(!athena_js_float(ctx,argv[i],&v[i],"axis/angle")) return JS_EXCEPTION;
    if(!athena_quaternion_axis_angle(q,v[0],v[1],v[2],v[3])) return JS_ThrowRangeError(ctx,"Axis must be nonzero");
    return JS_DupValue(ctx,self);
}
static JSValue euler(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,3,3,"Quaternion.setEuler")) return JS_EXCEPTION;
    AthenaQuaternion *q=get(ctx,self); float v[3]; if(!q) return JS_EXCEPTION;
    for(int i=0;i<3;i++) if(!athena_js_float(ctx,argv[i],&v[i],"angle")) return JS_EXCEPTION;
    if(!athena_quaternion_euler(q,v[0],v[1],v[2])) return JS_ThrowRangeError(ctx,"Invalid Euler angles");
    return JS_DupValue(ctx,self);
}
static JSValue multiply(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"Quaternion.multiply")) return JS_EXCEPTION;
    AthenaQuaternion *q=get(ctx,self),*other=get(ctx,argv[0]); if(!q||!other) return JS_EXCEPTION;
    if(!athena_quaternion_multiply(q,q,other)) return JS_ThrowRangeError(ctx,"Invalid rotation");
    return JS_DupValue(ctx,self);
}
static JSValue slerp(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,2,2,"Quaternion.slerp")) return JS_EXCEPTION;
    AthenaQuaternion *q=get(ctx,self),*other=get(ctx,argv[0]); float t; if(!q||!other) return JS_EXCEPTION;
    if(!athena_js_float(ctx,argv[1],&t,"t")) return JS_EXCEPTION;
    if(!athena_quaternion_slerp(q,q,other,t)) return JS_ThrowRangeError(ctx,"t must be in [0,1]");
    return JS_DupValue(ctx,self);
}
static JSClassDef class_def={"Quaternion",.finalizer=finalizer};
static const JSCFunctionListEntry methods[]={
    JS_CFUNC_DEF("toArray",0,to_array),JS_CFUNC_DEF("setAxisAngle",4,axis_angle),JS_CFUNC_DEF("setEuler",3,euler),
    JS_CFUNC_DEF("multiply",1,multiply),JS_CFUNC_DEF("slerp",2,slerp),JS_CFUNC_DEF("dispose",0,dispose)};
static int init(JSContext *ctx,JSModuleDef *m) {
    if(athena_register_class(ctx,&quaternion_id,&class_def)<0) return -1;
    JSValue proto=JS_NewObject(ctx);
    if(JS_IsException(proto)) return -1;
    JS_SetPropertyFunctionList(ctx,proto,methods,countof(methods));
    JSValue cls=JS_NewCFunction2(ctx,ctor,"Quaternion",4,JS_CFUNC_constructor,0);
    JS_SetConstructor(ctx,cls,proto); JS_SetClassProto(ctx,quaternion_id,proto);
    return JS_SetModuleExport(ctx,m,"Quaternion",cls);
}
JSModuleDef *athena_quaternion_js_init(JSContext *ctx) {
    JSModuleDef *m=athena_push_module(ctx,init,NULL,0,"Quaternion");
    if(m) JS_AddModuleExport(ctx,m,"Quaternion");
    return m;
}
