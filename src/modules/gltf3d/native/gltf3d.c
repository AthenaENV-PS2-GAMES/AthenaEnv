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
/* Attaches a converted primitive: the first on the node itself, the others
 * on children. Initial morph weights: the node's, else the mesh's, on every
 * primitive (animated weights reach every primitive: the extra ones follow). */
static int attach_primitive(AthenaNode3D *n,const cgltf_node *node,const cgltf_mesh *mesh,cgltf_size p,AthenaMesh3D *m) {
    const cgltf_float *initial=node->weights_count?node->weights:mesh->weights;
    cgltf_size initial_count=node->weights_count?node->weights_count:mesh->weights_count;
    float weights[ATHENA_MODEL3D_MAX_TARGETS]={0};
    if(initial_count>ATHENA_MODEL3D_MAX_TARGETS) {
        athena_model3d_set_detail("mesh '%s' has %u morph weights, at most %u",mesh->name?mesh->name:"",
            (unsigned)initial_count,ATHENA_MODEL3D_MAX_TARGETS);
        return ATHENA_MODEL3D_EUNSUPPORTED;
    }
    for(cgltf_size t=0;t<initial_count;t++) weights[t]=initial[t];
    int code;
    if(p==0) {
        code=athena_node3d_set_mesh(n,m);
        if(code>=0) code=athena_node3d_set_weights(n,weights,(uint32_t)initial_count);
    } else {
        AthenaNode3D *child=athena_node3d_create();
        code=child?athena_node3d_set_mesh(child,m):ATHENA_MODEL3D_ENOMEM;
        if(code>=0) code=athena_node3d_set_weights(child,weights,(uint32_t)initial_count);
        /* Animated weights target the glTF node: every primitive follows. */
        if(code>=0&&mesh->primitives[p].targets_count) code=athena_node3d_set_morph_follower(child,1);
        if(code>=0) code=athena_node3d_add_child(n,child);
        athena_node3d_release(child);
    }
    if(code<0) return code==ATHENA_SCENE3D_ENOMEM?ATHENA_MODEL3D_ENOMEM:ATHENA_MODEL3D_EFORMAT;
    return 0;
}
static int read_floats(const cgltf_accessor *a,float *out,cgltf_size width) {
    for(cgltf_size i=0;i<a->count;i++) if(!cgltf_accessor_read_float(a,i,&out[i*width],width)) return 0;
    return 1;
}
static int load_clip(const cgltf_data *data,const cgltf_animation *anim,AthenaClip3D **out) {
    AthenaTrack3DDesc *tracks=calloc(anim->channels_count?anim->channels_count:1,sizeof(*tracks));
    float **buffers=calloc(anim->channels_count*4+1,sizeof(*buffers));
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
            if(!width) { athena_model3d_set_detail("weights animation on a node without morph targets"); code=ATHENA_MODEL3D_EFORMAT; break; }
            if(width>ATHENA_MODEL3D_MAX_TARGETS) { athena_model3d_set_detail("weights animation with %u targets",(unsigned)width); code=ATHENA_MODEL3D_EUNSUPPORTED; break; }
            path=ATHENA_ANIM3D_WEIGHTS;
        }
        else { athena_model3d_set_detail("animation path %d is not supported",(int)ch->target_path); code=ATHENA_MODEL3D_EUNSUPPORTED; break; }
        const cgltf_accessor *in=sm->input,*outv=sm->output;
        int cubic=sm->interpolation==cgltf_interpolation_type_cubic_spline;
        int weights=path==ATHENA_ANIM3D_WEIGHTS;
        cgltf_size keys=in->count;
        /* Weights outputs are scalars, width of them per key. */
        if(!keys||in->type!=cgltf_type_scalar||outv->count*(weights?1:width)!=keys*(cubic?3:1)*width||
            (weights&&outv->type!=cgltf_type_scalar)) { athena_model3d_set_detail("animation sampler counts disagree"); code=ATHENA_MODEL3D_EFORMAT; break; }
        float *times=malloc(keys*sizeof(float)),*raw=malloc(keys*(cubic?3:1)*width*sizeof(float));
        float *in_tangents=cubic?malloc(keys*width*sizeof(float)):NULL,*out_tangents=cubic?malloc(keys*width*sizeof(float)):NULL;
        buffers[used*4]=times; buffers[used*4+1]=raw; buffers[used*4+2]=in_tangents; buffers[used*4+3]=out_tangents;
        if(!times||!raw||(cubic&&(!in_tangents||!out_tangents))) { code=ATHENA_MODEL3D_ENOMEM; break; }
        if(!read_floats(in,times,1)||!read_floats(outv,raw,weights?1:width)) {
            athena_model3d_set_detail("malformed animation sampler data"); code=ATHENA_MODEL3D_EFORMAT; break;
        }
        /* CUBICSPLINE stores in-tangent, value, out-tangent per key. */
        if(cubic) for(cgltf_size k=0;k<keys;k++) {
            memcpy(&in_tangents[k*width],&raw[k*3*width],width*sizeof(float));
            memcpy(&out_tangents[k*width],&raw[(k*3+2)*width],width*sizeof(float));
            memmove(&raw[k*width],&raw[(k*3+1)*width],width*sizeof(float));
        }
        tracks[used++]=(AthenaTrack3DDesc){.target=(uint32_t)(ch->target_node-data->nodes),.path=path,
            .interpolation=cubic?ATHENA_ANIM3D_CUBIC:sm->interpolation==cgltf_interpolation_type_step?ATHENA_ANIM3D_STEP:ATHENA_ANIM3D_LINEAR,
            .times=times,.values=raw,.key_count=(uint32_t)keys,.weight_count=weights?(uint32_t)width:0,
            .in_tangents=in_tangents,.out_tangents=out_tangents};
    }
    if(code==0) {
        if(!used) code=ATHENA_MODEL3D_EFORMAT;
        else {
            code=athena_clip3d_create(tracks,(uint32_t)used,out);
            if(code==ATHENA_ANIM3D_ENOMEM) code=ATHENA_MODEL3D_ENOMEM;
            else if(code<0) code=ATHENA_MODEL3D_EFORMAT;
        }
    }
    for(cgltf_size i=0;buffers&&i<anim->channels_count*4;i++) free(buffers[i]);
    free(buffers); free(tracks); return code;
}
/* Resumable loader: one athena_gltf3d_loader_step does one bounded unit
 * (parse, validation, one primitive or meshless node, hierarchy links, one
 * skin or one animation). The one-shot entry points run the same steps. */
struct AthenaGltf3DLoader {
    char *origin; unsigned char *owned; const void *bytes; size_t size;
    const void *source; size_t copied; /* source: borrowed until the copy phase ends */
    AthenaMaterial3D material; int override;
    cgltf_data *data; AthenaGltf3D *scene; AthenaGltfTextureCache cache; AthenaGltfPrimitiveBuild *build;
    int unit_done; /* the last step finished a counted unit */
    int external,borrowed; AthenaGltfDependencies deps; /* explicit resolver table (external mode) */
    uint32_t items; /* Mesh3D builder items per step; 0 = default (one-shot loads use the maximum) */
    AthenaLoaderPhaseStats stats[ATHENA_GLTF3D_PHASE_DONE+1];
    int phase,last_phase,code;
    uint32_t node,primitive,skin,clip,units,total;
};
static int loader_create(const char *path,const void *bytes,size_t size,int copy,
    const AthenaMaterial3D *material,AthenaGltf3DLoader **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    athena_model3d_clear_detail();
    if(!path||!*path||(material&&!athena_material3d_validate(material))) return ATHENA_MODEL3D_EINVAL;
    AthenaGltf3DLoader *l=calloc(1,sizeof(*l));
    if(!l) return ATHENA_MODEL3D_ENOMEM;
    l->origin=copy_name(path);
    if(!l->origin) { free(l); return ATHENA_MODEL3D_ENOMEM; }
    if(copy) {
        /* The bytes are copied by the first steps, ATHENA_GLTF3D_COPY_CHUNK each. */
        l->owned=malloc(size);
        if(!l->owned) { free(l->origin); free(l); return ATHENA_MODEL3D_ENOMEM; }
        l->source=bytes; bytes=l->owned;
    }
    l->bytes=bytes; l->size=size;
    athena_material3d_default(&l->material);
    if(material) { l->material=*material; l->override=1; athena_texture3d_retain(l->material.texture); }
    l->phase=l->last_phase=copy?ATHENA_GLTF3D_PHASE_COPY:ATHENA_GLTF3D_PHASE_PARSE;
    *out=l; return 0;
}
void athena_gltf3d_loader_destroy(AthenaGltf3DLoader *l) {
    if(!l) return;
    athena_model3d_gltf_primitive_destroy(l->build);
    athena_model3d_gltf_cache_release(&l->cache); cgltf_free(l->data); athena_model3d_gltf_deps_release(&l->deps); free(l->owned);
    athena_gltf3d_release(l->scene); athena_texture3d_release(l->material.texture);
    free(l->origin); free(l);
}
int athena_gltf3d_loader_begin_memory(const void *bytes,size_t size,const char *origin,
    const AthenaMaterial3D *material,AthenaGltf3DLoader **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL; athena_model3d_clear_detail();
    if(!bytes||!size||size>ATHENA_MODEL3D_MEMORY_MAX_BYTES) return ATHENA_MODEL3D_EINVAL;
    return loader_create(origin,bytes,size,1,material,out);
}
int athena_gltf3d_loader_begin(const void *bytes,size_t size,const char *origin,
    const AthenaMaterial3D *material,int flags,AthenaGltf3DLoader **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL; athena_model3d_clear_detail();
    if(!bytes||!size||size>ATHENA_MODEL3D_MEMORY_MAX_BYTES) return ATHENA_MODEL3D_EINVAL;
    int borrow=(flags&ATHENA_LOADER_BORROW)&&!((uintptr_t)bytes%4);
    int code=loader_create(origin,bytes,size,!borrow,material,out);
    if(code<0) return code;
    AthenaGltf3DLoader *l=*out;
    l->borrowed=borrow;
    if(flags&ATHENA_LOADER_EXTERNAL) { l->external=1; l->cache.deps=&l->deps; }
    return 0;
}
int athena_gltf3d_loader_begin_external(const void *bytes,size_t size,const char *origin,
    const AthenaMaterial3D *material,AthenaGltf3DLoader **out) {
    return athena_gltf3d_loader_begin(bytes,size,origin,material,ATHENA_LOADER_EXTERNAL,out);
}
uint32_t athena_gltf3d_loader_dependency_count(const AthenaGltf3DLoader *l) { return l?l->deps.count:0; }
const char *athena_gltf3d_loader_dependency_uri(const AthenaGltf3DLoader *l,uint32_t i) { return l&&i<l->deps.count?l->deps.items[i].uri:NULL; }
int athena_gltf3d_loader_dependency_image(const AthenaGltf3DLoader *l,uint32_t i) { return l&&i<l->deps.count&&l->deps.items[i].image; }
int athena_gltf3d_loader_dependency_provided(const AthenaGltf3DLoader *l,uint32_t i) { return l&&i<l->deps.count&&l->deps.items[i].bytes!=NULL; }
int athena_gltf3d_loader_provide(AthenaGltf3DLoader *l,uint32_t index,const void *bytes,size_t size) {
    if(!l||l->phase!=ATHENA_GLTF3D_PHASE_DEPENDENCIES) return ATHENA_MODEL3D_EINVAL;
    return athena_model3d_gltf_deps_provide(&l->deps,index,bytes,size);
}
static uint32_t primitive_units(const cgltf_node *node) {
    return node->mesh&&node->mesh->primitives_count>1?(uint32_t)node->mesh->primitives_count:1;
}
static int step_copy(AthenaGltf3DLoader *l) {
    size_t n=l->size-l->copied;
    if(n>ATHENA_GLTF3D_COPY_CHUNK) n=ATHENA_GLTF3D_COPY_CHUNK;
    memcpy(l->owned+l->copied,(const unsigned char *)l->source+l->copied,n);
    l->copied+=n;
    if(l->copied==l->size) { l->source=NULL; l->phase=ATHENA_GLTF3D_PHASE_PARSE; }
    return 0;
}
static int step_parse(AthenaGltf3DLoader *l) {
    cgltf_options options={0};
    if(l->external) {
        int code=athena_model3d_gltf_parse_external(l->bytes,l->size,&l->data,&l->deps);
        if(code<0) return code;
        l->phase=l->deps.count?ATHENA_GLTF3D_PHASE_DEPENDENCIES:ATHENA_GLTF3D_PHASE_VALIDATE; return 0;
    }
    if(l->bytes) {
        int code=athena_model3d_gltf_parse_memory(l->bytes,l->size,&l->data);
        if(code<0) return code;
    } else {
        cgltf_result parsed=cgltf_parse_file(&options,l->origin,&l->data);
        if(parsed!=cgltf_result_success) {
            athena_model3d_set_detail("glTF parse error %d",(int)parsed);
            return parsed==cgltf_result_out_of_memory?ATHENA_MODEL3D_ENOMEM:
                parsed==cgltf_result_file_not_found||parsed==cgltf_result_io_error?ATHENA_MODEL3D_EIO:ATHENA_MODEL3D_EFORMAT;
        }
    }
    l->phase=ATHENA_GLTF3D_PHASE_VALIDATE; return 0;
}
/* External mode: waits (returns 2) until the caller provided every file. */
static int step_dependencies(AthenaGltf3DLoader *l) {
    if(l->deps.provided<l->deps.count) { l->unit_done=0; return ATHENA_GLTF3D_WAITING; }
    l->phase=ATHENA_GLTF3D_PHASE_VALIDATE; l->unit_done=0; return 0;
}
static int step_validate(AthenaGltf3DLoader *l) {
    cgltf_data *data=l->data; int code;
    if((code=athena_model3d_gltf_extensions(data))<0) return code;
    for(cgltf_size n=0;n<data->nodes_count;n++)
        if(data->nodes[n].has_mesh_gpu_instancing) {
            athena_model3d_set_detail("EXT_mesh_gpu_instancing is not supported"); return ATHENA_MODEL3D_EUNSUPPORTED;
        }
    if(l->external) {
        if((code=athena_model3d_gltf_bind_buffers(data,&l->deps))<0) return code;
    } else if(!l->bytes) {
        cgltf_options options={0};
        cgltf_result parsed=cgltf_load_buffers(&options,data,l->origin);
        if(parsed!=cgltf_result_success) {
            athena_model3d_set_detail("cannot load glTF buffers (error %d)",(int)parsed);
            return parsed==cgltf_result_out_of_memory?ATHENA_MODEL3D_ENOMEM:ATHENA_MODEL3D_EIO;
        }
    }
    if(cgltf_validate(data)!=cgltf_result_success) { athena_model3d_set_detail("glTF validation failed"); return ATHENA_MODEL3D_EFORMAT; }
    AthenaGltf3D *s=calloc(1,sizeof(*s));
    if(!s) return ATHENA_MODEL3D_ENOMEM;
    l->scene=s;
    s->root=athena_node3d_create();
    s->nodes=calloc(data->nodes_count?data->nodes_count:1,sizeof(*s->nodes));
    s->node_names=calloc(data->nodes_count?data->nodes_count:1,sizeof(*s->node_names));
    if(!s->root||!s->nodes||!s->node_names) return ATHENA_MODEL3D_ENOMEM;
    /* Units: copy chunks, parse, validate, nodes, links, skins and clips. */
    uint64_t total=3+(uint64_t)data->skins_count+data->animations_count+
        (l->owned?(l->size+ATHENA_GLTF3D_COPY_CHUNK-1)/ATHENA_GLTF3D_COPY_CHUNK:0);
    for(cgltf_size n=0;n<data->nodes_count;n++) total+=primitive_units(&data->nodes[n]);
    l->total=total>UINT32_MAX?UINT32_MAX:(uint32_t)total;
    l->phase=data->nodes_count?ATHENA_GLTF3D_PHASE_NODES:ATHENA_GLTF3D_PHASE_LINKS;
    return 0;
}
/* A node and its primitives. The node is created with the first slice of its
 * first primitive; each later step advances the primitive's build (texture
 * decode, vertex/index reads, one morph attribute or a mesh builder stage)
 * and a finished primitive is attached. A meshless node is one unit. */
static int step_nodes(AthenaGltf3DLoader *l) {
    cgltf_data *data=l->data; AthenaGltf3D *s=l->scene; cgltf_size n=l->node;
    const cgltf_node *g=&data->nodes[n]; int code;
    const int meshy=g->mesh&&g->mesh->primitives_count;
    if(!l->primitive&&!l->build&&!s->nodes[n]) {
        s->nodes[n]=athena_node3d_create(); s->node_names[n]=copy_name(g->name);
        s->node_count=(uint32_t)n+1;
        if(!s->nodes[n]||!s->node_names[n]) return ATHENA_MODEL3D_ENOMEM;
        if((code=set_trs(s->nodes[n],g))<0) return code;
    }
    if(meshy) {
        const AthenaMaterial3D *material=l->override?&l->material:NULL;
        if(!l->build) {
            code=athena_model3d_gltf_primitive_begin(l->origin,&g->mesh->primitives[l->primitive],material,&l->cache,&l->build);
            if(code<0) goto failed;
        }
        code=athena_model3d_gltf_primitive_step(l->build,l->items);
        if(code<0) goto failed;
        if(code==0) { l->unit_done=0; return 0; }
        AthenaMesh3D *mesh=NULL;
        code=athena_model3d_gltf_primitive_take(l->build,&mesh);
        athena_model3d_gltf_primitive_destroy(l->build); l->build=NULL;
        if(code<0) goto failed;
        code=attach_primitive(s->nodes[n],g,g->mesh,l->primitive,mesh);
        athena_mesh3d_release(mesh);
        if(code<0) goto failed;
    }
    if(meshy&&++l->primitive<g->mesh->primitives_count) return 0;
    l->primitive=0;
    if(++l->node>=data->nodes_count) l->phase=ATHENA_GLTF3D_PHASE_LINKS;
    return 0;
failed:
    if(meshy) athena_model3d_detail_context("mesh '%s' primitive %u",g->mesh->name?g->mesh->name:"",(unsigned)l->primitive);
    return code;
}
static int step_links(AthenaGltf3DLoader *l) {
    cgltf_data *data=l->data; AthenaGltf3D *s=l->scene;
    for(cgltf_size n=0;n<data->nodes_count;n++)
        for(cgltf_size c=0;c<data->nodes[n].children_count;c++)
            if(athena_node3d_add_child(s->nodes[n],s->nodes[data->nodes[n].children[c]-data->nodes])<0) return ATHENA_MODEL3D_EFORMAT;
    /* The default scene's roots, or every parentless node without scenes. */
    const cgltf_scene *scene=data->scene?data->scene:data->scenes_count?&data->scenes[0]:NULL;
    if(scene) {
        for(cgltf_size r=0;r<scene->nodes_count;r++)
            if(athena_node3d_add_child(s->root,s->nodes[scene->nodes[r]-data->nodes])<0) return ATHENA_MODEL3D_EFORMAT;
    } else for(cgltf_size n=0;n<data->nodes_count;n++)
        if(!data->nodes[n].parent&&athena_node3d_add_child(s->root,s->nodes[n])<0) return ATHENA_MODEL3D_EFORMAT;
    s->clips=calloc(data->animations_count?data->animations_count:1,sizeof(*s->clips));
    s->clip_names=calloc(data->animations_count?data->animations_count:1,sizeof(*s->clip_names));
    if(!s->clips||!s->clip_names) return ATHENA_MODEL3D_ENOMEM;
    l->phase=data->skins_count?ATHENA_GLTF3D_PHASE_SKINS:data->animations_count?ATHENA_GLTF3D_PHASE_CLIPS:ATHENA_GLTF3D_PHASE_DONE;
    return 0;
}
/* One AthenaSkin3D per glTF skin, shared by the nodes using it. */
static int step_skin(AthenaGltf3DLoader *l) {
    cgltf_data *data=l->data; AthenaGltf3D *s=l->scene;
    const cgltf_skin *skin=&data->skins[l->skin];
    if(!skin->joints_count||skin->joints_count>ATHENA_MODEL3D_MAX_JOINTS) {
        athena_model3d_set_detail("skin with %u joints, 1..%u supported",(unsigned)skin->joints_count,ATHENA_MODEL3D_MAX_JOINTS);
        return ATHENA_MODEL3D_EUNSUPPORTED;
    }
    AthenaNode3D *joints[ATHENA_MODEL3D_MAX_JOINTS];
    static AthenaMatrix4 bind[ATHENA_MODEL3D_MAX_JOINTS] __attribute__((aligned(16)));
    const cgltf_accessor *ibm=skin->inverse_bind_matrices;
    if(ibm&&(ibm->type!=cgltf_type_mat4||ibm->count<skin->joints_count)) return ATHENA_MODEL3D_EFORMAT;
    for(cgltf_size j=0;j<skin->joints_count;j++) {
        joints[j]=s->nodes[skin->joints[j]-data->nodes];
        if(ibm&&!cgltf_accessor_read_float(ibm,j,bind[j].value,16)) return ATHENA_MODEL3D_EFORMAT;
    }
    AthenaSkin3D *made=athena_skin3d_create(joints,(uint32_t)skin->joints_count,ibm?bind:NULL);
    if(!made) return ATHENA_MODEL3D_EFORMAT;
    for(cgltf_size n=0;n<data->nodes_count;n++)
        if(data->nodes[n].skin==skin) athena_node3d_set_skin(s->nodes[n],made);
    athena_skin3d_release(made);
    if(++l->skin>=data->skins_count) l->phase=data->animations_count?ATHENA_GLTF3D_PHASE_CLIPS:ATHENA_GLTF3D_PHASE_DONE;
    return 0;
}
static int step_clip(AthenaGltf3DLoader *l) {
    cgltf_data *data=l->data; AthenaGltf3D *s=l->scene; uint32_t a=l->clip;
    s->clip_names[a]=copy_name(data->animations[a].name);
    if(!s->clip_names[a]) return ATHENA_MODEL3D_ENOMEM;
    int code=load_clip(data,&data->animations[a],&s->clips[a]);
    if(code<0) { free(s->clip_names[a]); s->clip_names[a]=NULL; return code; }
    s->clip_count=a+1;
    if(++l->clip>=data->animations_count) l->phase=ATHENA_GLTF3D_PHASE_DONE;
    return 0;
}
int athena_gltf3d_loader_step(AthenaGltf3DLoader *l) {
    if(!l) return ATHENA_MODEL3D_EINVAL;
    if(l->code<0) return l->code;
    if(l->phase==ATHENA_GLTF3D_PHASE_DONE) return 1;
    int code=0;
    l->unit_done=1;
    l->last_phase=l->phase;
    uint32_t clock_start=athena_model3d_clock();
    switch(l->phase) {
    case ATHENA_GLTF3D_PHASE_COPY: code=step_copy(l); break;
    case ATHENA_GLTF3D_PHASE_PARSE: code=step_parse(l); break;
    case ATHENA_GLTF3D_PHASE_DEPENDENCIES: code=step_dependencies(l); break;
    case ATHENA_GLTF3D_PHASE_VALIDATE: code=step_validate(l); break;
    case ATHENA_GLTF3D_PHASE_NODES: code=step_nodes(l); break;
    case ATHENA_GLTF3D_PHASE_LINKS: code=step_links(l); break;
    case ATHENA_GLTF3D_PHASE_SKINS: code=step_skin(l); break;
    default: code=step_clip(l); break;
    }
    if(code<0) { l->code=code; return code; }
    if(code==ATHENA_GLTF3D_WAITING) return code;
    {
        float ms=athena_model3d_clock_ms(athena_model3d_clock()-clock_start);
        AthenaLoaderPhaseStats *st=&l->stats[l->last_phase];
        st->units++; st->total_ms+=ms; if(ms>st->max_ms) st->max_ms=ms;
    }
    l->units+=l->unit_done;
    return l->phase==ATHENA_GLTF3D_PHASE_DONE?1:0;
}
int athena_gltf3d_loader_source_needed(const AthenaGltf3DLoader *l) { return l&&(l->source!=NULL||l->borrowed)&&l->code>=0; }
int athena_gltf3d_loader_run(AthenaGltf3DLoader *l,float budget_ms,uint32_t *ran) {
    uint32_t start=athena_model3d_clock(),n=0; int code; float last_ms=0;
    /* Another unit only starts when the previous one's duration still fits in the budget,
     * so a slice overshoots only when a unit is slower than the one before it. */
    do {
        uint32_t unit_start=athena_model3d_clock();
        code=athena_gltf3d_loader_step(l);
        if(code==ATHENA_GLTF3D_WAITING) break;
        last_ms=athena_model3d_clock_ms(athena_model3d_clock()-unit_start);
        n++;
    } while(code==0&&athena_model3d_clock_ms(athena_model3d_clock()-start)+last_ms<budget_ms);
    if(ran) *ran=n;
    return code;
}
void athena_gltf3d_loader_phase_stats(const AthenaGltf3DLoader *l,int phase,AthenaLoaderPhaseStats *out) {
    if(out) *out=l&&phase>=0&&phase<=ATHENA_GLTF3D_PHASE_DONE?l->stats[phase]:(AthenaLoaderPhaseStats){0};
}
int athena_gltf3d_loader_last_phase(const AthenaGltf3DLoader *l) { return l?l->last_phase:0; }
int athena_gltf3d_loader_phase(const AthenaGltf3DLoader *l) { return l?l->phase:0; }
void athena_gltf3d_loader_progress(const AthenaGltf3DLoader *l,uint32_t *done,uint32_t *total) {
    if(done) *done=l?l->units:0;
    if(total) *total=l?l->total:0;
}
int athena_gltf3d_loader_take(AthenaGltf3DLoader *l,AthenaGltf3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    if(!l||l->code<0||l->phase!=ATHENA_GLTF3D_PHASE_DONE||!l->scene) return ATHENA_MODEL3D_EINVAL;
    *out=l->scene; l->scene=NULL;
    return 0;
}
static int load_source(const char *path,const void *bytes,size_t size,
    const AthenaMaterial3D *material,AthenaGltf3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    AthenaGltf3DLoader *l=NULL;
    int code=loader_create(path,bytes,size,0,material,&l);
    if(l) l->items=UINT32_MAX;
    while(code==0) code=athena_gltf3d_loader_step(l);
    if(code==1) code=athena_gltf3d_loader_take(l,out);
    athena_gltf3d_loader_destroy(l);
    return code;
}
int athena_gltf3d_load(const char *path,const AthenaMaterial3D *material,AthenaGltf3D **out) {
    return load_source(path,NULL,0,material,out);
}
int athena_gltf3d_load_memory(const void *bytes,size_t size,const char *origin,
    const AthenaMaterial3D *material,AthenaGltf3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL; athena_model3d_clear_detail();
    if(!bytes||!size||size>ATHENA_MODEL3D_MEMORY_MAX_BYTES) return ATHENA_MODEL3D_EINVAL;
    return load_source(origin,bytes,size,material,out);
}
