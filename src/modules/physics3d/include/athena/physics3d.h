#ifndef ATHENA_PHYSICS3D_H
#define ATHENA_PHYSICS3D_H
#include <stdint.h>
#include <athena/collision3d.h>
/* Rigid bodies in C: spheres, boxes and capsules, sequential impulses with friction and
 * restitution, sleeping, against each other and against the static triangles
 * of a Collision3D world (the level). Float only, fixed 1/60 s substeps. */
typedef struct AthenaPhysics3DWorld AthenaPhysics3DWorld;
typedef struct AthenaBody3D AthenaBody3D;
#define ATHENA_PHYSICS3D_EINVAL (-1)
#define ATHENA_PHYSICS3D_ENOMEM (-2)
#define ATHENA_PHYSICS3D_MAX_BODIES 1024u
/* Default Loop priority: after the game's update and Animation3D (-100),
 * before Collision3D characters (-50) and Scene3D (0). */
#define ATHENA_PHYSICS3D_LOOP_PRIORITY (-60)
typedef enum { ATHENA_BODY3D_DYNAMIC=0, ATHENA_BODY3D_KINEMATIC=1, ATHENA_BODY3D_STATIC=2 } AthenaBody3DType;
typedef enum { ATHENA_SHAPE3D_SPHERE=0, ATHENA_SHAPE3D_BOX=1, ATHENA_SHAPE3D_CAPSULE=2 } AthenaShape3DType;
typedef struct {
    AthenaBody3DType type; AthenaShape3DType shape;
    float radius;        /* spheres and capsules */
    float half_height;   /* capsules: half the segment along local y (caps excluded) */
    float half[3];       /* boxes: half extents */
    float mass;          /* dynamic bodies, > 0 */
    float position[3];
    float rotation[4];   /* x, y, z, w; normalized */
    float velocity[3],angular_velocity[3];
    float friction,restitution,linear_damping,angular_damping;
    float rolling_friction; /* spheres: rolling resistance, times the radius */
    uint32_t layer,mask; /* bodies collide when each mask has the other's layer */
} AthenaBody3DDesc;
void athena_body3d_desc_default(AthenaBody3DDesc *desc);

/* statics: the level (retained; NULL for none). */
AthenaPhysics3DWorld *athena_physics3d_world_create(AthenaCollision3DWorld *statics);
void athena_physics3d_world_retain(AthenaPhysics3DWorld *world);
void athena_physics3d_world_release(AthenaPhysics3DWorld *world);
int athena_physics3d_set_gravity(AthenaPhysics3DWorld *world,float x,float y,float z);
/* Layers of the static triangles that bodies collide with. Default all. */
void athena_physics3d_set_static_mask(AthenaPhysics3DWorld *world,uint32_t mask);
/* Velocity iterations per substep (1..64, default 8). */
int athena_physics3d_set_iterations(AthenaPhysics3DWorld *world,int iterations);
/* Advances by dt in substeps of 1/60 s (at most 4 per call; the rest is
 * carried to the next call), then moves the bound nodes. */
int athena_physics3d_step(AthenaPhysics3DWorld *world,float dt);
uint32_t athena_physics3d_body_count(const AthenaPhysics3DWorld *world);
/* Contacts solved in the last substep. */
uint32_t athena_physics3d_contact_count(const AthenaPhysics3DWorld *world);

/* Bodies belong to their world until removed; the returned reference is the
 * caller's (release it too). */
AthenaBody3D *athena_body3d_create(AthenaPhysics3DWorld *world,const AthenaBody3DDesc *desc);
void athena_body3d_retain(AthenaBody3D *body);
void athena_body3d_release(AthenaBody3D *body);
/* Takes the body out of its world (idempotent). */
void athena_body3d_remove(AthenaBody3D *body);
int athena_body3d_alive(const AthenaBody3D *body);
int athena_body3d_set_position(AthenaBody3D *body,float x,float y,float z);
int athena_body3d_set_rotation(AthenaBody3D *body,float x,float y,float z,float w);
int athena_body3d_set_velocity(AthenaBody3D *body,float x,float y,float z);
int athena_body3d_set_angular_velocity(AthenaBody3D *body,float x,float y,float z);
void athena_body3d_get_position(const AthenaBody3D *body,float out[3]);
void athena_body3d_get_rotation(const AthenaBody3D *body,float out[4]);
void athena_body3d_get_velocity(const AthenaBody3D *body,float out[3]);
void athena_body3d_get_angular_velocity(const AthenaBody3D *body,float out[3]);
/* Impulse (mass * velocity) at a world point, or at the centre when point
 * is NULL; wakes the body. Dynamic bodies only (others ignore it). */
int athena_body3d_apply_impulse(AthenaBody3D *body,const float impulse[3],const float point[3]);
/* A force for the next step only (cleared after it). */
int athena_body3d_apply_force(AthenaBody3D *body,const float force[3]);
int athena_body3d_sleeping(const AthenaBody3D *body);
void athena_body3d_wake(AthenaBody3D *body);
AthenaBody3DType athena_body3d_type(const AthenaBody3D *body);
/* A node whose position and rotation follow the body after every step
 * (retained; NULL unbinds). */
int athena_body3d_bind(AthenaBody3D *body,AthenaNode3D *node);

/* Joints between two bodies, or a body and the world (b NULL). Points and
 * axes are in world space at creation and stay attached to the bodies. At
 * least one body must be dynamic. Joints are removed with either body. The
 * returned reference is the caller's (the world keeps its own).
 * - ball: the anchor points of both bodies stay together (free rotation);
 * - hinge: a ball that only turns about axis, with optional angle limits
 *   (radians, relative to the creation pose) and a motor;
 * - distance: keeps anchor_a and anchor_b at length (negative: the current
 *   distance); a rope only stops them from moving further apart;
 * - weld: keeps the relative position and rotation of the creation pose. */
typedef struct AthenaJoint3D AthenaJoint3D;
typedef enum { ATHENA_JOINT3D_BALL=0, ATHENA_JOINT3D_HINGE=1, ATHENA_JOINT3D_DISTANCE=2,
    ATHENA_JOINT3D_WELD=3 } AthenaJoint3DType;
#define ATHENA_PHYSICS3D_MAX_JOINTS 1024u
AthenaJoint3D *athena_joint3d_ball(AthenaPhysics3DWorld *world,AthenaBody3D *a,AthenaBody3D *b,const float anchor[3]);
AthenaJoint3D *athena_joint3d_hinge(AthenaPhysics3DWorld *world,AthenaBody3D *a,AthenaBody3D *b,
    const float anchor[3],const float axis[3]);
AthenaJoint3D *athena_joint3d_distance(AthenaPhysics3DWorld *world,AthenaBody3D *a,AthenaBody3D *b,
    const float anchor_a[3],const float anchor_b[3],float length,int rope);
AthenaJoint3D *athena_joint3d_weld(AthenaPhysics3DWorld *world,AthenaBody3D *a,AthenaBody3D *b,const float anchor[3]);
void athena_joint3d_retain(AthenaJoint3D *joint);
void athena_joint3d_release(AthenaJoint3D *joint);
/* Takes the joint out of its world (idempotent). */
void athena_joint3d_remove(AthenaJoint3D *joint);
int athena_joint3d_alive(const AthenaJoint3D *joint);
AthenaJoint3DType athena_joint3d_type(const AthenaJoint3D *joint);
/* Hinges: lower <= upper (radians); the limit is enforced from the next step. */
int athena_joint3d_set_limits(AthenaJoint3D *joint,float lower,float upper);
void athena_joint3d_disable_limits(AthenaJoint3D *joint);
/* Hinges: drives the relative angular speed (rad/s) with at most max_torque
 * (N m); max_torque 0 turns the motor off. */
int athena_joint3d_set_motor(AthenaJoint3D *joint,float speed,float max_torque);
/* Hinges: the angle of a relative to b about the axis, from the last step. */
float athena_joint3d_angle(const AthenaJoint3D *joint);
uint32_t athena_physics3d_joint_count(const AthenaPhysics3DWorld *world);
/* Time spent in the last step() (all its substeps), in microseconds:
 * collision detection, contact and joint preparation (with warm starting),
 * solver iterations, and integration with sleeping. */
typedef struct { float collide,prepare,solve,integrate; uint32_t substeps; } AthenaPhysics3DProfile;
void athena_physics3d_profile(const AthenaPhysics3DWorld *world,AthenaPhysics3DProfile *out);

/* One Loop POST_UPDATE system per world, stepping it with the frame dt. */
int athena_physics3d_attach_loop(AthenaPhysics3DWorld *world,int priority,void *owner);
int athena_physics3d_detach_loop(AthenaPhysics3DWorld *world);
void athena_physics3d_detach_owner(void *owner);
int athena_physics3d_loop_system(const AthenaPhysics3DWorld *world);
#endif
