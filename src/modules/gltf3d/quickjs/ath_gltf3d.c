#include <stdio.h>
#include <athena_js_args.h>
#include <athena/gltf3d.h>
#include <athena/js/scene3d.h>
#include <athena/js/animation3d.h>
#include <athena/js/model3d.h>
#include "ath_gltf3d.h"
/* GLTF3D.load(path, material?): { root, nodes, names, clips } as handles that
 * hold their own references; the loaded scene itself is released before
 * returning. material replaces every primitive's material, as Model3D.load(). */
static JSValue load(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,2,"GLTF3D.load")) return JS_EXCEPTION;
    if(!JS_IsString(argv[0])) return JS_ThrowTypeError(ctx,"GLTF3D.load expects a path");
    int override=argc==2&&!JS_IsUndefined(argv[1]);
    AthenaMaterial3D material;
    if(!athena_model3d_js_material(ctx,override?argv[1]:JS_UNDEFINED,&material)) return JS_EXCEPTION;
    const char *path=JS_ToCString(ctx,argv[0]);
    if(!path) { athena_texture3d_release(material.texture); return JS_EXCEPTION; }
    AthenaGltf3D *scene=NULL; int code=athena_gltf3d_load(path,override?&material:NULL,&scene);
    athena_texture3d_release(material.texture);
    if(code<0) {
        JSValue error=code==ATHENA_MODEL3D_ENOMEM?JS_ThrowOutOfMemory(ctx):
            JS_ThrowTypeError(ctx,"GLTF3D.load(%s): %s",path,athena_model3d_error(code));
        JS_FreeCString(ctx,path); return error;
    }
    JS_FreeCString(ctx,path);
    JSValue result=JS_NewObject(ctx),nodes=JS_NewArray(ctx),names=JS_NewArray(ctx),clips=JS_NewObject(ctx);
    int failed=JS_IsException(result)||JS_IsException(nodes)||JS_IsException(names)||JS_IsException(clips);
    for(uint32_t i=0;!failed&&i<athena_gltf3d_node_count(scene);i++) {
        JSValue node=athena_node3d_to_value(ctx,athena_gltf3d_node(scene,i));
        failed=JS_IsException(node)||JS_SetPropertyUint32(ctx,nodes,i,node)<0||
            JS_SetPropertyUint32(ctx,names,i,JS_NewString(ctx,athena_gltf3d_node_name(scene,i)))<0;
    }
    for(uint32_t i=0;!failed&&i<athena_gltf3d_clip_count(scene);i++) {
        char index[16]; const char *name=athena_gltf3d_clip_name(scene,i);
        if(!*name) { snprintf(index,sizeof(index),"%u",(unsigned)i); name=index; }
        JSValue clip=athena_clip3d_to_value(ctx,athena_gltf3d_clip(scene,i));
        failed=JS_IsException(clip)||JS_SetPropertyStr(ctx,clips,name,clip)<0;
    }
    if(!failed) {
        JSValue root=athena_node3d_to_value(ctx,athena_gltf3d_root(scene));
        failed=JS_IsException(root)||JS_SetPropertyStr(ctx,result,"root",root)<0||
            JS_SetPropertyStr(ctx,result,"nodes",JS_DupValue(ctx,nodes))<0||
            JS_SetPropertyStr(ctx,result,"names",JS_DupValue(ctx,names))<0||
            JS_SetPropertyStr(ctx,result,"clips",JS_DupValue(ctx,clips))<0;
    }
    athena_gltf3d_release(scene);
    JS_FreeValue(ctx,nodes); JS_FreeValue(ctx,names); JS_FreeValue(ctx,clips);
    if(failed) { JS_FreeValue(ctx,result); return JS_EXCEPTION; }
    return result;
}
static const JSCFunctionListEntry exports[]={JS_CFUNC_DEF("load",2,load)};
static int init(JSContext *ctx,JSModuleDef *m) { return JS_SetModuleExportList(ctx,m,exports,countof(exports)); }
JSModuleDef *athena_gltf3d_js_init(JSContext *ctx) {
    return athena_push_module(ctx,init,exports,countof(exports),"GLTF3D");
}
