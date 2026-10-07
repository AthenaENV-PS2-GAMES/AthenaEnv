#ifndef ATHENA_PHYSICS3D_INTERNAL_H
#define ATHENA_PHYSICS3D_INTERNAL_H
/* Private: shared by physics3d.c (bodies, contacts, stepping) and joint3d.c. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <athena/float_bits.h>
#include <athena/physics3d.h>
#define SUBSTEP (1.0f/60)
#define MAX_SUBSTEPS 4
#define BAUMGARTE 0.2f
#define SLOP 0.005f
#define RESTITUTION_THRESHOLD 1.0f
#define SLEEP_LINEAR 0.0025f   /* squared speeds */
#define SLEEP_ANGULAR 0.0025f
#define SLEEP_TIME 0.5f
#define MAX_STATIC_CONTACTS 8
/* Speculative margin: features this close (not yet touching) still make a
 * contact, with a negative depth that only stops the approach. Keeps the
 * contact set steady from step to step. */
#define MARGIN 0.02f
/* Warm starting: a new contact reuses the impulses of the previous step's
 * contact of the same pair within this distance. */
#define MATCH_DISTANCE_SQ (0.05f*0.05f)

struct AthenaBody3D {
    uint64_t refs;
    AthenaPhysics3DWorld *world;   /* NULL once removed */
    AthenaBody3DType type; AthenaShape3DType shape;
    float radius,half[3];
    float p[3],q[4];
    /* Quadword aligned (the w lanes unused): the VU0 contact solver loads
     * and stores them whole. */
    float v[4] __attribute__((aligned(16)));
    float w[4] __attribute__((aligned(16)));
    float inverse_mass,inverse_inertia[3]; /* local principal axes */
    float rotation[9];                     /* columns: the local axes in world */
    float world_inverse_inertia[9];        /* row-major */
    float force[3];
    float friction,restitution,linear_damping,angular_damping,rolling_friction;
    uint32_t layer,mask;
    float sleep_time; int sleeping;
    uint32_t island;                       /* union-find parent (body index) */
    uint32_t slot;                         /* index in the world's bodies */
    uint32_t joints;                       /* joints attached: joined pairs do not collide */
    float lo[3],hi[3];
    AthenaNode3D *node;
};
/* Per direction (normal, two tangents): r x d and I^-1 (r x d) of each
 * body, precomputed so that solver iterations are dot products only. */
typedef struct { float ra[3],rb[3],ia[3],ib[3],mass; } Row;
typedef struct {
    AthenaBody3D *a,*b;            /* b NULL: the level */
    float n[3],ra[3],rb[3],depth;
    float t1[3],t2[3];
    float ma,mb;                   /* inverse masses seen by the solver */
    Row rows[3];                   /* normal, t1, t2 */
    float bias,friction,rolling,rolling_used;
    float pn,pt[2];
} Contact;
/* The contact rows packed for the VU0 solver, rebuilt every substep from
 * the contacts: directions n, t1, t2; per row r x d and I^-1 (r x d) of a
 * and b; k = (normal, t1, t2 effective masses, bias); impulses = (pn, pt1,
 * pt2, friction); inverse = (ma, mb). va..wb point at the bodies'
 * velocities (b: a zero quadword when there is none). */
typedef struct {
    float d[3][4] __attribute__((aligned(16)));
    float ra[3][4],rb[3][4],ia[3][4],ib[3][4];
    float k[4],impulses[4],inverse[4];
    float *va,*wa,*vb,*wb;
    Contact *source;
} __attribute__((aligned(16))) SolverContact;
typedef struct Attachment {
    AthenaPhysics3DWorld *world; void *owner; int id;
    struct Attachment *next;
} Attachment;
struct AthenaPhysics3DWorld {
    uint64_t refs;
    AthenaCollision3DWorld *statics; uint32_t static_mask;
    float gravity[3];
    AthenaBody3D **bodies; uint32_t body_count,body_capacity;
    uint32_t *order;
    Contact *contacts; uint32_t contact_count,contact_capacity,solved_contacts;
    /* The previous substep's contacts and a pair -> first contact table. */
    Contact *previous; uint32_t previous_count,previous_capacity;
    uint32_t *table; uint32_t table_size;
    float accumulator; int iterations;
    Attachment *attachment;
    AthenaJoint3D **joints; uint32_t joint_count,joint_capacity;
    AthenaPhysics3DProfile profile,running;
    SolverContact *solver; uint32_t solver_capacity;
};

static inline float dot3(const float a[3],const float b[3]) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static inline void cross3(float o[3],const float a[3],const float b[3]) {
    float x=a[1]*b[2]-a[2]*b[1],y=a[2]*b[0]-a[0]*b[2],z=a[0]*b[1]-a[1]*b[0]; o[0]=x; o[1]=y; o[2]=z;
}
static inline void sub3(float o[3],const float a[3],const float b[3]) { o[0]=a[0]-b[0]; o[1]=a[1]-b[1]; o[2]=a[2]-b[2]; }
static inline void madd3(float o[3],const float a[3],float s,const float b[3]) { for(int i=0;i<3;i++) o[i]=a[i]+s*b[i]; }
static inline float length3(const float a[3]) { return sqrtf(dot3(a,a)); }
static inline int finite_n(const float *v,int n) { for(int i=0;i<n;i++) if(!athena_float_isfinite(v[i])) return 0; return 1; }
static inline void mul33(float o[3],const float m[9],const float v[3]) {
    float x=m[0]*v[0]+m[1]*v[1]+m[2]*v[2],y=m[3]*v[0]+m[4]*v[1]+m[5]*v[2],z=m[6]*v[0]+m[7]*v[1]+m[8]*v[2];
    o[0]=x; o[1]=y; o[2]=z;
}
static inline const float *axis(const AthenaBody3D *b,int i) { return &b->rotation[i*3]; }


/* Joints: anchors and axes in each body's local frame (b NULL: the world,
 * where local means world). Solver state is rebuilt every substep; the
 * accumulated impulses carry over for warm starting. */
struct AthenaJoint3D {
    uint64_t refs;
    AthenaPhysics3DWorld *world;   /* NULL once removed */
    AthenaJoint3DType type;
    AthenaBody3D *a,*b;
    float local_a[3],local_b[3];   /* anchors (distance: one per body) */
    float axis_a[3],axis_b[3];     /* hinge axis */
    float ref_a[3],ref_b[3];       /* hinge zero angle, perpendicular to the axis */
    float rest[4];                 /* weld: initial qa * conj(qb) */
    float length; int rope;
    int limits; float lower,upper;
    int motor; float motor_speed,motor_max;
    uint32_t slot;
    /* Solver state. */
    float ra[3],rb[3],ma,mb,k_inverse[9],bias[3],impulse[3];
    float angular_axes[3][3],angular_mass[3],angular_bias[3],angular_impulse[3]; int angular_rows;
    float hinge_axis[3],axis_mass,angle,limit_bias,limit_impulse,motor_impulse; int limit_state;
    float n[3],distance_mass,distance_bias,distance_impulse;
};
/* joint3d.c */
void athena_joint3d_prepare_all(AthenaPhysics3DWorld *world,float h);
void athena_joint3d_solve_all(AthenaPhysics3DWorld *world);
void athena_joint3d_body_removed(AthenaPhysics3DWorld *world,AthenaBody3D *body);
void athena_joint3d_release_all(AthenaPhysics3DWorld *world);
int athena_joint3d_joined(const AthenaPhysics3DWorld *world,const AthenaBody3D *a,const AthenaBody3D *b);
#endif
