#include <athena_js_args.h>
#include <athena/profiler.h>
#include "ath_profiler.h"
static JSValue throw_code(JSContext *ctx,int code,const char *name) {
    switch(code) {
        case ATHENA_PROFILER_EFULL: return JS_ThrowRangeError(ctx,"Profiler: more than %u scopes",ATHENA_PROFILER_MAX_SCOPES);
        case ATHENA_PROFILER_EKIND: return JS_ThrowTypeError(ctx,"Profiler: '%s' is registered with the other kind",name?name:"?");
        case ATHENA_PROFILER_EDEPTH: return JS_ThrowRangeError(ctx,"Profiler: more than %u nested scopes",ATHENA_PROFILER_MAX_DEPTH);
        case ATHENA_PROFILER_EOPEN: return JS_ThrowRangeError(ctx,"Profiler.end() does not match the innermost begin()");
        default: return JS_ThrowRangeError(ctx,"Profiler: invalid scope name or id (1 to %u printable characters)",ATHENA_PROFILER_NAME_MAX);
    }
}
/* A scope id (integer) or name (registered with kind on first use; kind < 0
 * only looks it up). Returns the id, or a negative code with *thrown set
 * when an exception is pending. */
static int resolve(JSContext *ctx,JSValueConst value,int kind,int *thrown) {
    *thrown=0;
    if(JS_VALUE_GET_TAG(value)==JS_TAG_INT) return JS_VALUE_GET_INT(value);
    if(!JS_IsString(value)) { *thrown=1; JS_ThrowTypeError(ctx,"scope must be a name or an id"); return ATHENA_PROFILER_EINVAL; }
    const char *name=JS_ToCString(ctx,value); if(!name) { *thrown=1; return ATHENA_PROFILER_EINVAL; }
    int id=kind<0?athena_profiler_find(name):athena_profiler_scope(name,(AthenaProfilerKind)kind);
    if(id<0) { *thrown=1; throw_code(ctx,id,name); }
    JS_FreeCString(ctx,name); return id;
}
static JSValue js_scope(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,1,magic?"Profiler.counter":"Profiler.scope")) return JS_EXCEPTION;
    if(!JS_IsString(argv[0])) return JS_ThrowTypeError(ctx,"name must be a string");
    int thrown,id=resolve(ctx,argv[0],magic,&thrown);
    return thrown?JS_EXCEPTION:JS_NewInt32(ctx,id);
}
static JSValue js_begin(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,1,"Profiler.begin")) return JS_EXCEPTION;
    int thrown,id=resolve(ctx,argv[0],ATHENA_PROFILER_TIMER,&thrown); if(thrown) return JS_EXCEPTION;
    int code=athena_profiler_begin(id);
    return code<0?throw_code(ctx,code,NULL):JS_UNDEFINED;
}
static JSValue js_end(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,0,1,"Profiler.end")) return JS_EXCEPTION;
    int id=-1,thrown;
    if(argc&&!JS_IsUndefined(argv[0])) { id=resolve(ctx,argv[0],-1,&thrown); if(thrown) return JS_EXCEPTION; }
    int code=athena_profiler_end(id);
    return code<0?throw_code(ctx,code,NULL):JS_UNDEFINED;
}
static JSValue js_count(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,2,"Profiler.count")) return JS_EXCEPTION;
    float value=1;
    if(argc==2&&!athena_js_float(ctx,argv[1],&value,"value")) return JS_EXCEPTION;
    int thrown,id=resolve(ctx,argv[0],ATHENA_PROFILER_COUNTER,&thrown); if(thrown) return JS_EXCEPTION;
    int code=athena_profiler_count(id,value);
    return code<0?throw_code(ctx,code,NULL):JS_UNDEFINED;
}
static JSValue js_frame(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Profiler.frame")) return JS_EXCEPTION;
    return JS_NewFloat64(ctx,athena_profiler_frame());
}
static const char *const stat_names[]={"name","kind","samples","last","average","p95","peak","lastCalls","averageCalls"};
static JSAtom stat_atoms[countof(stat_names)];
static AthenaJSAtoms stat_table={stat_names,countof(stat_names),stat_atoms,NULL};
static JSValue js_stats(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,3,"Profiler.stats")) return JS_EXCEPTION;
    float frames=0;
    if(argc>=2&&!JS_IsUndefined(argv[1])) {
        if(!athena_js_float(ctx,argv[1],&frames,"frames")) return JS_EXCEPTION;
        if(frames<0||frames!=(float)(uint32_t)frames) return JS_ThrowRangeError(ctx,"frames must be a non-negative integer");
    }
    JSValueConst out=argc==3?argv[2]:JS_UNDEFINED;
    if(!JS_IsUndefined(out)&&!JS_IsObject(out)) return JS_ThrowTypeError(ctx,"out must be an object");
    int thrown,id=resolve(ctx,argv[0],-1,&thrown); if(thrown) return JS_EXCEPTION;
    AthenaProfilerStats s;
    if(athena_profiler_stats(id,(uint32_t)frames,&s)<0) return throw_code(ctx,ATHENA_PROFILER_EINVAL,NULL);
    JSValue obj; int define;
    if(!athena_js_out_object(ctx,out,&obj,&define,"out")) return JS_EXCEPTION;
    JSValue values[]={JS_NewString(ctx,s.name),JS_NewString(ctx,s.kind==ATHENA_PROFILER_TIMER?"timer":"counter"),
        JS_NewUint32(ctx,s.samples),JS_NewFloat64(ctx,s.last),JS_NewFloat64(ctx,s.average),JS_NewFloat64(ctx,s.p95),
        JS_NewFloat64(ctx,s.peak),JS_NewUint32(ctx,s.last_calls),JS_NewFloat64(ctx,s.average_calls)};
    int failed=0;
    for(unsigned i=0;i<countof(values);i++) {
        if(failed) { JS_FreeValue(ctx,values[i]); continue; }
        if(JS_IsException(values[i])||athena_js_put(ctx,&stat_table,obj,define,i,values[i])<0) failed=1;
    }
    if(failed) { JS_FreeValue(ctx,obj); return JS_EXCEPTION; }
    return obj;
}
static JSValue js_names(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Profiler.names")) return JS_EXCEPTION;
    JSValue array=JS_NewArray(ctx); if(JS_IsException(array)) return array;
    for(uint32_t i=0;i<athena_profiler_scope_count();i++) {
        AthenaProfilerStats s; athena_profiler_stats((int)i,1,&s);
        if(JS_SetPropertyUint32(ctx,array,i,JS_NewString(ctx,s.name))<0) { JS_FreeValue(ctx,array); return JS_EXCEPTION; }
    }
    return array;
}
static JSValue js_scope_count(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Profiler.scopeCount")) return JS_EXCEPTION;
    return JS_NewUint32(ctx,athena_profiler_scope_count());
}
static JSValue js_errors(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Profiler.errors")) return JS_EXCEPTION;
    return JS_NewUint32(ctx,athena_profiler_errors());
}
static JSValue js_reset(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,0,1,"Profiler.reset")) return JS_EXCEPTION;
    int forget=0;
    if(argc&&!JS_IsUndefined(argv[0])) {
        if(!JS_IsBool(argv[0])) return JS_ThrowTypeError(ctx,"forget must be a boolean");
        forget=JS_ToBool(ctx,argv[0]);
    }
    athena_profiler_reset(forget); return JS_UNDEFINED;
}
static const JSCFunctionListEntry exports[]={
    JS_CFUNC_MAGIC_DEF("scope",1,js_scope,ATHENA_PROFILER_TIMER),
    JS_CFUNC_MAGIC_DEF("counter",1,js_scope,ATHENA_PROFILER_COUNTER),
    JS_CFUNC_DEF("begin",1,js_begin),JS_CFUNC_DEF("end",0,js_end),JS_CFUNC_DEF("count",1,js_count),
    JS_CFUNC_DEF("frame",0,js_frame),JS_CFUNC_DEF("stats",1,js_stats),JS_CFUNC_DEF("names",0,js_names),
    JS_CFUNC_DEF("scopeCount",0,js_scope_count),
    JS_CFUNC_DEF("errors",0,js_errors),JS_CFUNC_DEF("reset",0,js_reset),
    JS_PROP_INT32_DEF("HISTORY",ATHENA_PROFILER_HISTORY,JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MAX_SCOPES",ATHENA_PROFILER_MAX_SCOPES,JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MAX_DEPTH",ATHENA_PROFILER_MAX_DEPTH,JS_PROP_ENUMERABLE)};
static int init(JSContext *ctx,JSModuleDef *m) { return JS_SetModuleExportList(ctx,m,exports,countof(exports)); }
void athena_profiler_js_cleanup(JSContext *ctx) { athena_js_atoms_free(ctx,&stat_table); }
JSModuleDef *athena_profiler_js_init(JSContext *ctx) {
    return athena_push_module(ctx,init,exports,countof(exports),"ProfilerNative");
}
