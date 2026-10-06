#ifndef ATHENA_MODEL3D_H
#define ATHENA_MODEL3D_H
#include <stdint.h>
#include <athena/quaternion.h>
#include <athena/texture3d.h>
#define ATHENA_MODEL3D_MAX_VERTICES 65532u
typedef enum { ATHENA_MODEL3D_OK=0, ATHENA_MODEL3D_EINVAL=-1,
    ATHENA_MODEL3D_ENOMEM=-2, ATHENA_MODEL3D_EIO=-3, ATHENA_MODEL3D_EFORMAT=-4,
    ATHENA_MODEL3D_EUNSUPPORTED=-5, ATHENA_MODEL3D_EVRAM=-6 } AthenaModel3DResult;
typedef struct AthenaMesh3D AthenaMesh3D;
typedef struct AthenaInstance3D AthenaInstance3D;
typedef enum { ATHENA_MATERIAL3D_UNLIT=0, ATHENA_MATERIAL3D_DIFFUSE=1 } AthenaMaterial3DShading;
/* Immutable descriptor copied into the mesh. Opaque linear RGBA in [0,1];
 * base_color multiplies vertex colors once at creation. Mesh retains texture. */
typedef struct { AthenaMaterial3DShading shading; float base_color[4]; AthenaTexture3D *texture; } AthenaMaterial3D;
void athena_material3d_default(AthenaMaterial3D *material);
int athena_material3d_validate(const AthenaMaterial3D *material);
/* Borrowed inputs are copied; positions xyz, colors optional normalized rgba,
 * optional nonzero xyz normals and uint32 triangle-list indices. Material is
 * also copied. UVs are optional [0,1] pairs with top-left origin; required
 * when the material has a texture. No borrowed JS buffers or mutable views. */
typedef struct {
    const float *positions; uint32_t vertex_count;
    const float *colors; uint32_t color_count;
    const uint32_t *indices; uint32_t index_count;
    const float *normals; uint32_t normal_count;
    const AthenaMaterial3D *material;
    const float *texcoords; uint32_t texcoord_count;
} AthenaGeometry3D;
typedef enum {
    ATHENA_GEOMETRY3D_VALID=0, ATHENA_GEOMETRY3D_VERTEX_COUNT,
    ATHENA_GEOMETRY3D_COLOR_COUNT, ATHENA_GEOMETRY3D_INDEX_COUNT,
    ATHENA_GEOMETRY3D_TRIANGLE_COUNT, ATHENA_GEOMETRY3D_POSITION,
    ATHENA_GEOMETRY3D_COLOR, ATHENA_GEOMETRY3D_INDEX,
    ATHENA_GEOMETRY3D_NORMAL_COUNT, ATHENA_GEOMETRY3D_NORMAL, ATHENA_GEOMETRY3D_MATERIAL,
    ATHENA_GEOMETRY3D_TEXCOORD_COUNT, ATHENA_GEOMETRY3D_TEXCOORD
} AthenaGeometry3DIssue;
/* Optional diagnostic: component offset for positions/colors/normals, index offset for
 * indices. Validation performs no allocations and leaves inputs unchanged. */
AthenaGeometry3DIssue athena_geometry3d_validate(const AthenaGeometry3D *geometry,uint32_t *offset);
/* Compact immutable streams; C callers must retain mesh while reading this view.
 * Streams have four padded vertices, so VIF unpack rounding cannot overread. */
typedef struct { float x,y,z; } AthenaPosition3D;
typedef struct { uint8_t r,g,b,a; } AthenaColor3D;
typedef struct { float u,v; } AthenaTexcoord3D;
typedef struct {
    const AthenaPosition3D *positions;
    const AthenaColor3D *colors;
    uint32_t vertex_count;
    float minimum[3], maximum[3];
    const AthenaPosition3D *normals;
    AthenaMaterial3D material;
    const AthenaTexcoord3D *texcoords;
} AthenaMesh3DView;
int athena_mesh3d_create(const AthenaGeometry3D *geometry,AthenaMesh3D **out);
int athena_mesh3d_load(const char *path,AthenaMesh3D **out);
/* Optional copied override; default loader preserves the existing unlit subset.
 * Supplied normals are normalized; absent diffuse normals are flat-generated.
 * A diffuse degenerate triangle without supplied normals is rejected. */
int athena_mesh3d_load_with_material(const char *path,const AthenaMaterial3D *material,AthenaMesh3D **out);
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
