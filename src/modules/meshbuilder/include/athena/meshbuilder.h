#ifndef ATHENA_MESHBUILDER_H
#define ATHENA_MESHBUILDER_H
#include <stdint.h>
#include <athena/matrix4.h>
#include <athena/model3d.h>
/* Geometry accumulated in C and turned into Model3D meshes.
 *
 * Vertices carry position, normal, color and UV; triangles are indices,
 * counter-clockwise seen from the front (the side Render3D.CULL_BACK keeps).
 * The current color, transform (applied to positions and normals as they are
 * added) and UV rectangle (each shape's 0..1 UVs are mapped into it, e.g. a
 * texture atlas tile) are state, like a pen. build() splits the triangles
 * into as many meshes as the Model3D limits need (65532 indices and
 * vertices each) and welds vertices that are equal in everything the mesh
 * keeps (position and color, plus normal for DIFFUSE and UV when textured).
 * Main thread only. */
typedef struct AthenaMeshBuilder AthenaMeshBuilder;
#define ATHENA_MESHBUILDER_MAX_SEGMENTS 256u
#define ATHENA_MESHBUILDER_ENOMEM (-2)
#define ATHENA_MESHBUILDER_EINVAL (-1)
AthenaMeshBuilder *athena_meshbuilder_create(void);
void athena_meshbuilder_destroy(AthenaMeshBuilder *b);
/* Drops the geometry; the state (color, transform, UV rect) is kept. */
void athena_meshbuilder_clear(AthenaMeshBuilder *b);
/* Linear RGBA in [0,1]. */
int athena_meshbuilder_set_color(AthenaMeshBuilder *b,const float rgba[4]);
/* NULL restores the identity. The normal matrix is its inverse transpose. */
int athena_meshbuilder_set_transform(AthenaMeshBuilder *b,const AthenaMatrix4 *m);
int athena_meshbuilder_set_uv_rect(AthenaMeshBuilder *b,float u0,float v0,float u1,float v1);
/* One vertex with the current state; normal may be NULL (then 0,1,0).
 * Returns its index or a negative error (over 2^31 vertices or memory). */
int64_t athena_meshbuilder_vertex(AthenaMeshBuilder *b,const float p[3],const float n[3],float u,float v);
/* A vertex with an explicit color (current color ignored), for generators. */
int64_t athena_meshbuilder_vertex_color(AthenaMeshBuilder *b,const float p[3],const float n[3],const float rgba[4],
    float u,float v);
int athena_meshbuilder_triangle(AthenaMeshBuilder *b,uint32_t i0,uint32_t i1,uint32_t i2);
/* For trusted generators (e.g. the voxel mesher): reserve() room for
 * vertices and indices, then add them without validation. Values must be
 * finite, colors in [0,1], indices below the vertex count; the transform and
 * UV rectangle still apply. reserve() returns 0 or ENOMEM. */
int athena_meshbuilder_reserve(AthenaMeshBuilder *b,uint32_t vertices,uint32_t indices);
uint32_t athena_meshbuilder_put_unchecked(AthenaMeshBuilder *b,const float p[3],const float n[3],const float rgba[4],float u,float v);
void athena_meshbuilder_triangle_unchecked(AthenaMeshBuilder *b,uint32_t i0,uint32_t i1,uint32_t i2);
/* Shapes; return 0 or a negative error (nothing is left half added). */
int athena_meshbuilder_quad(AthenaMeshBuilder *b,const float p0[3],const float p1[3],const float p2[3],const float p3[3]);
int athena_meshbuilder_box(AthenaMeshBuilder *b,const float minimum[3],const float maximum[3]);
int athena_meshbuilder_sphere(AthenaMeshBuilder *b,const float center[3],float radius,uint32_t segments,uint32_t rings);
/* Along +Y from base; caps close both ends. */
int athena_meshbuilder_cylinder(AthenaMeshBuilder *b,const float base[3],float radius,float height,uint32_t segments,int caps);
/* XZ plane facing +Y, centred at center, divided into nx * nz cells. */
int athena_meshbuilder_plane(AthenaMeshBuilder *b,const float center[3],float width,float depth,uint32_t nx,uint32_t nz);
/* Terrain from w * h heights (row-major, x then z): cell spacing, heights
 * multiplied by scale, origin at the first sample; normals from central
 * differences. low/high (both or neither) color the vertices by height
 * between the minimum and maximum sample instead of the current color. */
int athena_meshbuilder_heightmap(AthenaMeshBuilder *b,const float *heights,uint32_t w,uint32_t h,float cell,float scale,
    const float origin[3],const float low[4],const float high[4]);
/* Static batching: the mesh's triangles under the current transform (and
 * `model` inside it when not NULL, e.g. an instance transform), with its
 * colors (times base color) and UVs when it has them. Skin and morph data
 * are not copied. */
int athena_meshbuilder_merge(AthenaMeshBuilder *b,const AthenaMesh3D *mesh,const AthenaMatrix4 *model);
uint32_t athena_meshbuilder_vertex_count(const AthenaMeshBuilder *b);
uint32_t athena_meshbuilder_triangle_count(const AthenaMeshBuilder *b);
/* Meshes for the geometry with material (NULL: default UNLIT). Normals are
 * included for DIFFUSE materials, UVs when the material has a texture. Up to
 * max meshes are stored in out (the caller owns them); returns how many the
 * geometry needs (call again with room when larger than max), 0 when empty,
 * or a negative error (ENOMEM, EINVAL with athena_model3d_detail()). */
int athena_meshbuilder_build(const AthenaMeshBuilder *b,const AthenaMaterial3D *material,AthenaMesh3D **out,uint32_t max);
/* How many meshes build() makes. */
uint32_t athena_meshbuilder_part_count(const AthenaMeshBuilder *b);
#endif
