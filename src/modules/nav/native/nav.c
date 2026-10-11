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
    uint32_t search,heap_size,expanded,revision,pending,stamp_clear;
    AthenaNavRequest *head,*tail;
};
/* A suspended supercover traversal: one cell per bounded work unit. */
typedef struct {
    int32_t cx,cz,ex,ez,sx,sz;
    float dx,dz,tdx,tdz,tmx,tmz;
    uint32_t guard,limit;
    int result; /* 2 running, 1 clear, 0 blocked, -1 invalid */
} NavSegment;
struct AthenaNavRequest {
    uint64_t refs;
    uint64_t work;
    AthenaNavGrid *grid;
    AthenaNavRequest *next;
    /* Weak owner; detached on every terminal outcome before crowd destruction. */
    AthenaNavCrowd *owner; int owner_id;
    AthenaNavQuery query;
    AthenaNavRequestState state;
    AthenaNavRequestPhase phase;
    int error;
    uint32_t revision,expanded,max_points,start,goal,count,k,i,np,w;
    int32_t tx,tz,cursor;
    float x0,z0,x1,z1,cost,anchor[2],prev[2],candidate[2];
    float *out;
    NavSegment segment;
};
static void grid_changed(AthenaNavGrid *g);
static void agent_request_finished(AthenaNavRequest *r);
AthenaNavGrid *athena_nav_grid_create(uint32_t w,uint32_t d,float cell,float ox,float oz) {
    if(!w||!d||(uint64_t)w*d>ATHENA_NAV_MAX_CELLS||!athena_float_isfinite(cell)||cell<=0||
        !athena_float_isfinite(ox)||!athena_float_isfinite(oz)) return NULL;
    AthenaNavGrid *g=calloc(1,sizeof(*g)); if(!g) return NULL;
    g->refs=1; g->revision=1; g->w=w; g->d=d; g->n=w*d; g->cell=cell; g->ox=ox; g->oz=oz;
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
    uint32_t i=(uint32_t)z*g->w+(uint32_t)x;
    if(g->cost[i]!=c) { g->cost[i]=c; grid_changed(g); }
    return 0;
}
int athena_nav_fill(AthenaNavGrid *g,int32_t x0,int32_t z0,int32_t x1,int32_t z1,uint8_t c) {
    if(!g) return -1;
    if(x0>x1) { int32_t t=x0; x0=x1; x1=t; }
    if(z0>z1) { int32_t t=z0; z0=z1; z1=t; }
    if(x0<0) x0=0;
    if(z0<0) z0=0;
    if(x1>=(int32_t)g->w) x1=(int32_t)g->w-1;
    if(z1>=(int32_t)g->d) z1=(int32_t)g->d-1;
    int n=0,changed=0;
    for(int32_t z=z0;z<=z1;z++) for(int32_t x=x0;x<=x1;x++) {
        uint32_t i=(uint32_t)z*g->w+(uint32_t)x;
        changed|=g->cost[i]!=c; g->cost[i]=c; n++;
    }
    if(changed) grid_changed(g);
    return n;
}
int athena_nav_set_costs(AthenaNavGrid *g,const uint8_t *c,uint32_t count) {
    if(!g||!c||count<g->n) return -1;
    if(memcmp(g->cost,c,g->n)) { memcpy(g->cost,c,g->n); grid_changed(g); }
    return 0;
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
/* One existing A* expansion, shared by synchronous and incremental paths. */
static uint32_t expand_one(AthenaNavGrid *g,const AthenaNavQuery *q,uint32_t goal,int32_t tx,int32_t tz) {
    static const int8_t DIR[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    uint32_t cur=g->heap[0];
    g->heap[0]=g->heap[--g->heap_size]; g->heap_pos[cur]=0;
    if(g->heap_size) { g->heap_pos[g->heap[0]]=1; heap_down(g,0); }
    g->expanded++;
    if(cur==goal) return cur;
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
        int seen=g->stamp[ni]==g->search;
        if(seen&&!g->heap_pos[ni]) continue;   /* closed: consistent heuristic, never reopened */
        float ng=gcur+(k>=4?1.41421356f:1.0f)*(float)c;
        if(seen) {
            if(ng>=g->g[ni]) continue;
            g->g[ni]=ng; g->parent[ni]=(int32_t)cur; g->f[ni]=ng+octile((int32_t)nx-tx,(int32_t)nz-tz,q->diagonal);
            heap_up(g,g->heap_pos[ni]-1);
        } else {
            g->stamp[ni]=g->search; g->g[ni]=ng; g->parent[ni]=(int32_t)cur;
            g->f[ni]=ng+octile((int32_t)nx-tx,(int32_t)nz-tz,q->diagonal);
            g->heap[g->heap_size]=ni; heap_up(g,g->heap_size++);
        }
    }
    return cur;
}

int athena_nav_find_path(AthenaNavGrid *g,float x0,float z0,float x1,float z1,const AthenaNavQuery *q,float *out,uint32_t max) {
    if(!g||!out||max<2) return -1;
    if(g->pending) return ATHENA_NAV_EBUSY;
    AthenaNavQuery def={1,1,0}; if(!q) q=&def;
    int32_t sx,sz,tx,tz;
    int a=athena_nav_world_to_cell(g,x0,z0,&sx,&sz),b=athena_nav_world_to_cell(g,x1,z1,&tx,&tz);
    if(a<0||b<0) return -1;
    if(!a||!b||!athena_nav_get_cost(g,sx,sz)||!athena_nav_get_cost(g,tx,tz)) return 0;
    uint32_t start=(uint32_t)sz*g->w+(uint32_t)sx,goal=(uint32_t)tz*g->w+(uint32_t)tx;
    if(++g->search==0||g->stamp_clear) {
        memset(g->stamp,0,g->n*sizeof(uint32_t)); g->search=1; g->stamp_clear=0;
    }
    uint32_t s=g->search,limit=q->max_iterations?q->max_iterations:g->n;
    g->heap_size=0; g->expanded=0;
    g->stamp[start]=s; g->g[start]=0; g->parent[start]=-1;
    g->f[start]=octile(sx-tx,sz-tz,q->diagonal);
    g->heap[g->heap_size++]=start; g->heap_pos[start]=1;
    int found=0;
    while(g->heap_size&&g->expanded<limit) {
        if(expand_one(g,q,goal,tx,tz)==goal) { found=1; break; }
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
/* --- Incremental requests ------------------------------------------------- */
#if defined(__mips__)
static uint32_t nav_ticks(void) { uint32_t c; __asm__ __volatile__("mfc0 %0, $9" : "=r"(c)); return c; }
#define NAV_TICKS_PER_MS 294912.0f
#else
#include <time.h>
static uint32_t nav_ticks(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint32_t)((uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u);
}
#define NAV_TICKS_PER_MS 1000.0f
#endif

static void request_finish(AthenaNavRequest *r,AthenaNavRequestState state,int error) {
    if(r->state!=ATHENA_NAV_PENDING) return;
    AthenaNavGrid *g=r->grid;
    AthenaNavRequest *prev=NULL,*at=g->head;
    while(at&&at!=r) { prev=at; at=at->next; }
    if(at) {
        if(prev) prev->next=r->next;
        else { g->head=r->next; g->heap_size=0; }
        if(g->tail==r) g->tail=prev;
        g->pending--;
    }
    r->next=NULL; r->state=state; r->error=error; r->phase=ATHENA_NAV_FINISHED;
    /* May release the last owner reference: do not touch r afterwards. */
    agent_request_finished(r);
}
static void grid_changed(AthenaNavGrid *g) {
    if(++g->revision==0) g->revision=1;
    while(g->head) request_finish(g->head,ATHENA_NAV_ERROR,ATHENA_NAV_EREVISION);
}
void athena_nav_cancel_requests(AthenaNavGrid *g) {
    if(g) while(g->head) request_finish(g->head,ATHENA_NAV_CANCELLED,0);
}
uint32_t athena_nav_pending_requests(const AthenaNavGrid *g) { return g?g->pending:0; }
uint32_t athena_nav_grid_revision(const AthenaNavGrid *g) { return g?g->revision:0; }
#ifdef ATHENA_NAV_TEST_HOOKS
/* Host-only rollover coverage; absent from module/EE builds. */
void athena_nav_test_search_wrap(AthenaNavGrid *g) { g->search=UINT32_MAX; }
#endif
static AthenaNavRequest *request_create(AthenaNavGrid *g,float x0,float z0,float x1,float z1,
    const AthenaNavQuery *query,uint32_t max,int *error,AthenaNavRequest *replace) {
    if(error) *error=ATHENA_NAV_EINVAL;
    if(!g||max<2||max>ATHENA_NAV_MAX_CELLS+1u) return NULL;
    int32_t sx,sz,tx,tz;
    int a=athena_nav_world_to_cell(g,x0,z0,&sx,&sz),b=athena_nav_world_to_cell(g,x1,z1,&tx,&tz);
    if(a<0||b<0) return NULL;
    if(g->pending==ATHENA_NAV_MAX_PENDING&&(!replace||replace->state!=ATHENA_NAV_PENDING)) {
        if(error) *error=ATHENA_NAV_EFULL;
        return NULL;
    }
    AthenaNavRequest *r=calloc(1,sizeof(*r));
    if(!r) { if(error) *error=ATHENA_NAV_ENOMEM; return NULL; }
    r->out=malloc((size_t)max*2*sizeof(float));
    if(!r->out) { free(r); if(error) *error=ATHENA_NAV_ENOMEM; return NULL; }
    r->refs=1; r->grid=g; athena_nav_grid_retain(g);
    r->query=query?*query:(AthenaNavQuery){1,1,0};
    r->state=ATHENA_NAV_PENDING; r->phase=ATHENA_NAV_QUEUED; r->revision=g->revision;
    r->x0=x0; r->z0=z0; r->x1=x1; r->z1=z1; r->max_points=max;
    r->start=a?(uint32_t)sz*g->w+(uint32_t)sx:UINT32_MAX;
    r->goal=b?(uint32_t)tz*g->w+(uint32_t)tx:UINT32_MAX;
    r->tx=tx; r->tz=tz;
    /* Allocate/validate first: a failed replacement must leave the old plan intact. */
    if(replace) athena_nav_request_cancel(replace);
    if(g->tail) g->tail->next=r; else g->head=r;
    g->tail=r; g->pending++;
    if(error) *error=0;
    return r;
}
AthenaNavRequest *athena_nav_request_create(AthenaNavGrid *g,float x0,float z0,float x1,float z1,
    const AthenaNavQuery *query,uint32_t max,int *error) {
    return request_create(g,x0,z0,x1,z1,query,max,error,NULL);
}
void athena_nav_request_retain(AthenaNavRequest *r) { if(r) r->refs++; }
void athena_nav_request_cancel(AthenaNavRequest *r) { if(r) request_finish(r,ATHENA_NAV_CANCELLED,0); }
void athena_nav_request_release(AthenaNavRequest *r) {
    if(!r||--r->refs) return;
    athena_nav_request_cancel(r); athena_nav_grid_release(r->grid); free(r->out); free(r);
}
int athena_nav_request_info(const AthenaNavRequest *r,AthenaNavRequestInfo *out) {
    if(!r||!out) return ATHENA_NAV_EINVAL;
    *out=(AthenaNavRequestInfo){r->state,r->phase,r->error,r->revision,r->expanded,r->work,
        r->state==ATHENA_NAV_FOUND?r->w:0,(uint32_t)sizeof(*r)+r->max_points*2u*sizeof(float),
        r->state==ATHENA_NAV_FOUND?r->cost:0};
    return 0;
}
const float *athena_nav_request_path(const AthenaNavRequest *r,uint32_t *points) {
    if(points) *points=r&&r->state==ATHENA_NAV_FOUND?r->w:0;
    return r&&r->state==ATHENA_NAV_FOUND?r->out:NULL;
}
int athena_nav_request_copy(const AthenaNavRequest *r,float *out,uint32_t max) {
    if(!r||!out) return ATHENA_NAV_EINVAL;
    if(r->state!=ATHENA_NAV_FOUND) return 0;
    if(max<r->w) return ATHENA_NAV_EOUTPUT;
    memcpy(out,r->out,(size_t)r->w*2*sizeof(float)); return (int)r->w;
}

static void segment_begin(const AthenaNavGrid *g,NavSegment *s,const float a[2],const float b[2]) {
    memset(s,0,sizeof(*s));
    int first=athena_nav_world_to_cell(g,a[0],a[1],&s->cx,&s->cz);
    int last=athena_nav_world_to_cell(g,b[0],b[1],&s->ex,&s->ez);
    if(first<0||last<0) { s->result=-1; return; }
    if(!first||!last) return;
    float ax=(a[0]-g->ox)/g->cell,az=(a[1]-g->oz)/g->cell;
    float bx=(b[0]-g->ox)/g->cell,bz=(b[1]-g->oz)/g->cell;
    if(!athena_float_isfinite(ax)||!athena_float_isfinite(az)||!athena_float_isfinite(bx)||!athena_float_isfinite(bz)) { s->result=-1; return; }
    s->dx=bx-ax; s->dz=bz-az;
    s->sx=s->dx>0?1:-1; s->sz=s->dz>0?1:-1;
    s->tdx=s->dx!=0?fabsf(1/s->dx):3e38f; s->tdz=s->dz!=0?fabsf(1/s->dz):3e38f;
    s->tmx=s->dx!=0?((s->sx>0?(float)(s->cx+1)-ax:ax-(float)s->cx)*s->tdx):3e38f;
    s->tmz=s->dz!=0?((s->sz>0?(float)(s->cz+1)-az:az-(float)s->cz)*s->tdz):3e38f;
    s->limit=4u*(g->w+g->d)+8u; s->result=2;
}
static void segment_step(const AthenaNavGrid *g,NavSegment *s) {
    if(s->result!=2) return;
    if(s->guard++>=s->limit) { s->result=0; return; }
    uint8_t cost=athena_nav_get_cost(g,s->cx,s->cz);
    if(!cost||cost>1) { s->result=0; return; }
    if(s->cx==s->ex&&s->cz==s->ez) { s->result=1; return; }
    if(s->dx!=0&&s->dz!=0&&fabsf(s->tmx-s->tmz)<1e-6f) {
        if(!athena_nav_get_cost(g,s->cx+s->sx,s->cz)||!athena_nav_get_cost(g,s->cx,s->cz+s->sz)) { s->result=0; return; }
        s->cx+=s->sx; s->cz+=s->sz; s->tmx+=s->tdx; s->tmz+=s->tdz;
    } else if(s->tmx<s->tmz) { s->cx+=s->sx; s->tmx+=s->tdx; }
    else { s->cz+=s->sz; s->tmz+=s->tdz; }
}
static void begin_search(AthenaNavRequest *r) {
    AthenaNavGrid *g=r->grid;
    uint32_t start=r->start;
    g->heap_size=1; g->expanded=0;
    g->stamp[start]=g->search; g->g[start]=0; g->parent[start]=-1;
    g->f[start]=octile((int32_t)(start%g->w)-r->tx,(int32_t)(start/g->w)-r->tz,r->query.diagonal);
    g->heap[0]=start; g->heap_pos[start]=1; r->phase=ATHENA_NAV_SEARCH;
}
static int write_point(AthenaNavRequest *r,const float p[2]) {
    if(r->w>=r->max_points) { request_finish(r,ATHENA_NAV_ERROR,ATHENA_NAV_EOUTPUT); return 0; }
    r->out[r->w*2]=p[0]; r->out[r->w*2+1]=p[1]; r->w++;
    return 1;
}
static void advance_candidate(AthenaNavRequest *r,int clear) {
    if(!clear) {
        if(!write_point(r,r->prev)) return;
        memcpy(r->anchor,r->prev,sizeof(r->anchor));
    }
    memcpy(r->prev,r->candidate,sizeof(r->prev)); r->i++; r->phase=ATHENA_NAV_EMIT_PATH;
}
static void request_unit(AthenaNavRequest *r) {
    AthenaNavGrid *g=r->grid;
    r->work++;
    switch(r->phase) {
    case ATHENA_NAV_QUEUED:
    case ATHENA_NAV_INITIALIZE:
        if(r->start==UINT32_MAX||r->goal==UINT32_MAX||!g->cost[r->start]||!g->cost[r->goal]) { request_finish(r,ATHENA_NAV_NO_PATH,0); return; }
        if(g->stamp_clear||++g->search==0) {
            g->search=1;
            if(!g->stamp_clear) g->stamp_clear=1;
            r->phase=ATHENA_NAV_CLEAR_STAMPS;
        }
        else begin_search(r);
        return;
    case ATHENA_NAV_CLEAR_STAMPS:
        g->stamp[g->stamp_clear-1]=0;
        if(++g->stamp_clear>g->n) { g->stamp_clear=0; begin_search(r); }
        return;
    case ATHENA_NAV_SEARCH: {
        if(!g->heap_size) { request_finish(r,ATHENA_NAV_NO_PATH,0); return; }
        uint32_t limit=r->query.max_iterations?r->query.max_iterations:g->n;
        if(g->expanded>=limit) { request_finish(r,ATHENA_NAV_ERROR,ATHENA_NAV_EITERATIONS); return; }
        uint32_t cur=expand_one(g,&r->query,r->goal,r->tx,r->tz);
        r->expanded=g->expanded;
        if(cur==r->goal) { r->cost=g->g[cur]; r->count=0; r->cursor=(int32_t)cur; r->phase=ATHENA_NAV_COUNT_PATH; }
        return;
    }
    case ATHENA_NAV_COUNT_PATH:
        r->count++; r->cursor=g->parent[r->cursor];
        if(r->cursor<0) { r->cursor=(int32_t)r->goal; r->k=r->count; r->phase=ATHENA_NAV_TRACE_PATH; }
        return;
    case ATHENA_NAV_TRACE_PATH:
        g->heap[--r->k]=(uint32_t)r->cursor; r->cursor=g->parent[r->cursor];
        if(r->cursor<0) {
            r->np=r->count>1?r->count:2; r->i=1; r->w=1;
            r->anchor[0]=r->prev[0]=r->out[0]=r->x0;
            r->anchor[1]=r->prev[1]=r->out[1]=r->z0;
            r->phase=ATHENA_NAV_EMIT_PATH;
        }
        return;
    case ATHENA_NAV_EMIT_PATH:
        if(r->i==r->np) {
            const float target[2]={r->x1,r->z1};
            if(write_point(r,target)) request_finish(r,ATHENA_NAV_FOUND,0);
            return;
        }
        if(r->i+1==r->np) { r->candidate[0]=r->x1; r->candidate[1]=r->z1; }
        else centre(g,g->heap[r->i],r->candidate);
        if(r->i==1) { advance_candidate(r,1); return; }
        if(!r->query.smooth) { advance_candidate(r,0); return; }
        segment_begin(g,&r->segment,r->anchor,r->candidate);
        r->phase=ATHENA_NAV_SMOOTH_PATH;
        return;
    case ATHENA_NAV_SMOOTH_PATH:
        segment_step(g,&r->segment);
        if(r->segment.result!=2) advance_candidate(r,r->segment.result==1);
        return;
    default: request_finish(r,ATHENA_NAV_ERROR,ATHENA_NAV_EINVAL); return;
    }
}
int athena_nav_step_requests(AthenaNavGrid *g,const AthenaNavBudget *budget) {
    const AthenaNavBudget def={256,0}; if(!budget) budget=&def;
    if(!g||!budget->max_work||budget->max_work>ATHENA_NAV_MAX_CELLS||
        !athena_float_isfinite(budget->budget_ms)||budget->budget_ms<0||budget->budget_ms>1000) return ATHENA_NAV_EINVAL;
    uint32_t done=0,start=budget->budget_ms>0?nav_ticks():0;
    float limit=budget->budget_ms*NAV_TICKS_PER_MS;
    while(g->head&&done<budget->max_work) {
        request_unit(g->head); done++;
        if(limit>0&&(float)(uint32_t)(nav_ticks()-start)>=limit) break;
    }
    return (int)done;
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
    AthenaNavRequest *request;
    AthenaNavRequestState planning_state; int planning_error;
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
    athena_nav_crowd_cancel_requests(c);
    for(uint32_t i=0;i<c->end;i++) athena_node3d_release(c->agents[i].node);
    athena_nav_grid_release(c->grid); free(c->agents); free(c);
}
void athena_nav_crowd_set_query(AthenaNavCrowd *c,const AthenaNavQuery *q) { if(c&&q) c->query=*q; }
static Agent *agent_at(const AthenaNavCrowd *c,int id) {
    return c&&id>=0&&(uint32_t)id<c->end&&c->agents[id].used?&c->agents[id]:NULL;
}
static void agent_request_finished(AthenaNavRequest *r) {
    AthenaNavCrowd *owner=r->owner;
    if(!owner) return;
    Agent *a=agent_at(owner,r->owner_id);
    r->owner=NULL;
    if(a&&a->request==r) {
        a->request=NULL; a->planning_state=r->state; a->planning_error=r->error;
        a->vx=a->vz=0; a->count=a->next=0; a->state=ATHENA_NAV_IDLE;
        if(r->state==ATHENA_NAV_FOUND) {
            memcpy(a->path,r->out,(size_t)r->w*2*sizeof(float));
            a->count=r->w; a->next=1; a->state=ATHENA_NAV_MOVING;
        }
    }
    /* The agent's reference, not the caller's result observer. */
    athena_nav_request_release(r);
}
void athena_nav_crowd_cancel_requests(AthenaNavCrowd *c) {
    if(c) for(uint32_t i=0;i<c->end;i++) if(c->agents[i].request)
        athena_nav_request_cancel(c->agents[i].request);
}
int athena_nav_agent_add(AthenaNavCrowd *c,float x,float y,float z,const AthenaNavAgentDesc *d) {
    if(!c||!d||!athena_float_isfinite(x)||!athena_float_isfinite(y)||!athena_float_isfinite(z)||
        !athena_float_isfinite(d->speed)||d->speed<0||!athena_float_isfinite(d->radius)||d->radius<0) return -1;
    uint32_t id=0; while(id<ATHENA_NAV_MAX_AGENTS&&c->agents[id].used) id++;
    if(id==ATHENA_NAV_MAX_AGENTS) return -1;
    Agent *a=&c->agents[id]; memset(a,0,sizeof(*a));
    a->used=1; a->x=x; a->y=y; a->z=z; a->speed=d->speed; a->radius=d->radius; a->face=d->face!=0;
    a->planning_state=ATHENA_NAV_NOT_REQUESTED;
    if(id>=c->end) c->end=id+1;
    return (int)id;
}
int athena_nav_agent_remove(AthenaNavCrowd *c,int id) {
    Agent *a=agent_at(c,id); if(!a) return -1;
    athena_nav_request_cancel(a->request);
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
    if(n==ATHENA_NAV_EBUSY) return n;
    a->planning_state=ATHENA_NAV_NOT_REQUESTED; a->planning_error=0;
    if(n<=0) { a->state=ATHENA_NAV_IDLE; a->count=0; a->vx=a->vz=0; return 0; }
    a->count=(uint32_t)n; a->next=1; a->state=ATHENA_NAV_MOVING;
    return 1;
}
AthenaNavRequest *athena_nav_agent_request_move_to(AthenaNavCrowd *c,int id,float x,float z,int *error) {
    Agent *a=agent_at(c,id);
    if(!a) { if(error) *error=ATHENA_NAV_EINVAL; return NULL; }
    AthenaNavRequest *r=request_create(c->grid,a->x,a->z,x,z,&c->query,ATHENA_NAV_MAX_PATH,error,a->request);
    if(!r) return NULL;
    r->owner=c; r->owner_id=id; athena_nav_request_retain(r); a->request=r;
    a->planning_state=ATHENA_NAV_PENDING; a->planning_error=0;
    a->state=ATHENA_NAV_PLANNING; a->count=a->next=0; a->vx=a->vz=0;
    return r;
}
int athena_nav_agent_stop(AthenaNavCrowd *c,int id) {
    Agent *a=agent_at(c,id); if(!a) return -1;
    athena_nav_request_cancel(a->request);
    a->state=ATHENA_NAV_IDLE; a->count=0; a->vx=a->vz=0; return 0;
}
int athena_nav_agent_set_position(AthenaNavCrowd *c,int id,float x,float y,float z) {
    Agent *a=agent_at(c,id); if(!a||!athena_float_isfinite(x)||!athena_float_isfinite(y)||!athena_float_isfinite(z)) return -1;
    athena_nav_request_cancel(a->request);
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
    *o=(AthenaNavAgentInfo){a->x,a->y,a->z,a->vx,a->vz,a->yaw,a->speed,a->state,a->next,a->count,
        a->planning_state,a->planning_error};
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
        Agent *a=&c->agents[i]; if(!a->used||a->radius<=0||a->state==ATHENA_NAV_PLANNING) continue;
        for(uint32_t j=i+1;j<c->end;j++) {
            Agent *b=&c->agents[j]; if(!b->used||b->radius<=0||b->state==ATHENA_NAV_PLANNING) continue;
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
