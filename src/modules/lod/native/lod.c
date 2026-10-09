#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <athena/lod.h>
#include <athena/float_bits.h>
struct AthenaLODGroup {
    AthenaNode3D *node;
    AthenaLODLevel levels[ATHENA_LOD_MAX_LEVELS];
    uint32_t count;
    float hysteresis,distance;
    int current;   /* level, -1 hidden, -2 never selected */
    uint8_t enabled,hidden;
    AthenaLODGroup *prev,*next;
};
static AthenaLODGroup *groups;
static float bias=1,draw_distance=0;

AthenaLODGroup *athena_lod_group_create(AthenaNode3D *node,const AthenaLODLevel *levels,uint32_t count,float hysteresis) {
    if(!node||!levels||!count||count>ATHENA_LOD_MAX_LEVELS||!athena_float_isfinite(hysteresis)||hysteresis<0||hysteresis>.5f)
        return NULL;
    for(uint32_t i=0;i<count;i++)
        if(!athena_float_isfinite(levels[i].until)||levels[i].until<=0||(i&&levels[i].until<=levels[i-1].until)) return NULL;
    AthenaLODGroup *g=calloc(1,sizeof(*g)); if(!g) return NULL;
    g->node=node; athena_node3d_retain(node);
    for(uint32_t i=0;i<count;i++) { g->levels[i]=levels[i]; athena_mesh3d_retain(levels[i].mesh); }
    g->count=count; g->hysteresis=hysteresis; g->current=-2; g->enabled=1;
    g->next=groups; if(groups) groups->prev=g; groups=g;
    return g;
}
void athena_lod_group_destroy(AthenaLODGroup *g) {
    if(!g) return;
    if(g->prev) g->prev->next=g->next; else groups=g->next;
    if(g->next) g->next->prev=g->prev;
    for(uint32_t i=0;i<g->count;i++) athena_mesh3d_release(g->levels[i].mesh);
    athena_node3d_release(g->node); free(g);
}
void athena_lod_group_set_enabled(AthenaLODGroup *g,int enabled) { if(g) { g->enabled=enabled!=0; if(!g->enabled) g->current=-2; } }
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
void athena_lod_update(const float eye[3],AthenaLODStats *stats) {
    AthenaLODStats ignored; if(!stats) stats=&ignored;
    memset(stats,0,sizeof(*stats));
    if(!eye) return;
    for(AthenaLODGroup *g=groups;g;g=g->next) {
        if(!g->enabled) continue;
        stats->groups++;
        float p[3]; athena_node3d_last_world_position(g->node,p);
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
        if(g->hidden) stats->hidden++; else stats->per_level[g->current]++;
    }
}
/* Groups belong to their JS handles (finalizers free them): only the
 * global settings are reset here. */
void athena_lod_module_shutdown(void) { bias=1; draw_distance=0; }
