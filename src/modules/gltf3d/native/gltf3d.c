#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <athena/gltf3d.h>
#include "../../model3d/native/model3d_gltf.h"
struct AthenaGltf3D {
    AthenaNode3D *root;
    AthenaNode3D **nodes; char **node_names; uint32_t node_count;
    AthenaClip3D **clips; char **clip_names; uint32_t clip_count;
};
static char *copy_name(const char *name) {
    size_t n=name?strlen(name):0;
    char *copy=malloc(n+1); if(!copy) return NULL;
    if(n) memcpy(copy,name,n);
    copy[n]=0; return copy;
}
void athena_gltf3d_release(AthenaGltf3D *s) {
    if(!s) return;
    for(uint32_t i=0;i<s->node_count;i++) { athena_node3d_release(s->nodes[i]); free(s->node_names[i]); }
    for(uint32_t i=0;i<s->clip_count;i++) { athena_clip3d_release(s->clips[i]); free(s->clip_names[i]); }
    athena_node3d_release(s->root);
    free(s->nodes); free(s->node_names); free(s->clips); free(s->clip_names); free(s);
}
AthenaNode3D *athena_gltf3d_root(const AthenaGltf3D *s) { return s?s->root:NULL; }
uint32_t athena_gltf3d_node_count(const AthenaGltf3D *s) { return s?s->node_count:0; }
AthenaNode3D *athena_gltf3d_node(const AthenaGltf3D *s,uint32_t i) { return s&&i<s->node_count?s->nodes[i]:NULL; }
const char *athena_gltf3d_node_name(const AthenaGltf3D *s,uint32_t i) { return s&&i<s->node_count?s->node_names[i]:NULL; }
uint32_t athena_gltf3d_clip_count(const AthenaGltf3D *s) { return s?s->clip_count:0; }
AthenaClip3D *athena_gltf3d_clip(const AthenaGltf3D *s,uint32_t i) { return s&&i<s->clip_count?s->clips[i]:NULL; }
const char *athena_gltf3d_clip_name(const AthenaGltf3D *s,uint32_t i) { return s&&i<s->clip_count?s->clip_names[i]:NULL; }

/* Column-major matrix without shear into TRS; a negative determinant puts
 * the reflection in the X scale. */
static int set_matrix(AthenaNode3D *n,const float *m) {
    float sx=sqrtf(m[0]*m[0]+m[1]*m[1]+m[2]*m[2]),sy=sqrtf(m[4]*m[4]+m[5]*m[5]+m[6]*m[6]),
        sz=sqrtf(m[8]*m[8]+m[9]*m[9]+m[10]*m[10]);
    if(!(sx>0&&sy>0&&sz>0)) return ATHENA_MODEL3D_EFORMAT;
    float det=m[0]*(m[5]*m[10]-m[9]*m[6])-m[4]*(m[1]*m[10]-m[9]*m[2])+m[8]*(m[1]*m[6]-m[5]*m[2]);
    if(det<0) sx=-sx;
    float r[9]={m[0]/sx,m[1]/sx,m[2]/sx, m[4]/sy,m[5]/sy,m[6]/sy, m[8]/sz,m[9]/sz,m[10]/sz};
    /* r[c*3+row]: rotation matrix to quaternion (Shepperd). */
    float trace=r[0]+r[4]+r[8],q[4];
    if(trace>0) {
        float s=sqrtf(trace+1)*2;
        q[3]=s/4; q[0]=(r[5]-r[7])/s; q[1]=(r[6]-r[2])/s; q[2]=(r[1]-r[3])/s;
    } else if(r[0]>r[4]&&r[0]>r[8]) {
        float s=sqrtf(1+r[0]-r[4]-r[8])*2;
        q[3]=(r[5]-r[7])/s; q[0]=s/4; q[1]=(r[3]+r[1])/s; q[2]=(r[6]+r[2])/s;
    } else if(r[4]>r[8]) {
        float s=sqrtf(1+r[4]-r[0]-r[8])*2;
        q[3]=(r[6]-r[2])/s; q[0]=(r[3]+r[1])/s; q[1]=s/4; q[2]=(r[7]+r[5])/s;
    } else {
        float s=sqrtf(1+r[8]-r[0]-r[4])*2;
        q[3]=(r[1]-r[3])/s; q[0]=(r[6]+r[2])/s; q[1]=(r[7]+r[5])/s; q[2]=s/4;
    }
    if(athena_node3d_set_position(n,m[12],m[13],m[14])<0||athena_node3d_set_scale(n,sx,sy,sz)<0||
        athena_node3d_set_rotation(n,q[0],q[1],q[2],q[3])<0) return ATHENA_MODEL3D_EFORMAT;
    return 0;
}
static int set_trs(AthenaNode3D *n,const cgltf_node *g) {
    if(g->has_matrix) return set_matrix(n,g->matrix);
    if(g->has_translation&&athena_node3d_set_position(n,g->translation[0],g->translation[1],g->translation[2])<0)
        return ATHENA_MODEL3D_EFORMAT;
    if(g->has_rotation&&athena_node3d_set_rotation(n,g->rotation[0],g->rotation[1],g->rotation[2],g->rotation[3])<0)
        return ATHENA_MODEL3D_EFORMAT;
    if(g->has_scale&&athena_node3d_set_scale(n,g->scale[0],g->scale[1],g->scale[2])<0) return ATHENA_MODEL3D_EFORMAT;
    return 0;
}
/* The node's mesh: its first primitive on the node, the others on children.
 * Initial morph weights: the node's, else the mesh's, on every primitive
 * (animated weights reach the first primitive only). */
static int attach_mesh(AthenaNode3D *n,const char *path,const cgltf_node *node,const cgltf_mesh *mesh,
    const AthenaMaterial3D *material) {
    const cgltf_float *initial=node->weights_count?node->weights:mesh->weights;
    cgltf_size initial_count=node->weights_count?node->weights_count:mesh->weights_count;
    float weights[ATHENA_MODEL3D_MAX_TARGETS]={0};
    if(initial_count>ATHENA_MODEL3D_MAX_TARGETS) return ATHENA_MODEL3D_EUNSUPPORTED;
    for(cgltf_size t=0;t<initial_count;t++) weights[t]=initial[t];
    for(cgltf_size p=0;p<mesh->primitives_count;p++) {
        AthenaMesh3D *m=NULL;
        int code=athena_model3d_gltf_primitive(path,&mesh->primitives[p],material,&m);
        if(code<0) return code;
        if(p==0) {
            code=athena_node3d_set_mesh(n,m);
            if(code>=0) code=athena_node3d_set_weights(n,weights,(uint32_t)initial_count);
        } else {
            AthenaNode3D *child=athena_node3d_create();
            code=child?athena_node3d_set_mesh(child,m):ATHENA_MODEL3D_ENOMEM;
            if(code>=0) code=athena_node3d_set_weights(child,weights,(uint32_t)initial_count);
            if(code>=0) code=athena_node3d_add_child(n,child);
            athena_node3d_release(child);
        }
        athena_mesh3d_release(m);
        if(code<0) return code==ATHENA_SCENE3D_ENOMEM?ATHENA_MODEL3D_ENOMEM:ATHENA_MODEL3D_EFORMAT;
    }
    return 0;
}
static int read_floats(const cgltf_accessor *a,float *out,cgltf_size width) {
    for(cgltf_size i=0;i<a->count;i++) if(!cgltf_accessor_read_float(a,i,&out[i*width],width)) return 0;
    return 1;
}
static int load_clip(const cgltf_data *data,const cgltf_animation *anim,AthenaClip3D **out) {
    AthenaTrack3DDesc *tracks=calloc(anim->channels_count?anim->channels_count:1,sizeof(*tracks));
    float **buffers=calloc(anim->channels_count*2+1,sizeof(*buffers));
    int code=tracks&&buffers?0:ATHENA_MODEL3D_ENOMEM;
    cgltf_size used=0;
    for(cgltf_size c=0;code==0&&c<anim->channels_count;c++) {
        const cgltf_animation_channel *ch=&anim->channels[c];
        const cgltf_animation_sampler *sm=ch->sampler;
        if(!ch->target_node) continue; /* channels without a node target nothing */
        AthenaAnim3DPath path;
        cgltf_size width;
        if(ch->target_path==cgltf_animation_path_type_translation) { path=ATHENA_ANIM3D_POSITION; width=3; }
        else if(ch->target_path==cgltf_animation_path_type_rotation) { path=ATHENA_ANIM3D_ROTATION; width=4; }
        else if(ch->target_path==cgltf_animation_path_type_scale) { path=ATHENA_ANIM3D_SCALE; width=3; }
        else if(ch->target_path==cgltf_animation_path_type_weights) {
            /* One scalar per morph target of the node's mesh, per key. */
            const cgltf_mesh *mesh=ch->target_node->mesh;
            width=mesh&&mesh->primitives_count?mesh->primitives[0].targets_count:0;
            if(!width) { code=ATHENA_MODEL3D_EFORMAT; break; }
            if(width>ATHENA_MODEL3D_MAX_TARGETS) { code=ATHENA_MODEL3D_EUNSUPPORTED; break; }
            path=ATHENA_ANIM3D_WEIGHTS;
        }
        else { code=ATHENA_MODEL3D_EUNSUPPORTED; break; }
        const cgltf_accessor *in=sm->input,*outv=sm->output;
        int cubic=sm->interpolation==cgltf_interpolation_type_cubic_spline;
        int weights=path==ATHENA_ANIM3D_WEIGHTS;
        cgltf_size keys=in->count;
        /* Weights outputs are scalars, width of them per key. */
        if(!keys||in->type!=cgltf_type_scalar||outv->count*(weights?1:width)!=keys*(cubic?3:1)*width||
            (weights&&outv->type!=cgltf_type_scalar)) { code=ATHENA_MODEL3D_EFORMAT; break; }
        float *times=malloc(keys*sizeof(float)),*raw=malloc(keys*(cubic?3:1)*width*sizeof(float));
        buffers[used*2]=times; buffers[used*2+1]=raw;
        if(!times||!raw) { code=ATHENA_MODEL3D_ENOMEM; break; }
        if(!read_floats(in,times,1)||!read_floats(outv,raw,weights?1:width)) { code=ATHENA_MODEL3D_EFORMAT; break; }
        /* CUBICSPLINE stores in-tangent, value, out-tangent: keep the values. */
        if(cubic) for(cgltf_size k=0;k<keys;k++) memmove(&raw[k*width],&raw[(k*3+1)*width],width*sizeof(float));
        tracks[used++]=(AthenaTrack3DDesc){.target=(uint32_t)(ch->target_node-data->nodes),.path=path,
            .interpolation=sm->interpolation==cgltf_interpolation_type_step?ATHENA_ANIM3D_STEP:ATHENA_ANIM3D_LINEAR,
            .times=times,.values=raw,.key_count=(uint32_t)keys,.weight_count=weights?(uint32_t)width:0};
    }
    if(code==0) {
        if(!used) code=ATHENA_MODEL3D_EFORMAT;
        else {
            code=athena_clip3d_create(tracks,(uint32_t)used,out);
            if(code==ATHENA_ANIM3D_ENOMEM) code=ATHENA_MODEL3D_ENOMEM;
            else if(code<0) code=ATHENA_MODEL3D_EFORMAT;
        }
    }
    for(cgltf_size i=0;buffers&&i<anim->channels_count*2;i++) free(buffers[i]);
    free(buffers); free(tracks); return code;
}
int athena_gltf3d_load(const char *path,const AthenaMaterial3D *material,AthenaGltf3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    if(!path||!*path||(material&&!athena_material3d_validate(material))) return ATHENA_MODEL3D_EINVAL;
    cgltf_options options={0}; cgltf_data *data=NULL;
    cgltf_result parsed=cgltf_parse_file(&options,path,&data);
    if(parsed!=cgltf_result_success)
        return parsed==cgltf_result_out_of_memory?ATHENA_MODEL3D_ENOMEM:
            parsed==cgltf_result_file_not_found||parsed==cgltf_result_io_error?ATHENA_MODEL3D_EIO:ATHENA_MODEL3D_EFORMAT;
    int code=0;
    AthenaGltf3D *s=calloc(1,sizeof(*s));
    if(!s) { code=ATHENA_MODEL3D_ENOMEM; goto done; }
    for(cgltf_size e=0;e<data->extensions_required_count;e++)
        if(strcmp(data->extensions_required[e],"KHR_materials_unlit")) { code=ATHENA_MODEL3D_EUNSUPPORTED; goto done; }
    for(cgltf_size n=0;n<data->nodes_count;n++)
        if(data->nodes[n].has_mesh_gpu_instancing) { code=ATHENA_MODEL3D_EUNSUPPORTED; goto done; }
    parsed=cgltf_load_buffers(&options,data,path);
    if(parsed!=cgltf_result_success) { code=parsed==cgltf_result_out_of_memory?ATHENA_MODEL3D_ENOMEM:ATHENA_MODEL3D_EIO; goto done; }
    if(cgltf_validate(data)!=cgltf_result_success) { code=ATHENA_MODEL3D_EFORMAT; goto done; }
    s->root=athena_node3d_create();
    s->nodes=calloc(data->nodes_count?data->nodes_count:1,sizeof(*s->nodes));
    s->node_names=calloc(data->nodes_count?data->nodes_count:1,sizeof(*s->node_names));
    if(!s->root||!s->nodes||!s->node_names) { code=ATHENA_MODEL3D_ENOMEM; goto done; }
    for(cgltf_size n=0;n<data->nodes_count;n++) {
        s->nodes[n]=athena_node3d_create(); s->node_names[n]=copy_name(data->nodes[n].name);
        s->node_count=(uint32_t)n+1;
        if(!s->nodes[n]||!s->node_names[n]) { code=ATHENA_MODEL3D_ENOMEM; goto done; }
        if((code=set_trs(s->nodes[n],&data->nodes[n]))<0) goto done;
        if(data->nodes[n].mesh&&(code=attach_mesh(s->nodes[n],path,&data->nodes[n],data->nodes[n].mesh,material))<0) goto done;
    }
    for(cgltf_size n=0;n<data->nodes_count;n++)
        for(cgltf_size c=0;c<data->nodes[n].children_count;c++)
            if(athena_node3d_add_child(s->nodes[n],s->nodes[data->nodes[n].children[c]-data->nodes])<0) {
                code=ATHENA_MODEL3D_EFORMAT; goto done;
            }
    /* Skins: one AthenaSkin3D per glTF skin, shared by the nodes using it. */
    for(cgltf_size k=0;k<data->skins_count;k++) {
        const cgltf_skin *skin=&data->skins[k];
        if(!skin->joints_count||skin->joints_count>ATHENA_MODEL3D_MAX_JOINTS) { code=ATHENA_MODEL3D_EUNSUPPORTED; goto done; }
        AthenaNode3D *joints[ATHENA_MODEL3D_MAX_JOINTS];
        static AthenaMatrix4 bind[ATHENA_MODEL3D_MAX_JOINTS] __attribute__((aligned(16)));
        const cgltf_accessor *ibm=skin->inverse_bind_matrices;
        if(ibm&&(ibm->type!=cgltf_type_mat4||ibm->count<skin->joints_count)) { code=ATHENA_MODEL3D_EFORMAT; goto done; }
        for(cgltf_size j=0;j<skin->joints_count;j++) {
            joints[j]=s->nodes[skin->joints[j]-data->nodes];
            if(ibm&&!cgltf_accessor_read_float(ibm,j,bind[j].value,16)) { code=ATHENA_MODEL3D_EFORMAT; goto done; }
        }
        AthenaSkin3D *made=athena_skin3d_create(joints,(uint32_t)skin->joints_count,ibm?bind:NULL);
        if(!made) { code=ATHENA_MODEL3D_EFORMAT; goto done; }
        for(cgltf_size n=0;n<data->nodes_count;n++)
            if(data->nodes[n].skin==skin) athena_node3d_set_skin(s->nodes[n],made);
        athena_skin3d_release(made);
    }
    /* The default scene's roots, or every parentless node without scenes. */
    const cgltf_scene *scene=data->scene?data->scene:data->scenes_count?&data->scenes[0]:NULL;
    if(scene) {
        for(cgltf_size r=0;r<scene->nodes_count;r++)
            if(athena_node3d_add_child(s->root,s->nodes[scene->nodes[r]-data->nodes])<0) { code=ATHENA_MODEL3D_EFORMAT; goto done; }
    } else for(cgltf_size n=0;n<data->nodes_count;n++)
        if(!data->nodes[n].parent&&athena_node3d_add_child(s->root,s->nodes[n])<0) { code=ATHENA_MODEL3D_EFORMAT; goto done; }
    s->clips=calloc(data->animations_count?data->animations_count:1,sizeof(*s->clips));
    s->clip_names=calloc(data->animations_count?data->animations_count:1,sizeof(*s->clip_names));
    if(!s->clips||!s->clip_names) { code=ATHENA_MODEL3D_ENOMEM; goto done; }
    for(cgltf_size a=0;a<data->animations_count;a++) {
        s->clip_names[a]=copy_name(data->animations[a].name);
        if(!s->clip_names[a]) { code=ATHENA_MODEL3D_ENOMEM; goto done; }
        if((code=load_clip(data,&data->animations[a],&s->clips[a]))<0) { free(s->clip_names[a]); s->clip_names[a]=NULL; goto done; }
        s->clip_count=(uint32_t)a+1;
    }
done:
    cgltf_free(data);
    if(code<0) { athena_gltf3d_release(s); return code; }
    *out=s; return 0;
}
