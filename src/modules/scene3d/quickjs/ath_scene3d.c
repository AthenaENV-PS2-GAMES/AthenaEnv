#include <athena_js_args.h>
#include <athena/scene3d.h>
#include <athena/js/model3d.h>
#include <athena/js/camera3d.h>
#include <athena/js/lights.h>
#include <athena/js/matrix4.h>
#include "ath_scene3d.h"
static JSClassID scene_id,node_id;
static AthenaScene3D *get_scene(JSContext *ctx,JSValueConst v) {
    AthenaScene3D *s=JS_GetOpaque2(ctx,v,scene_id);
    if(!s) JS_ThrowTypeError(ctx,"Expected a live Scene3D.Scene");
    return s;
}
static AthenaNode3D *get_node(JSContext *ctx,JSValueConst v) {
    AthenaNode3D *n=JS_GetOpaque2(ctx,v,node_id);
    if(!n) JS_ThrowTypeError(ctx,"Expected a live Scene3D.Node");
    return n;
}
static JSValue throw_code(JSContext *ctx,int code) {
    if(code==ATHENA_SCENE3D_ENOMEM) return JS_ThrowOutOfMemory(ctx);
    if(code==ATHENA_SCENE3D_ESTALE) return JS_ThrowInternalError(ctx,"%s",athena_scene3d_error(code));
    return JS_ThrowRangeError(ctx,"%s",athena_scene3d_error(code));
}
static void scene_finalizer(JSRuntime *rt,JSValue v) {
    (void)rt; AthenaScene3D *s=JS_GetOpaque(v,scene_id);
    /* Nobody can draw an unreachable scene: stop updating it as well. */
    athena_scene3d_detach_loop(s); athena_scene3d_release(s);
}
static void node_finalizer(JSRuntime *rt,JSValue v) { (void)rt; athena_node3d_release(JS_GetOpaque(v,node_id)); }
/* Each wrapper owns one native reference; wrappers of the same node are
 * distinct JS objects. */
static JSValue wrap_node(JSContext *ctx,AthenaNode3D *n) {
    if(!n) return JS_NULL;
    JSValue obj=JS_NewObjectClass(ctx,node_id); if(JS_IsException(obj)) return obj;
    athena_node3d_retain(n); JS_SetOpaque(obj,n); return obj;
}
static JSValue new_with_proto(JSContext *ctx,JSValueConst target,JSClassID id) {
    JSValue proto=JS_GetPropertyStr(ctx,target,"prototype"); if(JS_IsException(proto)) return proto;
    JSValue obj=JS_NewObjectProtoClass(ctx,proto,id); JS_FreeValue(ctx,proto); return obj;
}
static JSValue scene_ctor(JSContext *ctx,JSValueConst target,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Scene3D.Scene")) return JS_EXCEPTION;
    JSValue obj=new_with_proto(ctx,target,scene_id); if(JS_IsException(obj)) return obj;
    AthenaScene3D *s=athena_scene3d_create();
    if(!s) { JS_FreeValue(ctx,obj); return JS_ThrowOutOfMemory(ctx); }
    JS_SetOpaque(obj,s); return obj;
}
static JSValue node_ctor(JSContext *ctx,JSValueConst target,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,0,1,"Scene3D.Node")) return JS_EXCEPTION;
    AthenaMesh3D *mesh=NULL;
    if(argc==1&&!JS_IsUndefined(argv[0])&&!(mesh=athena_mesh3d_from_value(ctx,argv[0]))) return JS_EXCEPTION;
    JSValue obj=new_with_proto(ctx,target,node_id); if(JS_IsException(obj)) return obj;
    AthenaNode3D *n=athena_node3d_create();
    if(!n) { JS_FreeValue(ctx,obj); return JS_ThrowOutOfMemory(ctx); }
    athena_node3d_set_mesh(n,mesh); JS_SetOpaque(obj,n); return obj;
}
static JSValue scene_dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Scene.dispose")||!athena_js_class(ctx,self,scene_id)) return JS_EXCEPTION;
    AthenaScene3D *s=JS_GetOpaque(self,scene_id); JS_SetOpaque(self,NULL);
    athena_scene3d_detach_loop(s); athena_scene3d_release(s); return JS_UNDEFINED;
}
static JSValue node_dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Node.dispose")||!athena_js_class(ctx,self,node_id)) return JS_EXCEPTION;
    AthenaNode3D *n=JS_GetOpaque(self,node_id); JS_SetOpaque(self,NULL); athena_node3d_release(n); return JS_UNDEFINED;
}
static JSValue scene_root(JSContext *ctx,JSValueConst self) {
    AthenaScene3D *s=get_scene(ctx,self); return s?wrap_node(ctx,athena_scene3d_root(s)):JS_EXCEPTION;
}
static JSValue scene_stale(JSContext *ctx,JSValueConst self) {
    AthenaScene3D *s=get_scene(ctx,self); return s?JS_NewBool(ctx,athena_scene3d_stale(s)):JS_EXCEPTION;
}
static JSValue scene_attached(JSContext *ctx,JSValueConst self) {
    AthenaScene3D *s=get_scene(ctx,self); return s?JS_NewBool(ctx,athena_scene3d_loop_system(s)!=0):JS_EXCEPTION;
}
static int set_uint(JSContext *ctx,JSValue obj,const char *key,uint32_t value) {
    return JS_SetPropertyStr(ctx,obj,key,JS_NewUint32(ctx,value));
}
static JSValue scene_update(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Scene.update")) return JS_EXCEPTION;
    AthenaScene3D *s=get_scene(ctx,self); if(!s) return JS_EXCEPTION;
    AthenaScene3DUpdateStats stats; int code=athena_scene3d_update(s,&stats);
    if(code<0) return throw_code(ctx,code);
    JSValue obj=JS_NewObject(ctx); if(JS_IsException(obj)) return obj;
    if(set_uint(ctx,obj,"visitedNodes",stats.visited_nodes)<0||set_uint(ctx,obj,"worldUpdates",stats.world_updates)<0||
        set_uint(ctx,obj,"boundsUpdates",stats.bounds_updates)<0) { JS_FreeValue(ctx,obj); return JS_EXCEPTION; }
    return obj;
}
static JSValue scene_draw(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,3,"Scene.draw")) return JS_EXCEPTION;
    AthenaRender3DCull cull=ATHENA_RENDER3D_CULL_BACK;
    if(argc>=2&&!JS_IsUndefined(argv[1])) {
        float value;
        if(!athena_js_float(ctx,argv[1],&value,"cullMode")) return JS_EXCEPTION;
        if(value!=0&&value!=1&&value!=-1) return JS_ThrowRangeError(ctx,"Invalid cullMode");
        cull=(int)value;
    }
    AthenaScene3D *s=get_scene(ctx,self); if(!s) return JS_EXCEPTION;
    AthenaCamera3D *camera=athena_camera3d_from_value(ctx,argv[0]); if(!camera) return JS_EXCEPTION;
    AthenaLights *lights=NULL;
    if(argc==3&&!JS_IsUndefined(argv[2])&&!(lights=athena_lights_from_value(ctx,argv[2]))) return JS_EXCEPTION;
    AthenaScene3DDrawStats stats; int render=0;
    int code=athena_scene3d_draw(s,camera,lights,cull,&stats,&render);
    if(code==ATHENA_SCENE3D_ERENDER) {
        if(render==-2) return JS_ThrowOutOfMemory(ctx);
        if(render==-3) return JS_ThrowInternalError(ctx,"Render3D requires a screen mode with zbuffering enabled");
        return JS_ThrowRangeError(ctx,"Invalid or overflowing 3D transform");
    }
    if(code<0) return throw_code(ctx,code);
    const AthenaRender3DStats *r=&stats.render;
    JSValue obj=JS_NewObject(ctx); if(JS_IsException(obj)) return obj;
    if(set_uint(ctx,obj,"submittedObjects",r->submitted_objects)<0||set_uint(ctx,obj,"culledObjects",r->culled_objects)<0||
        set_uint(ctx,obj,"drawPasses",r->draw_passes)<0||set_uint(ctx,obj,"triangles",r->triangles)<0||
        set_uint(ctx,obj,"vuBatches",r->vu_batches)<0||set_uint(ctx,obj,"sourceTriangles",r->source_triangles)<0||
        set_uint(ctx,obj,"clippedTriangles",r->clipped_triangles)<0||set_uint(ctx,obj,"rejectedTriangles",r->rejected_triangles)<0||
        JS_SetPropertyStr(ctx,obj,"geometryBytes",JS_NewFloat64(ctx,(double)r->geometry_bytes))<0||
        set_uint(ctx,obj,"culledSubtrees",stats.culled_subtrees)<0||set_uint(ctx,obj,"queuedObjects",stats.queued_objects)<0) {
        JS_FreeValue(ctx,obj); return JS_EXCEPTION;
    }
    return obj;
}
static JSValue scene_attach(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,0,1,"Scene.attachLoop")) return JS_EXCEPTION;
    float priority=0;
    if(argc==1&&!JS_IsUndefined(argv[0])) {
        if(!athena_js_float(ctx,argv[0],&priority,"priority")) return JS_EXCEPTION;
        if(priority!=(int)priority||priority<-1000000||priority>1000000) return JS_ThrowRangeError(ctx,"priority must be an integer");
    }
    AthenaScene3D *s=get_scene(ctx,self); if(!s) return JS_EXCEPTION;
    if(athena_scene3d_loop_system(s)) return JS_ThrowInternalError(ctx,"Scene is already attached to the Loop");
    int id=athena_scene3d_attach_loop(s,(int)priority,ctx);
    if(id<0) return throw_code(ctx,id);
    return JS_DupValue(ctx,self);
}
static JSValue scene_detach(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Scene.detachLoop")) return JS_EXCEPTION;
    AthenaScene3D *s=get_scene(ctx,self); if(!s) return JS_EXCEPTION;
    athena_scene3d_detach_loop(s); return JS_DupValue(ctx,self);
}
static JSValue node_set_mesh(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"Node.setMesh")) return JS_EXCEPTION;
    AthenaMesh3D *mesh=NULL;
    if(!JS_IsNull(argv[0])&&!(mesh=athena_mesh3d_from_value(ctx,argv[0]))) return JS_EXCEPTION;
    AthenaNode3D *n=get_node(ctx,self); if(!n) return JS_EXCEPTION;
    athena_node3d_set_mesh(n,mesh); return JS_DupValue(ctx,self);
}
static JSValue node_has_mesh(JSContext *ctx,JSValueConst self) {
    AthenaNode3D *n=get_node(ctx,self); return n?JS_NewBool(ctx,athena_node3d_mesh(n)!=NULL):JS_EXCEPTION;
}
static JSValue node_set_vector(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic) {
    int count=magic==3?4:3;
    if(!athena_js_argc(ctx,argc,count,count,"Node transform setter")) return JS_EXCEPTION;
    float v[4];
    for(int i=0;i<count;i++) if(!athena_js_float(ctx,argv[i],&v[i],"transform")) return JS_EXCEPTION;
    AthenaNode3D *n=get_node(ctx,self); if(!n) return JS_EXCEPTION;
    int code=magic==0?athena_node3d_set_position(n,v[0],v[1],v[2]):
        magic==1?athena_node3d_set_scale(n,v[0],v[1],v[2]):
        magic==2?athena_node3d_set_euler(n,v[0],v[1],v[2]):
        athena_node3d_set_rotation(n,v[0],v[1],v[2],v[3]);
    if(code<0) return JS_ThrowRangeError(ctx,"Invalid transform");
    return JS_DupValue(ctx,self);
}
static JSValue node_visible(JSContext *ctx,JSValueConst self) {
    AthenaNode3D *n=get_node(ctx,self); return n?JS_NewBool(ctx,athena_node3d_visible(n)):JS_EXCEPTION;
}
static JSValue node_set_visible(JSContext *ctx,JSValueConst self,JSValueConst value) {
    if(!JS_IsBool(value)) return JS_ThrowTypeError(ctx,"visible must be a boolean");
    AthenaNode3D *n=get_node(ctx,self); if(!n) return JS_EXCEPTION;
    athena_node3d_set_visible(n,JS_ToBool(ctx,value)); return JS_UNDEFINED;
}
static JSValue node_add(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"Node.add")) return JS_EXCEPTION;
    AthenaNode3D *parent=get_node(ctx,self); if(!parent) return JS_EXCEPTION;
    AthenaNode3D *child=get_node(ctx,argv[0]); if(!child) return JS_EXCEPTION;
    int code=athena_node3d_add_child(parent,child);
    if(code<0) return throw_code(ctx,code);
    return JS_DupValue(ctx,self);
}
static JSValue node_detach(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Node.detach")) return JS_EXCEPTION;
    AthenaNode3D *n=get_node(ctx,self); if(!n) return JS_EXCEPTION;
    athena_node3d_detach(n); return JS_DupValue(ctx,self);
}
static JSValue node_parent(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Node.getParent")) return JS_EXCEPTION;
    AthenaNode3D *n=get_node(ctx,self); return n?wrap_node(ctx,athena_node3d_parent(n)):JS_EXCEPTION;
}
static JSValue node_child_count(JSContext *ctx,JSValueConst self) {
    AthenaNode3D *n=get_node(ctx,self); return n?JS_NewUint32(ctx,athena_node3d_child_count(n)):JS_EXCEPTION;
}
static JSValue node_child(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"Node.getChild")) return JS_EXCEPTION;
    float index; if(!athena_js_float(ctx,argv[0],&index,"index")) return JS_EXCEPTION;
    AthenaNode3D *n=get_node(ctx,self); if(!n) return JS_EXCEPTION;
    if(index<0||index!=(uint32_t)index||(uint32_t)index>=athena_node3d_child_count(n))
        return JS_ThrowRangeError(ctx,"Child index out of range");
    return wrap_node(ctx,athena_node3d_child(n,(uint32_t)index));
}
static JSValue node_matrix(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int world) {
    if(!athena_js_argc(ctx,argc,0,1,world?"Node.getWorldTransform":"Node.getLocalTransform")) return JS_EXCEPTION;
    AthenaNode3D *n=get_node(ctx,self); if(!n) return JS_EXCEPTION;
    AthenaMatrix4 m; int code=world?athena_node3d_world(n,&m):athena_node3d_local(n,&m);
    if(code<0) return throw_code(ctx,code);
    if(argc) {
        AthenaMatrix4 *out=athena_matrix4_from_value(ctx,argv[0]); if(!out) return JS_EXCEPTION;
        *out=m; return JS_DupValue(ctx,argv[0]);
    }
    return athena_matrix4_to_value(ctx,&m);
}
static JSValue vector3(JSContext *ctx,const float v[3]) {
    JSValue a=JS_NewArray(ctx); if(JS_IsException(a)) return a;
    for(uint32_t i=0;i<3;i++) if(JS_SetPropertyUint32(ctx,a,i,JS_NewFloat64(ctx,v[i]))<0) { JS_FreeValue(ctx,a); return JS_EXCEPTION; }
    return a;
}
static JSValue node_bounds(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Node.getWorldBounds")) return JS_EXCEPTION;
    AthenaNode3D *n=get_node(ctx,self); if(!n) return JS_EXCEPTION;
    float minimum[3],maximum[3]; int code=athena_node3d_world_bounds(n,minimum,maximum);
    if(code<0) return throw_code(ctx,code);
    if(!code) return JS_NULL;
    JSValue obj=JS_NewObject(ctx); if(JS_IsException(obj)) return obj;
    JSValue lo=vector3(ctx,minimum); if(JS_IsException(lo)) { JS_FreeValue(ctx,obj); return lo; }
    if(JS_SetPropertyStr(ctx,obj,"min",lo)<0) { JS_FreeValue(ctx,obj); return JS_EXCEPTION; }
    JSValue hi=vector3(ctx,maximum); if(JS_IsException(hi)) { JS_FreeValue(ctx,obj); return hi; }
    if(JS_SetPropertyStr(ctx,obj,"max",hi)<0) { JS_FreeValue(ctx,obj); return JS_EXCEPTION; }
    return obj;
}
static JSClassDef scene_class={"Scene3D.Scene",.finalizer=scene_finalizer};
static JSClassDef node_class={"Scene3D.Node",.finalizer=node_finalizer};
static const JSCFunctionListEntry scene_methods[]={
    JS_CFUNC_DEF("update",0,scene_update),JS_CFUNC_DEF("draw",1,scene_draw),
    JS_CFUNC_DEF("attachLoop",0,scene_attach),JS_CFUNC_DEF("detachLoop",0,scene_detach),
    JS_CFUNC_DEF("dispose",0,scene_dispose),JS_CGETSET_DEF("root",scene_root,NULL),
    JS_CGETSET_DEF("stale",scene_stale,NULL),JS_CGETSET_DEF("attached",scene_attached,NULL)};
static const JSCFunctionListEntry node_methods[]={
    JS_CFUNC_DEF("setMesh",1,node_set_mesh),JS_CGETSET_DEF("hasMesh",node_has_mesh,NULL),
    JS_CFUNC_MAGIC_DEF("setPosition",3,node_set_vector,0),JS_CFUNC_MAGIC_DEF("setScale",3,node_set_vector,1),
    JS_CFUNC_MAGIC_DEF("setRotationEuler",3,node_set_vector,2),JS_CFUNC_MAGIC_DEF("setRotationQuaternion",4,node_set_vector,3),
    JS_CGETSET_DEF("visible",node_visible,node_set_visible),
    JS_CFUNC_DEF("add",1,node_add),JS_CFUNC_DEF("detach",0,node_detach),
    JS_CFUNC_DEF("getParent",0,node_parent),JS_CGETSET_DEF("childCount",node_child_count,NULL),
    JS_CFUNC_DEF("getChild",1,node_child),
    JS_CFUNC_MAGIC_DEF("getLocalTransform",0,node_matrix,0),JS_CFUNC_MAGIC_DEF("getWorldTransform",0,node_matrix,1),
    JS_CFUNC_DEF("getWorldBounds",0,node_bounds),JS_CFUNC_DEF("dispose",0,node_dispose)};
static const JSCFunctionListEntry exports[]={
    JS_PROP_INT32_DEF("MAX_DEPTH",ATHENA_SCENE3D_MAX_DEPTH,JS_PROP_ENUMERABLE)};
static int define_class(JSContext *ctx,JSModuleDef *m,JSClassID id,const char *name,JSCFunction *ctor,
    const JSCFunctionListEntry *methods,int count) {
    JSValue proto=JS_NewObject(ctx); if(JS_IsException(proto)) return -1;
    JS_SetPropertyFunctionList(ctx,proto,methods,count);
    JSValue cls=JS_NewCFunction2(ctx,ctor,name,0,JS_CFUNC_constructor,0);
    if(JS_IsException(cls)) { JS_FreeValue(ctx,proto); return -1; }
    JS_SetConstructor(ctx,cls,proto); JS_SetClassProto(ctx,id,proto);
    return JS_SetModuleExport(ctx,m,name,cls);
}
static int init(JSContext *ctx,JSModuleDef *m) {
    if(athena_register_class(ctx,&scene_id,&scene_class)<0||athena_register_class(ctx,&node_id,&node_class)<0) return -1;
    if(define_class(ctx,m,scene_id,"Scene",scene_ctor,scene_methods,countof(scene_methods))<0||
        define_class(ctx,m,node_id,"Node",node_ctor,node_methods,countof(node_methods))<0) return -1;
    return JS_SetModuleExportList(ctx,m,exports,countof(exports));
}
JSModuleDef *athena_scene3d_js_init(JSContext *ctx) {
    JSModuleDef *m=athena_push_module(ctx,init,exports,countof(exports),"Scene3D");
    if(m) { JS_AddModuleExport(ctx,m,"Scene"); JS_AddModuleExport(ctx,m,"Node"); }
    return m;
}
/* Scenes attached by this context stop updating with it; C attachments stay. */
void athena_scene3d_js_cleanup(JSContext *ctx) { athena_scene3d_detach_owner(ctx); }
