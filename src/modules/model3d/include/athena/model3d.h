#ifndef ATHENA_MODEL3D_H
#define ATHENA_MODEL3D_H
#include <stdint.h>
#include <athena/quaternion.h>
#define ATHENA_MODEL3D_MAX_VERTICES 65532u
typedef enum { ATHENA_MODEL3D_OK=0, ATHENA_MODEL3D_EINVAL=-1,
    ATHENA_MODEL3D_ENOMEM=-2, ATHENA_MODEL3D_EIO=-3, ATHENA_MODEL3D_EFORMAT=-4,
    ATHENA_MODEL3D_EUNSUPPORTED=-5 } AthenaModel3DResult;
typedef struct AthenaMesh3D AthenaMesh3D;
typedef struct AthenaInstance3D AthenaInstance3D;
/* Borrowed inputs are copied; positions are xyz, colors optional normalized rgba.
 * indices optional, uint32 triangle list. No borrowed JS buffers or mutable views. */
typedef struct {
    const float *positions; uint32_t vertex_count;
    const float *colors; uint32_t color_count;
    const uint32_t *indices; uint32_t index_count;
} AthenaGeometry3D;
typedef enum {
    ATHENA_GEOMETRY3D_VALID=0, ATHENA_GEOMETRY3D_VERTEX_COUNT,
    ATHENA_GEOMETRY3D_COLOR_COUNT, ATHENA_GEOMETRY3D_INDEX_COUNT,
    ATHENA_GEOMETRY3D_TRIANGLE_COUNT, ATHENA_GEOMETRY3D_POSITION,
    ATHENA_GEOMETRY3D_COLOR, ATHENA_GEOMETRY3D_INDEX
} AthenaGeometry3DIssue;
/* Optional diagnostic: component offset for positions/colors, index offset for
 * indices. Validation performs no allocations and leaves inputs unchanged. */
AthenaGeometry3DIssue athena_geometry3d_validate(const AthenaGeometry3D *geometry,uint32_t *offset);
/* Compact immutable streams; C callers must retain mesh while reading this view.
 * Streams have four padded vertices, so VIF unpack rounding cannot overread. */
typedef struct { float x,y,z; } AthenaPosition3D;
typedef struct { uint8_t r,g,b,a; } AthenaColor3D;
typedef struct {
    const AthenaPosition3D *positions;
    const AthenaColor3D *colors;
    uint32_t vertex_count;
    float minimum[3], maximum[3];
} AthenaMesh3DView;
int athena_mesh3d_create(const AthenaGeometry3D *geometry,AthenaMesh3D **out);
int athena_mesh3d_load(const char *path,AthenaMesh3D **out);
const char *athena_model3d_error(int result);
/* Main thread only. These references own resources, independently of JS handles. */
void athena_mesh3d_retain(AthenaMesh3D *mesh);
void athena_mesh3d_release(AthenaMesh3D *mesh);
void athena_mesh3d_view(const AthenaMesh3D *mesh,AthenaMesh3DView *out);
AthenaInstance3D *athena_instance3d_create(AthenaMesh3D *mesh);
void athena_instance3d_retain(AthenaInstance3D *instance);
void athena_instance3d_release(AthenaInstance3D *instance);
int athena_instance3d_set_position(AthenaInstance3D *instance,float x,float y,float z);
int athena_instance3d_set_scale(AthenaInstance3D *instance,float x,float y,float z);
int athena_instance3d_set_rotation(AthenaInstance3D *instance,float x,float y,float z,float w);
/* Radians, local XYZ rotations, composed Rz * Ry * Rx. */
int athena_instance3d_set_euler(AthenaInstance3D *instance,float x,float y,float z);
void athena_instance3d_get_position(const AthenaInstance3D *instance,AthenaVector4 *out);
const AthenaMatrix4 *athena_instance3d_transform(AthenaInstance3D *instance);
const AthenaMesh3D *athena_instance3d_mesh(const AthenaInstance3D *instance);
#endif
