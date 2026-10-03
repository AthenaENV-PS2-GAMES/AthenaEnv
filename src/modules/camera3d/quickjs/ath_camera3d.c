#include <malloc.h>
#include <athena_js_args.h>
#include <athena/js/camera3d.h>
#include <athena/js/matrix4.h>
#include "ath_camera3d.h"
static JSClassID camera_id;
AthenaCamera3D *athena_camera3d_from_value(JSContext *ctx,JSValueConst value) {
    AthenaCamera3D *c=JS_GetOpaque2(ctx,value,camera_id);
    if(!c) JS_ThrowTypeError(ctx,"Expected a live Camera3D.Camera");
    return c;
}
static void finalizer(JSRuntime *rt,JSValue value) { (void)rt; free(JS_GetOpaque(value,camera_id)); }
static int projection(JSContext *ctx,AthenaCamera3D *c,JSValueConst options) {
    if(!JS_IsObject(options)||JS_IsNull(options)) { JS_ThrowTypeError(ctx,"Projection options must be an object"); return 0; }
    float f=c->fov_y_degrees,a=c->aspect,n=c->near_clip,r=c->far_clip;
    if(!athena_js_option_float(ctx,options,"fovYDegrees",&f)||!athena_js_option_float(ctx,options,"aspect",&a)||
        !athena_js_option_float(ctx,options,"near",&n)||!athena_js_option_float(ctx,options,"far",&r)) return 0;
    if(!athena_camera3d_set_projection(c,f,a,n,r)) {
        JS_ThrowRangeError(ctx,"Projection requires 0 < fovYDegrees < 179, aspect > 0 and 0 < near < far"); return 0;
    }
    return 1;
}
static JSValue ctor(JSContext *ctx,JSValueConst target,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,0,1,"Camera3D.Camera")) return JS_EXCEPTION;
    AthenaCamera3D next; athena_camera3d_init(&next);
    if(argc&&!projection(ctx,&next,argv[0])) return JS_EXCEPTION;
    AthenaCamera3D *c=memalign(16,sizeof(*c));
    if(!c) return JS_ThrowOutOfMemory(ctx);
    *c=next;
    JSValue proto=JS_GetPropertyStr(ctx,target,"prototype");
    if(JS_IsException(proto)) { free(c); return proto; }
    JSValue obj=JS_NewObjectProtoClass(ctx,proto,camera_id); JS_FreeValue(ctx,proto);
    if(JS_IsException(obj)) { free(c); return obj; }
    JS_SetOpaque(obj,c); return obj;
}
static JSValue dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Camera3D.dispose")||!athena_js_class(ctx,self,camera_id)) return JS_EXCEPTION;
    AthenaCamera3D *c=JS_GetOpaque(self,camera_id); JS_SetOpaque(self,NULL); free(c); return JS_UNDEFINED;
}
static JSValue set_projection(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"Camera3D.setProjection")) return JS_EXCEPTION;
    /* Read getters into a value copy: a getter may dispose self while options are evaluated. */
    AthenaCamera3D *c=athena_camera3d_from_value(ctx,self); if(!c) return JS_EXCEPTION;
    AthenaCamera3D next=*c;
    if(!projection(ctx,&next,argv[0])) return JS_EXCEPTION;
    c=athena_camera3d_from_value(ctx,self); if(!c) return JS_EXCEPTION;
    *c=next; return JS_DupValue(ctx,self);
}
static JSValue set_vector(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic) {
    if(!athena_js_argc(ctx,argc,3,3,"Camera3D vector setter")) return JS_EXCEPTION;
    float v[3];
    for(int i=0;i<3;i++) if(!athena_js_float(ctx,argv[i],&v[i],"coordinate")) return JS_EXCEPTION;
    AthenaCamera3D *c=athena_camera3d_from_value(ctx,self); if(!c) return JS_EXCEPTION;
    int ok=magic==0?athena_camera3d_set_position(c,v[0],v[1],v[2]):
        magic==1?athena_camera3d_look_at(c,v[0],v[1],v[2]):athena_camera3d_set_up(c,v[0],v[1],v[2]);
    if(!ok) return JS_ThrowRangeError(ctx,"Camera position/target/up must define a finite non-degenerate view");
    return JS_DupValue(ctx,self);
}
static JSValue get_matrix(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic) {
    if(!athena_js_argc(ctx,argc,0,1,"Camera3D matrix getter")) return JS_EXCEPTION;
    AthenaCamera3D *c=athena_camera3d_from_value(ctx,self); if(!c) return JS_EXCEPTION;
    if(!athena_camera3d_update(c)) return JS_ThrowRangeError(ctx,"Invalid camera view");
    const AthenaMatrix4 *m=magic==0?&c->view:magic==1?&c->projection:&c->view_projection;
    if(argc) {
        AthenaMatrix4 *out=athena_matrix4_from_value(ctx,argv[0]); if(!out) return JS_EXCEPTION;
        *out=*m; return JS_DupValue(ctx,argv[0]);
    }
    return athena_matrix4_to_value(ctx,m);
}
static JSClassDef class_def={"Camera3D.Camera",.finalizer=finalizer};
static const JSCFunctionListEntry methods[]={
    JS_CFUNC_MAGIC_DEF("setPosition",3,set_vector,0),JS_CFUNC_MAGIC_DEF("lookAt",3,set_vector,1),
    JS_CFUNC_MAGIC_DEF("setUp",3,set_vector,2),JS_CFUNC_DEF("setProjection",1,set_projection),
    JS_CFUNC_MAGIC_DEF("getView",0,get_matrix,0),JS_CFUNC_MAGIC_DEF("getProjection",0,get_matrix,1),
    JS_CFUNC_MAGIC_DEF("getViewProjection",0,get_matrix,2),JS_CFUNC_DEF("dispose",0,dispose)};
static int init(JSContext *ctx,JSModuleDef *m) {
    if(athena_register_class(ctx,&camera_id,&class_def)<0) return -1;
    JSValue proto=JS_NewObject(ctx); if(JS_IsException(proto)) return -1;
    JS_SetPropertyFunctionList(ctx,proto,methods,countof(methods));
    JSValue cls=JS_NewCFunction2(ctx,ctor,"Camera",1,JS_CFUNC_constructor,0);
    JS_SetConstructor(ctx,cls,proto); JS_SetClassProto(ctx,camera_id,proto);
    return JS_SetModuleExport(ctx,m,"Camera",cls);
}
JSModuleDef *athena_camera3d_js_init(JSContext *ctx) {
    JSModuleDef *m=athena_push_module(ctx,init,NULL,0,"Camera3D"); if(m) JS_AddModuleExport(ctx,m,"Camera"); return m;
}
