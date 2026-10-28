#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <athena/model3d.h>
#include "fast_obj/fast_obj.h"
/* Declarations first: the implementation below must be expanded only once. */
#include "model3d_gltf.h"
#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"

#if defined(ATHENA_MEMORY_DIAGNOSTICS) && ATHENA_MEMORY_DIAGNOSTICS
#define ATHENA_MEMORY_CATEGORY ATHENA_MEM_GEOMETRY
#include <athena/memory_tags.h>
#endif

#define REJECT(code,...) do { athena_model3d_set_detail(__VA_ARGS__); result=(code); goto done; } while(0)

/* Static subset: triangles, quads and convex polygons (fan triangulated),
 * Kd colours and at most one map_Kd texture (repeat addressing, as MTL
 * viewers sample). Lighting-only maps (Ka, Ks, Ns, Ni, bump, Ke) are ignored;
 * transparency (d < 1, map_d, map_Kt) is rejected. */
static int load_obj(const char *path,const AthenaMaterial3D *material_override,AthenaMesh3D **out) {
    fastObjMesh *obj=fast_obj_read(path);
    if(!obj) { athena_model3d_set_detail("OBJ parser failed"); return ATHENA_MODEL3D_EFORMAT; }
    int result=ATHENA_MODEL3D_EUNSUPPORTED;
    float *positions=NULL,*colors=NULL,*normals=NULL,*texcoords=NULL;
    AthenaTexture3D *loaded_texture=NULL; uint8_t *used_materials=NULL;
    AthenaMaterial3D material; athena_material3d_default(&material);
    if(material_override) material=*material_override;
    if(obj->strip_count) REJECT(ATHENA_MODEL3D_EUNSUPPORTED,"OBJ triangle strips are not supported");
    if(!obj->face_count) REJECT(ATHENA_MODEL3D_EFORMAT,"OBJ has no faces");
    uint64_t corners=0,used=0;
    for(uint32_t i=0;i<obj->face_count;i++) {
        if(obj->face_vertices[i]<3) REJECT(ATHENA_MODEL3D_EFORMAT,"OBJ face %u has %u vertices",(unsigned)i,(unsigned)obj->face_vertices[i]);
        corners+=(uint64_t)(obj->face_vertices[i]-2)*3; used+=obj->face_vertices[i];
    }
    if(used!=obj->index_count) REJECT(ATHENA_MODEL3D_EFORMAT,"OBJ face and index counts disagree");
    if(corners>ATHENA_MODEL3D_MAX_VERTICES)
        REJECT(ATHENA_MODEL3D_EUNSUPPORTED,"OBJ needs %llu triangle vertices, more than %u",(unsigned long long)corners,ATHENA_MODEL3D_MAX_VERTICES);
    const char *texture_path=NULL;
    /* Only the materials faces use: a library may define many more. */
    used_materials=calloc(obj->material_count?obj->material_count:1,1);
    if(!used_materials) { result=ATHENA_MODEL3D_ENOMEM; goto done; }
    for(uint32_t f=0;f<obj->face_count;f++) if(obj->face_materials[f]<obj->material_count) used_materials[obj->face_materials[f]]=1;
    for(uint32_t i=0;i<obj->material_count;i++) {
        fastObjMaterial *m=&obj->materials[i];
        if(!used_materials[i]) continue;
        if(m->d<1||m->map_d.path||m->map_Kt.path)
            REJECT(ATHENA_MODEL3D_EUNSUPPORTED,"OBJ material '%s' is transparent (d, map_d or map_Kt)",m->name?m->name:"");
        if(m->map_Kd.path) {
            if(texture_path&&strcmp(texture_path,m->map_Kd.path))
                REJECT(ATHENA_MODEL3D_EUNSUPPORTED,"OBJ uses several map_Kd textures; only one per file is supported");
            texture_path=m->map_Kd.path;
        }
    }
    uint32_t count=(uint32_t)corners;
    int all_normals=1,all_uvs=1;
    for(uint32_t i=0;i<obj->index_count;i++) {
        uint32_t n=obj->indices[i].n;
        if(n>=obj->normal_count) REJECT(ATHENA_MODEL3D_EFORMAT,"OBJ normal index %u out of range",(unsigned)n);
        if(!n) all_normals=0;
        uint32_t uv=obj->indices[i].t;
        if(uv>=obj->texcoord_count) REJECT(ATHENA_MODEL3D_EFORMAT,"OBJ texcoord index %u out of range",(unsigned)uv);
        if(!uv) all_uvs=0;
    }
    if(texture_path&&!material.texture) {
        if(!all_uvs) REJECT(ATHENA_MODEL3D_EFORMAT,"OBJ map_Kd '%s' needs texture coordinates on every vertex",texture_path);
        result=athena_texture3d_load_ex(texture_path,ATHENA_TEXTURE3D_LINEAR,ATHENA_TEXTURE3D_REPEAT,&loaded_texture);
        if(result<0) { athena_model3d_set_detail("cannot load map_Kd '%s'",texture_path); goto done; }
        material.texture=loaded_texture;
    }
    positions=malloc(count*3*sizeof(float));
    colors=malloc(count*4*sizeof(float));
    /* Mixed missing normals use consistent flat generation for the whole mesh. */
    if(all_normals) normals=malloc(count*3*sizeof(float));
    if(all_uvs) texcoords=malloc(count*2*sizeof(float));
    if(!positions||!colors||(all_normals&&!normals)||(all_uvs&&!texcoords)) { result=ATHENA_MODEL3D_ENOMEM; goto done; }
    uint32_t first=0,o=0;
    for(uint32_t f=0;f<obj->face_count;first+=obj->face_vertices[f],f++) {
        uint32_t material_index=obj->face_materials[f];
        for(uint32_t k=1;k+1<obj->face_vertices[f];k++) {
            const uint32_t corner[3]={first,first+k,first+k+1};
            for(int c=0;c<3;c++,o++) {
                const fastObjIndex *index=&obj->indices[corner[c]];
                if(index->p==0||index->p>=obj->position_count)
                    REJECT(ATHENA_MODEL3D_EFORMAT,"OBJ position index %u out of range",(unsigned)index->p);
                memcpy(&positions[o*3],&obj->positions[index->p*3],3*sizeof(float));
                if(normals) memcpy(&normals[o*3],&obj->normals[index->n*3],3*sizeof(float));
                if(texcoords) {
                    texcoords[o*2]=obj->texcoords[index->t*2];
                    texcoords[o*2+1]=1-obj->texcoords[index->t*2+1];
                }
                if(obj->material_count&&material_index<obj->material_count)
                    memcpy(&colors[o*4],obj->materials[material_index].Kd,3*sizeof(float));
                else colors[o*4]=colors[o*4+1]=colors[o*4+2]=1;
                colors[o*4+3]=1;
            }
        }
    }
    AthenaGeometry3D g={.positions=positions,.vertex_count=count,.colors=colors,.color_count=count,
        .normals=normals,.normal_count=normals?count:0,.material=&material,
        .texcoords=texcoords,.texcoord_count=texcoords?count:0};
    result=athena_mesh3d_create(&g,out);
    if(result==ATHENA_MODEL3D_EINVAL) {
        uint32_t offset; AthenaGeometry3DIssue issue=athena_geometry3d_validate(&g,&offset);
        athena_model3d_set_detail("OBJ geometry rejected (issue %d at offset %u)",(int)issue,(unsigned)offset);
        result=ATHENA_MODEL3D_EFORMAT;
    }
done:
    free(positions); free(colors); free(normals); free(texcoords); free(used_materials); fast_obj_destroy(obj);
    athena_texture3d_release(loaded_texture); return result;
}

void athena_model3d_gltf_cache_release(AthenaGltfTextureCache *cache) {
    if(!cache) return;
    for(uint32_t i=0;i<cache->count;i++) athena_texture3d_release(cache->items[i].texture);
    free(cache->items); cache->items=NULL; cache->count=cache->capacity=0;
}
int athena_model3d_gltf_extensions(const cgltf_data *data) {
    for(cgltf_size e=0;e<data->extensions_required_count;e++) {
        const char *name=data->extensions_required[e];
        /* Quantized attributes are read through cgltf_accessor_read_float. */
        if(strcmp(name,"KHR_materials_unlit")&&strcmp(name,"KHR_mesh_quantization")) {
            athena_model3d_set_detail("required extension %s is not supported",name);
            return ATHENA_MODEL3D_EUNSUPPORTED;
        }
    }
    return 0;
}
/* glTF sampler constants. */
enum { GL_NEAREST=9728, GL_LINEAR=9729, GL_NEAREST_MIPMAP_NEAREST=9984, GL_NEAREST_MIPMAP_LINEAR=9986,
    GL_CLAMP_TO_EDGE=33071, GL_MIRRORED_REPEAT=33648, GL_REPEAT=10497 };
/* The base color texture of a material: external image, sampled without
 * mipmaps (the base level of mipmapped filters); no sampler means repeat
 * and linear, as in glTF. *out is a new reference. */
static int gltf_texture(const char *path,const cgltf_texture *texture,AthenaGltfTextureCache *cache,AthenaTexture3D **out) {
    const cgltf_sampler *sampler=texture->sampler;
    int wrap_s=sampler&&sampler->wrap_s?sampler->wrap_s:GL_REPEAT,wrap_t=sampler&&sampler->wrap_t?sampler->wrap_t:GL_REPEAT;
    if(wrap_s==GL_MIRRORED_REPEAT||wrap_t==GL_MIRRORED_REPEAT) {
        athena_model3d_set_detail("MIRRORED_REPEAT texture wrap is not supported by the GS");
        return ATHENA_MODEL3D_EUNSUPPORTED;
    }
    if((wrap_s!=GL_REPEAT&&wrap_s!=GL_CLAMP_TO_EDGE)||(wrap_t!=GL_REPEAT&&wrap_t!=GL_CLAMP_TO_EDGE)) {
        athena_model3d_set_detail("invalid sampler wrap %d/%d",wrap_s,wrap_t);
        return ATHENA_MODEL3D_EFORMAT;
    }
    AthenaTexture3DWrap wrap=(wrap_s==GL_REPEAT?ATHENA_TEXTURE3D_REPEAT_U:0)|(wrap_t==GL_REPEAT?ATHENA_TEXTURE3D_REPEAT_V:0);
    int mag=sampler?sampler->mag_filter:0,min=sampler?sampler->min_filter:0;
    AthenaTexture3DFilter filter=mag==GL_NEAREST?ATHENA_TEXTURE3D_NEAREST:mag==GL_LINEAR?ATHENA_TEXTURE3D_LINEAR:
        min==GL_NEAREST||min==GL_NEAREST_MIPMAP_NEAREST||min==GL_NEAREST_MIPMAP_LINEAR?ATHENA_TEXTURE3D_NEAREST:ATHENA_TEXTURE3D_LINEAR;
    const cgltf_image *image=texture->image;
    if(!image) {
        athena_model3d_set_detail(texture->has_basisu?"KHR_texture_basisu images are not supported":
            texture->has_webp?"EXT_texture_webp images are not supported":"texture without an image");
        return ATHENA_MODEL3D_EUNSUPPORTED;
    }
    for(uint32_t i=0;cache&&i<cache->count;i++) {
        const AthenaGltfTextureEntry *e=&cache->items[i];
        if(e->image==image&&e->filter==filter&&e->wrap==wrap) { athena_texture3d_retain(e->texture); *out=e->texture; return 0; }
    }
    const char *uri=image->uri;
    AthenaTexture3D *loaded=NULL;
    int result;
    if(image->buffer_view||(uri&&!strncmp(uri,"data:",5))) {
        /* Embedded: a GLB bufferView or a base64 data URI. */
        const uint8_t *bytes=NULL; void *decoded=NULL; cgltf_size size=0;
        if(image->buffer_view) {
            bytes=cgltf_buffer_view_data(image->buffer_view); size=image->buffer_view->size;
        } else {
            const char *comma=strchr(uri,',');
            if(!comma||!strstr(uri,";base64,")) { athena_model3d_set_detail("data URI images must be base64"); return ATHENA_MODEL3D_EUNSUPPORTED; }
            size_t length=strlen(comma+1),padding=length&&comma[length]=='='?(length>1&&comma[length-1]=='='?2:1):0;
            size=length/4*3-padding;
            cgltf_options options={0};
            if(!size||cgltf_load_buffer_base64(&options,size,comma+1,&decoded)!=cgltf_result_success) {
                athena_model3d_set_detail("malformed base64 image data URI"); return ATHENA_MODEL3D_EFORMAT;
            }
            bytes=decoded;
        }
        if(!bytes||!size) { athena_model3d_set_detail("embedded image without data"); return ATHENA_MODEL3D_EFORMAT; }
        result=athena_texture3d_load_memory(bytes,size,filter,wrap,&loaded);
        free(decoded);
        if(result<0) { athena_model3d_set_detail("cannot decode embedded image"); return result; }
        goto cache;
    }
    if(!uri) { athena_model3d_set_detail("image without uri or bufferView"); return ATHENA_MODEL3D_EFORMAT; }
    if(cache&&cache->deps) {
        /* Explicit resolver: the caller already read the file. */
        char decoded_uri[512];
        if(strlen(uri)>=sizeof(decoded_uri)) { athena_model3d_set_detail("image uri too long"); return ATHENA_MODEL3D_EUNSUPPORTED; }
        strcpy(decoded_uri,uri); cgltf_decode_uri(decoded_uri);
        const AthenaGltfDependency *dep=NULL;
        for(uint32_t i=0;i<cache->deps->count;i++)
            if(!strcmp(cache->deps->items[i].uri,decoded_uri)) { dep=&cache->deps->items[i]; break; }
        if(!dep||!dep->bytes) { athena_model3d_set_detail("image '%s' was not provided",uri); return ATHENA_MODEL3D_EFORMAT; }
        result=athena_texture3d_load_memory(dep->bytes,dep->size,filter,wrap,&loaded);
        if(result<0) { athena_model3d_set_detail("cannot decode image '%s'",uri); return result; }
        goto cache;
    }
    if(strchr(uri,':')||uri[0]=='/'||uri[0]=='\\') {
        athena_model3d_set_detail("image uri '%s' must be relative to the model",uri);
        return ATHENA_MODEL3D_EUNSUPPORTED;
    }
    const char *slash=strrchr(path,'/'),*backslash=strrchr(path,'\\');
    if(backslash&&(!slash||backslash>slash)) slash=backslash;
    size_t prefix=slash?(size_t)(slash-path)+1:0;
    char image_path[1024];
    if(prefix+strlen(uri)>=sizeof(image_path)) { athena_model3d_set_detail("image path too long"); return ATHENA_MODEL3D_EUNSUPPORTED; }
    memcpy(image_path,path,prefix); strcpy(image_path+prefix,uri);
    cgltf_decode_uri(image_path+prefix); /* "my%20texture.png" */
    result=athena_texture3d_load_ex(image_path,filter,wrap,&loaded);
    if(result<0) { athena_model3d_set_detail("cannot load image '%s'",image_path); return result; }
cache:
    if(cache) {
        if(cache->count==cache->capacity) {
            uint32_t next=cache->capacity?cache->capacity*2:8;
            AthenaGltfTextureEntry *items=realloc(cache->items,next*sizeof(*items));
            if(!items) { athena_texture3d_release(loaded); return ATHENA_MODEL3D_ENOMEM; }
            cache->items=items; cache->capacity=next;
        }
        athena_texture3d_retain(loaded);
        cache->items[cache->count++]=(AthenaGltfTextureEntry){image,filter,wrap,loaded};
    }
    *out=loaded; return 0;
}

/* One triangle primitive of a parsed glTF whose buffers are loaded and
 * validated: base color factor and texture, positions, colors, normals,
 * texture coordinates and indices. Shared with the gltf3d scene loader.
 * The build is resumable: material and texture, accessor reads (positions,
 * normals, UVs, skin and colors per vertex, then indices), one morph target
 * attribute per step, then the Mesh3D builder's own stages. */
enum { P_MATERIAL=0, P_ATTRIBUTES, P_VERTICES, P_INDICES, P_TARGETS, P_MESH, P_DONE };
struct AthenaGltfPrimitiveBuild {
    const char *path; cgltf_primitive *prim; AthenaGltfTextureCache *cache;
    AthenaMaterial3D material; AthenaTexture3D *loaded_texture; float factor[4];
    const cgltf_accessor *pos,*col,*normal,*uv,*joint,*weight; cgltf_size count;
    float *positions,*colors,*normals,*texcoords,*weights; uint32_t *indices; uint16_t *joints;
    float *target_positions,*target_normals; int target_has_normals;
    AthenaGeometry3D g; AthenaMesh3DBuilder *mesh;
    int stage,failed; cgltf_size i,t,a;
};
void athena_model3d_gltf_primitive_destroy(AthenaGltfPrimitiveBuild *b) {
    if(!b) return;
    athena_mesh3d_builder_destroy(b->mesh);
    free(b->positions); free(b->colors); free(b->normals); free(b->texcoords); free(b->indices); free(b->joints); free(b->weights);
    free(b->target_positions); free(b->target_normals);
    athena_texture3d_release(b->loaded_texture); free(b);
}
int athena_model3d_gltf_primitive_begin(const char *path,cgltf_primitive *prim,
    const AthenaMaterial3D *material_override,AthenaGltfTextureCache *cache,AthenaGltfPrimitiveBuild **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    AthenaGltfPrimitiveBuild *b=calloc(1,sizeof(*b));
    if(!b) return ATHENA_MODEL3D_ENOMEM;
    b->path=path; b->prim=prim; b->cache=cache;
    athena_material3d_default(&b->material);
    if(material_override) b->material=*material_override;
    *out=b; return 0;
}
#define FAIL(code,...) do { athena_model3d_set_detail(__VA_ARGS__); return (code); } while(0)
static int primitive_material(AthenaGltfPrimitiveBuild *b) {
    cgltf_primitive *prim=b->prim; AthenaMaterial3D *material=&b->material;
    if(prim->type!=cgltf_primitive_type_triangles) {
        athena_model3d_set_detail("primitive mode %d is not supported; export triangles",(int)prim->type);
        return ATHENA_MODEL3D_EUNSUPPORTED;
    }
    if(prim->targets_count>ATHENA_MODEL3D_MAX_TARGETS) {
        athena_model3d_set_detail("%u morph targets, at most %u",(unsigned)prim->targets_count,ATHENA_MODEL3D_MAX_TARGETS);
        return ATHENA_MODEL3D_EUNSUPPORTED;
    }
    if(prim->has_draco_mesh_compression) { athena_model3d_set_detail("KHR_draco_mesh_compression is not supported"); return ATHENA_MODEL3D_EUNSUPPORTED; }
    b->factor[0]=b->factor[1]=b->factor[2]=b->factor[3]=1;
    if(prim->material) {
        /* Only the base colour has a Gouraud equivalent. Lighting-only data
         * (normal, metallic-roughness and occlusion maps, specular, sheen,
         * clearcoat, IOR...) is ignored; whatever changes the visible colour
         * or coverage is rejected with its name. */
        cgltf_material *mat=prim->material;
        const char *name=mat->name?mat->name:"";
        if(mat->alpha_mode==cgltf_alpha_mode_blend)
            FAIL(ATHENA_MODEL3D_EUNSUPPORTED,"material '%s': alphaMode BLEND is not supported (MASK is)",name);
        /* MASK: GS alpha test at alphaCutoff. Model3D.load() always passes
         * a material (UNLIT by default): only one with its own mask wins. */
        if(mat->alpha_mode==cgltf_alpha_mode_mask&&!material->alpha_mask) {
            if(!(mat->alpha_cutoff>=0&&mat->alpha_cutoff<=1))
                FAIL(ATHENA_MODEL3D_EFORMAT,"material '%s': alphaCutoff outside [0,1]",name);
            material->alpha_mask=1; material->alpha_cutoff=mat->alpha_cutoff;
        }
        if(mat->emissive_texture.texture||mat->emissive_factor[0]!=0||mat->emissive_factor[1]!=0||mat->emissive_factor[2]!=0)
            FAIL(ATHENA_MODEL3D_EUNSUPPORTED,"material '%s': emissive colour is not supported",name);
        if(mat->has_pbr_specular_glossiness)
            FAIL(ATHENA_MODEL3D_EUNSUPPORTED,"material '%s': KHR_materials_pbrSpecularGlossiness is not supported",name);
        if(mat->has_transmission||mat->has_volume||mat->has_diffuse_transmission)
            FAIL(ATHENA_MODEL3D_EUNSUPPORTED,"material '%s': transmission/volume is not supported",name);
        if(mat->has_pbr_metallic_roughness) memcpy(b->factor,mat->pbr_metallic_roughness.base_color_factor,sizeof(b->factor));
        cgltf_texture_view *view=&mat->pbr_metallic_roughness.base_color_texture;
        if(view->texture&&!material->texture) {
            if(view->texcoord!=0)
                FAIL(ATHENA_MODEL3D_EUNSUPPORTED,"material '%s': baseColorTexture uses TEXCOORD_%d; only TEXCOORD_0",name,view->texcoord);
            if(view->has_transform)
                FAIL(ATHENA_MODEL3D_EUNSUPPORTED,"material '%s': KHR_texture_transform is not supported",name);
            int result=gltf_texture(b->path,view->texture,b->cache,&b->loaded_texture);
            if(result<0) return result;
            material->texture=b->loaded_texture;
        }
    }
    b->stage=P_ATTRIBUTES; return 0;
}
static int primitive_attributes(AthenaGltfPrimitiveBuild *b) {
    cgltf_primitive *prim=b->prim;
    const cgltf_accessor *pos=NULL,*col=NULL,*normal=NULL,*uv=NULL,*joint=NULL,*weight=NULL;
    for(cgltf_size a=0;a<prim->attributes_count;a++) {
        if(prim->attributes[a].type==cgltf_attribute_type_position) pos=prim->attributes[a].data;
        if(prim->attributes[a].type==cgltf_attribute_type_color && prim->attributes[a].index==0) col=prim->attributes[a].data;
        if(prim->attributes[a].type==cgltf_attribute_type_normal && prim->attributes[a].index==0) normal=prim->attributes[a].data;
        if(prim->attributes[a].type==cgltf_attribute_type_texcoord && prim->attributes[a].index==0) uv=prim->attributes[a].data;
        if(prim->attributes[a].type==cgltf_attribute_type_joints && prim->attributes[a].index==0) joint=prim->attributes[a].data;
        if(prim->attributes[a].type==cgltf_attribute_type_weights && prim->attributes[a].index==0) weight=prim->attributes[a].data;
    }
    if(!pos||pos->type!=cgltf_type_vec3||pos->count==0||pos->count>ATHENA_MODEL3D_MAX_VERTICES||
        (col&&(col->count!=pos->count||(col->type!=cgltf_type_vec3&&col->type!=cgltf_type_vec4)))||
        (normal&&(normal->count!=pos->count||normal->type!=cgltf_type_vec3))||
        (uv&&(uv->count!=pos->count||uv->type!=cgltf_type_vec2))||
        (!joint)!=(!weight)||(joint&&(joint->count!=pos->count||joint->type!=cgltf_type_vec4||
            weight->count!=pos->count||weight->type!=cgltf_type_vec4)))
        FAIL(ATHENA_MODEL3D_EFORMAT,"primitive attributes: missing POSITION, or counts/types disagree (%u vertices, max %u)",
            pos?(unsigned)pos->count:0,ATHENA_MODEL3D_MAX_VERTICES);
    cgltf_size count=prim->indices?prim->indices->count:pos->count;
    if(!count||count%3||count>ATHENA_MODEL3D_MAX_VERTICES)
        FAIL(count>ATHENA_MODEL3D_MAX_VERTICES?ATHENA_MODEL3D_EUNSUPPORTED:ATHENA_MODEL3D_EFORMAT,
            "primitive has %u indices; a multiple of 3 up to %u is required",(unsigned)count,ATHENA_MODEL3D_MAX_VERTICES);
    if(pos->is_sparse||(col&&col->is_sparse)||(normal&&normal->is_sparse)||(uv&&uv->is_sparse)||(prim->indices&&prim->indices->is_sparse)||
        (joint&&(joint->is_sparse||weight->is_sparse)))
        FAIL(ATHENA_MODEL3D_EUNSUPPORTED,"sparse vertex accessors are not supported");
    b->pos=pos; b->col=col; b->normal=normal; b->uv=uv; b->joint=joint; b->weight=weight; b->count=count;
    b->positions=malloc(pos->count*3*sizeof(float)); b->colors=malloc(pos->count*4*sizeof(float));
    if(normal) b->normals=malloc(pos->count*3*sizeof(float));
    if(uv) b->texcoords=malloc(pos->count*2*sizeof(float));
    if(prim->indices) b->indices=malloc(count*sizeof(uint32_t));
    if(joint) { b->joints=malloc(pos->count*4*sizeof(uint16_t)); b->weights=malloc(pos->count*4*sizeof(float)); }
    if(!b->positions||!b->colors||(normal&&!b->normals)||(uv&&!b->texcoords)||(prim->indices&&!b->indices)||(joint&&(!b->joints||!b->weights)))
        return ATHENA_MODEL3D_ENOMEM;
    /* Morph targets: POSITION and NORMAL deltas (absent ones are zero;
     * TANGENT is ignored). unpack_floats also expands sparse accessors,
     * the usual encoding of targets that move few vertices. */
    for(cgltf_size t=0;t<prim->targets_count;t++) for(cgltf_size a=0;a<prim->targets[t].attributes_count;a++)
        if(prim->targets[t].attributes[a].type==cgltf_attribute_type_normal) b->target_has_normals=1;
    if(prim->targets_count) {
        b->target_positions=calloc(prim->targets_count*pos->count*3,sizeof(float));
        if(normal&&b->target_has_normals) b->target_normals=calloc(prim->targets_count*pos->count*3,sizeof(float));
        if(!b->target_positions||(normal&&b->target_has_normals&&!b->target_normals)) return ATHENA_MODEL3D_ENOMEM;
    }
    b->stage=P_VERTICES; return 0;
}
/* Accessor failures: the generic reason, kept only if nothing more specific was set. */
static int malformed(void) { athena_model3d_set_detail("malformed primitive accessor data"); return ATHENA_MODEL3D_EFORMAT; }
static int primitive_run(AthenaGltfPrimitiveBuild *b,uint32_t budget) {
    while(budget) {
        switch(b->stage) {
        case P_MATERIAL: { int code=primitive_material(b); if(code<0) return code==ATHENA_MODEL3D_EFORMAT?(malformed(),code):code; return 0; }
        case P_ATTRIBUTES: { int code=primitive_attributes(b); if(code<0) return code==ATHENA_MODEL3D_EFORMAT?(malformed(),code):code; return 0; }
        case P_VERTICES: {
            const cgltf_size n=b->pos->count;
            while(budget&&b->i<n) {
                cgltf_size i=b->i++; budget--;
                if(!cgltf_accessor_read_float(b->pos,i,&b->positions[i*3],3)) return malformed();
                if(b->normal&&!cgltf_accessor_read_float(b->normal,i,&b->normals[i*3],3)) return malformed();
                if(b->uv&&!cgltf_accessor_read_float(b->uv,i,&b->texcoords[i*2],2)) return malformed();
                if(b->joint) {
                    cgltf_uint j[4];
                    if(!cgltf_accessor_read_uint(b->joint,i,j,4)||!cgltf_accessor_read_float(b->weight,i,&b->weights[i*4],4)) return malformed();
                    for(int k=0;k<4;k++) { if(j[k]>0xffff) return malformed(); b->joints[i*4+k]=(uint16_t)j[k]; }
                }
                float c[4]={1,1,1,1};
                if(b->col&&!cgltf_accessor_read_float(b->col,i,c,b->col->type==cgltf_type_vec3?3:4)) return malformed();
                for(int j=0;j<4;j++) b->colors[i*4+j]=c[j]*b->factor[j];
            }
            if(b->i==n) { b->stage=P_INDICES; b->i=0; }
            break;
        }
        case P_INDICES:
            while(b->indices&&budget&&b->i<b->count) {
                cgltf_size i=b->i++; budget--;
                cgltf_size index=cgltf_accessor_read_index(b->prim->indices,i);
                if(index>=b->pos->count) return malformed();
                b->indices[i]=index;
            }
            if(!b->indices||b->i==b->count) { b->stage=P_TARGETS; b->t=b->a=0; }
            break;
        case P_TARGETS: {
            cgltf_primitive *prim=b->prim;
            if(b->t>=prim->targets_count) { b->stage=P_MESH; break; }
            const cgltf_morph_target *target=&prim->targets[b->t];
            if(b->a>=target->attributes_count) { b->t++; b->a=0; break; }
            const cgltf_attribute *attribute=&target->attributes[b->a++];
            float *out=attribute->type==cgltf_attribute_type_position?&b->target_positions[b->t*b->pos->count*3]:
                attribute->type==cgltf_attribute_type_normal&&b->target_normals?&b->target_normals[b->t*b->pos->count*3]:NULL;
            budget=budget>b->pos->count?(uint32_t)(budget-b->pos->count):0;
            if(!out) break;
            const cgltf_accessor *d=attribute->data;
            if(d->type!=cgltf_type_vec3||d->count!=b->pos->count||
                cgltf_accessor_unpack_floats(d,out,b->pos->count*3)!=b->pos->count*3) return malformed();
            break;
        }
        case P_MESH: {
            if(!b->mesh) {
                const cgltf_primitive *prim=b->prim;
                b->g=(AthenaGeometry3D){.positions=b->positions,.vertex_count=(uint32_t)b->pos->count,.colors=b->colors,.color_count=(uint32_t)b->pos->count,
                    .indices=b->indices,.index_count=b->indices?(uint32_t)b->count:0,.normals=b->normals,.normal_count=b->normal?(uint32_t)b->pos->count:0,
                    .material=&b->material,.texcoords=b->texcoords,.texcoord_count=b->uv?(uint32_t)b->pos->count:0,
                    .joints=b->joints,.weights=b->weights,.skin_count=b->joint?(uint32_t)b->pos->count:0,
                    .target_positions=b->target_positions,.target_normals=b->target_normals,.target_count=(uint32_t)prim->targets_count};
                int code=athena_mesh3d_builder_begin(&b->g,&b->mesh);
                if(code<0) return code;
            }
            int code=athena_mesh3d_builder_step(b->mesh,budget);
            if(code==ATHENA_MODEL3D_EINVAL) {
                uint32_t offset; AthenaGeometry3DIssue issue=athena_geometry3d_validate(&b->g,&offset);
                if(issue==ATHENA_GEOMETRY3D_TEXCOORD)
                    athena_model3d_set_detail("UV component %u is outside [-%g, %g]",(unsigned)offset,(double)ATHENA_MODEL3D_UV_LIMIT,(double)ATHENA_MODEL3D_UV_LIMIT);
                else if(issue==ATHENA_GEOMETRY3D_NORMAL) athena_model3d_set_detail("invalid normal or degenerate triangle at %u",(unsigned)offset);
                else if(issue==ATHENA_GEOMETRY3D_SKIN) athena_model3d_set_detail("invalid joint index or weight at %u",(unsigned)offset);
                else athena_model3d_set_detail("invalid geometry at offset %u (issue %d)",(unsigned)offset,(int)issue);
                return ATHENA_MODEL3D_EFORMAT;
            }
            if(code==1) { b->stage=P_DONE; return 1; }
            return code;
        }
        default: return 1;
        }
    }
    return b->stage==P_DONE?1:0;
}
int athena_model3d_gltf_primitive_step(AthenaGltfPrimitiveBuild *b,uint32_t max_items) {
    if(!b) return ATHENA_MODEL3D_EINVAL;
    if(b->failed) return b->failed;
    if(b->stage==P_DONE) return 1;
    int code=primitive_run(b,max_items?max_items:ATHENA_MESH3D_BUILD_ITEMS);
    if(code<0) b->failed=code;
    return code;
}
int athena_model3d_gltf_primitive_take(AthenaGltfPrimitiveBuild *b,AthenaMesh3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    if(!b||b->failed||b->stage!=P_DONE) return ATHENA_MODEL3D_EINVAL;
    return athena_mesh3d_builder_take(b->mesh,out);
}
int athena_model3d_gltf_primitive(const char *path,cgltf_primitive *prim,
    const AthenaMaterial3D *material_override,AthenaGltfTextureCache *cache,AthenaMesh3D **out) {
    *out=NULL;
    AthenaGltfPrimitiveBuild *b=NULL;
    int code=athena_model3d_gltf_primitive_begin(path,prim,material_override,cache,&b);
    if(code<0) return code;
    do code=athena_model3d_gltf_primitive_step(b,UINT32_MAX); while(code==0);
    if(code==1) code=athena_model3d_gltf_primitive_take(b,out);
    athena_model3d_gltf_primitive_destroy(b);
    return code;
}


void athena_model3d_gltf_deps_release(AthenaGltfDependencies *deps) {
    if(!deps) return;
    for(uint32_t i=0;i<deps->count;i++) free(deps->items[i].uri);
    free(deps->items); deps->items=NULL; deps->count=deps->provided=0;
}
static int deps_add(AthenaGltfDependencies *deps,const char *uri,int image) {
    if(strncmp(uri,"data:",5)==0) return 0;
    if(strchr(uri,':')||uri[0]=='/'||uri[0]=='\\') {
        athena_model3d_set_detail("uri '%s' must be relative to the model",uri); return ATHENA_MODEL3D_EUNSUPPORTED;
    }
    size_t n=strlen(uri);
    if(n>=512) { athena_model3d_set_detail("uri too long"); return ATHENA_MODEL3D_EUNSUPPORTED; }
    char *copy=malloc(n+1); if(!copy) return ATHENA_MODEL3D_ENOMEM;
    memcpy(copy,uri,n+1); cgltf_decode_uri(copy);
    for(const char *p=copy;*p;p++) if((p==copy||p[-1]=='/'||p[-1]=='\\')&&p[0]=='.'&&p[1]=='.'&&(!p[2]||p[2]=='/'||p[2]=='\\')) {
        athena_model3d_set_detail("uri '%s' leaves the model folder",uri); free(copy); return ATHENA_MODEL3D_EUNSUPPORTED;
    }
    for(uint32_t i=0;i<deps->count;i++) if(!strcmp(deps->items[i].uri,copy)) {
        if(deps->items[i].image!=image) deps->items[i].image=0; /* used as both: informational kind is `buffer` */
        free(copy); return 0;
    }
    if(deps->count>=ATHENA_GLTF_MAX_DEPENDENCIES) {
        athena_model3d_set_detail("more than %u external files",ATHENA_GLTF_MAX_DEPENDENCIES); free(copy); return ATHENA_MODEL3D_EUNSUPPORTED;
    }
    AthenaGltfDependency *items=realloc(deps->items,(deps->count+1)*sizeof(*items));
    if(!items) { free(copy); return ATHENA_MODEL3D_ENOMEM; }
    deps->items=items; deps->items[deps->count++]=(AthenaGltfDependency){.uri=copy,.image=image};
    return 0;
}
int athena_model3d_gltf_parse_external(const void *bytes,size_t size,cgltf_data **out,AthenaGltfDependencies *deps) {
    *out=NULL;
    if(!bytes||size<4||size>ATHENA_MODEL3D_MEMORY_MAX_BYTES||(uintptr_t)bytes%4) return ATHENA_MODEL3D_EINVAL;
    uint32_t magic; memcpy(&magic,bytes,4);
    cgltf_options options={0}; options.type=magic==0x46546c67u?cgltf_file_type_glb:cgltf_file_type_gltf;
    if(options.type==cgltf_file_type_glb) {
        uint32_t length,json_length;
        if(size<20) return ATHENA_MODEL3D_EFORMAT;
        memcpy(&length,(const uint8_t *)bytes+8,4); memcpy(&json_length,(const uint8_t *)bytes+12,4);
        if(length!=size||json_length%4) {
            athena_model3d_set_detail("memory input must be a complete GLB with aligned chunks"); return ATHENA_MODEL3D_EFORMAT;
        }
    }
    cgltf_data *data=NULL;
    cgltf_result parsed=cgltf_parse(&options,bytes,size,&data);
    if(parsed!=cgltf_result_success) {
        athena_model3d_set_detail("glTF memory parse error %d",(int)parsed);
        return parsed==cgltf_result_out_of_memory?ATHENA_MODEL3D_ENOMEM:ATHENA_MODEL3D_EFORMAT;
    }
    int code=0;
    for(cgltf_size i=0;!code&&i<data->buffers_count;i++) {
        if(data->buffers[i].uri) code=deps_add(deps,data->buffers[i].uri,0);
        else if(i!=0||!data->bin) { athena_model3d_set_detail("buffer %u has no uri and no GLB BIN",(unsigned)i); code=ATHENA_MODEL3D_EFORMAT; }
    }
    for(cgltf_size i=0;!code&&i<data->images_count;i++)
        if(data->images[i].uri&&!data->images[i].buffer_view) code=deps_add(deps,data->images[i].uri,1);
    if(code<0) { cgltf_free(data); return code; }
    *out=data; return 0;
}
int athena_model3d_gltf_deps_provide(AthenaGltfDependencies *deps,uint32_t index,const void *bytes,size_t size) {
    if(!deps||index>=deps->count||deps->items[index].bytes||!bytes||!size||size>ATHENA_MODEL3D_MEMORY_MAX_BYTES) return ATHENA_MODEL3D_EINVAL;
    deps->items[index].bytes=bytes; deps->items[index].size=size; deps->provided++;
    return 0;
}
int athena_model3d_gltf_bind_buffers(cgltf_data *data,const AthenaGltfDependencies *deps) {
    cgltf_options options={0};
    for(cgltf_size i=0;i<data->buffers_count;i++) {
        cgltf_buffer *b=&data->buffers[i];
        if(!b->uri) {
            if(i!=0||!data->bin||data->bin_size<b->size) { athena_model3d_set_detail("buffer %u: GLB BIN is missing or too small",(unsigned)i); return ATHENA_MODEL3D_EFORMAT; }
            b->data=(void *)data->bin; b->data_free_method=cgltf_data_free_method_none; continue;
        }
        if(!strncmp(b->uri,"data:",5)) {
            const char *comma=strchr(b->uri,',');
            if(!comma||comma-b->uri<7||strncmp(comma-7,";base64",7)||
                cgltf_load_buffer_base64(&options,b->size,comma+1,&b->data)!=cgltf_result_success) {
                athena_model3d_set_detail("buffer %u: malformed base64 data URI",(unsigned)i); return ATHENA_MODEL3D_EFORMAT;
            }
            b->data_free_method=cgltf_data_free_method_memory_free; continue;
        }
        char decoded[512]; strcpy(decoded,b->uri); cgltf_decode_uri(decoded);
        const AthenaGltfDependency *dep=NULL;
        for(uint32_t k=0;deps&&k<deps->count;k++) if(!strcmp(deps->items[k].uri,decoded)) { dep=&deps->items[k]; break; }
        if(!dep||!dep->bytes||dep->size<b->size) {
            athena_model3d_set_detail("buffer '%s' was not provided or is smaller than the glTF says",b->uri); return ATHENA_MODEL3D_EFORMAT;
        }
        b->data=(void *)dep->bytes; b->data_free_method=cgltf_data_free_method_none;
    }
    return 0;
}
int athena_model3d_gltf_parse_memory(const void *bytes,size_t size,cgltf_data **out) {
    *out=NULL;
    if(!bytes||size<20||size>ATHENA_MODEL3D_MEMORY_MAX_BYTES) return ATHENA_MODEL3D_EINVAL;
    uint32_t magic,length,json_length;
    memcpy(&magic,bytes,4); memcpy(&length,(const uint8_t *)bytes+8,4);
    memcpy(&json_length,(const uint8_t *)bytes+12,4);
    if(magic!=0x46546c67u||length!=size||json_length%4) {
        athena_model3d_set_detail("memory input must be a complete GLB with aligned chunks");
        return ATHENA_MODEL3D_EFORMAT;
    }
    void *copy=NULL;
    if((uintptr_t)bytes%4) {
        copy=malloc(size); if(!copy) return ATHENA_MODEL3D_ENOMEM;
        memcpy(copy,bytes,size); bytes=copy;
    }
    cgltf_options options={0}; options.type=cgltf_file_type_glb;
    cgltf_data *data=NULL;
    cgltf_result parsed=cgltf_parse(&options,bytes,size,&data);
    if(parsed!=cgltf_result_success) {
        free(copy); athena_model3d_set_detail("GLB memory parse error %d",(int)parsed);
        return parsed==cgltf_result_out_of_memory?ATHENA_MODEL3D_ENOMEM:ATHENA_MODEL3D_EFORMAT;
    }
    data->file_data=copy;
    int code=0;
    for(cgltf_size i=0;i<data->buffers_count;i++) if(data->buffers[i].uri) {
        athena_model3d_set_detail("GLB memory buffer %u has a URI; use an embedded BIN",(unsigned)i);
        code=ATHENA_MODEL3D_EUNSUPPORTED; goto done;
    }
    for(cgltf_size i=0;i<data->images_count;i++)
        if(data->images[i].uri&&strncmp(data->images[i].uri,"data:",5)) {
            athena_model3d_set_detail("GLB memory image '%s' is external; embed it before loading",data->images[i].uri);
            code=ATHENA_MODEL3D_EUNSUPPORTED; goto done;
        }
    parsed=cgltf_load_buffers(&options,data,NULL);
    if(parsed!=cgltf_result_success) { code=ATHENA_MODEL3D_EFORMAT; goto done; }
    for(cgltf_size i=0;i<data->buffers_count;i++) if(!data->buffers[i].data) {
        athena_model3d_set_detail("GLB memory buffer %u has no embedded data",(unsigned)i);
        code=ATHENA_MODEL3D_EFORMAT; goto done;
    }
done:
    if(code<0) { cgltf_free(data); return code; }
    *out=data; return 0;
}
/* The checks of the one-static-mesh subset, after parsing and before buffers. */
static int static_mesh_checks(const cgltf_data *data) {
    int result;
    if((result=athena_model3d_gltf_extensions(data))<0) return result;
    if(data->skins_count||data->animations_count) {
        athena_model3d_set_detail("skins and animations need Gltf3D.load (Model3D.load reads one static mesh)");
        return ATHENA_MODEL3D_EUNSUPPORTED;
    }
    if(data->meshes_count!=1||data->meshes[0].primitives_count!=1) {
        athena_model3d_set_detail("%u meshes / %u primitives: Model3D.load reads exactly one; use Gltf3D.load",
            (unsigned)data->meshes_count,data->meshes_count?(unsigned)data->meshes[0].primitives_count:0);
        return ATHENA_MODEL3D_EUNSUPPORTED;
    }
    for(cgltf_size n=0;n<data->nodes_count;n++) {
        const cgltf_node *node=&data->nodes[n];
        if(node->has_matrix||node->has_translation||node->has_rotation||node->has_scale||node->skin||
            node->has_mesh_gpu_instancing) {
            athena_model3d_set_detail("node '%s' has a transform; use Gltf3D.load for node hierarchies",node->name?node->name:"");
            return ATHENA_MODEL3D_EUNSUPPORTED;
        }
    }
    if(data->meshes[0].primitives[0].targets_count) {
        athena_model3d_set_detail("morph targets need Gltf3D.load");
        return ATHENA_MODEL3D_EUNSUPPORTED;
    }
    return 0;
}
static int load_gltf(const char *path,const void *bytes,size_t size,
    const AthenaMaterial3D *material_override,AthenaMesh3D **out) {
    cgltf_options options={0}; cgltf_data *data=NULL;
    int result=ATHENA_MODEL3D_EFORMAT;
    AthenaGltfTextureCache cache={0};
    if(bytes) {
        int code=athena_model3d_gltf_parse_memory(bytes,size,&data);
        if(code<0) return code;
    }
    cgltf_result parsed=bytes?cgltf_result_success:cgltf_parse_file(&options,path,&data);
    if(parsed!=cgltf_result_success) {
        athena_model3d_set_detail("glTF parse error %d",(int)parsed);
        return parsed==cgltf_result_out_of_memory?ATHENA_MODEL3D_ENOMEM:ATHENA_MODEL3D_EFORMAT;
    }
    if((result=static_mesh_checks(data))<0) goto done;
    result=ATHENA_MODEL3D_EUNSUPPORTED;
    cgltf_primitive *prim=&data->meshes[0].primitives[0];
    parsed=bytes?cgltf_result_success:cgltf_load_buffers(&options,data,path);
    if(parsed!=cgltf_result_success)
        REJECT(parsed==cgltf_result_out_of_memory?ATHENA_MODEL3D_ENOMEM:ATHENA_MODEL3D_EIO,"cannot load glTF buffers (error %d)",(int)parsed);
    if(cgltf_validate(data)!=cgltf_result_success) REJECT(ATHENA_MODEL3D_EFORMAT,"glTF validation failed");
    result=athena_model3d_gltf_primitive(path,prim,material_override,&cache,out);
done:
    athena_model3d_gltf_cache_release(&cache); cgltf_free(data); return result;
}

/* Resumable GLB mesh load: copy slices, parse and checks, the primitive build. */
struct AthenaMesh3DLoader {
    char *origin; unsigned char *owned; const void *source,*bytes; size_t size,copied; int borrowed;
    AthenaLoaderPhaseStats stats[ATHENA_MESH3D_LOADER_DONE+1];
    AthenaMaterial3D material; int override;
    cgltf_data *data; AthenaGltfTextureCache cache; AthenaGltfPrimitiveBuild *build; AthenaMesh3D *mesh;
    int phase,last_phase,code,unit_done; uint32_t units;
    int external; AthenaGltfDependencies deps;
};
int athena_mesh3d_loader_begin(const void *bytes,size_t size,const char *origin,
    const AthenaMaterial3D *material,int flags,AthenaMesh3DLoader **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL; athena_model3d_clear_detail();
    if(!bytes||!origin||!*origin||!size||size>ATHENA_MODEL3D_MEMORY_MAX_BYTES||
        (material&&!athena_material3d_validate(material))) return ATHENA_MODEL3D_EINVAL;
    AthenaMesh3DLoader *l=calloc(1,sizeof(*l));
    if(!l) return ATHENA_MODEL3D_ENOMEM;
    int borrow=(flags&ATHENA_LOADER_BORROW)&&!((uintptr_t)bytes%4);
    size_t n=strlen(origin)+1;
    l->origin=malloc(n); l->owned=borrow?NULL:malloc(size);
    if(!l->origin||(!borrow&&!l->owned)) { free(l->origin); free(l->owned); free(l); return ATHENA_MODEL3D_ENOMEM; }
    memcpy(l->origin,origin,n); l->size=size; l->borrowed=borrow;
    if(borrow) { l->bytes=bytes; l->phase=l->last_phase=ATHENA_MESH3D_LOADER_PARSE; }
    else { l->source=bytes; l->bytes=l->owned; }
    athena_material3d_default(&l->material);
    if(material) { l->material=*material; l->override=1; athena_texture3d_retain(l->material.texture); }
    if(flags&ATHENA_LOADER_EXTERNAL) { l->external=1; l->cache.deps=&l->deps; }
    *out=l; return 0;
}
int athena_mesh3d_loader_begin_memory(const void *bytes,size_t size,const char *origin,
    const AthenaMaterial3D *material,AthenaMesh3DLoader **out) {
    return athena_mesh3d_loader_begin(bytes,size,origin,material,0,out);
}
void athena_mesh3d_loader_destroy(AthenaMesh3DLoader *l) {
    if(!l) return;
    athena_model3d_gltf_primitive_destroy(l->build); athena_mesh3d_release(l->mesh);
    athena_model3d_gltf_cache_release(&l->cache); cgltf_free(l->data); athena_model3d_gltf_deps_release(&l->deps); free(l->owned);
    athena_texture3d_release(l->material.texture); free(l->origin); free(l);
}
int athena_mesh3d_loader_begin_external(const void *bytes,size_t size,const char *origin,
    const AthenaMaterial3D *material,AthenaMesh3DLoader **out) {
    return athena_mesh3d_loader_begin(bytes,size,origin,material,ATHENA_LOADER_EXTERNAL,out);
}
uint32_t athena_mesh3d_loader_dependency_count(const AthenaMesh3DLoader *l) { return l?l->deps.count:0; }
const char *athena_mesh3d_loader_dependency_uri(const AthenaMesh3DLoader *l,uint32_t i) { return l&&i<l->deps.count?l->deps.items[i].uri:NULL; }
int athena_mesh3d_loader_dependency_image(const AthenaMesh3DLoader *l,uint32_t i) { return l&&i<l->deps.count&&l->deps.items[i].image; }
int athena_mesh3d_loader_dependency_provided(const AthenaMesh3DLoader *l,uint32_t i) { return l&&i<l->deps.count&&l->deps.items[i].bytes!=NULL; }
int athena_mesh3d_loader_provide(AthenaMesh3DLoader *l,uint32_t index,const void *bytes,size_t size) {
    if(!l||l->phase!=ATHENA_MESH3D_LOADER_DEPENDENCIES) return ATHENA_MODEL3D_EINVAL;
    return athena_model3d_gltf_deps_provide(&l->deps,index,bytes,size);
}
int athena_mesh3d_loader_source_needed(const AthenaMesh3DLoader *l) { return l&&(l->source!=NULL||l->borrowed)&&l->code>=0; }
int athena_mesh3d_loader_phase(const AthenaMesh3DLoader *l) { return l?l->phase:0; }
int athena_mesh3d_loader_last_phase(const AthenaMesh3DLoader *l) { return l?l->last_phase:0; }
void athena_mesh3d_loader_progress(const AthenaMesh3DLoader *l,uint32_t *done,uint32_t *total) {
    if(done) *done=l?l->units:0;
    /* copy slices, parse, validate, the primitive (one unit) */
    if(total) *total=l?(uint32_t)((l->owned?(l->size+ATHENA_GLTF3D_COPY_CHUNK_BYTES-1)/ATHENA_GLTF3D_COPY_CHUNK_BYTES:0)+3):0; /* dependency waits are not units */
}
static int mesh_loader_run(AthenaMesh3DLoader *l,uint32_t items) {
    switch(l->phase) {
    case ATHENA_MESH3D_LOADER_COPY: {
        size_t n=l->size-l->copied;
        if(n>ATHENA_GLTF3D_COPY_CHUNK_BYTES) n=ATHENA_GLTF3D_COPY_CHUNK_BYTES;
        memcpy(l->owned+l->copied,(const unsigned char *)l->source+l->copied,n); l->copied+=n;
        if(l->copied==l->size) { l->source=NULL; l->phase=ATHENA_MESH3D_LOADER_PARSE; }
        return 0;
    }
    case ATHENA_MESH3D_LOADER_PARSE: {
        int code=l->external?athena_model3d_gltf_parse_external(l->bytes,l->size,&l->data,&l->deps):
            athena_model3d_gltf_parse_memory(l->bytes,l->size,&l->data);
        if(code<0) return code;
        if((code=static_mesh_checks(l->data))<0) return code;
        l->phase=l->deps.count?ATHENA_MESH3D_LOADER_DEPENDENCIES:ATHENA_MESH3D_LOADER_VALIDATE; return 0;
    }
    case ATHENA_MESH3D_LOADER_DEPENDENCIES:
        l->unit_done=0;
        if(l->deps.provided<l->deps.count) return ATHENA_MESH3D_WAITING;
        l->phase=ATHENA_MESH3D_LOADER_VALIDATE; return 0;
    case ATHENA_MESH3D_LOADER_VALIDATE:
        if(l->external) { int code=athena_model3d_gltf_bind_buffers(l->data,&l->deps); if(code<0) return code; }
        if(cgltf_validate(l->data)!=cgltf_result_success) { athena_model3d_set_detail("glTF validation failed"); return ATHENA_MODEL3D_EFORMAT; }
        l->phase=ATHENA_MESH3D_LOADER_BUILD; return 0;
    case ATHENA_MESH3D_LOADER_BUILD: {
        int code;
        if(!l->build) {
            code=athena_model3d_gltf_primitive_begin(l->origin,&l->data->meshes[0].primitives[0],l->override?&l->material:NULL,&l->cache,&l->build);
            if(code<0) return code;
        }
        code=athena_model3d_gltf_primitive_step(l->build,items);
        if(code<=0) { l->unit_done=0; return code; }
        code=athena_model3d_gltf_primitive_take(l->build,&l->mesh);
        athena_model3d_gltf_primitive_destroy(l->build); l->build=NULL;
        if(code<0) return code;
        l->phase=ATHENA_MESH3D_LOADER_DONE; return 0;
    }
    default: return 0;
    }
}
int athena_mesh3d_loader_step(AthenaMesh3DLoader *l,uint32_t max_items) {
    if(!l) return ATHENA_MODEL3D_EINVAL;
    if(l->code<0) return l->code;
    if(l->phase==ATHENA_MESH3D_LOADER_DONE) return 1;
    l->last_phase=l->phase; l->unit_done=1;
    uint32_t clock_start=athena_model3d_clock();
    int code=mesh_loader_run(l,max_items);
    if(code<0) { l->code=code; return code; }
    if(code==ATHENA_MESH3D_WAITING) return code;
    {
        float ms=athena_model3d_clock_ms(athena_model3d_clock()-clock_start);
        AthenaLoaderPhaseStats *st=&l->stats[l->last_phase];
        st->units++; st->total_ms+=ms; if(ms>st->max_ms) st->max_ms=ms;
    }
    l->units+=l->unit_done;
    return l->phase==ATHENA_MESH3D_LOADER_DONE?1:0;
}
int athena_mesh3d_loader_run(AthenaMesh3DLoader *l,float budget_ms,uint32_t *ran) {
    uint32_t start=athena_model3d_clock(),n=0; int code; float last_ms=0;
    /* Another unit only starts when the previous one's duration still fits in the budget,
     * so a slice overshoots only when a unit is slower than the one before it. */
    do {
        uint32_t unit_start=athena_model3d_clock();
        code=athena_mesh3d_loader_step(l,0);
        if(code==ATHENA_MESH3D_WAITING) break;
        last_ms=athena_model3d_clock_ms(athena_model3d_clock()-unit_start);
        n++;
    } while(code==0&&athena_model3d_clock_ms(athena_model3d_clock()-start)+last_ms<budget_ms);
    if(ran) *ran=n;
    return code;
}
void athena_mesh3d_loader_phase_stats(const AthenaMesh3DLoader *l,int phase,AthenaLoaderPhaseStats *out) {
    if(out) *out=l&&phase>=0&&phase<=ATHENA_MESH3D_LOADER_DONE?l->stats[phase]:(AthenaLoaderPhaseStats){0};
}
int athena_mesh3d_loader_take(AthenaMesh3DLoader *l,AthenaMesh3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    if(!l||l->code<0||l->phase!=ATHENA_MESH3D_LOADER_DONE||!l->mesh) return ATHENA_MODEL3D_EINVAL;
    *out=l->mesh; l->mesh=NULL; return 0;
}

int athena_mesh3d_load_with_material(const char *path,const AthenaMaterial3D *material,AthenaMesh3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    athena_model3d_clear_detail();
    if(material&&!athena_material3d_validate(material)) return ATHENA_MODEL3D_EINVAL;
    if(!path||!*path) return ATHENA_MODEL3D_EINVAL;
    const char *ext=strrchr(path,'.');
    if(!ext||(strcmp(ext,".obj")&&strcmp(ext,".gltf")&&strcmp(ext,".glb"))) {
        athena_model3d_set_detail("unknown model extension; use .obj, .gltf or .glb");
        return ATHENA_MODEL3D_EUNSUPPORTED;
    }
    FILE *file=fopen(path,"rb"); if(!file) { athena_model3d_set_detail("cannot open file"); return ATHENA_MODEL3D_EIO; }
    fclose(file);
    return !strcmp(ext,".obj")?load_obj(path,material,out):load_gltf(path,NULL,0,material,out);
}
int athena_mesh3d_load(const char *path,AthenaMesh3D **out) { return athena_mesh3d_load_with_material(path,NULL,out); }
int athena_mesh3d_load_memory(const void *bytes,size_t size,const char *origin,
    const AthenaMaterial3D *material,AthenaMesh3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL; athena_model3d_clear_detail();
    if(!bytes||!origin||!*origin||!size||size>ATHENA_MODEL3D_MEMORY_MAX_BYTES||
        (material&&!athena_material3d_validate(material))) return ATHENA_MODEL3D_EINVAL;
    return load_gltf(origin,bytes,size,material,out);
}
