#ifndef ATHENA_SCENE3D_H
#define ATHENA_SCENE3D_H
#include <stdint.h>
#include <athena/render3d.h>
/* Native transform hierarchy. A node is the scene-graph instance of a mesh:
 * local TRS (Rz*Ry*Rx Euler or xyzw quaternion), optional retained mesh and
 * visibility. world = parent.world * local, column-major as Matrix4.
 * Ownership: a parent retains its children; a child only points back to its
 * parent. Ancestor cycles and depth beyond MAX_DEPTH are rejected, so the
 * strong references form a tree and every release happens once.
 * Setters mark dirty flags only. athena_scene3d_update() recomputes dirty
 * paths; queries and draw never update implicitly and report STALE instead.
 * Main thread only. Failed operations leave the previous state unchanged. */
#define ATHENA_SCENE3D_MAX_DEPTH 64u
typedef enum { ATHENA_SCENE3D_OK=0, ATHENA_SCENE3D_EINVAL=-1, ATHENA_SCENE3D_ENOMEM=-2,
    ATHENA_SCENE3D_ECYCLE=-3, ATHENA_SCENE3D_EDEPTH=-4, ATHENA_SCENE3D_ESTALE=-5,
    ATHENA_SCENE3D_EROOT=-6, ATHENA_SCENE3D_ERENDER=-7 } AthenaScene3DResult;
typedef struct AthenaNode3D AthenaNode3D;
typedef struct AthenaScene3D AthenaScene3D;
const char *athena_scene3d_error(int result);

AthenaNode3D *athena_node3d_create(void);
void athena_node3d_retain(AthenaNode3D *node);
/* The last release detaches children (now parentless) and releases the mesh. */
void athena_node3d_release(AthenaNode3D *node);
/* Retains mesh; NULL removes it. Instances keep their own transforms and are
 * not scene nodes: a node draws its mesh with its world transform. */
int athena_node3d_set_mesh(AthenaNode3D *node,AthenaMesh3D *mesh);
const AthenaMesh3D *athena_node3d_mesh(const AthenaNode3D *node);
int athena_node3d_set_position(AthenaNode3D *node,float x,float y,float z);
int athena_node3d_set_scale(AthenaNode3D *node,float x,float y,float z);
int athena_node3d_set_rotation(AthenaNode3D *node,float x,float y,float z,float w);
/* Radians, local XYZ rotations, composed Rz * Ry * Rx as Model3D.Instance. */
int athena_node3d_set_euler(AthenaNode3D *node,float x,float y,float z);
/* Native motion, integrated by athena_scene3d_advance() (and by the Loop
 * system) instead of a setter call per node per frame. Velocity is in
 * units/s in the parent space; spin is an angular velocity in rad/s about
 * the node's local axes (direction = axis, length = speed). Zero stops it. */
int athena_node3d_set_velocity(AthenaNode3D *node,float x,float y,float z);
int athena_node3d_set_spin(AthenaNode3D *node,float x,float y,float z);
/* Skins: the joint nodes (weak references: a joint is often an ancestor of
 * the skinned node, so retaining it would form a cycle; a freed joint makes
 * the skinned draw report ESTALE) and their inverse bind matrices (copied;
 * NULL means identity). A node with a skinned mesh (Model3D joints and
 * weights) and a skin is drawn with its vertices deformed by
 * world(joint) * inverse_bind each frame, in world space: the node's own
 * transform does not move it, as in glTF. Joints must be in the same scene.
 * Subtrees holding skinned meshes are never culled by their bounds. */
typedef struct AthenaSkin3D AthenaSkin3D;
AthenaSkin3D *athena_skin3d_create(AthenaNode3D *const *joints,uint32_t count,const AthenaMatrix4 *inverse_bind);
void athena_skin3d_retain(AthenaSkin3D *skin);
void athena_skin3d_release(AthenaSkin3D *skin);
uint32_t athena_skin3d_joint_count(const AthenaSkin3D *skin);
/* Retains skin; NULL removes it. */
int athena_node3d_set_skin(AthenaNode3D *node,AthenaSkin3D *skin);
/* Morph target weights (glTF "weights"): count <= ATHENA_MODEL3D_MAX_TARGETS
 * finite values, the rest become 0. A mesh with morph targets and a nonzero
 * weight is blended on the EE at draw (base + sum(weight * delta)), before
 * skinning; the node bounds grow by the weighted delta ranges. */
int athena_node3d_set_weights(AthenaNode3D *node,const float *weights,uint32_t count);
/* A follower takes its parent's weights whenever the parent's are set (the
 * extra primitives of a glTF mesh, which share its morph targets). */
int athena_node3d_set_morph_follower(AthenaNode3D *node,int follow);
/* Copies the ATHENA_MODEL3D_MAX_TARGETS weights. */
void athena_node3d_get_weights(const AthenaNode3D *node,float weights[ATHENA_MODEL3D_MAX_TARGETS]);
/* Local TRS as set (rotation unit length). */
void athena_node3d_get_trs(const AthenaNode3D *node,float position[3],AthenaQuaternion *rotation,float scale[3]);
/* Hidden nodes and their descendants are skipped by draw and bounds. */
int athena_node3d_set_visible(AthenaNode3D *node,int visible);
int athena_node3d_visible(const AthenaNode3D *node);
/* Reparents child, keeping its local transform. Rejects child == parent,
 * child being an ancestor of parent (ECYCLE), a scene root (EROOT) and
 * hierarchies deeper than MAX_DEPTH (EDEPTH). */
int athena_node3d_add_child(AthenaNode3D *parent,AthenaNode3D *child);
/* Detaches node from its parent; harmless for parentless nodes. */
void athena_node3d_detach(AthenaNode3D *node);
AthenaNode3D *athena_node3d_parent(const AthenaNode3D *node);
uint32_t athena_node3d_child_count(const AthenaNode3D *node);
AthenaNode3D *athena_node3d_child(const AthenaNode3D *node,uint32_t index);
/* Local TRS is always current. */
int athena_node3d_local(AthenaNode3D *node,AthenaMatrix4 *out);
/* World data of the last update; ESTALE when the node or an ancestor changed
 * since, or the node is not under a scene root. */
int athena_node3d_world(const AthenaNode3D *node,AthenaMatrix4 *out);
/* World AABB of visible meshes in the subtree. Returns 1 with bounds, 0 for
 * an empty subtree or a negative result. */
int athena_node3d_world_bounds(const AthenaNode3D *node,float minimum[3],float maximum[3]);

typedef struct {
    uint32_t visited_nodes,world_updates,bounds_updates;
} AthenaScene3DUpdateStats;
typedef struct {
    AthenaRender3DStats render;
    /* Subtrees rejected by their world bounds and meshes queued for draw. */
    uint32_t culled_subtrees,queued_objects;
} AthenaScene3DDrawStats;
AthenaScene3D *athena_scene3d_create(void);
void athena_scene3d_retain(AthenaScene3D *scene);
void athena_scene3d_release(AthenaScene3D *scene);
/* Borrowed root owned by the scene; it cannot become another node's child. */
AthenaNode3D *athena_scene3d_root(const AthenaScene3D *scene);
int athena_scene3d_stale(const AthenaScene3D *scene);
/* Integrates node motion for dt seconds (>= 0), descending only into
 * branches with moving nodes, and marks them dirty. Returns the number of
 * moved nodes; EINVAL for a bad dt or an overflowing position or rotation. */
int athena_scene3d_advance(AthenaScene3D *scene,float dt);
/* Recomputes dirty world transforms and subtree bounds; stats may be NULL.
 * Overflowing transforms return EINVAL and leave the scene stale. */
int athena_scene3d_update(AthenaScene3D *scene,AthenaScene3DUpdateStats *stats);
/* Culls subtrees by world bounds, queues visible meshes sorted by pipeline
 * (unlit, diffuse, textured by texture; stable traversal order) and draws them with
 * Render3D. Never advances game state. Stats are reset. ESTALE before update;
 * ENOMEM before any submission; ERENDER stores the Render3D code (-1 invalid
 * transform, -2 packet/program memory, -3 no zbuffer) in *render_error. */
int athena_scene3d_draw(AthenaScene3D *scene,AthenaCamera3D *camera,const AthenaLights *lights,
    AthenaRender3DCull cull,AthenaScene3DDrawStats *stats,int *render_error);
/* Registers a native Loop POST_UPDATE system that advances (dt) and updates the scene and
 * retains it until detach (or Loop system removal). Returns the system id,
 * or a negative Loop/Scene3D code; a scene is attached at most once (EINVAL).
 * owner tags the attachment so a runtime can detach everything it attached. */
int athena_scene3d_attach_loop(AthenaScene3D *scene,int priority,void *owner);
/* Returns 1 when it was attached. The Loop may release the scene meanwhile. */
int athena_scene3d_detach_loop(AthenaScene3D *scene);
void athena_scene3d_detach_owner(void *owner);
int athena_scene3d_loop_system(const AthenaScene3D *scene);
#endif
