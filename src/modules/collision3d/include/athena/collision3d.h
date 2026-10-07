#ifndef ATHENA_COLLISION3D_H
#define ATHENA_COLLISION3D_H
#include <stdint.h>
#include <athena/scene3d.h>
/* Light 3D collision in C: static triangles (level meshes, glTF scenes,
 * boxes) in a bounding volume hierarchy, ray/sphere queries, and kinematic
 * characters that move with collide-and-slide. Not a rigid body solver.
 * Everything is float: the R5900 emulates double in software. */
typedef struct AthenaCollision3DWorld AthenaCollision3DWorld;
typedef struct AthenaCharacter3D AthenaCharacter3D;
#define ATHENA_COLLISION3D_EINVAL (-1)
#define ATHENA_COLLISION3D_ENOMEM (-2)
#define ATHENA_COLLISION3D_EFULL (-3)
/* At most this many triangles in a world. */
#define ATHENA_COLLISION3D_MAX_TRIANGLES 262144u
/* Default Loop priority of the character system: after the game's update
 * and Animation3D (-100), before Scene3D (0) reads the bound nodes. */
#define ATHENA_CHARACTER3D_LOOP_PRIORITY (-50)

AthenaCollision3DWorld *athena_collision3d_world_create(void);
void athena_collision3d_world_retain(AthenaCollision3DWorld *world);
void athena_collision3d_world_release(AthenaCollision3DWorld *world);
/* Shapes: groups of static triangles, copied in world space. Each returns a
 * shape id (> 0) or a negative error. Triangles face their counter-clockwise
 * side; sweeps (characters, sphere casts) are blocked by front faces only,
 * rays hit both sides. Degenerate triangles are skipped. layer is a 32-bit
 * flag set tested against query masks. The tree is rebuilt lazily by the
 * next query after any change. */
int athena_collision3d_add_triangles(AthenaCollision3DWorld *world,const float *positions,
    uint32_t triangle_count,const AthenaMatrix4 *transform,uint32_t layer);
/* A mesh's triangles under transform (NULL: identity). Skinned and morphed
 * meshes are added in their base pose. */
int athena_collision3d_add_mesh(AthenaCollision3DWorld *world,const AthenaMesh3D *mesh,
    const AthenaMatrix4 *transform,uint32_t layer);
/* Every visible mesh of a node subtree, with the transforms composed from
 * the node's ancestors (the scene need not be updated). One shape. */
int athena_collision3d_add_node(AthenaCollision3DWorld *world,AthenaNode3D *node,uint32_t layer);
/* An axis-aligned box (12 triangles facing out). */
int athena_collision3d_add_box(AthenaCollision3DWorld *world,const float min[3],const float max[3],uint32_t layer);
int athena_collision3d_remove(AthenaCollision3DWorld *world,int shape);
int athena_collision3d_set_layer(AthenaCollision3DWorld *world,int shape,uint32_t layer);
uint32_t athena_collision3d_triangle_count(const AthenaCollision3DWorld *world);

typedef struct {
    float distance;     /* along the ray or sweep, in world units */
    float point[3];     /* contact point */
    float normal[3];    /* unit, pointing toward the query */
    int shape;          /* shape id */
    uint32_t triangle;  /* triangle index within the shape */
} AthenaCollision3DHit;
/* Nearest hit along origin + t * direction for t in [0, max_distance]
 * (direction need not be unit; max_distance is in world units). Returns 1 on
 * a hit, 0 on none, negative on invalid input. */
int athena_collision3d_raycast(AthenaCollision3DWorld *world,const float origin[3],const float direction[3],
    float max_distance,uint32_t mask,AthenaCollision3DHit *hit);
/* A sphere swept from center along direction: the first front face it
 * touches. A sphere already overlapping a face hits it at distance 0. */
int athena_collision3d_sphere_cast(AthenaCollision3DWorld *world,const float center[3],float radius,
    const float direction[3],float max_distance,uint32_t mask,AthenaCollision3DHit *hit);
/* Shapes with a triangle within radius of center (both sides): writes up to
 * capacity distinct shape ids and returns how many were found in all. */
int athena_collision3d_overlap_sphere(AthenaCollision3DWorld *world,const float center[3],float radius,
    uint32_t mask,int *shapes,uint32_t capacity);

/* Calls visit for every triangle in the layers of mask whose bounds meet
 * [min, max]: corners and unit front normal, in world space. For contact
 * generation in other modules (Physics3D). Returns a negative error when
 * the tree cannot be built, else 0. */
typedef void (*AthenaCollision3DVisit)(void *context,const float a[3],const float b[3],const float c[3],
    const float normal[3],int shape);
int athena_collision3d_query_triangles(AthenaCollision3DWorld *world,const float min[3],const float max[3],
    uint32_t mask,AthenaCollision3DVisit visit,void *context);

/* Characters: an upright ellipsoid (radius around, height tall) placed by
 * its feet, moved with collide-and-slide against the world's front faces.
 * Gravity accelerates vy; surfaces up to max_slope degrees are ground
 * (standing still on them does not slide), steeper ones are walls; ledges
 * up to step_height are climbed; walking down slopes and steps keeps the
 * character on the ground. */
typedef struct {
    float radius,height,step_height,max_slope;
    float gravity[3];
    uint32_t mask;
} AthenaCharacter3DDesc;
void athena_character3d_desc_default(AthenaCharacter3DDesc *desc);
/* Retains the world. */
AthenaCharacter3D *athena_character3d_create(AthenaCollision3DWorld *world,const AthenaCharacter3DDesc *desc);
void athena_character3d_retain(AthenaCharacter3D *character);
void athena_character3d_release(AthenaCharacter3D *character);
/* Teleports the feet, without collisions. */
int athena_character3d_set_position(AthenaCharacter3D *character,float x,float y,float z);
void athena_character3d_get_position(const AthenaCharacter3D *character,float out[3]);
int athena_character3d_set_velocity(AthenaCharacter3D *character,float x,float y,float z);
void athena_character3d_get_velocity(const AthenaCharacter3D *character,float out[3]);
/* Moves by a displacement with collisions (no gravity; ground snapping and
 * steps when on the ground). */
int athena_character3d_move(AthenaCharacter3D *character,float dx,float dy,float dz);
/* Gravity, then move by velocity * dt; a landing or ceiling zeroes vy. */
int athena_character3d_step(AthenaCharacter3D *character,float dt);
typedef struct {
    int on_ground,hit_wall,hit_ceiling;
    float ground_normal[3];
} AthenaCharacter3DState;
void athena_character3d_state(const AthenaCharacter3D *character,AthenaCharacter3DState *out);
/* A node whose position follows the feet after every move and step
 * (retained; NULL unbinds). */
int athena_character3d_bind(AthenaCharacter3D *character,AthenaNode3D *node);
void athena_character3d_set_enabled(AthenaCharacter3D *character,int enabled);
int athena_character3d_enabled(const AthenaCharacter3D *character);
/* Steps every live enabled character. */
int athena_character3d_step_all(float dt);
/* One Loop POST_UPDATE system stepping every character (see LOOP_PRIORITY);
 * owner tags it for athena_character3d_detach_owner(). */
int athena_character3d_attach_loop(int priority,void *owner);
int athena_character3d_detach_loop(void);
void athena_character3d_detach_owner(void *owner);
int athena_character3d_loop_system(void);
#endif
