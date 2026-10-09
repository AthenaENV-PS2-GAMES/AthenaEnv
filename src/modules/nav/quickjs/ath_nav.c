#include <math.h>
#include <athena_js_args.h>
#include <athena/nav.h>
#include <athena/js/scene3d.h>
#include "ath_nav.h"
static JSClassID grid_id,crowd_id,agent_id;
typedef struct { AthenaNavCrowd *crowd; int id; } AgentHandle;
static AthenaNavGrid *get_grid(JSContext *ctx,JSValueConst v) {
    AthenaNavGrid *g=JS_GetOpaque2(ctx,v,grid_id); if(!g) JS_ThrowTypeError(ctx,"Expected a live Nav.Grid"); return g;
}
static AthenaNavCrowd *get_crowd(JSContext *ctx,JSValueConst v) {
    AthenaNavCrowd *c=JS_GetOpaque2(ctx,v,crowd_id); if(!c) JS_ThrowTypeError(ctx,"Expected a live Nav.Crowd"); return c;
}
static AgentHandle *get_agent(JSContext *ctx,JSValueConst v) {
    AgentHandle *a=JS_GetOpaque2(ctx,v,agent_id); if(!a) JS_ThrowTypeError(ctx,"Expected a live Nav.Agent"); return a;
}
static void grid_finalizer(JSRuntime *rt,JSValue v) { (void)rt; athena_nav_grid_release(JS_GetOpaque(v,grid_id)); }
static void crowd_finalizer(JSRuntime *rt,JSValue v) { (void)rt; athena_nav_crowd_release(JS_GetOpaque(v,crowd_id)); }
static void agent_free(AgentHandle *a) {
    if(!a) return;
    athena_nav_agent_remove(a->crowd,a->id); athena_nav_crowd_release(a->crowd); free(a);
}
static void agent_finalizer(JSRuntime *rt,JSValue v) { (void)rt; agent_free(JS_GetOpaque(v,agent_id)); }
static JSValue new_object(JSContext *ctx,JSValueConst target,JSClassID id) {
    JSValue proto=JS_GetPropertyStr(ctx,target,"prototype"); if(JS_IsException(proto)) return proto;
    JSValue obj=JS_NewObjectProtoClass(ctx,proto,id); JS_FreeValue(ctx,proto); return obj;
}
static int floats(JSContext *ctx,JSValueConst *argv,int n,float *out,const char *name) {
    for(int i=0;i<n;i++) if(!athena_js_float(ctx,argv[i],&out[i],name)) return 0;
    return 1;
}
static int cell_args(JSContext *ctx,JSValueConst *argv,int n,int32_t *out) {
    for(int i=0;i<n;i++) if(JS_ToInt32(ctx,&out[i],argv[i])<0) return 0;
    return 1;
}
static int cost_arg(JSContext *ctx,JSValueConst v,uint8_t *out) {
    int32_t c; if(JS_ToInt32(ctx,&c,v)<0) return 0;
    if(c<0||c>255) { JS_ThrowRangeError(ctx,"cost must be 0 (blocked) to 255"); return 0; }
    *out=(uint8_t)c; return 1;
}
/* new Nav.Grid(width, depth, { cellSize = 1, x = 0, z = 0 }) */
static JSValue grid_ctor(JSContext *ctx,JSValueConst target,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,2,3,"Nav.Grid")) return JS_EXCEPTION;
    uint32_t w,d; if(JS_ToUint32(ctx,&w,argv[0])<0||JS_ToUint32(ctx,&d,argv[1])<0) return JS_EXCEPTION;
    float cell=1,ox=0,oz=0;
    if(argc==3&&!JS_IsUndefined(argv[2])) {
        if(!JS_IsObject(argv[2])) return JS_ThrowTypeError(ctx,"Nav.Grid options must be an object");
        if(!athena_js_option_float(ctx,argv[2],"cellSize",&cell)||!athena_js_option_float(ctx,argv[2],"x",&ox)||
            !athena_js_option_float(ctx,argv[2],"z",&oz)) return JS_EXCEPTION;
    }
    if(!w||!d||(uint64_t)w*d>ATHENA_NAV_MAX_CELLS||cell<=0) return JS_ThrowRangeError(ctx,"Nav.Grid: positive sizes, at most %u cells, cellSize > 0",ATHENA_NAV_MAX_CELLS);
    JSValue obj=new_object(ctx,target,grid_id); if(JS_IsException(obj)) return obj;
    AthenaNavGrid *g=athena_nav_grid_create(w,d,cell,ox,oz);
    if(!g) { JS_FreeValue(ctx,obj); return JS_ThrowOutOfMemory(ctx); }
    JS_SetOpaque(obj,g); return obj;
}
static JSValue grid_dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argc; (void)argv;
    if(!athena_js_class(ctx,self,grid_id)) return JS_EXCEPTION;
    AthenaNavGrid *g=JS_GetOpaque(self,grid_id); JS_SetOpaque(self,NULL); athena_nav_grid_release(g); return JS_UNDEFINED;
}
static JSValue grid_set_cost(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,3,3,"Grid.setCost")) return JS_EXCEPTION;
    int32_t c[2]; uint8_t v; if(!cell_args(ctx,argv,2,c)||!cost_arg(ctx,argv[2],&v)) return JS_EXCEPTION;
    AthenaNavGrid *g=get_grid(ctx,self); if(!g) return JS_EXCEPTION;
    if(athena_nav_set_cost(g,c[0],c[1],v)<0) return JS_ThrowRangeError(ctx,"Grid.setCost: cell outside the grid");
    return JS_DupValue(ctx,self);
}
static JSValue grid_get_cost(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,2,2,"Grid.getCost")) return JS_EXCEPTION;
    int32_t c[2]; if(!cell_args(ctx,argv,2,c)) return JS_EXCEPTION;
    AthenaNavGrid *g=get_grid(ctx,self); if(!g) return JS_EXCEPTION;
    return JS_NewInt32(ctx,athena_nav_get_cost(g,c[0],c[1]));
}
static JSValue grid_fill(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,5,5,"Grid.fill")) return JS_EXCEPTION;
    int32_t c[4]; uint8_t v; if(!cell_args(ctx,argv,4,c)||!cost_arg(ctx,argv[4],&v)) return JS_EXCEPTION;
    AthenaNavGrid *g=get_grid(ctx,self); if(!g) return JS_EXCEPTION;
    return JS_NewInt32(ctx,athena_nav_fill(g,c[0],c[1],c[2],c[3],v));
}
static JSValue grid_set_costs(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"Grid.setCosts")) return JS_EXCEPTION;
    AthenaJSArray a; if(!athena_js_array(ctx,argv[0],JS_TYPED_ARRAY_UINT8,&a,"costs")) return JS_EXCEPTION;
    AthenaNavGrid *g=get_grid(ctx,self);
    JSValue r=JS_DupValue(ctx,self);
    if(!g) { JS_FreeValue(ctx,r); r=JS_EXCEPTION; }
    else if(athena_nav_set_costs(g,a.data,(uint32_t)a.count)<0) { JS_FreeValue(ctx,r); r=JS_ThrowRangeError(ctx,"Grid.setCosts needs width * depth bytes"); }
    JS_FreeValue(ctx,a.backing); return r;
}
static JSValue grid_los(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,4,4,"Grid.lineOfSight")) return JS_EXCEPTION;
    float f[4]; if(!floats(ctx,argv,4,f,"coordinate")) return JS_EXCEPTION;
    AthenaNavGrid *g=get_grid(ctx,self); if(!g) return JS_EXCEPTION;
    return JS_NewBool(ctx,athena_nav_line_of_sight(g,f[0],f[1],f[2],f[3])==1);
}
static int query_option(JSContext *ctx,JSValueConst o,AthenaNavQuery *q) {
    *q=(AthenaNavQuery){1,1,0};
    if(JS_IsUndefined(o)) return 1;
    if(!JS_IsObject(o)) { JS_ThrowTypeError(ctx,"path options must be an object"); return 0; }
    static const char *const keys[2]={"diagonal","smooth"};
    for(int i=0;i<2;i++) {
        JSValue v=JS_GetPropertyStr(ctx,o,keys[i]); if(JS_IsException(v)) return 0;
        if(!JS_IsUndefined(v)) { if(i) q->smooth=JS_ToBool(ctx,v); else q->diagonal=JS_ToBool(ctx,v); }
        JS_FreeValue(ctx,v);
    }
    float it=0; if(!athena_js_option_float(ctx,o,"maxIterations",&it)) return 0;
    if(it<0||it>ATHENA_NAV_MAX_CELLS||it!=floorf(it)) { JS_ThrowRangeError(ctx,"maxIterations must be an integer from 0 to %u",ATHENA_NAV_MAX_CELLS); return 0; }
    q->max_iterations=(uint32_t)it; return 1;
}
/* findPath(x0, z0, x1, z1, options?): Float32Array of x, z pairs, or null. */
static JSValue grid_find_path(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,4,5,"Grid.findPath")) return JS_EXCEPTION;
    float f[4]; AthenaNavQuery q;
    if(!floats(ctx,argv,4,f,"coordinate")||!query_option(ctx,argc==5?argv[4]:JS_UNDEFINED,&q)) return JS_EXCEPTION;
    AthenaNavGrid *g=get_grid(ctx,self); if(!g) return JS_EXCEPTION;
    float pts[ATHENA_NAV_MAX_PATH*2];
    int n=athena_nav_find_path(g,f[0],f[1],f[2],f[3],&q,pts,ATHENA_NAV_MAX_PATH);
    if(n==-1) return JS_ThrowRangeError(ctx,"Grid.findPath: invalid coordinates");
    if(n==-2) return JS_ThrowRangeError(ctx,"Grid.findPath: the path has more than %u points (use smooth)",ATHENA_NAV_MAX_PATH);
    if(n==0) return JS_NULL;
    JSValue buffer=JS_NewArrayBufferCopy(ctx,(const uint8_t *)pts,(size_t)n*2*sizeof(float));
    if(JS_IsException(buffer)) return buffer;
    JSValue global=JS_GetGlobalObject(ctx),f32=JS_GetPropertyStr(ctx,global,"Float32Array"); JS_FreeValue(ctx,global);
    JSValue arr=JS_CallConstructor(ctx,f32,1,(JSValueConst *)&buffer);
    JS_FreeValue(ctx,f32); JS_FreeValue(ctx,buffer); return arr;
}
static JSValue grid_nearest(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,2,3,"Grid.nearestWalkable")) return JS_EXCEPTION;
    float f[2]; uint32_t radius=8;
    if(!floats(ctx,argv,2,f,"coordinate")||(argc==3&&JS_ToUint32(ctx,&radius,argv[2])<0)) return JS_EXCEPTION;
    AthenaNavGrid *g=get_grid(ctx,self); if(!g) return JS_EXCEPTION;
    float out[2]; int r=athena_nav_nearest_walkable(g,f[0],f[1],radius,out);
    if(r<0) return JS_ThrowRangeError(ctx,"Grid.nearestWalkable: invalid coordinates");
    if(!r) return JS_NULL;
    JSValue obj=JS_NewObject(ctx);
    JS_SetPropertyStr(ctx,obj,"x",JS_NewFloat64(ctx,out[0])); JS_SetPropertyStr(ctx,obj,"z",JS_NewFloat64(ctx,out[1]));
    return obj;
}
static JSValue grid_prop(JSContext *ctx,JSValueConst self,int magic) {
    AthenaNavGrid *g=get_grid(ctx,self); if(!g) return JS_EXCEPTION;
    uint32_t w,d; float cell,ox,oz; athena_nav_grid_size(g,&w,&d,&cell,&ox,&oz);
    switch(magic) { case 0: return JS_NewUint32(ctx,w); case 1: return JS_NewUint32(ctx,d); case 2: return JS_NewFloat64(ctx,cell);
        default: return JS_NewUint32(ctx,athena_nav_last_expanded(g)); }
}
/* --- Crowd and agents --- */
static JSValue crowd_ctor(JSContext *ctx,JSValueConst target,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,2,"Nav.Crowd")) return JS_EXCEPTION;
    AthenaNavGrid *g=get_grid(ctx,argv[0]); if(!g) return JS_EXCEPTION;
    AthenaNavQuery q; if(!query_option(ctx,argc==2?argv[1]:JS_UNDEFINED,&q)) return JS_EXCEPTION;
    g=get_grid(ctx,argv[0]); if(!g) return JS_EXCEPTION;
    JSValue obj=new_object(ctx,target,crowd_id); if(JS_IsException(obj)) return obj;
    /* A custom newTarget.prototype getter may dispose the grid. */
    g=get_grid(ctx,argv[0]);
    if(!g) { JS_FreeValue(ctx,obj); return JS_EXCEPTION; }
    AthenaNavCrowd *c=athena_nav_crowd_create(g);
    if(!c) { JS_FreeValue(ctx,obj); return JS_ThrowOutOfMemory(ctx); }
    athena_nav_crowd_set_query(c,&q);
    JS_SetOpaque(obj,c); return obj;
}
static JSValue crowd_dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argc; (void)argv;
    if(!athena_js_class(ctx,self,crowd_id)) return JS_EXCEPTION;
    AthenaNavCrowd *c=JS_GetOpaque(self,crowd_id); JS_SetOpaque(self,NULL); athena_nav_crowd_release(c); return JS_UNDEFINED;
}
/* add({ x, y, z, speed = 3, radius = 0.4, face = true, node }) */
static JSValue crowd_add(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"Crowd.add")||!JS_IsObject(argv[0])) return JS_ThrowTypeError(ctx,"Crowd.add expects { x, y, z, speed, radius, face, node }");
    float x=0,y=0,z=0; AthenaNavAgentDesc d={3,.4f,1};
    JSValueConst o=argv[0];
    if(!athena_js_option_float(ctx,o,"x",&x)||!athena_js_option_float(ctx,o,"y",&y)||!athena_js_option_float(ctx,o,"z",&z)||
        !athena_js_option_float(ctx,o,"speed",&d.speed)||!athena_js_option_float(ctx,o,"radius",&d.radius)) return JS_EXCEPTION;
    JSValue face=JS_GetPropertyStr(ctx,o,"face"); if(JS_IsException(face)) return face;
    if(!JS_IsUndefined(face)) d.face=JS_ToBool(ctx,face);
    JS_FreeValue(ctx,face);
    JSValue nodev=JS_GetPropertyStr(ctx,o,"node"); if(JS_IsException(nodev)) return nodev;
    AthenaNode3D *node=NULL;
    if(!JS_IsUndefined(nodev)&&!JS_IsNull(nodev)&&!(node=athena_node3d_from_value(ctx,nodev))) { JS_FreeValue(ctx,nodev); return JS_EXCEPTION; }
    AthenaNavCrowd *c=get_crowd(ctx,self); if(!c) { JS_FreeValue(ctx,nodev); return JS_EXCEPTION; }
    JSValue obj=JS_NewObjectClass(ctx,agent_id);
    if(JS_IsException(obj)) { JS_FreeValue(ctx,nodev); return obj; }
    int id=athena_nav_agent_add(c,x,y,z,&d);
    if(id<0) { JS_FreeValue(ctx,nodev); JS_FreeValue(ctx,obj); return JS_ThrowRangeError(ctx,"Crowd.add: invalid agent or more than %u",ATHENA_NAV_MAX_AGENTS); }
    if(node) athena_nav_agent_bind(c,id,node);
    JS_FreeValue(ctx,nodev);
    AgentHandle *h=malloc(sizeof(*h));
    if(!h) { athena_nav_agent_remove(c,id); JS_FreeValue(ctx,obj); return JS_ThrowOutOfMemory(ctx); }
    h->crowd=c; h->id=id; athena_nav_crowd_retain(c);
    JS_SetOpaque(obj,h); return obj;
}
static JSValue crowd_update(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"Crowd.update")) return JS_EXCEPTION;
    float dt; if(!athena_js_float(ctx,argv[0],&dt,"dt")) return JS_EXCEPTION;
    if(dt<0) return JS_ThrowRangeError(ctx,"dt must not be negative");
    AthenaNavCrowd *c=get_crowd(ctx,self); if(!c) return JS_EXCEPTION;
    return JS_NewInt32(ctx,athena_nav_crowd_update(c,dt));
}
static JSValue agent_move_to(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,2,2,"Agent.moveTo")) return JS_EXCEPTION;
    float f[2]; if(!floats(ctx,argv,2,f,"target")) return JS_EXCEPTION;
    AgentHandle *a=get_agent(ctx,self); if(!a) return JS_EXCEPTION;
    int r=athena_nav_agent_move_to(a->crowd,a->id,f[0],f[1]);
    if(r<0) return JS_ThrowRangeError(ctx,"Agent.moveTo: invalid target");
    return JS_NewBool(ctx,r==1);
}
static JSValue agent_stop(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argc; (void)argv;
    AgentHandle *a=get_agent(ctx,self); if(!a) return JS_EXCEPTION;
    athena_nav_agent_stop(a->crowd,a->id); return JS_DupValue(ctx,self);
}
static JSValue agent_set_position(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,3,3,"Agent.setPosition")) return JS_EXCEPTION;
    float f[3]; if(!floats(ctx,argv,3,f,"position")) return JS_EXCEPTION;
    AgentHandle *a=get_agent(ctx,self); if(!a) return JS_EXCEPTION;
    athena_nav_agent_set_position(a->crowd,a->id,f[0],f[1],f[2]); return JS_DupValue(ctx,self);
}
static JSValue agent_bind(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"Agent.bind")) return JS_EXCEPTION;
    AthenaNode3D *node=NULL;
    if(!JS_IsNull(argv[0])&&!(node=athena_node3d_from_value(ctx,argv[0]))) return JS_EXCEPTION;
    AgentHandle *a=get_agent(ctx,self); if(!a) return JS_EXCEPTION;
    athena_nav_agent_bind(a->crowd,a->id,node); return JS_DupValue(ctx,self);
}
static JSValue agent_dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argc; (void)argv;
    if(!athena_js_class(ctx,self,agent_id)) return JS_EXCEPTION;
    AgentHandle *a=JS_GetOpaque(self,agent_id); JS_SetOpaque(self,NULL); agent_free(a); return JS_UNDEFINED;
}
static const char *const STATES[3]={"idle","moving","arrived"};
static JSValue agent_prop(JSContext *ctx,JSValueConst self,int magic) {
    AgentHandle *a=get_agent(ctx,self); if(!a) return JS_EXCEPTION;
    AthenaNavAgentInfo i; athena_nav_agent_info(a->crowd,a->id,&i);
    switch(magic) {
        case 0: return JS_NewFloat64(ctx,i.x); case 1: return JS_NewFloat64(ctx,i.y); case 2: return JS_NewFloat64(ctx,i.z);
        case 3: return JS_NewString(ctx,STATES[i.state]); case 4: return JS_NewFloat64(ctx,i.yaw);
        case 5: return JS_NewFloat64(ctx,sqrtf(i.vx*i.vx+i.vz*i.vz)); case 6: return JS_NewUint32(ctx,i.waypoints);
        case 7: return JS_NewFloat64(ctx,i.speed);
        default: return JS_UNDEFINED;
    }
}
static JSValue agent_set_speed(JSContext *ctx,JSValueConst self,JSValueConst v) {
    float s; if(!athena_js_float(ctx,v,&s,"speed")) return JS_EXCEPTION;
    AgentHandle *a=get_agent(ctx,self); if(!a) return JS_EXCEPTION;
    if(athena_nav_agent_set_speed(a->crowd,a->id,s)<0) return JS_ThrowRangeError(ctx,"speed must not be negative");
    return JS_UNDEFINED;
}
static JSValue agent_get_speed(JSContext *ctx,JSValueConst self) { return agent_prop(ctx,self,7); }
static JSClassDef grid_class={"Nav.Grid",.finalizer=grid_finalizer},crowd_class={"Nav.Crowd",.finalizer=crowd_finalizer},
    agent_class={"Nav.Agent",.finalizer=agent_finalizer};
static const JSCFunctionListEntry grid_methods[]={
    JS_CFUNC_DEF("setCost",3,grid_set_cost),JS_CFUNC_DEF("getCost",2,grid_get_cost),JS_CFUNC_DEF("fill",5,grid_fill),
    JS_CFUNC_DEF("setCosts",1,grid_set_costs),JS_CFUNC_DEF("lineOfSight",4,grid_los),JS_CFUNC_DEF("findPath",4,grid_find_path),
    JS_CFUNC_DEF("nearestWalkable",2,grid_nearest),JS_CFUNC_DEF("dispose",0,grid_dispose),
    JS_CGETSET_MAGIC_DEF("width",grid_prop,NULL,0),JS_CGETSET_MAGIC_DEF("depth",grid_prop,NULL,1),
    JS_CGETSET_MAGIC_DEF("cellSize",grid_prop,NULL,2),JS_CGETSET_MAGIC_DEF("lastExpanded",grid_prop,NULL,3)};
static const JSCFunctionListEntry crowd_methods[]={
    JS_CFUNC_DEF("add",1,crowd_add),JS_CFUNC_DEF("update",1,crowd_update),JS_CFUNC_DEF("dispose",0,crowd_dispose)};
static const JSCFunctionListEntry agent_methods[]={
    JS_CFUNC_DEF("moveTo",2,agent_move_to),JS_CFUNC_DEF("stop",0,agent_stop),JS_CFUNC_DEF("setPosition",3,agent_set_position),
    JS_CFUNC_DEF("bind",1,agent_bind),JS_CFUNC_DEF("dispose",0,agent_dispose),
    JS_CGETSET_MAGIC_DEF("x",agent_prop,NULL,0),JS_CGETSET_MAGIC_DEF("y",agent_prop,NULL,1),JS_CGETSET_MAGIC_DEF("z",agent_prop,NULL,2),
    JS_CGETSET_MAGIC_DEF("state",agent_prop,NULL,3),JS_CGETSET_MAGIC_DEF("yaw",agent_prop,NULL,4),
    JS_CGETSET_MAGIC_DEF("velocity",agent_prop,NULL,5),JS_CGETSET_MAGIC_DEF("waypoints",agent_prop,NULL,6),
    JS_CGETSET_DEF("speed",agent_get_speed,agent_set_speed)};
static const JSCFunctionListEntry exports[]={
    JS_PROP_INT32_DEF("MAX_CELLS",ATHENA_NAV_MAX_CELLS,JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MAX_PATH",ATHENA_NAV_MAX_PATH,JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MAX_AGENTS",ATHENA_NAV_MAX_AGENTS,JS_PROP_ENUMERABLE)};
static int add_class(JSContext *ctx,JSModuleDef *m,JSClassID *id,JSClassDef *def,const JSCFunctionListEntry *list,int n,
    JSCFunction *ctor,const char *name) {
    if(athena_register_class(ctx,id,def)<0) return -1;
    JSValue proto=JS_NewObject(ctx); if(JS_IsException(proto)) return -1;
    JS_SetPropertyFunctionList(ctx,proto,list,n);
    JS_SetClassProto(ctx,*id,JS_DupValue(ctx,proto));
    if(!ctor) { JS_FreeValue(ctx,proto); return 0; }
    JSValue cls=JS_NewCFunction2(ctx,ctor,name,2,JS_CFUNC_constructor,0);
    JS_SetConstructor(ctx,cls,proto); JS_FreeValue(ctx,proto);
    return JS_SetModuleExport(ctx,m,name,cls);
}
static int init(JSContext *ctx,JSModuleDef *m) {
    if(add_class(ctx,m,&grid_id,&grid_class,grid_methods,countof(grid_methods),grid_ctor,"Grid")<0||
        add_class(ctx,m,&crowd_id,&crowd_class,crowd_methods,countof(crowd_methods),crowd_ctor,"Crowd")<0||
        add_class(ctx,m,&agent_id,&agent_class,agent_methods,countof(agent_methods),NULL,"Agent")<0) return -1;
    return JS_SetModuleExportList(ctx,m,exports,countof(exports));
}
JSModuleDef *athena_nav_js_init(JSContext *ctx) {
    JSModuleDef *m=athena_push_module(ctx,init,exports,countof(exports),"Nav");
    if(m) { JS_AddModuleExport(ctx,m,"Grid"); JS_AddModuleExport(ctx,m,"Crowd"); }
    return m;
}
