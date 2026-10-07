#include <athena_js_args.h>
#include <athena/scene3d.h>
#include <athena/js/model3d.h>
#include <athena/js/camera3d.h>
#include <athena/js/lights.h>
#include <athena/js/render3d.h>
#include <athena/js/matrix4.h>
#include <athena/js/scene3d.h>
#include "ath_scene3d.h"
static JSClassID scene_id,node_id;
/* Weak entries: value is borrowed, never duplicated or marked for GC. Each
 * live wrapper owns one native reference. Its finalizer/dispose removes the
 * entry BEFORE releasing that reference, including during cycle collection.
 * Runtime keys keep separate JS heaps from sharing objects. */
#define NODE_WRAPPER_BUCKETS 128u
typedef struct NodeWrapper {
    JSRuntime *runtime; AthenaNode3D *node; JSValue value;
    struct NodeWrapper *next;
} NodeWrapper;
static NodeWrapper *node_wrappers[NODE_WRAPPER_BUCKETS];
static NodeWrapper **node_wrapper_bucket(JSRuntime *rt,AthenaNode3D *node) {
    uintptr_t key=((uintptr_t)node>>4)^((uintptr_t)rt>>4);
    return &node_wrappers[key&(NODE_WRAPPER_BUCKETS-1)];
}
static int node_wrapper_add(JSContext *ctx,AthenaNode3D *node,JSValueConst value) {
    NodeWrapper *entry=js_malloc(ctx,sizeof(*entry)); if(!entry) return 0;
    NodeWrapper **bucket=node_wrapper_bucket(JS_GetRuntime(ctx),node);
    *entry=(NodeWrapper){JS_GetRuntime(ctx),node,value,*bucket}; *bucket=entry; return 1;
}
static void node_wrapper_remove(JSRuntime *rt,AthenaNode3D *node,JSValueConst value) {
    if(!node) return;
    NodeWrapper **link=node_wrapper_bucket(rt,node);
    while(*link) {
        NodeWrapper *entry=*link;
        if(entry->runtime==rt&&entry->node==node&&JS_VALUE_GET_PTR(entry->value)==JS_VALUE_GET_PTR(value)) {
            *link=entry->next; js_free_rt(rt,entry); return;
        }
        link=&entry->next;
    }
}
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
AthenaNode3D *athena_node3d_from_value(JSContext *ctx,JSValueConst v) { return get_node(ctx,v); }
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
static void node_finalizer(JSRuntime *rt,JSValue v) {
    AthenaNode3D *n=JS_GetOpaque(v,node_id);
    node_wrapper_remove(rt,n,v); athena_node3d_release(n);
}
static JSValue wrap_node(JSContext *ctx,AthenaNode3D *n) {
    if(!n) return JS_NULL;
    JSRuntime *rt=JS_GetRuntime(ctx);
    for(NodeWrapper *entry=*node_wrapper_bucket(rt,n);entry;entry=entry->next)
        if(entry->runtime==rt&&entry->node==n) return JS_DupValue(ctx,entry->value);
    /* Pin the borrowed node before allocating a JS object, which may GC. */
    athena_node3d_retain(n);
    JSValue obj=JS_NewObjectClass(ctx,node_id);
    if(JS_IsException(obj)) { athena_node3d_release(n); return obj; }
    JS_SetOpaque(obj,n);
    if(!node_wrapper_add(ctx,n,obj)) { JS_FreeValue(ctx,obj); return JS_EXCEPTION; }
    return obj;
}
JSValue athena_node3d_to_value(JSContext *ctx,AthenaNode3D *n) {
    if(!node_id) return JS_ThrowInternalError(ctx,"Scene3D is not initialized");
    return wrap_node(ctx,n);
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
    /* Read newTarget.prototype before borrowing a mesh: a proxy getter may
     * dispose the argument. Revalidate its handle after every such getter. */
    JSValue obj=new_with_proto(ctx,target,node_id); if(JS_IsException(obj)) return obj;
    AthenaMesh3D *mesh=NULL;
    if(argc==1&&!JS_IsUndefined(argv[0])&&!(mesh=athena_mesh3d_from_value(ctx,argv[0]))) {
        JS_FreeValue(ctx,obj); return JS_EXCEPTION;
    }
    AthenaNode3D *n=athena_node3d_create();
    if(!n) { JS_FreeValue(ctx,obj); return JS_ThrowOutOfMemory(ctx); }
    athena_node3d_set_mesh(n,mesh); JS_SetOpaque(obj,n);
    if(!node_wrapper_add(ctx,n,obj)) { JS_FreeValue(ctx,obj); return JS_EXCEPTION; }
    return obj;
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
    AthenaNode3D *n=JS_GetOpaque(self,node_id);
    node_wrapper_remove(JS_GetRuntime(ctx),n,self);
    JS_SetOpaque(self,NULL); athena_node3d_release(n); return JS_UNDEFINED;
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
static const char *const stat_names[]={"visitedNodes","worldUpdates","boundsUpdates","culledSubtrees","queuedObjects"};
static JSAtom stat_atoms[countof(stat_names)];
static AthenaJSAtoms stat_table={stat_names,countof(stat_names),stat_atoms,NULL};
static int put_uints(JSContext *ctx,JSValueConst obj,int define,unsigned first,const uint32_t *values,unsigned count) {
    for(unsigned i=0;i<count;i++)
        if(athena_js_put(ctx,&stat_table,obj,define,first+i,JS_NewUint32(ctx,values[i]))<0) return -1;
    return 0;
}
static int out_argument(JSContext *ctx,int argc,JSValueConst *argv,int index,JSValueConst *out) {
    *out=argc>index?argv[index]:JS_UNDEFINED;
    if(JS_IsUndefined(*out)||JS_IsObject(*out)) return 1;
    JS_ThrowTypeError(ctx,"stats must be an object"); return 0;
}
static JSValue scene_advance(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"Scene.advance")) return JS_EXCEPTION;
    float dt; if(!athena_js_float(ctx,argv[0],&dt,"dt")) return JS_EXCEPTION;
    AthenaScene3D *s=get_scene(ctx,self); if(!s) return JS_EXCEPTION;
    if(dt<0) return JS_ThrowRangeError(ctx,"dt must not be negative");
    int moved=athena_scene3d_advance(s,dt);
    return moved<0?throw_code(ctx,moved):JS_NewUint32(ctx,(uint32_t)moved);
}
static JSValue scene_update(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    JSValueConst out;
    if(!athena_js_argc(ctx,argc,0,1,"Scene.update")||!out_argument(ctx,argc,argv,0,&out)) return JS_EXCEPTION;
    AthenaScene3D *s=get_scene(ctx,self); if(!s) return JS_EXCEPTION;
    AthenaScene3DUpdateStats stats; int code=athena_scene3d_update(s,&stats);
    if(code<0) return throw_code(ctx,code);
    JSValue obj; int define;
    if(!athena_js_out_object(ctx,out,&obj,&define,"stats")) return JS_EXCEPTION;
    const uint32_t values[]={stats.visited_nodes,stats.world_updates,stats.bounds_updates};
    if(put_uints(ctx,obj,define,0,values,3)<0) { JS_FreeValue(ctx,obj); return JS_EXCEPTION; }
    return obj;
}
static JSValue scene_draw(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    JSValueConst out;
    if(!athena_js_argc(ctx,argc,1,4,"Scene.draw")||!out_argument(ctx,argc,argv,3,&out)) return JS_EXCEPTION;
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
    if(argc>=3&&!JS_IsUndefined(argv[2])&&!(lights=athena_lights_from_value(ctx,argv[2]))) return JS_EXCEPTION;
    AthenaScene3DDrawStats stats; int render=0;
    athena_render3d_set_error_detail(NULL);
    int code=athena_scene3d_draw(s,camera,lights,cull,&stats,&render);
    if(code==ATHENA_SCENE3D_ERENDER) return athena_render3d_js_throw(ctx,render);
    if(code<0) return throw_code(ctx,code);
    JSValue obj; int define;
    if(!athena_js_out_object(ctx,out,&obj,&define,"stats")) return JS_EXCEPTION;
    const uint32_t values[]={stats.culled_subtrees,stats.queued_objects};
    if(athena_render3d_js_put_stats(ctx,obj,define,&stats.render)<0||put_uints(ctx,obj,define,3,values,2)<0) {
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
        magic==4?athena_node3d_set_velocity(n,v[0],v[1],v[2]):
        magic==5?athena_node3d_set_spin(n,v[0],v[1],v[2]):
        athena_node3d_set_rotation(n,v[0],v[1],v[2],v[3]);
    if(code<0) return JS_ThrowRangeError(ctx,"Invalid transform");
    return JS_DupValue(ctx,self);
}
static void *node_item(JSContext *ctx,JSValueConst v) { return get_node(ctx,v); }
static void node_retain(void *n) { athena_node3d_retain(n); }
static void node_release(void *n) { athena_node3d_release(n); }
/* Scene3D.setPositions/setRotationsEuler(nodes, values): xyz per node from a
 * Float32Array; setTransforms2D(nodes, values): x, y and an angle about Z per
 * node (Box2D's readTransforms() layout), keeping each node's z. Validated
 * before any node changes. */
static JSValue set_many(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic) {
    (void)self;
    const char *name=magic==2?"Scene3D.setTransforms2D":magic?"Scene3D.setRotationsEuler":"Scene3D.setPositions";
    if(!athena_js_argc(ctx,argc,2,2,name)) return JS_EXCEPTION;
    AthenaJSHandles h;
    if(!athena_js_handles(ctx,argv[0],&h,node_item,node_retain,node_release,"nodes")) return JS_EXCEPTION;
    AthenaJSArray values;
    if(!athena_js_array(ctx,argv[1],JS_TYPED_ARRAY_FLOAT32,&values,"values")) { athena_js_handles_free(&h); return JS_EXCEPTION; }
    const float *v=values.data; JSValue result=JS_NewUint32(ctx,h.count);
    if(values.count/3<h.count) result=JS_ThrowRangeError(ctx,"values needs 3 floats per node");
    else for(uint32_t i=0;i<h.count*3;i++) if(!athena_float_isfinite(v[i])) {
        result=JS_ThrowRangeError(ctx,"values[%u] is not a finite float",(unsigned)i); break;
    }
    if(!JS_IsException(result)) for(uint32_t i=0;i<h.count;i++) {
        const float *x=&v[i*3];
        int code;
        if(magic==2) {
            float p[3],s[3]; AthenaQuaternion q; athena_node3d_get_trs(h.items[i],p,&q,s);
            code=athena_node3d_set_position(h.items[i],x[0],x[1],p[2]);
            if(code>=0) code=athena_node3d_set_euler(h.items[i],0,0,x[2]);
        } else code=magic?athena_node3d_set_euler(h.items[i],x[0],x[1],x[2]):
            athena_node3d_set_position(h.items[i],x[0],x[1],x[2]);
        if(code<0) { result=throw_code(ctx,code); break; }
    }
    JS_FreeValue(ctx,values.backing); athena_js_handles_free(&h); return result;
}
/* Node.setWeights(weights): morph target weights, an array or Float32Array
 * of up to 8 finite numbers; missing ones become 0. */
static JSValue node_set_weights(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"Node.setWeights")) return JS_EXCEPTION;
    float w[ATHENA_MODEL3D_MAX_TARGETS]; int64_t length=0;
    JSValue value=JS_GetPropertyStr(ctx,argv[0],"length");
    int failed=JS_IsException(value)||JS_ToInt64(ctx,&length,value)<0;
    JS_FreeValue(ctx,value);
    if(failed) return JS_EXCEPTION;
    if(!JS_IsObject(argv[0])||length<0||length>ATHENA_MODEL3D_MAX_TARGETS)
        return JS_ThrowRangeError(ctx,"weights must be an array of at most %u numbers",(unsigned)ATHENA_MODEL3D_MAX_TARGETS);
    for(int64_t i=0;i<length;i++) {
        JSValue e=JS_GetPropertyUint32(ctx,argv[0],(uint32_t)i);
        int ok=!JS_IsException(e)&&athena_js_float(ctx,e,&w[i],"weight");
        JS_FreeValue(ctx,e);
        if(!ok) return JS_EXCEPTION;
    }
    AthenaNode3D *n=get_node(ctx,self); if(!n) return JS_EXCEPTION;
    int code=athena_node3d_set_weights(n,w,(uint32_t)length);
    if(code<0) return throw_code(ctx,code);
    return JS_DupValue(ctx,self);
}
/* Node.getWeights(): one number per morph target of the node's mesh. */
static JSValue node_get_weights(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Node.getWeights")) return JS_EXCEPTION;
    AthenaNode3D *n=get_node(ctx,self); if(!n) return JS_EXCEPTION;
    float w[ATHENA_MODEL3D_MAX_TARGETS]; athena_node3d_get_weights(n,w);
    uint32_t count=athena_mesh3d_target_count(athena_node3d_mesh(n));
    JSValue a=JS_NewArray(ctx); if(JS_IsException(a)) return a;
    for(uint32_t i=0;i<count;i++) if(JS_SetPropertyUint32(ctx,a,i,JS_NewFloat64(ctx,w[i]))<0) { JS_FreeValue(ctx,a); return JS_EXCEPTION; }
    return a;
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
    JS_CFUNC_DEF("advance",1,scene_advance),JS_CFUNC_DEF("update",0,scene_update),JS_CFUNC_DEF("draw",1,scene_draw),
    JS_CFUNC_DEF("attachLoop",0,scene_attach),JS_CFUNC_DEF("detachLoop",0,scene_detach),
    JS_CFUNC_DEF("dispose",0,scene_dispose),JS_CGETSET_DEF("root",scene_root,NULL),
    JS_CGETSET_DEF("stale",scene_stale,NULL),JS_CGETSET_DEF("attached",scene_attached,NULL)};
static const JSCFunctionListEntry node_methods[]={
    JS_CFUNC_DEF("setMesh",1,node_set_mesh),JS_CGETSET_DEF("hasMesh",node_has_mesh,NULL),
    JS_CFUNC_MAGIC_DEF("setPosition",3,node_set_vector,0),JS_CFUNC_MAGIC_DEF("setScale",3,node_set_vector,1),
    JS_CFUNC_MAGIC_DEF("setRotationEuler",3,node_set_vector,2),JS_CFUNC_MAGIC_DEF("setRotationQuaternion",4,node_set_vector,3),
    JS_CFUNC_MAGIC_DEF("setVelocity",3,node_set_vector,4),JS_CFUNC_MAGIC_DEF("setSpin",3,node_set_vector,5),
    JS_CGETSET_DEF("visible",node_visible,node_set_visible),
    JS_CFUNC_DEF("add",1,node_add),JS_CFUNC_DEF("detach",0,node_detach),
    JS_CFUNC_DEF("getParent",0,node_parent),JS_CGETSET_DEF("childCount",node_child_count,NULL),
    JS_CFUNC_DEF("getChild",1,node_child),
    JS_CFUNC_MAGIC_DEF("getLocalTransform",0,node_matrix,0),JS_CFUNC_MAGIC_DEF("getWorldTransform",0,node_matrix,1),
    JS_CFUNC_DEF("getWorldBounds",0,node_bounds),JS_CFUNC_DEF("dispose",0,node_dispose),
    JS_CFUNC_DEF("setWeights",1,node_set_weights),JS_CFUNC_DEF("getWeights",0,node_get_weights)};
static JSValue trim_scratch(JSContext *ctx,JSValueConst this_val,int argc,JSValueConst *argv) {
    (void)this_val; (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Scene3D.trimScratch")) return JS_EXCEPTION;
    athena_scene3d_trim_scratch();
    return JS_UNDEFINED;
}
static const JSCFunctionListEntry exports[]={
    JS_CFUNC_DEF("trimScratch",0,trim_scratch),
    JS_CFUNC_MAGIC_DEF("setPositions",2,set_many,0),JS_CFUNC_MAGIC_DEF("setRotationsEuler",2,set_many,1),
    JS_CFUNC_MAGIC_DEF("setTransforms2D",2,set_many,2),
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
void athena_scene3d_js_cleanup(JSContext *ctx) { athena_scene3d_detach_owner(ctx); athena_js_atoms_free(ctx,&stat_table); }
