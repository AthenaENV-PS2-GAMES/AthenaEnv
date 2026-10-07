#include <athena/float_bits.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include <athena/scene3d.h>
#include <athena/loop.h>
/* LOCAL: own TRS changed. WORLD: parent changed. SUBTREE: this node or a
 * descendant needs update; set on every ancestor of a flagged node, so update
 * only descends flagged paths and draw checks staleness at the root in O(1). */
enum { NODE_LOCAL=1, NODE_WORLD=2, NODE_SUBTREE=4 };
struct AthenaNode3D {
    AthenaMatrix4 local,world;
    AthenaVector4 position,scale;
    AthenaQuaternion rotation;
    uint64_t refs;
    AthenaNode3D *parent,**children;
    uint32_t child_count,child_capacity;
    AthenaMesh3D *mesh;
    /* Mesh AABB centre and half extents, cached by set_mesh (meshes are
     * immutable), so update() does not copy a mesh view per node. */
    float mesh_mid[3],mesh_half[3];
    float minimum[3],maximum[3];
    uint32_t drawables; /* visible meshes in the subtree at the last update */
    /* Motion integrated by advance(): velocity in parent space (units/s) and
     * spin about a local unit axis (rad/s). moving counts nodes with motion in
     * this subtree, self included, so advance() skips still branches. */
    float velocity[3],spin_axis[3],spin_speed;
    uint32_t moving;
    /* Skinned mesh: deformed by the skin's joints at draw. skinned counts the
     * visible skinned meshes of the subtree at the last update. */
    AthenaSkin3D *skin;
    uint32_t skinned;
    /* Morph target weights; morphed is set while any of them is nonzero. */
    float weights[ATHENA_MODEL3D_MAX_TARGETS];
    uint8_t morphed;
    uint8_t flags,local_valid,visible,has_bounds,scene_root;
};
typedef struct { AthenaNode3D *node; uint32_t pipeline,contained; } QueueItem;
struct AthenaSkin3D {
    uint64_t refs;
    uint32_t count;
    AthenaNode3D **joints;
    AthenaMatrix4 *inverse_bind;
};
/* One per Loop registration: the Loop may release it after a later attach. */
typedef struct Attachment {
    AthenaScene3D *scene;
    void *owner;
    int id;
    struct Attachment *next;
} Attachment;
struct AthenaScene3D {
    uint64_t refs;
    AthenaNode3D *root;
    QueueItem *queue,*sorted;
    uint32_t queue_capacity;
    Attachment *attachment;
};
static Attachment *attached;
const char *athena_scene3d_error(int result) {
    switch(result) {
        case ATHENA_SCENE3D_EINVAL: return "Invalid or overflowing scene transform";
        case ATHENA_SCENE3D_ENOMEM: return "Out of memory";
        case ATHENA_SCENE3D_ECYCLE: return "A node cannot become a child of itself or of a descendant";
        case ATHENA_SCENE3D_EDEPTH: return "Scene hierarchy exceeds MAX_DEPTH";
        case ATHENA_SCENE3D_ESTALE: return "Scene3D is stale: call update() or attach it to the Loop";
        case ATHENA_SCENE3D_EROOT: return "A scene root cannot have a parent";
        case ATHENA_SCENE3D_ERENDER: return "Render3D failed";
        default: return "OK";
    }
}
static void mark(AthenaNode3D *n,uint8_t flags) {
    n->flags|=flags|NODE_SUBTREE;
    for(AthenaNode3D *p=n->parent;p&&!(p->flags&NODE_SUBTREE);p=p->parent) p->flags|=NODE_SUBTREE;
}
AthenaNode3D *athena_node3d_create(void) {
    AthenaNode3D *n=memalign(16,sizeof(*n)); if(!n) return NULL;
    memset(n,0,sizeof(*n));
    n->refs=1; n->visible=1; n->position.w=1; n->scale=(AthenaVector4){1,1,1,0};
    athena_quaternion_identity(&n->rotation);
    ath_matrix4_identity(&n->world); n->flags=NODE_LOCAL|NODE_SUBTREE;
    return n;
}
void athena_node3d_retain(AthenaNode3D *n) { if(n) n->refs++; }
void athena_node3d_release(AthenaNode3D *n) {
    if(!n||--n->refs) return;
    for(uint32_t i=0;i<n->child_count;i++) {
        AthenaNode3D *c=n->children[i];
        c->parent=NULL; mark(c,NODE_WORLD); athena_node3d_release(c);
    }
    free(n->children); athena_mesh3d_release(n->mesh); athena_skin3d_release(n->skin); free(n);
}
AthenaSkin3D *athena_skin3d_create(AthenaNode3D *const *joints,uint32_t count,const AthenaMatrix4 *inverse_bind) {
    if(!joints||!count||count>ATHENA_MODEL3D_MAX_JOINTS) return NULL;
    for(uint32_t i=0;i<count;i++) if(!joints[i]) return NULL;
    if(inverse_bind) for(uint32_t i=0;i<count;i++) for(int k=0;k<16;k++)
        if(!athena_float_isfinite(inverse_bind[i].value[k])) return NULL;
    AthenaSkin3D *k=calloc(1,sizeof(*k)); if(!k) return NULL;
    k->joints=malloc(count*sizeof(*k->joints)); k->inverse_bind=memalign(16,count*sizeof(*k->inverse_bind));
    if(!k->joints||!k->inverse_bind) { free(k->joints); free(k->inverse_bind); free(k); return NULL; }
    for(uint32_t i=0;i<count;i++) {
        k->joints[i]=joints[i]; athena_node3d_retain(joints[i]);
        if(inverse_bind) k->inverse_bind[i]=inverse_bind[i]; else ath_matrix4_identity(&k->inverse_bind[i]);
    }
    k->refs=1; k->count=count; return k;
}
void athena_skin3d_retain(AthenaSkin3D *k) { if(k) k->refs++; }
void athena_skin3d_release(AthenaSkin3D *k) {
    if(!k||--k->refs) return;
    for(uint32_t i=0;i<k->count;i++) athena_node3d_release(k->joints[i]);
    free(k->joints); free(k->inverse_bind); free(k);
}
uint32_t athena_skin3d_joint_count(const AthenaSkin3D *k) { return k?k->count:0; }
int athena_node3d_set_skin(AthenaNode3D *n,AthenaSkin3D *k) {
    if(!n) return ATHENA_SCENE3D_EINVAL;
    if(k==n->skin) return 0;
    athena_skin3d_retain(k); athena_skin3d_release(n->skin); n->skin=k;
    mark(n,0); return 0;
}
/* Bounds of the mesh under the node's morph weights: the base box grown by
 * each weighted target's delta range (exact for one target, conservative for
 * more). Cached as centre and half extents for update(). */
static void morph_box(const AthenaMesh3DView *v,const float weights[],float lo[3],float hi[3]) {
    memcpy(lo,v->minimum,sizeof(v->minimum)); memcpy(hi,v->maximum,sizeof(v->maximum));
    for(uint32_t t=0;t<v->target_count;t++) {
        float w=weights[t]; if(w==0) continue;
        for(int c=0;c<3;c++) {
            float a=w*v->target_minimum[t][c],b=w*v->target_maximum[t][c];
            lo[c]+=a<b?a:b; hi[c]+=a<b?b:a;
        }
    }
}
static void cache_mesh_box(AthenaNode3D *n) {
    if(!n->mesh) return;
    AthenaMesh3DView v; athena_mesh3d_view(n->mesh,&v);
    float lo[3],hi[3]; morph_box(&v,n->weights,lo,hi);
    for(int c=0;c<3;c++) { n->mesh_mid[c]=lo[c]*.5f+hi[c]*.5f; n->mesh_half[c]=hi[c]*.5f-lo[c]*.5f; }
}
int athena_node3d_set_mesh(AthenaNode3D *n,AthenaMesh3D *mesh) {
    if(!n) return ATHENA_SCENE3D_EINVAL;
    if(mesh==n->mesh) return 0;
    athena_mesh3d_retain(mesh); athena_mesh3d_release(n->mesh); n->mesh=mesh;
    cache_mesh_box(n);
    mark(n,0); return 0;
}
int athena_node3d_set_weights(AthenaNode3D *n,const float *weights,uint32_t count) {
    if(!n||count>ATHENA_MODEL3D_MAX_TARGETS||(count&&!weights)) return ATHENA_SCENE3D_EINVAL;
    for(uint32_t t=0;t<count;t++) if(!athena_float_isfinite(weights[t])) return ATHENA_SCENE3D_EINVAL;
    int same=1; uint8_t morphed=0;
    for(uint32_t t=0;t<ATHENA_MODEL3D_MAX_TARGETS;t++) {
        float w=t<count?weights[t]:0;
        if(w!=n->weights[t]) same=0;
        n->weights[t]=w; if(w!=0) morphed=1;
    }
    n->morphed=morphed;
    if(same) return 0; /* animation players set every frame: skip unchanged */
    cache_mesh_box(n); mark(n,0); return 0;
}
void athena_node3d_get_weights(const AthenaNode3D *n,float weights[ATHENA_MODEL3D_MAX_TARGETS]) {
    for(uint32_t t=0;t<ATHENA_MODEL3D_MAX_TARGETS;t++) weights[t]=n?n->weights[t]:0;
}
const AthenaMesh3D *athena_node3d_mesh(const AthenaNode3D *n) { return n?n->mesh:NULL; }
static int finite3(float x,float y,float z) { return athena_float_isfinite(x)&&athena_float_isfinite(y)&&athena_float_isfinite(z); }
int athena_node3d_set_position(AthenaNode3D *n,float x,float y,float z) {
    if(!n||!finite3(x,y,z)) return ATHENA_SCENE3D_EINVAL;
    n->position=(AthenaVector4){x,y,z,1}; n->local_valid=0; mark(n,NODE_LOCAL); return 0;
}
int athena_node3d_set_scale(AthenaNode3D *n,float x,float y,float z) {
    if(!n||!finite3(x,y,z)) return ATHENA_SCENE3D_EINVAL;
    n->scale=(AthenaVector4){x,y,z,0}; n->local_valid=0; mark(n,NODE_LOCAL); return 0;
}
int athena_node3d_set_rotation(AthenaNode3D *n,float x,float y,float z,float w) {
    AthenaQuaternion q={x,y,z,w},normalized;
    if(!n||!athena_quaternion_normalize(&normalized,&q)) return ATHENA_SCENE3D_EINVAL;
    n->rotation=normalized; n->local_valid=0; mark(n,NODE_LOCAL); return 0;
}
int athena_node3d_set_euler(AthenaNode3D *n,float x,float y,float z) {
    if(!n||!finite3(x,y,z)) return ATHENA_SCENE3D_EINVAL;
    AthenaQuaternion q;
    if(!athena_quaternion_euler(&q,x,y,z)) return ATHENA_SCENE3D_EINVAL;
    n->rotation=q; n->local_valid=0; mark(n,NODE_LOCAL); return 0;
}
void athena_node3d_get_trs(const AthenaNode3D *n,float position[3],AthenaQuaternion *rotation,float scale[3]) {
    position[0]=n->position.x; position[1]=n->position.y; position[2]=n->position.z;
    *rotation=n->rotation; scale[0]=n->scale.x; scale[1]=n->scale.y; scale[2]=n->scale.z;
}
int athena_node3d_set_visible(AthenaNode3D *n,int visible) {
    if(!n) return ATHENA_SCENE3D_EINVAL;
    if(n->visible!=!!visible) { n->visible=!!visible; mark(n,0); }
    return 0;
}
int athena_node3d_visible(const AthenaNode3D *n) { return n&&n->visible; }
static uint32_t depth_of(const AthenaNode3D *n) {
    uint32_t depth=0; for(n=n->parent;n;n=n->parent) depth++;
    return depth;
}
static uint32_t height_of(const AthenaNode3D *n) {
    uint32_t height=0;
    for(uint32_t i=0;i<n->child_count;i++) {
        uint32_t h=height_of(n->children[i])+1; if(h>height) height=h;
    }
    return height;
}
static int has_motion(const AthenaNode3D *n) {
    return n->spin_speed!=0||n->velocity[0]!=0||n->velocity[1]!=0||n->velocity[2]!=0;
}
static void add_moving(AthenaNode3D *n,int64_t delta) {
    for(;n;n=n->parent) n->moving=(uint32_t)((int64_t)n->moving+delta);
}
/* Applies a motion change to the subtree counters of n and its ancestors. */
static void motion_changed(AthenaNode3D *n,int before) {
    int after=has_motion(n);
    if(after!=before) add_moving(n,after?1:-1);
}
int athena_node3d_set_velocity(AthenaNode3D *n,float x,float y,float z) {
    if(!n||!finite3(x,y,z)) return ATHENA_SCENE3D_EINVAL;
    int before=has_motion(n);
    n->velocity[0]=x; n->velocity[1]=y; n->velocity[2]=z; motion_changed(n,before); return 0;
}
int athena_node3d_set_spin(AthenaNode3D *n,float x,float y,float z) {
    if(!n||!finite3(x,y,z)) return ATHENA_SCENE3D_EINVAL;
    float speed=sqrtf(x*x+y*y+z*z);
    if(!athena_float_isfinite(speed)) return ATHENA_SCENE3D_EINVAL;
    int before=has_motion(n);
    n->spin_speed=speed;
    if(speed>0) { n->spin_axis[0]=x/speed; n->spin_axis[1]=y/speed; n->spin_axis[2]=z/speed; }
    motion_changed(n,before); return 0;
}
static void remove_from_parent(AthenaNode3D *c) {
    AthenaNode3D *p=c->parent;
    if(c->moving) add_moving(p,-(int64_t)c->moving);
    for(uint32_t i=0;i<p->child_count;i++) if(p->children[i]==c) {
        memmove(&p->children[i],&p->children[i+1],(p->child_count-i-1)*sizeof(*p->children));
        p->child_count--; break;
    }
    c->parent=NULL; mark(p,0); mark(c,NODE_WORLD);
}
int athena_node3d_add_child(AthenaNode3D *parent,AthenaNode3D *child) {
    if(!parent||!child) return ATHENA_SCENE3D_EINVAL;
    if(child->scene_root) return ATHENA_SCENE3D_EROOT;
    for(const AthenaNode3D *a=parent;a;a=a->parent) if(a==child) return ATHENA_SCENE3D_ECYCLE;
    if(child->parent==parent) return 0;
    /* Levels counted from the top ancestor: recursion stays within MAX_DEPTH. */
    if(depth_of(parent)+2+height_of(child)>ATHENA_SCENE3D_MAX_DEPTH) return ATHENA_SCENE3D_EDEPTH;
    if(parent->child_count==parent->child_capacity) {
        uint32_t next=parent->child_capacity?parent->child_capacity*2:4;
        void *grown=realloc(parent->children,next*sizeof(*parent->children));
        if(!grown) return ATHENA_SCENE3D_ENOMEM;
        parent->children=grown; parent->child_capacity=next;
    }
    athena_node3d_retain(child);
    if(child->parent) { remove_from_parent(child); athena_node3d_release(child); }
    parent->children[parent->child_count++]=child; child->parent=parent;
    if(child->moving) add_moving(parent,child->moving);
    mark(child,NODE_WORLD); return 0;
}
void athena_node3d_detach(AthenaNode3D *n) {
    if(!n||!n->parent) return;
    remove_from_parent(n); athena_node3d_release(n);
}
AthenaNode3D *athena_node3d_parent(const AthenaNode3D *n) { return n?n->parent:NULL; }
uint32_t athena_node3d_child_count(const AthenaNode3D *n) { return n?n->child_count:0; }
AthenaNode3D *athena_node3d_child(const AthenaNode3D *n,uint32_t i) { return n&&i<n->child_count?n->children[i]:NULL; }
/* TRS from the stored rotation, which every writer keeps unit length
 * (setters and advance() normalize): no normalization per update, and one
 * finiteness pass over the result. */
static int local_matrix(AthenaNode3D *n) {
    if(!n->local_valid) {
        const AthenaQuaternion *q=&n->rotation; const AthenaVector4 *p=&n->position,*s=&n->scale;
        float x=q->x,y=q->y,z=q->z,w=q->w,xx=x*x,yy=y*y,zz=z*z,xy=x*y,xz=x*z,yz=y*z,xw=x*w,yw=y*w,zw=z*w;
        float *m=n->local.value;
        m[0]=(1-2*(yy+zz))*s->x; m[1]=2*(xy+zw)*s->x; m[2]=2*(xz-yw)*s->x; m[3]=0;
        m[4]=2*(xy-zw)*s->y; m[5]=(1-2*(xx+zz))*s->y; m[6]=2*(yz+xw)*s->y; m[7]=0;
        m[8]=2*(xz+yw)*s->z; m[9]=2*(yz-xw)*s->z; m[10]=(1-2*(xx+yy))*s->z; m[11]=0;
        m[12]=p->x; m[13]=p->y; m[14]=p->z; m[15]=1;
        for(int i=0;i<15;i++) if(!athena_float_isfinite(m[i])) return 0;
        n->local_valid=1;
    }
    return 1;
}
int athena_node3d_local(AthenaNode3D *n,AthenaMatrix4 *out) {
    if(!n||!out) return ATHENA_SCENE3D_EINVAL;
    if(!local_matrix(n)) return ATHENA_SCENE3D_EINVAL;
    *out=n->local; return 0;
}
/* World data is current when no ancestor-or-self transform changed since
 * the last update and the top ancestor is a scene root. */
static int world_current(const AthenaNode3D *n) {
    for(;n;n=n->parent) {
        if(n->flags&(NODE_LOCAL|NODE_WORLD)) return 0;
        if(!n->parent) return n->scene_root;
    }
    return 0;
}
int athena_node3d_world(const AthenaNode3D *n,AthenaMatrix4 *out) {
    if(!n||!out) return ATHENA_SCENE3D_EINVAL;
    if(!world_current(n)) return ATHENA_SCENE3D_ESTALE;
    *out=n->world; return 0;
}
int athena_node3d_world_bounds(const AthenaNode3D *n,float minimum[3],float maximum[3]) {
    if(!n||!minimum||!maximum) return ATHENA_SCENE3D_EINVAL;
    if((n->flags&NODE_SUBTREE)||!world_current(n)) return ATHENA_SCENE3D_ESTALE;
    if(!n->has_bounds) return 0;
    memcpy(minimum,n->minimum,sizeof(n->minimum)); memcpy(maximum,n->maximum,sizeof(n->maximum));
    return 1;
}
/* Affine AABB transform: transformed center plus |M| times the half extents.
 * Float: the R5900 emulates double in software; the finiteness check below
 * rejects overflow on hosts with IEEE arithmetic. */
static int mesh_bounds(const AthenaNode3D *n,float minimum[3],float maximum[3]) {
    const float *m=n->world.value,*mid=n->mesh_mid,*half=n->mesh_half;
    for(int r=0;r<3;r++) {
        float center=m[12+r],extent=0;
        for(int c=0;c<3;c++) { center+=m[c*4+r]*mid[c]; extent+=fabsf(m[c*4+r])*half[c]; }
        minimum[r]=center-extent; maximum[r]=center+extent;
        if(!athena_float_isfinite(minimum[r])||!athena_float_isfinite(maximum[r])) return 0;
    }
    return 1;
}
static void merge(AthenaNode3D *n,const float minimum[3],const float maximum[3]) {
    for(int i=0;i<3;i++) {
        if(!n->has_bounds||minimum[i]<n->minimum[i]) n->minimum[i]=minimum[i];
        if(!n->has_bounds||maximum[i]>n->maximum[i]) n->maximum[i]=maximum[i];
    }
    n->has_bounds=1;
}
static int update_node(AthenaNode3D *n,const AthenaMatrix4 *parent_world,int parent_changed,AthenaScene3DUpdateStats *s) {
    int changed=parent_changed||(n->flags&(NODE_LOCAL|NODE_WORLD));
    s->visited_nodes++;
    if(changed) {
        if(!local_matrix(n)) return ATHENA_SCENE3D_EINVAL;
        AthenaMatrix4 world;
        if(parent_world) ath_matrix4_multiply(&world,parent_world,&n->local); else world=n->local;
        for(int i=0;i<16;i++) if(!athena_float_isfinite(world.value[i])) return ATHENA_SCENE3D_EINVAL;
        n->world=world; s->world_updates++;
    }
    for(uint32_t i=0;i<n->child_count;i++) {
        AthenaNode3D *c=n->children[i];
        if(changed||(c->flags&NODE_SUBTREE)) {
            int code=update_node(c,&n->world,changed,s);
            if(code<0) return code;
        }
    }
    n->has_bounds=0; n->drawables=0; n->skinned=0;
    if(n->visible) {
        float minimum[3],maximum[3];
        if(n->mesh&&n->skin) { n->drawables=1; n->skinned=1; } /* bounds move with the joints */
        else if(n->mesh) {
            if(!mesh_bounds(n,minimum,maximum)) return ATHENA_SCENE3D_EINVAL;
            merge(n,minimum,maximum); n->drawables=1;
        }
        for(uint32_t i=0;i<n->child_count;i++) {
            AthenaNode3D *c=n->children[i];
            if(c->has_bounds) merge(n,c->minimum,c->maximum);
            if(c->has_bounds||c->skinned) n->drawables+=c->drawables;
            n->skinned+=c->skinned;
        }
    }
    s->bounds_updates++;
    n->flags=0; return 0;
}
/* Integrates one node: position += velocity*dt, rotation = rotation * spin
 * step (local axes), renormalized once. */
static int advance_node(AthenaNode3D *n,float dt,uint32_t *moved) {
    if(has_motion(n)) {
        float x=n->position.x+n->velocity[0]*dt,y=n->position.y+n->velocity[1]*dt,z=n->position.z+n->velocity[2]*dt;
        if(!finite3(x,y,z)) return ATHENA_SCENE3D_EINVAL;
        n->position.x=x; n->position.y=y; n->position.z=z;
        if(n->spin_speed>0) {
            float half=n->spin_speed*dt*.5f,sh=sinf(half),w=cosf(half);
            float ax=n->spin_axis[0]*sh,ay=n->spin_axis[1]*sh,az=n->spin_axis[2]*sh;
            const AthenaQuaternion *q=&n->rotation;
            AthenaQuaternion r={q->w*ax+q->x*w+q->y*az-q->z*ay, q->w*ay-q->x*az+q->y*w+q->z*ax,
                q->w*az+q->x*ay-q->y*ax+q->z*w, q->w*w-q->x*ax-q->y*ay-q->z*az};
            if(!athena_quaternion_normalize(&n->rotation,&r)) return ATHENA_SCENE3D_EINVAL;
        }
        n->local_valid=0; mark(n,NODE_LOCAL); (*moved)++;
    }
    for(uint32_t i=0;i<n->child_count;i++) if(n->children[i]->moving) {
        int code=advance_node(n->children[i],dt,moved);
        if(code<0) return code;
    }
    return 0;
}
int athena_scene3d_advance(AthenaScene3D *s,float dt) {
    if(!s||!athena_float_isfinite(dt)||dt<0) return ATHENA_SCENE3D_EINVAL;
    uint32_t moved=0;
    if(dt==0||!s->root->moving) return 0;
    int code=advance_node(s->root,dt,&moved);
    return code<0?code:(int)moved;
}
AthenaScene3D *athena_scene3d_create(void) {
    AthenaScene3D *s=calloc(1,sizeof(*s)); if(!s) return NULL;
    s->root=athena_node3d_create(); if(!s->root) { free(s); return NULL; }
    s->root->scene_root=1; s->refs=1; return s;
}
void athena_scene3d_retain(AthenaScene3D *s) { if(s) s->refs++; }
void athena_scene3d_release(AthenaScene3D *s) {
    if(!s||--s->refs) return;
    s->root->scene_root=0; athena_node3d_release(s->root);
    free(s->queue); free(s->sorted); free(s);
}
AthenaNode3D *athena_scene3d_root(const AthenaScene3D *s) { return s?s->root:NULL; }
int athena_scene3d_stale(const AthenaScene3D *s) { return s&&(s->root->flags&NODE_SUBTREE); }
int athena_scene3d_update(AthenaScene3D *s,AthenaScene3DUpdateStats *stats) {
    AthenaScene3DUpdateStats ignored;
    if(!stats) stats=&ignored;
    memset(stats,0,sizeof(*stats));
    if(!s) return ATHENA_SCENE3D_EINVAL;
    if(!(s->root->flags&NODE_SUBTREE)) return 0;
    return update_node(s->root,NULL,0,stats);
}
static uint32_t pipeline_of(const AthenaMesh3D *mesh) {
    AthenaMesh3DView v; athena_mesh3d_view(mesh,&v);
    return v.material.texture?2:v.material.shading==ATHENA_MATERIAL3D_DIFFUSE?1:0;
}
typedef struct { AthenaScene3D *scene; AthenaCamera3D *camera; AthenaScene3DDrawStats *stats; uint32_t count; } Collect;
/* A subtree whose bounds are INSIDE needs no further frustum tests: every
 * descendant and mesh lies within those bounds, and the frustum is convex. */
static int collect(const AthenaNode3D *n,Collect *c,int inside) {
    if(!n->visible||(!n->has_bounds&&!n->skinned)) return 0;
    int relation=inside?ATHENA_FRUSTUM3D_INSIDE:!n->has_bounds?ATHENA_FRUSTUM3D_INTERSECT:
        athena_camera3d_box_relation(c->camera,NULL,n->minimum,n->maximum);
    if(relation<0) return ATHENA_SCENE3D_EINVAL;
    /* Skinned meshes move with their joints: their subtrees are never culled
     * by the bounds of the last update, and they are not contained. */
    if(n->skinned&&relation!=ATHENA_FRUSTUM3D_INSIDE) relation=ATHENA_FRUSTUM3D_INTERSECT;
    if(relation==ATHENA_FRUSTUM3D_OUTSIDE) {
        c->stats->culled_subtrees++;
        c->stats->render.submitted_objects+=n->drawables; c->stats->render.culled_objects+=n->drawables;
        return 0;
    }
    if(n->mesh) {
        if(c->count==c->scene->queue_capacity) {
            uint32_t next=c->count?c->count*2:32;
            QueueItem *queue=realloc(c->scene->queue,next*sizeof(*queue)); if(!queue) return ATHENA_SCENE3D_ENOMEM;
            c->scene->queue=queue;
            QueueItem *sorted=realloc(c->scene->sorted,next*sizeof(*sorted)); if(!sorted) return ATHENA_SCENE3D_ENOMEM;
            c->scene->sorted=sorted; c->scene->queue_capacity=next;
        }
        c->scene->queue[c->count++]=(QueueItem){(AthenaNode3D *)n,pipeline_of(n->mesh),
            relation==ATHENA_FRUSTUM3D_INSIDE&&!n->skin};
    }
    for(uint32_t i=0;i<n->child_count;i++) {
        int code=collect(n->children[i],c,relation==ATHENA_FRUSTUM3D_INSIDE);
        if(code<0) return code;
    }
    return 0;
}
/* CPU skinning scratch: the deformed positions and normals of one mesh, with
 * the four padding vertices Render3D's VIF unpack may read. Main thread only. */
static AthenaPosition3D *skin_positions,*skin_normals;
static uint32_t skin_capacity;
/* Morph scratch: the blended positions and normals of one mesh, padded like
 * the skin scratch. Main thread only. */
static AthenaPosition3D *morph_positions,*morph_normals;
static uint32_t morph_capacity;
static int scratch(AthenaPosition3D **positions,AthenaPosition3D **normals,uint32_t *capacity,uint32_t count) {
    if(count+4>*capacity) {
        AthenaPosition3D *p=memalign(16,(count+4)*sizeof(*p)),*q=memalign(16,(count+4)*sizeof(*q));
        if(!p||!q) { free(p); free(q); return 0; }
        free(*positions); free(*normals); *positions=p; *normals=q; *capacity=count+4;
    }
    memset(&(*positions)[count],0,4*sizeof(**positions));
    memset(&(*normals)[count],0,4*sizeof(**normals));
    return 1;
}
/* Blends the weighted targets into the morph scratch and points v at it.
 * Normals: blended deltas renormalized, or, for generated flat normals, the
 * face normals of the morphed triangles; otherwise the base normals stay.
 * Bounds: the conservative morph box, not a per-vertex min/max. */
static int morph_view(const AthenaNode3D *n,AthenaMesh3DView *v) {
    if(!scratch(&morph_positions,&morph_normals,&morph_capacity,v->vertex_count)) return ATHENA_SCENE3D_ENOMEM;
    uint32_t active[ATHENA_MODEL3D_MAX_TARGETS],count=0;
    for(uint32_t t=0;t<v->target_count;t++) if(n->weights[t]!=0) active[count++]=t;
    for(uint32_t i=0;i<v->vertex_count;i++) {
        AthenaPosition3D p=v->positions[i];
        for(uint32_t a=0;a<count;a++) {
            uint32_t t=active[a]; float w=n->weights[t];
            const AthenaPosition3D *d=&v->target_positions[t*v->vertex_count+i];
            p.x+=w*d->x; p.y+=w*d->y; p.z+=w*d->z;
        }
        morph_positions[i]=p;
        if(v->normals&&v->target_normals) {
            AthenaPosition3D q=v->normals[i];
            for(uint32_t a=0;a<count;a++) {
                uint32_t t=active[a]; float w=n->weights[t];
                const AthenaPosition3D *d=&v->target_normals[t*v->vertex_count+i];
                q.x+=w*d->x; q.y+=w*d->y; q.z+=w*d->z;
            }
            float length=sqrtf(q.x*q.x+q.y*q.y+q.z*q.z),inverse=length>1e-12f?1/length:0;
            morph_normals[i]=inverse?(AthenaPosition3D){q.x*inverse,q.y*inverse,q.z*inverse}:v->normals[i];
        }
    }
    if(v->normals&&!v->target_normals&&v->flat_normals) for(uint32_t i=0;i<v->vertex_count;i+=3) {
        AthenaPosition3D a=morph_positions[i],b=morph_positions[i+1],c=morph_positions[i+2];
        float ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z,vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;
        float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx,length=sqrtf(nx*nx+ny*ny+nz*nz);
        float inverse=length>1e-12f?1/length:0;
        AthenaPosition3D q=inverse?(AthenaPosition3D){nx*inverse,ny*inverse,nz*inverse}:v->normals[i];
        morph_normals[i]=morph_normals[i+1]=morph_normals[i+2]=q;
    }
    v->positions=morph_positions;
    if(v->normals&&(v->target_normals||v->flat_normals)) v->normals=morph_normals;
    float lo[3],hi[3]; morph_box(v,n->weights,lo,hi);
    memcpy(v->minimum,lo,sizeof(lo)); memcpy(v->maximum,hi,sizeof(hi));
    return 0;
}
static int skin_draw(const AthenaNode3D *n,AthenaMesh3DView v,AthenaCamera3D *camera,const AthenaLights *lights,
    AthenaRender3DCull cull,AthenaRender3DStats *stats,int *render_error) {
    const AthenaSkin3D *k=n->skin;
    if(!v.joints||v.joint_count>k->count) return ATHENA_SCENE3D_EINVAL;
    static AthenaMatrix4 palette[ATHENA_MODEL3D_MAX_JOINTS] __attribute__((aligned(16))); /* 16 KB: off the stack */
    for(uint32_t j=0;j<v.joint_count;j++) {
        AthenaMatrix4 world;
        if(athena_node3d_world(k->joints[j],&world)<0) return ATHENA_SCENE3D_ESTALE;
        ath_matrix4_multiply(&palette[j],&world,&k->inverse_bind[j]);
    }
    /* Conservative world bounds: every deformed vertex is a convex blend of
     * its bind position under its joints, so it lies within the union of the
     * bind-pose box transformed by every joint. Contained meshes deform on
     * VU1; the others here on the EE, then clip in C. */
    if(v.joint_count<=ATHENA_RENDER3D_SKIN_JOINTS&&v.normals&&v.weights8&&!v.material.texture) {
        float lo[3],hi[3];
        for(uint32_t j=0;j<v.joint_count;j++) for(int corner=0;corner<8;corner++) {
            float p[3]={corner&1?v.maximum[0]:v.minimum[0],corner&2?v.maximum[1]:v.minimum[1],corner&4?v.maximum[2]:v.minimum[2]};
            const float *m=palette[j].value;
            for(int r=0;r<3;r++) {
                float w=m[r]*p[0]+m[4+r]*p[1]+m[8+r]*p[2]+m[12+r];
                if((j==0&&corner==0)||w<lo[r]) lo[r]=w;
                if((j==0&&corner==0)||w>hi[r]) hi[r]=w;
            }
        }
        /* Within the GS guard band: VU1 too, the scissor trims the edges. */
        float guard=athena_render3d_guard_band(); int guard_inside=0;
        int relation=athena_camera3d_box_relation_ex(camera,NULL,lo,hi,guard,&guard_inside);
        if(relation<0) return ATHENA_SCENE3D_EINVAL;
        if(relation==ATHENA_FRUSTUM3D_OUTSIDE) { stats->submitted_objects++; stats->culled_objects++; return 0; }
        if(relation==ATHENA_FRUSTUM3D_INTERSECT&&guard>1&&guard_inside) {
            relation=ATHENA_FRUSTUM3D_INSIDE; stats->guard_band_objects++;
        }
        if(relation==ATHENA_FRUSTUM3D_INSIDE) {
            int code=athena_render3d_draw_skinned_contained(&v,palette,v.joint_count,camera,lights,cull,stats);
            if(code<0) { if(render_error) *render_error=code; return ATHENA_SCENE3D_ERENDER; }
            return 0;
        }
    }
    if(!scratch(&skin_positions,&skin_normals,&skin_capacity,v.vertex_count)) return ATHENA_SCENE3D_ENOMEM;
    for(uint32_t i=0;i<v.vertex_count;i++) {
        /* Blend the four joint matrices, then transform once. */
        float m[12]={0};
        const uint8_t *joint=&v.joints[i*4]; const float *weight=&v.weights[i*4];
        for(int w=0;w<4;w++) {
            if(weight[w]==0) continue;
            const float *pm=palette[joint[w]].value;
            for(int c=0;c<4;c++) for(int r=0;r<3;r++) m[c*3+r]+=pm[c*4+r]*weight[w];
        }
        const AthenaPosition3D *p=&v.positions[i];
        float x=m[0]*p->x+m[3]*p->y+m[6]*p->z+m[9],y=m[1]*p->x+m[4]*p->y+m[7]*p->z+m[10],
            z=m[2]*p->x+m[5]*p->y+m[8]*p->z+m[11];
        skin_positions[i]=(AthenaPosition3D){x,y,z};
        float point[3]={x,y,z};
        for(int a=0;a<3;a++) {
            if(i==0||point[a]<v.minimum[a]) v.minimum[a]=point[a];
            if(i==0||point[a]>v.maximum[a]) v.maximum[a]=point[a];
        }
        if(v.normals) {
            const AthenaPosition3D *q=&v.normals[i];
            float nx=m[0]*q->x+m[3]*q->y+m[6]*q->z,ny=m[1]*q->x+m[4]*q->y+m[7]*q->z,nz=m[2]*q->x+m[5]*q->y+m[8]*q->z;
            float length=sqrtf(nx*nx+ny*ny+nz*nz);
            if(!(length>1e-12f)) { nx=q->x; ny=q->y; nz=q->z; length=1; }
            skin_normals[i]=(AthenaPosition3D){nx/length,ny/length,nz/length};
        }
    }
    v.positions=skin_positions; if(v.normals) v.normals=skin_normals;
    v.joints=NULL; v.weights=NULL;
    AthenaMatrix4 identity; ath_matrix4_identity(&identity);
    int code=athena_render3d_draw_view(&v,&identity,camera,lights,cull,stats);
    if(code<0) { if(render_error) *render_error=code; return ATHENA_SCENE3D_ERENDER; }
    return 0;
}
int athena_scene3d_draw(AthenaScene3D *s,AthenaCamera3D *camera,const AthenaLights *lights,
    AthenaRender3DCull cull,AthenaScene3DDrawStats *stats,int *render_error) {
    if(render_error) *render_error=0;
    if(!s||!camera||!stats||(cull!=0&&cull!=1&&cull!=-1)) return ATHENA_SCENE3D_EINVAL;
    memset(stats,0,sizeof(*stats));
    if(s->root->flags&NODE_SUBTREE) return ATHENA_SCENE3D_ESTALE;
    if(!athena_camera3d_update(camera)) return ATHENA_SCENE3D_EINVAL;
    Collect c={s,camera,stats,0};
    int code=collect(s->root,&c,0);
    if(code<0) { memset(stats,0,sizeof(*stats)); return code; }
    /* Counting sort by pipeline: stable, so traversal order breaks ties. */
    uint32_t start[4]={0};
    for(uint32_t i=0;i<c.count;i++) start[s->queue[i].pipeline+1]++;
    for(int p=1;p<4;p++) start[p]+=start[p-1];
    for(uint32_t i=0;i<c.count;i++) s->sorted[start[s->queue[i].pipeline]++]=s->queue[i];
    stats->queued_objects=c.count;
    /* The pipeline order lets consecutive meshes share one render pass. */
    int owned=athena_render3d_group_begin()==0;
    for(uint32_t i=0;i<c.count&&code>=0;i++) {
        const AthenaNode3D *n=s->sorted[i].node;
        if(n->skin||(n->morphed&&athena_mesh3d_target_count(n->mesh))) {
            AthenaMesh3DView v; athena_mesh3d_view(n->mesh,&v);
            if(!n->skin&&n->morphed&&v.target_count) {
                /* VU1 blends it when it can (contained, <= 4 targets, no
                 * normal deltas); otherwise the EE blends below. */
                float lo[3],hi[3]; morph_box(&v,n->weights,lo,hi);
                memcpy(v.minimum,lo,sizeof(lo)); memcpy(v.maximum,hi,sizeof(hi));
                code=athena_render3d_draw_morph(&v,&n->world,n->weights,camera,lights,cull,&stats->render);
                if(code<0) { if(render_error) *render_error=code; code=ATHENA_SCENE3D_ERENDER; break; }
                if(code==0) continue;
                athena_mesh3d_view(n->mesh,&v);
            }
            if(n->morphed&&v.target_count&&(code=morph_view(n,&v))<0) break;
            if(n->skin) { code=skin_draw(n,v,camera,lights,cull,&stats->render,render_error); if(code<0) break; continue; }
            code=athena_render3d_draw_view(&v,&n->world,camera,lights,cull,&stats->render);
            if(code<0) { if(render_error) *render_error=code; code=ATHENA_SCENE3D_ERENDER; }
            continue;
        }
        code=s->sorted[i].contained?athena_render3d_draw_mesh_contained(n->mesh,&n->world,camera,lights,cull,&stats->render):
            athena_render3d_draw_mesh(n->mesh,&n->world,camera,lights,cull,&stats->render);
        if(code<0) { if(render_error) *render_error=code; code=ATHENA_SCENE3D_ERENDER; }
    }
    if(owned) athena_render3d_group_end();
    return code<0?code:0;
}
static int loop_update(void *opaque,AthenaLoopPhase phase,float value) {
    (void)phase;
    Attachment *a=opaque;
    if(athena_scene3d_advance(a->scene,value)<0) return -1;
    return athena_scene3d_update(a->scene,NULL)<0?-1:0;
}
static void unlink_attachment(Attachment *a) {
    for(Attachment **p=&attached;*p;p=&(*p)->next) if(*p==a) { *p=a->next; break; }
    if(a->scene->attachment==a) a->scene->attachment=NULL;
}
/* The Loop calls this once the system is gone, also from systems_clear(). */
static void loop_release(void *opaque) {
    Attachment *a=opaque; unlink_attachment(a);
    athena_scene3d_release(a->scene); free(a);
}
int athena_scene3d_attach_loop(AthenaScene3D *s,int priority,void *owner) {
    if(!s||s->attachment) return ATHENA_SCENE3D_EINVAL;
    Attachment *a=calloc(1,sizeof(*a)); if(!a) return ATHENA_SCENE3D_ENOMEM;
    a->scene=s; a->owner=owner;
    AthenaLoopSystemDesc desc={.priority=priority,.phases=ATHENA_LOOP_PHASE_BIT(ATHENA_LOOP_POST_UPDATE),
        .func=loop_update,.release=loop_release,.opaque=a};
    int id=athena_loop_system_add(&desc);
    if(id<0) { free(a); return id==ATHENA_LOOP_SYSTEM_ENOMEM?ATHENA_SCENE3D_ENOMEM:ATHENA_SCENE3D_EINVAL; }
    athena_scene3d_retain(s);
    a->id=id; a->next=attached; attached=a; s->attachment=a;
    return id;
}
int athena_scene3d_detach_loop(AthenaScene3D *s) {
    if(!s||!s->attachment) return 0;
    Attachment *a=s->attachment;
    /* Unlink now; a removal during a Loop phase defers only the release. */
    unlink_attachment(a);
    athena_loop_system_remove(a->id); return 1;
}
void athena_scene3d_detach_owner(void *owner) {
    for(Attachment *a=attached,*next;a;a=next) {
        next=a->next;
        if(a->owner==owner) athena_scene3d_detach_loop(a->scene);
    }
}
int athena_scene3d_loop_system(const AthenaScene3D *s) { return s&&s->attachment?s->attachment->id:0; }
