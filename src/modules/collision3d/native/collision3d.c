#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <athena/float_bits.h>
#include "collision3d_internal.h"
/* A triangle in world space with its unit normal (counter-clockwise front). */
/* lo/hi: its bounds, tested before the per-triangle work of a query. */
typedef struct { float a[3],b[3],c[3],n[3],lo[3],hi[3]; uint32_t layer; int shape; uint32_t index; } Triangle;
typedef struct { int id; uint32_t first,count,layer; } Shape;
/* BVH node: a leaf when count > 0 (order[start..start+count)), else an
 * inner node whose left child follows it and whose right child is right. */
typedef struct { float min[3],max[3]; uint32_t start,count,right; } TreeNode;
struct AthenaCollision3DWorld {
    uint64_t refs;
    Triangle *triangles; uint32_t triangle_count,triangle_capacity;
    Shape *shapes; uint32_t shape_count,shape_capacity;
    int next_id;
    TreeNode *nodes; uint32_t node_count;
    uint32_t *order; float *centroids;
    int dirty;
    int *found; uint32_t found_capacity; /* overlap scratch: one id per shape */
};
#define LEAF_SIZE 4u
#define STACK_DEPTH 64

static void sub3(float o[3],const float a[3],const float b[3]) { o[0]=a[0]-b[0]; o[1]=a[1]-b[1]; o[2]=a[2]-b[2]; }
static float dot3(const float a[3],const float b[3]) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static void cross3(float o[3],const float a[3],const float b[3]) {
    o[0]=a[1]*b[2]-a[2]*b[1]; o[1]=a[2]*b[0]-a[0]*b[2]; o[2]=a[0]*b[1]-a[1]*b[0];
}
static int finite3(const float v[3]) {
    return athena_float_isfinite(v[0])&&athena_float_isfinite(v[1])&&athena_float_isfinite(v[2]);
}

AthenaCollision3DWorld *athena_collision3d_world_create(void) {
    AthenaCollision3DWorld *w=calloc(1,sizeof(*w)); if(!w) return NULL;
    w->refs=1; w->next_id=1; return w;
}
void athena_collision3d_world_retain(AthenaCollision3DWorld *w) { if(w) w->refs++; }
void athena_collision3d_world_release(AthenaCollision3DWorld *w) {
    if(!w||--w->refs) return;
    free(w->triangles); free(w->shapes); free(w->nodes); free(w->order); free(w->centroids); free(w->found); free(w);
}
uint32_t athena_collision3d_triangle_count(const AthenaCollision3DWorld *w) { return w?w->triangle_count:0; }

/* positions: triangle_count * 9 floats; transform applied as column-major. */
static int add_triangles(AthenaCollision3DWorld *w,const float *positions,
    const uint16_t *indices,uint32_t triangle_count,const AthenaMatrix4 *transform,uint32_t layer) {
    if(!w||(triangle_count&&!positions)) return ATHENA_COLLISION3D_EINVAL;
    if(triangle_count>ATHENA_COLLISION3D_MAX_TRIANGLES-w->triangle_count) return ATHENA_COLLISION3D_EFULL;
    if(transform) for(int i=0;i<16;i++) if(!athena_float_isfinite(transform->value[i])) return ATHENA_COLLISION3D_EINVAL;
    for(uint32_t i=0;i<triangle_count*3;i++) {
        uint32_t vertex=indices?indices[i]:i;
        for(unsigned k=0;k<3;k++) if(!athena_float_isfinite(positions[vertex*3+k])) return ATHENA_COLLISION3D_EINVAL;
    }
    if(w->triangle_count+triangle_count>w->triangle_capacity) {
        uint32_t capacity=w->triangle_capacity?w->triangle_capacity:256;
        while(capacity<w->triangle_count+triangle_count) capacity*=2;
        Triangle *t=realloc(w->triangles,capacity*sizeof(*t)); if(!t) return ATHENA_COLLISION3D_ENOMEM;
        w->triangles=t; w->triangle_capacity=capacity;
    }
    if(w->shape_count==w->shape_capacity) {
        uint32_t capacity=w->shape_capacity?w->shape_capacity*2:16;
        Shape *s=realloc(w->shapes,capacity*sizeof(*s)); if(!s) return ATHENA_COLLISION3D_ENOMEM;
        w->shapes=s; w->shape_capacity=capacity;
    }
    int id=w->next_id++;
    if(w->next_id<=0) w->next_id=1;
    uint32_t first=w->triangle_count,added=0;
    const float *m=transform?transform->value:NULL;
    for(uint32_t i=0;i<triangle_count;i++) {
        Triangle *t=&w->triangles[first+added];
        float *out[3]={t->a,t->b,t->c};
        for(int v=0;v<3;v++) {
            uint32_t corner=i*3+v;
            const float *p=&positions[(indices?indices[corner]:corner)*3];
            if(m) for(int r=0;r<3;r++) out[v][r]=m[r]*p[0]+m[4+r]*p[1]+m[8+r]*p[2]+m[12+r];
            else memcpy(out[v],p,3*sizeof(float));
        }
        float e1[3],e2[3],n[3]; sub3(e1,t->b,t->a); sub3(e2,t->c,t->a); cross3(n,e1,e2);
        float length=sqrtf(dot3(n,n));
        if(!(length>1e-12f)||!athena_float_isfinite(length)||!finite3(t->a)||!finite3(t->b)||!finite3(t->c)) continue;
        for(int k=0;k<3;k++) {
            t->n[k]=n[k]/length;
            t->lo[k]=fminf(t->a[k],fminf(t->b[k],t->c[k])); t->hi[k]=fmaxf(t->a[k],fmaxf(t->b[k],t->c[k]));
        }
        t->layer=layer; t->shape=id; t->index=i; added++;
    }
    w->shapes[w->shape_count++]=(Shape){id,first,added,layer};
    w->triangle_count+=added; w->dirty=1;
    return id;
}
int athena_collision3d_add_triangles(AthenaCollision3DWorld *w,const float *positions,
    uint32_t triangle_count,const AthenaMatrix4 *transform,uint32_t layer) {
    return add_triangles(w,positions,NULL,triangle_count,transform,layer);
}
int athena_collision3d_add_mesh(AthenaCollision3DWorld *w,const AthenaMesh3D *mesh,
    const AthenaMatrix4 *transform,uint32_t layer) {
    if(!mesh) return ATHENA_COLLISION3D_EINVAL;
    AthenaMesh3DView v; athena_mesh3d_view(mesh,&v);
    return add_triangles(w,&v.positions[0].x,v.indices,v.vertex_count/3,transform,layer);
}
/* Collects the subtree's visible meshes into one buffer of world triangles. */
typedef struct { float *data; uint32_t count,capacity; } Soup;
static int soup_node(Soup *s,AthenaNode3D *n,const AthenaMatrix4 *parent) {
    if(!athena_node3d_visible(n)) return 0;
    AthenaMatrix4 local,world;
    if(athena_node3d_local(n,&local)<0) return ATHENA_COLLISION3D_EINVAL;
    if(parent) ath_matrix4_multiply(&world,parent,&local); else world=local;
    const AthenaMesh3D *mesh=athena_node3d_mesh(n);
    if(mesh) {
        AthenaMesh3DView v; athena_mesh3d_view(mesh,&v);
        uint32_t count=v.vertex_count/3;
        if(count>ATHENA_COLLISION3D_MAX_TRIANGLES-s->count) return ATHENA_COLLISION3D_EFULL;
        if(s->count+count>s->capacity) {
            uint32_t capacity=s->capacity?s->capacity:256;
            while(capacity<s->count+count) capacity*=2;
            float *d=realloc(s->data,(size_t)capacity*9*sizeof(float)); if(!d) return ATHENA_COLLISION3D_ENOMEM;
            s->data=d; s->capacity=capacity;
        }
        const float *m=world.value;
        for(uint32_t i=0;i<count*3;i++) {
            const AthenaPosition3D *p=&v.positions[athena_mesh3d_corner(&v,i)]; float *o=&s->data[(size_t)s->count*9+i*3];
            for(int r=0;r<3;r++) o[r]=m[r]*p->x+m[4+r]*p->y+m[8+r]*p->z+m[12+r];
        }
        s->count+=count;
    }
    for(uint32_t i=0;i<athena_node3d_child_count(n);i++) {
        int code=soup_node(s,athena_node3d_child(n,i),&world);
        if(code<0) return code;
    }
    return 0;
}
int athena_collision3d_add_node(AthenaCollision3DWorld *w,AthenaNode3D *node,uint32_t layer) {
    if(!w||!node) return ATHENA_COLLISION3D_EINVAL;
    /* The ancestors' composed transform: parents first. */
    AthenaNode3D *chain[ATHENA_SCENE3D_MAX_DEPTH+1]; uint32_t depth=0;
    for(AthenaNode3D *p=athena_node3d_parent(node);p&&depth<ATHENA_SCENE3D_MAX_DEPTH+1;p=athena_node3d_parent(p)) chain[depth++]=p;
    AthenaMatrix4 base; ath_matrix4_identity(&base);
    while(depth--) {
        AthenaMatrix4 local,next;
        if(athena_node3d_local(chain[depth],&local)<0) return ATHENA_COLLISION3D_EINVAL;
        ath_matrix4_multiply(&next,&base,&local); base=next;
    }
    Soup s={0};
    int code=soup_node(&s,node,&base);
    if(code>=0) code=athena_collision3d_add_triangles(w,s.data,s.count,NULL,layer);
    free(s.data); return code;
}
int athena_collision3d_add_box(AthenaCollision3DWorld *w,const float min[3],const float max[3],uint32_t layer) {
    if(!min||!max||!finite3(min)||!finite3(max)) return ATHENA_COLLISION3D_EINVAL;
    for(int i=0;i<3;i++) if(min[i]>max[i]) return ATHENA_COLLISION3D_EINVAL;
    float c[8][3];
    for(int i=0;i<8;i++) { c[i][0]=i&1?max[0]:min[0]; c[i][1]=i&2?max[1]:min[1]; c[i][2]=i&4?max[2]:min[2]; }
    /* Two counter-clockwise triangles per face, seen from outside. */
    static const uint8_t faces[12][3]={{0,2,3},{0,3,1},{4,5,7},{4,7,6},{0,1,5},{0,5,4},
        {2,6,7},{2,7,3},{0,4,6},{0,6,2},{1,3,7},{1,7,5}};
    float p[12*9];
    for(int f=0;f<12;f++) for(int v=0;v<3;v++) memcpy(&p[f*9+v*3],c[faces[f][v]],3*sizeof(float));
    return athena_collision3d_add_triangles(w,p,12,NULL,layer);
}
static Shape *find_shape(AthenaCollision3DWorld *w,int id,uint32_t *index) {
    for(uint32_t i=0;w&&i<w->shape_count;i++) if(w->shapes[i].id==id) { if(index) *index=i; return &w->shapes[i]; }
    return NULL;
}
int athena_collision3d_remove(AthenaCollision3DWorld *w,int id) {
    uint32_t index; Shape *s=find_shape(w,id,&index); if(!s) return ATHENA_COLLISION3D_EINVAL;
    uint32_t first=s->first,count=s->count;
    memmove(&w->triangles[first],&w->triangles[first+count],(w->triangle_count-first-count)*sizeof(Triangle));
    w->triangle_count-=count;
    memmove(&w->shapes[index],&w->shapes[index+1],(w->shape_count-index-1)*sizeof(Shape));
    w->shape_count--;
    for(uint32_t i=index;i<w->shape_count;i++) w->shapes[i].first-=count;
    w->dirty=1; return 0;
}
int athena_collision3d_set_layer(AthenaCollision3DWorld *w,int id,uint32_t layer) {
    Shape *s=find_shape(w,id,NULL); if(!s) return ATHENA_COLLISION3D_EINVAL;
    s->layer=layer;
    /* Layers live in the triangles: the tree keeps its indices. */
    for(uint32_t i=0;i<s->count;i++) w->triangles[s->first+i].layer=layer;
    return 0;
}

/* Tree build: median split on the longest axis of the centroid bounds. */
static void bounds_of(const AthenaCollision3DWorld *w,uint32_t start,uint32_t count,float min[3],float max[3]) {
    for(int k=0;k<3;k++) { min[k]=INFINITY; max[k]=-INFINITY; }
    for(uint32_t i=start;i<start+count;i++) {
        const Triangle *t=&w->triangles[w->order[i]];
        for(int k=0;k<3;k++) {
            if(t->lo[k]<min[k]) min[k]=t->lo[k];
            if(t->hi[k]>max[k]) max[k]=t->hi[k];
        }
    }
}
static const float *sort_keys; static int sort_axis;
static int by_centroid(const void *a,const void *b) {
    float x=sort_keys[*(const uint32_t *)a*3+sort_axis],y=sort_keys[*(const uint32_t *)b*3+sort_axis];
    return (x>y)-(x<y);
}
/* Half the surface area of a box. */
static float half_area(const float lo[3],const float hi[3]) {
    float x=hi[0]-lo[0],y=hi[1]-lo[1],z=hi[2]-lo[2];
    return x*y+y*z+z*x;
}
#define SAH_BINS 12
/* Binned SAH split on the longest centroid axis: the boundary minimizing
 * area(left) * count(left) + area(right) * count(right). Large triangles
 * (floors, walls) end up alone near the root instead of widening every
 * leaf. Returns the left count, or 0 when no split beats a leaf. */
static uint32_t sah_split(AthenaCollision3DWorld *w,uint32_t start,uint32_t count,const float lo[3],const float hi[3],int axis) {
    float extent=hi[axis]-lo[axis];
    if(!(extent>1e-12f)) return 0;
    float scale=SAH_BINS/extent;
    uint32_t bin_count[SAH_BINS]={0}; float bin_lo[SAH_BINS][3],bin_hi[SAH_BINS][3];
    for(int b=0;b<SAH_BINS;b++) for(int k=0;k<3;k++) { bin_lo[b][k]=INFINITY; bin_hi[b][k]=-INFINITY; }
    for(uint32_t i=start;i<start+count;i++) {
        const Triangle *t=&w->triangles[w->order[i]];
        int b=(int)((w->centroids[w->order[i]*3+axis]-lo[axis])*scale); if(b>=SAH_BINS) b=SAH_BINS-1;
        bin_count[b]++;
        for(int k=0;k<3;k++) { bin_lo[b][k]=fminf(bin_lo[b][k],t->lo[k]); bin_hi[b][k]=fmaxf(bin_hi[b][k],t->hi[k]); }
    }
    /* Right-to-left sweeps of areas and counts. */
    float right_area[SAH_BINS]; uint32_t right_count[SAH_BINS];
    float rl[3]={INFINITY,INFINITY,INFINITY},rh[3]={-INFINITY,-INFINITY,-INFINITY}; uint32_t rc=0;
    for(int b=SAH_BINS-1;b>0;b--) {
        rc+=bin_count[b];
        for(int k=0;k<3;k++) { rl[k]=fminf(rl[k],bin_lo[b][k]); rh[k]=fmaxf(rh[k],bin_hi[b][k]); }
        right_area[b]=rc?half_area(rl,rh):0; right_count[b]=rc;
    }
    float ll[3]={INFINITY,INFINITY,INFINITY},lh[3]={-INFINITY,-INFINITY,-INFINITY}; uint32_t lc=0;
    float best_cost=INFINITY; int best=-1;
    for(int b=0;b<SAH_BINS-1;b++) {
        lc+=bin_count[b];
        for(int k=0;k<3;k++) { ll[k]=fminf(ll[k],bin_lo[b][k]); lh[k]=fmaxf(lh[k],bin_hi[b][k]); }
        if(!lc||!right_count[b+1]) continue;
        float cost=half_area(ll,lh)*lc+right_area[b+1]*right_count[b+1];
        if(cost<best_cost) { best_cost=cost; best=b; }
    }
    float node_lo[3],node_hi[3]; bounds_of(w,start,count,node_lo,node_hi);
    if(best<0||best_cost>=half_area(node_lo,node_hi)*count) return 0;
    /* Partition in place by bin. */
    uint32_t i=start,j=start+count;
    while(i<j) {
        int b=(int)((w->centroids[w->order[i]*3+axis]-lo[axis])*scale); if(b>=SAH_BINS) b=SAH_BINS-1;
        if(b<=best) i++;
        else { uint32_t x=w->order[i]; w->order[i]=w->order[--j]; w->order[j]=x; }
    }
    return i-start;
}
static uint32_t build_node(AthenaCollision3DWorld *w,uint32_t start,uint32_t count) {
    uint32_t index=w->node_count++;
    TreeNode *n=&w->nodes[index];
    bounds_of(w,start,count,n->min,n->max);
    if(count<=LEAF_SIZE) { n->start=start; n->count=count; n->right=0; return index; }
    float lo[3]={INFINITY,INFINITY,INFINITY},hi[3]={-INFINITY,-INFINITY,-INFINITY};
    for(uint32_t i=start;i<start+count;i++) for(int k=0;k<3;k++) {
        float c=w->centroids[w->order[i]*3+k]; if(c<lo[k]) lo[k]=c; if(c>hi[k]) hi[k]=c;
    }
    int axis=0; for(int k=1;k<3;k++) if(hi[k]-lo[k]>hi[axis]-lo[axis]) axis=k;
    uint32_t half=sah_split(w,start,count,lo,hi,axis);
    if(!half) {
        /* Median fallback (equal centroids, or no cheaper split). */
        sort_keys=w->centroids; sort_axis=axis;
        qsort(&w->order[start],count,sizeof(uint32_t),by_centroid);
        half=count/2;
    }
    w->nodes[index].count=0; w->nodes[index].start=0;
    build_node(w,start,half);
    uint32_t right=build_node(w,start+half,count-half);
    w->nodes[index].right=right;
    return index;
}
static int ensure_tree(AthenaCollision3DWorld *w) {
    if(!w->dirty) return 0;
    free(w->nodes); free(w->order); free(w->centroids);
    w->nodes=NULL; w->order=NULL; w->centroids=NULL; w->node_count=0;
    if(w->triangle_count) {
        w->nodes=malloc(2*w->triangle_count*sizeof(*w->nodes));
        w->order=malloc(w->triangle_count*sizeof(*w->order));
        w->centroids=malloc(w->triangle_count*3*sizeof(float));
        if(!w->nodes||!w->order||!w->centroids) {
            free(w->nodes); free(w->order); free(w->centroids);
            w->nodes=NULL; w->order=NULL; w->centroids=NULL; return ATHENA_COLLISION3D_ENOMEM;
        }
        for(uint32_t i=0;i<w->triangle_count;i++) {
            const Triangle *t=&w->triangles[i]; w->order[i]=i;
            for(int k=0;k<3;k++) w->centroids[i*3+k]=(t->a[k]+t->b[k]+t->c[k])*(1.0f/3);
        }
        build_node(w,0,w->triangle_count);
    }
    w->dirty=0; return 0;
}
static int box_overlap(const TreeNode *n,const float min[3],const float max[3]) {
    return n->min[0]<=max[0]&&n->max[0]>=min[0]&&n->min[1]<=max[1]&&n->max[1]>=min[1]&&
        n->min[2]<=max[2]&&n->max[2]>=min[2];
}
/* Calls visit(triangle) for every triangle whose leaf box meets [min,max]. */
typedef void (*Visit)(void *context,const Triangle *t);
static void query_box(const AthenaCollision3DWorld *w,const float min[3],const float max[3],uint32_t mask,
    Visit visit,void *context) {
    if(!w->node_count) return;
    uint32_t stack[STACK_DEPTH]; int top=0; stack[top++]=0;
    while(top) {
        const TreeNode *n=&w->nodes[stack[--top]];
        if(!box_overlap(n,min,max)) continue;
        if(n->count) {
            for(uint32_t i=n->start;i<n->start+n->count;i++) {
                const Triangle *t=&w->triangles[w->order[i]];
                if(!(t->layer&mask)||t->lo[0]>max[0]||t->hi[0]<min[0]||t->lo[1]>max[1]||t->hi[1]<min[1]||
                    t->lo[2]>max[2]||t->hi[2]<min[2]) continue;
                visit(context,t);
            }
        } else if(top+2<=STACK_DEPTH) {
            stack[top++]=n->right; stack[top++]=(uint32_t)(n-w->nodes)+1;
        }
    }
}

typedef struct { AthenaCollision3DVisit visit; void *context; } PublicVisit;
static void public_visit(void *context,const Triangle *t) {
    PublicVisit *v=context; v->visit(v->context,t->a,t->b,t->c,t->n,t->shape);
}
int athena_collision3d_query_triangles(AthenaCollision3DWorld *w,const float min[3],const float max[3],
    uint32_t mask,AthenaCollision3DVisit visit,void *context) {
    if(!w||!min||!max||!visit) return ATHENA_COLLISION3D_EINVAL;
    int code=ensure_tree(w); if(code<0) return code;
    PublicVisit v={visit,context};
    query_box(w,min,max,mask,public_visit,&v);
    return 0;
}
/* Entry distance of a ray into a node box within [0, best], or -1. */
static float ray_box(const TreeNode *n,const float origin[3],const float inv[3],float best) {
    float t0=0,t1=best;
    for(int k=0;k<3;k++) {
        float a=(n->min[k]-origin[k])*inv[k],b=(n->max[k]-origin[k])*inv[k];
        if(a>b) { float s=a; a=b; b=s; }
        if(a>t0) t0=a;
        if(b<t1) t1=b;
    }
    return t0<=t1?t0:-1;
}
/* Rays: front-to-back traversal (the nearer child first, boxes beyond the
 * best hit skipped), Moller-Trumbore per triangle without a division until
 * a hit is accepted, both sides. */
int athena_collision3d_raycast(AthenaCollision3DWorld *w,const float origin[3],const float direction[3],
    float max_distance,uint32_t mask,AthenaCollision3DHit *hit) {
    if(!w||!origin||!direction||!hit||!finite3(origin)||!finite3(direction)||
        !athena_float_isfinite(max_distance)||max_distance<0) return ATHENA_COLLISION3D_EINVAL;
    float length=sqrtf(dot3(direction,direction));
    if(!(length>1e-12f)) return ATHENA_COLLISION3D_EINVAL;
    int code=ensure_tree(w); if(code<0) return code;
    float d[3]={direction[0]/length,direction[1]/length,direction[2]/length},inv[3];
    for(int k=0;k<3;k++) inv[k]=fabsf(d[k])>1e-20f?1/d[k]:(d[k]<0?-1e30f:1e30f);
    float best=max_distance; const Triangle *found=NULL;
    if(w->node_count&&ray_box(&w->nodes[0],origin,inv,best)>=0) {
        /* Entries carry the box entry distance, rechecked against best. */
        uint32_t stack[STACK_DEPTH]; float entry[STACK_DEPTH]; int top=0;
        stack[top]=0; entry[top++]=0;
        while(top) {
            top--;
            if(entry[top]>best) continue;
            const TreeNode *n=&w->nodes[stack[top]];
            if(!n->count) {
                uint32_t left=stack[top]+1,right=n->right;
                float tl=ray_box(&w->nodes[left],origin,inv,best),tr=ray_box(&w->nodes[right],origin,inv,best);
                if(top+2>STACK_DEPTH) continue;
                /* Push the farther first: the nearer is popped next. */
                if(tl>=0&&tr>=0) {
                    int near_left=tl<=tr;
                    stack[top]=near_left?right:left; entry[top++]=near_left?tr:tl;
                    stack[top]=near_left?left:right; entry[top++]=near_left?tl:tr;
                } else if(tl>=0) { stack[top]=left; entry[top++]=tl; }
                else if(tr>=0) { stack[top]=right; entry[top++]=tr; }
                continue;
            }
            for(uint32_t i=n->start;i<n->start+n->count;i++) {
                const Triangle *t=&w->triangles[w->order[i]];
                if(!(t->layer&mask)) continue;
                float e1[3],e2[3],p[3],q[3],s[3];
                sub3(e1,t->b,t->a); sub3(e2,t->c,t->a); cross3(p,d,e2);
                float det=dot3(e1,p);
                if(fabsf(det)<1e-12f) continue;
                /* Barycentric tests scaled by det: no division per triangle. */
                float sign=det<0?-1.0f:1.0f,adet=det*sign;
                sub3(s,origin,t->a);
                float u=dot3(s,p)*sign; if(u<0||u>adet) continue;
                cross3(q,s,e1);
                float v=dot3(d,q)*sign; if(v<0||u+v>adet) continue;
                float scaled=dot3(e2,q)*sign;
                if(scaled<0||scaled>best*adet) continue;
                best=scaled/adet; found=t;
            }
        }
    }
    if(!found) return 0;
    hit->distance=best;
    for(int k=0;k<3;k++) hit->point[k]=origin[k]+d[k]*best;
    float sign=dot3(found->n,d)>0?-1.0f:1.0f;
    for(int k=0;k<3;k++) hit->normal[k]=found->n[k]*sign;
    hit->shape=found->shape; hit->triangle=found->index;
    return 1;
}

/* Swept unit sphere against one triangle in ellipsoid space. */
static int lowest_root(float a,float b,float c,float max,float *root) {
    float det=b*b-4*a*c;
    if(det<0||fabsf(a)<1e-20f) return 0;
    float s=sqrtf(det),r1=(-b-s)/(2*a),r2=(-b+s)/(2*a);
    if(r1>r2) { float t=r1; r1=r2; r2=t; }
    if(r1>0&&r1<max) { *root=r1; return 1; }
    if(r2>0&&r2<max) { *root=r2; return 1; }
    return 0;
}
static int inside_triangle(const float p[3],const float a[3],const float b[3],const float c[3]) {
    float v0[3],v1[3],v2[3]; sub3(v0,c,a); sub3(v1,b,a); sub3(v2,p,a);
    float d00=dot3(v0,v0),d01=dot3(v0,v1),d02=dot3(v0,v2),d11=dot3(v1,v1),d12=dot3(v1,v2);
    float denominator=d00*d11-d01*d01; if(fabsf(denominator)<1e-20f) return 0;
    float inverse=1/denominator,u=(d11*d02-d01*d12)*inverse,v=(d00*d12-d01*d02)*inverse;
    return u>=0&&v>=0&&u+v<=1;
}
/* base and velocity also in world units, and 1/radius, per sweep. */
typedef struct { AthenaSweep3D *s; float velocity_length,velocity_sq,best_t,world_base[3],world_velocity[3],inverse[3]; } SweepContext;
static void sweep_triangle(void *context,const Triangle *t) {
    SweepContext *c=context; AthenaSweep3D *s=c->s;
    /* Rejections from the stored world normal, before converting vertices:
     * in ellipsoid space the normal is n_e = (n * radius) / |n * radius|,
     * the facing sign is that of dot(n, world velocity), and the distance
     * to the plane is dot(n, base - a) / |n * radius|. */
    if(dot3(t->n,c->world_velocity)>0) return; /* back face */
    float n[3]={t->n[0]*s->radius[0],t->n[1]*s->radius[1],t->n[2]*s->radius[2]};
    float length=sqrtf(dot3(n,n)); if(!(length>1e-20f)) return;
    float inverse_length=1/length;
    for(int k=0;k<3;k++) n[k]*=inverse_length;
    float to_base[3]; sub3(to_base,c->world_base,t->a);
    float signed_distance=dot3(t->n,to_base)*inverse_length,normal_dot_velocity=dot3(t->n,c->world_velocity)*inverse_length;
    float t0,t1; int embedded=0;
    if(fabsf(normal_dot_velocity)<1e-12f) {
        if(fabsf(signed_distance)>=1) return;
        embedded=1; t0=0; t1=1;
    } else {
        t0=(-1-signed_distance)/normal_dot_velocity; t1=(1-signed_distance)/normal_dot_velocity;
        if(t0>t1) { float x=t0; t0=t1; t1=x; }
        if(t0>1||t1<0) return;
        if(t0<0) t0=0;
        if(t1>1) t1=1;
    }
    float p[3][3];
    for(int k=0;k<3;k++) {
        p[0][k]=t->a[k]*c->inverse[k]; p[1][k]=t->b[k]*c->inverse[k]; p[2][k]=t->c[k]*c->inverse[k];
    }
    float best=c->best_t,point[3]; int found=0,edge=0;
    if(!embedded) {
        float q[3]; for(int k=0;k<3;k++) q[k]=s->base[k]-n[k]+t0*s->velocity[k];
        if(inside_triangle(q,p[0],p[1],p[2])&&t0<best) { best=t0; memcpy(point,q,sizeof(q)); found=1; }
    }
    if(!found) {
        float root;
        for(int v=0;v<3;v++) {
            float d[3]; sub3(d,s->base,p[v]);
            if(lowest_root(c->velocity_sq,2*dot3(s->velocity,d),dot3(d,d)-1,best,&root)) {
                best=root; memcpy(point,p[v],sizeof(point)); found=1; edge=1;
            }
        }
        for(int e=0;e<3;e++) {
            const float *a=p[e],*b=p[(e+1)%3];
            float e_vec[3],to[3]; sub3(e_vec,b,a); sub3(to,a,s->base);
            float edge_sq=dot3(e_vec,e_vec),edge_dot_velocity=dot3(e_vec,s->velocity),edge_dot_to=dot3(e_vec,to);
            float qa=edge_sq*-c->velocity_sq+edge_dot_velocity*edge_dot_velocity;
            float qb=edge_sq*(2*dot3(s->velocity,to))-2*edge_dot_velocity*edge_dot_to;
            float qc=edge_sq*(1-dot3(to,to))+edge_dot_to*edge_dot_to;
            if(lowest_root(qa,qb,qc,best,&root)) {
                float f=(edge_dot_velocity*root-edge_dot_to)/edge_sq;
                if(f>=0&&f<=1) {
                    best=root; found=1; edge=1;
                    for(int k=0;k<3;k++) point[k]=a[k]+f*e_vec[k];
                }
            }
        }
    }
    if(found&&best<=c->best_t) {
        c->best_t=best; s->found=1; s->distance=best*c->velocity_length;
        memcpy(s->point,point,sizeof(point)); s->shape=t->shape; s->triangle=t->index; s->edge=edge;
    }
}
int athena_collision3d_sweep(AthenaCollision3DWorld *w,AthenaSweep3D *s) {
    s->found=0;
    int code=ensure_tree(w); if(code<0) return code;
    SweepContext c={.s=s,.velocity_length=sqrtf(dot3(s->velocity,s->velocity)),.best_t=1};
    if(!(c.velocity_length>1e-12f)) return 0;
    c.velocity_sq=c.velocity_length*c.velocity_length;
    for(int k=0;k<3;k++) {
        c.world_base[k]=s->base[k]*s->radius[k]; c.world_velocity[k]=s->velocity[k]*s->radius[k];
        c.inverse[k]=1/s->radius[k];
    }
    float min[3],max[3];
    for(int k=0;k<3;k++) {
        float a=s->base[k]*s->radius[k],b=(s->base[k]+s->velocity[k])*s->radius[k];
        min[k]=(a<b?a:b)-s->radius[k]*1.01f; max[k]=(a<b?b:a)+s->radius[k]*1.01f;
    }
    query_box(w,min,max,s->mask,sweep_triangle,&c);
    return 0;
}
int athena_collision3d_sphere_cast(AthenaCollision3DWorld *w,const float center[3],float radius,
    const float direction[3],float max_distance,uint32_t mask,AthenaCollision3DHit *hit) {
    if(!w||!center||!direction||!hit||!finite3(center)||!finite3(direction)||!athena_float_isfinite(radius)||
        !(radius>0)||!athena_float_isfinite(max_distance)||max_distance<0) return ATHENA_COLLISION3D_EINVAL;
    float length=sqrtf(dot3(direction,direction));
    if(!(length>1e-12f)) return ATHENA_COLLISION3D_EINVAL;
    AthenaSweep3D s={.radius={radius,radius,radius},.mask=mask};
    for(int k=0;k<3;k++) { s.base[k]=center[k]/radius; s.velocity[k]=direction[k]/length*max_distance/radius; }
    int code=athena_collision3d_sweep(w,&s); if(code<0) return code;
    if(!s.found) return 0;
    float travel=s.distance; /* ellipsoid space = world / radius */
    hit->distance=travel*radius;
    float at[3],normal[3];
    for(int k=0;k<3;k++) at[k]=s.base[k]+s.velocity[k]*(travel/(max_distance/radius));
    sub3(normal,at,s.point);
    float n=sqrtf(dot3(normal,normal));
    for(int k=0;k<3;k++) { hit->point[k]=s.point[k]*radius; hit->normal[k]=n>1e-12f?normal[k]/n:-direction[k]/length; }
    hit->shape=s.shape; hit->triangle=s.triangle;
    return 1;
}
/* Closest point on a triangle (Ericson, Real-Time Collision Detection 5.1.5). */
static void closest_point(float out[3],const float p[3],const Triangle *t) {
    const float *a=t->a,*b=t->b,*c=t->c;
    float ab[3],ac[3],ap[3]; sub3(ab,b,a); sub3(ac,c,a); sub3(ap,p,a);
    float d1=dot3(ab,ap),d2=dot3(ac,ap);
    if(d1<=0&&d2<=0) { memcpy(out,a,3*sizeof(float)); return; }
    float bp[3]; sub3(bp,p,b); float d3=dot3(ab,bp),d4=dot3(ac,bp);
    if(d3>=0&&d4<=d3) { memcpy(out,b,3*sizeof(float)); return; }
    float vc=d1*d4-d3*d2;
    if(vc<=0&&d1>=0&&d3<=0) { float v=d1/(d1-d3); for(int k=0;k<3;k++) out[k]=a[k]+v*ab[k]; return; }
    float cp[3]; sub3(cp,p,c); float d5=dot3(ab,cp),d6=dot3(ac,cp);
    if(d6>=0&&d5<=d6) { memcpy(out,c,3*sizeof(float)); return; }
    float vb=d5*d2-d1*d6;
    if(vb<=0&&d2>=0&&d6<=0) { float w=d2/(d2-d6); for(int k=0;k<3;k++) out[k]=a[k]+w*ac[k]; return; }
    float va=d3*d6-d5*d4;
    if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0) {
        float w=(d4-d3)/((d4-d3)+(d5-d6)); for(int k=0;k<3;k++) out[k]=b[k]+w*(c[k]-b[k]); return;
    }
    float denominator=1/(va+vb+vc),v=vb*denominator,w=vc*denominator;
    for(int k=0;k<3;k++) out[k]=a[k]+ab[k]*v+ac[k]*w;
}
typedef struct { const float *center; float radius_sq; int *found; uint32_t count; } OverlapContext;
static void overlap_triangle(void *context,const Triangle *t) {
    OverlapContext *c=context;
    float q[3],d[3]; closest_point(q,c->center,t); sub3(d,q,c->center);
    if(dot3(d,d)>c->radius_sq) return;
    for(uint32_t i=0;i<c->count;i++) if(c->found[i]==t->shape) return;
    c->found[c->count++]=t->shape; /* at most one per shape: fits the scratch */
}
int athena_collision3d_overlap_sphere(AthenaCollision3DWorld *w,const float center[3],float radius,
    uint32_t mask,int *shapes,uint32_t capacity) {
    if(!w||!center||!finite3(center)||!athena_float_isfinite(radius)||radius<0||(capacity&&!shapes))
        return ATHENA_COLLISION3D_EINVAL;
    int code=ensure_tree(w); if(code<0) return code;
    if(w->found_capacity<w->shape_count) {
        int *found=realloc(w->found,w->shape_count*sizeof(*found)); if(!found) return ATHENA_COLLISION3D_ENOMEM;
        w->found=found; w->found_capacity=w->shape_count;
    }
    float min[3],max[3];
    for(int k=0;k<3;k++) { min[k]=center[k]-radius; max[k]=center[k]+radius; }
    OverlapContext c={center,radius*radius,w->found,0};
    query_box(w,min,max,mask,overlap_triangle,&c);
    if(capacity) memcpy(shapes,w->found,(c.count<capacity?c.count:capacity)*sizeof(int));
    return (int)c.count;
}
