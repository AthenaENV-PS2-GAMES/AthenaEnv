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
 * texture coordinates and indices. Shared with the gltf3d scene loader. */
int athena_model3d_gltf_primitive(const char *path,cgltf_primitive *prim,
    const AthenaMaterial3D *material_override,AthenaGltfTextureCache *cache,AthenaMesh3D **out) {
    float *positions=NULL,*colors=NULL,*normals=NULL,*texcoords=NULL,*weights=NULL; uint32_t *indices=NULL;
    float *target_positions=NULL,*target_normals=NULL;
    uint16_t *joints=NULL;
    AthenaMaterial3D material; athena_material3d_default(&material);
    if(material_override) material=*material_override;
    AthenaTexture3D *loaded_texture=NULL;
    int result=ATHENA_MODEL3D_EFORMAT;
    if(prim->type!=cgltf_primitive_type_triangles) {
        athena_model3d_set_detail("primitive mode %d is not supported; export triangles",(int)prim->type);
        return ATHENA_MODEL3D_EUNSUPPORTED;
    }
    if(prim->targets_count>ATHENA_MODEL3D_MAX_TARGETS) {
        athena_model3d_set_detail("%u morph targets, at most %u",(unsigned)prim->targets_count,ATHENA_MODEL3D_MAX_TARGETS);
        return ATHENA_MODEL3D_EUNSUPPORTED;
    }
    if(prim->has_draco_mesh_compression) { athena_model3d_set_detail("KHR_draco_mesh_compression is not supported"); return ATHENA_MODEL3D_EUNSUPPORTED; }
    float factor[4]={1,1,1,1};
    if(prim->material) {
        /* Only the base colour has a Gouraud equivalent. Lighting-only data
         * (normal, metallic-roughness and occlusion maps, specular, sheen,
         * clearcoat, IOR...) is ignored; whatever changes the visible colour
         * or coverage is rejected with its name. */
        cgltf_material *mat=prim->material;
        const char *name=mat->name?mat->name:"";
        if(mat->alpha_mode==cgltf_alpha_mode_blend)
            REJECT(ATHENA_MODEL3D_EUNSUPPORTED,"material '%s': alphaMode BLEND is not supported (MASK is)",name);
        /* MASK: GS alpha test at alphaCutoff. Model3D.load() always passes
         * a material (UNLIT by default): only one with its own mask wins. */
        if(mat->alpha_mode==cgltf_alpha_mode_mask&&!material.alpha_mask) {
            if(!(mat->alpha_cutoff>=0&&mat->alpha_cutoff<=1))
                REJECT(ATHENA_MODEL3D_EFORMAT,"material '%s': alphaCutoff outside [0,1]",name);
            material.alpha_mask=1; material.alpha_cutoff=mat->alpha_cutoff;
        }
        if(mat->emissive_texture.texture||mat->emissive_factor[0]!=0||mat->emissive_factor[1]!=0||mat->emissive_factor[2]!=0)
            REJECT(ATHENA_MODEL3D_EUNSUPPORTED,"material '%s': emissive colour is not supported",name);
        if(mat->has_pbr_specular_glossiness)
            REJECT(ATHENA_MODEL3D_EUNSUPPORTED,"material '%s': KHR_materials_pbrSpecularGlossiness is not supported",name);
        if(mat->has_transmission||mat->has_volume||mat->has_diffuse_transmission)
            REJECT(ATHENA_MODEL3D_EUNSUPPORTED,"material '%s': transmission/volume is not supported",name);
        if(mat->has_pbr_metallic_roughness) memcpy(factor,mat->pbr_metallic_roughness.base_color_factor,sizeof(factor));
        cgltf_texture_view *view=&mat->pbr_metallic_roughness.base_color_texture;
        if(view->texture&&!material.texture) {
            if(view->texcoord!=0)
                REJECT(ATHENA_MODEL3D_EUNSUPPORTED,"material '%s': baseColorTexture uses TEXCOORD_%d; only TEXCOORD_0",name,view->texcoord);
            if(view->has_transform)
                REJECT(ATHENA_MODEL3D_EUNSUPPORTED,"material '%s': KHR_texture_transform is not supported",name);
            result=gltf_texture(path,view->texture,cache,&loaded_texture);
            if(result<0) goto done;
            material.texture=loaded_texture;
            result=ATHENA_MODEL3D_EFORMAT;
        }
    }
    cgltf_accessor *pos=NULL,*col=NULL,*normal=NULL,*uv=NULL,*joint=NULL,*weight=NULL;
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
        REJECT(ATHENA_MODEL3D_EFORMAT,"primitive attributes: missing POSITION, or counts/types disagree (%u vertices, max %u)",
            pos?(unsigned)pos->count:0,ATHENA_MODEL3D_MAX_VERTICES);
    cgltf_size count=prim->indices?prim->indices->count:pos->count;
    if(!count||count%3||count>ATHENA_MODEL3D_MAX_VERTICES)
        REJECT(count>ATHENA_MODEL3D_MAX_VERTICES?ATHENA_MODEL3D_EUNSUPPORTED:ATHENA_MODEL3D_EFORMAT,
            "primitive has %u indices; a multiple of 3 up to %u is required",(unsigned)count,ATHENA_MODEL3D_MAX_VERTICES);
    if(pos->is_sparse||(col&&col->is_sparse)||(normal&&normal->is_sparse)||(uv&&uv->is_sparse)||(prim->indices&&prim->indices->is_sparse)||
        (joint&&(joint->is_sparse||weight->is_sparse)))
        REJECT(ATHENA_MODEL3D_EUNSUPPORTED,"sparse vertex accessors are not supported");
    positions=malloc(pos->count*3*sizeof(float)); colors=malloc(pos->count*4*sizeof(float));
    if(normal) normals=malloc(pos->count*3*sizeof(float));
    if(uv) texcoords=malloc(pos->count*2*sizeof(float));
    if(prim->indices) indices=malloc(count*sizeof(uint32_t));
    if(joint) { joints=malloc(pos->count*4*sizeof(uint16_t)); weights=malloc(pos->count*4*sizeof(float)); }
    if(!positions||!colors||(normal&&!normals)||(uv&&!texcoords)||(prim->indices&&!indices)||(joint&&(!joints||!weights))) {
        result=ATHENA_MODEL3D_ENOMEM; goto done;
    }
    for(cgltf_size i=0;i<pos->count;i++) {
        if(!cgltf_accessor_read_float(pos,i,&positions[i*3],3)) goto done;
        if(normal&&!cgltf_accessor_read_float(normal,i,&normals[i*3],3)) goto done;
        if(uv&&!cgltf_accessor_read_float(uv,i,&texcoords[i*2],2)) goto done;
        if(joint) {
            cgltf_uint j[4];
            if(!cgltf_accessor_read_uint(joint,i,j,4)||!cgltf_accessor_read_float(weight,i,&weights[i*4],4)) goto done;
            for(int k=0;k<4;k++) { if(j[k]>0xffff) goto done; joints[i*4+k]=(uint16_t)j[k]; }
        }
        float c[4]={1,1,1,1};
        if(col&&!cgltf_accessor_read_float(col,i,c,col->type==cgltf_type_vec3?3:4)) goto done;
        for(int j=0;j<4;j++) colors[i*4+j]=c[j]*factor[j];
    }
    if(indices) for(cgltf_size i=0;i<count;i++) {
        cgltf_size index=cgltf_accessor_read_index(prim->indices,i);
        if(index>=pos->count) goto done;
        indices[i]=index;
    }
    /* Morph targets: POSITION and NORMAL deltas (absent ones are zero;
     * TANGENT is ignored). unpack_floats also expands sparse accessors,
     * the usual encoding of targets that move few vertices. */
    int target_has_normals=0;
    for(cgltf_size t=0;t<prim->targets_count;t++) for(cgltf_size a=0;a<prim->targets[t].attributes_count;a++)
        if(prim->targets[t].attributes[a].type==cgltf_attribute_type_normal) target_has_normals=1;
    if(prim->targets_count) {
        target_positions=calloc(prim->targets_count*pos->count*3,sizeof(float));
        if(normal&&target_has_normals) target_normals=calloc(prim->targets_count*pos->count*3,sizeof(float));
        if(!target_positions||(normal&&target_has_normals&&!target_normals)) { result=ATHENA_MODEL3D_ENOMEM; goto done; }
    }
    for(cgltf_size t=0;t<prim->targets_count;t++) for(cgltf_size a=0;a<prim->targets[t].attributes_count;a++) {
        const cgltf_attribute *attribute=&prim->targets[t].attributes[a];
        float *out=attribute->type==cgltf_attribute_type_position?&target_positions[t*pos->count*3]:
            attribute->type==cgltf_attribute_type_normal&&target_normals?&target_normals[t*pos->count*3]:NULL;
        if(!out) continue;
        const cgltf_accessor *d=attribute->data;
        if(d->type!=cgltf_type_vec3||d->count!=pos->count||
            cgltf_accessor_unpack_floats(d,out,pos->count*3)!=pos->count*3) goto done;
    }
    AthenaGeometry3D g={.positions=positions,.vertex_count=pos->count,.colors=colors,.color_count=pos->count,
        .indices=indices,.index_count=indices?count:0,.normals=normals,.normal_count=normal?pos->count:0,
        .material=&material,.texcoords=texcoords,.texcoord_count=uv?pos->count:0,
        .joints=joints,.weights=weights,.skin_count=joint?pos->count:0,
        .target_positions=target_positions,.target_normals=target_normals,.target_count=(uint32_t)prim->targets_count};
    result=athena_mesh3d_create(&g,out);
    if(result==ATHENA_MODEL3D_EINVAL) {
        uint32_t offset; AthenaGeometry3DIssue issue=athena_geometry3d_validate(&g,&offset);
        if(issue==ATHENA_GEOMETRY3D_TEXCOORD)
            athena_model3d_set_detail("UV component %u is outside [-%g, %g]",(unsigned)offset,(double)ATHENA_MODEL3D_UV_LIMIT,(double)ATHENA_MODEL3D_UV_LIMIT);
        else if(issue==ATHENA_GEOMETRY3D_NORMAL) athena_model3d_set_detail("invalid normal or degenerate triangle at %u",(unsigned)offset);
        else if(issue==ATHENA_GEOMETRY3D_SKIN) athena_model3d_set_detail("invalid joint index or weight at %u",(unsigned)offset);
        else athena_model3d_set_detail("invalid geometry at offset %u (issue %d)",(unsigned)offset,(int)issue);
        result=ATHENA_MODEL3D_EFORMAT;
    }
done:
    if(result==ATHENA_MODEL3D_EFORMAT) athena_model3d_set_detail("malformed primitive accessor data");
    free(positions); free(colors); free(normals); free(texcoords); free(indices); free(joints); free(weights);
    free(target_positions); free(target_normals);
    athena_texture3d_release(loaded_texture); return result;
}

static int load_gltf(const char *path,const AthenaMaterial3D *material_override,AthenaMesh3D **out) {
    cgltf_options options={0}; cgltf_data *data=NULL;
    int result=ATHENA_MODEL3D_EFORMAT;
    AthenaGltfTextureCache cache={0};
    cgltf_result parsed=cgltf_parse_file(&options,path,&data);
    if(parsed!=cgltf_result_success) {
        athena_model3d_set_detail("glTF parse error %d",(int)parsed);
        return parsed==cgltf_result_out_of_memory?ATHENA_MODEL3D_ENOMEM:ATHENA_MODEL3D_EFORMAT;
    }
    if((result=athena_model3d_gltf_extensions(data))<0) goto done;
    result=ATHENA_MODEL3D_EUNSUPPORTED;
    if(data->skins_count||data->animations_count)
        REJECT(ATHENA_MODEL3D_EUNSUPPORTED,"skins and animations need Gltf3D.load (Model3D.load reads one static mesh)");
    if(data->meshes_count!=1||data->meshes[0].primitives_count!=1)
        REJECT(ATHENA_MODEL3D_EUNSUPPORTED,"%u meshes / %u primitives: Model3D.load reads exactly one; use Gltf3D.load",
            (unsigned)data->meshes_count,data->meshes_count?(unsigned)data->meshes[0].primitives_count:0);
    for(cgltf_size n=0;n<data->nodes_count;n++) {
        const cgltf_node *node=&data->nodes[n];
        if(node->has_matrix||node->has_translation||node->has_rotation||node->has_scale||node->skin||
            node->has_mesh_gpu_instancing)
            REJECT(ATHENA_MODEL3D_EUNSUPPORTED,"node '%s' has a transform; use Gltf3D.load for node hierarchies",node->name?node->name:"");
    }
    cgltf_primitive *prim=&data->meshes[0].primitives[0];
    if(prim->targets_count) REJECT(ATHENA_MODEL3D_EUNSUPPORTED,"morph targets need Gltf3D.load");
    parsed=cgltf_load_buffers(&options,data,path);
    if(parsed!=cgltf_result_success)
        REJECT(parsed==cgltf_result_out_of_memory?ATHENA_MODEL3D_ENOMEM:ATHENA_MODEL3D_EIO,"cannot load glTF buffers (error %d)",(int)parsed);
    if(cgltf_validate(data)!=cgltf_result_success) REJECT(ATHENA_MODEL3D_EFORMAT,"glTF validation failed");
    result=athena_model3d_gltf_primitive(path,prim,material_override,&cache,out);
done:
    athena_model3d_gltf_cache_release(&cache); cgltf_free(data); return result;
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
    return !strcmp(ext,".obj")?load_obj(path,material,out):load_gltf(path,material,out);
}
int athena_mesh3d_load(const char *path,AthenaMesh3D **out) { return athena_mesh3d_load_with_material(path,NULL,out); }
