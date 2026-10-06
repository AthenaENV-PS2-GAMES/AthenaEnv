#include <athena_js_args.h>
#include <athena/render3d.h>
#include <athena/js/model3d.h>
#include <athena/js/camera3d.h>
#include <athena/js/lights.h>
#include "ath_render3d.h"
static JSClassID batch_id;
static AthenaBatch3D *get_batch(JSContext *ctx,JSValueConst self) {
    AthenaBatch3D *b=JS_GetOpaque2(ctx,self,batch_id);
    if(!b) JS_ThrowTypeError(ctx,"Expected a live Render3D.Batch");
    return b;
}
static void finalizer(JSRuntime *rt,JSValue obj) { (void)rt; athena_batch3d_destroy(JS_GetOpaque(obj,batch_id)); }
static JSValue ctor(JSContext *ctx,JSValueConst target,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Render3D.Batch")) return JS_EXCEPTION;
    AthenaBatch3D *b=athena_batch3d_create(); if(!b) return JS_ThrowOutOfMemory(ctx);
    JSValue proto=JS_GetPropertyStr(ctx,target,"prototype");
    if(JS_IsException(proto)) { athena_batch3d_destroy(b); return proto; }
    JSValue obj=JS_NewObjectProtoClass(ctx,proto,batch_id); JS_FreeValue(ctx,proto);
    if(JS_IsException(obj)) { athena_batch3d_destroy(b); return obj; }
    JS_SetOpaque(obj,b); return obj;
}
static JSValue dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Batch.dispose")||!athena_js_class(ctx,self,batch_id)) return JS_EXCEPTION;
    AthenaBatch3D *b=JS_GetOpaque(self,batch_id); JS_SetOpaque(self,NULL); athena_batch3d_destroy(b); return JS_UNDEFINED;
}
static JSValue add(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"Batch.add")) return JS_EXCEPTION;
    AthenaBatch3D *b=get_batch(ctx,self);
    AthenaInstance3D *i=athena_instance3d_from_value(ctx,argv[0]);
    if(!b||!i) return JS_EXCEPTION;
    int code=athena_batch3d_add(b,i);
    if(code==-2) return JS_ThrowOutOfMemory(ctx);
    if(code<0) return JS_ThrowRangeError(ctx,"Batch is full");
    return JS_DupValue(ctx,self);
}
static JSValue clear(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv; if(!athena_js_argc(ctx,argc,0,0,"Batch.clear")) return JS_EXCEPTION;
    AthenaBatch3D *b=get_batch(ctx,self); if(!b) return JS_EXCEPTION;
    athena_batch3d_clear(b); return JS_DupValue(ctx,self);
}
static JSValue size(JSContext *ctx,JSValueConst self) {
    AthenaBatch3D *b=get_batch(ctx,self); return b?JS_NewUint32(ctx,athena_batch3d_size(b)):JS_EXCEPTION;
}
static JSValue stats_value(JSContext *ctx,int code,const AthenaRender3DStats *s) {
    if(code==-2) return JS_ThrowOutOfMemory(ctx);
    if(code==-3) return JS_ThrowInternalError(ctx,"Render3D requires a screen mode with zbuffering enabled");
    if(code<0) return JS_ThrowRangeError(ctx,"Invalid or overflowing 3D transform");
    JSValue obj=JS_NewObject(ctx); if(JS_IsException(obj)) return obj;
    if(JS_SetPropertyStr(ctx,obj,"submittedObjects",JS_NewUint32(ctx,s->submitted_objects))<0||
        JS_SetPropertyStr(ctx,obj,"culledObjects",JS_NewUint32(ctx,s->culled_objects))<0||
        JS_SetPropertyStr(ctx,obj,"drawPasses",JS_NewUint32(ctx,s->draw_passes))<0||
        JS_SetPropertyStr(ctx,obj,"triangles",JS_NewUint32(ctx,s->triangles))<0||
        JS_SetPropertyStr(ctx,obj,"vuBatches",JS_NewUint32(ctx,s->vu_batches))<0||
        JS_SetPropertyStr(ctx,obj,"sourceTriangles",JS_NewUint32(ctx,s->source_triangles))<0||
        JS_SetPropertyStr(ctx,obj,"clippedTriangles",JS_NewUint32(ctx,s->clipped_triangles))<0||
        JS_SetPropertyStr(ctx,obj,"rejectedTriangles",JS_NewUint32(ctx,s->rejected_triangles))<0||
        JS_SetPropertyStr(ctx,obj,"geometryBytes",JS_NewFloat64(ctx,(double)s->geometry_bytes))<0) {
        JS_FreeValue(ctx,obj); return JS_EXCEPTION;
    }
    return obj;
}
static int cull_option(JSContext *ctx,int argc,JSValueConst *argv,AthenaRender3DCull *out) {
    *out=ATHENA_RENDER3D_CULL_BACK;
    if(argc>=3&&!JS_IsUndefined(argv[2])) {
        float value;
        if(!athena_js_float(ctx,argv[2],&value,"cullMode")) return 0;
        if(value!=0&&value!=1&&value!=-1) { JS_ThrowRangeError(ctx,"Invalid cullMode"); return 0; }
        *out=(int)value;
    }
    return 1;
}
static JSValue draw(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,2,4,"Render3D.draw")) return JS_EXCEPTION;
    AthenaRender3DCull cull; if(!cull_option(ctx,argc,argv,&cull)) return JS_EXCEPTION;
    AthenaInstance3D *i=athena_instance3d_from_value(ctx,argv[0]);
    AthenaCamera3D *c=athena_camera3d_from_value(ctx,argv[1]); if(!i||!c) return JS_EXCEPTION;
    AthenaLights *lights=NULL;
    if(argc==4&&!JS_IsUndefined(argv[3])) { lights=athena_lights_from_value(ctx,argv[3]); if(!lights) return JS_EXCEPTION; }
    AthenaRender3DStats s={0}; int result=athena_render3d_draw_lit(i,c,lights,cull,&s); return stats_value(ctx,result,&s);
}
static JSValue batch_draw(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,3,"Batch.draw")) return JS_EXCEPTION;
    AthenaRender3DCull cull=ATHENA_RENDER3D_CULL_BACK;
    if(argc>=2&&!JS_IsUndefined(argv[1])) {
        float value;
        if(!athena_js_float(ctx,argv[1],&value,"cullMode")) return JS_EXCEPTION;
        if(value!=0&&value!=1&&value!=-1) return JS_ThrowRangeError(ctx,"Invalid cullMode");
        cull=(int)value;
    }
    AthenaBatch3D *b=get_batch(ctx,self);
    AthenaCamera3D *c=athena_camera3d_from_value(ctx,argv[0]); if(!b||!c) return JS_EXCEPTION;
    AthenaLights *lights=NULL;
    if(argc==3&&!JS_IsUndefined(argv[2])) { lights=athena_lights_from_value(ctx,argv[2]); if(!lights) return JS_EXCEPTION; }
    AthenaRender3DStats s={0}; int result=athena_batch3d_draw_lit(b,c,lights,cull,&s); return stats_value(ctx,result,&s);
}
static JSClassDef class_def={"Render3D.Batch",.finalizer=finalizer};
static const JSCFunctionListEntry methods[]={
    JS_CFUNC_DEF("add",1,add),JS_CFUNC_DEF("clear",0,clear),JS_CFUNC_DEF("draw",1,batch_draw),
    JS_CFUNC_DEF("dispose",0,dispose),JS_CGETSET_DEF("size",size,NULL)};
static const JSCFunctionListEntry exports[]={JS_CFUNC_DEF("draw",2,draw),
    JS_PROP_INT32_DEF("CULL_NONE",0,JS_PROP_ENUMERABLE),JS_PROP_INT32_DEF("CULL_BACK",1,JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("CULL_FRONT",-1,JS_PROP_ENUMERABLE)};
static int init(JSContext *ctx,JSModuleDef *m) {
    if(athena_register_class(ctx,&batch_id,&class_def)<0) return -1;
    JSValue proto=JS_NewObject(ctx); if(JS_IsException(proto)) return -1;
    JS_SetPropertyFunctionList(ctx,proto,methods,countof(methods));
    JSValue cls=JS_NewCFunction2(ctx,ctor,"Batch",0,JS_CFUNC_constructor,0);
    JS_SetConstructor(ctx,cls,proto); JS_SetClassProto(ctx,batch_id,proto);
    if(JS_SetModuleExport(ctx,m,"Batch",cls)<0) return -1;
    return JS_SetModuleExportList(ctx,m,exports,countof(exports));
}
JSModuleDef *athena_render3d_js_init(JSContext *ctx) {
    JSModuleDef *m=athena_push_module(ctx,init,exports,countof(exports),"Render3D");
    if(m) JS_AddModuleExport(ctx,m,"Batch");
    return m;
}
