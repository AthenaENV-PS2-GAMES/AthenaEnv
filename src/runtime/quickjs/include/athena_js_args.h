#ifndef ATHENA_JS_ARGS_H
#define ATHENA_JS_ARGS_H
#include <ath_env.h>
#include <math.h>
#include <float.h>
#include <athena/float_bits.h>

static inline int athena_js_class(JSContext *ctx, JSValueConst value, JSClassID id) {
    if(JS_GetClassID(value)==id) return 1;
    JS_ThrowTypeError(ctx,"Invalid object class"); return 0;
}

static inline int athena_js_argc(JSContext *ctx, int argc, int min, int max, const char *name) {
    if(argc>=min && argc<=max) return 1;
    JS_ThrowTypeError(ctx,"%s expects %d..%d arguments",name,min,max); return 0;
}
static inline int athena_js_float(JSContext *ctx, JSValueConst value, float *out, const char *name) {
    double n;
    if(!JS_IsNumber(value)) { JS_ThrowTypeError(ctx,"%s must be a number",name); return 0; }
    if(JS_ToFloat64(ctx,&n,value)) return 0;
    if(!athena_double_isfinite(n) || n > FLT_MAX || n < -FLT_MAX) {
        JS_ThrowRangeError(ctx,"%s must be a finite float",name); return 0;
    }
    *out=(float)n; return 1;
}
static inline int athena_js_option_float(JSContext *ctx, JSValueConst options,
    const char *key, float *out) {
    JSValue value=JS_GetPropertyStr(ctx,options,key);
    if(JS_IsException(value)) return 0;
    int ok=JS_IsUndefined(value) || athena_js_float(ctx,value,out,key);
    JS_FreeValue(ctx,value); return ok;
}
/* Pins the actual backing allocation; callers release backing after their copy.
 * Strict TypedArray types and subtraction-based bounds checks honor subarray(). */
typedef struct { JSValue backing; void *data; size_t count; } AthenaJSArray;
static inline int athena_js_array(JSContext *ctx, JSValueConst value, int type,
    AthenaJSArray *out, const char *name) {
    out->backing=JS_UNDEFINED; out->data=NULL; out->count=0;
    if(JS_GetTypedArrayType(value)!=type) {
        JS_ThrowTypeError(ctx,"%s has an invalid TypedArray type",name); return 0;
    }
    size_t offset=0,length=0,stride=0,size=0;
    JSValue backing=JS_GetTypedArrayBuffer(ctx,value,&offset,&length,&stride);
    if(JS_IsException(backing)) return 0;
    /* Concurrent mutation would invalidate validation-before-copy guarantees. */
    if(JS_IsSharedArrayBuffer(backing)) {
        JS_FreeValue(ctx,backing);
        JS_ThrowTypeError(ctx,"%s requires a nonshared ArrayBuffer",name); return 0;
    }
    uint8_t *data=JS_GetArrayBuffer(ctx,&size,backing);
    if(!data || offset>size || length>size-offset || stride==0 || length%stride) {
        JS_FreeValue(ctx,backing); JS_ThrowTypeError(ctx,"%s has a detached or invalid buffer",name); return 0;
    }
    out->backing=backing; out->data=data+offset; out->count=length/stride; return 1;
}
#endif
