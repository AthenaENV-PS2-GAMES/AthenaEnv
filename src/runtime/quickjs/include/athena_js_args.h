#ifndef ATHENA_JS_ARGS_H
#define ATHENA_JS_ARGS_H
#include <ath_env.h>
#include <math.h>
#include <stdlib.h>
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
/* Hot path of every 3D setter. The R5900 emulates double in software, so
 * integers and float32 values skip it and doubles are only narrowed: values
 * beyond the float range become Inf and fail the bit test, like NaN/Inf. */
static inline int athena_js_float(JSContext *ctx, JSValueConst value, float *out, const char *name) {
    float f;
    int tag=JS_VALUE_GET_NORM_TAG(value);
    if(tag==JS_TAG_INT) { *out=(float)JS_VALUE_GET_INT(value); return 1; }
    if(tag==JS_CUSTOM_TAG_FLOAT32) f=JS_VALUE_GET_FLOAT32(value);
    else if(tag==JS_TAG_FLOAT64) f=(float)JS_VALUE_GET_FLOAT64(value);
    else { JS_ThrowTypeError(ctx,"%s must be a number",name); return 0; }
    if(!athena_float_isfinite(f)) {
        JS_ThrowRangeError(ctx,"%s must be a finite float",name); return 0;
    }
    *out=f; return 1;
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
/* Property names cached as atoms, for objects written every frame. Atoms
 * belong to a runtime: the first runtime that writes fills the table and owns
 * it until athena_js_atoms_free() in the module cleanup, which must run
 * before that runtime is freed. Other runtimes make an atom per write. */
typedef struct { const char *const *names; unsigned count; JSAtom *atoms; JSRuntime *runtime; } AthenaJSAtoms;
static inline void athena_js_atoms_free(JSContext *ctx, AthenaJSAtoms *t) {
    if(t->runtime!=JS_GetRuntime(ctx)) return;
    for(unsigned i=0;i<t->count;i++) JS_FreeAtom(ctx,t->atoms[i]);
    t->runtime=NULL;
}
static inline void athena_js_atoms_fill(JSContext *ctx, AthenaJSAtoms *t) {
    if(t->runtime) return;
    for(unsigned i=0;i<t->count;i++) {
        t->atoms[i]=JS_NewAtom(ctx,t->names[i]);
        if(t->atoms[i]==JS_ATOM_NULL) {
            while(i--) JS_FreeAtom(ctx,t->atoms[i]);
            JS_FreeValue(ctx,JS_GetException(ctx)); return; /* stay on the slow path */
        }
    }
    t->runtime=JS_GetRuntime(ctx);
}
/* Defines names[index] on a new object, or assigns it on a caller's object
 * (an existing own data property is the fast path). Consumes value. */
static inline int athena_js_put(JSContext *ctx, AthenaJSAtoms *t, JSValueConst obj,
    int define, unsigned index, JSValue value) {
    athena_js_atoms_fill(ctx,t);
    int cached=t->runtime==JS_GetRuntime(ctx);
    JSAtom atom=cached?t->atoms[index]:JS_NewAtom(ctx,t->names[index]);
    if(atom==JS_ATOM_NULL) { JS_FreeValue(ctx,value); return -1; }
    int result=define?JS_DefinePropertyValue(ctx,obj,atom,value,JS_PROP_C_W_E):
        JS_SetProperty(ctx,obj,atom,value);
    if(!cached) JS_FreeAtom(ctx,atom);
    return result<0?-1:0;
}
/* Bulk calls over an array of handles. Element reads may run script (getters,
 * proxies) that disposes handles or detaches buffers, so every element is read
 * and its native object retained first; the caller takes TypedArray pointers
 * only afterwards and releases with athena_js_handles_free(). */
typedef struct {
    void **items; uint32_t count; void *stack[64];
    void (*release)(void *item);
} AthenaJSHandles;
static inline void athena_js_handles_free(AthenaJSHandles *h) {
    for(uint32_t i=0;i<h->count;i++) h->release(h->items[i]);
    if(h->items!=h->stack) free(h->items);
    h->items=h->stack; h->count=0;
}
/* get() returns NULL with a pending exception for a wrong or dead handle. */
static inline int athena_js_handles(JSContext *ctx, JSValueConst array, AthenaJSHandles *h,
    void *(*get)(JSContext *ctx, JSValueConst value), void (*retain)(void *item),
    void (*release)(void *item), const char *name) {
    h->items=h->stack; h->count=0; h->release=release;
    int is_array=JS_IsArray(ctx,array);
    if(is_array<0) return 0;
    if(!is_array) { JS_ThrowTypeError(ctx,"%s must be an array",name); return 0; }
    JSValue value=JS_GetPropertyStr(ctx,array,"length"); int64_t length=0;
    int failed=JS_IsException(value)||JS_ToInt64(ctx,&length,value)<0;
    JS_FreeValue(ctx,value);
    if(failed) return 0;
    if(length>65536) { JS_ThrowRangeError(ctx,"%s: at most 65536 handles per call",name); return 0; }
    if(length>(int64_t)countof(h->stack)) {
        h->items=malloc(sizeof(*h->items)*(size_t)length);
        if(!h->items) { h->items=h->stack; JS_ThrowOutOfMemory(ctx); return 0; }
    }
    for(int64_t i=0;i<length;i++) {
        JSValue element=JS_GetPropertyUint32(ctx,array,(uint32_t)i);
        void *item=JS_IsException(element)?NULL:get(ctx,element);
        JS_FreeValue(ctx,element);
        if(!item) { athena_js_handles_free(h); return 0; }
        retain(item); h->items[h->count++]=item;
    }
    return 1;
}
/* Handles of objects that act by themselves in a native system (camera rigs,
 * animation players) are kept alive until dispose(), so dropping the last JS
 * reference does not silently stop them. The module's cleanup frees what is
 * left before its runtime goes away. */
typedef struct AthenaJSKept { JSContext *ctx; JSValue value; struct AthenaJSKept *next; } AthenaJSKept;
static inline int athena_js_keep(JSContext *ctx, AthenaJSKept **list, JSValueConst value) {
    AthenaJSKept *k=js_malloc(ctx,sizeof(*k)); if(!k) return 0;
    k->ctx=ctx; k->value=JS_DupValue(ctx,value); k->next=*list; *list=k; return 1;
}
/* Drops the kept reference to the object handled by opaque, if any. */
static inline void athena_js_unkeep(JSContext *ctx, AthenaJSKept **list, void *opaque, JSClassID id) {
    for(AthenaJSKept **p=list;*p;p=&(*p)->next) if((*p)->ctx==ctx&&JS_GetOpaque((*p)->value,id)==opaque) {
        AthenaJSKept *k=*p; *p=k->next; JS_FreeValue(ctx,k->value); js_free(ctx,k); return;
    }
}
/* Frees the references kept for ctx (module cleanup). */
static inline void athena_js_keep_free(JSContext *ctx, AthenaJSKept **list) {
    for(AthenaJSKept **p=list;*p;) {
        if((*p)->ctx!=ctx) { p=&(*p)->next; continue; }
        AthenaJSKept *k=*p; *p=k->next; JS_FreeValue(ctx,k->value); js_free(ctx,k);
    }
}
/* Optional output object: undefined makes a new object (*define=1); an
 * object is reused and returned (*define=0). */
static inline int athena_js_out_object(JSContext *ctx, JSValueConst out, JSValue *obj, int *define,
    const char *name) {
    if(JS_IsUndefined(out)) { *obj=JS_NewObject(ctx); *define=1; return !JS_IsException(*obj); }
    if(!JS_IsObject(out)) { JS_ThrowTypeError(ctx,"%s must be an object",name); return 0; }
    *obj=JS_DupValue(ctx,out); *define=0; return 1;
}
#endif
