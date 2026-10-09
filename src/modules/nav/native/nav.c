#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <athena/nav.h>
#include <athena/float_bits.h>

struct AthenaNavGrid {
    uint64_t refs;
    uint32_t w,d,n;
    float cell,ox,oz;
    uint8_t *cost;
    /* A* state, reused by every search: g score, parent, heap position
     * (index + 1 while open), and the search stamp of the cell's data. */
    float *g; int32_t *parent; uint32_t *heap_pos,*stamp,*heap; float *f;
    uint32_t search,heap_size,expanded;
};
AthenaNavGrid *athena_nav_grid_create(uint32_t w,uint32_t d,float cell,float ox,float oz) {
    if(!w||!d||(uint64_t)w*d>ATHENA_NAV_MAX_CELLS||!athena_float_isfinite(cell)||cell<=0||
        !athena_float_isfinite(ox)||!athena_float_isfinite(oz)) return NULL;
    AthenaNavGrid *g=calloc(1,sizeof(*g)); if(!g) return NULL;
    g->refs=1; g->w=w; g->d=d; g->n=w*d; g->cell=cell; g->ox=ox; g->oz=oz;
    g->cost=malloc(g->n); g->g=malloc(g->n*sizeof(float)); g->f=malloc(g->n*sizeof(float));
    g->parent=malloc(g->n*sizeof(int32_t)); g->heap_pos=malloc(g->n*sizeof(uint32_t));
    g->stamp=calloc(g->n,sizeof(uint32_t)); g->heap=malloc(g->n*sizeof(uint32_t));
    if(!g->cost||!g->g||!g->f||!g->parent||!g->heap_pos||!g->stamp||!g->heap) { athena_nav_grid_release(g); return NULL; }
    memset(g->cost,1,g->n);
    return g;
}
void athena_nav_grid_retain(AthenaNavGrid *g) { if(g) g->refs++; }
void athena_nav_grid_release(AthenaNavGrid *g) {
    if(!g||--g->refs) return;
    free(g->cost); free(g->g); free(g->f); free(g->parent); free(g->heap_pos); free(g->stamp); free(g->heap); free(g);
}
void athena_nav_grid_size(const AthenaNavGrid *g,uint32_t *w,uint32_t *d,float *cell,float *ox,float *oz) {
    if(w) *w=g->w;
    if(d) *d=g->d;
    if(cell) *cell=g->cell;
    if(ox) *ox=g->ox;
    if(oz) *oz=g->oz;
}
static inline int in_grid(const AthenaNavGrid *g,int32_t x,int32_t z) { return x>=0&&z>=0&&(uint32_t)x<g->w&&(uint32_t)z<g->d; }
uint8_t athena_nav_get_cost(const AthenaNavGrid *g,int32_t x,int32_t z) { return g&&in_grid(g,x,z)?g->cost[(uint32_t)z*g->w+(uint32_t)x]:0; }
int athena_nav_set_cost(AthenaNavGrid *g,int32_t x,int32_t z,uint8_t c) {
    if(!g||!in_grid(g,x,z)) return -1;
    g->cost[(uint32_t)z*g->w+(uint32_t)x]=c; return 0;
}
int athena_nav_fill(AthenaNavGrid *g,int32_t x0,int32_t z0,int32_t x1,int32_t z1,uint8_t c) {
    if(!g) return -1;
    if(x0>x1) { int32_t t=x0; x0=x1; x1=t; }
    if(z0>z1) { int32_t t=z0; z0=z1; z1=t; }
    if(x0<0) x0=0;
    if(z0<0) z0=0;
    if(x1>=(int32_t)g->w) x1=(int32_t)g->w-1;
    if(z1>=(int32_t)g->d) z1=(int32_t)g->d-1;
    int n=0;
    for(int32_t z=z0;z<=z1;z++) for(int32_t x=x0;x<=x1;x++) { g->cost[(uint32_t)z*g->w+(uint32_t)x]=c; n++; }
    return n;
}
int athena_nav_set_costs(AthenaNavGrid *g,const uint8_t *c,uint32_t count) {
    if(!g||!c||count<g->n) return -1;
    memcpy(g->cost,c,g->n); return 0;
}
int athena_nav_world_to_cell(const AthenaNavGrid *g,float x,float z,int32_t *cx,int32_t *cz) {
    if(!g||!athena_float_isfinite(x)||!athena_float_isfinite(z)) return -1;
    float fx=floorf((x-g->ox)/g->cell),fz=floorf((z-g->oz)/g->cell);
    if(!athena_float_isfinite(fx)||!athena_float_isfinite(fz)||fx<-1e9f||fx>1e9f||fz<-1e9f||fz>1e9f) return -1;
    *cx=(int32_t)fx; *cz=(int32_t)fz;
    return in_grid(g,*cx,*cz);
}
static void centre(const AthenaNavGrid *g,uint32_t i,float out[2]) {
    out[0]=g->ox+((float)(i%g->w)+.5f)*g->cell; out[1]=g->oz+((float)(i/g->w)+.5f)*g->cell;
}
static int segment_clear(const AthenaNavGrid *g,float x0,float z0,float x1,float z1,uint8_t max_cost) {
    if(!g) return -1;
    int32_t cx,cz,ex,ez;
    int a=athena_nav_world_to_cell(g,x0,z0,&cx,&cz),b=athena_nav_world_to_cell(g,x1,z1,&ex,&ez);
    if(a<0||b<0) return -1;
    if(!a||!b) return 0;
    /* Grid traversal (Amanatides-Woo) in cell units: every cell touched. */
    float ax=(x0-g->ox)/g->cell,az=(z0-g->oz)/g->cell,bx=(x1-g->ox)/g->cell,bz=(z1-g->oz)/g->cell;
    if(!athena_float_isfinite(ax)||!athena_float_isfinite(az)||!athena_float_isfinite(bx)||!athena_float_isfinite(bz)) return -1;
    float dx=bx-ax,dz=bz-az;
    int sx=dx>0?1:-1,sz=dz>0?1:-1;
    float tdx=dx!=0?fabsf(1/dx):3e38f,tdz=dz!=0?fabsf(1/dz):3e38f;
    float tmx=dx!=0?((sx>0?(float)(cx+1)-ax:ax-(float)cx)*tdx):3e38f;
    float tmz=dz!=0?((sz>0?(float)(cz+1)-az:az-(float)cz)*tdz):3e38f;
    for(uint32_t guard=0;guard<4u*(g->w+g->d)+8u;guard++) {
        uint8_t cost=athena_nav_get_cost(g,cx,cz);
        if(!cost||cost>max_cost) return 0;
        if(cx==ex&&cz==ez) return 1;
        /* Exactly through a corner: both side cells must be walkable. */
        if(dx!=0&&dz!=0&&fabsf(tmx-tmz)<1e-6f) {
            if(!athena_nav_get_cost(g,cx+sx,cz)||!athena_nav_get_cost(g,cx,cz+sz)) return 0;
            cx+=sx; cz+=sz; tmx+=tdx; tmz+=tdz;
        } else if(tmx<tmz) { cx+=sx; tmx+=tdx; }
        else { cz+=sz; tmz+=tdz; }
    }
    return 0;
}
int athena_nav_line_of_sight(const AthenaNavGrid *g,float x0,float z0,float x1,float z1) {
    return segment_clear(g,x0,z0,x1,z1,255);
}
/* Binary min-heap of cell indices keyed by f. */
static void heap_up(AthenaNavGrid *g,uint32_t i) {
    uint32_t c=g->heap[i]; float f=g->f[c];
    while(i) {
        uint32_t p=(i-1)/2, pc=g->heap[p];
        if(g->f[pc]<=f) break;
        g->heap[i]=pc; g->heap_pos[pc]=i+1; i=p;
    }
    g->heap[i]=c; g->heap_pos[c]=i+1;
}
static void heap_down(AthenaNavGrid *g,uint32_t i) {
    uint32_t c=g->heap[i]; float f=g->f[c];
    for(;;) {
        uint32_t l=i*2+1,r=l+1,m=i; float mf=f;
        if(l<g->heap_size&&g->f[g->heap[l]]<mf) { m=l; mf=g->f[g->heap[l]]; }
        if(r<g->heap_size&&g->f[g->heap[r]]<mf) m=r;
        if(m==i) break;
        g->heap[i]=g->heap[m]; g->heap_pos[g->heap[i]]=i+1; i=m;
    }
    g->heap[i]=c; g->heap_pos[c]=i+1;
}
static float octile(int32_t dx,int32_t dz,int diagonal) {
    dx=dx<0?-dx:dx; dz=dz<0?-dz:dz;
    if(!diagonal) return (float)(dx+dz);
    int32_t mn=dx<dz?dx:dz,mx=dx<dz?dz:dx;
    return (float)(mx-mn)+1.41421356f*(float)mn;
}
int athena_nav_find_path(AthenaNavGrid *g,float x0,float z0,float x1,float z1,const AthenaNavQuery *q,float *out,uint32_t max) {
    if(!g||!out||max<2) return -1;
    AthenaNavQuery def={1,1,0}; if(!q) q=&def;
    int32_t sx,sz,tx,tz;
    int a=athena_nav_world_to_cell(g,x0,z0,&sx,&sz),b=athena_nav_world_to_cell(g,x1,z1,&tx,&tz);
    if(a<0||b<0) return -1;
    if(!a||!b||!athena_nav_get_cost(g,sx,sz)||!athena_nav_get_cost(g,tx,tz)) return 0;
    uint32_t start=(uint32_t)sz*g->w+(uint32_t)sx,goal=(uint32_t)tz*g->w+(uint32_t)tx;
    if(++g->search==0) { memset(g->stamp,0,g->n*sizeof(uint32_t)); g->search=1; }
    uint32_t s=g->search,limit=q->max_iterations?q->max_iterations:g->n;
    g->heap_size=0; g->expanded=0;
    g->stamp[start]=s; g->g[start]=0; g->parent[start]=-1;
    g->f[start]=octile(sx-tx,sz-tz,q->diagonal);
    g->heap[g->heap_size++]=start; g->heap_pos[start]=1;
    static const int8_t DIR[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    int found=0;
    while(g->heap_size&&g->expanded<limit) {
        uint32_t cur=g->heap[0];
        g->heap[0]=g->heap[--g->heap_size]; g->heap_pos[cur]=0;
        if(g->heap_size) { g->heap_pos[g->heap[0]]=1; heap_down(g,0); }
        g->expanded++;
        if(cur==goal) { found=1; break; }
        int32_t cx=(int32_t)(cur%g->w),cz=(int32_t)(cur/g->w);
        const uint8_t *cost=g->cost; const uint32_t W=g->w,D=g->d;
        /* Walkability of the four orthogonal neighbours, for the corner rule. */
        uint8_t open4[4];
        for(int k=0;k<4;k++) {
            uint32_t nx=(uint32_t)(cx+DIR[k][0]),nz=(uint32_t)(cz+DIR[k][1]);
            open4[k]=nx<W&&nz<D?cost[nz*W+nx]:0;
        }
        float gcur=g->g[cur];
        for(int k=0;k<(q->diagonal?8:4);k++) {
            uint32_t nx=(uint32_t)(cx+DIR[k][0]),nz=(uint32_t)(cz+DIR[k][1]);
            if(nx>=W||nz>=D) continue;
            uint32_t ni=nz*W+nx;
            uint8_t c=cost[ni];
            if(!c) continue;
            /* No corner cutting: k 4..7 are (+1,+1),(+1,-1),(-1,+1),(-1,-1);
             * open4 is +x, -x, +z, -z. */
            if(k>=4&&(!open4[DIR[k][0]>0?0:1]||!open4[DIR[k][1]>0?2:3])) continue;
            int seen=g->stamp[ni]==s;
            if(seen&&!g->heap_pos[ni]) continue;   /* closed: consistent heuristic, never reopened */
            float ng=gcur+(k>=4?1.41421356f:1.0f)*(float)c;
            if(seen) {
                if(ng>=g->g[ni]) continue;
                g->g[ni]=ng; g->parent[ni]=(int32_t)cur; g->f[ni]=ng+octile((int32_t)nx-tx,(int32_t)nz-tz,q->diagonal);
                heap_up(g,g->heap_pos[ni]-1);
            } else {
                g->stamp[ni]=s; g->g[ni]=ng; g->parent[ni]=(int32_t)cur;
                g->f[ni]=ng+octile((int32_t)nx-tx,(int32_t)nz-tz,q->diagonal);
                g->heap[g->heap_size]=ni; heap_up(g,g->heap_size++);
            }
        }
    }
    if(!found) return 0;
    /* Cells from the goal back to the start, then reversed into out. */
    uint32_t count=0;
    for(int32_t i=(int32_t)goal;i>=0;i=g->parent[i]) count++;
    /* A* has finished; reuse its heap as the path scratch (count <= n).
     * Searches and crowd moveTo() need no temporary allocation. */
    uint32_t *cells=g->heap;
    uint32_t k=count;
    for(int32_t i=(int32_t)goal;i>=0;i=g->parent[i]) cells[--k]=(uint32_t)i;
    /* Stream the exact start, intermediate centres and exact target into
     * out. Smoothing shortcuts only cost-1 cells, preserving weighted
     * detours through roads instead of cutting back through expensive mud. */
    uint32_t np=count>1?count:2,w=1;
    float anchor[2]={x0,z0},prev[2]={x0,z0};
    out[0]=x0; out[1]=z0;
    for(uint32_t i=1;i<np;i++) {
        float p[2];
        if(i+1==np) { p[0]=x1; p[1]=z1; } else centre(g,cells[i],p);
        if(i>1&&(!q->smooth||segment_clear(g,anchor[0],anchor[1],p[0],p[1],1)!=1)) {
            if(w>=max) return -2;
            out[w*2]=prev[0]; out[w*2+1]=prev[1]; w++;
            anchor[0]=prev[0]; anchor[1]=prev[1];
        }
        prev[0]=p[0]; prev[1]=p[1];
    }
    if(w>=max) return -2;
    out[w*2]=x1; out[w*2+1]=z1;
    return (int)(w+1);
}
uint32_t athena_nav_last_expanded(const AthenaNavGrid *g) { return g?g->expanded:0; }
int athena_nav_nearest_walkable(const AthenaNavGrid *g,float x,float z,uint32_t radius,float out[2]) {
    int32_t cx,cz;
    if(!g||!out||athena_nav_world_to_cell(g,x,z,&cx,&cz)<0) return -1;
    /* Clip once to the grid, including queries just outside it. A huge
     * radius must not overflow an int or iterate billions of empty rings. */
    int64_t x0=(int64_t)cx-radius,x1=(int64_t)cx+radius;
    int64_t z0=(int64_t)cz-radius,z1=(int64_t)cz+radius;
    if(x0<0) x0=0;
    if(z0<0) z0=0;
    if(x1>=(int64_t)g->w) x1=g->w-1;
    if(z1>=(int64_t)g->d) z1=g->d-1;
    float best=3e38f; int found=0;
    for(int32_t zi=(int32_t)z0;zi<=z1;zi++) for(int32_t xi=(int32_t)x0;xi<=x1;xi++) {
        if(!athena_nav_get_cost(g,xi,zi)) continue;
        float c[2]; centre(g,(uint32_t)zi*g->w+(uint32_t)xi,c);
        float d=(c[0]-x)*(c[0]-x)+(c[1]-z)*(c[1]-z);
        if(d<best) { best=d; out[0]=c[0]; out[1]=c[1]; found=1; }
    }
    return found;
}

/* --- Crowd ---------------------------------------------------------------- */
typedef struct {
    uint8_t used,face; AthenaNavState state;
    float x,y,z,vx,vz,yaw,speed,radius;
    float path[ATHENA_NAV_MAX_PATH*2]; uint32_t count,next;
    AthenaNode3D *node;
} Agent;
struct AthenaNavCrowd {
    uint64_t refs;
    AthenaNavGrid *grid;
    AthenaNavQuery query;
    Agent *agents; uint32_t end;
};
AthenaNavCrowd *athena_nav_crowd_create(AthenaNavGrid *grid) {
    if(!grid) return NULL;
    AthenaNavCrowd *c=calloc(1,sizeof(*c)); if(!c) return NULL;
    c->agents=calloc(ATHENA_NAV_MAX_AGENTS,sizeof(Agent)); if(!c->agents) { free(c); return NULL; }
    c->refs=1; c->grid=grid; athena_nav_grid_retain(grid);
    c->query=(AthenaNavQuery){1,1,0};
    return c;
}
void athena_nav_crowd_retain(AthenaNavCrowd *c) { if(c) c->refs++; }
void athena_nav_crowd_release(AthenaNavCrowd *c) {
    if(!c||--c->refs) return;
    for(uint32_t i=0;i<c->end;i++) athena_node3d_release(c->agents[i].node);
    athena_nav_grid_release(c->grid); free(c->agents); free(c);
}
void athena_nav_crowd_set_query(AthenaNavCrowd *c,const AthenaNavQuery *q) { if(c&&q) c->query=*q; }
static Agent *agent_at(const AthenaNavCrowd *c,int id) {
    return c&&id>=0&&(uint32_t)id<c->end&&c->agents[id].used?&c->agents[id]:NULL;
}
int athena_nav_agent_add(AthenaNavCrowd *c,float x,float y,float z,const AthenaNavAgentDesc *d) {
    if(!c||!d||!athena_float_isfinite(x)||!athena_float_isfinite(y)||!athena_float_isfinite(z)||
        !athena_float_isfinite(d->speed)||d->speed<0||!athena_float_isfinite(d->radius)||d->radius<0) return -1;
    uint32_t id=0; while(id<ATHENA_NAV_MAX_AGENTS&&c->agents[id].used) id++;
    if(id==ATHENA_NAV_MAX_AGENTS) return -1;
    Agent *a=&c->agents[id]; memset(a,0,sizeof(*a));
    a->used=1; a->x=x; a->y=y; a->z=z; a->speed=d->speed; a->radius=d->radius; a->face=d->face!=0;
    if(id>=c->end) c->end=id+1;
    return (int)id;
}
int athena_nav_agent_remove(AthenaNavCrowd *c,int id) {
    Agent *a=agent_at(c,id); if(!a) return -1;
    athena_node3d_release(a->node); memset(a,0,sizeof(*a)); return 0;
}
int athena_nav_agent_bind(AthenaNavCrowd *c,int id,AthenaNode3D *node) {
    Agent *a=agent_at(c,id); if(!a) return -1;
    athena_node3d_retain(node); athena_node3d_release(a->node); a->node=node;
    if(node) athena_node3d_set_position(node,a->x,a->y,a->z);
    return 0;
}
int athena_nav_agent_move_to(AthenaNavCrowd *c,int id,float x,float z) {
    Agent *a=agent_at(c,id); if(!a) return -1;
    int n=athena_nav_find_path(c->grid,a->x,a->z,x,z,&c->query,a->path,ATHENA_NAV_MAX_PATH);
    if(n<=0) { a->state=ATHENA_NAV_IDLE; a->count=0; a->vx=a->vz=0; return n<0&&n!=-2?-1:0; }
    a->count=(uint32_t)n; a->next=1; a->state=ATHENA_NAV_MOVING;
    return 1;
}
int athena_nav_agent_stop(AthenaNavCrowd *c,int id) {
    Agent *a=agent_at(c,id); if(!a) return -1;
    a->state=ATHENA_NAV_IDLE; a->count=0; a->vx=a->vz=0; return 0;
}
int athena_nav_agent_set_position(AthenaNavCrowd *c,int id,float x,float y,float z) {
    Agent *a=agent_at(c,id); if(!a||!athena_float_isfinite(x)||!athena_float_isfinite(y)||!athena_float_isfinite(z)) return -1;
    a->x=x; a->y=y; a->z=z;
    if(a->node) athena_node3d_set_position(a->node,x,y,z);
    return 0;
}
int athena_nav_agent_set_speed(AthenaNavCrowd *c,int id,float speed) {
    Agent *a=agent_at(c,id); if(!a||!athena_float_isfinite(speed)||speed<0) return -1;
    a->speed=speed; return 0;
}
int athena_nav_agent_info(const AthenaNavCrowd *c,int id,AthenaNavAgentInfo *o) {
    const Agent *a=agent_at(c,id); if(!a||!o) return -1;
    *o=(AthenaNavAgentInfo){a->x,a->y,a->z,a->vx,a->vz,a->yaw,a->speed,a->state,a->next,a->count};
    return 0;
}
int athena_nav_crowd_update(AthenaNavCrowd *c,float dt) {
    if(!c||!athena_float_isfinite(dt)||dt<0) return -1;
    int arrived=0;
    for(uint32_t i=0;i<c->end;i++) {
        Agent *a=&c->agents[i];
        if(!a->used) continue;
        a->vx=a->vz=0;
        if(a->state!=ATHENA_NAV_MOVING) continue;
        float step=a->speed*dt;
        while(step>0&&a->next<a->count) {
            float tx=a->path[a->next*2],tz=a->path[a->next*2+1],dx=tx-a->x,dz=tz-a->z,dist=sqrtf(dx*dx+dz*dz);
            int last=a->next+1==a->count;
            /* Arrival: slow down over the last radius-scaled stretch. */
            float limit=step;
            if(last) { float slow=a->radius>0?a->radius*2:a->speed*.25f; if(dist<slow&&slow>0) limit=step*(.35f+.65f*dist/slow); }
            if(dist<=limit||dist<1e-4f) {
                a->vx+=dx; a->vz+=dz; a->x=tx; a->z=tz; step-=dist; a->next++;
                if(a->next>=a->count) { a->state=ATHENA_NAV_ARRIVED; arrived++; }
            } else {
                float k=limit/dist; a->x+=dx*k; a->z+=dz*k; a->vx+=dx*k; a->vz+=dz*k; step=0;
            }
        }
        if(dt>0) { a->vx/=dt; a->vz/=dt; }
        if(a->face&&(a->vx!=0||a->vz!=0)) a->yaw=atan2f(a->vx,a->vz);
    }
    /* Separation: overlapping agents push apart, staying on walkable cells. */
    for(uint32_t i=0;i<c->end;i++) {
        Agent *a=&c->agents[i]; if(!a->used||a->radius<=0) continue;
        for(uint32_t j=i+1;j<c->end;j++) {
            Agent *b=&c->agents[j]; if(!b->used||b->radius<=0) continue;
            float dx=b->x-a->x,dz=b->z-a->z,d2=dx*dx+dz*dz,r=a->radius+b->radius;
            if(d2>=r*r) continue;
            float d=sqrtf(d2),push;
            if(d<1e-4f) { dx=1; dz=0; d=1; push=r*.5f; } else push=(r-d)*.5f;
            float px=dx/d*push,pz=dz/d*push;
            int32_t cx,cz;
            if(athena_nav_world_to_cell(c->grid,a->x-px,a->z-pz,&cx,&cz)==1&&athena_nav_get_cost(c->grid,cx,cz)) { a->x-=px; a->z-=pz; }
            if(athena_nav_world_to_cell(c->grid,b->x+px,b->z+pz,&cx,&cz)==1&&athena_nav_get_cost(c->grid,cx,cz)) { b->x+=px; b->z+=pz; }
        }
    }
    for(uint32_t i=0;i<c->end;i++) {
        Agent *a=&c->agents[i];
        if(!a->used||!a->node) continue;
        athena_node3d_set_position(a->node,a->x,a->y,a->z);
        if(a->face) athena_node3d_set_euler(a->node,0,a->yaw,0);
    }
    return arrived;
}
