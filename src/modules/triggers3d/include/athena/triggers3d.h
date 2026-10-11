#ifndef ATHENA_TRIGGERS3D_H
#define ATHENA_TRIGGERS3D_H
#include <stdint.h>
#include <athena/scene3d.h>
/* Trigger volumes: zones (boxes or spheres) and bodies (spheres, a radius
 * of 0 is a point). update() tests every enabled zone against every enabled
 * body whose layers it watches (layers & mask) and queues ENTER/EXIT events
 * for the pairs that changed; zones that opt in with set_zone_stay() (or the
 * whole world, set_stay_all()) also get a STAY event for every body that was
 * already inside at the previous update and still is. Event order per update:
 * every ENTER/EXIT first (by zone id, then body id), then every STAY (same
 * order); a body that enters in an update gets STAY from the next one. An
 * optional spatial grid (set_grid) avoids testing far pairs and produces
 * exactly the same events. Zones and bodies may follow a Scene3D node
 * (its world position of the last update plus an offset). Ids are small
 * integers reused after removal. Main thread only. */
#define ATHENA_TRIGGERS3D_MAX 1024u
typedef struct AthenaTriggers3D AthenaTriggers3D;
typedef enum { ATHENA_TRIGGER3D_BOX=0, ATHENA_TRIGGER3D_SPHERE=1 } AthenaTrigger3DShape;
typedef enum { ATHENA_TRIGGER3D_ENTER=1, ATHENA_TRIGGER3D_EXIT=2, ATHENA_TRIGGER3D_STAY=3 } AthenaTrigger3DEventType;
typedef struct { uint8_t type; uint16_t zone,body; } AthenaTrigger3DEvent;

AthenaTriggers3D *athena_triggers3d_create(void);
void athena_triggers3d_destroy(AthenaTriggers3D *t);
/* Zone: box (a = min, b = max) or sphere (a = center, b[0] = radius). Returns the id or -1. */
int athena_triggers3d_add_zone(AthenaTriggers3D *t,AthenaTrigger3DShape shape,const float a[3],const float b[3],uint32_t mask);
int athena_triggers3d_set_zone(AthenaTriggers3D *t,int zone,AthenaTrigger3DShape shape,const float a[3],const float b[3]);
int athena_triggers3d_add_body(AthenaTriggers3D *t,const float position[3],float radius,uint32_t layers);
int athena_triggers3d_set_body(AthenaTriggers3D *t,int body,const float position[3],float radius);
/* Removal ends the pairs silently (no EXIT events): the caller, which knows
 * the occupants (occupants()), reports them if it wants. */
int athena_triggers3d_remove_zone(AthenaTriggers3D *t,int zone);
int athena_triggers3d_remove_body(AthenaTriggers3D *t,int body);
int athena_triggers3d_set_zone_enabled(AthenaTriggers3D *t,int zone,int enabled);
int athena_triggers3d_set_mask(AthenaTriggers3D *t,int zone,uint32_t mask);
int athena_triggers3d_set_layers(AthenaTriggers3D *t,int body,uint32_t layers);
/* Follow a node (retained; NULL stops): the shape moves with the node's
 * world position, offset added (box: the box is offset from the node). */
int athena_triggers3d_zone_follow(AthenaTriggers3D *t,int zone,AthenaNode3D *node,const float offset[3]);
int athena_triggers3d_body_follow(AthenaTriggers3D *t,int body,AthenaNode3D *node,const float offset[3]);
/* STAY events for a zone (opt-in; cost is proportional to its occupants), or for every zone. */
int athena_triggers3d_set_zone_stay(AthenaTriggers3D *t,int zone,int enabled);
int athena_triggers3d_set_stay_all(AthenaTriggers3D *t,int enabled);
/* Optional broad phase. cell_size > 0 bins bodies into a hash of cells by their bounding box (a body
 * covering more than 64 cells, or arriving when `capacity` entries (default 4096, at most 65536) are
 * used up, goes to a list tested against every zone) and each zone only tests the bodies of the
 * cells its own box covers (a zone covering more than 256 cells tests every body). Moved bodies are
 * noticed through Scene3D's moved hooks and set_body(), so still bodies cost nothing per update. A
 * body that stops being a candidate of a zone it was inside of gets its EXIT as usual. Events and
 * their order are identical to cell_size = 0. 0 turns the grid off; -1 on bad arguments or memory
 * (the grid is then off). Choose cell_size near the typical zone size. */
int athena_triggers3d_set_grid(AthenaTriggers3D *t,float cell_size,uint32_t capacity);
float athena_triggers3d_grid_cell_size(const AthenaTriggers3D *t);
typedef struct {
    uint32_t zones,bodies;          /* in use */
    uint32_t candidates;            /* (zone, body) pairs considered by the last update */
    uint32_t tested;                /* of those, pairs whose overlap was computed (enabled zone, layers match) */
    uint32_t events,stay_events;    /* ENTER/EXIT and STAY events of the last update */
    uint32_t occupied_pairs;        /* pairs inside as of the last update */
    uint32_t dirty_bodies;          /* bodies re-binned by the last update (grid) */
    uint32_t cells,entries;         /* grid cells and body-cell entries in use */
    uint32_t large_bodies,large_zones; /* bodies/zones handled outside the grid */
} AthenaTriggers3DStats;
void athena_triggers3d_stats(const AthenaTriggers3D *t,AthenaTriggers3DStats *out);
/* Tests the pairs; returns the number of events queued (read with events()). */
int athena_triggers3d_update(AthenaTriggers3D *t);
/* The events of the last update; valid until the next update. */
const AthenaTrigger3DEvent *athena_triggers3d_events(const AthenaTriggers3D *t,uint32_t *count);
/* 1 when the body is inside the zone as of the last update. */
int athena_triggers3d_inside(const AthenaTriggers3D *t,int zone,int body);
/* Bodies inside the zone (up to max ids in out); returns the total. */
int athena_triggers3d_occupants(const AthenaTriggers3D *t,int zone,uint16_t *out,uint32_t max);
#endif
