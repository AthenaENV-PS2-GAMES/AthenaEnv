#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <athena/triggers3d.h>
#include <athena/float_bits.h>
#define DEFAULT_ENTRIES 4096u
#define MAX_ENTRIES 65536u
#define LARGE_BODY_CELLS 64.0
#define LARGE_ZONE_CELLS 256.0
#define KEY_LIMIT 1048576
typedef struct { uint16_t body; uint32_t epoch; } Occ;   /* epoch: the update in which the body entered */
typedef struct { AthenaTriggers3D *t; uint16_t id; } HookRef;
typedef struct {
    uint8_t used,enabled,shape,stay; uint32_t mask;
    float a[3],b[3];          /* as given (relative to the node when following) */
    AthenaNode3D *node; float offset[3];
    Occ *occ; uint32_t occ_count,occ_cap;
    uint8_t large;
} Zone;
typedef struct {
    uint8_t used,dirty,large,inlist; uint32_t layers;
    float p[3],radius;
    AthenaNode3D *node; float offset[3];
    float eff[3];             /* effective position: p, or the node's world position + offset */
    HookRef *hook;
    int32_t lo[3],hi[3]; uint32_t first_entry;   /* cell range of the bounding box; entries of this body (1-based, 0 none) */
    uint32_t stamp;
} Body;
typedef struct { int32_t key[3]; int32_t chain; uint32_t head,count; uint8_t used; } Cell;
typedef struct { uint32_t next,prev,next_body,cell; uint16_t body; } Entry;
struct AthenaTriggers3D {
    Zone *zones; Body *bodies;
    uint32_t zone_cap,body_cap,zone_end,body_end;  /* end: one past the highest used id */
    uint8_t *inside;                                /* zone_cap * body_cap bits as bytes */
    AthenaTrigger3DEvent *events; uint32_t event_count,event_cap;
    uint8_t stay_all;
    uint32_t epoch,stamp;
    AthenaTriggers3DStats stats;
    /* grid */
    float cell_size; uint32_t capacity,bucket_mask,cell_high,cell_free,entry_free,entry_high,entries_used,cells_used;
    Cell *cells; int32_t *buckets; Entry *entries;
    uint16_t *dirty_ids; uint32_t dirty_count,dirty_cap;
    uint16_t *large_bodies; uint32_t large_count,large_cap;
};
static void body_moved(void *user,AthenaNode3D *node);
AthenaTriggers3D *athena_triggers3d_create(void) { return calloc(1,sizeof(AthenaTriggers3D)); }
static void grid_free(AthenaTriggers3D *t) {
    free(t->cells); free(t->buckets); free(t->entries); free(t->dirty_ids); free(t->large_bodies);
    t->cells=NULL; t->buckets=NULL; t->entries=NULL; t->dirty_ids=NULL; t->large_bodies=NULL;
    t->cell_size=0; t->capacity=t->bucket_mask=t->cell_high=t->cell_free=t->entry_free=t->entry_high=t->entries_used=t->cells_used=0;
    t->dirty_count=t->dirty_cap=t->large_count=t->large_cap=0;
}
static void body_unhook(AthenaTriggers3D *t,Body *b) {
    (void)t;
    if(b->hook) { if(b->node) athena_node3d_remove_moved_hook(b->node,body_moved,b->hook); free(b->hook); b->hook=NULL; }
}
void athena_triggers3d_destroy(AthenaTriggers3D *t) {
    if(!t) return;
    for(uint32_t i=0;i<t->zone_end;i++) { athena_node3d_release(t->zones[i].node); free(t->zones[i].occ); }
    for(uint32_t i=0;i<t->body_end;i++) { body_unhook(t,&t->bodies[i]); athena_node3d_release(t->bodies[i].node); }
    grid_free(t);
    free(t->zones); free(t->bodies); free(t->inside); free(t->events); free(t);
}
static int finite3(const float v[3]) { return athena_float_isfinite(v[0])&&athena_float_isfinite(v[1])&&athena_float_isfinite(v[2]); }
static int shape_ok(AthenaTrigger3DShape s,const float a[3],const float b[3]) {
    if(!a||!b||!finite3(a)) return 0;
    if(s==ATHENA_TRIGGER3D_BOX) return finite3(b)&&a[0]<=b[0]&&a[1]<=b[1]&&a[2]<=b[2];
    return s==ATHENA_TRIGGER3D_SPHERE&&athena_float_isfinite(b[0])&&b[0]>=0;
}
/* Grows the pair matrix keeping the bytes of existing pairs. */
static int resize(AthenaTriggers3D *t,uint32_t zc,uint32_t bc) {
    uint8_t *m=calloc((size_t)zc*bc,1); if(!m) return 0;
    for(uint32_t z=0;z<t->zone_cap&&z<zc;z++) for(uint32_t b=0;b<t->body_cap&&b<bc;b++) m[z*bc+b]=t->inside[z*t->body_cap+b];
    free(t->inside); t->inside=m; return 1;
}
static int push_event(AthenaTriggers3D *t,uint8_t type,uint32_t z,uint32_t b) {
    if(t->event_count==t->event_cap) {
        uint32_t next=t->event_cap?t->event_cap*2:32;
        AthenaTrigger3DEvent *e=realloc(t->events,next*sizeof(*e)); if(!e) return 0;
        t->events=e; t->event_cap=next;
    }
    t->events[t->event_count++]=(AthenaTrigger3DEvent){type,(uint16_t)z,(uint16_t)b};
    return 1;
}
/* ---- occupants of each zone ---- */
static int occ_add(Zone *z,uint32_t body,uint32_t epoch) {
    if(z->occ_count==z->occ_cap) {
        uint32_t next=z->occ_cap?z->occ_cap*2:8; Occ *o=realloc(z->occ,next*sizeof(*o)); if(!o) return 0;
        z->occ=o; z->occ_cap=next;
    }
    z->occ[z->occ_count++]=(Occ){(uint16_t)body,epoch}; return 1;
}
static void occ_remove(Zone *z,uint32_t body) {
    for(uint32_t i=0;i<z->occ_count;i++) if(z->occ[i].body==body) { z->occ[i]=z->occ[--z->occ_count]; return; }
}
/* ---- grid ---- */
static uint32_t hash_key(const int32_t k[3]) {
    uint32_t h=(uint32_t)k[0]*73856093u^(uint32_t)k[1]*19349663u^(uint32_t)k[2]*83492791u; h^=h>>15; return h;
}
static int32_t axis_cell(const AthenaTriggers3D *t,float v) {
    float c=floorf(v/t->cell_size); if(c>KEY_LIMIT) c=KEY_LIMIT; if(c<-KEY_LIMIT) c=-KEY_LIMIT; return (int32_t)c;
}
/* Closed interval [lo, hi] to cell indices; the small margin keeps boundary cases conservative. */
static void cell_range(const AthenaTriggers3D *t,const float lo[3],const float hi[3],int32_t out_lo[3],int32_t out_hi[3]) {
    float eps=t->cell_size*1e-4f;
    for(int i=0;i<3;i++) { out_lo[i]=axis_cell(t,lo[i]-eps); out_hi[i]=axis_cell(t,hi[i]+eps); }
}
static double range_cells(const int32_t lo[3],const int32_t hi[3]) {
    return ((double)hi[0]-lo[0]+1)*((double)hi[1]-lo[1]+1)*((double)hi[2]-lo[2]+1);
}
static int32_t cell_find(AthenaTriggers3D *t,const int32_t k[3],int create) {
    uint32_t b=hash_key(k)&t->bucket_mask;
    for(int32_t i=t->buckets[b];i>=0;i=t->cells[i].chain)
        if(t->cells[i].key[0]==k[0]&&t->cells[i].key[1]==k[1]&&t->cells[i].key[2]==k[2]) return i;
    if(!create) return -1;
    int32_t i;
    if(t->cell_free) { i=(int32_t)t->cell_free; t->cell_free=(uint32_t)t->cells[i].chain; }
    else if(t->cell_high<t->capacity) i=(int32_t)t->cell_high++;
    else return -1;
    Cell *c=&t->cells[i]; memset(c,0,sizeof(*c)); c->used=1; memcpy(c->key,k,12);
    c->chain=t->buckets[b]; t->buckets[b]=i; t->cells_used++; return i;
}
static void cell_release(AthenaTriggers3D *t,int32_t i) {
    Cell *c=&t->cells[i];
    uint32_t b=hash_key(c->key)&t->bucket_mask;
    for(int32_t *link=&t->buckets[b];*link>=0;link=&t->cells[*link].chain) if(*link==i) { *link=c->chain; break; }
    c->used=0; c->chain=(int32_t)t->cell_free; t->cell_free=(uint32_t)i; t->cells_used--;
}
static uint32_t entry_alloc(AthenaTriggers3D *t) {
    if(t->entry_free) { uint32_t e=t->entry_free; t->entry_free=t->entries[e].next; t->entries_used++; return e; }
    if(t->entry_high<t->capacity) { t->entries_used++; return ++t->entry_high; }   /* 1-based */
    return 0;
}
static void body_leave_grid(AthenaTriggers3D *t,Body *b,uint32_t id) {
    for(uint32_t e=b->first_entry;e;) {
        Entry *en=&t->entries[e]; uint32_t next=en->next_body; Cell *c=&t->cells[en->cell];
        if(en->prev) t->entries[en->prev].next=en->next; else c->head=en->next;
        if(en->next) t->entries[en->next].prev=en->prev;
        if(!--c->count) cell_release(t,(int32_t)en->cell);
        en->next=t->entry_free; t->entry_free=e; t->entries_used--;
        e=next;
    }
    b->first_entry=0;
    if(b->large) {
        for(uint32_t i=0;i<t->large_count;i++) if(t->large_bodies[i]==id) { t->large_bodies[i]=t->large_bodies[--t->large_count]; break; }
        b->large=0;
    }
}
static int large_add(AthenaTriggers3D *t,uint32_t id) {
    if(t->large_count==t->large_cap) {
        uint32_t next=t->large_cap?t->large_cap*2:16; uint16_t *l=realloc(t->large_bodies,next*sizeof(*l)); if(!l) return 0;
        t->large_bodies=l; t->large_cap=next;
    }
    t->large_bodies[t->large_count++]=(uint16_t)id; return 1;
}
/* Bins the body by its bounding box; too large or no room: the always-tested list (the result never changes). */
static int body_enter_grid(AthenaTriggers3D *t,Body *b,uint32_t id) {
    float lo[3],hi[3]; for(int i=0;i<3;i++) { lo[i]=b->eff[i]-b->radius; hi[i]=b->eff[i]+b->radius; }
    cell_range(t,lo,hi,b->lo,b->hi);
    double n=range_cells(b->lo,b->hi);
    if(n<=LARGE_BODY_CELLS&&(double)(t->capacity-t->entries_used)>=n) {
        int32_t k[3];
        for(k[0]=b->lo[0];k[0]<=b->hi[0];k[0]++) for(k[1]=b->lo[1];k[1]<=b->hi[1];k[1]++) for(k[2]=b->lo[2];k[2]<=b->hi[2];k[2]++) {
            int32_t c=cell_find(t,k,1); uint32_t e=c>=0?entry_alloc(t):0;
            if(!e) { body_leave_grid(t,b,id); goto large; }
            Entry *en=&t->entries[e]; Cell *cell=&t->cells[c];
            en->body=(uint16_t)id; en->cell=(uint32_t)c; en->prev=0; en->next=cell->head; if(cell->head) t->entries[cell->head].prev=e;
            cell->head=e; cell->count++; en->next_body=b->first_entry; b->first_entry=e;
        }
        return 1;
    }
large:
    if(!large_add(t,id)) return 0;
    b->large=1; return 1;
}
static void body_refresh(AthenaTriggers3D *t,Body *b) {
    if(b->node) { athena_node3d_last_world_position(b->node,b->eff); for(int i=0;i<3;i++) b->eff[i]+=b->offset[i]; }
    else memcpy(b->eff,b->p,sizeof(b->eff));
    (void)t;
}
static void mark_dirty(AthenaTriggers3D *t,uint32_t id) {
    Body *b=&t->bodies[id];
    if(!t->cell_size||b->dirty) return;
    if(t->dirty_count==t->dirty_cap) {
        uint32_t next=t->dirty_cap?t->dirty_cap*2:32; uint16_t *d=realloc(t->dirty_ids,next*sizeof(*d));
        if(!d) { b->dirty=0; /* cannot queue: rebin immediately */
            body_refresh(t,b); body_leave_grid(t,b,id); body_enter_grid(t,b,id); return; }
        t->dirty_ids=d; t->dirty_cap=next;
    }
    b->dirty=1; t->dirty_ids[t->dirty_count++]=(uint16_t)id;
}
static void body_moved(void *user,AthenaNode3D *node) { (void)node; HookRef *h=user; if(h->t->cell_size&&h->t->bodies[h->id].used) mark_dirty(h->t,h->id); }
static int body_hook(AthenaTriggers3D *t,Body *b,uint32_t id) {
    if(!b->node||b->hook) return 0;
    HookRef *h=malloc(sizeof(*h)); if(!h) return -1;
    h->t=t; h->id=(uint16_t)id;
    if(athena_node3d_add_moved_hook(b->node,body_moved,h)<0) { free(h); return -1; }
    b->hook=h; return 0;
}
int athena_triggers3d_set_grid(AthenaTriggers3D *t,float size,uint32_t capacity) {
    if(!t||!athena_float_isfinite(size)||size<0||capacity>MAX_ENTRIES) return -1;
    for(uint32_t i=0;i<t->body_end;i++) if(t->bodies[i].used) { body_unhook(t,&t->bodies[i]); t->bodies[i].dirty=0; t->bodies[i].large=0; t->bodies[i].first_entry=0; }
    for(uint32_t i=0;i<t->zone_end;i++) t->zones[i].large=0;
    grid_free(t);
    if(size==0) return 0;
    if(!capacity) capacity=DEFAULT_ENTRIES;
    uint32_t buckets=16; while(buckets<capacity*2u) buckets<<=1;
    t->cells=calloc(capacity+1u,sizeof(Cell)); t->buckets=malloc(buckets*sizeof(int32_t)); t->entries=calloc(capacity+1u,sizeof(Entry));
    if(!t->cells||!t->buckets||!t->entries) { grid_free(t); return -1; }
    for(uint32_t i=0;i<buckets;i++) t->buckets[i]=-1;
    t->cell_size=size; t->capacity=capacity; t->bucket_mask=buckets-1;
    for(uint32_t i=0;i<t->body_end;i++) {
        Body *b=&t->bodies[i]; if(!b->used) continue;
        body_refresh(t,b);
        if(body_hook(t,b,i)<0||!body_enter_grid(t,b,i)) {
            for(uint32_t j=0;j<t->body_end;j++) if(t->bodies[j].used) body_unhook(t,&t->bodies[j]);
            grid_free(t); return -1;
        }
    }
    return 0;
}
float athena_triggers3d_grid_cell_size(const AthenaTriggers3D *t) { return t?t->cell_size:0; }

int athena_triggers3d_add_zone(AthenaTriggers3D *t,AthenaTrigger3DShape shape,const float a[3],const float b[3],uint32_t mask) {
    if(!t||!shape_ok(shape,a,b)) return -1;
    uint32_t id=0; while(id<t->zone_end&&t->zones[id].used) id++;
    if(id>=ATHENA_TRIGGERS3D_MAX) return -1;
    if(id>=t->zone_cap) {
        uint32_t next=t->zone_cap?t->zone_cap*2:16;
        Zone *z=realloc(t->zones,next*sizeof(*z)); if(!z) return -1;
        t->zones=z;
        if(!resize(t,next,t->body_cap)) return -1;
        memset(t->zones+t->zone_cap,0,(next-t->zone_cap)*sizeof(*z)); t->zone_cap=next;
    }
    Zone *z=&t->zones[id]; free(z->occ); memset(z,0,sizeof(*z));
    z->used=z->enabled=1; z->shape=(uint8_t)shape; z->mask=mask;
    memcpy(z->a,a,12); if(shape==ATHENA_TRIGGER3D_BOX) memcpy(z->b,b,12); else z->b[0]=b[0];
    memset(&t->inside[id*t->body_cap],0,t->body_cap);
    if(id>=t->zone_end) t->zone_end=id+1;
    return (int)id;
}
static Zone *zone_at(const AthenaTriggers3D *t,int id) { return t&&id>=0&&(uint32_t)id<t->zone_end&&t->zones[id].used?&t->zones[id]:NULL; }
static Body *body_at(const AthenaTriggers3D *t,int id) { return t&&id>=0&&(uint32_t)id<t->body_end&&t->bodies[id].used?&t->bodies[id]:NULL; }
int athena_triggers3d_set_zone(AthenaTriggers3D *t,int id,AthenaTrigger3DShape shape,const float a[3],const float b[3]) {
    Zone *z=zone_at(t,id); if(!z||!shape_ok(shape,a,b)) return -1;
    z->shape=(uint8_t)shape; memcpy(z->a,a,12); if(shape==ATHENA_TRIGGER3D_BOX) memcpy(z->b,b,12); else z->b[0]=b[0];
    return 0;
}
int athena_triggers3d_add_body(AthenaTriggers3D *t,const float p[3],float radius,uint32_t layers) {
    if(!t||!p||!finite3(p)||!athena_float_isfinite(radius)||radius<0) return -1;
    uint32_t id=0; while(id<t->body_end&&t->bodies[id].used) id++;
    if(id>=ATHENA_TRIGGERS3D_MAX) return -1;
    if(id>=t->body_cap) {
        uint32_t next=t->body_cap?t->body_cap*2:16;
        Body *b=realloc(t->bodies,next*sizeof(*b)); if(!b) return -1;
        t->bodies=b;
        if(!resize(t,t->zone_cap,next)) return -1;
        memset(t->bodies+t->body_cap,0,(next-t->body_cap)*sizeof(*b)); t->body_cap=next;
    }
    Body *b=&t->bodies[id]; memset(b,0,sizeof(*b));
    b->used=1; b->layers=layers; memcpy(b->p,p,12); memcpy(b->eff,p,12); b->radius=radius;
    for(uint32_t z=0;z<t->zone_cap;z++) t->inside[z*t->body_cap+id]=0;
    if(id>=t->body_end) t->body_end=id+1;
    if(t->cell_size&&!body_enter_grid(t,b,id)) { memset(b,0,sizeof(*b)); return -1; }
    return (int)id;
}
int athena_triggers3d_set_body(AthenaTriggers3D *t,int id,const float p[3],float radius) {
    Body *b=body_at(t,id); if(!b||!p||!finite3(p)||!athena_float_isfinite(radius)||radius<0) return -1;
    memcpy(b->p,p,12); b->radius=radius; mark_dirty(t,(uint32_t)id); return 0;
}
int athena_triggers3d_remove_zone(AthenaTriggers3D *t,int id) {
    Zone *z=zone_at(t,id); if(!z) return -1;
    memset(&t->inside[(uint32_t)id*t->body_cap],0,t->body_cap);
    athena_node3d_release(z->node); free(z->occ); memset(z,0,sizeof(*z)); return 0;
}
int athena_triggers3d_remove_body(AthenaTriggers3D *t,int id) {
    Body *b=body_at(t,id); if(!b) return -1;
    for(uint32_t z=0;z<t->zone_cap;z++) if(t->inside[z*t->body_cap+(uint32_t)id]) {
        t->inside[z*t->body_cap+(uint32_t)id]=0; if(z<t->zone_end&&t->zones[z].used) occ_remove(&t->zones[z],(uint32_t)id);
    }
    if(t->cell_size) { body_leave_grid(t,b,(uint32_t)id); body_unhook(t,b); }
    athena_node3d_release(b->node); memset(b,0,sizeof(*b)); return 0;
}
int athena_triggers3d_set_zone_enabled(AthenaTriggers3D *t,int id,int enabled) { Zone *z=zone_at(t,id); if(!z) return -1; z->enabled=enabled!=0; return 0; }
int athena_triggers3d_set_mask(AthenaTriggers3D *t,int id,uint32_t mask) { Zone *z=zone_at(t,id); if(!z) return -1; z->mask=mask; return 0; }
int athena_triggers3d_set_layers(AthenaTriggers3D *t,int id,uint32_t layers) { Body *b=body_at(t,id); if(!b) return -1; b->layers=layers; return 0; }
int athena_triggers3d_set_zone_stay(AthenaTriggers3D *t,int id,int enabled) { Zone *z=zone_at(t,id); if(!z) return -1; z->stay=enabled!=0; return 0; }
int athena_triggers3d_set_stay_all(AthenaTriggers3D *t,int enabled) { if(!t) return -1; t->stay_all=enabled!=0; return 0; }
static int follow(AthenaNode3D **slot,float off[3],AthenaNode3D *node,const float offset[3]) {
    if(offset&&!finite3(offset)) return -1;
    athena_node3d_retain(node); athena_node3d_release(*slot); *slot=node;
    if(offset) memcpy(off,offset,12); else off[0]=off[1]=off[2]=0;
    return 0;
}
int athena_triggers3d_zone_follow(AthenaTriggers3D *t,int id,AthenaNode3D *node,const float offset[3]) {
    Zone *z=zone_at(t,id); return z?follow(&z->node,z->offset,node,offset):-1;
}
int athena_triggers3d_body_follow(AthenaTriggers3D *t,int id,AthenaNode3D *node,const float offset[3]) {
    Body *b=body_at(t,id); if(!b) return -1;
    if(t->cell_size) body_unhook(t,b);
    if(follow(&b->node,b->offset,node,offset)<0) { if(t->cell_size) body_hook(t,b,(uint32_t)id); return -1; }
    if(t->cell_size) { body_hook(t,b,(uint32_t)id); mark_dirty(t,(uint32_t)id); }
    return 0;
}
static int overlaps(const Zone *z,const float zo[3],const float p[3],float r) {
    if(z->shape==ATHENA_TRIGGER3D_SPHERE) {
        float dx=p[0]-(z->a[0]+zo[0]),dy=p[1]-(z->a[1]+zo[1]),dz=p[2]-(z->a[2]+zo[2]),rr=z->b[0]+r;
        return dx*dx+dy*dy+dz*dz<=rr*rr;
    }
    float d2=0;
    for(int i=0;i<3;i++) {
        float lo=z->a[i]+zo[i],hi=z->b[i]+zo[i],v=p[i]<lo?lo-p[i]:p[i]>hi?p[i]-hi:0;
        d2+=v*v;
    }
    return d2<=r*r;
}
/* Tests one pair and queues the change: 0 on failure, 1 otherwise, 2 when the overlap was computed. */
static inline __attribute__((always_inline)) int test_pair(AthenaTriggers3D *t,Zone *z,const float zo[3],uint32_t zi,uint32_t bi) {
    Body *b=&t->bodies[bi]; uint8_t *row=&t->inside[zi*t->body_cap];
    int match=z->enabled&&(z->mask&b->layers);
    int now=match&&overlaps(z,zo,b->eff,b->radius);
    if(now!=row[bi]) {
        row[bi]=(uint8_t)now;
        if(now) { if(!occ_add(z,bi,t->epoch)) return 0; } else occ_remove(z,bi);
        if(!push_event(t,now?ATHENA_TRIGGER3D_ENTER:ATHENA_TRIGGER3D_EXIT,zi,bi)) return 0;
    }
    return 1+(match!=0);
}
static void sort_events(AthenaTrigger3DEvent *e,uint32_t n) {   /* by body id; segments are short */
    for(uint32_t i=1;i<n;i++) { AthenaTrigger3DEvent v=e[i]; uint32_t j=i; while(j&&e[j-1].body>v.body) { e[j]=e[j-1]; j--; } e[j]=v; }
}
static void zone_bounds(const Zone *z,const float zo[3],float lo[3],float hi[3]) {
    for(int i=0;i<3;i++) {
        if(z->shape==ATHENA_TRIGGER3D_SPHERE) { lo[i]=z->a[i]+zo[i]-z->b[0]; hi[i]=z->a[i]+zo[i]+z->b[0]; }
        else { lo[i]=z->a[i]+zo[i]; hi[i]=z->b[i]+zo[i]; }
    }
}
int athena_triggers3d_update(AthenaTriggers3D *t) {
    if(!t) return -1;
    t->event_count=0; t->epoch++;
    memset(&t->stats,0,sizeof(t->stats));
    /* Effective body positions: re-binned when moved (grid), re-read every update otherwise. */
    if(t->cell_size) {
        t->stats.dirty_bodies=t->dirty_count;
        for(uint32_t i=0;i<t->dirty_count;i++) {
            uint32_t id=t->dirty_ids[i]; Body *b=&t->bodies[id];
            b->dirty=0; if(!b->used) continue;
            body_refresh(t,b);
            float lo[3],hi[3]; int32_t rl[3],rh[3];
            for(int k=0;k<3;k++) { lo[k]=b->eff[k]-b->radius; hi[k]=b->eff[k]+b->radius; }
            cell_range(t,lo,hi,rl,rh);
            if(!b->large&&b->first_entry&&!memcmp(rl,b->lo,12)&&!memcmp(rh,b->hi,12)) continue;   /* same cells */
            body_leave_grid(t,b,id);
            if(!body_enter_grid(t,b,id)) return -1;
        }
        t->dirty_count=0;
    } else for(uint32_t i=0;i<t->body_end;i++) if(t->bodies[i].used) body_refresh(t,&t->bodies[i]);
    for(uint32_t zi=0;zi<t->zone_end;zi++) {
        Zone *z=&t->zones[zi];
        if(!z->used) continue;
        t->stats.zones++;
        float zo[3]={0,0,0};
        if(z->node) { athena_node3d_last_world_position(z->node,zo); for(int i=0;i<3;i++) zo[i]+=z->offset[i]; }
        uint32_t first=t->event_count; t->stamp++; uint32_t tested=0,seen=0; int r;
        if(t->cell_size&&!z->enabled) {
            /* A disabled zone releases its occupants and tests nothing. */
            for(uint32_t i=z->occ_count;i-->0;) { r=test_pair(t,z,zo,zi,z->occ[i].body); if(!r) return -1; tested+=r>1; }
        } else if(t->cell_size) {
            float lo[3],hi[3]; int32_t rl[3],rh[3];
            zone_bounds(z,zo,lo,hi); cell_range(t,lo,hi,rl,rh);
            double n=range_cells(rl,rh);
            if(n>LARGE_ZONE_CELLS) {
                t->stats.large_zones++;
                for(uint32_t bi=0;bi<t->body_end;bi++) { if(!t->bodies[bi].used) continue; seen++; r=test_pair(t,z,zo,zi,bi); if(!r) return -1; tested+=r>1; }
            } else {
                int32_t k[3];
                for(k[0]=rl[0];k[0]<=rh[0];k[0]++) for(k[1]=rl[1];k[1]<=rh[1];k[1]++) for(k[2]=rl[2];k[2]<=rh[2];k[2]++) {
                    int32_t c=cell_find(t,k,0); if(c<0) continue;
                    for(uint32_t e=t->cells[c].head;e;e=t->entries[e].next) {
                        Body *b=&t->bodies[t->entries[e].body];
                        if(b->stamp==t->stamp) continue;
                        b->stamp=t->stamp; seen++;
                        r=test_pair(t,z,zo,zi,t->entries[e].body); if(!r) return -1; tested+=r>1;
                    }
                }
                for(uint32_t i=0;i<t->large_count;i++) {
                    Body *b=&t->bodies[t->large_bodies[i]];
                    if(b->stamp==t->stamp) continue;
                    b->stamp=t->stamp; seen++;
                    r=test_pair(t,z,zo,zi,t->large_bodies[i]); if(!r) return -1; tested+=r>1;
                }
                /* Occupants that are no longer candidates are not overlapping: they leave. */
                for(uint32_t i=z->occ_count;i-->0;) if(t->bodies[z->occ[i].body].stamp!=t->stamp) {
                    r=test_pair(t,z,zo,zi,z->occ[i].body); if(!r) return -1; tested+=r>1;
                }
            }
        } else {
            for(uint32_t bi=0;bi<t->body_end;bi++) {
                if(!t->bodies[bi].used) continue;
                seen++; r=test_pair(t,z,zo,zi,bi); if(!r) return -1; tested+=r>1;
            }
        }
        t->stats.candidates+=seen; t->stats.tested+=tested;
        sort_events(t->events+first,t->event_count-first);
    }
    t->stats.events=t->event_count;
    /* STAY: bodies inside since an earlier update, after every ENTER/EXIT. */
    uint32_t stay_first=t->event_count;
    for(uint32_t zi=0;zi<t->zone_end;zi++) {
        Zone *z=&t->zones[zi];
        if(!z->used||!(z->stay||t->stay_all)) continue;
        uint32_t first=t->event_count;
        for(uint32_t i=0;i<z->occ_count;i++) if(z->occ[i].epoch!=t->epoch&&!push_event(t,ATHENA_TRIGGER3D_STAY,zi,z->occ[i].body)) return -1;
        sort_events(t->events+first,t->event_count-first);
    }
    t->stats.stay_events=t->event_count-stay_first;
    for(uint32_t zi=0;zi<t->zone_end;zi++) if(t->zones[zi].used) t->stats.occupied_pairs+=t->zones[zi].occ_count;
    for(uint32_t bi=0;bi<t->body_end;bi++) if(t->bodies[bi].used) t->stats.bodies++;
    t->stats.cells=t->cells_used; t->stats.entries=t->entries_used; t->stats.large_bodies=t->large_count;
    return (int)t->event_count;
}
void athena_triggers3d_stats(const AthenaTriggers3D *t,AthenaTriggers3DStats *out) { if(out) { if(t) *out=t->stats; else memset(out,0,sizeof(*out)); } }
const AthenaTrigger3DEvent *athena_triggers3d_events(const AthenaTriggers3D *t,uint32_t *count) {
    if(count) *count=t?t->event_count:0;
    return t?t->events:NULL;
}
int athena_triggers3d_inside(const AthenaTriggers3D *t,int zone,int body) {
    return zone_at(t,zone)&&body_at(t,body)&&t->inside[(uint32_t)zone*t->body_cap+(uint32_t)body];
}
int athena_triggers3d_occupants(const AthenaTriggers3D *t,int zone,uint16_t *out,uint32_t max) {
    if(!zone_at(t,zone)) return -1;
    uint32_t n=0;
    for(uint32_t b=0;b<t->body_end;b++) if(t->bodies[b].used&&t->inside[(uint32_t)zone*t->body_cap+b]) { if(n<max&&out) out[n]=(uint16_t)b; n++; }
    return (int)n;
}
