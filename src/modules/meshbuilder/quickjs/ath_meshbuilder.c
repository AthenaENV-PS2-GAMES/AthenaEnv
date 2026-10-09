#include <athena_js_args.h>
#include <athena/meshbuilder.h>
#include <athena/js/model3d.h>
#include <athena/js/matrix4.h>
#include "ath_meshbuilder.h"
static JSClassID builder_id;
static AthenaMeshBuilder *get_builder(JSContext *ctx,JSValueConst self) {
    AthenaMeshBuilder *b=JS_GetOpaque2(ctx,self,builder_id);
    if(!b) JS_ThrowTypeError(ctx,"Expected a live MeshBuilder.Builder");
    return b;
}
static void finalizer(JSRuntime *rt,JSValue v) { (void)rt; athena_meshbuilder_destroy(JS_GetOpaque(v,builder_id)); }
static JSValue ctor(JSContext *ctx,JSValueConst target,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"MeshBuilder.Builder")) return JS_EXCEPTION;
    JSValue proto=JS_GetPropertyStr(ctx,target,"prototype"); if(JS_IsException(proto)) return proto;
    JSValue obj=JS_NewObjectProtoClass(ctx,proto,builder_id); JS_FreeValue(ctx,proto);
    if(JS_IsException(obj)) return obj;
    AthenaMeshBuilder *b=athena_meshbuilder_create();
    if(!b) { JS_FreeValue(ctx,obj); return JS_ThrowOutOfMemory(ctx); }
    JS_SetOpaque(obj,b); return obj;
}
static JSValue dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Builder.dispose")||!athena_js_class(ctx,self,builder_id)) return JS_EXCEPTION;
    AthenaMeshBuilder *b=JS_GetOpaque(self,builder_id); JS_SetOpaque(self,NULL); athena_meshbuilder_destroy(b);
    return JS_UNDEFINED;
}
static int floats(JSContext *ctx,int argc,JSValueConst *argv,int first,int count,float *out,const char *name) {
    for(int i=0;i<count;i++) {
        if(first+i>=argc) { JS_ThrowTypeError(ctx,"%s expects %d numbers",name,count); return 0; }
        if(!athena_js_float(ctx,argv[first+i],&out[i],name)) return 0;
    }
    return 1;
}
/* Optional number at index, default def. */
static int opt_float(JSContext *ctx,int argc,JSValueConst *argv,int index,float def,float *out,const char *name) {
    *out=def;
    return argc<=index||JS_IsUndefined(argv[index])||athena_js_float(ctx,argv[index],out,name);
}
static int opt_count(JSContext *ctx,int argc,JSValueConst *argv,int index,uint32_t def,uint32_t lo,uint32_t hi,uint32_t *out,const char *name) {
    float v; if(!opt_float(ctx,argc,argv,index,(float)def,&v,name)) return 0;
    if(v!=(float)(uint32_t)v||v<(float)lo||v>(float)hi) { JS_ThrowRangeError(ctx,"%s must be an integer from %u to %u",name,(unsigned)lo,(unsigned)hi); return 0; }
    *out=(uint32_t)v; return 1;
}
static JSValue done(JSContext *ctx,JSValueConst self,int code,const char *name) {
    if(code==ATHENA_MESHBUILDER_ENOMEM) return JS_ThrowOutOfMemory(ctx);
    if(code<0) return JS_ThrowRangeError(ctx,"%s: invalid arguments",name);
    return JS_DupValue(ctx,self);
}
/* color(r, g, b, a = 1): linear components in [0, 1]. */
static JSValue js_color(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,3,4,"Builder.color")) return JS_EXCEPTION;
    float c[4]; if(!floats(ctx,argc,argv,0,3,c,"color")||!opt_float(ctx,argc,argv,3,1,&c[3],"alpha")) return JS_EXCEPTION;
    AthenaMeshBuilder *b=get_builder(ctx,self); if(!b) return JS_EXCEPTION;
    if(athena_meshbuilder_set_color(b,c)<0) return JS_ThrowRangeError(ctx,"Builder.color components must be in [0, 1]");
    return JS_DupValue(ctx,self);
}
static JSValue js_transform(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"Builder.transform")) return JS_EXCEPTION;
    AthenaMatrix4 m,*mp=NULL;
    if(!JS_IsNull(argv[0])) { AthenaMatrix4 *src=athena_matrix4_from_value(ctx,argv[0]); if(!src) return JS_EXCEPTION; m=*src; mp=&m; }
    AthenaMeshBuilder *b=get_builder(ctx,self); if(!b) return JS_EXCEPTION;
    if(athena_meshbuilder_set_transform(b,mp)<0) return JS_ThrowRangeError(ctx,"Builder.transform needs a finite, invertible matrix");
    return JS_DupValue(ctx,self);
}
/* uvRect(u0, v0, u1, v1) */
static JSValue js_uv_rect(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,4,4,"Builder.uvRect")) return JS_EXCEPTION;
    float r[4]; if(!floats(ctx,argc,argv,0,4,r,"uv")) return JS_EXCEPTION;
    AthenaMeshBuilder *b=get_builder(ctx,self); if(!b) return JS_EXCEPTION;
    return done(ctx,self,athena_meshbuilder_set_uv_rect(b,r[0],r[1],r[2],r[3]),"Builder.uvRect");
}
/* uvTile(index, columns, rows, inset = 0): tile `index` of an atlas grid,
 * row by row from the top-left, shrunk by `inset` (in tile fractions) against bleeding. */
static JSValue js_uv_tile(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,3,4,"Builder.uvTile")) return JS_EXCEPTION;
    uint32_t index,cols,rows; float inset;
    if(!opt_count(ctx,argc,argv,1,1,1,4096,&cols,"columns")||!opt_count(ctx,argc,argv,2,1,1,4096,&rows,"rows")||
        !opt_count(ctx,argc,argv,0,0,0,cols*rows-1,&index,"index")||!opt_float(ctx,argc,argv,3,0,&inset,"inset")) return JS_EXCEPTION;
    if(inset<0||inset>=.5f) return JS_ThrowRangeError(ctx,"inset must be in [0, 0.5)");
    AthenaMeshBuilder *b=get_builder(ctx,self); if(!b) return JS_EXCEPTION;
    float w=1.0f/(float)cols,h=1.0f/(float)rows,u=(float)(index%cols)*w,v=(float)(index/cols)*h;
    return done(ctx,self,athena_meshbuilder_set_uv_rect(b,u+inset*w,v+inset*h,u+w-inset*w,v+h-inset*h),"Builder.uvTile");
}
/* vertex(x, y, z, nx?, ny?, nz?, u?, v?): the new vertex index. */
static JSValue js_vertex(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,3,8,"Builder.vertex")) return JS_EXCEPTION;
    float p[3],n[3],u,v; int has_n=argc>=6&&!JS_IsUndefined(argv[3]);
    if(!floats(ctx,argc,argv,0,3,p,"position")||(has_n&&!floats(ctx,argc,argv,3,3,n,"normal"))||
        !opt_float(ctx,argc,argv,6,0,&u,"u")||!opt_float(ctx,argc,argv,7,0,&v,"v")) return JS_EXCEPTION;
    AthenaMeshBuilder *b=get_builder(ctx,self); if(!b) return JS_EXCEPTION;
    int64_t i=athena_meshbuilder_vertex(b,p,has_n?n:NULL,u,v);
    if(i==ATHENA_MESHBUILDER_ENOMEM) return JS_ThrowOutOfMemory(ctx);
    if(i<0) return JS_ThrowRangeError(ctx,"Builder.vertex: invalid arguments");
    return JS_NewInt64(ctx,i);
}
static JSValue js_triangle(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,3,3,"Builder.triangle")) return JS_EXCEPTION;
    uint32_t i[3]; for(int k=0;k<3;k++) if(JS_ToUint32(ctx,&i[k],argv[k])<0) return JS_EXCEPTION;
    AthenaMeshBuilder *b=get_builder(ctx,self); if(!b) return JS_EXCEPTION;
    int code=athena_meshbuilder_triangle(b,i[0],i[1],i[2]);
    if(code==ATHENA_MESHBUILDER_EINVAL) return JS_ThrowRangeError(ctx,"Builder.triangle: index out of range");
    return done(ctx,self,code,"Builder.triangle");
}
static JSValue js_quad(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,12,12,"Builder.quad")) return JS_EXCEPTION;
    float p[12]; if(!floats(ctx,argc,argv,0,12,p,"corner")) return JS_EXCEPTION;
    AthenaMeshBuilder *b=get_builder(ctx,self); if(!b) return JS_EXCEPTION;
    return done(ctx,self,athena_meshbuilder_quad(b,p,p+3,p+6,p+9),"Builder.quad");
}
static JSValue js_box(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,6,6,"Builder.box")) return JS_EXCEPTION;
    float p[6]; if(!floats(ctx,argc,argv,0,6,p,"box")) return JS_EXCEPTION;
    AthenaMeshBuilder *b=get_builder(ctx,self); if(!b) return JS_EXCEPTION;
    return done(ctx,self,athena_meshbuilder_box(b,p,p+3),"Builder.box (minimum must not exceed maximum)");
}
static JSValue js_sphere(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,4,6,"Builder.sphere")) return JS_EXCEPTION;
    float p[4]; uint32_t s,r;
    if(!floats(ctx,argc,argv,0,4,p,"sphere")||!opt_count(ctx,argc,argv,4,16,3,ATHENA_MESHBUILDER_MAX_SEGMENTS,&s,"segments")||
        !opt_count(ctx,argc,argv,5,8,2,ATHENA_MESHBUILDER_MAX_SEGMENTS,&r,"rings")) return JS_EXCEPTION;
    AthenaMeshBuilder *b=get_builder(ctx,self); if(!b) return JS_EXCEPTION;
    return done(ctx,self,athena_meshbuilder_sphere(b,p,p[3],s,r),"Builder.sphere");
}
static JSValue js_cylinder(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,5,7,"Builder.cylinder")) return JS_EXCEPTION;
    float p[5]; uint32_t s;
    if(!floats(ctx,argc,argv,0,5,p,"cylinder")||!opt_count(ctx,argc,argv,5,16,3,ATHENA_MESHBUILDER_MAX_SEGMENTS,&s,"segments")) return JS_EXCEPTION;
    int caps=argc<7||JS_IsUndefined(argv[6])?1:JS_ToBool(ctx,argv[6]);
    AthenaMeshBuilder *b=get_builder(ctx,self); if(!b) return JS_EXCEPTION;
    return done(ctx,self,athena_meshbuilder_cylinder(b,p,p[3],p[4],s,caps),"Builder.cylinder");
}
static JSValue js_plane(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,5,7,"Builder.plane")) return JS_EXCEPTION;
    float p[5]; uint32_t nx,nz;
    if(!floats(ctx,argc,argv,0,5,p,"plane")||!opt_count(ctx,argc,argv,5,1,1,ATHENA_MESHBUILDER_MAX_SEGMENTS,&nx,"divisionsX")||
        !opt_count(ctx,argc,argv,6,nx,1,ATHENA_MESHBUILDER_MAX_SEGMENTS,&nz,"divisionsZ")) return JS_EXCEPTION;
    AthenaMeshBuilder *b=get_builder(ctx,self); if(!b) return JS_EXCEPTION;
    return done(ctx,self,athena_meshbuilder_plane(b,p,p[3],p[4],nx,nz),"Builder.plane");
}
static int color_option(JSContext *ctx,JSValueConst options,const char *key,float out[4],int *present) {
    JSValue v=JS_GetPropertyStr(ctx,options,key); if(JS_IsException(v)) return 0;
    *present=!JS_IsUndefined(v);
    int ok=1;
    if(*present) {
        out[3]=1;
        for(uint32_t i=0;ok&&i<4;i++) {
            JSValue c=JS_GetPropertyUint32(ctx,v,i);
            if(JS_IsException(c)) ok=0;
            else if(i==3&&JS_IsUndefined(c)) {}
            else ok=athena_js_float(ctx,c,&out[i],key);
            JS_FreeValue(ctx,c);
        }
    }
    JS_FreeValue(ctx,v); return ok;
}
/* heightmap(heights, width, depth, { cell, scale, x, y, z, low, high }) */
static JSValue js_heightmap(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,3,4,"Builder.heightmap")) return JS_EXCEPTION;
    uint32_t w,d;
    if(!opt_count(ctx,argc,argv,1,0,2,1024,&w,"width")||!opt_count(ctx,argc,argv,2,0,2,1024,&d,"depth")) return JS_EXCEPTION;
    float cell=1,scale=1,o[3]={0,0,0},low[4],high[4]; int has_low=0,has_high=0;
    if(argc==4&&!JS_IsUndefined(argv[3])) {
        JSValueConst opt=argv[3];
        if(!JS_IsObject(opt)) return JS_ThrowTypeError(ctx,"heightmap options must be an object");
        if(!athena_js_option_float(ctx,opt,"cell",&cell)||!athena_js_option_float(ctx,opt,"scale",&scale)||
            !athena_js_option_float(ctx,opt,"x",&o[0])||!athena_js_option_float(ctx,opt,"y",&o[1])||
            !athena_js_option_float(ctx,opt,"z",&o[2])||!color_option(ctx,opt,"low",low,&has_low)||
            !color_option(ctx,opt,"high",high,&has_high)) return JS_EXCEPTION;
        if(has_low!=has_high) return JS_ThrowTypeError(ctx,"heightmap low and high go together");
    }
    AthenaJSArray heights;
    if(!athena_js_array(ctx,argv[0],JS_TYPED_ARRAY_FLOAT32,&heights,"heights")) return JS_EXCEPTION;
    AthenaMeshBuilder *b=get_builder(ctx,self);
    JSValue r;
    if(!b) r=JS_EXCEPTION;
    else if(heights.count<(size_t)w*d) r=JS_ThrowRangeError(ctx,"heights needs width * depth values");
    else r=done(ctx,self,athena_meshbuilder_heightmap(b,heights.data,w,d,cell,scale,o,has_low?low:NULL,has_low?high:NULL),
        "Builder.heightmap (cell > 0, finite heights)");
    JS_FreeValue(ctx,heights.backing); return r;
}
/* merge(mesh | instance): static batching under the current transform (an
 * instance's own transform is applied after it). */
static JSValue js_merge(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"Builder.merge")) return JS_EXCEPTION;
    AthenaMeshBuilder *b=get_builder(ctx,self); if(!b) return JS_EXCEPTION;
    AthenaInstance3D *instance=athena_instance3d_from_value(ctx,argv[0]);
    if(instance) {
        AthenaMatrix4 model=*athena_instance3d_transform(instance);
        return done(ctx,self,athena_meshbuilder_merge(b,athena_instance3d_mesh(instance),&model),"Builder.merge");
    }
    JS_FreeValue(ctx,JS_GetException(ctx));
    AthenaMesh3D *mesh=athena_mesh3d_from_value(ctx,argv[0]);
    if(!mesh) { JS_FreeValue(ctx,JS_GetException(ctx)); return JS_ThrowTypeError(ctx,"Builder.merge expects a Model3D.Mesh or Instance"); }
    return done(ctx,self,athena_meshbuilder_merge(b,mesh,NULL),"Builder.merge");
}
static JSValue js_clear(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"Builder.clear")) return JS_EXCEPTION;
    AthenaMeshBuilder *b=get_builder(ctx,self); if(!b) return JS_EXCEPTION;
    athena_meshbuilder_clear(b); return JS_DupValue(ctx,self);
}
static JSValue js_counts(JSContext *ctx,JSValueConst self,int magic) {
    AthenaMeshBuilder *b=get_builder(ctx,self); if(!b) return JS_EXCEPTION;
    return JS_NewUint32(ctx,magic==0?athena_meshbuilder_vertex_count(b):magic==1?athena_meshbuilder_triangle_count(b):
        athena_meshbuilder_part_count(b));
}
/* build(material?): Model3D.Mesh[] (several when the geometry exceeds one mesh). */
static JSValue js_build(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,0,1,"Builder.build")) return JS_EXCEPTION;
    AthenaMaterial3D material;
    if(!athena_model3d_js_material(ctx,argc?argv[0]:JS_UNDEFINED,&material)) return JS_EXCEPTION;
    AthenaMeshBuilder *b=get_builder(ctx,self);
    if(!b) { athena_texture3d_release(material.texture); return JS_EXCEPTION; }
    uint32_t parts=athena_meshbuilder_part_count(b);
    AthenaMesh3D **meshes=parts?js_mallocz(ctx,parts*sizeof(*meshes)):NULL;
    if(parts&&!meshes) { athena_texture3d_release(material.texture); return JS_EXCEPTION; }
    athena_model3d_clear_detail();
    int code=parts?athena_meshbuilder_build(b,&material,meshes,parts):0;
    athena_texture3d_release(material.texture);
    if(code<0) {
        js_free(ctx,meshes);
        if(code==ATHENA_MESHBUILDER_ENOMEM) return JS_ThrowOutOfMemory(ctx);
        const char *detail=athena_model3d_detail();
        return JS_ThrowRangeError(ctx,"Builder.build: invalid geometry%s%s",detail[0]?": ":"",detail);
    }
    JSValue array=JS_NewArray(ctx);
    for(int i=0;i<code;i++) {
        /* Ownership moves to the wrapper, also on failure. */
        JSValue mesh=JS_IsException(array)?(athena_mesh3d_release(meshes[i]),JS_EXCEPTION):athena_mesh3d_js_wrap(ctx,meshes[i]);
        if(JS_IsException(array)) continue;
        if(JS_IsException(mesh)||JS_SetPropertyUint32(ctx,array,(uint32_t)i,mesh)<0) { JS_FreeValue(ctx,array); array=JS_EXCEPTION; }
    }
    js_free(ctx,meshes);
    return array;
}
static JSClassDef class_def={"MeshBuilder.Builder",.finalizer=finalizer};
static const JSCFunctionListEntry methods[]={
    JS_CFUNC_DEF("color",3,js_color),JS_CFUNC_DEF("transform",1,js_transform),JS_CFUNC_DEF("uvRect",4,js_uv_rect),
    JS_CFUNC_DEF("uvTile",3,js_uv_tile),JS_CFUNC_DEF("vertex",3,js_vertex),JS_CFUNC_DEF("triangle",3,js_triangle),
    JS_CFUNC_DEF("quad",12,js_quad),JS_CFUNC_DEF("box",6,js_box),JS_CFUNC_DEF("sphere",4,js_sphere),
    JS_CFUNC_DEF("cylinder",5,js_cylinder),JS_CFUNC_DEF("plane",5,js_plane),JS_CFUNC_DEF("heightmap",3,js_heightmap),
    JS_CFUNC_DEF("merge",1,js_merge),JS_CFUNC_DEF("clear",0,js_clear),JS_CFUNC_DEF("build",0,js_build),
    JS_CFUNC_DEF("dispose",0,dispose),
    JS_CGETSET_MAGIC_DEF("vertexCount",js_counts,NULL,0),JS_CGETSET_MAGIC_DEF("triangleCount",js_counts,NULL,1),
    JS_CGETSET_MAGIC_DEF("partCount",js_counts,NULL,2)};
static int init(JSContext *ctx,JSModuleDef *m) {
    if(athena_register_class(ctx,&builder_id,&class_def)<0) return -1;
    JSValue proto=JS_NewObject(ctx); if(JS_IsException(proto)) return -1;
    JS_SetPropertyFunctionList(ctx,proto,methods,countof(methods));
    JSValue cls=JS_NewCFunction2(ctx,ctor,"Builder",0,JS_CFUNC_constructor,0);
    JS_SetConstructor(ctx,cls,proto); JS_SetClassProto(ctx,builder_id,proto);
    if(JS_SetModuleExport(ctx,m,"Builder",cls)<0) return -1;
    return 0;
}
JSModuleDef *athena_meshbuilder_js_init(JSContext *ctx) {
    JSModuleDef *m=athena_push_module(ctx,init,NULL,0,"MeshBuilder");
    if(m) JS_AddModuleExport(ctx,m,"Builder");
    return m;
}
