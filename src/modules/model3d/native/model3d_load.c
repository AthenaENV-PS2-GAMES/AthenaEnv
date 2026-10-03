#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <athena/model3d.h>
#include "fast_obj/fast_obj.h"
#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"

/* Stage 1 deliberately rejects unsupported features, never silently discards
 * skins, texture materials, strips, multiple meshes or node transforms. */
static int load_obj(const char *path,AthenaMesh3D **out) {
    fastObjMesh *obj=fast_obj_read(path);
    if(!obj) return ATHENA_MODEL3D_EFORMAT;
    int result=ATHENA_MODEL3D_EUNSUPPORTED;
    float *positions=NULL,*colors=NULL;
    if(obj->strip_count||!obj->face_count||obj->index_count>ATHENA_MODEL3D_MAX_VERTICES) goto done;
    for(uint32_t i=0;i<obj->face_count;i++) if(obj->face_vertices[i]!=3) goto done;
    if(obj->face_count>ATHENA_MODEL3D_MAX_VERTICES/3||obj->index_count!=obj->face_count*3) {
        result=ATHENA_MODEL3D_EFORMAT; goto done;
    }
    for(uint32_t i=0;i<obj->material_count;i++) {
        fastObjMaterial *m=&obj->materials[i];
        if(m->map_Kd.path||m->map_Ka.path||m->map_Ks.path||m->map_bump.path||m->map_d.path||
            m->map_Ke.path||m->map_Kt.path||m->map_Ns.path||m->map_Ni.path||m->d<1) goto done;
    }
    positions=malloc(obj->index_count*3*sizeof(float));
    colors=malloc(obj->index_count*4*sizeof(float));
    if(!positions||!colors) { result=ATHENA_MODEL3D_ENOMEM; goto done; }
    for(uint32_t i=0;i<obj->index_count;i++) {
        uint32_t p=obj->indices[i].p;
        if(p==0||p>=obj->position_count) { result=ATHENA_MODEL3D_EFORMAT; goto done; }
        memcpy(&positions[i*3],&obj->positions[p*3],3*sizeof(float));
        uint32_t material=obj->face_materials[i/3];
        if(obj->material_count && material<obj->material_count) {
            memcpy(&colors[i*4],obj->materials[material].Kd,3*sizeof(float));
        } else colors[i*4]=colors[i*4+1]=colors[i*4+2]=1;
        colors[i*4+3]=1;
    }
    AthenaGeometry3D g={positions,obj->index_count,colors,obj->index_count,NULL,0};
    result=athena_mesh3d_create(&g,out);
done:
    free(positions); free(colors); fast_obj_destroy(obj); return result;
}

static int load_gltf(const char *path,AthenaMesh3D **out) {
    cgltf_options options={0}; cgltf_data *data=NULL;
    float *positions=NULL,*colors=NULL; uint32_t *indices=NULL;
    int result=ATHENA_MODEL3D_EFORMAT;
    cgltf_result parsed=cgltf_parse_file(&options,path,&data);
    if(parsed!=cgltf_result_success)
        return parsed==cgltf_result_out_of_memory?ATHENA_MODEL3D_ENOMEM:ATHENA_MODEL3D_EFORMAT;
    for(cgltf_size e=0;e<data->extensions_required_count;e++) {
        if(strcmp(data->extensions_required[e],"KHR_materials_unlit")) {
            result=ATHENA_MODEL3D_EUNSUPPORTED; goto done;
        }
    }
    if(data->skins_count||data->animations_count||data->meshes_count!=1||
        data->meshes[0].primitives_count!=1) { result=ATHENA_MODEL3D_EUNSUPPORTED; goto done; }
    for(cgltf_size n=0;n<data->nodes_count;n++) {
        const cgltf_node *node=&data->nodes[n];
        if(node->has_matrix||node->has_translation||node->has_rotation||node->has_scale||node->skin||
            node->has_mesh_gpu_instancing) { result=ATHENA_MODEL3D_EUNSUPPORTED; goto done; }
    }
    cgltf_primitive *prim=&data->meshes[0].primitives[0];
    if(prim->type!=cgltf_primitive_type_triangles||prim->targets_count||prim->has_draco_mesh_compression) {
        result=ATHENA_MODEL3D_EUNSUPPORTED; goto done;
    }
    float factor[4]={1,1,1,1};
    if(prim->material) {
        cgltf_material *mat=prim->material;
        if(mat->alpha_mode!=cgltf_alpha_mode_opaque||mat->normal_texture.texture||mat->emissive_texture.texture||
            mat->pbr_metallic_roughness.base_color_texture.texture||mat->pbr_metallic_roughness.metallic_roughness_texture.texture||
            mat->has_pbr_specular_glossiness||mat->has_transmission||mat->has_clearcoat||mat->has_sheen||
            mat->has_volume||mat->has_ior||mat->has_specular||mat->has_emissive_strength||mat->has_iridescence||
            mat->has_diffuse_transmission||mat->has_anisotropy||mat->has_dispersion||mat->occlusion_texture.texture||
            mat->emissive_factor[0]!=0||mat->emissive_factor[1]!=0||mat->emissive_factor[2]!=0) {
            result=ATHENA_MODEL3D_EUNSUPPORTED; goto done;
        }
        if(mat->has_pbr_metallic_roughness) memcpy(factor,mat->pbr_metallic_roughness.base_color_factor,sizeof(factor));
    }
    cgltf_accessor *pos=NULL,*col=NULL;
    for(cgltf_size a=0;a<prim->attributes_count;a++) {
        if(prim->attributes[a].type==cgltf_attribute_type_position) pos=prim->attributes[a].data;
        if(prim->attributes[a].type==cgltf_attribute_type_color && prim->attributes[a].index==0) col=prim->attributes[a].data;
    }
    if(!pos||pos->type!=cgltf_type_vec3||pos->count==0||pos->count>ATHENA_MODEL3D_MAX_VERTICES||
        (col&&(col->count!=pos->count||(col->type!=cgltf_type_vec3&&col->type!=cgltf_type_vec4)))) goto done;
    cgltf_size count=prim->indices?prim->indices->count:pos->count;
    if(!count||count%3||count>ATHENA_MODEL3D_MAX_VERTICES) goto done;
    parsed=cgltf_load_buffers(&options,data,path);
    if(parsed!=cgltf_result_success) { result=parsed==cgltf_result_out_of_memory?ATHENA_MODEL3D_ENOMEM:ATHENA_MODEL3D_EIO; goto done; }
    if(cgltf_validate(data)!=cgltf_result_success) goto done;
    if(pos->is_sparse||(col&&col->is_sparse)||(prim->indices&&prim->indices->is_sparse)) {
        result=ATHENA_MODEL3D_EUNSUPPORTED; goto done;
    }
    positions=malloc(pos->count*3*sizeof(float)); colors=malloc(pos->count*4*sizeof(float));
    if(prim->indices) indices=malloc(count*sizeof(uint32_t));
    if(!positions||!colors||(prim->indices&&!indices)) { result=ATHENA_MODEL3D_ENOMEM; goto done; }
    for(cgltf_size i=0;i<pos->count;i++) {
        if(!cgltf_accessor_read_float(pos,i,&positions[i*3],3)) goto done;
        float c[4]={1,1,1,1};
        if(col&&!cgltf_accessor_read_float(col,i,c,col->type==cgltf_type_vec3?3:4)) goto done;
        for(int j=0;j<4;j++) colors[i*4+j]=c[j]*factor[j];
    }
    if(indices) for(cgltf_size i=0;i<count;i++) {
        cgltf_size index=cgltf_accessor_read_index(prim->indices,i);
        if(index>=pos->count) goto done;
        indices[i]=index;
    }
    AthenaGeometry3D g={positions,pos->count,colors,pos->count,indices,indices?count:0};
    result=athena_mesh3d_create(&g,out);
done:
    free(positions); free(colors); free(indices); cgltf_free(data); return result;
}

int athena_mesh3d_load(const char *path,AthenaMesh3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    if(!path||!*path) return ATHENA_MODEL3D_EINVAL;
    const char *ext=strrchr(path,'.');
    if(!ext) return ATHENA_MODEL3D_EUNSUPPORTED;
    if(strcmp(ext,".obj")&&strcmp(ext,".gltf")&&strcmp(ext,".glb")) return ATHENA_MODEL3D_EUNSUPPORTED;
    FILE *file=fopen(path,"rb"); if(!file) return ATHENA_MODEL3D_EIO;
    fclose(file);
    return !strcmp(ext,".obj")?load_obj(path,out):load_gltf(path,out);
}
