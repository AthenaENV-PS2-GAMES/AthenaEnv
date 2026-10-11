#include <malloc.h>
#include <athena_js_args.h>
#include <athena/js/camera3d.h>
#include <athena/js/matrix4.h>
#include "ath_camera3d.h"
static JSClassID camera_id;
/* Handles are reference counted so native controllers (CameraRig3D) can keep
 * a camera after its JS handle is disposed. The camera comes first, so the
 * opaque pointer is the AthenaCamera3D pointer. */
typedef struct { AthenaCamera3D camera; uint32_t refs; float viewport[2]; } CameraHandle;
void athena_camera3d_js_retain(AthenaCamera3D *c) { if(c) ((CameraHandle *)c)->refs++; }
void athena_camera3d_js_release(AthenaCamera3D *c) { if(c&&!--((CameraHandle *)c)->refs) free(c); }
AthenaCamera3D *athena_camera3d_from_value(JSContext *ctx,JSValueConst value) {
    AthenaCamera3D *c=JS_GetOpaque2(ctx,value,camera_id);
    if(!c) JS_ThrowTypeError(ctx,"Expected a live Camera3D.Camera");
    return c;
}
static void finalizer(JSRuntime *rt,JSValue value) { (void)rt; athena_camera3d_js_release(JS_GetOpaque(value,camera_id)); }
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
    CameraHandle *h=memalign(16,sizeof(*h));
    if(!h) return JS_ThrowOutOfMemory(ctx);
    h->camera=next; h->refs=1; h->viewport[0]=640; h->viewport[1]=448;
    AthenaCamera3D *c=&h->camera;
    JSValue proto=JS_GetPropertyStr(ctx,target,"prototype");
    if(JS_IsException(proto)) { free(c); return proto; }
    JSValue obj=JS_NewObjectProtoClass(ctx,proto,camera_id); JS_FreeValue(ctx,proto);
    if(JS_IsException(obj)) { free(c); return obj; }
    JS_SetOpaque(obj,c); return obj;
}
static JSValue dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Camera3D.dispose")||!athena_js_class(ctx,self,camera_id)) return JS_EXCEPTION;
    AthenaCamera3D *c=JS_GetOpaque(self,camera_id); JS_SetOpaque(self,NULL); athena_camera3d_js_release(c); return JS_UNDEFINED;
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
/* The viewport that worldToScreen/screenToRay map to (default 640x448). */
static JSValue set_viewport(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,2,2,"Camera3D.setViewport")) return JS_EXCEPTION;
    float w,h;
    if(!athena_js_float(ctx,argv[0],&w,"width")||!athena_js_float(ctx,argv[1],&h,"height")) return JS_EXCEPTION;
    AthenaCamera3D *c=athena_camera3d_from_value(ctx,self); if(!c) return JS_EXCEPTION;
    if(!(w>0)||!(h>0)||w>4096||h>4096) return JS_ThrowRangeError(ctx,"Viewport requires 0 < width, height <= 4096");
    ((CameraHandle *)c)->viewport[0]=w; ((CameraHandle *)c)->viewport[1]=h;
    return JS_DupValue(ctx,self);
}
static const char *const out_names[]={"x","y","depth","z","dx","dy","dz"};
static JSAtom out_atoms[countof(out_names)];
static AthenaJSAtoms out_table={out_names,countof(out_names),out_atoms,NULL};
static JSValue put_floats(JSContext *ctx,JSValueConst out,const unsigned *fields,const float *values,unsigned count) {
    JSValue obj; int define;
    if(!athena_js_out_object(ctx,out,&obj,&define,"out")) return JS_EXCEPTION;
    for(unsigned i=0;i<count;i++)
        if(athena_js_put(ctx,&out_table,obj,define,fields[i],JS_NewFloat64(ctx,values[i]))<0) {
            JS_FreeValue(ctx,obj); return JS_EXCEPTION;
        }
    return obj;
}
static JSValue world_to_screen(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,3,4,"Camera3D.worldToScreen")) return JS_EXCEPTION;
    float p[3];
    for(int i=0;i<3;i++) if(!athena_js_float(ctx,argv[i],&p[i],"coordinate")) return JS_EXCEPTION;
    JSValueConst out=argc==4?argv[3]:JS_UNDEFINED;
    if(!JS_IsUndefined(out)&&!JS_IsObject(out)) return JS_ThrowTypeError(ctx,"out must be an object");
    AthenaCamera3D *c=athena_camera3d_from_value(ctx,self); if(!c) return JS_EXCEPTION;
    const float *vp=((CameraHandle *)c)->viewport; float screen[3];
    int code=athena_camera3d_world_to_screen(c,p,vp[0],vp[1],screen);
    if(code<0) return JS_ThrowRangeError(ctx,"Invalid point or camera view");
    if(!code) return JS_NULL;
    static const unsigned fields[]={0,1,2};
    return put_floats(ctx,out,fields,screen,3);
}
static JSValue screen_to_ray(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,2,3,"Camera3D.screenToRay")) return JS_EXCEPTION;
    float sx,sy;
    if(!athena_js_float(ctx,argv[0],&sx,"x")||!athena_js_float(ctx,argv[1],&sy,"y")) return JS_EXCEPTION;
    JSValueConst out=argc==3?argv[2]:JS_UNDEFINED;
    if(!JS_IsUndefined(out)&&!JS_IsObject(out)) return JS_ThrowTypeError(ctx,"out must be an object");
    AthenaCamera3D *c=athena_camera3d_from_value(ctx,self); if(!c) return JS_EXCEPTION;
    const float *vp=((CameraHandle *)c)->viewport; float ray[6];
    if(athena_camera3d_screen_to_ray(c,sx,sy,vp[0],vp[1],ray,ray+3)<0) return JS_ThrowRangeError(ctx,"Invalid screen point or camera view");
    static const unsigned fields[]={0,1,3,4,5,6};
    return put_floats(ctx,out,fields,ray,6);
}
static JSClassDef class_def={"Camera3D.Camera",.finalizer=finalizer};
static const JSCFunctionListEntry methods[]={
    JS_CFUNC_MAGIC_DEF("setPosition",3,set_vector,0),JS_CFUNC_MAGIC_DEF("lookAt",3,set_vector,1),
    JS_CFUNC_MAGIC_DEF("setUp",3,set_vector,2),JS_CFUNC_DEF("setProjection",1,set_projection),
    JS_CFUNC_MAGIC_DEF("getView",0,get_matrix,0),JS_CFUNC_MAGIC_DEF("getProjection",0,get_matrix,1),
    JS_CFUNC_MAGIC_DEF("getViewProjection",0,get_matrix,2),JS_CFUNC_DEF("dispose",0,dispose),
    JS_CFUNC_DEF("setViewport",2,set_viewport),JS_CFUNC_DEF("worldToScreen",3,world_to_screen),
    JS_CFUNC_DEF("screenToRay",2,screen_to_ray)};
static int init(JSContext *ctx,JSModuleDef *m) {
    if(athena_register_class(ctx,&camera_id,&class_def)<0) return -1;
    JSValue proto=JS_NewObject(ctx); if(JS_IsException(proto)) return -1;
    JS_SetPropertyFunctionList(ctx,proto,methods,countof(methods));
    JSValue cls=JS_NewCFunction2(ctx,ctor,"Camera",1,JS_CFUNC_constructor,0);
    JS_SetConstructor(ctx,cls,proto); JS_SetClassProto(ctx,camera_id,proto);
    return JS_SetModuleExport(ctx,m,"Camera",cls);
}
void athena_camera3d_js_cleanup(JSContext *ctx) { athena_js_atoms_free(ctx,&out_table); }
JSModuleDef *athena_camera3d_js_init(JSContext *ctx) {
    JSModuleDef *m=athena_push_module(ctx,init,NULL,0,"Camera3D"); if(m) JS_AddModuleExport(ctx,m,"Camera"); return m;
}
