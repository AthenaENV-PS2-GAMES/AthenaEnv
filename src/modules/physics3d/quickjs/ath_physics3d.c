#include <athena_js_args.h>
#include <athena/physics3d.h>
#include <athena/js/collision3d.h>
#include <athena/js/scene3d.h>
#include "ath_physics3d.h"
static JSClassID world_id,body_id,joint_id;

static AthenaPhysics3DWorld *get_world(JSContext *ctx,JSValueConst v) {
    AthenaPhysics3DWorld *w=JS_GetOpaque2(ctx,v,world_id);
    if(!w) JS_ThrowTypeError(ctx,"Expected a live Physics3D.World");
    return w;
}
static AthenaBody3D *get_body(JSContext *ctx,JSValueConst v) {
    AthenaBody3D *b=JS_GetOpaque2(ctx,v,body_id);
    if(!b) JS_ThrowTypeError(ctx,"Expected a live Physics3D.Body");
    return b;
}
static AthenaJoint3D *get_joint(JSContext *ctx,JSValueConst v) {
    AthenaJoint3D *j=JS_GetOpaque2(ctx,v,joint_id);
    if(!j) JS_ThrowTypeError(ctx,"Expected a live Physics3D.Joint");
    return j;
}
static void joint_finalizer(JSRuntime *rt,JSValue v) { (void)rt; athena_joint3d_release(JS_GetOpaque(v,joint_id)); }
static void world_finalizer(JSRuntime *rt,JSValue v) { (void)rt; athena_physics3d_world_release(JS_GetOpaque(v,world_id)); }
static void body_finalizer(JSRuntime *rt,JSValue v) { (void)rt; athena_body3d_release(JS_GetOpaque(v,body_id)); }
static JSValue throw_code(JSContext *ctx,int code,const char *name) {
    if(code==ATHENA_PHYSICS3D_ENOMEM) return JS_ThrowOutOfMemory(ctx);
    return JS_ThrowRangeError(ctx,"%s: invalid arguments",name);
}
/* [x, y, z] or [x, y, z, w] from an array or typed array. */
static int vector(JSContext *ctx,JSValueConst value,float *out,uint32_t count,const char *name) {
    if(!JS_IsObject(value)) { JS_ThrowTypeError(ctx,"%s must be an array of %u numbers",name,(unsigned)count); return 0; }
    for(uint32_t i=0;i<count;i++) {
        JSValue e=JS_GetPropertyUint32(ctx,value,i);
        int ok=!JS_IsException(e)&&athena_js_float(ctx,e,&out[i],name);
        JS_FreeValue(ctx,e);
        if(!ok) return 0;
    }
    return 1;
}
static int option_vector(JSContext *ctx,JSValueConst o,const char *key,float *out,uint32_t count) {
    JSValue v=JS_GetPropertyStr(ctx,o,key); if(JS_IsException(v)) return 0;
    int ok=JS_IsUndefined(v)||vector(ctx,v,out,count,key);
    JS_FreeValue(ctx,v); return ok;
}
static int option_bits(JSContext *ctx,JSValueConst o,const char *key,uint32_t *out) {
    JSValue v=JS_GetPropertyStr(ctx,o,key); if(JS_IsException(v)) return 0;
    int32_t bits; int ok=JS_IsUndefined(v)||JS_ToInt32(ctx,&bits,v)==0;
    if(ok&&!JS_IsUndefined(v)) *out=(uint32_t)bits;
    JS_FreeValue(ctx,v); return ok;
}
/* new World(statics?: Collision3D.World | null, { gravity, iterations, staticMask }) */
static JSValue world_ctor(JSContext *ctx,JSValueConst target,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,0,2,"Physics3D.World")) return JS_EXCEPTION;
    AthenaCollision3DWorld *statics=NULL;
    if(argc>0&&!JS_IsUndefined(argv[0])&&!JS_IsNull(argv[0])&&!(statics=athena_collision3d_world_from_value(ctx,argv[0])))
        return JS_EXCEPTION;
    float gravity[3]={0,-9.81f,0},iterations=8; uint32_t mask=0xffffffffu;
    JSValueConst o=argc>1?argv[1]:JS_UNDEFINED;
    if(!JS_IsUndefined(o)) {
        if(!JS_IsObject(o)) return JS_ThrowTypeError(ctx,"World options must be an object");
        if(!option_vector(ctx,o,"gravity",gravity,3)||!athena_js_option_float(ctx,o,"iterations",&iterations)||
            !option_bits(ctx,o,"staticMask",&mask)) return JS_EXCEPTION;
    }
    if(iterations!=(float)(int)iterations||iterations<1||iterations>64) return JS_ThrowRangeError(ctx,"iterations must be an integer in 1..64");
    JSValue proto=JS_GetPropertyStr(ctx,target,"prototype"); if(JS_IsException(proto)) return proto;
    JSValue obj=JS_NewObjectProtoClass(ctx,proto,world_id); JS_FreeValue(ctx,proto);
    if(JS_IsException(obj)) return obj;
    AthenaPhysics3DWorld *w=athena_physics3d_world_create(statics);
    if(!w) { JS_FreeValue(ctx,obj); return JS_ThrowOutOfMemory(ctx); }
    athena_physics3d_set_gravity(w,gravity[0],gravity[1],gravity[2]);
    athena_physics3d_set_iterations(w,(int)iterations); athena_physics3d_set_static_mask(w,mask);
    JS_SetOpaque(obj,w); return obj;
}
static JSValue wrap_body(JSContext *ctx,AthenaBody3D *b) {
    JSValue obj=JS_NewObjectClass(ctx,body_id);
    if(JS_IsException(obj)) { athena_body3d_remove(b); athena_body3d_release(b); return obj; }
    JS_SetOpaque(obj,b); return obj; /* the handle owns the caller's reference */
}
/* addSphere/addBox/addCapsule(options): magic is the shape. */
static JSValue world_add(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int shape) {
    static const char *const names[]={"World.addSphere","World.addBox","World.addCapsule"};
    const char *name=names[shape];
    if(!athena_js_argc(ctx,argc,0,1,name)) return JS_EXCEPTION;
    AthenaBody3DDesc d; athena_body3d_desc_default(&d);
    d.shape=(AthenaShape3DType)shape; d.half_height=.5f;
    JSValueConst o=argc?argv[0]:JS_UNDEFINED;
    if(!JS_IsUndefined(o)) {
        if(!JS_IsObject(o)) return JS_ThrowTypeError(ctx,"%s options must be an object",name);
        JSValue type=JS_GetPropertyStr(ctx,o,"type"); if(JS_IsException(type)) return type;
        if(!JS_IsUndefined(type)) {
            const char *s=JS_ToCString(ctx,type); JS_FreeValue(ctx,type);
            if(!s) return JS_EXCEPTION;
            int t=!strcmp(s,"dynamic")?0:!strcmp(s,"kinematic")?1:!strcmp(s,"static")?2:-1;
            JS_FreeCString(ctx,s);
            if(t<0) return JS_ThrowRangeError(ctx,"type must be \"dynamic\", \"kinematic\" or \"static\"");
            d.type=(AthenaBody3DType)t;
        }
        if(!athena_js_option_float(ctx,o,"radius",&d.radius)||!option_vector(ctx,o,"halfExtents",d.half,3)||
            !athena_js_option_float(ctx,o,"halfHeight",&d.half_height)||
            !athena_js_option_float(ctx,o,"mass",&d.mass)||!option_vector(ctx,o,"position",d.position,3)||
            !option_vector(ctx,o,"rotation",d.rotation,4)||!option_vector(ctx,o,"velocity",d.velocity,3)||
            !option_vector(ctx,o,"angularVelocity",d.angular_velocity,3)||
            !athena_js_option_float(ctx,o,"friction",&d.friction)||!athena_js_option_float(ctx,o,"restitution",&d.restitution)||
            !athena_js_option_float(ctx,o,"linearDamping",&d.linear_damping)||
            !athena_js_option_float(ctx,o,"angularDamping",&d.angular_damping)||
            !athena_js_option_float(ctx,o,"rollingFriction",&d.rolling_friction)||
            !option_bits(ctx,o,"layer",&d.layer)||!option_bits(ctx,o,"mask",&d.mask)) return JS_EXCEPTION;
    }
    AthenaPhysics3DWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    if(athena_physics3d_body_count(w)>=ATHENA_PHYSICS3D_MAX_BODIES)
        return JS_ThrowRangeError(ctx,"%s: at most %u bodies per world",name,(unsigned)ATHENA_PHYSICS3D_MAX_BODIES);
    AthenaBody3D *b=athena_body3d_create(w,&d);
    if(!b) return JS_ThrowRangeError(ctx,"%s: invalid body (sizes and dynamic mass > 0, friction and rollingFriction >= 0, restitution in [0, 1], damping >= 0, nonzero rotation)",name);
    return wrap_body(ctx,b);
}
static int floats(JSContext *ctx,int argc,JSValueConst *argv,int min,int max,float *v,const char *name) {
    if(!athena_js_argc(ctx,argc,min,max,name)) return 0;
    for(int i=0;i<argc;i++) if(!athena_js_float(ctx,argv[i],&v[i],name)) return 0;
    return 1;
}
static JSValue world_step(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    float dt; if(!floats(ctx,argc,argv,1,1,&dt,"World.step")) return JS_EXCEPTION;
    AthenaPhysics3DWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    int code=athena_physics3d_step(w,dt);
    return code<0?throw_code(ctx,code,"World.step"):JS_NewInt32(ctx,code);
}
static JSValue world_set_gravity(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    float g[3]; if(!floats(ctx,argc,argv,3,3,g,"World.setGravity")) return JS_EXCEPTION;
    AthenaPhysics3DWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    athena_physics3d_set_gravity(w,g[0],g[1],g[2]); return JS_DupValue(ctx,self);
}
static JSValue world_counts(JSContext *ctx,JSValueConst self,int magic) {
    AthenaPhysics3DWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    return JS_NewUint32(ctx,magic==2?athena_physics3d_joint_count(w):magic?athena_physics3d_contact_count(w):athena_physics3d_body_count(w));
}
static JSValue world_attach(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,0,1,"World.attachLoop")) return JS_EXCEPTION;
    float priority=ATHENA_PHYSICS3D_LOOP_PRIORITY;
    if(argc==1&&!JS_IsUndefined(argv[0])&&!athena_js_float(ctx,argv[0],&priority,"priority")) return JS_EXCEPTION;
    if(priority!=(float)(int)priority) return JS_ThrowRangeError(ctx,"priority must be an integer");
    AthenaPhysics3DWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    if(athena_physics3d_loop_system(w)) return JS_DupValue(ctx,self);
    int id=athena_physics3d_attach_loop(w,(int)priority,ctx);
    if(id==ATHENA_PHYSICS3D_ENOMEM) return JS_ThrowOutOfMemory(ctx);
    return id<0?JS_ThrowInternalError(ctx,"Physics3D could not join the Loop"):JS_DupValue(ctx,self);
}
static JSValue world_detach(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"World.detachLoop")) return JS_EXCEPTION;
    AthenaPhysics3DWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    return JS_NewBool(ctx,athena_physics3d_detach_loop(w));
}
/* world.profile: { collide, prepare, solve, integrate, substeps } of the last step. */
static JSValue world_profile(JSContext *ctx,JSValueConst self) {
    AthenaPhysics3DWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    AthenaPhysics3DProfile p; athena_physics3d_profile(w,&p);
    JSValue o=JS_NewObject(ctx); if(JS_IsException(o)) return o;
    const char *const names[]={"collide","prepare","solve","integrate"}; const float values[]={p.collide,p.prepare,p.solve,p.integrate};
    for(int i=0;i<4;i++) if(JS_SetPropertyStr(ctx,o,names[i],JS_NewFloat64(ctx,values[i]))<0) { JS_FreeValue(ctx,o); return JS_EXCEPTION; }
    if(JS_SetPropertyStr(ctx,o,"substeps",JS_NewUint32(ctx,p.substeps))<0) { JS_FreeValue(ctx,o); return JS_EXCEPTION; }
    return o;
}
static JSValue world_attached(JSContext *ctx,JSValueConst self) {
    AthenaPhysics3DWorld *w=get_world(ctx,self); return w?JS_NewBool(ctx,athena_physics3d_loop_system(w)!=0):JS_EXCEPTION;
}
static JSValue world_dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"World.dispose")) return JS_EXCEPTION;
    if(!athena_js_class(ctx,self,world_id)) return JS_EXCEPTION;
    AthenaPhysics3DWorld *w=JS_GetOpaque(self,world_id);
    /* Detached: a disposed world stops stepping. Its bodies go with it. */
    if(w) athena_physics3d_detach_loop(w);
    JS_SetOpaque(self,NULL); athena_physics3d_world_release(w);
    return JS_UNDEFINED;
}

/* null or undefined: the world. */
static int optional_body(JSContext *ctx,JSValueConst v,AthenaBody3D **out) {
    *out=NULL;
    if(JS_IsNull(v)||JS_IsUndefined(v)) return 1;
    return (*out=get_body(ctx,v))!=NULL;
}
/* addBallJoint(a, b, anchor), addHingeJoint(a, b, anchor, axis, options?),
 * addDistanceJoint(a, b, anchorA, anchorB, options?), addWeldJoint(a, b, anchor). */
static JSValue world_add_joint(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic) {
    static const char *const names[]={"World.addBallJoint","World.addHingeJoint","World.addDistanceJoint","World.addWeldJoint"};
    static const int minimum[]={3,4,4,3},maximum[]={3,5,5,3};
    if(!athena_js_argc(ctx,argc,minimum[magic],maximum[magic],names[magic])) return JS_EXCEPTION;
    AthenaBody3D *a=get_body(ctx,argv[0]),*b; if(!a) return JS_EXCEPTION;
    if(!optional_body(ctx,argv[1],&b)) return JS_EXCEPTION;
    float p[3],q[3];
    if(!vector(ctx,argv[2],p,3,magic==2?"anchorA":"anchor")) return JS_EXCEPTION;
    if((magic==1||magic==2)&&!vector(ctx,argv[3],q,3,magic==1?"axis":"anchorB")) return JS_EXCEPTION;
    JSValueConst o=(magic==1||magic==2)&&argc>4?argv[4]:JS_UNDEFINED;
    if(!JS_IsUndefined(o)&&!JS_IsObject(o)) return JS_ThrowTypeError(ctx,"%s options must be an object",names[magic]);
    float length=-1,lower=0,upper=0,speed=0,torque=0; int rope=0,limits=0;
    if(!JS_IsUndefined(o)) {
        if(magic==2) {
            if(!athena_js_option_float(ctx,o,"length",&length)) return JS_EXCEPTION;
            JSValue r=JS_GetPropertyStr(ctx,o,"rope"); if(JS_IsException(r)) return r;
            rope=JS_ToBool(ctx,r); JS_FreeValue(ctx,r);
        } else {
            JSValue l=JS_GetPropertyStr(ctx,o,"lower"); if(JS_IsException(l)) return l;
            limits=!JS_IsUndefined(l); JS_FreeValue(ctx,l);
            if(!athena_js_option_float(ctx,o,"lower",&lower)||!athena_js_option_float(ctx,o,"upper",&upper)||
                !athena_js_option_float(ctx,o,"motorSpeed",&speed)||!athena_js_option_float(ctx,o,"maxMotorTorque",&torque))
                return JS_EXCEPTION;
        }
    }
    AthenaPhysics3DWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    if(athena_physics3d_joint_count(w)>=ATHENA_PHYSICS3D_MAX_JOINTS)
        return JS_ThrowRangeError(ctx,"%s: at most %u joints per world",names[magic],(unsigned)ATHENA_PHYSICS3D_MAX_JOINTS);
    AthenaJoint3D *j=magic==0?athena_joint3d_ball(w,a,b,p):magic==1?athena_joint3d_hinge(w,a,b,p,q):
        magic==2?athena_joint3d_distance(w,a,b,p,q,length,rope):athena_joint3d_weld(w,a,b,p);
    if(!j) return JS_ThrowRangeError(ctx,"%s: bodies of this world, at least one dynamic, a != b, finite points, nonzero axis",names[magic]);
    if(magic==1&&((limits&&athena_joint3d_set_limits(j,lower,upper)<0)||(torque>0&&athena_joint3d_set_motor(j,speed,torque)<0))) {
        athena_joint3d_remove(j); athena_joint3d_release(j);
        return JS_ThrowRangeError(ctx,"%s: lower <= upper, maxMotorTorque >= 0",names[magic]);
    }
    JSValue obj=JS_NewObjectClass(ctx,joint_id);
    if(JS_IsException(obj)) { athena_joint3d_remove(j); athena_joint3d_release(j); return obj; }
    JS_SetOpaque(obj,j); return obj;
}
static JSValue joint_limits(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    float v[2]; if(!floats(ctx,argc,argv,2,2,v,"Joint.setLimits")) return JS_EXCEPTION;
    AthenaJoint3D *j=get_joint(ctx,self); if(!j) return JS_EXCEPTION;
    if(athena_joint3d_set_limits(j,v[0],v[1])<0) return JS_ThrowRangeError(ctx,"setLimits: a hinge, lower <= upper");
    return JS_DupValue(ctx,self);
}
static JSValue joint_disable_limits(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Joint.disableLimits")) return JS_EXCEPTION;
    AthenaJoint3D *j=get_joint(ctx,self); if(!j) return JS_EXCEPTION;
    athena_joint3d_disable_limits(j); return JS_DupValue(ctx,self);
}
static JSValue joint_motor(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    float v[2]; if(!floats(ctx,argc,argv,2,2,v,"Joint.setMotor")) return JS_EXCEPTION;
    AthenaJoint3D *j=get_joint(ctx,self); if(!j) return JS_EXCEPTION;
    if(athena_joint3d_set_motor(j,v[0],v[1])<0) return JS_ThrowRangeError(ctx,"setMotor: a hinge, maxTorque >= 0");
    return JS_DupValue(ctx,self);
}
static JSValue joint_get(JSContext *ctx,JSValueConst self,int magic) {
    AthenaJoint3D *j=JS_GetOpaque2(ctx,self,joint_id);
    if(magic==1) return JS_NewBool(ctx,j&&athena_joint3d_alive(j));
    if(!j) return JS_ThrowTypeError(ctx,"Expected a live Physics3D.Joint");
    if(magic==2) return JS_NewFloat64(ctx,athena_joint3d_angle(j));
    static const char *const types[]={"ball","hinge","distance","weld"};
    return JS_NewString(ctx,types[athena_joint3d_type(j)]);
}
static JSValue joint_remove(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int dispose) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,dispose?"Joint.dispose":"Joint.remove")) return JS_EXCEPTION;
    if(!athena_js_class(ctx,self,joint_id)) return JS_EXCEPTION;
    AthenaJoint3D *j=JS_GetOpaque(self,joint_id);
    if(j) athena_joint3d_remove(j);
    if(dispose) { JS_SetOpaque(self,NULL); athena_joint3d_release(j); }
    return JS_UNDEFINED;
}
/* Body: 0 setPosition, 1 setVelocity, 2 setAngularVelocity (x, y, z),
 * 3 setRotation (x, y, z, w), 4 applyForce (x, y, z),
 * 5 applyImpulse (x, y, z[, px, py, pz]). */
static JSValue body_call(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic) {
    static const char *const names[]={"setPosition","setVelocity","setAngularVelocity","setRotation","applyForce","applyImpulse"};
    float v[6];
    int min=magic==3?4:3,max=magic==3?4:magic==5?6:3;
    if(!floats(ctx,argc,argv,min,max,v,names[magic])) return JS_EXCEPTION;
    if(magic==5&&argc!=3&&argc!=6) return JS_ThrowTypeError(ctx,"applyImpulse expects 3 or 6 arguments");
    AthenaBody3D *b=get_body(ctx,self); if(!b) return JS_EXCEPTION;
    int code;
    switch(magic) {
        case 0: code=athena_body3d_set_position(b,v[0],v[1],v[2]); break;
        case 1: code=athena_body3d_set_velocity(b,v[0],v[1],v[2]); break;
        case 2: code=athena_body3d_set_angular_velocity(b,v[0],v[1],v[2]); break;
        case 3: code=athena_body3d_set_rotation(b,v[0],v[1],v[2],v[3]); break;
        case 4: code=athena_body3d_apply_force(b,v); break;
        default: code=athena_body3d_apply_impulse(b,v,argc==6?&v[3]:NULL); break;
    }
    if(code<0) return throw_code(ctx,code,names[magic]);
    return JS_DupValue(ctx,self);
}
/* 0..2 x, y, z; 3..5 vx, vy, vz; 6..8 wx, wy, wz. */
static JSValue body_get(JSContext *ctx,JSValueConst self,int magic) {
    AthenaBody3D *b=get_body(ctx,self); if(!b) return JS_EXCEPTION;
    float v[3];
    if(magic<3) athena_body3d_get_position(b,v);
    else if(magic<6) athena_body3d_get_velocity(b,v);
    else athena_body3d_get_angular_velocity(b,v);
    return JS_NewFloat64(ctx,v[magic%3]);
}
static JSValue body_set_velocity(JSContext *ctx,JSValueConst self,JSValueConst value,int magic) {
    float f; if(!athena_js_float(ctx,value,&f,"velocity")) return JS_EXCEPTION;
    AthenaBody3D *b=get_body(ctx,self); if(!b) return JS_EXCEPTION;
    float v[3];
    if(magic<6) athena_body3d_get_velocity(b,v); else athena_body3d_get_angular_velocity(b,v);
    v[magic%3]=f;
    int code=magic<6?athena_body3d_set_velocity(b,v[0],v[1],v[2]):athena_body3d_set_angular_velocity(b,v[0],v[1],v[2]);
    return code<0?throw_code(ctx,code,"velocity"):JS_UNDEFINED;
}
static JSValue body_rotation(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,0,1,"Body.getRotation")) return JS_EXCEPTION;
    AthenaBody3D *b=get_body(ctx,self); if(!b) return JS_EXCEPTION;
    float q[4]; athena_body3d_get_rotation(b,q);
    JSValue a=argc&&!JS_IsUndefined(argv[0])?JS_DupValue(ctx,argv[0]):JS_NewArray(ctx);
    if(JS_IsException(a)) return a;
    for(uint32_t i=0;i<4;i++)
        if(JS_SetPropertyUint32(ctx,a,i,JS_NewFloat64(ctx,q[i]))<0) { JS_FreeValue(ctx,a); return JS_EXCEPTION; }
    return a;
}
static JSValue body_flag(JSContext *ctx,JSValueConst self,int magic) {
    AthenaBody3D *b=JS_GetOpaque2(ctx,self,body_id);
    if(magic==1) return JS_NewBool(ctx,b&&athena_body3d_alive(b));
    if(!b) return JS_ThrowTypeError(ctx,"Expected a live Physics3D.Body");
    if(magic==0) return JS_NewBool(ctx,athena_body3d_sleeping(b));
    static const char *const types[]={"dynamic","kinematic","static"};
    return JS_NewString(ctx,types[athena_body3d_type(b)]);
}
static JSValue body_wake(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Body.wake")) return JS_EXCEPTION;
    AthenaBody3D *b=get_body(ctx,self); if(!b) return JS_EXCEPTION;
    athena_body3d_wake(b); return JS_DupValue(ctx,self);
}
static JSValue body_bind(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"Body.bind")) return JS_EXCEPTION;
    AthenaNode3D *n=NULL;
    if(!JS_IsNull(argv[0])&&!JS_IsUndefined(argv[0])&&!(n=athena_node3d_from_value(ctx,argv[0]))) return JS_EXCEPTION;
    AthenaBody3D *b=get_body(ctx,self); if(!b) return JS_EXCEPTION;
    athena_body3d_bind(b,n); return JS_DupValue(ctx,self);
}
/* remove(): out of the world; dispose(): also drops this handle. */
static JSValue body_remove(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int dispose) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,dispose?"Body.dispose":"Body.remove")) return JS_EXCEPTION;
    if(!athena_js_class(ctx,self,body_id)) return JS_EXCEPTION;
    AthenaBody3D *b=JS_GetOpaque(self,body_id);
    if(b) athena_body3d_remove(b);
    if(dispose) { JS_SetOpaque(self,NULL); athena_body3d_release(b); }
    return JS_UNDEFINED;
}
static JSClassDef world_class={"Physics3D.World",.finalizer=world_finalizer};
static JSClassDef body_class={"Physics3D.Body",.finalizer=body_finalizer};
static JSClassDef joint_class={"Physics3D.Joint",.finalizer=joint_finalizer};
static const JSCFunctionListEntry world_methods[]={
    JS_CFUNC_MAGIC_DEF("addSphere",1,world_add,0),JS_CFUNC_MAGIC_DEF("addBox",1,world_add,1),
    JS_CFUNC_MAGIC_DEF("addCapsule",1,world_add,2),
    JS_CFUNC_DEF("step",1,world_step),JS_CFUNC_DEF("setGravity",3,world_set_gravity),
    JS_CGETSET_MAGIC_DEF("bodyCount",world_counts,NULL,0),JS_CGETSET_MAGIC_DEF("contactCount",world_counts,NULL,1),
    JS_CFUNC_DEF("attachLoop",0,world_attach),JS_CFUNC_DEF("detachLoop",0,world_detach),
    JS_CGETSET_DEF("attached",world_attached,NULL),JS_CFUNC_DEF("dispose",0,world_dispose),
    JS_CFUNC_MAGIC_DEF("addBallJoint",3,world_add_joint,0),JS_CFUNC_MAGIC_DEF("addHingeJoint",4,world_add_joint,1),
    JS_CFUNC_MAGIC_DEF("addDistanceJoint",4,world_add_joint,2),JS_CFUNC_MAGIC_DEF("addWeldJoint",3,world_add_joint,3),
    JS_CGETSET_MAGIC_DEF("jointCount",world_counts,NULL,2),JS_CGETSET_DEF("profile",world_profile,NULL)};
static const JSCFunctionListEntry joint_methods[]={
    JS_CGETSET_MAGIC_DEF("type",joint_get,NULL,0),JS_CGETSET_MAGIC_DEF("alive",joint_get,NULL,1),
    JS_CGETSET_MAGIC_DEF("angle",joint_get,NULL,2),JS_CFUNC_DEF("setLimits",2,joint_limits),
    JS_CFUNC_DEF("disableLimits",0,joint_disable_limits),JS_CFUNC_DEF("setMotor",2,joint_motor),
    JS_CFUNC_MAGIC_DEF("remove",0,joint_remove,0),JS_CFUNC_MAGIC_DEF("dispose",0,joint_remove,1)};
static const JSCFunctionListEntry body_methods[]={
    JS_CFUNC_MAGIC_DEF("setPosition",3,body_call,0),JS_CFUNC_MAGIC_DEF("setVelocity",3,body_call,1),
    JS_CFUNC_MAGIC_DEF("setAngularVelocity",3,body_call,2),JS_CFUNC_MAGIC_DEF("setRotation",4,body_call,3),
    JS_CFUNC_MAGIC_DEF("applyForce",3,body_call,4),JS_CFUNC_MAGIC_DEF("applyImpulse",3,body_call,5),
    JS_CGETSET_MAGIC_DEF("x",body_get,NULL,0),JS_CGETSET_MAGIC_DEF("y",body_get,NULL,1),JS_CGETSET_MAGIC_DEF("z",body_get,NULL,2),
    JS_CGETSET_MAGIC_DEF("vx",body_get,body_set_velocity,3),JS_CGETSET_MAGIC_DEF("vy",body_get,body_set_velocity,4),
    JS_CGETSET_MAGIC_DEF("vz",body_get,body_set_velocity,5),
    JS_CGETSET_MAGIC_DEF("wx",body_get,body_set_velocity,6),JS_CGETSET_MAGIC_DEF("wy",body_get,body_set_velocity,7),
    JS_CGETSET_MAGIC_DEF("wz",body_get,body_set_velocity,8),
    JS_CFUNC_DEF("getRotation",0,body_rotation),
    JS_CGETSET_MAGIC_DEF("sleeping",body_flag,NULL,0),JS_CGETSET_MAGIC_DEF("alive",body_flag,NULL,1),
    JS_CGETSET_MAGIC_DEF("type",body_flag,NULL,2),
    JS_CFUNC_DEF("wake",0,body_wake),JS_CFUNC_DEF("bind",1,body_bind),
    JS_CFUNC_MAGIC_DEF("remove",0,body_remove,0),JS_CFUNC_MAGIC_DEF("dispose",0,body_remove,1)};
static const JSCFunctionListEntry exports[]={
    JS_PROP_INT32_DEF("LOOP_PRIORITY",ATHENA_PHYSICS3D_LOOP_PRIORITY,JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MAX_BODIES",ATHENA_PHYSICS3D_MAX_BODIES,JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MAX_JOINTS",ATHENA_PHYSICS3D_MAX_JOINTS,JS_PROP_ENUMERABLE)};
static int init(JSContext *ctx,JSModuleDef *m) {
    if(athena_register_class(ctx,&world_id,&world_class)<0||athena_register_class(ctx,&body_id,&body_class)<0||
        athena_register_class(ctx,&joint_id,&joint_class)<0) return -1;
    JSValue proto=JS_NewObject(ctx); if(JS_IsException(proto)) return -1;
    JS_SetPropertyFunctionList(ctx,proto,world_methods,countof(world_methods));
    JSValue cls=JS_NewCFunction2(ctx,world_ctor,"World",0,JS_CFUNC_constructor,0);
    if(JS_IsException(cls)) { JS_FreeValue(ctx,proto); return -1; }
    JS_SetConstructor(ctx,cls,proto); JS_SetClassProto(ctx,world_id,proto);
    if(JS_SetModuleExport(ctx,m,"World",cls)<0) return -1;
    JSValue body_proto=JS_NewObject(ctx); if(JS_IsException(body_proto)) return -1;
    JS_SetPropertyFunctionList(ctx,body_proto,body_methods,countof(body_methods));
    JS_SetClassProto(ctx,body_id,body_proto);
    JSValue joint_proto=JS_NewObject(ctx); if(JS_IsException(joint_proto)) return -1;
    JS_SetPropertyFunctionList(ctx,joint_proto,joint_methods,countof(joint_methods));
    JS_SetClassProto(ctx,joint_id,joint_proto);
    return JS_SetModuleExportList(ctx,m,exports,countof(exports));
}
void athena_physics3d_js_cleanup(JSContext *ctx) { athena_physics3d_detach_owner(ctx); }
JSModuleDef *athena_physics3d_js_init(JSContext *ctx) {
    JSModuleDef *m=athena_push_module(ctx,init,exports,countof(exports),"Physics3D");
    if(m) JS_AddModuleExport(ctx,m,"World");
    return m;
}
