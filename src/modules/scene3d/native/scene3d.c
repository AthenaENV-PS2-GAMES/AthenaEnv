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
    float minimum[3],maximum[3];
    uint32_t drawables; /* visible meshes in the subtree at the last update */
    uint8_t flags,local_valid,visible,has_bounds,scene_root;
};
typedef struct { AthenaNode3D *node; uint32_t pipeline,contained; } QueueItem;
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
    free(n->children); athena_mesh3d_release(n->mesh); free(n);
}
int athena_node3d_set_mesh(AthenaNode3D *n,AthenaMesh3D *mesh) {
    if(!n) return ATHENA_SCENE3D_EINVAL;
    if(mesh==n->mesh) return 0;
    athena_mesh3d_retain(mesh); athena_mesh3d_release(n->mesh); n->mesh=mesh;
    mark(n,0); return 0;
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
    AthenaQuaternion qx,qy,qz,q;
    athena_quaternion_axis_angle(&qx,1,0,0,x);
    athena_quaternion_axis_angle(&qy,0,1,0,y);
    athena_quaternion_axis_angle(&qz,0,0,1,z);
    athena_quaternion_multiply(&q,&qy,&qx); athena_quaternion_multiply(&q,&qz,&q);
    n->rotation=q; n->local_valid=0; mark(n,NODE_LOCAL); return 0;
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
static void remove_from_parent(AthenaNode3D *c) {
    AthenaNode3D *p=c->parent;
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
    mark(child,NODE_WORLD); return 0;
}
void athena_node3d_detach(AthenaNode3D *n) {
    if(!n||!n->parent) return;
    remove_from_parent(n); athena_node3d_release(n);
}
AthenaNode3D *athena_node3d_parent(const AthenaNode3D *n) { return n?n->parent:NULL; }
uint32_t athena_node3d_child_count(const AthenaNode3D *n) { return n?n->child_count:0; }
AthenaNode3D *athena_node3d_child(const AthenaNode3D *n,uint32_t i) { return n&&i<n->child_count?n->children[i]:NULL; }
static int local_matrix(AthenaNode3D *n) {
    if(!n->local_valid) {
        if(!athena_quaternion_trs(&n->local,&n->position,&n->rotation,&n->scale)) return 0;
        for(int i=0;i<16;i++) if(!athena_float_isfinite(n->local.value[i])) return 0;
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
    AthenaMesh3DView v; athena_mesh3d_view(n->mesh,&v);
    const float *m=n->world.value;
    float mid[3],half[3];
    for(int c=0;c<3;c++) {
        mid[c]=v.minimum[c]*.5f+v.maximum[c]*.5f; half[c]=v.maximum[c]*.5f-v.minimum[c]*.5f;
    }
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
    n->has_bounds=0; n->drawables=0;
    if(n->visible) {
        float minimum[3],maximum[3];
        if(n->mesh) {
            if(!mesh_bounds(n,minimum,maximum)) return ATHENA_SCENE3D_EINVAL;
            merge(n,minimum,maximum); n->drawables=1;
        }
        for(uint32_t i=0;i<n->child_count;i++) {
            AthenaNode3D *c=n->children[i];
            if(c->has_bounds) { merge(n,c->minimum,c->maximum); n->drawables+=c->drawables; }
        }
    }
    s->bounds_updates++;
    n->flags=0; return 0;
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
    if(!n->visible||!n->has_bounds) return 0;
    int relation=inside?ATHENA_FRUSTUM3D_INSIDE:athena_camera3d_box_relation(c->camera,NULL,n->minimum,n->maximum);
    if(relation<0) return ATHENA_SCENE3D_EINVAL;
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
            relation==ATHENA_FRUSTUM3D_INSIDE};
    }
    for(uint32_t i=0;i<n->child_count;i++) {
        int code=collect(n->children[i],c,relation==ATHENA_FRUSTUM3D_INSIDE);
        if(code<0) return code;
    }
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
    for(uint32_t i=0;i<c.count;i++) {
        const AthenaNode3D *n=s->sorted[i].node;
        code=s->sorted[i].contained?athena_render3d_draw_mesh_contained(n->mesh,&n->world,camera,lights,cull,&stats->render):
            athena_render3d_draw_mesh(n->mesh,&n->world,camera,lights,cull,&stats->render);
        if(code<0) { if(render_error) *render_error=code; return ATHENA_SCENE3D_ERENDER; }
    }
    return 0;
}
static int loop_update(void *opaque,AthenaLoopPhase phase,float value) {
    (void)phase; (void)value;
    Attachment *a=opaque;
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
