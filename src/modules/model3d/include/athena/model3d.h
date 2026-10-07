#ifndef ATHENA_MODEL3D_H
#define ATHENA_MODEL3D_H
#include <stdint.h>
#include <athena/quaternion.h>
#include <athena/texture3d.h>
#define ATHENA_MODEL3D_MAX_VERTICES 65532u
/* UVs are finite with |u|,|v| <= UV_LIMIT: repeat addressing beyond it would
 * exceed the GS texel coordinate precision for 512-texel textures. */
#define ATHENA_MODEL3D_UV_LIMIT 16.0f
typedef enum { ATHENA_MODEL3D_OK=0, ATHENA_MODEL3D_EINVAL=-1,
    ATHENA_MODEL3D_ENOMEM=-2, ATHENA_MODEL3D_EIO=-3, ATHENA_MODEL3D_EFORMAT=-4,
    ATHENA_MODEL3D_EUNSUPPORTED=-5, ATHENA_MODEL3D_EVRAM=-6 } AthenaModel3DResult;
typedef struct AthenaMesh3D AthenaMesh3D;
typedef struct AthenaInstance3D AthenaInstance3D;
typedef enum { ATHENA_MATERIAL3D_UNLIT=0, ATHENA_MATERIAL3D_DIFFUSE=1 } AthenaMaterial3DShading;
/* Immutable descriptor copied into the mesh. Opaque linear RGBA in [0,1];
 * base_color multiplies vertex colors once at creation. Mesh retains texture.
 * alpha_mask (glTF alphaMode MASK): pixels whose alpha (texture alpha times
 * vertex alpha) is below alpha_cutoff are discarded by the GS alpha test,
 * depth included; the rest stay opaque. No blending, no sorting needed. */
typedef struct {
    AthenaMaterial3DShading shading; float base_color[4]; AthenaTexture3D *texture;
    int alpha_mask; float alpha_cutoff; /* cutoff in [0,1], 0.5 by default */
} AthenaMaterial3D;
void athena_material3d_default(AthenaMaterial3D *material);
int athena_material3d_validate(const AthenaMaterial3D *material);
/* Borrowed inputs are copied; positions xyz, colors optional normalized rgba,
 * optional nonzero xyz normals and uint32 triangle-list indices. Material is
 * also copied. UVs are optional pairs (see UV_LIMIT) with top-left origin; required
 * when the material has a texture. No borrowed JS buffers or mutable views. */
typedef struct {
    const float *positions; uint32_t vertex_count;
    const float *colors; uint32_t color_count;
    const uint32_t *indices; uint32_t index_count;
    const float *normals; uint32_t normal_count;
    const AthenaMaterial3D *material;
    const float *texcoords; uint32_t texcoord_count;
    /* Optional skin: four joint indices (< ATHENA_MODEL3D_MAX_JOINTS) and four
     * weights per vertex; weights are finite, >= 0, with a positive sum, and
     * are normalized to sum 1. skin_count is vertex_count when present. */
    const uint16_t *joints; const float *weights; uint32_t skin_count;
    /* Optional morph targets: target_count blocks of vertex_count xyz
     * position deltas (target t at t*vertex_count*3), and optionally normal
     * deltas in the same layout (only with normals). A drawn vertex is
     * base + sum(weight[t] * delta[t]); normals are renormalized. */
    const float *target_positions,*target_normals; uint32_t target_count;
} AthenaGeometry3D;
#define ATHENA_MODEL3D_MAX_JOINTS 256u
#define ATHENA_MODEL3D_MAX_TARGETS 8u
typedef enum {
    ATHENA_GEOMETRY3D_VALID=0, ATHENA_GEOMETRY3D_VERTEX_COUNT,
    ATHENA_GEOMETRY3D_COLOR_COUNT, ATHENA_GEOMETRY3D_INDEX_COUNT,
    ATHENA_GEOMETRY3D_TRIANGLE_COUNT, ATHENA_GEOMETRY3D_POSITION,
    ATHENA_GEOMETRY3D_COLOR, ATHENA_GEOMETRY3D_INDEX,
    ATHENA_GEOMETRY3D_NORMAL_COUNT, ATHENA_GEOMETRY3D_NORMAL, ATHENA_GEOMETRY3D_MATERIAL,
    ATHENA_GEOMETRY3D_TEXCOORD_COUNT, ATHENA_GEOMETRY3D_TEXCOORD,
    ATHENA_GEOMETRY3D_SKIN_COUNT, ATHENA_GEOMETRY3D_SKIN,
    ATHENA_GEOMETRY3D_TARGET_COUNT, ATHENA_GEOMETRY3D_TARGET
} AthenaGeometry3DIssue;
/* Optional diagnostic: component offset for positions/colors/normals, index offset for
 * indices. Validation performs no allocations and leaves inputs unchanged. */
AthenaGeometry3DIssue athena_geometry3d_validate(const AthenaGeometry3D *geometry,uint32_t *offset);
/* Compact immutable streams; C callers must retain mesh while reading this view.
 * Streams have four padded vertices, so VIF unpack rounding cannot overread. */
typedef struct { float x,y,z; } AthenaPosition3D;
typedef struct { uint8_t r,g,b,a; } AthenaColor3D;
typedef struct { float u,v; } AthenaTexcoord3D;
#define ATHENA_MESH3D_INDEXED_VERTICES 24u
#define ATHENA_MESH3D_INDEXED_CORNERS 48u
#define ATHENA_MESH3D_INDEXED_INDEX_BYTES 48u
/* Each batch owns a padded, contiguous subset of the immutable streams.
 * Triangles retain their original order; indices are local uint8 for VIF. */
typedef struct { uint32_t first,stream_first,vertex_count,index_count; } AthenaMesh3DChunk;
typedef struct {
    const AthenaPosition3D *positions;
    const AthenaColor3D *colors;
    uint32_t vertex_count;
    float minimum[3], maximum[3];
    const AthenaPosition3D *normals;
    AthenaMaterial3D material;
    const AthenaTexcoord3D *texcoords;
    /* Skinned meshes: four joint indices and four weights (sum 1) per vertex;
     * NULL otherwise. joint_count is the highest joint index + 1. */
    const uint8_t *joints; const float *weights; uint32_t joint_count;
    /* The weights quantized to 0..255 with an exact sum of 255, for VU1. */
    const uint8_t *weights8;
    /* Morph targets use the same storage as positions (target t at
     * t*athena_mesh3d_stream_count(view)); target_normals is NULL when the file had none.
     * target_minimum/maximum bound each target's position deltas. When
     * flat_normals is set the normals were generated per face and a morphed
     * draw generates them again from the morphed triangles. */
    uint32_t target_count;
    const AthenaPosition3D *target_positions,*target_normals;
    const float (*target_minimum)[3],(*target_maximum)[3];
    int flat_normals;
    /* Optional CPU deformation reuse: first stored occurrence of each
     * source index (<= this vertex), or NULL. No sharing of flat normals.
     * The map operates on stream indices, including padded batch vertices. */
    const uint16_t *deform_reuse;
    /* vertex_count remains the triangle-list corner count. For indexed
     * storage, address streams through indices[corner]; morph target stride
     * is stream_vertex_count. NULL indices preserves the original layout.
     * C consumers must recompile and use the accessors below. */
    const uint16_t *indices;
    uint32_t stream_vertex_count,chunk_count;
    const AthenaMesh3DChunk *chunks;
    const uint8_t *chunk_indices;
} AthenaMesh3DView;
static inline uint32_t athena_mesh3d_corner(const AthenaMesh3DView *v,uint32_t corner) {
    return v->indices?v->indices[corner]:corner;
}
static inline uint32_t athena_mesh3d_stream_count(const AthenaMesh3DView *v) {
    return v->indices?v->stream_vertex_count:v->vertex_count;
}
int athena_mesh3d_create(const AthenaGeometry3D *geometry,AthenaMesh3D **out);
int athena_mesh3d_load(const char *path,AthenaMesh3D **out);
/* Optional copied override; default loader preserves the existing unlit subset.
 * Supplied normals are normalized; absent diffuse normals are flat-generated.
 * A diffuse degenerate triangle without supplied normals is rejected. */
int athena_mesh3d_load_with_material(const char *path,const AthenaMaterial3D *material,AthenaMesh3D **out);
const char *athena_model3d_error(int result);
/* Module shutdown: waits for the GS and frees deferred texture releases. */
void athena_model3d_module_shutdown(void);
/* Why the last failing load/create was rejected ("" when unknown), e.g. the
 * exact unsupported glTF feature. Main thread only; loaders clear it on entry
 * and keep the first (most specific) reason. */
const char *athena_model3d_detail(void);
void athena_model3d_clear_detail(void);
void athena_model3d_set_detail(const char *format,...) __attribute__((format(printf,1,2)));
/* Prefixes "context: " to the detail (e.g. which mesh/primitive failed). */
void athena_model3d_detail_context(const char *format,...) __attribute__((format(printf,1,2)));
/* Main thread only. These references own resources, independently of JS handles. */
void athena_mesh3d_retain(AthenaMesh3D *mesh);
void athena_mesh3d_release(AthenaMesh3D *mesh);
void athena_mesh3d_view(const AthenaMesh3D *mesh,AthenaMesh3DView *out);
/* Render3D DMA ownership: the streams of a mesh are written back to RAM at
 * creation and may be read in place by DMA. Returns 1 the first time a
 * generation (owl_flush_generation()) claims the mesh, so the renderer
 * retains it once per generation until that DMA has read it. */
int athena_mesh3d_dma_claim(AthenaMesh3D *mesh,uint64_t generation);
/* Morph targets of the mesh (0 without), without copying a view. */
uint32_t athena_mesh3d_target_count(const AthenaMesh3D *mesh);
AthenaInstance3D *athena_instance3d_create(AthenaMesh3D *mesh);
void athena_instance3d_retain(AthenaInstance3D *instance);
void athena_instance3d_release(AthenaInstance3D *instance);
int athena_instance3d_set_position(AthenaInstance3D *instance,float x,float y,float z);
int athena_instance3d_set_scale(AthenaInstance3D *instance,float x,float y,float z);
int athena_instance3d_set_rotation(AthenaInstance3D *instance,float x,float y,float z,float w);
/* Radians, local XYZ rotations, composed Rz * Ry * Rx. */
int athena_instance3d_set_euler(AthenaInstance3D *instance,float x,float y,float z);
void athena_instance3d_get_position(const AthenaInstance3D *instance,AthenaVector4 *out);
/* Local TRS as set (rotation normalized). */
void athena_instance3d_get_trs(const AthenaInstance3D *instance,float position[3],AthenaQuaternion *rotation,float scale[3]);
const AthenaMatrix4 *athena_instance3d_transform(AthenaInstance3D *instance);
const AthenaMesh3D *athena_instance3d_mesh(const AthenaInstance3D *instance);
#endif
