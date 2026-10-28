#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <athena_js_args.h>
#include <athena/gltf3d.h>
#include <athena/js/scene3d.h>
#include <athena/js/animation3d.h>
#include <athena/js/model3d.h>
#include "ath_gltf3d.h"
/* The scene as { root, nodes, names, clips } handles that hold their own
 * references. Does not release scene. */
static JSValue scene_value(JSContext *ctx,AthenaGltf3D *scene) {
    JSValue result=JS_NewObject(ctx),nodes=JS_NewArray(ctx),names=JS_NewArray(ctx),clips=JS_NewObject(ctx);
    int failed=JS_IsException(result)||JS_IsException(nodes)||JS_IsException(names)||JS_IsException(clips);
    for(uint32_t i=0;!failed&&i<athena_gltf3d_node_count(scene);i++) {
        JSValue node=athena_node3d_to_value(ctx,athena_gltf3d_node(scene,i));
        failed=JS_IsException(node)||JS_SetPropertyUint32(ctx,nodes,i,node)<0||
            JS_SetPropertyUint32(ctx,names,i,JS_NewString(ctx,athena_gltf3d_node_name(scene,i)))<0;
    }
    for(uint32_t i=0;!failed&&i<athena_gltf3d_clip_count(scene);i++) {
        char index[16]; const char *name=athena_gltf3d_clip_name(scene,i);
        if(!*name) { snprintf(index,sizeof(index),"%u",(unsigned)i); name=index; }
        JSValue clip=athena_clip3d_to_value(ctx,athena_gltf3d_clip(scene,i));
        failed=JS_IsException(clip)||JS_SetPropertyStr(ctx,clips,name,clip)<0;
    }
    if(!failed) {
        JSValue root=athena_node3d_to_value(ctx,athena_gltf3d_root(scene));
        failed=JS_IsException(root)||JS_SetPropertyStr(ctx,result,"root",root)<0||
            JS_SetPropertyStr(ctx,result,"nodes",JS_DupValue(ctx,nodes))<0||
            JS_SetPropertyStr(ctx,result,"names",JS_DupValue(ctx,names))<0||
            JS_SetPropertyStr(ctx,result,"clips",JS_DupValue(ctx,clips))<0;
    }
    JS_FreeValue(ctx,nodes); JS_FreeValue(ctx,names); JS_FreeValue(ctx,clips);
    if(failed) { JS_FreeValue(ctx,result); return JS_EXCEPTION; }
    return result;
}
/* GLTF3D.load(path, material?): { root, nodes, names, clips } as handles that
 * hold their own references; the loaded scene itself is released before
 * returning. material replaces every primitive's material, as Model3D.load(). */
static JSClassID loader_id;
/* The loader plus the JS buffer it reads while copying: pinned (a nonshared
 * ArrayBuffer is fixed-size and cannot be detached by scripts) until the copy
 * phase ends, then released. */
#define MAX_DEPS 64
typedef struct { AthenaGltf3DLoader *loader; JSValue backing; uint32_t ran; JSValue deps[MAX_DEPS]; } JSLoader;
static JSValue loader_value(JSContext *ctx,AthenaGltf3DLoader *loader,JSValue backing);
static int external_option(JSContext *ctx,int argc,JSValueConst *argv,int *flags);
/* mode 0: path, 1: whole GLB from memory, 2: resumable loader from memory. */
static JSValue load_source(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int mode) {
    (void)self;
    int memory=mode!=0;
    const char *name=mode==2?"GLTF3D.beginMemory":memory?"GLTF3D.loadMemory":"GLTF3D.load";
    int index=memory?1:0;
    if(!athena_js_argc(ctx,argc,index+1,mode==2?index+3:index+2,name)) return JS_EXCEPTION;
    int external=0;
    if(mode==2&&!external_option(ctx,argc,argv,&external)) return JS_EXCEPTION;
    if(!JS_IsString(argv[index])) return JS_ThrowTypeError(ctx,"%s expects a path/origin string",name);
    int override=argc>=index+2&&!JS_IsUndefined(argv[index+1]);
    AthenaMaterial3D material;
    if(!athena_model3d_js_material_borrow(ctx,override?argv[index+1]:JS_UNDEFINED,&material)) return JS_EXCEPTION;
    size_t length=0; const char *path=JS_ToCStringLen(ctx,&length,argv[index]);
    if(!path) { athena_texture3d_release(material.texture); return JS_EXCEPTION; }
    if(!length||strlen(path)!=length) {
        JS_FreeCString(ctx,path); athena_texture3d_release(material.texture);
        return JS_ThrowRangeError(ctx,"Path/origin must be nonempty without NUL");
    }
    AthenaJSArray input;
    if(memory&&!athena_js_bytes(ctx,argv[0],&input,"GLB bytes")) {
        JS_FreeCString(ctx,path); athena_texture3d_release(material.texture); return JS_EXCEPTION;
    }
    AthenaGltf3D *scene=NULL; AthenaGltf3DLoader *loader=NULL;
    int code=mode==2?athena_gltf3d_loader_begin(input.data,input.count,path,override?&material:NULL,external,&loader):
        memory?athena_gltf3d_load_memory(input.data,input.count,path,override?&material:NULL,&scene):
        athena_gltf3d_load(path,override?&material:NULL,&scene);
    if(memory&&(mode!=2||code<0)) JS_FreeValue(ctx,input.backing);
    athena_texture3d_release(material.texture);
    if(code<0) {
        JSValue error=code==ATHENA_MODEL3D_ENOMEM?JS_ThrowOutOfMemory(ctx):
            JS_ThrowTypeError(ctx,"%s(%s): %s%s%s",name,path,athena_model3d_error(code),
                athena_model3d_detail()[0]?": ":"",athena_model3d_detail());
        JS_FreeCString(ctx,path); return error;
    }
    JS_FreeCString(ctx,path);
    if(mode==2) return loader_value(ctx,loader,input.backing);
    JSValue result=scene_value(ctx,scene);
    athena_gltf3d_release(scene);
    return result;
}
static JSValue load(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    return load_source(ctx,self,argc,argv,0);
}
static JSValue load_memory(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    return load_source(ctx,self,argc,argv,1);
}
static JSValue begin_memory(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    return load_source(ctx,self,argc,argv,2);
}
static const char *const phase_names[]={"copy","parse","dependencies","validate","nodes","links","skins","clips","done"};
/* options: { dependencies: true } selects the explicit resolver (external files),
 * { borrow: true } uses the bytes in place (no snapshot copy; pinned until finish/dispose). */
static int option_flag(JSContext *ctx,JSValueConst options,const char *name,int bit,int *flags) {
    JSValue v=JS_GetPropertyStr(ctx,options,name);
    if(JS_IsException(v)) return 0;
    if(!JS_IsUndefined(v)&&!JS_IsBool(v)) { JS_FreeValue(ctx,v); JS_ThrowTypeError(ctx,"GLTF3D.beginMemory options.%s must be a boolean",name); return 0; }
    if(JS_ToBool(ctx,v)>0) *flags|=bit;
    JS_FreeValue(ctx,v); return 1;
}
static int external_option(JSContext *ctx,int argc,JSValueConst *argv,int *flags) {
    *flags=0;
    if(argc<4||JS_IsUndefined(argv[3])) return 1;
    if(!JS_IsObject(argv[3])) { JS_ThrowTypeError(ctx,"GLTF3D.beginMemory options must be an object"); return 0; }
    return option_flag(ctx,argv[3],"dependencies",ATHENA_LOADER_EXTERNAL,flags)&&option_flag(ctx,argv[3],"borrow",ATHENA_LOADER_BORROW,flags);
}
static void loader_free(JSRuntime *rt,JSLoader *state) {
    if(!state) return;
    athena_gltf3d_loader_destroy(state->loader);
    JS_FreeValueRT(rt,state->backing);
    for(int i=0;i<MAX_DEPS;i++) JS_FreeValueRT(rt,state->deps[i]);
    free(state);
}
static void loader_finalizer(JSRuntime *rt,JSValue v) { loader_free(rt,JS_GetOpaque(v,loader_id)); }
static JSClassDef loader_class={"GLTF3D.Loader",.finalizer=loader_finalizer};
static JSValue loader_value(JSContext *ctx,AthenaGltf3DLoader *loader,JSValue backing) {
    JSLoader *state=malloc(sizeof(*state));
    JSValue value=state?JS_NewObjectClass(ctx,loader_id):JS_ThrowOutOfMemory(ctx);
    if(!state||JS_IsException(value)) {
        athena_gltf3d_loader_destroy(loader); JS_FreeValue(ctx,backing); free(state); return JS_EXCEPTION;
    }
    state->loader=loader; state->backing=backing;
    for(int i=0;i<MAX_DEPS;i++) state->deps[i]=JS_UNDEFINED;
    JS_SetOpaque(value,state); return value;
}
static JSLoader *loader_of(JSContext *ctx,JSValueConst this_val) {
    JSLoader *state=JS_GetOpaque(this_val,loader_id);
    if(!state) JS_ThrowTypeError(ctx,"GLTF3D.Loader is disposed, finished or not a Loader");
    return state;
}
/* Drops the pin once no step can read the caller's bytes any more. */
static void loader_unpin(JSContext *ctx,JSLoader *state) {
    if(!JS_IsUndefined(state->backing)&&!athena_gltf3d_loader_source_needed(state->loader)) {
        JS_FreeValue(ctx,state->backing); state->backing=JS_UNDEFINED;
    }
}
/* step(): false while work remains, true once finished. Throws on error and
 * keeps throwing; dispose() afterwards. One unit is atomic. */
static JSValue loader_step(JSContext *ctx,JSValueConst this_val,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,0,1,"GLTF3D.Loader.step")) return JS_EXCEPTION;
    int32_t units=1;
    if(argc==1&&!JS_IsUndefined(argv[0])) {
        double d;
        if(JS_ToFloat64(ctx,&d,argv[0])<0) return JS_EXCEPTION;
        if(!(d>=1&&d<=64&&d==(double)(int32_t)d)) return JS_ThrowRangeError(ctx,"GLTF3D.Loader.step units must be an integer from 1 to 64");
        units=(int32_t)d;
    }
    JSLoader *state=loader_of(ctx,this_val); if(!state) return JS_EXCEPTION;
    uint32_t ran=0; int code=0,phase=athena_gltf3d_loader_phase(state->loader);
    /* Up to `units` steps while the phase stays the same: one JS call for a batch of similar units. */
    for(int32_t i=0;i<units&&code==0;i++) {
        code=athena_gltf3d_loader_step(state->loader);
        if(code==ATHENA_GLTF3D_WAITING) { code=0; break; }
        ran++;
        if(code==0&&athena_gltf3d_loader_phase(state->loader)!=phase) break;
    }
    state->ran=ran;
    loader_unpin(ctx,state);
    if(code<0) return code==ATHENA_MODEL3D_ENOMEM?JS_ThrowOutOfMemory(ctx):
        JS_ThrowTypeError(ctx,"GLTF3D.beginMemory: %s%s%s",athena_model3d_error(code),
            athena_model3d_detail()[0]?": ":"",athena_model3d_detail());
    return JS_NewBool(ctx,code==1);
}
/* run(budgetMs?): at least one unit, then more until budgetMs elapsed (default 0: one).
 * Returns true once finished; lastUnits is the number of units that ran. */
static JSValue loader_run(JSContext *ctx,JSValueConst this_val,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,0,1,"GLTF3D.Loader.run")) return JS_EXCEPTION;
    double budget=0;
    if(argc==1&&!JS_IsUndefined(argv[0])) {
        if(JS_ToFloat64(ctx,&budget,argv[0])<0) return JS_EXCEPTION;
        if(!(budget>=0&&budget<=1000)) return JS_ThrowRangeError(ctx,"GLTF3D.Loader.run budget must be 0 to 1000 ms");
    }
    JSLoader *state=loader_of(ctx,this_val); if(!state) return JS_EXCEPTION;
    uint32_t ran=0;
    int code=athena_gltf3d_loader_run(state->loader,(float)budget,&ran);
    state->ran=ran;
    loader_unpin(ctx,state);
    if(code<0) return code==ATHENA_MODEL3D_ENOMEM?JS_ThrowOutOfMemory(ctx):
        JS_ThrowTypeError(ctx,"GLTF3D.beginMemory: %s%s%s",athena_model3d_error(code),
            athena_model3d_detail()[0]?": ":"",athena_model3d_detail());
    return JS_NewBool(ctx,code==1);
}
/* { phase: { units, totalMs, maxMs } } for the phases that ran, so far. */
static JSValue loader_phase_stats(JSContext *ctx,JSValueConst this_val) {
    JSLoader *state=JS_GetOpaque(this_val,loader_id);
    JSValue o=JS_NewObject(ctx); if(JS_IsException(o)||!state) return o;
    for(int ph=0;ph<=ATHENA_GLTF3D_PHASE_DONE;ph++) {
        AthenaLoaderPhaseStats st; athena_gltf3d_loader_phase_stats(state->loader,ph,&st);
        if(!st.units) continue;
        JSValue e=JS_NewObject(ctx);
        if(JS_IsException(e)||JS_SetPropertyStr(ctx,e,"units",JS_NewUint32(ctx,st.units))<0||
            JS_SetPropertyStr(ctx,e,"totalMs",JS_NewFloat64(ctx,st.total_ms))<0||JS_SetPropertyStr(ctx,e,"maxMs",JS_NewFloat64(ctx,st.max_ms))<0||
            JS_SetPropertyStr(ctx,o,phase_names[ph],e)<0) { JS_FreeValue(ctx,o); return JS_EXCEPTION; }
    }
    return o;
}
/* phaseStatsInto(Float64Array): units, totalMs, maxMs per phase at index 3*phase; no allocation. */
static JSValue loader_phase_stats_into(JSContext *ctx,JSValueConst this_val,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"GLTF3D.Loader.phaseStatsInto")) return JS_EXCEPTION;
    JSLoader *state=loader_of(ctx,this_val); if(!state) return JS_EXCEPTION;
    AthenaJSArray out;
    if(!athena_js_array(ctx,argv[0],JS_TYPED_ARRAY_FLOAT64,&out,"stats")) return JS_EXCEPTION;
    if(out.count<3*(ATHENA_GLTF3D_PHASE_DONE+1)) { JS_FreeValue(ctx,out.backing); return JS_ThrowRangeError(ctx,"stats needs %d elements",3*(ATHENA_GLTF3D_PHASE_DONE+1)); }
    double *d=out.data;
    for(int ph=0;ph<=ATHENA_GLTF3D_PHASE_DONE;ph++) {
        AthenaLoaderPhaseStats st; athena_gltf3d_loader_phase_stats(state->loader,ph,&st);
        d[3*ph]=st.units; d[3*ph+1]=st.total_ms; d[3*ph+2]=st.max_ms;
    }
    JS_FreeValue(ctx,out.backing); return JS_UNDEFINED;
}
/* finish(): the Asset, once step() returned true. The loader is then spent. */
static JSValue loader_finish(JSContext *ctx,JSValueConst this_val,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"GLTF3D.Loader.finish")) return JS_EXCEPTION;
    JSLoader *state=loader_of(ctx,this_val); if(!state) return JS_EXCEPTION;
    AthenaGltf3D *scene=NULL;
    if(athena_gltf3d_loader_take(state->loader,&scene)<0) return JS_ThrowTypeError(ctx,"GLTF3D.Loader.finish: loading is not complete");
    JS_SetOpaque(this_val,NULL); loader_free(JS_GetRuntime(ctx),state);
    JSValue result=scene_value(ctx,scene);
    athena_gltf3d_release(scene);
    return result;
}
/* provide(index, bytes): the file for dependencies[index]. Borrowed: the buffer is
 * pinned until the loader finishes or is disposed; do not modify it. */
static JSValue loader_provide(JSContext *ctx,JSValueConst this_val,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,2,2,"GLTF3D.Loader.provide")) return JS_EXCEPTION;
    JSLoader *state=loader_of(ctx,this_val); if(!state) return JS_EXCEPTION;
    int32_t index;
    if(JS_ToInt32(ctx,&index,argv[0])<0) return JS_EXCEPTION;
    if(index<0||(uint32_t)index>=athena_gltf3d_loader_dependency_count(state->loader))
        return JS_ThrowRangeError(ctx,"GLTF3D.Loader.provide: no such dependency");
    AthenaJSArray input;
    if(!athena_js_bytes(ctx,argv[1],&input,"dependency bytes")) return JS_EXCEPTION;
    int code=athena_gltf3d_loader_provide(state->loader,(uint32_t)index,input.data,input.count);
    if(code<0) {
        JS_FreeValue(ctx,input.backing);
        return JS_ThrowTypeError(ctx,"GLTF3D.Loader.provide: not waiting for files, already provided, empty or over 16 MiB");
    }
    state->deps[index]=input.backing;
    return JS_UNDEFINED;
}
/* [{ uri, image, provided }]: empty until the parse step ran. */
static JSValue loader_dependencies(JSContext *ctx,JSValueConst this_val) {
    JSLoader *state=JS_GetOpaque(this_val,loader_id);
    JSValue list=JS_NewArray(ctx); if(JS_IsException(list)) return list;
    uint32_t n=state?athena_gltf3d_loader_dependency_count(state->loader):0;
    for(uint32_t i=0;i<n;i++) {
        JSValue o=JS_NewObject(ctx);
        if(JS_IsException(o)||JS_SetPropertyStr(ctx,o,"uri",JS_NewString(ctx,athena_gltf3d_loader_dependency_uri(state->loader,i)))<0||
            JS_SetPropertyStr(ctx,o,"image",JS_NewBool(ctx,athena_gltf3d_loader_dependency_image(state->loader,i)))<0||
            JS_SetPropertyStr(ctx,o,"provided",JS_NewBool(ctx,athena_gltf3d_loader_dependency_provided(state->loader,i)))<0||
            JS_SetPropertyUint32(ctx,list,i,o)<0) { JS_FreeValue(ctx,list); return JS_EXCEPTION; }
    }
    return list;
}
static JSValue loader_dispose(JSContext *ctx,JSValueConst this_val,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"GLTF3D.Loader.dispose")) return JS_EXCEPTION;
    JSLoader *state=JS_GetOpaque(this_val,loader_id);
    if(!state) return JS_UNDEFINED;
    JS_SetOpaque(this_val,NULL); loader_free(JS_GetRuntime(ctx),state);
    return JS_UNDEFINED;
}
/* magic 0: next phase, 1: phase of the last step, 2: units done, 3: total. */
static JSValue loader_info(JSContext *ctx,JSValueConst this_val,int magic) {
    JSLoader *state=JS_GetOpaque(this_val,loader_id);
    if(!state) return magic<2?JS_NewString(ctx,"disposed"):JS_NewInt32(ctx,0);
    if(magic==4) return JS_NewUint32(ctx,state->ran);
    if(magic<2) return JS_NewString(ctx,phase_names[magic?athena_gltf3d_loader_last_phase(state->loader):athena_gltf3d_loader_phase(state->loader)]);
    uint32_t done,total; athena_gltf3d_loader_progress(state->loader,&done,&total);
    return JS_NewUint32(ctx,magic==2?done:total);
}
static const JSCFunctionListEntry loader_methods[]={JS_CFUNC_DEF("step",0,loader_step),JS_CFUNC_DEF("run",0,loader_run),JS_CFUNC_DEF("phaseStatsInto",1,loader_phase_stats_into),JS_CGETSET_DEF("phaseStats",loader_phase_stats,NULL),
    JS_CFUNC_DEF("finish",0,loader_finish),JS_CFUNC_DEF("provide",2,loader_provide),JS_CGETSET_DEF("dependencies",loader_dependencies,NULL),JS_CFUNC_DEF("dispose",0,loader_dispose),
    JS_CGETSET_MAGIC_DEF("phase",loader_info,NULL,0),JS_CGETSET_MAGIC_DEF("lastPhase",loader_info,NULL,1),
    JS_CGETSET_MAGIC_DEF("unitsDone",loader_info,NULL,2),JS_CGETSET_MAGIC_DEF("unitsTotal",loader_info,NULL,3),JS_CGETSET_MAGIC_DEF("lastUnits",loader_info,NULL,4)};
static JSValue forbidden_ctor(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx,"Use GLTF3D.beginMemory");
}
static const JSCFunctionListEntry exports[]={JS_CFUNC_DEF("load",2,load),JS_CFUNC_DEF("loadMemory",2,load_memory),
    JS_CFUNC_DEF("beginMemory",2,begin_memory)};
static int init(JSContext *ctx,JSModuleDef *m) {
    if(athena_register_class(ctx,&loader_id,&loader_class)<0) return -1;
    JSValue proto=JS_NewObject(ctx),ctor=JS_NewCFunction2(ctx,forbidden_ctor,"Loader",0,JS_CFUNC_constructor,0);
    if(JS_IsException(proto)||JS_IsException(ctor)) { JS_FreeValue(ctx,proto); JS_FreeValue(ctx,ctor); return -1; }
    JS_SetPropertyFunctionList(ctx,proto,loader_methods,countof(loader_methods));
    JS_SetConstructor(ctx,ctor,proto); JS_SetClassProto(ctx,loader_id,proto);
    {
        JSValue names=JS_NewArray(ctx);
        for(uint32_t i=0;i<countof(phase_names);i++) JS_SetPropertyUint32(ctx,names,i,JS_NewString(ctx,phase_names[i]));
        JS_SetPropertyStr(ctx,ctor,"PHASES",names); /* phase names by native phase index */
    }
    if(JS_SetModuleExport(ctx,m,"Loader",ctor)<0) return -1;
    return JS_SetModuleExportList(ctx,m,exports,countof(exports));
}
JSModuleDef *athena_gltf3d_js_init(JSContext *ctx) {
    JSModuleDef *m=athena_push_module(ctx,init,exports,countof(exports),"GLTF3D");
    if(m) JS_AddModuleExport(ctx,m,"Loader");
    return m;
}
