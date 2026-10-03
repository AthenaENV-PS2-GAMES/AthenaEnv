#include <athena_js_args.h>
#include <athena/js/model3d.h>
#include <athena/js/matrix4.h>
#include "ath_model3d.h"
static JSClassID mesh_id,instance_id;
static AthenaMesh3D *get_mesh(JSContext *ctx,JSValueConst v) {
    AthenaMesh3D *m=JS_GetOpaque2(ctx,v,mesh_id);
    if(!m) JS_ThrowTypeError(ctx,"Expected a live Model3D.Mesh");
    return m;
}
AthenaInstance3D *athena_instance3d_from_value(JSContext *ctx,JSValueConst v) {
    AthenaInstance3D *i=JS_GetOpaque2(ctx,v,instance_id);
    if(!i) JS_ThrowTypeError(ctx,"Expected a live Model3D.Instance");
    return i;
}
static void mesh_finalizer(JSRuntime *rt,JSValue v) { (void)rt; athena_mesh3d_release(JS_GetOpaque(v,mesh_id)); }
static void instance_finalizer(JSRuntime *rt,JSValue v) { (void)rt; athena_instance3d_release(JS_GetOpaque(v,instance_id)); }
static JSValue wrap_mesh(JSContext *ctx,AthenaMesh3D *m) {
    JSValue obj=JS_NewObjectClass(ctx,mesh_id);
    if(JS_IsException(obj)) { athena_mesh3d_release(m); return obj; }
    JS_SetOpaque(obj,m); return obj;
}
static JSValue throw_result(JSContext *ctx,int result) {
    if(result==ATHENA_MODEL3D_ENOMEM) return JS_ThrowOutOfMemory(ctx);
    if(result==ATHENA_MODEL3D_EINVAL) return JS_ThrowRangeError(ctx,"%s",athena_model3d_error(result));
    return JS_ThrowInternalError(ctx,"%s (code %d)",athena_model3d_error(result),result);
}
static JSValue throw_geometry(JSContext *ctx,const AthenaGeometry3D *g) {
    uint32_t offset;
    AthenaGeometry3DIssue issue=athena_geometry3d_validate(g,&offset);
    switch(issue) {
        case ATHENA_GEOMETRY3D_POSITION: {
            uint32_t bits; memcpy(&bits,&g->positions[offset],sizeof(bits));
            return JS_ThrowRangeError(ctx,"positions[%u] must be finite (bits 0x%08x)",offset,bits);
        }
        case ATHENA_GEOMETRY3D_COLOR: {
            uint32_t bits; memcpy(&bits,&g->colors[offset],sizeof(bits));
            return JS_ThrowRangeError(ctx,"colors[%u] must be finite and in [0, 1] (bits 0x%08x)",offset,bits);
        }
        case ATHENA_GEOMETRY3D_INDEX:
            return JS_ThrowRangeError(ctx,"indices[%u] = %u exceeds vertex count %u",offset,g->indices[offset],g->vertex_count);
        default:
            return JS_ThrowRangeError(ctx,"Invalid geometry counts (issue %d, vertices %u, colors %u, indices %u)",
                issue,g->vertex_count,g->color_count,g->index_count);
    }
}
static JSValue from_geometry(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,1,"Mesh.fromGeometry")) return JS_EXCEPTION;
    if(!JS_IsObject(argv[0])) return JS_ThrowTypeError(ctx,"Geometry must be an object");
    JSValue pos=JS_GetPropertyStr(ctx,argv[0],"positions"), colors=JS_UNDEFINED,indices=JS_UNDEFINED;
    AthenaJSArray p={.backing=JS_UNDEFINED},c={.backing=JS_UNDEFINED},idx={.backing=JS_UNDEFINED};
    JSValue result=JS_EXCEPTION;
    if(JS_IsException(pos)) goto done;
    colors=JS_GetPropertyStr(ctx,argv[0],"colors"); if(JS_IsException(colors)) goto done;
    indices=JS_GetPropertyStr(ctx,argv[0],"indices"); if(JS_IsException(indices)) goto done;
    if(!athena_js_array(ctx,pos,JS_TYPED_ARRAY_FLOAT32,&p,"positions")) goto done;
    if(!JS_IsUndefined(colors)&&!athena_js_array(ctx,colors,JS_TYPED_ARRAY_FLOAT32,&c,"colors")) goto done;
    if(!JS_IsUndefined(indices)&&!athena_js_array(ctx,indices,JS_TYPED_ARRAY_UINT32,&idx,"indices")) goto done;
    if(p.count%3||p.count/3>ATHENA_MODEL3D_MAX_VERTICES||c.count%4||c.count/4>ATHENA_MODEL3D_MAX_VERTICES||
        idx.count>ATHENA_MODEL3D_MAX_VERTICES) { result=JS_ThrowRangeError(ctx,"Invalid geometry stream lengths"); goto done; }
    AthenaGeometry3D g={p.data,p.count/3,c.data,c.count/4,idx.data,idx.count};
    AthenaMesh3D *mesh=NULL; int code=athena_mesh3d_create(&g,&mesh);
    result=code==ATHENA_MODEL3D_EINVAL?throw_geometry(ctx,&g):code<0?throw_result(ctx,code):wrap_mesh(ctx,mesh);
done:
    JS_FreeValue(ctx,p.backing); JS_FreeValue(ctx,c.backing); JS_FreeValue(ctx,idx.backing);
    JS_FreeValue(ctx,pos); JS_FreeValue(ctx,colors); JS_FreeValue(ctx,indices); return result;
}
static JSValue load(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,1,"Model3D.load")||!JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx,"Model3D.load expects a path string");
    size_t length; const char *path=JS_ToCStringLen(ctx,&length,argv[0]); if(!path) return JS_EXCEPTION;
    if(!length||strlen(path)!=length) { JS_FreeCString(ctx,path); return JS_ThrowRangeError(ctx,"Path must be nonempty without NUL"); }
    AthenaMesh3D *m=NULL; int code=athena_mesh3d_load(path,&m); JS_FreeCString(ctx,path);
    return code<0?throw_result(ctx,code):wrap_mesh(ctx,m);
}
static JSValue create_instance(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Mesh.createInstance")) return JS_EXCEPTION;
    AthenaMesh3D *m=get_mesh(ctx,self); if(!m) return JS_EXCEPTION;
    AthenaInstance3D *i=athena_instance3d_create(m); if(!i) return JS_ThrowOutOfMemory(ctx);
    JSValue obj=JS_NewObjectClass(ctx,instance_id);
    if(JS_IsException(obj)) { athena_instance3d_release(i); return obj; }
    JS_SetOpaque(obj,i); return obj;
}
static JSValue mesh_dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv; if(!athena_js_argc(ctx,argc,0,0,"Mesh.dispose")||!athena_js_class(ctx,self,mesh_id)) return JS_EXCEPTION;
    AthenaMesh3D *m=JS_GetOpaque(self,mesh_id); JS_SetOpaque(self,NULL); athena_mesh3d_release(m); return JS_UNDEFINED;
}
static JSValue instance_dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv; if(!athena_js_argc(ctx,argc,0,0,"Instance.dispose")||!athena_js_class(ctx,self,instance_id)) return JS_EXCEPTION;
    AthenaInstance3D *i=JS_GetOpaque(self,instance_id); JS_SetOpaque(self,NULL); athena_instance3d_release(i); return JS_UNDEFINED;
}
static JSValue vertex_count(JSContext *ctx,JSValueConst self) {
    AthenaMesh3D *m=get_mesh(ctx,self); if(!m) return JS_EXCEPTION;
    AthenaMesh3DView view; athena_mesh3d_view(m,&view); return JS_NewUint32(ctx,view.vertex_count);
}
static JSValue set_vector(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic) {
    int n=magic==3?4:3;
    if(!athena_js_argc(ctx,argc,n,n,"Instance transform setter")) return JS_EXCEPTION;
    float v[4];
    for(int j=0;j<n;j++) if(!athena_js_float(ctx,argv[j],&v[j],"transform")) return JS_EXCEPTION;
    AthenaInstance3D *i=athena_instance3d_from_value(ctx,self); if(!i) return JS_EXCEPTION;
    int ok=magic==0?athena_instance3d_set_position(i,v[0],v[1],v[2]):
        magic==1?athena_instance3d_set_scale(i,v[0],v[1],v[2]):
        magic==2?athena_instance3d_set_euler(i,v[0],v[1],v[2]):
        athena_instance3d_set_rotation(i,v[0],v[1],v[2],v[3]);
    if(!ok) return JS_ThrowRangeError(ctx,"Invalid transform");
    return JS_DupValue(ctx,self);
}
static JSValue get_matrix(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,0,1,"Instance.getTransform")) return JS_EXCEPTION;
    AthenaInstance3D *i=athena_instance3d_from_value(ctx,self); if(!i) return JS_EXCEPTION;
    const AthenaMatrix4 *m=athena_instance3d_transform(i); if(!m) return JS_ThrowRangeError(ctx,"Transform overflow");
    if(argc) {
        AthenaMatrix4 *out=athena_matrix4_from_value(ctx,argv[0]); if(!out) return JS_EXCEPTION;
        *out=*m; return JS_DupValue(ctx,argv[0]);
    }
    return athena_matrix4_to_value(ctx,m);
}
static JSValue forbidden_ctor(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx,"Use Mesh.fromGeometry/Model3D.load and mesh.createInstance");
}
static JSClassDef mesh_class={"Model3D.Mesh",.finalizer=mesh_finalizer};
static JSClassDef instance_class={"Model3D.Instance",.finalizer=instance_finalizer};
static const JSCFunctionListEntry mesh_methods[]={
    JS_CFUNC_DEF("createInstance",0,create_instance),JS_CFUNC_DEF("dispose",0,mesh_dispose),
    JS_CGETSET_DEF("vertexCount",vertex_count,NULL)};
static const JSCFunctionListEntry instance_methods[]={
    JS_CFUNC_MAGIC_DEF("setPosition",3,set_vector,0),JS_CFUNC_MAGIC_DEF("setScale",3,set_vector,1),
    JS_CFUNC_MAGIC_DEF("setRotationEuler",3,set_vector,2),JS_CFUNC_MAGIC_DEF("setRotationQuaternion",4,set_vector,3),
    JS_CFUNC_DEF("getTransform",0,get_matrix),JS_CFUNC_DEF("dispose",0,instance_dispose)};
static const JSCFunctionListEntry exports[]={JS_CFUNC_DEF("load",1,load),
    JS_PROP_INT32_DEF("MAX_VERTICES",ATHENA_MODEL3D_MAX_VERTICES,JS_PROP_ENUMERABLE)};
static int init(JSContext *ctx,JSModuleDef *m) {
    if(athena_register_class(ctx,&mesh_id,&mesh_class)<0||athena_register_class(ctx,&instance_id,&instance_class)<0) return -1;
    JSValue proto=JS_NewObject(ctx), ip=JS_NewObject(ctx);
    if(JS_IsException(proto)||JS_IsException(ip)) { JS_FreeValue(ctx,proto); JS_FreeValue(ctx,ip); return -1; }
    JS_SetPropertyFunctionList(ctx,proto,mesh_methods,countof(mesh_methods));
    JS_SetPropertyFunctionList(ctx,ip,instance_methods,countof(instance_methods));
    JSValue cls=JS_NewCFunction2(ctx,forbidden_ctor,"Mesh",0,JS_CFUNC_constructor,0),
        ic=JS_NewCFunction2(ctx,forbidden_ctor,"Instance",0,JS_CFUNC_constructor,0);
    JS_SetPropertyStr(ctx,cls,"fromGeometry",JS_NewCFunction(ctx,from_geometry,"fromGeometry",1));
    JS_SetConstructor(ctx,cls,proto); JS_SetClassProto(ctx,mesh_id,proto);
    JS_SetConstructor(ctx,ic,ip); JS_SetClassProto(ctx,instance_id,ip);
    if(JS_SetModuleExport(ctx,m,"Mesh",cls)<0) { JS_FreeValue(ctx,ic); return -1; }
    if(JS_SetModuleExport(ctx,m,"Instance",ic)<0) return -1;
    return JS_SetModuleExportList(ctx,m,exports,countof(exports));
}
JSModuleDef *athena_model3d_js_init(JSContext *ctx) {
    JSModuleDef *m=athena_push_module(ctx,init,exports,countof(exports),"Model3D");
    if(m) { JS_AddModuleExport(ctx,m,"Mesh"); JS_AddModuleExport(ctx,m,"Instance"); } return m;
}
