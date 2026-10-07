#include <athena_js_args.h>
#include <athena/render3d.h>
#include <athena/js/render3d.h>
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
static const char *const stat_names[]={"submittedObjects","culledObjects","drawPasses","pipelinePasses",
    "triangles","vuBatches","sourceTriangles","clippedTriangles","rejectedTriangles","geometryBytes","guardBandObjects","nearClipObjects","vuMorphObjects"};
static JSAtom stat_atoms[countof(stat_names)];
static AthenaJSAtoms stat_table={stat_names,countof(stat_names),stat_atoms,NULL};
int athena_render3d_js_put_stats(JSContext *ctx,JSValueConst obj,int define,const AthenaRender3DStats *s) {
    const uint32_t values[]={s->submitted_objects,s->culled_objects,s->draw_passes,s->pipeline_passes,
        s->triangles,s->vu_batches,s->source_triangles,s->clipped_triangles,s->rejected_triangles};
    for(unsigned i=0;i<countof(values);i++)
        if(athena_js_put(ctx,&stat_table,obj,define,i,JS_NewUint32(ctx,values[i]))<0) return -1;
    if(athena_js_put(ctx,&stat_table,obj,define,9,JS_NewFloat64(ctx,(double)s->geometry_bytes))<0) return -1;
    if(athena_js_put(ctx,&stat_table,obj,define,10,JS_NewUint32(ctx,s->guard_band_objects))<0) return -1;
    if(athena_js_put(ctx,&stat_table,obj,define,11,JS_NewUint32(ctx,s->near_clip_objects))<0) return -1;
    return athena_js_put(ctx,&stat_table,obj,define,12,JS_NewUint32(ctx,s->vu_morph_objects));
}
/* The failure of a draw, with the native reason when one was recorded. */
JSValue athena_render3d_js_throw(JSContext *ctx,int code) {
    const char *detail=athena_render3d_error_detail();
    if(code==-2&&!detail[0]) return JS_ThrowOutOfMemory(ctx);
    if(code==-3) return JS_ThrowInternalError(ctx,"Render3D requires a screen mode with zbuffering enabled");
    if(code==-2) return JS_ThrowInternalError(ctx,"Render3D: %s",detail);
    if(detail[0]) return JS_ThrowRangeError(ctx,"Render3D: %s",detail);
    return JS_ThrowRangeError(ctx,"Invalid or overflowing 3D transform");
}
/* out: optional object to reuse, so a draw per frame allocates nothing. */
static JSValue stats_value(JSContext *ctx,int code,const AthenaRender3DStats *s,JSValueConst out) {
    if(code<0) return athena_render3d_js_throw(ctx,code);
    JSValue obj; int define;
    if(!athena_js_out_object(ctx,out,&obj,&define,"stats")) return JS_EXCEPTION;
    if(athena_render3d_js_put_stats(ctx,obj,define,s)<0) { JS_FreeValue(ctx,obj); return JS_EXCEPTION; }
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
    if(!athena_js_argc(ctx,argc,2,5,"Render3D.draw")) return JS_EXCEPTION;
    AthenaRender3DCull cull; if(!cull_option(ctx,argc,argv,&cull)) return JS_EXCEPTION;
    AthenaInstance3D *i=athena_instance3d_from_value(ctx,argv[0]);
    AthenaCamera3D *c=athena_camera3d_from_value(ctx,argv[1]); if(!i||!c) return JS_EXCEPTION;
    AthenaLights *lights=NULL;
    if(argc>=4&&!JS_IsUndefined(argv[3])) { lights=athena_lights_from_value(ctx,argv[3]); if(!lights) return JS_EXCEPTION; }
    JSValueConst out=argc==5?argv[4]:JS_UNDEFINED;
    if(!JS_IsUndefined(out)&&!JS_IsObject(out)) return JS_ThrowTypeError(ctx,"stats must be an object");
    athena_render3d_set_error_detail(NULL);
    AthenaRender3DStats s={0}; int result=athena_render3d_draw_lit(i,c,lights,cull,&s); return stats_value(ctx,result,&s,out);
}
static JSValue batch_draw(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,4,"Batch.draw")) return JS_EXCEPTION;
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
    if(argc>=3&&!JS_IsUndefined(argv[2])) { lights=athena_lights_from_value(ctx,argv[2]); if(!lights) return JS_EXCEPTION; }
    JSValueConst out=argc==4?argv[3]:JS_UNDEFINED;
    if(!JS_IsUndefined(out)&&!JS_IsObject(out)) return JS_ThrowTypeError(ctx,"stats must be an object");
    athena_render3d_set_error_detail(NULL);
    AthenaRender3DStats s={0}; int result=athena_batch3d_draw_lit(b,c,lights,cull,&s); return stats_value(ctx,result,&s,out);
}
/* Render3D.group(fn): consecutive draws inside fn that share camera,
 * program and texture reuse one GS/VU1 pass. The pass closes when fn
 * returns or throws; fn must not draw 2D, flip or change the camera. */
static JSValue group(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,1,"Render3D.group")) return JS_EXCEPTION;
    if(!JS_IsFunction(ctx,argv[0])) return JS_ThrowTypeError(ctx,"Render3D.group expects a function");
    if(athena_render3d_group_begin()<0) return JS_ThrowTypeError(ctx,"Render3D.group cannot be nested");
    JSValue result=JS_Call(ctx,argv[0],JS_UNDEFINED,0,NULL);
    athena_render3d_group_end();
    return result;
}
static JSClassDef class_def={"Render3D.Batch",.finalizer=finalizer};
static const JSCFunctionListEntry methods[]={
    JS_CFUNC_DEF("add",1,add),JS_CFUNC_DEF("clear",0,clear),JS_CFUNC_DEF("draw",1,batch_draw),
    JS_CFUNC_DEF("dispose",0,dispose),JS_CGETSET_DEF("size",size,NULL)};
static const JSCFunctionListEntry exports[]={JS_CFUNC_DEF("draw",2,draw),JS_CFUNC_DEF("group",1,group),
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
void athena_render3d_js_cleanup(JSContext *ctx) { athena_js_atoms_free(ctx,&stat_table); }
JSModuleDef *athena_render3d_js_init(JSContext *ctx) {
    JSModuleDef *m=athena_push_module(ctx,init,exports,countof(exports),"Render3D");
    if(m) JS_AddModuleExport(ctx,m,"Batch");
    return m;
}
