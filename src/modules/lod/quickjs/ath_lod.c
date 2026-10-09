#include <athena_js_args.h>
#include <athena/lod.h>
#include <athena/loop.h>
#include <athena/js/scene3d.h>
#include <athena/js/model3d.h>
#include <athena/js/camera3d.h>
#include <athena/js/lights.h>
#include "ath_lod.h"
static JSClassID group_id;
static AthenaLODGroup *get_group(JSContext *ctx,JSValueConst v) {
    AthenaLODGroup *g=JS_GetOpaque2(ctx,v,group_id);
    if(!g) JS_ThrowTypeError(ctx,"Expected a live LOD.Group");
    return g;
}
static void finalizer(JSRuntime *rt,JSValue v) { (void)rt; athena_lod_group_destroy(JS_GetOpaque(v,group_id)); }
/* new LOD.Group(node, [{ mesh, until }, ...], { hysteresis = 0.1 }) */
static JSValue ctor(JSContext *ctx,JSValueConst target,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,2,3,"LOD.Group")) return JS_EXCEPTION;
    AthenaNode3D *node=athena_node3d_from_value(ctx,argv[0]); if(!node) return JS_EXCEPTION;
    if(!JS_IsArray(ctx,argv[1])) return JS_ThrowTypeError(ctx,"LOD.Group levels must be an array of { mesh, until }");
    float hysteresis=.1f;
    if(argc==3&&!JS_IsUndefined(argv[2])) {
        if(!JS_IsObject(argv[2])) return JS_ThrowTypeError(ctx,"LOD.Group options must be an object");
        if(!athena_js_option_float(ctx,argv[2],"hysteresis",&hysteresis)) return JS_EXCEPTION;
    }
    JSValue len=JS_GetPropertyStr(ctx,argv[1],"length"); uint32_t count=0;
    if(JS_IsException(len)||JS_ToUint32(ctx,&count,len)<0) { JS_FreeValue(ctx,len); return JS_EXCEPTION; }
    JS_FreeValue(ctx,len);
    if(!count||count>ATHENA_LOD_MAX_LEVELS) return JS_ThrowRangeError(ctx,"LOD.Group needs 1 to %u levels",ATHENA_LOD_MAX_LEVELS);
    AthenaLODLevel levels[ATHENA_LOD_MAX_LEVELS];
    uint32_t retained=0;
    JSValue obj=JS_EXCEPTION;
    for(uint32_t i=0;i<count;i++) {
        JSValue item=JS_GetPropertyUint32(ctx,argv[1],i); if(JS_IsException(item)) goto done;
        if(!JS_IsObject(item)) { JS_FreeValue(ctx,item); JS_ThrowTypeError(ctx,"LOD level %u must be { mesh, until }",(unsigned)i); goto done; }
        JSValue mesh=JS_GetPropertyStr(ctx,item,"mesh");
        float until=0; int ok=!JS_IsException(mesh)&&athena_js_option_float(ctx,item,"until",&until);
        levels[i].mesh=NULL; levels[i].until=until;
        if(ok&&!JS_IsNull(mesh)&&!JS_IsUndefined(mesh)) ok=(levels[i].mesh=athena_mesh3d_from_value(ctx,mesh))!=NULL;
        /* Later getters may dispose earlier mesh wrappers or trigger GC. */
        if(ok) { athena_mesh3d_retain(levels[i].mesh); retained++; }
        JS_FreeValue(ctx,mesh); JS_FreeValue(ctx,item);
        if(!ok) goto done;
    }
    /* Getters above may have run script: look the node up again. */
    JSValue proto=JS_GetPropertyStr(ctx,target,"prototype"); if(JS_IsException(proto)) goto done;
    obj=JS_NewObjectProtoClass(ctx,proto,group_id); JS_FreeValue(ctx,proto);
    if(JS_IsException(obj)) goto done;
    node=athena_node3d_from_value(ctx,argv[0]);
    if(!node) { JS_FreeValue(ctx,obj); obj=JS_EXCEPTION; goto done; }
    AthenaLODGroup *g=athena_lod_group_create(node,levels,count,hysteresis);
    if(!g) { JS_FreeValue(ctx,obj); obj=JS_ThrowRangeError(ctx,"LOD.Group: `until` must be positive and increasing, hysteresis 0 to 0.5"); goto done; }
    JS_SetOpaque(obj,g);
done:
    for(uint32_t i=0;i<retained;i++) athena_mesh3d_release(levels[i].mesh);
    return obj;
}
static JSValue dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Group.dispose")||!athena_js_class(ctx,self,group_id)) return JS_EXCEPTION;
    AthenaLODGroup *g=JS_GetOpaque(self,group_id); JS_SetOpaque(self,NULL); athena_lod_group_destroy(g);
    return JS_UNDEFINED;
}
static JSValue get_level(JSContext *ctx,JSValueConst self) {
    AthenaLODGroup *g=get_group(ctx,self); return g?JS_NewInt32(ctx,athena_lod_group_level(g)):JS_EXCEPTION;
}
static JSValue get_distance(JSContext *ctx,JSValueConst self) {
    AthenaLODGroup *g=get_group(ctx,self); return g?JS_NewFloat64(ctx,athena_lod_group_distance(g)):JS_EXCEPTION;
}
static JSValue set_enabled(JSContext *ctx,JSValueConst self,JSValueConst v) {
    AthenaLODGroup *g=get_group(ctx,self); if(!g) return JS_EXCEPTION;
    athena_lod_group_set_enabled(g,JS_ToBool(ctx,v)); return JS_UNDEFINED;
}
static JSValue get_enabled(JSContext *ctx,JSValueConst self) {
    AthenaLODGroup *g=get_group(ctx,self); return g?JS_NewBool(ctx,athena_lod_group_enabled(g)):JS_EXCEPTION;
}
static AthenaLODStats last;
static AthenaCamera3D *auto_camera;
static int auto_system;
static void update_with(AthenaCamera3D *c) {
    float eye[3]={c->position.x,c->position.y,c->position.z};
    athena_lod_update(eye,&last);
}
static int loop_func(void *opaque,AthenaLoopPhase phase,float value) {
    (void)opaque; (void)phase; (void)value;
    if(auto_camera) update_with(auto_camera);
    return 0;
}
static void loop_release(void *opaque) {
    (void)opaque; auto_system=0;
    if(auto_camera) { athena_camera3d_js_release(auto_camera); auto_camera=NULL; }
}
/* setCamera(camera | null): selects levels every frame in a Loop
 * POST_UPDATE system that runs before Scene3D's (priority -100). */
static JSValue js_set_camera(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,1,"LOD.setCamera")) return JS_EXCEPTION;
    AthenaCamera3D *c=NULL;
    if(!JS_IsNull(argv[0])&&!(c=athena_camera3d_from_value(ctx,argv[0]))) return JS_EXCEPTION;
    if(c) athena_camera3d_js_retain(c);
    if(auto_camera) athena_camera3d_js_release(auto_camera);
    auto_camera=c;
    if(c&&!auto_system) {
        AthenaLoopSystemDesc desc={.name="lod",.priority=-100,.phases=ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_POST_UPDATE),
            .func=loop_func,.release=loop_release};
        int id=athena_loop_system_add(&desc);
        if(id<0) { athena_camera3d_js_release(c); auto_camera=NULL; return JS_ThrowInternalError(ctx,"LOD: cannot add the Loop system"); }
        auto_system=id;
    } else if(!c&&auto_system) athena_loop_system_remove(auto_system);
    return JS_UNDEFINED;
}
static const char *const names[]={"groups","hidden","changes","perLevel"};
static JSAtom atoms[countof(names)];
static AthenaJSAtoms table={names,countof(names),atoms,NULL};
static JSValue put_stats(JSContext *ctx,JSValueConst out) {
    JSValue obj; int define;
    if(!athena_js_out_object(ctx,out,&obj,&define,"out")) return JS_EXCEPTION;
    JSValue levels=JS_NewArray(ctx);
    for(uint32_t i=0;i<ATHENA_LOD_MAX_LEVELS&&!JS_IsException(levels);i++) JS_SetPropertyUint32(ctx,levels,i,JS_NewUint32(ctx,last.per_level[i]));
    const uint32_t v[]={last.groups,last.hidden,last.changes};
    int failed=JS_IsException(levels);
    for(unsigned i=0;!failed&&i<3;i++) failed=athena_js_put(ctx,&table,obj,define,i,JS_NewUint32(ctx,v[i]))<0;
    if(!failed) failed=athena_js_put(ctx,&table,obj,define,3,levels)<0; else JS_FreeValue(ctx,levels);
    if(failed) { JS_FreeValue(ctx,obj); return JS_EXCEPTION; }
    return obj;
}
/* update(camera, stats?): selects every group's level now (games without setCamera). */
static JSValue js_update(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,2,"LOD.update")) return JS_EXCEPTION;
    AthenaCamera3D *c=athena_camera3d_from_value(ctx,argv[0]); if(!c) return JS_EXCEPTION;
    update_with(c);
    if(argc==2&&!JS_IsUndefined(argv[1])) return put_stats(ctx,argv[1]);
    return JS_NewUint32(ctx,last.changes);
}
static JSValue js_stats(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,0,1,"LOD.stats")) return JS_EXCEPTION;
    return put_stats(ctx,argc?argv[0]:JS_UNDEFINED);
}
static JSValue js_set_bias(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,1,"LOD.setBias")) return JS_EXCEPTION;
    float b; if(!athena_js_float(ctx,argv[0],&b,"bias")) return JS_EXCEPTION;
    if(athena_lod_set_bias(b)<0) return JS_ThrowRangeError(ctx,"bias must be positive");
    return JS_UNDEFINED;
}
/* setDrawDistance(distance, { lights, fogStart, color: [r, g, b] }): hides
 * nodes beyond distance and, with lights, fades to color from fogStart
 * (default 60% of distance) so the cut is not visible. 0 removes the limit
 * (and the fog when lights are given). */
static JSValue js_set_draw_distance(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,2,"LOD.setDrawDistance")) return JS_EXCEPTION;
    float d; if(!athena_js_float(ctx,argv[0],&d,"distance")) return JS_EXCEPTION;
    if(d<0) return JS_ThrowRangeError(ctx,"distance must not be negative");
    AthenaLights *lights=NULL; float start=d*.6f,color[3]={.5f,.6f,.75f};
    if(argc==2&&!JS_IsUndefined(argv[1])) {
        JSValueConst o=argv[1];
        if(!JS_IsObject(o)) return JS_ThrowTypeError(ctx,"setDrawDistance options must be an object");
        if(!athena_js_option_float(ctx,o,"fogStart",&start)) return JS_EXCEPTION;
        JSValue c=JS_GetPropertyStr(ctx,o,"color"); if(JS_IsException(c)) return c;
        for(uint32_t i=0;!JS_IsUndefined(c)&&i<3;i++) {
            JSValue v=JS_GetPropertyUint32(ctx,c,i); int ok=athena_js_float(ctx,v,&color[i],"color"); JS_FreeValue(ctx,v);
            if(!ok) { JS_FreeValue(ctx,c); return JS_EXCEPTION; }
        }
        JS_FreeValue(ctx,c);
        JSValue l=JS_GetPropertyStr(ctx,o,"lights"); if(JS_IsException(l)) return l;
        if(!JS_IsUndefined(l)) { lights=athena_lights_from_value(ctx,l); JS_FreeValue(ctx,l); if(!lights) return JS_EXCEPTION; }
        else JS_FreeValue(ctx,l);
    }
    for(int i=0;i<3;i++) if(color[i]<0||color[i]>1) return JS_ThrowRangeError(ctx,"setDrawDistance: color must be in [0, 1]");
    if(lights) {
        if(d==0) athena_lights_disable_fog(lights);
        else if(athena_lights_set_fog(lights,start<d?start:d*.6f,d,color[0],color[1],color[2])<0)
            return JS_ThrowRangeError(ctx,"setDrawDistance: fog needs 0 <= fogStart < distance and color in [0, 1]");
    }
    athena_lod_set_draw_distance(d);
    return JS_UNDEFINED;
}
static JSClassDef class_def={"LOD.Group",.finalizer=finalizer};
static const JSCFunctionListEntry methods[]={
    JS_CFUNC_DEF("dispose",0,dispose),JS_CGETSET_DEF("level",get_level,NULL),
    JS_CGETSET_DEF("distance",get_distance,NULL),JS_CGETSET_DEF("enabled",get_enabled,set_enabled)};
static const JSCFunctionListEntry exports[]={
    JS_CFUNC_DEF("setCamera",1,js_set_camera),JS_CFUNC_DEF("update",1,js_update),JS_CFUNC_DEF("stats",0,js_stats),
    JS_CFUNC_DEF("setBias",1,js_set_bias),JS_CFUNC_DEF("setDrawDistance",1,js_set_draw_distance),
    JS_PROP_INT32_DEF("MAX_LEVELS",ATHENA_LOD_MAX_LEVELS,JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("HIDDEN",-1,JS_PROP_ENUMERABLE)};
static int init(JSContext *ctx,JSModuleDef *m) {
    if(athena_register_class(ctx,&group_id,&class_def)<0) return -1;
    JSValue proto=JS_NewObject(ctx); if(JS_IsException(proto)) return -1;
    JS_SetPropertyFunctionList(ctx,proto,methods,countof(methods));
    JSValue cls=JS_NewCFunction2(ctx,ctor,"Group",2,JS_CFUNC_constructor,0);
    JS_SetConstructor(ctx,cls,proto); JS_SetClassProto(ctx,group_id,proto);
    if(JS_SetModuleExport(ctx,m,"Group",cls)<0) return -1;
    return JS_SetModuleExportList(ctx,m,exports,countof(exports));
}
void athena_lod_js_cleanup(JSContext *ctx) {
    athena_js_atoms_free(ctx,&table);
    athena_lod_set_bias(1); athena_lod_set_draw_distance(0);   /* settings belong to the runtime */
    if(auto_system) athena_loop_system_remove(auto_system);
    if(auto_camera) { athena_camera3d_js_release(auto_camera); auto_camera=NULL; }
}
JSModuleDef *athena_lod_js_init(JSContext *ctx) {
    JSModuleDef *m=athena_push_module(ctx,init,exports,countof(exports),"LOD");
    if(m) JS_AddModuleExport(ctx,m,"Group");
    return m;
}
