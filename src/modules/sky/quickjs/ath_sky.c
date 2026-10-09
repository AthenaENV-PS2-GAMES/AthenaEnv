#include <athena_js_args.h>
#include <athena/sky.h>
#include <athena/js/camera3d.h>
#include <athena/js/lights.h>
#include "ath_sky.h"
static int rgb_arg(JSContext *ctx,JSValueConst v,float out[3],const char *name) {
    if(!JS_IsArray(ctx,v)) { JS_ThrowTypeError(ctx,"%s must be [r, g, b]",name); return 0; }
    for(uint32_t i=0;i<3;i++) {
        JSValue c=JS_GetPropertyUint32(ctx,v,i); int ok=athena_js_float(ctx,c,&out[i],name); JS_FreeValue(ctx,c);
        if(!ok) return 0;
    }
    return 1;
}
static JSValue rgb_value(JSContext *ctx,const float c[3]) {
    JSValue a=JS_NewArray(ctx);
    for(uint32_t i=0;i<3&&!JS_IsException(a);i++) JS_SetPropertyUint32(ctx,a,i,JS_NewFloat64(ctx,c[i]));
    return a;
}
/* setColors({ zenith, horizon, ground }): missing ones keep their value. */
static JSValue set_colors(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,1,"Sky.setColors")||!JS_IsObject(argv[0])) return JS_ThrowTypeError(ctx,"Sky.setColors expects { zenith, horizon, ground }");
    AthenaSkyState s; athena_sky_get(&s);
    float *dst[3]={s.zenith,s.horizon,s.ground}; static const char *const keys[3]={"zenith","horizon","ground"};
    for(int i=0;i<3;i++) {
        JSValue v=JS_GetPropertyStr(ctx,argv[0],keys[i]); if(JS_IsException(v)) return v;
        int ok=JS_IsUndefined(v)||rgb_arg(ctx,v,dst[i],keys[i]); JS_FreeValue(ctx,v);
        if(!ok) return JS_EXCEPTION;
    }
    if(athena_sky_set_colors(s.zenith,s.horizon,s.ground)<0) return JS_ThrowRangeError(ctx,"Sky colors must be in [0, 1]");
    return JS_UNDEFINED;
}
/* setSun(dx, dy, dz, { color, size }) */
static JSValue set_sun(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,3,4,"Sky.setSun")) return JS_EXCEPTION;
    float d[3]; for(int i=0;i<3;i++) if(!athena_js_float(ctx,argv[i],&d[i],"direction")) return JS_EXCEPTION;
    AthenaSkyState s; athena_sky_get(&s);
    float color[3]={s.sun_color[0],s.sun_color[1],s.sun_color[2]},size=s.sun_size;
    if(argc==4&&!JS_IsUndefined(argv[3])) {
        if(!JS_IsObject(argv[3])) return JS_ThrowTypeError(ctx,"Sky.setSun options must be an object");
        JSValue c=JS_GetPropertyStr(ctx,argv[3],"color"); if(JS_IsException(c)) return c;
        int ok=JS_IsUndefined(c)||rgb_arg(ctx,c,color,"color"); JS_FreeValue(ctx,c);
        if(!ok||!athena_js_option_float(ctx,argv[3],"size",&size)) return JS_EXCEPTION;
    }
    if(athena_sky_set_sun(d,color,size)<0) return JS_ThrowRangeError(ctx,"Sky.setSun: nonzero direction, color in [0, 1], size 0-256");
    return JS_UNDEFINED;
}
static JSValue set_time(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,1,"Sky.setTime")) return JS_EXCEPTION;
    float h; if(!athena_js_float(ctx,argv[0],&h,"hours")) return JS_EXCEPTION;
    athena_sky_set_time(h); return JS_UNDEFINED;
}
/* apply(lights, { fog = true }) */
static JSValue apply(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,2,"Sky.apply")) return JS_EXCEPTION;
    int fog=1;
    if(argc==2&&JS_IsObject(argv[1])) {
        JSValue f=JS_GetPropertyStr(ctx,argv[1],"fog"); if(JS_IsException(f)) return f;
        if(!JS_IsUndefined(f)) fog=JS_ToBool(ctx,f);
        JS_FreeValue(ctx,f);
    }
    AthenaLights *l=athena_lights_from_value(ctx,argv[0]); if(!l) return JS_EXCEPTION;
    athena_sky_apply(l,fog); return JS_UNDEFINED;
}
/* draw(camera, bands = 16): call after Screen.clear(), before the 3D scene. */
static JSValue draw(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,2,"Sky.draw")) return JS_EXCEPTION;
    uint32_t bands=16;
    if(argc==2&&!JS_IsUndefined(argv[1])&&JS_ToUint32(ctx,&bands,argv[1])<0) return JS_EXCEPTION;
    if(bands<1||bands>64) return JS_ThrowRangeError(ctx,"bands must be 1 to 64");
    AthenaCamera3D *c=athena_camera3d_from_value(ctx,argv[0]); if(!c) return JS_EXCEPTION;
    int n=athena_sky_draw(c,bands);
    if(n<0) return JS_ThrowInternalError(ctx,"Sky.draw: graphics not initialized or invalid camera");
    return JS_NewInt32(ctx,n);
}
static JSValue color_at(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,1,"Sky.colorAt")) return JS_EXCEPTION;
    float e; if(!athena_js_float(ctx,argv[0],&e,"elevation")) return JS_EXCEPTION;
    float c[3]; athena_sky_color_at(e,c); return rgb_value(ctx,c);
}
/* clearColor(): the horizon as a Color.new() value, for Screen.clear(). */
static JSValue clear_color(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; (void)argc; (void)argv;
    AthenaSkyState s; athena_sky_get(&s);
    uint32_t v=0x80000000u;
    for(int i=0;i<3;i++) { float x=s.horizon[i]*255+.5f; v|=(uint32_t)(x<0?0:x>255?255:x)<<(8*i); }
    return JS_NewUint32(ctx,v);
}
static JSValue state(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; (void)argc; (void)argv;
    AthenaSkyState s; athena_sky_get(&s);
    JSValue o=JS_NewObject(ctx);
    JS_SetPropertyStr(ctx,o,"time",JS_NewFloat64(ctx,s.time));
    JS_SetPropertyStr(ctx,o,"zenith",rgb_value(ctx,s.zenith)); JS_SetPropertyStr(ctx,o,"horizon",rgb_value(ctx,s.horizon));
    JS_SetPropertyStr(ctx,o,"ground",rgb_value(ctx,s.ground)); JS_SetPropertyStr(ctx,o,"sunDirection",rgb_value(ctx,s.sun_direction));
    JS_SetPropertyStr(ctx,o,"sunColor",rgb_value(ctx,s.sun_color)); JS_SetPropertyStr(ctx,o,"ambient",rgb_value(ctx,s.ambient));
    JS_SetPropertyStr(ctx,o,"light",rgb_value(ctx,s.light)); JS_SetPropertyStr(ctx,o,"lightDirection",rgb_value(ctx,s.light_direction));
    return o;
}
static const JSCFunctionListEntry exports[]={
    JS_CFUNC_DEF("setColors",1,set_colors),JS_CFUNC_DEF("setSun",3,set_sun),JS_CFUNC_DEF("setTime",1,set_time),
    JS_CFUNC_DEF("apply",1,apply),JS_CFUNC_DEF("draw",1,draw),JS_CFUNC_DEF("colorAt",1,color_at),
    JS_CFUNC_DEF("clearColor",0,clear_color),JS_CFUNC_DEF("state",0,state)};
static int init(JSContext *ctx,JSModuleDef *m) { return JS_SetModuleExportList(ctx,m,exports,countof(exports)); }
JSModuleDef *athena_sky_js_init(JSContext *ctx) { return athena_push_module(ctx,init,exports,countof(exports),"Sky"); }
