#include <athena_js_args.h>
#include <athena/js/model3d.h>
#include <athena/js/matrix4.h>
#include "ath_model3d.h"
static JSClassID mesh_id,instance_id,texture_id;
static AthenaTexture3D *get_texture(JSContext *ctx,JSValueConst value) {
    AthenaTexture3D *t=JS_GetOpaque2(ctx,value,texture_id);
    if(!t) JS_ThrowTypeError(ctx,"Expected a live Model3D.Texture");
    return t;
}
static AthenaMesh3D *get_mesh(JSContext *ctx,JSValueConst v) {
    AthenaMesh3D *m=JS_GetOpaque2(ctx,v,mesh_id);
    if(!m) JS_ThrowTypeError(ctx,"Expected a live Model3D.Mesh");
    return m;
}
AthenaMesh3D *athena_mesh3d_from_value(JSContext *ctx,JSValueConst v) { return get_mesh(ctx,v); }
AthenaInstance3D *athena_instance3d_from_value(JSContext *ctx,JSValueConst v) {
    AthenaInstance3D *i=JS_GetOpaque2(ctx,v,instance_id);
    if(!i) JS_ThrowTypeError(ctx,"Expected a live Model3D.Instance");
    return i;
}
static void mesh_finalizer(JSRuntime *rt,JSValue v) { (void)rt; athena_mesh3d_release(JS_GetOpaque(v,mesh_id)); }
static void instance_finalizer(JSRuntime *rt,JSValue v) { (void)rt; athena_instance3d_release(JS_GetOpaque(v,instance_id)); }
static void texture_finalizer(JSRuntime *rt,JSValue v) { (void)rt; athena_texture3d_release(JS_GetOpaque(v,texture_id)); }
static JSValue wrap_texture(JSContext *ctx,AthenaTexture3D *t) {
    JSValue obj=JS_NewObjectClass(ctx,texture_id);
    if(JS_IsException(obj)) { athena_texture3d_release(t); return obj; }
    JS_SetOpaque(obj,t); return obj;
}
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
        case ATHENA_GEOMETRY3D_NORMAL:
            return JS_ThrowRangeError(ctx,"Invalid normal or degenerate diffuse triangle at offset %u",offset);
        case ATHENA_GEOMETRY3D_NORMAL_COUNT:
            return JS_ThrowRangeError(ctx,"normals must contain one xyz vector per source vertex");
        case ATHENA_GEOMETRY3D_MATERIAL:
            return JS_ThrowRangeError(ctx,"Invalid material shading or baseColor");
        case ATHENA_GEOMETRY3D_TEXCOORD_COUNT:
            return JS_ThrowRangeError(ctx,"texcoords require one uv pair per source vertex; textured materials require UVs");
        case ATHENA_GEOMETRY3D_TEXCOORD:
            return JS_ThrowRangeError(ctx,"texcoords[%u] must be finite and in [0,1]",offset);
        case ATHENA_GEOMETRY3D_SKIN_COUNT:
            return JS_ThrowRangeError(ctx,"joints and weights need four values per source vertex, together");
        case ATHENA_GEOMETRY3D_SKIN:
            return JS_ThrowRangeError(ctx,"Invalid skin at offset %u: joints below %u, weights finite, >= 0 and not all zero",
                offset,(unsigned)ATHENA_MODEL3D_MAX_JOINTS);
        case ATHENA_GEOMETRY3D_TARGET_COUNT:
            return JS_ThrowRangeError(ctx,"At most %u morph targets, with position deltas; normal deltas need normals",
                (unsigned)ATHENA_MODEL3D_MAX_TARGETS);
        case ATHENA_GEOMETRY3D_TARGET:
            return JS_ThrowRangeError(ctx,"Morph target delta %u must be finite",offset);
        default:
            return JS_ThrowRangeError(ctx,"Invalid geometry counts (issue %d, vertices %u, colors %u, indices %u)",
                issue,g->vertex_count,g->color_count,g->index_count);
    }
}
/* Complete every descriptor getter before pinning geometry buffers. A getter
 * can detach a TypedArray, dispose another handle, or throw. Native material
 * data is copied and does not depend on a JS object's lifetime. */
static int material_option(JSContext *ctx,JSValueConst value,AthenaMaterial3D *out) {
    athena_material3d_default(out);
    if(JS_IsUndefined(value)) return 1;
    if(!JS_IsObject(value)) { JS_ThrowTypeError(ctx,"material must be a descriptor"); return 0; }
    JSValue shading=JS_GetPropertyStr(ctx,value,"shading"),color=JS_UNDEFINED,texture=JS_UNDEFINED;
    int ok=0;
    if(JS_IsException(shading)) goto done;
    color=JS_GetPropertyStr(ctx,value,"baseColor"); if(JS_IsException(color)) goto done;
    texture=JS_GetPropertyStr(ctx,value,"texture"); if(JS_IsException(texture)) goto done;
    if(!JS_IsUndefined(shading)) {
        float s; if(!athena_js_float(ctx,shading,&s,"shading")) goto done;
        if(s!=0&&s!=1) { JS_ThrowRangeError(ctx,"Invalid material shading"); goto done; }
        out->shading=(int)s;
    }
    if(!JS_IsUndefined(color)) {
        AthenaJSArray array;
        if(!athena_js_array(ctx,color,JS_TYPED_ARRAY_FLOAT32,&array,"baseColor")) goto done;
        if(array.count==4) memcpy(out->base_color,array.data,sizeof(out->base_color));
        else JS_ThrowRangeError(ctx,"baseColor requires four components");
        JS_FreeValue(ctx,array.backing); if(array.count!=4) goto done;
    }
    if(!athena_material3d_validate(out)) { JS_ThrowRangeError(ctx,"baseColor must be finite RGBA in [0,1]"); goto done; }
    if(!JS_IsUndefined(texture)) { out->texture=get_texture(ctx,texture); if(!out->texture) goto done; }
    /* Hold a temporary native reference until the geometry/path is consumed. */
    athena_texture3d_retain(out->texture);
    ok=1;
done:
    JS_FreeValue(ctx,shading); JS_FreeValue(ctx,color); JS_FreeValue(ctx,texture); return ok;
}
static JSValue from_geometry(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,1,"Mesh.fromGeometry")) return JS_EXCEPTION;
    if(!JS_IsObject(argv[0])) return JS_ThrowTypeError(ctx,"Geometry must be an object");
    JSValue pos=JS_GetPropertyStr(ctx,argv[0],"positions"), colors=JS_UNDEFINED,indices=JS_UNDEFINED,
        normals=JS_UNDEFINED,material=JS_UNDEFINED,texcoords=JS_UNDEFINED;
    AthenaJSArray p={.backing=JS_UNDEFINED},c={.backing=JS_UNDEFINED},idx={.backing=JS_UNDEFINED},n={.backing=JS_UNDEFINED},uv={.backing=JS_UNDEFINED};
    AthenaMaterial3D descriptor; athena_material3d_default(&descriptor);
    JSValue result=JS_EXCEPTION;
    if(JS_IsException(pos)) goto done;
    colors=JS_GetPropertyStr(ctx,argv[0],"colors"); if(JS_IsException(colors)) goto done;
    indices=JS_GetPropertyStr(ctx,argv[0],"indices"); if(JS_IsException(indices)) goto done;
    normals=JS_GetPropertyStr(ctx,argv[0],"normals"); if(JS_IsException(normals)) goto done;
    texcoords=JS_GetPropertyStr(ctx,argv[0],"texcoords"); if(JS_IsException(texcoords)) goto done;
    material=JS_GetPropertyStr(ctx,argv[0],"material"); if(JS_IsException(material)) goto done;
    if(!material_option(ctx,material,&descriptor)) goto done;
    if(!athena_js_array(ctx,pos,JS_TYPED_ARRAY_FLOAT32,&p,"positions")) goto done;
    if(!JS_IsUndefined(colors)&&!athena_js_array(ctx,colors,JS_TYPED_ARRAY_FLOAT32,&c,"colors")) goto done;
    if(!JS_IsUndefined(indices)&&!athena_js_array(ctx,indices,JS_TYPED_ARRAY_UINT32,&idx,"indices")) goto done;
    if(!JS_IsUndefined(normals)&&!athena_js_array(ctx,normals,JS_TYPED_ARRAY_FLOAT32,&n,"normals")) goto done;
    if(!JS_IsUndefined(texcoords)&&!athena_js_array(ctx,texcoords,JS_TYPED_ARRAY_FLOAT32,&uv,"texcoords")) goto done;
    if(p.count%3||p.count/3>ATHENA_MODEL3D_MAX_VERTICES||c.count%4||c.count/4>ATHENA_MODEL3D_MAX_VERTICES||
        idx.count>ATHENA_MODEL3D_MAX_VERTICES||n.count%3||n.count/3>ATHENA_MODEL3D_MAX_VERTICES||uv.count%2||uv.count/2>ATHENA_MODEL3D_MAX_VERTICES) {
        result=JS_ThrowRangeError(ctx,"Invalid geometry stream lengths"); goto done;
    }
    AthenaGeometry3D g={.positions=p.data,.vertex_count=p.count/3,.colors=c.data,.color_count=c.count/4,
        .indices=idx.data,.index_count=idx.count,.normals=n.data,.normal_count=n.count/3,.material=&descriptor,
        .texcoords=uv.data,.texcoord_count=uv.count/2};
    AthenaMesh3D *mesh=NULL; int code=athena_mesh3d_create(&g,&mesh);
    result=code==ATHENA_MODEL3D_EINVAL?throw_geometry(ctx,&g):code<0?throw_result(ctx,code):wrap_mesh(ctx,mesh);
done:
    JS_FreeValue(ctx,p.backing); JS_FreeValue(ctx,c.backing); JS_FreeValue(ctx,idx.backing); JS_FreeValue(ctx,n.backing);
    JS_FreeValue(ctx,pos); JS_FreeValue(ctx,colors); JS_FreeValue(ctx,indices);
    JS_FreeValue(ctx,normals); JS_FreeValue(ctx,material); JS_FreeValue(ctx,texcoords); JS_FreeValue(ctx,uv.backing);
    athena_texture3d_release(descriptor.texture); return result;
}
int athena_model3d_js_material(JSContext *ctx,JSValueConst value,AthenaMaterial3D *out) {
    return material_option(ctx,value,out);
}
static JSValue load(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,2,"Model3D.load")||!JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx,"Model3D.load expects a path string");
    AthenaMaterial3D descriptor;
    if(!material_option(ctx,argc==2?argv[1]:JS_UNDEFINED,&descriptor)) return JS_EXCEPTION;
    size_t length; const char *path=JS_ToCStringLen(ctx,&length,argv[0]);
    if(!path) { athena_texture3d_release(descriptor.texture); return JS_EXCEPTION; }
    if(!length||strlen(path)!=length) { JS_FreeCString(ctx,path); athena_texture3d_release(descriptor.texture); return JS_ThrowRangeError(ctx,"Path must be nonempty without NUL"); }
    AthenaMesh3D *m=NULL; int code=athena_mesh3d_load_with_material(path,&descriptor,&m); JS_FreeCString(ctx,path);
    athena_texture3d_release(descriptor.texture);
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
static JSValue texture_pixels(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,1,"Texture.fromPixels")||!JS_IsObject(argv[0]))
        return JS_ThrowTypeError(ctx,"Texture.fromPixels expects a descriptor");
    JSValue w=JS_GetPropertyStr(ctx,argv[0],"width"),h=JS_UNDEFINED,f=JS_UNDEFINED,p=JS_UNDEFINED;
    AthenaJSArray a={.backing=JS_UNDEFINED}; JSValue result=JS_EXCEPTION;
    if(JS_IsException(w)) goto done;
    h=JS_GetPropertyStr(ctx,argv[0],"height"); if(JS_IsException(h)) goto done;
    f=JS_GetPropertyStr(ctx,argv[0],"filter"); if(JS_IsException(f)) goto done;
    p=JS_GetPropertyStr(ctx,argv[0],"pixels"); if(JS_IsException(p)) goto done;
    float width,height,filter=0;
    if(!athena_js_float(ctx,w,&width,"width")||!athena_js_float(ctx,h,&height,"height")||
        (!JS_IsUndefined(f)&&!athena_js_float(ctx,f,&filter,"filter"))) goto done;
    if(width<1||height<1||width>ATHENA_TEXTURE3D_MAX_SIZE||height>ATHENA_TEXTURE3D_MAX_SIZE||
        width!=(uint32_t)width||height!=(uint32_t)height||(filter!=0&&filter!=1)) {
        result=JS_ThrowRangeError(ctx,"Invalid texture dimensions or filter"); goto done;
    }
    if(!athena_js_array(ctx,p,JS_TYPED_ARRAY_UINT32,&a,"pixels")) goto done;
    if(a.count!=(uint32_t)width*(uint32_t)height) { result=JS_ThrowRangeError(ctx,"pixels length must equal width * height"); goto done; }
    AthenaTexture3DPixels pixels={.width=width,.height=height,.filter=filter,.pixels=a.data,.pixel_count=a.count};
    AthenaTexture3D *texture=NULL; int code=athena_texture3d_create(&pixels,&texture);
    result=code<0?throw_result(ctx,code):wrap_texture(ctx,texture);
done:
    JS_FreeValue(ctx,a.backing); JS_FreeValue(ctx,w); JS_FreeValue(ctx,h); JS_FreeValue(ctx,f); JS_FreeValue(ctx,p); return result;
}
static JSValue texture_load(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,2,"Texture.load")||!JS_IsString(argv[0])) return JS_ThrowTypeError(ctx,"Texture.load expects a path string");
    float filter=0;
    if(argc==2&&!athena_js_float(ctx,argv[1],&filter,"filter")) return JS_EXCEPTION;
    if(filter!=0&&filter!=1) return JS_ThrowRangeError(ctx,"Invalid texture filter");
    size_t length; const char *path=JS_ToCStringLen(ctx,&length,argv[0]); if(!path) return JS_EXCEPTION;
    if(!length||length!=strlen(path)) { JS_FreeCString(ctx,path); return JS_ThrowRangeError(ctx,"Invalid texture path"); }
    AthenaTexture3D *t=NULL; int code=athena_texture3d_load(path,filter,&t); JS_FreeCString(ctx,path);
    return code<0?throw_result(ctx,code):wrap_texture(ctx,t);
}
static JSValue texture_dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv; if(!athena_js_argc(ctx,argc,0,0,"Texture.dispose")||!athena_js_class(ctx,self,texture_id)) return JS_EXCEPTION;
    AthenaTexture3D *t=JS_GetOpaque(self,texture_id); JS_SetOpaque(self,NULL); athena_texture3d_release(t); return JS_UNDEFINED;
}
static JSValue texture_dimension(JSContext *ctx,JSValueConst self,int magic) {
    AthenaTexture3D *t=get_texture(ctx,self); if(!t) return JS_EXCEPTION;
    AthenaTexture3DPixels p; athena_texture3d_view(t,&p); return JS_NewUint32(ctx,magic?p.height:p.width);
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
static void *instance_item(JSContext *ctx,JSValueConst v) { return athena_instance3d_from_value(ctx,v); }
static void instance_retain(void *i) { athena_instance3d_retain(i); }
static void instance_release(void *i) { athena_instance3d_release(i); }
/* Model3D.setPositions/setRotationsEuler(instances, values): xyz per instance
 * from a Float32Array, validated before any instance changes. */
static JSValue set_many(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic) {
    (void)self;
    const char *name=magic?"Model3D.setRotationsEuler":"Model3D.setPositions";
    if(!athena_js_argc(ctx,argc,2,2,name)) return JS_EXCEPTION;
    AthenaJSHandles h;
    if(!athena_js_handles(ctx,argv[0],&h,instance_item,instance_retain,instance_release,"instances")) return JS_EXCEPTION;
    AthenaJSArray values;
    if(!athena_js_array(ctx,argv[1],JS_TYPED_ARRAY_FLOAT32,&values,"values")) { athena_js_handles_free(&h); return JS_EXCEPTION; }
    const float *v=values.data; JSValue result=JS_NewUint32(ctx,h.count);
    if(values.count/3<h.count) result=JS_ThrowRangeError(ctx,"values needs 3 floats per instance");
    else for(uint32_t i=0;i<h.count*3;i++) if(!athena_float_isfinite(v[i])) {
        result=JS_ThrowRangeError(ctx,"values[%u] is not a finite float",(unsigned)i); break;
    }
    if(!JS_IsException(result)) for(uint32_t i=0;i<h.count;i++) {
        const float *x=&v[i*3];
        int ok=magic?athena_instance3d_set_euler(h.items[i],x[0],x[1],x[2]):
            athena_instance3d_set_position(h.items[i],x[0],x[1],x[2]);
        if(!ok) { result=JS_ThrowRangeError(ctx,"Invalid transform for instances[%u]",(unsigned)i); break; }
    }
    JS_FreeValue(ctx,values.backing); athena_js_handles_free(&h); return result;
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
static JSClassDef texture_class={"Model3D.Texture",.finalizer=texture_finalizer};
static const JSCFunctionListEntry texture_methods[]={JS_CFUNC_DEF("dispose",0,texture_dispose),
    JS_CGETSET_MAGIC_DEF("width",texture_dimension,NULL,0),JS_CGETSET_MAGIC_DEF("height",texture_dimension,NULL,1)};
static const JSCFunctionListEntry mesh_methods[]={
    JS_CFUNC_DEF("createInstance",0,create_instance),JS_CFUNC_DEF("dispose",0,mesh_dispose),
    JS_CGETSET_DEF("vertexCount",vertex_count,NULL)};
static const JSCFunctionListEntry instance_methods[]={
    JS_CFUNC_MAGIC_DEF("setPosition",3,set_vector,0),JS_CFUNC_MAGIC_DEF("setScale",3,set_vector,1),
    JS_CFUNC_MAGIC_DEF("setRotationEuler",3,set_vector,2),JS_CFUNC_MAGIC_DEF("setRotationQuaternion",4,set_vector,3),
    JS_CFUNC_DEF("getTransform",0,get_matrix),JS_CFUNC_DEF("dispose",0,instance_dispose)};
static const JSCFunctionListEntry exports[]={JS_CFUNC_DEF("load",1,load),
    JS_CFUNC_MAGIC_DEF("setPositions",2,set_many,0),JS_CFUNC_MAGIC_DEF("setRotationsEuler",2,set_many,1),
    JS_PROP_INT32_DEF("MAX_VERTICES",ATHENA_MODEL3D_MAX_VERTICES,JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("UNLIT",ATHENA_MATERIAL3D_UNLIT,JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("DIFFUSE",ATHENA_MATERIAL3D_DIFFUSE,JS_PROP_ENUMERABLE)};
static int init(JSContext *ctx,JSModuleDef *m) {
    if(athena_register_class(ctx,&mesh_id,&mesh_class)<0||athena_register_class(ctx,&instance_id,&instance_class)<0||
        athena_register_class(ctx,&texture_id,&texture_class)<0) return -1;
    JSValue tp=JS_NewObject(ctx),tc=JS_NewCFunction2(ctx,forbidden_ctor,"Texture",0,JS_CFUNC_constructor,0);
    if(JS_IsException(tp)||JS_IsException(tc)) { JS_FreeValue(ctx,tp); JS_FreeValue(ctx,tc); return -1; }
    JS_SetPropertyFunctionList(ctx,tp,texture_methods,countof(texture_methods));
    JS_SetPropertyStr(ctx,tc,"fromPixels",JS_NewCFunction(ctx,texture_pixels,"fromPixels",1));
    JS_SetPropertyStr(ctx,tc,"load",JS_NewCFunction(ctx,texture_load,"load",1));
    JS_SetPropertyStr(ctx,tc,"NEAREST",JS_NewInt32(ctx,0)); JS_SetPropertyStr(ctx,tc,"LINEAR",JS_NewInt32(ctx,1));
    JS_SetConstructor(ctx,tc,tp); JS_SetClassProto(ctx,texture_id,tp);
    if(JS_SetModuleExport(ctx,m,"Texture",tc)<0) return -1;
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
    if(m) { JS_AddModuleExport(ctx,m,"Mesh"); JS_AddModuleExport(ctx,m,"Instance"); JS_AddModuleExport(ctx,m,"Texture"); } return m;
}
