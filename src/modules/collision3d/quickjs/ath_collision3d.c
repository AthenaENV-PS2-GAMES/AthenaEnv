#include <athena_js_args.h>
#include <athena/collision3d.h>
#include <athena/js/model3d.h>
#include <athena/js/scene3d.h>
#include <athena/js/matrix4.h>
#include <athena/js/collision3d.h>
#include "ath_collision3d.h"
static JSClassID world_id,character_id;
/* Characters move by themselves in the Loop system: kept until dispose(). */
static AthenaJSKept *kept_characters;
static const char *const hit_names[]={"distance","x","y","z","nx","ny","nz","shape","triangle"};
static JSAtom hit_atom_storage[countof(hit_names)];
static AthenaJSAtoms hit_atoms={hit_names,countof(hit_names),hit_atom_storage,NULL};

static AthenaCollision3DWorld *get_world(JSContext *ctx,JSValueConst v) {
    AthenaCollision3DWorld *w=JS_GetOpaque2(ctx,v,world_id);
    if(!w) JS_ThrowTypeError(ctx,"Expected a live Collision3D.World");
    return w;
}
AthenaCollision3DWorld *athena_collision3d_world_from_value(JSContext *ctx,JSValueConst value) {
    return get_world(ctx,value);
}
static AthenaCharacter3D *get_character(JSContext *ctx,JSValueConst v) {
    AthenaCharacter3D *c=JS_GetOpaque2(ctx,v,character_id);
    if(!c) JS_ThrowTypeError(ctx,"Expected a live Collision3D.Character");
    return c;
}
static void world_finalizer(JSRuntime *rt,JSValue v) { (void)rt; athena_collision3d_world_release(JS_GetOpaque(v,world_id)); }
static void character_finalizer(JSRuntime *rt,JSValue v) { (void)rt; athena_character3d_release(JS_GetOpaque(v,character_id)); }
static JSValue throw_code(JSContext *ctx,int code,const char *name) {
    if(code==ATHENA_COLLISION3D_ENOMEM) return JS_ThrowOutOfMemory(ctx);
    if(code==ATHENA_COLLISION3D_EFULL)
        return JS_ThrowRangeError(ctx,"%s: more than %u triangles in the world",name,(unsigned)ATHENA_COLLISION3D_MAX_TRIANGLES);
    return JS_ThrowRangeError(ctx,"%s: invalid arguments",name);
}
/* [x, y, z] (an array or a typed array). */
static int vector3(JSContext *ctx,JSValueConst value,float out[3],const char *name) {
    if(!JS_IsObject(value)) { JS_ThrowTypeError(ctx,"%s must be [x, y, z]",name); return 0; }
    for(uint32_t i=0;i<3;i++) {
        JSValue e=JS_GetPropertyUint32(ctx,value,i);
        int ok=!JS_IsException(e)&&athena_js_float(ctx,e,&out[i],name);
        JS_FreeValue(ctx,e);
        if(!ok) return 0;
    }
    return 1;
}
/* Layers and masks: 32-bit flags, -1 for all. */
static int option_bits(JSContext *ctx,JSValueConst options,const char *key,uint32_t *out) {
    if(JS_IsUndefined(options)) return 1;
    if(!JS_IsObject(options)) { JS_ThrowTypeError(ctx,"options must be an object"); return 0; }
    JSValue v=JS_GetPropertyStr(ctx,options,key); if(JS_IsException(v)) return 0;
    int32_t bits; int ok=JS_IsUndefined(v)||JS_ToInt32(ctx,&bits,v)==0;
    if(ok&&!JS_IsUndefined(v)) *out=(uint32_t)bits;
    JS_FreeValue(ctx,v); return ok;
}
static JSValue world_ctor(JSContext *ctx,JSValueConst target,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Collision3D.World")) return JS_EXCEPTION;
    JSValue proto=JS_GetPropertyStr(ctx,target,"prototype"); if(JS_IsException(proto)) return proto;
    JSValue obj=JS_NewObjectProtoClass(ctx,proto,world_id); JS_FreeValue(ctx,proto);
    if(JS_IsException(obj)) return obj;
    AthenaCollision3DWorld *w=athena_collision3d_world_create();
    if(!w) { JS_FreeValue(ctx,obj); return JS_ThrowOutOfMemory(ctx); }
    JS_SetOpaque(obj,w); return obj;
}
static JSValue shape_result(JSContext *ctx,int id,const char *name) {
    return id<0?throw_code(ctx,id,name):JS_NewInt32(ctx,id);
}
/* addMesh(mesh, transform?, options?) */
static JSValue world_add_mesh(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,3,"World.addMesh")) return JS_EXCEPTION;
    AthenaMesh3D *mesh=athena_mesh3d_from_value(ctx,argv[0]); if(!mesh) return JS_EXCEPTION;
    AthenaMatrix4 *m=NULL;
    if(argc>1&&!JS_IsUndefined(argv[1])&&!JS_IsNull(argv[1])&&!(m=athena_matrix4_from_value(ctx,argv[1]))) return JS_EXCEPTION;
    uint32_t layer=1; if(!option_bits(ctx,argc>2?argv[2]:JS_UNDEFINED,"layer",&layer)) return JS_EXCEPTION;
    AthenaCollision3DWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    return shape_result(ctx,athena_collision3d_add_mesh(w,mesh,m,layer),"World.addMesh");
}
static JSValue world_add_node(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,2,"World.addNode")) return JS_EXCEPTION;
    AthenaNode3D *n=athena_node3d_from_value(ctx,argv[0]); if(!n) return JS_EXCEPTION;
    uint32_t layer=1; if(!option_bits(ctx,argc>1?argv[1]:JS_UNDEFINED,"layer",&layer)) return JS_EXCEPTION;
    AthenaCollision3DWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    return shape_result(ctx,athena_collision3d_add_node(w,n,layer),"World.addNode");
}
static JSValue world_add_box(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,2,3,"World.addBox")) return JS_EXCEPTION;
    float lo[3],hi[3];
    if(!vector3(ctx,argv[0],lo,"min")||!vector3(ctx,argv[1],hi,"max")) return JS_EXCEPTION;
    uint32_t layer=1; if(!option_bits(ctx,argc>2?argv[2]:JS_UNDEFINED,"layer",&layer)) return JS_EXCEPTION;
    AthenaCollision3DWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    return shape_result(ctx,athena_collision3d_add_box(w,lo,hi,layer),"World.addBox");
}
/* addTriangles(positions: Float32Array of 9 floats per triangle, options?) */
static JSValue world_add_triangles(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,2,"World.addTriangles")) return JS_EXCEPTION;
    uint32_t layer=1; if(!option_bits(ctx,argc>1?argv[1]:JS_UNDEFINED,"layer",&layer)) return JS_EXCEPTION;
    AthenaCollision3DWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    AthenaJSArray a; if(!athena_js_array(ctx,argv[0],JS_TYPED_ARRAY_FLOAT32,&a,"positions")) return JS_EXCEPTION;
    JSValue result=a.count%9?JS_ThrowRangeError(ctx,"positions needs 9 floats per triangle"):
        shape_result(ctx,athena_collision3d_add_triangles(w,a.data,(uint32_t)(a.count/9),NULL,layer),"World.addTriangles");
    JS_FreeValue(ctx,a.backing); return result;
}
static int shape_id(JSContext *ctx,JSValueConst v,int *id) {
    int32_t i; if(JS_ToInt32(ctx,&i,v)<0) return 0;
    *id=i; return 1;
}
static JSValue world_remove(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"World.remove")) return JS_EXCEPTION;
    int id; if(!shape_id(ctx,argv[0],&id)) return JS_EXCEPTION;
    AthenaCollision3DWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    return JS_NewBool(ctx,athena_collision3d_remove(w,id)==0);
}
static JSValue world_set_layer(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,2,2,"World.setLayer")) return JS_EXCEPTION;
    int id; int32_t layer;
    if(!shape_id(ctx,argv[0],&id)||JS_ToInt32(ctx,&layer,argv[1])<0) return JS_EXCEPTION;
    AthenaCollision3DWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    return JS_NewBool(ctx,athena_collision3d_set_layer(w,id,(uint32_t)layer)==0);
}
/* Fills or creates { distance, x, y, z, nx, ny, nz, shape, triangle }. */
static JSValue hit_value(JSContext *ctx,const AthenaCollision3DHit *h,JSValueConst out) {
    JSValue obj; int define;
    if(!athena_js_out_object(ctx,out,&obj,&define,"out")) return JS_EXCEPTION;
    const float v[7]={h->distance,h->point[0],h->point[1],h->point[2],h->normal[0],h->normal[1],h->normal[2]};
    for(unsigned i=0;i<7;i++)
        if(athena_js_put(ctx,&hit_atoms,obj,define,i,JS_NewFloat64(ctx,v[i]))<0) { JS_FreeValue(ctx,obj); return JS_EXCEPTION; }
    if(athena_js_put(ctx,&hit_atoms,obj,define,7,JS_NewInt32(ctx,h->shape))<0||
        athena_js_put(ctx,&hit_atoms,obj,define,8,JS_NewUint32(ctx,h->triangle))<0) { JS_FreeValue(ctx,obj); return JS_EXCEPTION; }
    return obj;
}
/* raycast(origin, direction, maxDistance, options?, out?) and
 * sphereCast(center, radius, direction, maxDistance, options?, out?). */
static JSValue world_cast(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int sphere) {
    const char *name=sphere?"World.sphereCast":"World.raycast";
    int base=sphere?1:0;
    if(!athena_js_argc(ctx,argc,3+base,5+base,name)) return JS_EXCEPTION;
    float origin[3],direction[3],radius=0,distance;
    if(!vector3(ctx,argv[0],origin,sphere?"center":"origin")) return JS_EXCEPTION;
    if(sphere&&!athena_js_float(ctx,argv[1],&radius,"radius")) return JS_EXCEPTION;
    if(!vector3(ctx,argv[1+base],direction,"direction")||!athena_js_float(ctx,argv[2+base],&distance,"maxDistance")) return JS_EXCEPTION;
    uint32_t mask=0xffffffffu;
    if(!option_bits(ctx,argc>3+base?argv[3+base]:JS_UNDEFINED,"mask",&mask)) return JS_EXCEPTION;
    AthenaCollision3DWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    AthenaCollision3DHit hit;
    int code=sphere?athena_collision3d_sphere_cast(w,origin,radius,direction,distance,mask,&hit):
        athena_collision3d_raycast(w,origin,direction,distance,mask,&hit);
    if(code<0) return throw_code(ctx,code,name);
    if(!code) return JS_NULL;
    return hit_value(ctx,&hit,argc>4+base?argv[4+base]:JS_UNDEFINED);
}
/* raycastMany(origins, directions, maxDistance, out, options?): n rays in one
 * call. origins: 3n floats; directions: 3 (shared) or 3n; out: 4n floats per
 * ray, [distance, nx, ny, nz], distance -1 for a miss. Returns the hits. */
static JSValue world_raycast_many(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,4,5,"World.raycastMany")) return JS_EXCEPTION;
    float distance; if(!athena_js_float(ctx,argv[2],&distance,"maxDistance")) return JS_EXCEPTION;
    uint32_t mask=0xffffffffu; if(!option_bits(ctx,argc>4?argv[4]:JS_UNDEFINED,"mask",&mask)) return JS_EXCEPTION;
    AthenaCollision3DWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    AthenaJSArray o,d,r;
    if(!athena_js_array(ctx,argv[0],JS_TYPED_ARRAY_FLOAT32,&o,"origins")) return JS_EXCEPTION;
    if(!athena_js_array(ctx,argv[1],JS_TYPED_ARRAY_FLOAT32,&d,"directions")) { JS_FreeValue(ctx,o.backing); return JS_EXCEPTION; }
    if(!athena_js_array(ctx,argv[3],JS_TYPED_ARRAY_FLOAT32,&r,"out")) {
        JS_FreeValue(ctx,o.backing); JS_FreeValue(ctx,d.backing); return JS_EXCEPTION;
    }
    size_t rays=o.count/3; JSValue result;
    if(o.count%3||(d.count!=3&&d.count!=o.count)||r.count<rays*4)
        result=JS_ThrowRangeError(ctx,"raycastMany: origins 3n floats, directions 3 or 3n, out 4n");
    else {
        const float *origins=o.data,*directions=d.data; float *out=r.data; uint32_t hits=0; int code=0;
        for(size_t i=0;i<rays&&code>=0;i++) {
            AthenaCollision3DHit hit;
            code=athena_collision3d_raycast(w,&origins[i*3],d.count==3?directions:&directions[i*3],distance,mask,&hit);
            float *slot=&out[i*4];
            if(code==1) { slot[0]=hit.distance; slot[1]=hit.normal[0]; slot[2]=hit.normal[1]; slot[3]=hit.normal[2]; hits++; }
            else { slot[0]=-1; slot[1]=slot[2]=slot[3]=0; }
        }
        result=code<0?throw_code(ctx,code,"World.raycastMany"):JS_NewUint32(ctx,hits);
    }
    JS_FreeValue(ctx,o.backing); JS_FreeValue(ctx,d.backing); JS_FreeValue(ctx,r.backing);
    return result;
}
static JSValue world_overlap(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,2,3,"World.overlapSphere")) return JS_EXCEPTION;
    float center[3],radius;
    if(!vector3(ctx,argv[0],center,"center")||!athena_js_float(ctx,argv[1],&radius,"radius")) return JS_EXCEPTION;
    uint32_t mask=0xffffffffu; if(!option_bits(ctx,argc>2?argv[2]:JS_UNDEFINED,"mask",&mask)) return JS_EXCEPTION;
    AthenaCollision3DWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    int ids[64];
    int count=athena_collision3d_overlap_sphere(w,center,radius,mask,ids,countof(ids));
    if(count<0) return throw_code(ctx,count,"World.overlapSphere");
    JSValue a=JS_NewArray(ctx); if(JS_IsException(a)) return a;
    for(int i=0;i<count&&i<(int)countof(ids);i++)
        if(JS_SetPropertyUint32(ctx,a,(uint32_t)i,JS_NewInt32(ctx,ids[i]))<0) { JS_FreeValue(ctx,a); return JS_EXCEPTION; }
    return a;
}
static JSValue world_triangles(JSContext *ctx,JSValueConst self) {
    AthenaCollision3DWorld *w=get_world(ctx,self); return w?JS_NewUint32(ctx,athena_collision3d_triangle_count(w)):JS_EXCEPTION;
}
static JSValue world_dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"World.dispose")) return JS_EXCEPTION;
    if(!athena_js_class(ctx,self,world_id)) return JS_EXCEPTION;
    AthenaCollision3DWorld *w=JS_GetOpaque(self,world_id);
    JS_SetOpaque(self,NULL); athena_collision3d_world_release(w); /* characters keep their world */
    return JS_UNDEFINED;
}

/* new Character(world, { radius, height, stepHeight, maxSlope, gravity, mask, position }) */
static JSValue character_ctor(JSContext *ctx,JSValueConst target,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,2,"Collision3D.Character")) return JS_EXCEPTION;
    AthenaCollision3DWorld *w=get_world(ctx,argv[0]); if(!w) return JS_EXCEPTION;
    AthenaCharacter3DDesc d; athena_character3d_desc_default(&d);
    float position[3]={0,0,0};
    JSValueConst o=argc>1?argv[1]:JS_UNDEFINED;
    if(!JS_IsUndefined(o)) {
        if(!JS_IsObject(o)) return JS_ThrowTypeError(ctx,"Character options must be an object");
        if(!athena_js_option_float(ctx,o,"radius",&d.radius)||!athena_js_option_float(ctx,o,"height",&d.height)||
            !athena_js_option_float(ctx,o,"stepHeight",&d.step_height)||!athena_js_option_float(ctx,o,"maxSlope",&d.max_slope)||
            !option_bits(ctx,o,"mask",&d.mask)) return JS_EXCEPTION;
        static const char *const vectors[]={"gravity","position"};
        float *targets[]={d.gravity,position};
        for(int i=0;i<2;i++) {
            JSValue v=JS_GetPropertyStr(ctx,o,vectors[i]); if(JS_IsException(v)) return v;
            int ok=JS_IsUndefined(v)||vector3(ctx,v,targets[i],vectors[i]);
            JS_FreeValue(ctx,v);
            if(!ok) return JS_EXCEPTION;
        }
    }
    JSValue proto=JS_GetPropertyStr(ctx,target,"prototype"); if(JS_IsException(proto)) return proto;
    JSValue obj=JS_NewObjectProtoClass(ctx,proto,character_id); JS_FreeValue(ctx,proto);
    if(JS_IsException(obj)) return obj;
    AthenaCharacter3D *c=athena_character3d_create(w,&d);
    if(!c) {
        JS_FreeValue(ctx,obj);
        return JS_ThrowRangeError(ctx,"Collision3D.Character: radius and height > 0, 0 <= stepHeight < height, 0 <= maxSlope < 90");
    }
    if(athena_character3d_set_position(c,position[0],position[1],position[2])<0) {
        athena_character3d_release(c); JS_FreeValue(ctx,obj); return JS_ThrowRangeError(ctx,"Invalid position");
    }
    JS_SetOpaque(obj,c);
    if(!athena_js_keep(ctx,&kept_characters,obj)) { JS_FreeValue(ctx,obj); return JS_EXCEPTION; }
    return obj;
}
static int floats(JSContext *ctx,int argc,JSValueConst *argv,int count,float *v,const char *name) {
    if(!athena_js_argc(ctx,argc,count,count,name)) return 0;
    for(int i=0;i<count;i++) if(!athena_js_float(ctx,argv[i],&v[i],name)) return 0;
    return 1;
}
/* 0 setPosition, 1 setVelocity, 2 move (x, y, z); 3 step (dt). */
static JSValue character_call(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic) {
    static const char *const names[]={"setPosition","setVelocity","move","step"};
    float v[3];
    if(!floats(ctx,argc,argv,magic==3?1:3,v,names[magic])) return JS_EXCEPTION;
    AthenaCharacter3D *c=get_character(ctx,self); if(!c) return JS_EXCEPTION;
    int code=magic==0?athena_character3d_set_position(c,v[0],v[1],v[2]):
        magic==1?athena_character3d_set_velocity(c,v[0],v[1],v[2]):
        magic==2?athena_character3d_move(c,v[0],v[1],v[2]):athena_character3d_step(c,v[0]);
    if(code<0) return throw_code(ctx,code,names[magic]);
    return JS_DupValue(ctx,self);
}
/* x, y, z (feet; read-only, use setPosition) and vx, vy, vz. */
static JSValue character_get(JSContext *ctx,JSValueConst self,int magic) {
    AthenaCharacter3D *c=get_character(ctx,self); if(!c) return JS_EXCEPTION;
    float v[3];
    if(magic<3) athena_character3d_get_position(c,v); else athena_character3d_get_velocity(c,v);
    return JS_NewFloat64(ctx,v[magic%3]);
}
static JSValue character_set_velocity(JSContext *ctx,JSValueConst self,JSValueConst value,int magic) {
    float f; if(!athena_js_float(ctx,value,&f,"velocity")) return JS_EXCEPTION;
    AthenaCharacter3D *c=get_character(ctx,self); if(!c) return JS_EXCEPTION;
    float v[3]; athena_character3d_get_velocity(c,v); v[magic%3]=f;
    athena_character3d_set_velocity(c,v[0],v[1],v[2]); return JS_UNDEFINED;
}
/* 0 onGround, 1 hitWall, 2 hitCeiling. */
static JSValue character_flag(JSContext *ctx,JSValueConst self,int magic) {
    AthenaCharacter3D *c=get_character(ctx,self); if(!c) return JS_EXCEPTION;
    AthenaCharacter3DState s; athena_character3d_state(c,&s);
    return JS_NewBool(ctx,magic==0?s.on_ground:magic==1?s.hit_wall:s.hit_ceiling);
}
static JSValue character_ground_normal(JSContext *ctx,JSValueConst self) {
    AthenaCharacter3D *c=get_character(ctx,self); if(!c) return JS_EXCEPTION;
    AthenaCharacter3DState s; athena_character3d_state(c,&s);
    JSValue a=JS_NewArray(ctx); if(JS_IsException(a)) return a;
    for(uint32_t i=0;i<3;i++)
        if(JS_SetPropertyUint32(ctx,a,i,JS_NewFloat64(ctx,s.ground_normal[i]))<0) { JS_FreeValue(ctx,a); return JS_EXCEPTION; }
    return a;
}
static JSValue character_bind(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"Character.bind")) return JS_EXCEPTION;
    AthenaNode3D *n=NULL;
    if(!JS_IsNull(argv[0])&&!JS_IsUndefined(argv[0])&&!(n=athena_node3d_from_value(ctx,argv[0]))) return JS_EXCEPTION;
    AthenaCharacter3D *c=get_character(ctx,self); if(!c) return JS_EXCEPTION;
    athena_character3d_bind(c,n); return JS_DupValue(ctx,self);
}
static JSValue character_enabled(JSContext *ctx,JSValueConst self) {
    AthenaCharacter3D *c=get_character(ctx,self); return c?JS_NewBool(ctx,athena_character3d_enabled(c)):JS_EXCEPTION;
}
static JSValue character_set_enabled(JSContext *ctx,JSValueConst self,JSValueConst value) {
    if(!JS_IsBool(value)) return JS_ThrowTypeError(ctx,"enabled must be a boolean");
    AthenaCharacter3D *c=get_character(ctx,self); if(!c) return JS_EXCEPTION;
    athena_character3d_set_enabled(c,JS_ToBool(ctx,value)); return JS_UNDEFINED;
}
static JSValue character_dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Character.dispose")) return JS_EXCEPTION;
    if(!athena_js_class(ctx,self,character_id)) return JS_EXCEPTION;
    AthenaCharacter3D *c=JS_GetOpaque(self,character_id);
    if(c) athena_js_unkeep(ctx,&kept_characters,c,character_id);
    JS_SetOpaque(self,NULL); athena_character3d_release(c);
    return JS_UNDEFINED;
}
static JSValue step_all(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; float dt; if(!floats(ctx,argc,argv,1,&dt,"Collision3D.step")) return JS_EXCEPTION;
    int code=athena_character3d_step_all(dt);
    return code<0?throw_code(ctx,code,"Collision3D.step"):JS_UNDEFINED;
}
static JSValue attach_loop(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,0,1,"Collision3D.attachLoop")) return JS_EXCEPTION;
    float priority=ATHENA_CHARACTER3D_LOOP_PRIORITY;
    if(argc==1&&!JS_IsUndefined(argv[0])&&!athena_js_float(ctx,argv[0],&priority,"priority")) return JS_EXCEPTION;
    if(priority!=(float)(int)priority) return JS_ThrowRangeError(ctx,"priority must be an integer");
    if(athena_character3d_loop_system()) return JS_UNDEFINED;
    int id=athena_character3d_attach_loop((int)priority,ctx);
    if(id==ATHENA_COLLISION3D_ENOMEM) return JS_ThrowOutOfMemory(ctx);
    return id<0?JS_ThrowInternalError(ctx,"Collision3D could not join the Loop"):JS_UNDEFINED;
}
static JSValue detach_loop(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Collision3D.detachLoop")) return JS_EXCEPTION;
    return JS_NewBool(ctx,athena_character3d_detach_loop());
}
static JSValue attached(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Collision3D.isAttached")) return JS_EXCEPTION;
    return JS_NewBool(ctx,athena_character3d_loop_system()!=0);
}
static JSClassDef world_class={"Collision3D.World",.finalizer=world_finalizer};
static JSClassDef character_class={"Collision3D.Character",.finalizer=character_finalizer};
static const JSCFunctionListEntry world_methods[]={
    JS_CFUNC_DEF("addMesh",1,world_add_mesh),JS_CFUNC_DEF("addNode",1,world_add_node),
    JS_CFUNC_DEF("addBox",2,world_add_box),JS_CFUNC_DEF("addTriangles",1,world_add_triangles),
    JS_CFUNC_DEF("remove",1,world_remove),JS_CFUNC_DEF("setLayer",2,world_set_layer),
    JS_CFUNC_MAGIC_DEF("raycast",3,world_cast,0),JS_CFUNC_MAGIC_DEF("sphereCast",4,world_cast,1),
    JS_CFUNC_DEF("raycastMany",4,world_raycast_many),
    JS_CFUNC_DEF("overlapSphere",2,world_overlap),JS_CGETSET_DEF("triangleCount",world_triangles,NULL),
    JS_CFUNC_DEF("dispose",0,world_dispose)};
static const JSCFunctionListEntry character_methods[]={
    JS_CFUNC_MAGIC_DEF("setPosition",3,character_call,0),JS_CFUNC_MAGIC_DEF("setVelocity",3,character_call,1),
    JS_CFUNC_MAGIC_DEF("move",3,character_call,2),JS_CFUNC_MAGIC_DEF("step",1,character_call,3),
    JS_CGETSET_MAGIC_DEF("x",character_get,NULL,0),JS_CGETSET_MAGIC_DEF("y",character_get,NULL,1),
    JS_CGETSET_MAGIC_DEF("z",character_get,NULL,2),
    JS_CGETSET_MAGIC_DEF("vx",character_get,character_set_velocity,3),
    JS_CGETSET_MAGIC_DEF("vy",character_get,character_set_velocity,4),
    JS_CGETSET_MAGIC_DEF("vz",character_get,character_set_velocity,5),
    JS_CGETSET_MAGIC_DEF("onGround",character_flag,NULL,0),JS_CGETSET_MAGIC_DEF("hitWall",character_flag,NULL,1),
    JS_CGETSET_MAGIC_DEF("hitCeiling",character_flag,NULL,2),JS_CGETSET_DEF("groundNormal",character_ground_normal,NULL),
    JS_CFUNC_DEF("bind",1,character_bind),JS_CGETSET_DEF("enabled",character_enabled,character_set_enabled),
    JS_CFUNC_DEF("dispose",0,character_dispose)};
static const JSCFunctionListEntry exports[]={
    JS_CFUNC_DEF("step",1,step_all),JS_CFUNC_DEF("attachLoop",0,attach_loop),
    JS_CFUNC_DEF("detachLoop",0,detach_loop),JS_CFUNC_DEF("isAttached",0,attached),
    JS_PROP_INT32_DEF("LOOP_PRIORITY",ATHENA_CHARACTER3D_LOOP_PRIORITY,JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MAX_TRIANGLES",ATHENA_COLLISION3D_MAX_TRIANGLES,JS_PROP_ENUMERABLE)};
static int define_class(JSContext *ctx,JSModuleDef *m,JSClassID id,const char *name,JSCFunction *ctor,int length,
    const JSCFunctionListEntry *methods,int count) {
    JSValue proto=JS_NewObject(ctx); if(JS_IsException(proto)) return -1;
    JS_SetPropertyFunctionList(ctx,proto,methods,count);
    JSValue cls=JS_NewCFunction2(ctx,ctor,name,length,JS_CFUNC_constructor,0);
    if(JS_IsException(cls)) { JS_FreeValue(ctx,proto); return -1; }
    JS_SetConstructor(ctx,cls,proto); JS_SetClassProto(ctx,id,proto);
    return JS_SetModuleExport(ctx,m,name,cls);
}
static int init(JSContext *ctx,JSModuleDef *m) {
    if(athena_register_class(ctx,&world_id,&world_class)<0||athena_register_class(ctx,&character_id,&character_class)<0) return -1;
    if(define_class(ctx,m,world_id,"World",world_ctor,0,world_methods,countof(world_methods))<0||
        define_class(ctx,m,character_id,"Character",character_ctor,1,character_methods,countof(character_methods))<0) return -1;
    return JS_SetModuleExportList(ctx,m,exports,countof(exports));
}
void athena_collision3d_js_cleanup(JSContext *ctx) {
    athena_character3d_detach_owner(ctx); athena_js_keep_free(ctx,&kept_characters);
    athena_js_atoms_free(ctx,&hit_atoms);
}
JSModuleDef *athena_collision3d_js_init(JSContext *ctx) {
    JSModuleDef *m=athena_push_module(ctx,init,exports,countof(exports),"Collision3D");
    if(m) { JS_AddModuleExport(ctx,m,"World"); JS_AddModuleExport(ctx,m,"Character"); }
    return m;
}
