#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <athena/lod.h>
#include <athena/float_bits.h>
#define LOOSE 0   /* cell 0 holds the groups that do not fit the hash: never skipped */
#define DEFAULT_CELLS 1024u
#define MAX_CELLS 65535u
#define KEY_LIMIT 1048576
struct AthenaLODGroup {
    AthenaNode3D *node;
    AthenaLODLevel levels[ATHENA_LOD_MAX_LEVELS];
    uint32_t count;
    float hysteresis,distance;
    int current;   /* level, -1 hidden, -2 never selected */
    uint8_t enabled,hidden;
    AthenaLODGroup *prev,*next;
    /* Grid state, valid while cell >= 0. */
    float pos[3],far_raw;   /* far_raw: last threshold * (1 + hysteresis), before the bias */
    int32_t cell;
    uint8_t settled,dirty;
    AthenaLODGroup *cprev,*cnext,*dprev,*dnext;
};
typedef struct { int32_t key[3]; int32_t chain; AthenaLODGroup *head; uint32_t count,settled; float far_raw; uint8_t used; } Cell;
static AthenaLODGroup *groups;
static float bias=1,draw_distance=0;
static struct { float size; Cell *cells; int32_t *buckets; uint32_t capacity,mask,high,free_head; AthenaLODGroup *dirty; } grid;

static void moved(void *user,AthenaNode3D *node);
static uint32_t hash_key(const int32_t k[3]) {
    uint32_t h=(uint32_t)k[0]*73856093u^(uint32_t)k[1]*19349663u^(uint32_t)k[2]*83492791u; h^=h>>15; return h;
}
static int32_t axis_key(float v) {
    float c=floorf(v/grid.size); if(c>KEY_LIMIT) c=KEY_LIMIT; if(c<-KEY_LIMIT) c=-KEY_LIMIT; return (int32_t)c;
}
static int32_t cell_find(const int32_t k[3],int create) {
    uint32_t b=hash_key(k)&grid.mask;
    for(int32_t i=grid.buckets[b];i>=0;i=grid.cells[i].chain)
        if(grid.cells[i].key[0]==k[0]&&grid.cells[i].key[1]==k[1]&&grid.cells[i].key[2]==k[2]) return i;
    if(!create) return -1;
    int32_t i;
    if(grid.free_head) { i=(int32_t)grid.free_head; grid.free_head=(uint32_t)grid.cells[i].chain; }
    else if(grid.high<grid.capacity) i=(int32_t)grid.high++;
    else return LOOSE;
    Cell *c=&grid.cells[i]; memset(c,0,sizeof(*c)); c->used=1; memcpy(c->key,k,12);
    c->chain=grid.buckets[b]; grid.buckets[b]=i; return i;
}
static void cell_release(int32_t i) {
    Cell *c=&grid.cells[i];
    uint32_t b=hash_key(c->key)&grid.mask;
    for(int32_t *link=&grid.buckets[b];*link>=0;link=&grid.cells[*link].chain) if(*link==i) { *link=c->chain; break; }
    c->used=0; c->chain=(int32_t)grid.free_head; grid.free_head=(uint32_t)i;
}
static void cell_add(int32_t i,AthenaLODGroup *g) {
    Cell *c=&grid.cells[i];
    g->cprev=NULL; g->cnext=c->head; if(c->head) c->head->cprev=g; c->head=g; g->cell=i;
    c->count++; if(g->settled) c->settled++;
    if(i!=LOOSE&&g->far_raw>c->far_raw) c->far_raw=g->far_raw;
}
static void cell_remove(AthenaLODGroup *g) {
    if(g->cell<0) return;
    Cell *c=&grid.cells[g->cell];
    if(g->cprev) g->cprev->cnext=g->cnext; else c->head=g->cnext;
    if(g->cnext) g->cnext->cprev=g->cprev;
    c->count--; if(g->settled) c->settled--;
    int32_t i=g->cell; g->cell=-1; g->cprev=g->cnext=NULL;
    if(!c->count&&i!=LOOSE) cell_release(i);
}
static void dirty_push(AthenaLODGroup *g) {
    if(g->dirty) return;
    g->dirty=1; g->dprev=NULL; g->dnext=grid.dirty; if(grid.dirty) grid.dirty->dprev=g; grid.dirty=g;
}
static void dirty_remove(AthenaLODGroup *g) {
    if(!g->dirty) return;
    if(g->dprev) g->dprev->dnext=g->dnext; else grid.dirty=g->dnext;
    if(g->dnext) g->dnext->dprev=g->dprev;
    g->dirty=0; g->dprev=g->dnext=NULL;
}
static void place(AthenaLODGroup *g) {   /* reads the node position and bins the group */
    athena_node3d_last_world_position(g->node,g->pos);
    int32_t k[3]={axis_key(g->pos[0]),axis_key(g->pos[1]),axis_key(g->pos[2])};
    int32_t i=cell_find(k,1);
    if(g->cell==i) return;
    cell_remove(g); cell_add(i,g);
}
static int grid_attach(AthenaLODGroup *g) {
    if(athena_node3d_add_moved_hook(g->node,moved,g)<0) return -1;
    g->far_raw=g->levels[g->count-1].until*(1+g->hysteresis);
    g->settled=g->current==-1&&g->hidden; g->cell=-1;
    place(g); return 0;
}
static void grid_detach(AthenaLODGroup *g) {
    athena_node3d_remove_moved_hook(g->node,moved,g);
    dirty_remove(g); cell_remove(g); g->settled=0;
}
static void moved(void *user,AthenaNode3D *node) { (void)node; AthenaLODGroup *g=user; if(g->cell>=0) dirty_push(g); }
static void grid_free(void) {
    free(grid.cells); free(grid.buckets); memset(&grid,0,sizeof(grid));
}
int athena_lod_set_grid(float size,uint32_t capacity) {
    if(!athena_float_isfinite(size)||size<0||capacity>MAX_CELLS) return -1;
    for(AthenaLODGroup *g=groups;g;g=g->next) if(g->cell>=0||g->dirty) grid_detach(g);
    grid_free();
    if(size==0) return 0;
    if(!capacity) capacity=DEFAULT_CELLS;
    uint32_t buckets=16; while(buckets<capacity*2u) buckets<<=1;
    grid.cells=calloc(capacity+1u,sizeof(Cell)); grid.buckets=malloc(buckets*sizeof(int32_t));
    if(!grid.cells||!grid.buckets) { grid_free(); return -1; }
    for(uint32_t i=0;i<buckets;i++) grid.buckets[i]=-1;
    grid.size=size; grid.capacity=capacity; grid.mask=buckets-1; grid.high=1; grid.cells[LOOSE].used=1;
    for(AthenaLODGroup *g=groups;g;g=g->next) if(g->enabled&&grid_attach(g)<0) {
        for(AthenaLODGroup *h=groups;h;h=h->next) if(h->cell>=0||h->dirty) grid_detach(h);
        grid_free(); return -1;
    }
    return 0;
}
float athena_lod_grid_cell_size(void) { return grid.size; }

AthenaLODGroup *athena_lod_group_create(AthenaNode3D *node,const AthenaLODLevel *levels,uint32_t count,float hysteresis) {
    if(!node||!levels||!count||count>ATHENA_LOD_MAX_LEVELS||!athena_float_isfinite(hysteresis)||hysteresis<0||hysteresis>.5f)
        return NULL;
    for(uint32_t i=0;i<count;i++)
        if(!athena_float_isfinite(levels[i].until)||levels[i].until<=0||(i&&levels[i].until<=levels[i-1].until)) return NULL;
    AthenaLODGroup *g=calloc(1,sizeof(*g)); if(!g) return NULL;
    g->node=node; athena_node3d_retain(node);
    for(uint32_t i=0;i<count;i++) { g->levels[i]=levels[i]; athena_mesh3d_retain(levels[i].mesh); }
    g->count=count; g->hysteresis=hysteresis; g->current=-2; g->enabled=1; g->cell=-1;
    if(grid.size>0&&grid_attach(g)<0) {
        for(uint32_t i=0;i<count;i++) athena_mesh3d_release(levels[i].mesh);
        athena_node3d_release(node); free(g); return NULL;
    }
    g->next=groups; if(groups) groups->prev=g; groups=g;
    return g;
}
void athena_lod_group_destroy(AthenaLODGroup *g) {
    if(!g) return;
    if(g->cell>=0||g->dirty) grid_detach(g);
    if(g->prev) g->prev->next=g->next; else groups=g->next;
    if(g->next) g->next->prev=g->prev;
    for(uint32_t i=0;i<g->count;i++) athena_mesh3d_release(g->levels[i].mesh);
    athena_node3d_release(g->node); free(g);
}
void athena_lod_group_set_enabled(AthenaLODGroup *g,int enabled) {
    if(!g) return;
    enabled=enabled!=0;
    if(enabled==g->enabled) return;
    g->enabled=(uint8_t)enabled;
    if(!enabled) { g->current=-2; if(g->cell>=0||g->dirty) grid_detach(g); }
    else if(grid.size>0) grid_attach(g);   /* on failure the group stays outside the index: grid_attach cleaned up */
}
int athena_lod_group_enabled(const AthenaLODGroup *g) { return g&&g->enabled; }
int athena_lod_group_level(const AthenaLODGroup *g) { return !g||g->current==-2?-2:g->hidden?-1:g->current; }
float athena_lod_group_distance(const AthenaLODGroup *g) { return g?g->distance:0; }
int athena_lod_set_bias(float b) { if(!athena_float_isfinite(b)||b<=0) return -1; bias=b; return 0; }
int athena_lod_set_draw_distance(float d) { if(!athena_float_isfinite(d)) return -1; draw_distance=d>0?d:0; return 0; }
float athena_lod_draw_distance(void) { return draw_distance; }
/* The band of level i is [until(i-1), until(i)); staying in the current
 * band needs the distance to leave it widened by the hysteresis. */
static int select_level(const AthenaLODGroup *g,float d) {
    int cur=g->current;
    if(cur>=0&&(uint32_t)cur<g->count) {
        float lo=cur?g->levels[cur-1].until*bias*(1-g->hysteresis):0,hi=g->levels[cur].until*bias*(1+g->hysteresis);
        if(d>=lo&&d<hi) return cur;
    } else if(cur==-1&&g->count) {
        if(d>=g->levels[g->count-1].until*bias*(1-g->hysteresis)) return -1;
    }
    for(uint32_t i=0;i<g->count;i++) if(d<g->levels[i].until*bias) return (int)i;
    return -1;
}
static void evaluate(AthenaLODGroup *g,const float eye[3],const float p[3],AthenaLODStats *stats) {
    float dx=p[0]-eye[0],dy=p[1]-eye[1],dz=p[2]-eye[2],d=sqrtf(dx*dx+dy*dy+dz*dz);
    g->distance=d;
    int level=select_level(g,d);
    /* Keep the selected band separate from visibility: a null mesh in
     * an intermediate band must not inherit the last band's hysteresis. */
    int hidden=level<0||(draw_distance>0&&d>=draw_distance)||!g->levels[level].mesh;
    if(level!=g->current||hidden!=g->hidden) {
        stats->changes++;
        if(hidden) athena_node3d_set_visible(g->node,0);
        else {
            athena_node3d_set_mesh(g->node,g->levels[level].mesh);
            if(!athena_node3d_visible(g->node)) athena_node3d_set_visible(g->node,1);
        }
        g->current=level;
        g->hidden=(uint8_t)hidden;
    }
    stats->evaluated++;
    if(g->hidden) stats->hidden++; else stats->per_level[g->current]++;
}
static void evaluate_cell(Cell *c,const float eye[3],AthenaLODStats *stats) {
    for(AthenaLODGroup *g=c->head;g;g=g->cnext) {
        evaluate(g,eye,g->pos,stats); stats->groups++;
        uint8_t settled=g->current==-1&&g->hidden;
        if(settled!=g->settled) { if(settled) c->settled++; else c->settled--; g->settled=settled; }
    }
}
static float axis_gap(float e,int32_t key) {
    float lo=(float)key*grid.size,hi=lo+grid.size;
    return e<lo?lo-e:e>hi?e-hi:0;
}
static void update_grid(const float eye[3],AthenaLODStats *stats) {
    while(grid.dirty) { AthenaLODGroup *g=grid.dirty; dirty_remove(g); place(g); }
    stats->cells=0;
    for(uint32_t i=1;i<grid.high;i++) {
        Cell *c=&grid.cells[i];
        if(!c->used||!c->count) continue;
        stats->cells++;
        if(c->settled==c->count) {
            float gx=axis_gap(eye[0],c->key[0]),gy=axis_gap(eye[1],c->key[1]),gz=axis_gap(eye[2],c->key[2]);
            float far=c->far_raw*bias*1.0001f+1e-3f;
            if(gx*gx+gy*gy+gz*gz>=far*far) { stats->cells_skipped++; stats->groups+=c->count; stats->hidden+=c->count; continue; }
        }
        evaluate_cell(c,eye,stats);
    }
    evaluate_cell(&grid.cells[LOOSE],eye,stats);
}
void athena_lod_update(const float eye[3],AthenaLODStats *stats) {
    AthenaLODStats ignored; if(!stats) stats=&ignored;
    memset(stats,0,sizeof(*stats));
    if(!eye) return;
    if(grid.size>0) { update_grid(eye,stats); return; }
    for(AthenaLODGroup *g=groups;g;g=g->next) {
        if(!g->enabled) continue;
        stats->groups++;
        float p[3]; athena_node3d_last_world_position(g->node,p);
        evaluate(g,eye,p,stats);
    }
}
/* Groups belong to their JS handles (finalizers free them): only the
 * global settings are reset here. */
void athena_lod_module_shutdown(void) { bias=1; draw_distance=0; athena_lod_set_grid(0,0); }
