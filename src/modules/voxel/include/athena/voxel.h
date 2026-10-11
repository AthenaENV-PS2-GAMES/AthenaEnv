#ifndef ATHENA_VOXEL_H
#define ATHENA_VOXEL_H
#include <stdint.h>
#include <athena/model3d.h>
#include <athena/camera3d.h>
#include <athena/lights.h>
#include <athena/render3d.h>
/* A block world in C: one byte per block (0 is air), chunked meshes built
 * with athena/meshbuilder, terrain generation, DDA raycast, AABB collision
 * and per-chunk culled drawing. Coordinates are block units: block (x,y,z)
 * fills [x,x+1) x [y,y+1) x [z,z+1). Main thread only. */
#define ATHENA_VOXEL_TYPES 256u
#define ATHENA_VOXEL_MAX_BLOCKS (16u*1024u*1024u)
typedef struct AthenaVoxelWorld AthenaVoxelWorld;
typedef enum { ATHENA_VOXEL_OK=0, ATHENA_VOXEL_EINVAL=-1, ATHENA_VOXEL_ENOMEM=-2, ATHENA_VOXEL_ERENDER=-3 } AthenaVoxelResult;
/* Faces: 0 top (+Y), 1 bottom (-Y), 2 sides (+-X, +-Z). */
typedef struct {
    uint8_t solid;            /* blocks movement and rays, hides neighbours' faces */
    uint8_t visible;          /* drawn (a solid invisible block is a barrier) */
    float color[3][4];        /* per face group, linear RGBA in [0,1] */
    int16_t tile[3];          /* atlas tile per face group, -1 for none */
} AthenaVoxelMaterial;
typedef struct {
    uint32_t size[3];         /* world blocks in x, y, z */
    uint32_t chunk;           /* chunk edge in blocks: 4 to 32 (default 16) */
} AthenaVoxelDesc;
typedef enum { ATHENA_VOXEL_MESH_NAIVE=0, ATHENA_VOXEL_MESH_GREEDY=1 } AthenaVoxelMeshing;
typedef struct {
    AthenaVoxelMeshing meshing;   /* GREEDY merges equal faces; colors only, no AO */
    float ambient_occlusion;      /* 0 off .. 1 strong (NAIVE only), default 0.5 */
    int baked_light;              /* shade faces by direction (UNLIT), default 1 */
    AthenaMaterial3DShading shading; /* DIFFUSE adds normals for Render3D lights */
    AthenaTexture3D *atlas;       /* retained; tiles of tile_size pixels */
    uint32_t tile_size;
} AthenaVoxelStyle;

AthenaVoxelWorld *athena_voxel_create(const AthenaVoxelDesc *desc);
void athena_voxel_retain(AthenaVoxelWorld *w);
void athena_voxel_release(AthenaVoxelWorld *w);
void athena_voxel_size(const AthenaVoxelWorld *w,uint32_t size[3],uint32_t *chunk);
int athena_voxel_set_material(AthenaVoxelWorld *w,uint32_t type,const AthenaVoxelMaterial *m);
/* Copies the style; the atlas is retained. Marks every chunk dirty. */
int athena_voxel_set_style(AthenaVoxelWorld *w,const AthenaVoxelStyle *style);
/* Out of the world: get() returns 0 (air), set() returns EINVAL. */
uint8_t athena_voxel_get(const AthenaVoxelWorld *w,int32_t x,int32_t y,int32_t z);
int athena_voxel_set(AthenaVoxelWorld *w,int32_t x,int32_t y,int32_t z,uint8_t type);
/* Fills the box [min, max] (inclusive, clamped to the world). Returns blocks changed. */
int athena_voxel_fill(AthenaVoxelWorld *w,const int32_t min[3],const int32_t max[3],uint8_t type);
/* Region copies, x fastest then z then y (the storage order): out/data hold
 * size[0]*size[1]*size[2] bytes. The region must lie inside the world. */
int athena_voxel_read(const AthenaVoxelWorld *w,const int32_t origin[3],const uint32_t size[3],uint8_t *out);
int athena_voxel_write(AthenaVoxelWorld *w,const int32_t origin[3],const uint32_t size[3],const uint8_t *data);
typedef struct {
    uint32_t seed;
    float base_height,amplitude,frequency;  /* frequency: cycles per block */
    uint32_t octaves;
    uint8_t top,filler,stone;               /* block types (0 skips the layer) */
    uint32_t filler_depth;
    float caves;                            /* 0 none; else 3D noise threshold in (0,1) */
    float cave_frequency;
    uint8_t water; float water_level;       /* water type fills air below water_level */
} AthenaVoxelTerrain;
void athena_voxel_terrain_default(AthenaVoxelTerrain *t);
/* Replaces the world's blocks; marks every chunk dirty. */
int athena_voxel_generate(AthenaVoxelWorld *w,const AthenaVoxelTerrain *t);
/* Height (y of the topmost solid block + 1) of column x,z, 0 when empty. */
int32_t athena_voxel_surface(const AthenaVoxelWorld *w,int32_t x,int32_t z);

/* Meshes the dirty chunks, nearest to `focus` first (NULL: storage order),
 * until budget_ms passes (at least one chunk; <= 0 rebuilds them all).
 * Returns chunks rebuilt or a negative error. */
int athena_voxel_rebuild(AthenaVoxelWorld *w,float budget_ms,const float focus[3]);
uint32_t athena_voxel_dirty_count(const AthenaVoxelWorld *w);
typedef struct {
    uint32_t chunks,meshed_chunks,meshes,faces;  /* faces = quads in the meshes */
    uint64_t mesh_bytes;                         /* vertex + index streams, estimated */
    float last_rebuild_ms;
    /* Of the last rebuild: face generation and Model3D mesh creation. */
    float last_mesh_ms,last_build_ms;
} AthenaVoxelStats;
void athena_voxel_stats(const AthenaVoxelWorld *w,AthenaVoxelStats *out);

typedef struct {
    int32_t block[3];   /* the block hit */
    int32_t normal[3];  /* face normal (where a new block would go: block + normal) */
    uint8_t type;
    float distance,point[3];
} AthenaVoxelHit;
/* Amanatides-Woo DDA through solid blocks within max_distance. A ray that
 * starts inside a solid block hits it at distance 0 with normal 0. Returns
 * 1 with *hit, 0 for no hit, EINVAL. */
int athena_voxel_raycast(const AthenaVoxelWorld *w,const float origin[3],const float direction[3],float max_distance,
    AthenaVoxelHit *hit);
typedef struct { float moved[3]; uint8_t hit[3]; uint8_t on_ground; } AthenaVoxelMove;
/* Moves the box [min, max] by delta against solid blocks, one axis at a
 * time (Y, X, Z), stopping each at the first block face (skin 1e-3). The
 * world's outside is open. Boxes may not span more than 64 blocks per axis. */
int athena_voxel_move_box(const AthenaVoxelWorld *w,const float min[3],const float max[3],const float delta[3],
    AthenaVoxelMove *out);
/* 1 when the box overlaps a solid block. */
int athena_voxel_box_solid(const AthenaVoxelWorld *w,const float min[3],const float max[3]);
/* Draws meshed chunks within distance (<= 0: no limit) of the camera that
 * intersect its frustum, through Render3D (one shared pass), with identity
 * model transforms. Stats accumulate into *stats (reset first). */
int athena_voxel_draw(AthenaVoxelWorld *w,AthenaCamera3D *camera,const AthenaLights *lights,AthenaRender3DCull cull,
    float distance,AthenaRender3DStats *stats,int *render_error);
void athena_voxel_clear_meshes(AthenaVoxelWorld *w);
#endif
