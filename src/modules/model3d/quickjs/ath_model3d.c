#include <stdio.h>
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
JSValue athena_mesh3d_js_wrap(JSContext *ctx,AthenaMesh3D *m) {
    if(!m) return JS_ThrowTypeError(ctx,"Expected a native mesh");
    if(!mesh_id) { athena_mesh3d_release(m); return JS_ThrowInternalError(ctx,"Model3D is not initialized"); }
    return wrap_mesh(ctx,m);
}
/* path: the file being loaded, or NULL. The loader's detail names the
 * exact cause (e.g. "material 'Glass': alphaMode BLEND is not supported"). */
static JSValue throw_load(JSContext *ctx,int result,const char *path) {
    if(result==ATHENA_MODEL3D_ENOMEM) return JS_ThrowOutOfMemory(ctx);
    const char *detail=athena_model3d_detail();
    char where[192]="";
    if(path) snprintf(where,sizeof(where)," '%s'",path);
    if(result==ATHENA_MODEL3D_EINVAL)
        return JS_ThrowRangeError(ctx,"%s%s%s%s",athena_model3d_error(result),where,detail[0]?": ":"",detail);
    return JS_ThrowInternalError(ctx,"%s%s%s%s (code %d)",athena_model3d_error(result),where,detail[0]?": ":"",detail,result);
}
static JSValue throw_result(JSContext *ctx,int result) { return throw_load(ctx,result,NULL); }
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
            return JS_ThrowRangeError(ctx,"texcoords[%u] must be finite and within [-%g, %g]",offset,
                (double)ATHENA_MODEL3D_UV_LIMIT,(double)ATHENA_MODEL3D_UV_LIMIT);
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
    JSValue shading=JS_GetPropertyStr(ctx,value,"shading"),color=JS_UNDEFINED,texture=JS_UNDEFINED,cutoff=JS_UNDEFINED;
    int ok=0;
    if(JS_IsException(shading)) goto done;
    color=JS_GetPropertyStr(ctx,value,"baseColor"); if(JS_IsException(color)) goto done;
    texture=JS_GetPropertyStr(ctx,value,"texture"); if(JS_IsException(texture)) goto done;
    cutoff=JS_GetPropertyStr(ctx,value,"alphaCutoff"); if(JS_IsException(cutoff)) goto done;
    if(!JS_IsUndefined(cutoff)) {
        float c; if(!athena_js_float(ctx,cutoff,&c,"alphaCutoff")) goto done;
        if(!(c>=0&&c<=1)) { JS_ThrowRangeError(ctx,"alphaCutoff must be in [0,1]"); goto done; }
        out->alpha_mask=1; out->alpha_cutoff=c;
    }
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
    JS_FreeValue(ctx,shading); JS_FreeValue(ctx,color); JS_FreeValue(ctx,texture); JS_FreeValue(ctx,cutoff); return ok;
}
static JSValue from_geometry(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,1,"Mesh.fromGeometry")) return JS_EXCEPTION;
    if(!JS_IsObject(argv[0])) return JS_ThrowTypeError(ctx,"Geometry must be an object");
    enum { POS,COLOR,INDEX,NORMAL,UV,JOINT,WEIGHT,TARGET_POS,TARGET_NORMAL,STREAMS };
    static const char *names[STREAMS]={"positions","colors","indices","normals","texcoords",
        "joints","weights","targetPositions","targetNormals"};
    JSValue values[STREAMS],material=JS_UNDEFINED,result=JS_EXCEPTION;
    AthenaJSArray arrays[STREAMS];
    for(int i=0;i<STREAMS;i++) { values[i]=JS_UNDEFINED; arrays[i]=(AthenaJSArray){.backing=JS_UNDEFINED}; }
    AthenaMaterial3D descriptor; athena_material3d_default(&descriptor);
    /* Every getter runs before any borrowed TypedArray pointer is obtained. */
    for(int i=0;i<STREAMS;i++) {
        values[i]=JS_GetPropertyStr(ctx,argv[0],names[i]);
        if(JS_IsException(values[i])) goto done;
    }
    material=JS_GetPropertyStr(ctx,argv[0],"material"); if(JS_IsException(material)) goto done;
    if(!material_option(ctx,material,&descriptor)) goto done;
    for(int i=0;i<STREAMS;i++) {
        int type=i==INDEX?JS_TYPED_ARRAY_UINT32:i==JOINT?JS_TYPED_ARRAY_UINT16:JS_TYPED_ARRAY_FLOAT32;
        if((i==POS||!JS_IsUndefined(values[i]))&&!athena_js_array(ctx,values[i],type,&arrays[i],names[i])) goto done;
    }
    size_t positions=arrays[POS].count,vertices=positions/3;
    if(positions%3||vertices>ATHENA_MODEL3D_MAX_VERTICES||arrays[COLOR].count%4||
        arrays[COLOR].count/4>ATHENA_MODEL3D_MAX_VERTICES||arrays[INDEX].count>ATHENA_MODEL3D_MAX_VERTICES||
        arrays[NORMAL].count%3||arrays[NORMAL].count/3>ATHENA_MODEL3D_MAX_VERTICES||
        arrays[UV].count%2||arrays[UV].count/2>ATHENA_MODEL3D_MAX_VERTICES) {
        result=JS_ThrowRangeError(ctx,"Invalid geometry stream lengths"); goto done;
    }
    int skin=!JS_IsUndefined(values[JOINT])||!JS_IsUndefined(values[WEIGHT]);
    if(skin&&(JS_IsUndefined(values[JOINT])||JS_IsUndefined(values[WEIGHT])||
        arrays[JOINT].count!=vertices*4||arrays[WEIGHT].count!=vertices*4)) {
        result=JS_ThrowRangeError(ctx,"joints and weights need four values per source vertex, together"); goto done;
    }
    size_t targets=arrays[TARGET_POS].count;
    if((!JS_IsUndefined(values[TARGET_POS])&&(!positions||!targets||targets%positions||
        targets/positions>ATHENA_MODEL3D_MAX_TARGETS))||
        (!JS_IsUndefined(values[TARGET_NORMAL])&&(JS_IsUndefined(values[TARGET_POS])||
        JS_IsUndefined(values[NORMAL])||arrays[TARGET_NORMAL].count!=targets))) {
        result=JS_ThrowRangeError(ctx,"Morph streams require 1..%u complete xyz targets; targetNormals need normals and matching targetPositions",
            (unsigned)ATHENA_MODEL3D_MAX_TARGETS); goto done;
    }
    AthenaGeometry3D g={.positions=arrays[POS].data,.vertex_count=vertices,
        .colors=arrays[COLOR].data,.color_count=arrays[COLOR].count/4,
        .indices=arrays[INDEX].data,.index_count=arrays[INDEX].count,
        .normals=arrays[NORMAL].data,.normal_count=arrays[NORMAL].count/3,.material=&descriptor,
        .texcoords=arrays[UV].data,.texcoord_count=arrays[UV].count/2,
        .joints=arrays[JOINT].data,.weights=arrays[WEIGHT].data,.skin_count=skin?vertices:0,
        .target_positions=arrays[TARGET_POS].data,.target_normals=arrays[TARGET_NORMAL].data,
        .target_count=positions?targets/positions:0};
    athena_model3d_clear_detail();
    AthenaMesh3D *mesh=NULL; int code=athena_mesh3d_create(&g,&mesh);
    result=code==ATHENA_MODEL3D_EINVAL?throw_geometry(ctx,&g):code<0?throw_result(ctx,code):wrap_mesh(ctx,mesh);
done:
    for(int i=0;i<STREAMS;i++) { JS_FreeValue(ctx,arrays[i].backing); JS_FreeValue(ctx,values[i]); }
    JS_FreeValue(ctx,material);
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
    AthenaMesh3D *m=NULL; int code=athena_mesh3d_load_with_material(path,&descriptor,&m);
    athena_texture3d_release(descriptor.texture);
    JSValue result=code<0?throw_load(ctx,code,path):wrap_mesh(ctx,m);
    JS_FreeCString(ctx,path); return result;
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
    JSValue w=JS_GetPropertyStr(ctx,argv[0],"width"),h=JS_UNDEFINED,f=JS_UNDEFINED,p=JS_UNDEFINED,r=JS_UNDEFINED;
    AthenaJSArray a={.backing=JS_UNDEFINED}; JSValue result=JS_EXCEPTION;
    if(JS_IsException(w)) goto done;
    h=JS_GetPropertyStr(ctx,argv[0],"height"); if(JS_IsException(h)) goto done;
    f=JS_GetPropertyStr(ctx,argv[0],"filter"); if(JS_IsException(f)) goto done;
    p=JS_GetPropertyStr(ctx,argv[0],"pixels"); if(JS_IsException(p)) goto done;
    r=JS_GetPropertyStr(ctx,argv[0],"wrap"); if(JS_IsException(r)) goto done;
    float width,height,filter=0,wrap=0;
    if(!athena_js_float(ctx,w,&width,"width")||!athena_js_float(ctx,h,&height,"height")||
        (!JS_IsUndefined(f)&&!athena_js_float(ctx,f,&filter,"filter"))||
        (!JS_IsUndefined(r)&&!athena_js_float(ctx,r,&wrap,"wrap"))) goto done;
    if(width<1||height<1||width>ATHENA_TEXTURE3D_MAX_SIZE||height>ATHENA_TEXTURE3D_MAX_SIZE||
        width!=(uint32_t)width||height!=(uint32_t)height||(filter!=0&&filter!=1)||(wrap!=0&&wrap!=1&&wrap!=2&&wrap!=3)) {
        result=JS_ThrowRangeError(ctx,"Invalid texture dimensions, filter or wrap"); goto done;
    }
    if(!athena_js_array(ctx,p,JS_TYPED_ARRAY_UINT32,&a,"pixels")) goto done;
    if(a.count!=(uint32_t)width*(uint32_t)height) { result=JS_ThrowRangeError(ctx,"pixels length must equal width * height"); goto done; }
    AthenaTexture3DPixels pixels={.width=width,.height=height,.filter=filter,.pixels=a.data,.pixel_count=a.count,
        .wrap=(AthenaTexture3DWrap)wrap};
    athena_model3d_clear_detail();
    AthenaTexture3D *texture=NULL; int code=athena_texture3d_create(&pixels,&texture);
    result=code<0?throw_result(ctx,code):wrap_texture(ctx,texture);
done:
    JS_FreeValue(ctx,a.backing); JS_FreeValue(ctx,w); JS_FreeValue(ctx,h); JS_FreeValue(ctx,f); JS_FreeValue(ctx,p);
    JS_FreeValue(ctx,r); return result;
}
static JSValue texture_load(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;
    if(!athena_js_argc(ctx,argc,1,3,"Texture.load")||!JS_IsString(argv[0])) return JS_ThrowTypeError(ctx,"Texture.load expects a path string");
    float filter=0,wrap=0;
    if(argc>=2&&!JS_IsUndefined(argv[1])&&!athena_js_float(ctx,argv[1],&filter,"filter")) return JS_EXCEPTION;
    if(argc==3&&!JS_IsUndefined(argv[2])&&!athena_js_float(ctx,argv[2],&wrap,"wrap")) return JS_EXCEPTION;
    if(filter!=0&&filter!=1) return JS_ThrowRangeError(ctx,"Invalid texture filter");
    if(wrap!=0&&wrap!=1&&wrap!=2&&wrap!=3) return JS_ThrowRangeError(ctx,"Invalid texture wrap");
    size_t length; const char *path=JS_ToCStringLen(ctx,&length,argv[0]); if(!path) return JS_EXCEPTION;
    if(!length||length!=strlen(path)) { JS_FreeCString(ctx,path); return JS_ThrowRangeError(ctx,"Invalid texture path"); }
    athena_model3d_clear_detail();
    AthenaTexture3D *t=NULL; int code=athena_texture3d_load_ex(path,filter,(AthenaTexture3DWrap)wrap,&t);
    JSValue result=code<0?throw_load(ctx,code,path):wrap_texture(ctx,t);
    JS_FreeCString(ctx,path); return result;
}
static JSValue texture_dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv; if(!athena_js_argc(ctx,argc,0,0,"Texture.dispose")||!athena_js_class(ctx,self,texture_id)) return JS_EXCEPTION;
    AthenaTexture3D *t=JS_GetOpaque(self,texture_id); JS_SetOpaque(self,NULL); athena_texture3d_release(t); return JS_UNDEFINED;
}
static JSValue texture_dimension(JSContext *ctx,JSValueConst self,int magic) {
    AthenaTexture3D *t=get_texture(ctx,self); if(!t) return JS_EXCEPTION;
    AthenaTexture3DPixels p; athena_texture3d_view(t,&p);
    return JS_NewUint32(ctx,magic==2?(uint32_t)p.wrap:magic?p.height:p.width);
}
static JSValue texture_upload(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv; if(!athena_js_argc(ctx,argc,0,0,"Texture.upload")) return JS_EXCEPTION;
    AthenaTexture3D *t=get_texture(ctx,self); if(!t) return JS_EXCEPTION;
    athena_model3d_clear_detail();
    int code=athena_texture3d_upload(t);
    return code<0?throw_result(ctx,code):JS_DupValue(ctx,self);
}
/* Fills out (a Float32Array of at least count floats) or returns a new Array. */
static JSValue floats_out(JSContext *ctx,int argc,JSValueConst *argv,const float *values,uint32_t count,const char *name) {
    if(argc&&!JS_IsUndefined(argv[0])) {
        AthenaJSArray a;
        if(!athena_js_array(ctx,argv[0],JS_TYPED_ARRAY_FLOAT32,&a,name)) return JS_EXCEPTION;
        if(a.count<count) { JS_FreeValue(ctx,a.backing); return JS_ThrowRangeError(ctx,"%s needs %u floats",name,(unsigned)count); }
        memcpy(a.data,values,count*sizeof(float)); JS_FreeValue(ctx,a.backing);
        return JS_DupValue(ctx,argv[0]);
    }
    JSValue array=JS_NewArray(ctx); if(JS_IsException(array)) return array;
    for(uint32_t i=0;i<count;i++)
        if(JS_SetPropertyUint32(ctx,array,i,JS_NewFloat64(ctx,values[i]))<0) { JS_FreeValue(ctx,array); return JS_EXCEPTION; }
    return array;
}
/* magic 0 position (xyz), 1 rotation (xyzw), 2 scale (xyz): as set. */
static JSValue get_trs(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv,int magic) {
    if(!athena_js_argc(ctx,argc,0,1,"Instance TRS getter")) return JS_EXCEPTION;
    AthenaInstance3D *i=athena_instance3d_from_value(ctx,self); if(!i) return JS_EXCEPTION;
    float p[3],sc[3]; AthenaQuaternion q; athena_instance3d_get_trs(i,p,&q,sc);
    const float r[4]={q.x,q.y,q.z,q.w};
    return floats_out(ctx,argc,argv,magic==0?p:magic==1?r:sc,magic==1?4:3,"out");
}
/* minX, minY, minZ, maxX, maxY, maxZ in model space. */
static JSValue mesh_bounds(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,0,1,"Mesh.getBounds")) return JS_EXCEPTION;
    AthenaMesh3D *m=get_mesh(ctx,self); if(!m) return JS_EXCEPTION;
    AthenaMesh3DView v; athena_mesh3d_view(m,&v);
    const float b[6]={v.minimum[0],v.minimum[1],v.minimum[2],v.maximum[0],v.maximum[1],v.maximum[2]};
    return floats_out(ctx,argc,argv,b,6,"out");
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
    JS_CFUNC_DEF("upload",0,texture_upload),
    JS_CGETSET_MAGIC_DEF("width",texture_dimension,NULL,0),JS_CGETSET_MAGIC_DEF("height",texture_dimension,NULL,1),
    JS_CGETSET_MAGIC_DEF("wrap",texture_dimension,NULL,2)};
static const JSCFunctionListEntry mesh_methods[]={
    JS_CFUNC_DEF("createInstance",0,create_instance),JS_CFUNC_DEF("dispose",0,mesh_dispose),
    JS_CFUNC_DEF("getBounds",0,mesh_bounds),
    JS_CGETSET_DEF("vertexCount",vertex_count,NULL)};
static const JSCFunctionListEntry instance_methods[]={
    JS_CFUNC_MAGIC_DEF("setPosition",3,set_vector,0),JS_CFUNC_MAGIC_DEF("setScale",3,set_vector,1),
    JS_CFUNC_MAGIC_DEF("setRotationEuler",3,set_vector,2),JS_CFUNC_MAGIC_DEF("setRotationQuaternion",4,set_vector,3),
    JS_CFUNC_DEF("getTransform",0,get_matrix),JS_CFUNC_DEF("dispose",0,instance_dispose),
    JS_CFUNC_MAGIC_DEF("getPosition",0,get_trs,0),JS_CFUNC_MAGIC_DEF("getRotation",0,get_trs,1),
    JS_CFUNC_MAGIC_DEF("getScale",0,get_trs,2)};
static const JSCFunctionListEntry exports[]={JS_CFUNC_DEF("load",1,load),
    JS_CFUNC_MAGIC_DEF("setPositions",2,set_many,0),JS_CFUNC_MAGIC_DEF("setRotationsEuler",2,set_many,1),
    JS_PROP_INT32_DEF("MAX_VERTICES",ATHENA_MODEL3D_MAX_VERTICES,JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MAX_JOINTS",ATHENA_MODEL3D_MAX_JOINTS,JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MAX_TARGETS",ATHENA_MODEL3D_MAX_TARGETS,JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("UV_LIMIT",(int32_t)ATHENA_MODEL3D_UV_LIMIT,JS_PROP_ENUMERABLE),
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
    JS_SetPropertyStr(ctx,tc,"CLAMP",JS_NewInt32(ctx,ATHENA_TEXTURE3D_CLAMP));
    JS_SetPropertyStr(ctx,tc,"REPEAT",JS_NewInt32(ctx,ATHENA_TEXTURE3D_REPEAT));
    JS_SetPropertyStr(ctx,tc,"REPEAT_U",JS_NewInt32(ctx,ATHENA_TEXTURE3D_REPEAT_U));
    JS_SetPropertyStr(ctx,tc,"REPEAT_V",JS_NewInt32(ctx,ATHENA_TEXTURE3D_REPEAT_V));
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
