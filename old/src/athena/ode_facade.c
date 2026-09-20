#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <athena/ode_facade.h>

void updateGeomPosRot(athena_object_data *obj);
void updateBodyPosRot(athena_object_data *obj);

static void rot_vector_to_matrix(VECTOR vec, dMatrix3 result)
{
    float angle = sqrtf(vec[0] * vec[0] + vec[1] * vec[1] + vec[2] * vec[2]);
    float nx, ny, nz;
    float cos_angle, sin_angle, one_minus_cos;

    if (angle < 0.001f) {
        memset(result, 0, sizeof(dMatrix3));
        result[0] = result[5] = result[10] = 1.0f;
        return;
    }

    nx = vec[0] / angle;
    ny = vec[1] / angle;
    nz = vec[2] / angle;

    cos_angle = cosf(angle);
    sin_angle = sinf(angle);
    one_minus_cos = 1.0f - cos_angle;

    result[0] = cos_angle + nx * nx * one_minus_cos;
    result[1] = nx * ny * one_minus_cos - nz * sin_angle;
    result[2] = nx * nz * one_minus_cos + ny * sin_angle;

    result[4] = ny * nx * one_minus_cos + nz * sin_angle;
    result[5] = cos_angle + ny * ny * one_minus_cos;
    result[6] = ny * nz * one_minus_cos - nx * sin_angle;

    result[8] = nz * nx * one_minus_cos - ny * sin_angle;
    result[9] = nz * ny * one_minus_cos + nx * sin_angle;
    result[10] = cos_angle + nz * nz * one_minus_cos;
}

void athena_ode_cleanup(void)
{
    dCloseODE();
}

AthenaOdeWorld *athena_ode_world_create(void)
{
    AthenaOdeWorld *world = calloc(1, sizeof(*world));
    if (!world)
        return NULL;

    world->world = dWorldCreate();
    if (!world->world) {
        free(world);
        return NULL;
    }

    return world;
}

void athena_ode_world_destroy(AthenaOdeWorld *world)
{
    if (!world)
        return;

    if (world->world) {
        dWorldDestroy(world->world);
        world->world = NULL;
    }

    free(world);
}

void athena_ode_world_set_gravity(AthenaOdeWorld *world, float x, float y, float z)
{
    if (world && world->world)
        dWorldSetGravity(world->world, x, y, z);
}

void athena_ode_world_get_gravity(const AthenaOdeWorld *world, float out[3])
{
    dVector3 gravity;

    if (!world || !world->world || !out)
        return;

    dWorldGetGravity(world->world, gravity);
    out[0] = gravity[0];
    out[1] = gravity[1];
    out[2] = gravity[2];
}

void athena_ode_world_set_cfm(AthenaOdeWorld *world, float cfm)
{
    if (world && world->world)
        dWorldSetCFM(world->world, cfm);
}

void athena_ode_world_set_erp(AthenaOdeWorld *world, float erp)
{
    if (world && world->world)
        dWorldSetERP(world->world, erp);
}

void athena_ode_world_step(AthenaOdeWorld *world, float step_size)
{
    if (world && world->world)
        dWorldStep(world->world, step_size);
}

void athena_ode_world_quick_step(AthenaOdeWorld *world, float step_size)
{
    if (world && world->world)
        dWorldQuickStep(world->world, step_size);
}

void athena_ode_world_set_quick_step_iterations(AthenaOdeWorld *world, unsigned int iterations)
{
    if (world && world->world)
        dWorldSetQuickStepNumIterations(world->world, iterations);
}

AthenaOdeSpace *athena_ode_space_create(AthenaOdeSpace *parent)
{
    AthenaOdeSpace *space = calloc(1, sizeof(*space));
    if (!space)
        return NULL;

    space->parent = parent ? parent->space : NULL;
    space->space = dHashSpaceCreate(space->parent);
    if (!space->space) {
        free(space);
        return NULL;
    }

    return space;
}

void athena_ode_space_destroy(AthenaOdeSpace *space)
{
    if (!space)
        return;

    if (space->space) {
        dSpaceDestroy(space->space);
        space->space = NULL;
    }

    free(space);
}

AthenaOdeBody *athena_ode_body_create(AthenaOdeWorld *world)
{
    AthenaOdeBody *body;

    if (!world || !world->world)
        return NULL;

    body = calloc(1, sizeof(*body));
    if (!body)
        return NULL;

    body->parent_world = world->world;
    body->body = dBodyCreate(world->world);
    if (!body->body) {
        free(body);
        return NULL;
    }

    return body;
}

void athena_ode_body_destroy(AthenaOdeBody *body)
{
    if (!body)
        return;

    if (body->body) {
        dBodyDestroy(body->body);
        body->body = NULL;
    }

    free(body);
}

void athena_ode_body_set_position(AthenaOdeBody *body, float x, float y, float z)
{
    if (body && body->body)
        dBodySetPosition(body->body, x, y, z);
}

void athena_ode_body_get_position(const AthenaOdeBody *body, float out[3])
{
    const dReal *pos;

    if (!body || !body->body || !out)
        return;

    pos = dBodyGetPosition(body->body);
    out[0] = pos[0];
    out[1] = pos[1];
    out[2] = pos[2];
}

void athena_ode_body_set_rotation(AthenaOdeBody *body, const float matrix[9])
{
    dMatrix3 r;
    int i;

    if (!body || !body->body || !matrix)
        return;

    for (i = 0; i < 9; i++)
        r[i] = matrix[i];

    dBodySetRotation(body->body, r);
}

void athena_ode_body_get_rotation(const AthenaOdeBody *body, float out[9])
{
    const dReal *rot;
    int i;

    if (!body || !body->body || !out)
        return;

    rot = dBodyGetRotation(body->body);
    for (i = 0; i < 9; i++)
        out[i] = rot[i];
}

void athena_ode_body_set_linear_vel(AthenaOdeBody *body, float x, float y, float z)
{
    if (body && body->body)
        dBodySetLinearVel(body->body, x, y, z);
}

void athena_ode_body_get_linear_vel(const AthenaOdeBody *body, float out[3])
{
    const dReal *vel;

    if (!body || !body->body || !out)
        return;

    vel = dBodyGetLinearVel(body->body);
    out[0] = vel[0];
    out[1] = vel[1];
    out[2] = vel[2];
}

void athena_ode_body_set_angular_vel(AthenaOdeBody *body, float x, float y, float z)
{
    if (body && body->body)
        dBodySetAngularVel(body->body, x, y, z);
}

void athena_ode_body_get_angular_vel(const AthenaOdeBody *body, float out[3])
{
    const dReal *vel;

    if (!body || !body->body || !out)
        return;

    vel = dBodyGetAngularVel(body->body);
    out[0] = vel[0];
    out[1] = vel[1];
    out[2] = vel[2];
}

void athena_ode_body_set_mass(AthenaOdeBody *body, float mass)
{
    dMass m;

    if (!body || !body->body)
        return;

    dMassSetSphere(&m, 1.0, 1.0);
    dMassAdjust(&m, mass);
    dBodySetMass(body->body, &m);
}

void athena_ode_body_set_mass_box(AthenaOdeBody *body, float mass, float lx, float ly, float lz)
{
    dMass m;

    if (!body || !body->body)
        return;

    dMassSetBox(&m, 1.0, lx, ly, lz);
    dMassAdjust(&m, mass);
    dBodySetMass(body->body, &m);
}

void athena_ode_body_set_mass_sphere(AthenaOdeBody *body, float mass, float radius)
{
    dMass m;

    if (!body || !body->body)
        return;

    dMassSetSphere(&m, 1.0, radius);
    dMassAdjust(&m, mass);
    dBodySetMass(body->body, &m);
}

void athena_ode_body_add_force(AthenaOdeBody *body, float fx, float fy, float fz)
{
    if (body && body->body)
        dBodyAddForce(body->body, fx, fy, fz);
}

void athena_ode_body_add_torque(AthenaOdeBody *body, float fx, float fy, float fz)
{
    if (body && body->body)
        dBodyAddTorque(body->body, fx, fy, fz);
}

void athena_ode_body_enable(AthenaOdeBody *body)
{
    if (body && body->body)
        dBodyEnable(body->body);
}

void athena_ode_body_disable(AthenaOdeBody *body)
{
    if (body && body->body)
        dBodyDisable(body->body);
}

bool athena_ode_body_is_enabled(const AthenaOdeBody *body)
{
    return body && body->body && dBodyIsEnabled(body->body);
}

void athena_ode_update_geom_pos_rot(athena_object_data *obj)
{
    dGeomID geom;
    dMatrix3 r;

    if (!obj || !obj->collision)
        return;

    geom = (dGeomID)obj->collision;
    dGeomSetPosition(geom, obj->position[0], obj->position[1], obj->position[2]);
    rot_vector_to_matrix(obj->rotation, r);
    dGeomSetRotation(geom, r);
}

void athena_ode_update_body_pos_rot(athena_object_data *obj)
{
    dBodyID body;
    const dReal *pos;
    const dReal *rot;
    dQuaternion q;
    float angle;
    float sin_half;

    if (!obj || !obj->physics)
        return;

    body = (dBodyID)obj->physics;
    pos = dBodyGetPosition(body);
    rot = dBodyGetRotation(body);

    obj->position[0] = pos[0];
    obj->position[1] = pos[1];
    obj->position[2] = pos[2];

    dRtoQ(rot, q);

    angle = 2.0f * acosf(q[0]);
    if (angle > 0.001f) {
        sin_half = sinf(angle * 0.5f);
        obj->rotation[0] = (q[1] / sin_half) * angle;
        obj->rotation[1] = (q[2] / sin_half) * angle;
        obj->rotation[2] = (q[3] / sin_half) * angle;
    } else {
        obj->rotation[0] = 0.0f;
        obj->rotation[1] = 0.0f;
        obj->rotation[2] = 0.0f;
    }

    update_object_space(obj);
}

void updateGeomPosRot(athena_object_data *obj)
{
    athena_ode_update_geom_pos_rot(obj);
}

void updateBodyPosRot(athena_object_data *obj)
{
    athena_ode_update_body_pos_rot(obj);
}
