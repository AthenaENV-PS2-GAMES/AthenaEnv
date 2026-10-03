#ifndef ATHENA_ODE_FACADE_H
#define ATHENA_ODE_FACADE_H

#include <stdbool.h>

#include <ode/ode.h>
#include <render.h>

typedef struct AthenaOdeWorld {
    dWorldID world;
} AthenaOdeWorld;

typedef struct AthenaOdeSpace {
    dSpaceID space;
    dSpaceID parent;
} AthenaOdeSpace;

typedef struct AthenaOdeBody {
    dBodyID body;
    dWorldID parent_world;
} AthenaOdeBody;

void athena_ode_cleanup(void);

AthenaOdeWorld *athena_ode_world_create(void);
void athena_ode_world_destroy(AthenaOdeWorld *world);
void athena_ode_world_set_gravity(AthenaOdeWorld *world, float x, float y, float z);
void athena_ode_world_get_gravity(const AthenaOdeWorld *world, float out[3]);
void athena_ode_world_set_cfm(AthenaOdeWorld *world, float cfm);
void athena_ode_world_set_erp(AthenaOdeWorld *world, float erp);
void athena_ode_world_step(AthenaOdeWorld *world, float step_size);
void athena_ode_world_quick_step(AthenaOdeWorld *world, float step_size);
void athena_ode_world_set_quick_step_iterations(AthenaOdeWorld *world, unsigned int iterations);

AthenaOdeSpace *athena_ode_space_create(AthenaOdeSpace *parent);
void athena_ode_space_destroy(AthenaOdeSpace *space);

AthenaOdeBody *athena_ode_body_create(AthenaOdeWorld *world);
void athena_ode_body_destroy(AthenaOdeBody *body);
void athena_ode_body_set_position(AthenaOdeBody *body, float x, float y, float z);
void athena_ode_body_get_position(const AthenaOdeBody *body, float out[3]);
void athena_ode_body_set_rotation(AthenaOdeBody *body, const float matrix[9]);
void athena_ode_body_get_rotation(const AthenaOdeBody *body, float out[9]);
void athena_ode_body_set_linear_vel(AthenaOdeBody *body, float x, float y, float z);
void athena_ode_body_get_linear_vel(const AthenaOdeBody *body, float out[3]);
void athena_ode_body_set_angular_vel(AthenaOdeBody *body, float x, float y, float z);
void athena_ode_body_get_angular_vel(const AthenaOdeBody *body, float out[3]);
void athena_ode_body_set_mass(AthenaOdeBody *body, float mass);
void athena_ode_body_set_mass_box(AthenaOdeBody *body, float mass, float lx, float ly, float lz);
void athena_ode_body_set_mass_sphere(AthenaOdeBody *body, float mass, float radius);
void athena_ode_body_add_force(AthenaOdeBody *body, float fx, float fy, float fz);
void athena_ode_body_add_torque(AthenaOdeBody *body, float fx, float fy, float fz);
void athena_ode_body_enable(AthenaOdeBody *body);
void athena_ode_body_disable(AthenaOdeBody *body);
bool athena_ode_body_is_enabled(const AthenaOdeBody *body);

void athena_ode_update_geom_pos_rot(athena_object_data *obj);
void athena_ode_update_body_pos_rot(athena_object_data *obj);

#endif /* ATHENA_ODE_FACADE_H */
