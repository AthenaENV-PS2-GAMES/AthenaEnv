#include <string.h>
#include <athena_js_args.h>
#include <athena/savegame.h>
#include "ath_savegame.h"
/* The bytes of an ArrayBuffer or typed array view (borrowed). */
static int bytes_of(JSContext *ctx,JSValueConst value,const uint8_t **data,size_t *size,JSValue *hold) {
    *hold=JS_UNDEFINED;
    size_t length;
    uint8_t *buffer=JS_GetArrayBuffer(ctx,&length,value);
    if(buffer) { *data=buffer; *size=length; return 1; }
    JS_FreeValue(ctx,JS_GetException(ctx));
    size_t offset,element;
    JSValue backing=JS_GetTypedArrayBuffer(ctx,value,&offset,&length,&element);
    if(JS_IsException(backing)) { JS_FreeValue(ctx,JS_GetException(ctx)); JS_ThrowTypeError(ctx,"expected an ArrayBuffer or a typed array"); return 0; }
    size_t total;
    buffer=JS_GetArrayBuffer(ctx,&total,backing);
    if(!buffer||offset+length>total) { JS_FreeValue(ctx,backing); JS_ThrowTypeError(ctx,"detached buffer"); return 0; }
    *data=buffer+offset; *size=length; *hold=backing; return 1;
}
/* crc32(bytes, crc = 0) */
static JSValue js_crc32(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,2,"SaveGame.crc32")) return JS_EXCEPTION;
    uint32_t crc=0;
    if(argc==2&&JS_ToUint32(ctx,&crc,argv[1])<0) return JS_EXCEPTION;
    const uint8_t *data; size_t size; JSValue hold;
    if(!bytes_of(ctx,argv[0],&data,&size,&hold)) return JS_EXCEPTION;
    crc=athena_savegame_crc32(crc,data,size);
    JS_FreeValue(ctx,hold);
    return JS_NewUint32(ctx,crc);
}
/* utf8Encode(string): ArrayBuffer of its UTF-8 bytes (QuickJS's own conversion). */
static JSValue js_encode(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,1,"SaveGame.utf8Encode")) return JS_EXCEPTION;
    if(!JS_IsString(argv[0])) return JS_ThrowTypeError(ctx,"utf8Encode expects a string");
    size_t length; const char *text=JS_ToCStringLen(ctx,&length,argv[0]);
    if(!text) return JS_EXCEPTION;
    JSValue buffer=JS_NewArrayBufferCopy(ctx,(const uint8_t *)text,length);
    JS_FreeCString(ctx,text);
    return buffer;
}
/* utf8Decode(bytes): the string (invalid sequences become U+FFFD). */
static JSValue js_decode(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,1,"SaveGame.utf8Decode")) return JS_EXCEPTION;
    const uint8_t *data; size_t size; JSValue hold;
    if(!bytes_of(ctx,argv[0],&data,&size,&hold)) return JS_EXCEPTION;
    JSValue text=JS_NewStringLen(ctx,(const char *)data,size);
    JS_FreeValue(ctx,hold);
    return text;
}
static const JSCFunctionListEntry exports[]={
    JS_CFUNC_DEF("crc32",1,js_crc32),JS_CFUNC_DEF("utf8Encode",1,js_encode),JS_CFUNC_DEF("utf8Decode",1,js_decode)};
static int init(JSContext *ctx,JSModuleDef *m) { return JS_SetModuleExportList(ctx,m,exports,countof(exports)); }
JSModuleDef *athena_savegame_js_init(JSContext *ctx) {
    return athena_push_module(ctx,init,exports,countof(exports),"SaveGameNative");
}
