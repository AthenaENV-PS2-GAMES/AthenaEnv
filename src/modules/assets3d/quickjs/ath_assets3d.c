#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <athena_js_args.h>
#include <athena/texture3d.h>
#include <athena/texture_decode_job.h>
#include <athena/js/job.h>
#include <athena/js/model3d.h>
#include "ath_assets3d.h"
/* Assets3DNative.decodeTexture(path, filter?, wrap?, maxBytes?): a Job of a Model3D.Texture whose
 * file was read and decoded on the job pool; the script thread only adopts the pixels. */
typedef struct { AthenaTexture3DFilter filter; AthenaTexture3DWrap wrap; char path[256]; } DecodeUser;
static const char *decode_message(int result) {
    switch(result) {
    case ATHENA_TEXTURE_DECODE_TOO_LARGE: return "the file is larger than maxBytes:";
    case ATHENA_TEXTURE_DECODE_CANCELLED: return "cancelled decoding";
    case -3: return "cannot read";
    case -5: return "unsupported texture size or format:";
    case -4: return "not a decodable image:";
    case -2: return "out of memory decoding";
    default: return "cannot decode";
    }
}
static int decode_settle(JSContext *ctx,AthenaJob *job,AthenaJobState state,int result,void *user,JSValue *outcome,bool *failed) {
    DecodeUser *u=user;
    uint32_t width=0,height=0; AthenaTexture3D *texture=NULL; int code=0; AthenaTexture3DIndexed *indexed=NULL;
    uint32_t *pixels=state==ATHENA_JOB_DONE?athena_texture_decode_take_indexed(job,&indexed,&width,&height):NULL;
    if(pixels) { code=athena_texture3d_adopt_indexed(pixels,indexed,width,height,u->filter,u->wrap,&texture); if(code<0) { free(pixels); athena_texture3d_indexed_free(indexed); } else athena_texture3d_allow_compaction(texture); }
    if(!texture) {
        char message[384];
        snprintf(message,sizeof(message),"Assets3D texture: %s '%s'",state==ATHENA_JOB_CANCELLED?"cancelled decoding":
            state==ATHENA_JOB_DONE?"cannot adopt":decode_message(result),u->path);
        *failed=true; *outcome=JS_NewError(ctx);
        if(JS_IsException(*outcome)) return -1;
        JS_DefinePropertyValueStr(ctx,*outcome,"message",JS_NewString(ctx,message),JS_PROP_WRITABLE|JS_PROP_CONFIGURABLE);
        JS_DefinePropertyValueStr(ctx,*outcome,"path",JS_NewString(ctx,u->path),JS_PROP_C_W_E);
        return 0;
    }
    *outcome=athena_texture3d_js_wrap(ctx,texture);
    return JS_IsException(*outcome)?-1:0;
}
static void decode_free_user(JSRuntime *rt,void *user) { (void)rt; free(user); }
static const AthenaJsJobKind decode_kind={.owner="Assets3D",.settle=decode_settle,.free_user=decode_free_user};
static JSValue decode_texture(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    const char *name="Assets3DNative.decodeTexture";
    if(!athena_js_argc(ctx,argc,1,4,name)||!JS_IsString(argv[0])) return JS_ThrowTypeError(ctx,"%s expects a path string",name);
    float filter=0,wrap=0,max=0;
    if(argc>=2&&!JS_IsUndefined(argv[1])&&!athena_js_float(ctx,argv[1],&filter,"filter")) return JS_EXCEPTION;
    if(argc>=3&&!JS_IsUndefined(argv[2])&&!athena_js_float(ctx,argv[2],&wrap,"wrap")) return JS_EXCEPTION;
    if(argc>=4&&!JS_IsUndefined(argv[3])&&!athena_js_float(ctx,argv[3],&max,"maxBytes")) return JS_EXCEPTION;
    if(filter!=0&&filter!=1) return JS_ThrowRangeError(ctx,"Invalid texture filter");
    if(wrap!=0&&wrap!=1&&wrap!=2&&wrap!=3) return JS_ThrowRangeError(ctx,"Invalid texture wrap");
    if(!(max>=0&&max<=16777216.0f)) return JS_ThrowRangeError(ctx,"%s maxBytes must be from 1 to 16777216",name);
    size_t length; const char *path=JS_ToCStringLen(ctx,&length,argv[0]);
    if(!path) return JS_EXCEPTION;
    if(!length||length!=strlen(path)||length>=sizeof(((DecodeUser *)0)->path)) { JS_FreeCString(ctx,path); return JS_ThrowRangeError(ctx,"Invalid texture path"); }
    DecodeUser *user=calloc(1,sizeof(*user));
    if(!user) { JS_FreeCString(ctx,path); return JS_ThrowOutOfMemory(ctx); }
    user->filter=(AthenaTexture3DFilter)filter; user->wrap=(AthenaTexture3DWrap)wrap; strcpy(user->path,path);
    AthenaJob *job=athena_texture_decode_submit(path,(size_t)max);
    JS_FreeCString(ctx,path);
    return athena_js_job_new(ctx,&decode_kind,job,user,name);
}
static const JSCFunctionListEntry exports[]={JS_CFUNC_DEF("decodeTexture",1,decode_texture)};
static int init(JSContext *ctx,JSModuleDef *m) { return JS_SetModuleExportList(ctx,m,exports,countof(exports)); }
JSModuleDef *athena_assets3d_native_js_init(JSContext *ctx) {
    return athena_push_module(ctx,init,exports,countof(exports),"Assets3DNative");
}
