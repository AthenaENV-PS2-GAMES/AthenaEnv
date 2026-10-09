#include <athena_js_args.h>
#include <athena/debug3d.h>
#include <athena/loop.h>
#include <athena/js/camera3d.h>
#include <athena/js/matrix4.h>
#include <athena/js/model3d.h>
#include "ath_debug3d.h"
#define DEFAULT_COLOR 0x8000FF00u /* green */
/* setCamera(): a native Loop system ages segments in PRE_UPDATE (real time)
 * and draws them in POST_DRAW with the retained camera, before the Debug
 * overlay (priority 1000) so its panels stay on top. */
#define SYSTEM_PRIORITY 900
static AthenaCamera3D *auto_camera;
static int auto_system;
static int loop_func(void *opaque,AthenaLoopPhase phase,float value) {
    (void)opaque;
    if(phase==ATHENA_LOOP_PRE_UPDATE) athena_debug3d_age(value);
    else if(auto_camera) athena_debug3d_draw(auto_camera);
    return 0;
}
static void loop_release(void *opaque) {
    (void)opaque; auto_system=0;
    if(auto_camera) { athena_camera3d_js_release(auto_camera); auto_camera=NULL; }
}
static int color_arg(JSContext *ctx,int argc,JSValueConst *argv,int index,uint32_t *out) {
    *out=DEFAULT_COLOR;
    if(argc<=index||JS_IsUndefined(argv[index])) return 1;
    double v;
    if(!JS_IsNumber(argv[index])||JS_ToFloat64(ctx,&v,argv[index])<0||v!=(double)(int64_t)v||v<-2147483648.0||v>4294967295.0) {
        JS_ThrowTypeError(ctx,"color must be a Color.new() value"); return 0;
    }
    *out=(uint32_t)(int64_t)v; return 1;
}
static int seconds_arg(JSContext *ctx,int argc,JSValueConst *argv,int index,float *out) {
    *out=0;
    if(argc<=index||JS_IsUndefined(argv[index])) return 1;
    if(!athena_js_float(ctx,argv[index],out,"seconds")) return 0;
    if(*out<0) { JS_ThrowRangeError(ctx,"seconds must not be negative"); return 0; }
    return 1;
}
static int floats(JSContext *ctx,JSValueConst *argv,int count,float *out,const char *name) {
    for(int i=0;i<count;i++) if(!athena_js_float(ctx,argv[i],&out[i],name)) return 0;
    return 1;
}
static JSValue result(JSContext *ctx,int code) {
    return code<0?JS_ThrowRangeError(ctx,"Debug3D: invalid shape"):JS_NewInt32(ctx,code);
}
/* line(x1, y1, z1, x2, y2, z2, color?, seconds?) */
static JSValue js_line(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,6,8,"Debug3D.line")) return JS_EXCEPTION;
    float p[6],seconds; uint32_t color;
    if(!floats(ctx,argv,6,p,"coordinate")||!color_arg(ctx,argc,argv,6,&color)||!seconds_arg(ctx,argc,argv,7,&seconds)) return JS_EXCEPTION;
    int code=athena_debug3d_line(p,p+3,color,seconds);
    return code<0?result(ctx,code):JS_NewBool(ctx,code==0);
}
/* lines(values: Float32Array of x1,y1,z1,x2,y2,z2 groups, color?, seconds?) */
static JSValue js_lines(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,3,"Debug3D.lines")) return JS_EXCEPTION;
    float seconds; uint32_t color;
    if(!color_arg(ctx,argc,argv,1,&color)||!seconds_arg(ctx,argc,argv,2,&seconds)) return JS_EXCEPTION;
    AthenaJSArray values;
    if(!athena_js_array(ctx,argv[0],JS_TYPED_ARRAY_FLOAT32,&values,"values")) return JS_EXCEPTION;
    JSValue r;
    if(values.count%6) r=JS_ThrowRangeError(ctx,"values needs 6 floats per line");
    else {
        const float *v=values.data; int queued=0;
        for(size_t i=0;i+6<=values.count;i+=6) queued+=athena_debug3d_line(v+i,v+i+3,color,seconds)==0;
        r=JS_NewInt32(ctx,queued);
    }
    JS_FreeValue(ctx,values.backing); return r;
}
static int matrix_arg(JSContext *ctx,int argc,JSValueConst *argv,int index,const AthenaMatrix4 **out) {
    *out=NULL;
    if(argc<=index||JS_IsUndefined(argv[index])) return 1;
    return (*out=athena_matrix4_from_value(ctx,argv[index]))!=NULL;
}
/* box(minX, minY, minZ, maxX, maxY, maxZ, color?, seconds?, matrix?) */
static JSValue js_box(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,6,9,"Debug3D.box")) return JS_EXCEPTION;
    float p[6],seconds; uint32_t color; const AthenaMatrix4 *m;
    if(!floats(ctx,argv,6,p,"coordinate")||!color_arg(ctx,argc,argv,6,&color)||!seconds_arg(ctx,argc,argv,7,&seconds)||
        !matrix_arg(ctx,argc,argv,8,&m)) return JS_EXCEPTION;
    return result(ctx,athena_debug3d_box(p,p+3,m,color,seconds));
}
/* sphere(x, y, z, radius, color?, seconds?, segments = 16) */
static JSValue js_sphere(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,4,7,"Debug3D.sphere")) return JS_EXCEPTION;
    float p[4],seconds,segments=16; uint32_t color;
    if(!floats(ctx,argv,4,p,"sphere")||!color_arg(ctx,argc,argv,4,&color)||!seconds_arg(ctx,argc,argv,5,&seconds)) return JS_EXCEPTION;
    if(argc==7&&!JS_IsUndefined(argv[6])&&!athena_js_float(ctx,argv[6],&segments,"segments")) return JS_EXCEPTION;
    if(segments!=(float)(int)segments||segments<3||segments>ATHENA_DEBUG3D_MAX_SEGMENTS)
        return JS_ThrowRangeError(ctx,"segments must be an integer from 3 to %u",ATHENA_DEBUG3D_MAX_SEGMENTS);
    if(p[3]<0) return JS_ThrowRangeError(ctx,"radius must not be negative");
    return result(ctx,athena_debug3d_sphere(p,p[3],(uint32_t)segments,color,seconds));
}
/* axes(matrix, size = 1, seconds?) */
static JSValue js_axes(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,3,"Debug3D.axes")) return JS_EXCEPTION;
    AthenaMatrix4 *m=athena_matrix4_from_value(ctx,argv[0]); if(!m) return JS_EXCEPTION;
    float size=1,seconds;
    if(argc>=2&&!JS_IsUndefined(argv[1])&&!athena_js_float(ctx,argv[1],&size,"size")) return JS_EXCEPTION;
    if(!seconds_arg(ctx,argc,argv,2,&seconds)) return JS_EXCEPTION;
    AthenaMatrix4 copy=*m; /* the getters above may have run script */
    return result(ctx,athena_debug3d_axes(&copy,size,seconds));
}
/* grid(x, y, z, halfExtent, step, color?, seconds?) */
static JSValue js_grid(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,5,7,"Debug3D.grid")) return JS_EXCEPTION;
    float p[5],seconds; uint32_t color;
    if(!floats(ctx,argv,5,p,"grid")||!color_arg(ctx,argc,argv,5,&color)||!seconds_arg(ctx,argc,argv,6,&seconds)) return JS_EXCEPTION;
    if(p[3]<0||p[4]<=0||p[3]/p[4]>128) return JS_ThrowRangeError(ctx,"grid needs halfExtent >= 0, step > 0 and at most 128 lines per side");
    return result(ctx,athena_debug3d_grid(p,p[3],p[4],color,seconds));
}
/* frustum(camera, color?, seconds?) */
static JSValue js_frustum(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,3,"Debug3D.frustum")) return JS_EXCEPTION;
    float seconds; uint32_t color;
    if(!color_arg(ctx,argc,argv,1,&color)||!seconds_arg(ctx,argc,argv,2,&seconds)) return JS_EXCEPTION;
    AthenaCamera3D *c=athena_camera3d_from_value(ctx,argv[0]); if(!c) return JS_EXCEPTION;
    return result(ctx,athena_debug3d_frustum(c,color,seconds));
}
/* normals(instance | mesh, length = 0.25, color?, seconds?, matrix?) */
static JSValue js_normals(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,5,"Debug3D.normals")) return JS_EXCEPTION;
    float length=.25f,seconds; uint32_t color; const AthenaMatrix4 *m;
    if(argc>=2&&!JS_IsUndefined(argv[1])&&!athena_js_float(ctx,argv[1],&length,"length")) return JS_EXCEPTION;
    if(!color_arg(ctx,argc,argv,2,&color)||!seconds_arg(ctx,argc,argv,3,&seconds)||!matrix_arg(ctx,argc,argv,4,&m)) return JS_EXCEPTION;
    AthenaMatrix4 model; int has_model=m!=NULL; if(m) model=*m;
    const AthenaMesh3D *mesh=NULL;
    /* An Instance (with its own transform), else a Mesh: the failed lookup's
     * TypeError is discarded. */
    AthenaInstance3D *instance=athena_instance3d_from_value(ctx,argv[0]);
    if(instance) {
        mesh=athena_instance3d_mesh(instance);
        if(!has_model) { model=*athena_instance3d_transform(instance); has_model=1; }
    } else {
        JS_FreeValue(ctx,JS_GetException(ctx));
        mesh=athena_mesh3d_from_value(ctx,argv[0]);
        if(!mesh) { JS_FreeValue(ctx,JS_GetException(ctx)); return JS_ThrowTypeError(ctx,"Debug3D.normals expects a Model3D.Instance or Mesh"); }
    }
    return result(ctx,athena_debug3d_normals(mesh,has_model?&model:NULL,length,color,seconds));
}
/* setCamera(camera | null): draws the queue after every Loop draw. */
static JSValue js_set_camera(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,1,"Debug3D.setCamera")) return JS_EXCEPTION;
    AthenaCamera3D *c=NULL;
    if(!JS_IsNull(argv[0])&&!(c=athena_camera3d_from_value(ctx,argv[0]))) return JS_EXCEPTION;
    if(c) athena_camera3d_js_retain(c);
    if(auto_camera) athena_camera3d_js_release(auto_camera);
    auto_camera=c;
    if(c&&!auto_system) {
        AthenaLoopSystemDesc desc={.name="debug3d",.priority=SYSTEM_PRIORITY,.real_time=true,
            .phases=ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_PRE_UPDATE)|ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_POST_DRAW),
            .func=loop_func,.release=loop_release};
        int id=athena_loop_system_add(&desc);
        if(id<0) {
            athena_camera3d_js_release(c); auto_camera=NULL;
            return id==ATHENA_LOOP_SYSTEM_ENOMEM?JS_ThrowOutOfMemory(ctx):JS_ThrowInternalError(ctx,"Debug3D: cannot add the Loop system");
        }
        auto_system=id;
    } else if(!c&&auto_system) athena_loop_system_remove(auto_system); /* release() clears it */
    return JS_UNDEFINED;
}
/* draw(camera, dt = 0): for games without Loop.run(); draws, then ages. */
static JSValue js_draw(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,2,"Debug3D.draw")) return JS_EXCEPTION;
    float dt=0;
    if(argc==2&&!JS_IsUndefined(argv[1])) {
        if(!athena_js_float(ctx,argv[1],&dt,"dt")) return JS_EXCEPTION;
        if(dt<0) return JS_ThrowRangeError(ctx,"dt must not be negative");
    }
    AthenaCamera3D *c=athena_camera3d_from_value(ctx,argv[0]); if(!c) return JS_EXCEPTION;
    int drawn=athena_debug3d_draw(c);
    if(drawn<0) return JS_ThrowInternalError(ctx,"Debug3D.draw: graphics not initialized or invalid camera");
    athena_debug3d_age(dt);
    return JS_NewInt32(ctx,drawn);
}
static JSValue js_clear(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Debug3D.clear")) return JS_EXCEPTION;
    athena_debug3d_clear(); return JS_UNDEFINED;
}
static JSValue js_count(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic) {
    (void)self; (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,magic?"Debug3D.dropped":"Debug3D.count")) return JS_EXCEPTION;
    return JS_NewUint32(ctx,magic?athena_debug3d_dropped():athena_debug3d_count());
}
static JSValue js_show(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,0,1,"Debug3D.show")) return JS_EXCEPTION;
    if(argc&&!JS_IsUndefined(argv[0])) athena_debug3d_set_enabled(JS_ToBool(ctx,argv[0]));
    return JS_NewBool(ctx,athena_debug3d_enabled());
}
/* age(dt): what the Loop system does in PRE_UPDATE, for manual loops. */
static JSValue js_age(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,1,"Debug3D.age")) return JS_EXCEPTION;
    float dt; if(!athena_js_float(ctx,argv[0],&dt,"dt")) return JS_EXCEPTION;
    if(dt<0) return JS_ThrowRangeError(ctx,"dt must not be negative");
    athena_debug3d_age(dt); return JS_UNDEFINED;
}
static const JSCFunctionListEntry exports[]={
    JS_CFUNC_DEF("line",6,js_line),JS_CFUNC_DEF("lines",1,js_lines),JS_CFUNC_DEF("box",6,js_box),
    JS_CFUNC_DEF("sphere",4,js_sphere),JS_CFUNC_DEF("axes",1,js_axes),JS_CFUNC_DEF("grid",5,js_grid),
    JS_CFUNC_DEF("frustum",1,js_frustum),JS_CFUNC_DEF("normals",1,js_normals),
    JS_CFUNC_DEF("setCamera",1,js_set_camera),JS_CFUNC_DEF("draw",1,js_draw),JS_CFUNC_DEF("age",1,js_age),
    JS_CFUNC_DEF("clear",0,js_clear),JS_CFUNC_MAGIC_DEF("count",0,js_count,0),JS_CFUNC_MAGIC_DEF("dropped",0,js_count,1),
    JS_CFUNC_DEF("show",0,js_show),
    JS_PROP_INT32_DEF("MAX_LINES",ATHENA_DEBUG3D_MAX_LINES,JS_PROP_ENUMERABLE)};
static int init(JSContext *ctx,JSModuleDef *m) { return JS_SetModuleExportList(ctx,m,exports,countof(exports)); }
void athena_debug3d_js_cleanup(JSContext *ctx) {
    (void)ctx;
    if(auto_system) athena_loop_system_remove(auto_system);
    if(auto_camera) { athena_camera3d_js_release(auto_camera); auto_camera=NULL; }
    athena_debug3d_clear();
}
JSModuleDef *athena_debug3d_js_init(JSContext *ctx) {
    return athena_push_module(ctx,init,exports,countof(exports),"Debug3D");
}
