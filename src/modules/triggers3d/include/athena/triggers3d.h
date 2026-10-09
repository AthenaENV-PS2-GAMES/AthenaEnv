#ifndef ATHENA_TRIGGERS3D_H
#define ATHENA_TRIGGERS3D_H
#include <stdint.h>
#include <athena/scene3d.h>
/* Trigger volumes: zones (boxes or spheres) and bodies (spheres, a radius
 * of 0 is a point). update() tests every enabled zone against every enabled
 * body whose layers it watches (layers & mask) and queues ENTER/EXIT events
 * for the pairs that changed. Zones and bodies may follow a Scene3D node
 * (its world position of the last update plus an offset). Ids are small
 * integers reused after removal. Main thread only. */
#define ATHENA_TRIGGERS3D_MAX 1024u
typedef struct AthenaTriggers3D AthenaTriggers3D;
typedef enum { ATHENA_TRIGGER3D_BOX=0, ATHENA_TRIGGER3D_SPHERE=1 } AthenaTrigger3DShape;
typedef enum { ATHENA_TRIGGER3D_ENTER=1, ATHENA_TRIGGER3D_EXIT=2 } AthenaTrigger3DEventType;
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
/* Tests every pair; returns the number of events queued (read with events()). */
int athena_triggers3d_update(AthenaTriggers3D *t);
/* The events of the last update; valid until the next update. */
const AthenaTrigger3DEvent *athena_triggers3d_events(const AthenaTriggers3D *t,uint32_t *count);
/* 1 when the body is inside the zone as of the last update. */
int athena_triggers3d_inside(const AthenaTriggers3D *t,int zone,int body);
/* Bodies inside the zone (up to max ids in out); returns the total. */
int athena_triggers3d_occupants(const AthenaTriggers3D *t,int zone,uint16_t *out,uint32_t max);
#endif
