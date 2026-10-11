#include <math.h>
#include <string.h>
#include <athena_js_args.h>
#include <athena/voxel.h>
#include <athena/js/model3d.h>
#include <athena/js/camera3d.h>
#include <athena/js/lights.h>
#include <athena/js/render3d.h>
#include "ath_voxel.h"
static JSClassID world_id;
static AthenaVoxelWorld *get_world(JSContext *ctx,JSValueConst v) {
    AthenaVoxelWorld *w=JS_GetOpaque2(ctx,v,world_id);
    if(!w) JS_ThrowTypeError(ctx,"Expected a live Voxel.World");
    return w;
}
static void finalizer(JSRuntime *rt,JSValue v) { (void)rt; athena_voxel_release(JS_GetOpaque(v,world_id)); }
static JSValue throw_code(JSContext *ctx,int code,const char *what) {
    if(code==ATHENA_VOXEL_ENOMEM) return JS_ThrowOutOfMemory(ctx);
    return JS_ThrowRangeError(ctx,"%s: invalid arguments",what);
}
static int get_int(JSContext *ctx,JSValueConst v,int32_t *out,const char *name) {
    float f; if(!athena_js_float(ctx,v,&f,name)) return 0;
    if(f!=floorf(f)||f<-1e9f||f>1e9f) { JS_ThrowRangeError(ctx,"%s must be an integer",name); return 0; }
    *out=(int32_t)f; return 1;
}
static int ints(JSContext *ctx,JSValueConst *argv,int count,int32_t *out,const char *name) {
    for(int i=0;i<count;i++) if(!get_int(ctx,argv[i],&out[i],name)) return 0;
    return 1;
}
static int type_arg(JSContext *ctx,JSValueConst v,uint8_t *out) {
    int32_t t; if(!get_int(ctx,v,&t,"type")) return 0;
    if(t<0||t>255) { JS_ThrowRangeError(ctx,"block type must be 0 to 255"); return 0; }
    *out=(uint8_t)t; return 1;
}
/* new Voxel.World({ size: [x, y, z], chunk = 16 }) */
static JSValue ctor(JSContext *ctx,JSValueConst target,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"Voxel.World")||!JS_IsObject(argv[0])) return JS_ThrowTypeError(ctx,"Voxel.World expects { size: [x, y, z], chunk? }");
    AthenaVoxelDesc d={{0,0,0},16};
    JSValue size=JS_GetPropertyStr(ctx,argv[0],"size"); if(JS_IsException(size)) return size;
    int ok=JS_IsArray(ctx,size),thrown=0;
    for(uint32_t i=0;ok&&i<3;i++) {
        JSValue v=JS_GetPropertyUint32(ctx,size,i); int32_t n=0;
        if(JS_IsException(v)||!get_int(ctx,v,&n,"size")) { ok=0; thrown=1; }
        else if(n<1||n>4096) ok=0;
        else d.size[i]=(uint32_t)n;
        JS_FreeValue(ctx,v);
    }
    JS_FreeValue(ctx,size);
    if(!ok) return thrown?JS_EXCEPTION:JS_ThrowRangeError(ctx,"Voxel.World size must be [x, y, z], 1 to 4096 each");
    float chunk=16; if(!athena_js_option_float(ctx,argv[0],"chunk",&chunk)) return JS_EXCEPTION;
    if(chunk!=floorf(chunk)||chunk<4||chunk>32) return JS_ThrowRangeError(ctx,"chunk must be an integer from 4 to 32");
    d.chunk=(uint32_t)chunk;
    if((uint64_t)d.size[0]*d.size[1]*d.size[2]>ATHENA_VOXEL_MAX_BLOCKS) return JS_ThrowRangeError(ctx,"Voxel.World holds at most %u blocks",ATHENA_VOXEL_MAX_BLOCKS);
    JSValue proto=JS_GetPropertyStr(ctx,target,"prototype"); if(JS_IsException(proto)) return proto;
    JSValue obj=JS_NewObjectProtoClass(ctx,proto,world_id); JS_FreeValue(ctx,proto);
    if(JS_IsException(obj)) return obj;
    AthenaVoxelWorld *w=athena_voxel_create(&d);
    if(!w) { JS_FreeValue(ctx,obj); return JS_ThrowOutOfMemory(ctx); }
    JS_SetOpaque(obj,w); return obj;
}
static JSValue dispose(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"World.dispose")||!athena_js_class(ctx,self,world_id)) return JS_EXCEPTION;
    AthenaVoxelWorld *w=JS_GetOpaque(self,world_id); JS_SetOpaque(self,NULL); athena_voxel_release(w);
    return JS_UNDEFINED;
}
/* A color [r, g, b, a?] or { top, bottom, side } of them, into the three groups. */
static int read_rgba(JSContext *ctx,JSValueConst v,float out[4]) {
    if(!JS_IsArray(ctx,v)) { JS_ThrowTypeError(ctx,"color must be [r, g, b, a?]"); return 0; }
    out[3]=1;
    for(uint32_t i=0;i<4;i++) {
        JSValue c=JS_GetPropertyUint32(ctx,v,i); int ok=1;
        if(JS_IsException(c)) return 0;
        if(!(i==3&&JS_IsUndefined(c))) ok=athena_js_float(ctx,c,&out[i],"color");
        JS_FreeValue(ctx,c);
        if(!ok) return 0;
    }
    return 1;
}
static const char *const GROUPS[3]={"top","bottom","side"};
static int per_face(JSContext *ctx,JSValueConst opt,const char *key,int is_tile,AthenaVoxelMaterial *m) {
    JSValue v=JS_GetPropertyStr(ctx,opt,key); if(JS_IsException(v)) return 0;
    int ok=1;
    if(!JS_IsUndefined(v)) {
        int grouped=JS_IsObject(v)&&!JS_IsArray(ctx,v);
        for(int g=0;ok&&g<3;g++) {
            JSValue item=grouped?JS_GetPropertyStr(ctx,v,GROUPS[g]):JS_DupValue(ctx,v);
            if(JS_IsException(item)) { ok=0; break; }
            if(grouped&&JS_IsUndefined(item)) { JS_FreeValue(ctx,item); continue; }
            if(is_tile) {
                int32_t t; ok=get_int(ctx,item,&t,"tile");
                if(ok&&(t<-1||t>32767)) { JS_ThrowRangeError(ctx,"tile must be -1 to 32767"); ok=0; }
                if(ok) m->tile[g]=(int16_t)t;
            } else ok=read_rgba(ctx,item,m->color[g]);
            JS_FreeValue(ctx,item);
        }
    }
    JS_FreeValue(ctx,v); return ok;
}
static int bool_option(JSContext *ctx,JSValueConst opt,const char *key,uint8_t *out) {
    JSValue v=JS_GetPropertyStr(ctx,opt,key); if(JS_IsException(v)) return 0;
    if(!JS_IsUndefined(v)) *out=(uint8_t)JS_ToBool(ctx,v);
    JS_FreeValue(ctx,v); return 1;
}
/* setMaterial(type, { solid, visible, color, tile }) */
static JSValue set_material(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,2,2,"World.setMaterial")) return JS_EXCEPTION;
    uint8_t type; if(!type_arg(ctx,argv[0],&type)) return JS_EXCEPTION;
    if(!type) return JS_ThrowRangeError(ctx,"type 0 is air and cannot be changed");
    if(!JS_IsObject(argv[1])) return JS_ThrowTypeError(ctx,"setMaterial expects an object");
    AthenaVoxelMaterial m; memset(&m,0,sizeof(m)); m.solid=m.visible=1;
    for(int g=0;g<3;g++) { m.color[g][0]=m.color[g][1]=m.color[g][2]=m.color[g][3]=1; m.tile[g]=-1; }
    if(!bool_option(ctx,argv[1],"solid",&m.solid)||!bool_option(ctx,argv[1],"visible",&m.visible)||
        !per_face(ctx,argv[1],"color",0,&m)||!per_face(ctx,argv[1],"tile",1,&m)) return JS_EXCEPTION;
    AthenaVoxelWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    if(athena_voxel_set_material(w,type,&m)<0) return JS_ThrowRangeError(ctx,"setMaterial: colors must be in [0, 1]");
    return JS_DupValue(ctx,self);
}
/* setStyle({ meshing, ambientOcclusion, bakedLight, shading, atlas, tileSize }) */
static JSValue set_style(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,1,"World.setStyle")||!JS_IsObject(argv[0])) return JS_ThrowTypeError(ctx,"setStyle expects an object");
    AthenaVoxelWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    AthenaVoxelStyle s={ATHENA_VOXEL_MESH_NAIVE,.5f,1,ATHENA_MATERIAL3D_UNLIT,NULL,0};
    JSValue v=JS_GetPropertyStr(ctx,argv[0],"meshing"); if(JS_IsException(v)) return v;
    if(!JS_IsUndefined(v)) {
        const char *m=JS_ToCString(ctx,v); JS_FreeValue(ctx,v); if(!m) return JS_EXCEPTION;
        int greedy=!strcmp(m,"greedy"),naive=!strcmp(m,"naive"); JS_FreeCString(ctx,m);
        if(!greedy&&!naive) return JS_ThrowRangeError(ctx,"meshing must be \"naive\" or \"greedy\"");
        s.meshing=greedy?ATHENA_VOXEL_MESH_GREEDY:ATHENA_VOXEL_MESH_NAIVE;
    }
    uint8_t baked=1; float shading=0,tile=0;
    if(!athena_js_option_float(ctx,argv[0],"ambientOcclusion",&s.ambient_occlusion)||!bool_option(ctx,argv[0],"bakedLight",&baked)||
        !athena_js_option_float(ctx,argv[0],"shading",&shading)||!athena_js_option_float(ctx,argv[0],"tileSize",&tile)) return JS_EXCEPTION;
    if(shading!=0&&shading!=1) return JS_ThrowRangeError(ctx,"shading must be Model3D.UNLIT or DIFFUSE");
    s.baked_light=baked; s.shading=(AthenaMaterial3DShading)shading; s.tile_size=tile>0?(uint32_t)tile:0;
    v=JS_GetPropertyStr(ctx,argv[0],"atlas"); if(JS_IsException(v)) return v;
    if(!JS_IsUndefined(v)&&!JS_IsNull(v)) {
        AthenaMaterial3D probe; JSValue holder=JS_NewObject(ctx);
        JS_SetPropertyStr(ctx,holder,"texture",JS_DupValue(ctx,v));
        int ok=athena_model3d_js_material(ctx,holder,&probe);
        JS_FreeValue(ctx,holder); JS_FreeValue(ctx,v);
        if(!ok) return JS_EXCEPTION;
        s.atlas=probe.texture;   /* a reference we own */
        if(!s.tile_size) { athena_texture3d_release(s.atlas); return JS_ThrowTypeError(ctx,"setStyle atlas needs tileSize"); }
    } else JS_FreeValue(ctx,v);
    int code=athena_voxel_set_style(w,&s);
    athena_texture3d_release(s.atlas);
    if(code<0) return JS_ThrowRangeError(ctx,"setStyle: invalid style (ambientOcclusion 0..1; the atlas must be whole tiles; greedy meshing cannot use an atlas)");
    return JS_DupValue(ctx,self);
}
static JSValue get_block(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,3,3,"World.get")) return JS_EXCEPTION;
    int32_t p[3]; if(!ints(ctx,argv,3,p,"coordinate")) return JS_EXCEPTION;
    AthenaVoxelWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    return JS_NewInt32(ctx,athena_voxel_get(w,p[0],p[1],p[2]));
}
static JSValue set_block(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,4,4,"World.set")) return JS_EXCEPTION;
    int32_t p[3]; uint8_t t;
    if(!ints(ctx,argv,3,p,"coordinate")||!type_arg(ctx,argv[3],&t)) return JS_EXCEPTION;
    AthenaVoxelWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    int code=athena_voxel_set(w,p[0],p[1],p[2],t);
    if(code<0) return JS_ThrowRangeError(ctx,"World.set: (%d, %d, %d) is outside the world",(int)p[0],(int)p[1],(int)p[2]);
    return JS_NewBool(ctx,code>0);
}
static JSValue fill(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,7,7,"World.fill")) return JS_EXCEPTION;
    int32_t p[6]; uint8_t t;
    if(!ints(ctx,argv,6,p,"coordinate")||!type_arg(ctx,argv[6],&t)) return JS_EXCEPTION;
    AthenaVoxelWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    return JS_NewInt32(ctx,athena_voxel_fill(w,p,p+3,t));
}
static int region(JSContext *ctx,JSValueConst *argv,int32_t o[3],uint32_t s[3]) {
    int32_t v[6]; if(!ints(ctx,argv,6,v,"region")) return 0;
    for(int i=0;i<3;i++) { o[i]=v[i]; if(v[3+i]<1) { JS_ThrowRangeError(ctx,"region sizes must be positive"); return 0; } s[i]=(uint32_t)v[3+i]; }
    return 1;
}
/* read(x, y, z, sx, sy, sz): Uint8Array, x fastest, then z, then y. */
static JSValue read_region(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,6,6,"World.read")) return JS_EXCEPTION;
    int32_t o[3]; uint32_t s[3]; if(!region(ctx,argv,o,s)) return JS_EXCEPTION;
    AthenaVoxelWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    size_t n=(size_t)s[0]*s[1]*s[2];
    uint8_t *tmp=js_malloc(ctx,n?n:1); if(!tmp) return JS_EXCEPTION;
    if(athena_voxel_read(w,o,s,tmp)<0) { js_free(ctx,tmp); return JS_ThrowRangeError(ctx,"World.read: region outside the world"); }
    JSValue buffer=JS_NewArrayBufferCopy(ctx,tmp,n); js_free(ctx,tmp);
    if(JS_IsException(buffer)) return buffer;
    JSValue ctor_u8=JS_GetGlobalObject(ctx),u8=JS_GetPropertyStr(ctx,ctor_u8,"Uint8Array"); JS_FreeValue(ctx,ctor_u8);
    JSValue arr=JS_CallConstructor(ctx,u8,1,(JSValueConst *)&buffer);
    JS_FreeValue(ctx,u8); JS_FreeValue(ctx,buffer); return arr;
}
static JSValue write_region(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,7,7,"World.write")) return JS_EXCEPTION;
    int32_t o[3]; uint32_t s[3]; if(!region(ctx,argv,o,s)) return JS_EXCEPTION;
    AthenaJSArray data; if(!athena_js_array(ctx,argv[6],JS_TYPED_ARRAY_UINT8,&data,"data")) return JS_EXCEPTION;
    AthenaVoxelWorld *w=get_world(ctx,self);
    JSValue r=JS_DupValue(ctx,self);
    if(!w) { JS_FreeValue(ctx,r); r=JS_EXCEPTION; }
    else if(data.count<(size_t)s[0]*s[1]*s[2]) { JS_FreeValue(ctx,r); r=JS_ThrowRangeError(ctx,"World.write: data is smaller than the region"); }
    else if(athena_voxel_write(w,o,s,data.data)<0) { JS_FreeValue(ctx,r); r=JS_ThrowRangeError(ctx,"World.write: region outside the world"); }
    JS_FreeValue(ctx,data.backing); return r;
}
static int opt_u8(JSContext *ctx,JSValueConst opt,const char *key,uint8_t *out) {
    float f=*out; if(!athena_js_option_float(ctx,opt,key,&f)) return 0;
    if(f!=floorf(f)||f<0||f>255) { JS_ThrowRangeError(ctx,"%s must be a block type (0 to 255)",key); return 0; }
    *out=(uint8_t)f; return 1;
}
/* generate({ seed, baseHeight, amplitude, frequency, octaves, top, filler, stone, fillerDepth, caves, caveFrequency, water, waterLevel }) */
static JSValue generate(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,0,1,"World.generate")) return JS_EXCEPTION;
    AthenaVoxelTerrain t; athena_voxel_terrain_default(&t);
    if(argc&&!JS_IsUndefined(argv[0])) {
        JSValueConst o=argv[0];
        if(!JS_IsObject(o)) return JS_ThrowTypeError(ctx,"generate expects an object");
        float seed=(float)t.seed,octaves=(float)t.octaves,depth=(float)t.filler_depth;
        if(!athena_js_option_float(ctx,o,"seed",&seed)||!athena_js_option_float(ctx,o,"baseHeight",&t.base_height)||
            !athena_js_option_float(ctx,o,"amplitude",&t.amplitude)||!athena_js_option_float(ctx,o,"frequency",&t.frequency)||
            !athena_js_option_float(ctx,o,"octaves",&octaves)||!opt_u8(ctx,o,"top",&t.top)||!opt_u8(ctx,o,"filler",&t.filler)||
            !opt_u8(ctx,o,"stone",&t.stone)||!athena_js_option_float(ctx,o,"fillerDepth",&depth)||
            !athena_js_option_float(ctx,o,"caves",&t.caves)||!athena_js_option_float(ctx,o,"caveFrequency",&t.cave_frequency)||
            !opt_u8(ctx,o,"water",&t.water)||!athena_js_option_float(ctx,o,"waterLevel",&t.water_level)) return JS_EXCEPTION;
        if(seed<0||seed!=floorf(seed)||octaves<1||octaves>8||octaves!=floorf(octaves)||depth<0||depth>255)
            return JS_ThrowRangeError(ctx,"generate: seed >= 0 integer, octaves 1-8, fillerDepth 0-255");
        t.seed=(uint32_t)seed; t.octaves=(uint32_t)octaves; t.filler_depth=(uint32_t)depth;
    }
    AthenaVoxelWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    int code=athena_voxel_generate(w,&t);
    if(code<0) return code==ATHENA_VOXEL_ENOMEM?JS_ThrowOutOfMemory(ctx):JS_ThrowRangeError(ctx,"generate: frequency > 0, caves in [0, 1)");
    return JS_DupValue(ctx,self);
}
static JSValue surface(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,2,2,"World.surface")) return JS_EXCEPTION;
    int32_t p[2]; if(!ints(ctx,argv,2,p,"coordinate")) return JS_EXCEPTION;
    AthenaVoxelWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    return JS_NewInt32(ctx,athena_voxel_surface(w,p[0],p[1]));
}
/* rebuild(budgetMs = 4, x?, y?, z?): chunks rebuilt, nearest to (x, y, z) first. */
static JSValue rebuild(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,0,4,"World.rebuild")) return JS_EXCEPTION;
    float budget=4,focus[3];
    if(argc>=1&&!JS_IsUndefined(argv[0])&&!athena_js_float(ctx,argv[0],&budget,"budgetMs")) return JS_EXCEPTION;
    int has_focus=argc==4;
    if(argc!=1&&argc!=0&&argc!=4) return JS_ThrowTypeError(ctx,"World.rebuild(budgetMs?, x, y, z): give all three focus coordinates");
    for(int i=0;has_focus&&i<3;i++) if(!athena_js_float(ctx,argv[1+i],&focus[i],"focus")) return JS_EXCEPTION;
    AthenaVoxelWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    int code=athena_voxel_rebuild(w,budget,has_focus?focus:NULL);
    if(code<0) return throw_code(ctx,code,"World.rebuild");
    return JS_NewInt32(ctx,code);
}
static const char *const names[]={"chunks","meshedChunks","meshes","faces","meshBytes","lastRebuildMs","lastMeshMs","lastBuildMs",
    "x","y","z","nx","ny","nz","type","distance","px","py","pz","dx","dy","dz","hitX","hitY","hitZ","onGround"};
static JSAtom atoms[countof(names)];
static AthenaJSAtoms table={names,countof(names),atoms,NULL};
static JSValue stats_fn(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,0,1,"World.stats")) return JS_EXCEPTION;
    AthenaVoxelWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    AthenaVoxelStats s; athena_voxel_stats(w,&s);
    JSValue obj; int define;
    if(!athena_js_out_object(ctx,argc?argv[0]:JS_UNDEFINED,&obj,&define,"out")) return JS_EXCEPTION;
    const double v[]={s.chunks,s.meshed_chunks,s.meshes,s.faces,(double)s.mesh_bytes,s.last_rebuild_ms,s.last_mesh_ms,s.last_build_ms};
    for(unsigned i=0;i<countof(v);i++) if(athena_js_put(ctx,&table,obj,define,i,JS_NewFloat64(ctx,v[i]))<0) { JS_FreeValue(ctx,obj); return JS_EXCEPTION; }
    return obj;
}
static JSValue dirty_count(JSContext *ctx,JSValueConst self) {
    AthenaVoxelWorld *w=get_world(ctx,self); return w?JS_NewUint32(ctx,athena_voxel_dirty_count(w)):JS_EXCEPTION;
}
static JSValue size_get(JSContext *ctx,JSValueConst self,int magic) {
    AthenaVoxelWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    uint32_t size[3],chunk; athena_voxel_size(w,size,&chunk);
    return JS_NewUint32(ctx,magic<3?size[magic]:chunk);
}
static int put_list(JSContext *ctx,JSValueConst obj,int define,const unsigned *fields,const double *values,unsigned n) {
    for(unsigned i=0;i<n;i++) if(athena_js_put(ctx,&table,obj,define,fields[i],JS_NewFloat64(ctx,values[i]))<0) return 0;
    return 1;
}
/* raycast(ray, maxDistance = 8, out?): { x, y, z, nx, ny, nz, type, distance, px, py, pz } or null. */
static JSValue raycast(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,3,"World.raycast")) return JS_EXCEPTION;
    if(!JS_IsObject(argv[0])) return JS_ThrowTypeError(ctx,"raycast expects a ray { x, y, z, dx, dy, dz }");
    float r[6]; static const char *const keys[6]={"x","y","z","dx","dy","dz"};
    for(int i=0;i<6;i++) {
        JSValue v=JS_GetPropertyStr(ctx,argv[0],keys[i]); if(JS_IsException(v)) return v;
        int ok=athena_js_float(ctx,v,&r[i],keys[i]); JS_FreeValue(ctx,v); if(!ok) return JS_EXCEPTION;
    }
    float max=8; if(argc>=2&&!JS_IsUndefined(argv[1])&&!athena_js_float(ctx,argv[1],&max,"maxDistance")) return JS_EXCEPTION;
    JSValueConst out=argc==3?argv[2]:JS_UNDEFINED;
    if(!JS_IsUndefined(out)&&!JS_IsObject(out)) return JS_ThrowTypeError(ctx,"out must be an object");
    AthenaVoxelWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    AthenaVoxelHit hit;
    int code=athena_voxel_raycast(w,r,r+3,max,&hit);
    if(code<0) return JS_ThrowRangeError(ctx,"raycast: zero direction or negative maxDistance");
    if(!code) return JS_NULL;
    JSValue obj; int define;
    if(!athena_js_out_object(ctx,out,&obj,&define,"out")) return JS_EXCEPTION;
    static const unsigned fields[]={8,9,10,11,12,13,14,15,16,17,18};
    const double v[]={hit.block[0],hit.block[1],hit.block[2],hit.normal[0],hit.normal[1],hit.normal[2],hit.type,
        hit.distance,hit.point[0],hit.point[1],hit.point[2]};
    if(!put_list(ctx,obj,define,fields,v,countof(v))) { JS_FreeValue(ctx,obj); return JS_EXCEPTION; }
    return obj;
}
/* moveBox(minX, minY, minZ, maxX, maxY, maxZ, dx, dy, dz, out?) */
static JSValue move_box(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,9,10,"World.moveBox")) return JS_EXCEPTION;
    float f[9]; for(int i=0;i<9;i++) if(!athena_js_float(ctx,argv[i],&f[i],"moveBox")) return JS_EXCEPTION;
    JSValueConst out=argc==10?argv[9]:JS_UNDEFINED;
    if(!JS_IsUndefined(out)&&!JS_IsObject(out)) return JS_ThrowTypeError(ctx,"out must be an object");
    AthenaVoxelWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    AthenaVoxelMove m;
    if(athena_voxel_move_box(w,f,f+3,f+6,&m)<0) return JS_ThrowRangeError(ctx,"moveBox: min <= max, at most 64 blocks per axis and |delta| <= 256");
    JSValue obj; int define;
    if(!athena_js_out_object(ctx,out,&obj,&define,"out")) return JS_EXCEPTION;
    static const unsigned fields[]={19,20,21};
    const double v[]={m.moved[0],m.moved[1],m.moved[2]};
    int ok=put_list(ctx,obj,define,fields,v,3);
    for(int i=0;ok&&i<3;i++) ok=athena_js_put(ctx,&table,obj,define,22+i,JS_NewBool(ctx,m.hit[i]))>=0;
    if(ok) ok=athena_js_put(ctx,&table,obj,define,25,JS_NewBool(ctx,m.on_ground))>=0;
    if(!ok) { JS_FreeValue(ctx,obj); return JS_EXCEPTION; }
    return obj;
}
static JSValue box_solid(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,6,6,"World.boxSolid")) return JS_EXCEPTION;
    float f[6]; for(int i=0;i<6;i++) if(!athena_js_float(ctx,argv[i],&f[i],"box")) return JS_EXCEPTION;
    AthenaVoxelWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    int code=athena_voxel_box_solid(w,f,f+3);
    if(code<0) return JS_ThrowRangeError(ctx,"boxSolid: min <= max, at most 256 blocks per axis");
    return JS_NewBool(ctx,code);
}
/* draw(camera, cullMode = CULL_BACK, lights?, stats?, distance = 0) */
static JSValue draw(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    if(!athena_js_argc(ctx,argc,1,5,"World.draw")) return JS_EXCEPTION;
    AthenaRender3DCull cull=ATHENA_RENDER3D_CULL_BACK; float distance=0;
    if(argc>=2&&!JS_IsUndefined(argv[1])) {
        float v; if(!athena_js_float(ctx,argv[1],&v,"cullMode")) return JS_EXCEPTION;
        if(v!=0&&v!=1&&v!=-1) return JS_ThrowRangeError(ctx,"Invalid cullMode");
        cull=(AthenaRender3DCull)v;
    }
    AthenaLights *lights=NULL;
    if(argc>=3&&!JS_IsUndefined(argv[2])&&!(lights=athena_lights_from_value(ctx,argv[2]))) return JS_EXCEPTION;
    JSValueConst out=argc>=4?argv[3]:JS_UNDEFINED;
    if(!JS_IsUndefined(out)&&!JS_IsNull(out)&&!JS_IsObject(out)) return JS_ThrowTypeError(ctx,"stats must be an object or null");
    if(argc==5&&!JS_IsUndefined(argv[4])&&!athena_js_float(ctx,argv[4],&distance,"distance")) return JS_EXCEPTION;
    AthenaCamera3D *camera=athena_camera3d_from_value(ctx,argv[0]); if(!camera) return JS_EXCEPTION;
    AthenaVoxelWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    AthenaRender3DStats s; int render=0;
    athena_render3d_set_error_detail(NULL);
    int code=athena_voxel_draw(w,camera,lights,cull,distance,&s,&render);
    if(code==ATHENA_VOXEL_ERENDER) return athena_render3d_js_throw(ctx,render);
    if(code<0) return JS_ThrowRangeError(ctx,"World.draw: invalid camera or distance");
    athena_render3d_stats_add(&s);
    if(JS_IsNull(out)) return JS_UNDEFINED;
    JSValue obj; int define;
    if(!athena_js_out_object(ctx,out,&obj,&define,"stats")) return JS_EXCEPTION;
    if(athena_render3d_js_put_stats(ctx,obj,define,&s)<0) { JS_FreeValue(ctx,obj); return JS_EXCEPTION; }
    return obj;
}
static JSValue clear_meshes(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)argv;
    if(!athena_js_argc(ctx,argc,0,0,"World.clearMeshes")) return JS_EXCEPTION;
    AthenaVoxelWorld *w=get_world(ctx,self); if(!w) return JS_EXCEPTION;
    athena_voxel_clear_meshes(w); return JS_DupValue(ctx,self);
}
static JSClassDef class_def={"Voxel.World",.finalizer=finalizer};
static const JSCFunctionListEntry methods[]={
    JS_CFUNC_DEF("setMaterial",2,set_material),JS_CFUNC_DEF("setStyle",1,set_style),
    JS_CFUNC_DEF("get",3,get_block),JS_CFUNC_DEF("set",4,set_block),JS_CFUNC_DEF("fill",7,fill),
    JS_CFUNC_DEF("read",6,read_region),JS_CFUNC_DEF("write",7,write_region),JS_CFUNC_DEF("generate",0,generate),
    JS_CFUNC_DEF("surface",2,surface),JS_CFUNC_DEF("rebuild",0,rebuild),JS_CFUNC_DEF("stats",0,stats_fn),
    JS_CFUNC_DEF("raycast",1,raycast),JS_CFUNC_DEF("moveBox",9,move_box),JS_CFUNC_DEF("boxSolid",6,box_solid),
    JS_CFUNC_DEF("draw",1,draw),JS_CFUNC_DEF("clearMeshes",0,clear_meshes),JS_CFUNC_DEF("dispose",0,dispose),
    JS_CGETSET_DEF("dirtyCount",dirty_count,NULL),
    JS_CGETSET_MAGIC_DEF("sizeX",size_get,NULL,0),JS_CGETSET_MAGIC_DEF("sizeY",size_get,NULL,1),
    JS_CGETSET_MAGIC_DEF("sizeZ",size_get,NULL,2),JS_CGETSET_MAGIC_DEF("chunkSize",size_get,NULL,3)};
static const JSCFunctionListEntry exports[]={
    JS_PROP_INT32_DEF("AIR",0,JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("MAX_BLOCKS",ATHENA_VOXEL_MAX_BLOCKS,JS_PROP_ENUMERABLE)};
static int init(JSContext *ctx,JSModuleDef *m) {
    if(athena_register_class(ctx,&world_id,&class_def)<0) return -1;
    JSValue proto=JS_NewObject(ctx); if(JS_IsException(proto)) return -1;
    JS_SetPropertyFunctionList(ctx,proto,methods,countof(methods));
    JSValue cls=JS_NewCFunction2(ctx,ctor,"World",1,JS_CFUNC_constructor,0);
    JS_SetConstructor(ctx,cls,proto); JS_SetClassProto(ctx,world_id,proto);
    if(JS_SetModuleExport(ctx,m,"World",cls)<0) return -1;
    return JS_SetModuleExportList(ctx,m,exports,countof(exports));
}
void athena_voxel_js_cleanup(JSContext *ctx) { athena_js_atoms_free(ctx,&table); }
JSModuleDef *athena_voxel_js_init(JSContext *ctx) {
    JSModuleDef *m=athena_push_module(ctx,init,exports,countof(exports),"Voxel");
    if(m) JS_AddModuleExport(ctx,m,"World");
    return m;
}
