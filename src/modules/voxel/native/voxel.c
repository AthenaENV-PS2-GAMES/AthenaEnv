#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <athena/voxel.h>
#include <athena/meshbuilder.h>
#include <athena/float_bits.h>

#if defined(__mips__)
static uint32_t ticks(void) { uint32_t c; __asm__ __volatile__("mfc0 %0, $9" : "=r"(c)); return c; }
#define TICKS_PER_MS 294912.0f
#else
#include <time.h>
static uint32_t ticks(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint32_t)((uint64_t)ts.tv_sec*1000000u+(uint64_t)ts.tv_nsec/1000u);
}
#define TICKS_PER_MS 1000.0f
#endif

typedef struct {
    AthenaMesh3D **meshes; uint32_t mesh_count;
    uint32_t faces;
    uint8_t dirty;
    float min[3],max[3];   /* bounds of the faces (valid when faces > 0) */
} Chunk;
struct AthenaVoxelWorld {
    uint64_t refs;
    uint32_t size[3],chunk,chunks[3],chunk_count,dirty;
    uint8_t *blocks;
    Chunk *chunk_list;
    AthenaVoxelMaterial materials[ATHENA_VOXEL_TYPES];
    AthenaVoxelStyle style;
    AthenaMeshBuilder *builder;
    uint8_t *padded;                   /* (chunk + 2)^3 scratch for the mesher */
    float last_rebuild_ms;
    uint32_t mesh_ticks,build_ticks;   /* accumulated by rebuild_chunk() */
    float last_mesh_ms,last_build_ms;
};

static void default_material(AthenaVoxelMaterial *m,uint32_t type) {
    memset(m,0,sizeof(*m));
    m->solid=m->visible=type!=0;
    /* A spread of greys so untouched types still read apart. */
    float g=type?.35f+.5f*(float)((type*37u)%11u)/10.0f:0;
    for(int f=0;f<3;f++) { m->color[f][0]=m->color[f][1]=m->color[f][2]=g; m->color[f][3]=1; m->tile[f]=-1; }
}
AthenaVoxelWorld *athena_voxel_create(const AthenaVoxelDesc *d) {
    if(!d) return NULL;
    uint32_t chunk=d->chunk?d->chunk:16;
    if(chunk<4||chunk>32) return NULL;
    uint64_t n=1;
    for(int i=0;i<3;i++) { if(!d->size[i]||d->size[i]>4096) return NULL; n*=d->size[i]; }
    if(n>ATHENA_VOXEL_MAX_BLOCKS) return NULL;
    AthenaVoxelWorld *w=calloc(1,sizeof(*w)); if(!w) return NULL;
    w->refs=1; w->chunk=chunk; memcpy(w->size,d->size,sizeof(w->size));
    w->chunk_count=1;
    for(int i=0;i<3;i++) { w->chunks[i]=(d->size[i]+chunk-1)/chunk; w->chunk_count*=w->chunks[i]; }
    w->blocks=calloc((size_t)n,1);
    w->chunk_list=calloc(w->chunk_count,sizeof(Chunk));
    w->builder=athena_meshbuilder_create();
    w->padded=malloc((size_t)(chunk+2)*(chunk+2)*(chunk+2));
    if(!w->blocks||!w->chunk_list||!w->builder||!w->padded) { athena_voxel_release(w); return NULL; }
    for(uint32_t t=0;t<ATHENA_VOXEL_TYPES;t++) default_material(&w->materials[t],t);
    w->style.meshing=ATHENA_VOXEL_MESH_NAIVE; w->style.ambient_occlusion=.5f; w->style.baked_light=1;
    w->style.shading=ATHENA_MATERIAL3D_UNLIT;
    return w;
}
void athena_voxel_retain(AthenaVoxelWorld *w) { if(w) w->refs++; }
static void free_chunk(Chunk *c) {
    for(uint32_t i=0;i<c->mesh_count;i++) athena_mesh3d_release(c->meshes[i]);
    free(c->meshes); c->meshes=NULL; c->mesh_count=0; c->faces=0;
}
void athena_voxel_clear_meshes(AthenaVoxelWorld *w) {
    if(!w||!w->chunk_list) return;
    w->dirty=0;
    for(uint32_t i=0;i<w->chunk_count;i++) { free_chunk(&w->chunk_list[i]); w->chunk_list[i].dirty=1; w->dirty++; }
}
void athena_voxel_release(AthenaVoxelWorld *w) {
    if(!w||--w->refs) return;
    if(w->chunk_list) for(uint32_t i=0;i<w->chunk_count;i++) free_chunk(&w->chunk_list[i]);
    athena_texture3d_release(w->style.atlas);
    athena_meshbuilder_destroy(w->builder);
    free(w->padded); free(w->chunk_list); free(w->blocks); free(w);
}
void athena_voxel_size(const AthenaVoxelWorld *w,uint32_t size[3],uint32_t *chunk) {
    if(size) memcpy(size,w->size,sizeof(w->size));
    if(chunk) *chunk=w->chunk;
}
static void mark_all(AthenaVoxelWorld *w) {
    w->dirty=0;
    for(uint32_t i=0;i<w->chunk_count;i++) { w->chunk_list[i].dirty=1; w->dirty++; }
}
int athena_voxel_set_material(AthenaVoxelWorld *w,uint32_t type,const AthenaVoxelMaterial *m) {
    if(!w||!m||!type||type>=ATHENA_VOXEL_TYPES) return ATHENA_VOXEL_EINVAL;
    for(int f=0;f<3;f++) for(int k=0;k<4;k++)
        if(!athena_float_isfinite(m->color[f][k])||m->color[f][k]<0||m->color[f][k]>1) return ATHENA_VOXEL_EINVAL;
    w->materials[type]=*m; mark_all(w); return 0;
}
int athena_voxel_set_style(AthenaVoxelWorld *w,const AthenaVoxelStyle *s) {
    if(!w||!s||(s->meshing!=ATHENA_VOXEL_MESH_NAIVE&&s->meshing!=ATHENA_VOXEL_MESH_GREEDY)||
        !athena_float_isfinite(s->ambient_occlusion)||s->ambient_occlusion<0||s->ambient_occlusion>1||
        (s->shading!=ATHENA_MATERIAL3D_UNLIT&&s->shading!=ATHENA_MATERIAL3D_DIFFUSE)) return ATHENA_VOXEL_EINVAL;
    if(s->atlas) {
        AthenaTexture3DPixels px; athena_texture3d_view(s->atlas,&px);
        if(!s->tile_size||px.width%s->tile_size||px.height%s->tile_size) return ATHENA_VOXEL_EINVAL;
        if(s->meshing==ATHENA_VOXEL_MESH_GREEDY) return ATHENA_VOXEL_EINVAL; /* atlas tiles cannot repeat */
    }
    athena_texture3d_retain(s->atlas);
    athena_texture3d_release(w->style.atlas);
    w->style=*s; mark_all(w); return 0;
}
static inline int inside(const AthenaVoxelWorld *w,int32_t x,int32_t y,int32_t z) {
    return x>=0&&y>=0&&z>=0&&(uint32_t)x<w->size[0]&&(uint32_t)y<w->size[1]&&(uint32_t)z<w->size[2];
}
static inline size_t at(const AthenaVoxelWorld *w,int32_t x,int32_t y,int32_t z) {
    return ((size_t)y*w->size[2]+(size_t)z)*w->size[0]+(size_t)x;
}
uint8_t athena_voxel_get(const AthenaVoxelWorld *w,int32_t x,int32_t y,int32_t z) {
    return w&&inside(w,x,y,z)?w->blocks[at(w,x,y,z)]:0;
}
static void mark_chunk(AthenaVoxelWorld *w,int32_t cx,int32_t cy,int32_t cz) {
    if(cx<0||cy<0||cz<0||(uint32_t)cx>=w->chunks[0]||(uint32_t)cy>=w->chunks[1]||(uint32_t)cz>=w->chunks[2]) return;
    Chunk *c=&w->chunk_list[((uint32_t)cy*w->chunks[2]+(uint32_t)cz)*w->chunks[0]+(uint32_t)cx];
    if(!c->dirty) { c->dirty=1; w->dirty++; }
}
/* The chunk of the block and, for blocks on chunk borders, the neighbours
 * whose faces or ambient occlusion it changes (edges and corners too). */
static void mark_block(AthenaVoxelWorld *w,int32_t x,int32_t y,int32_t z) {
    int32_t c=(int32_t)w->chunk,p[3]={x,y,z},lo[3],hi[3];
    for(int i=0;i<3;i++) {
        int32_t k=p[i]/c,l=p[i]%c;
        lo[i]=l==0?k-1:k; hi[i]=l==c-1?k+1:k;
    }
    for(int32_t cy=lo[1];cy<=hi[1];cy++) for(int32_t cz=lo[2];cz<=hi[2];cz++) for(int32_t cx=lo[0];cx<=hi[0];cx++)
        mark_chunk(w,cx,cy,cz);
}
int athena_voxel_set(AthenaVoxelWorld *w,int32_t x,int32_t y,int32_t z,uint8_t type) {
    if(!w||!inside(w,x,y,z)) return ATHENA_VOXEL_EINVAL;
    uint8_t *b=&w->blocks[at(w,x,y,z)];
    if(*b==type) return 0;
    *b=type; mark_block(w,x,y,z); return 1;
}
static void mark_region(AthenaVoxelWorld *w,const int32_t lo[3],const int32_t hi[3]) {
    int32_t c=(int32_t)w->chunk;
    for(int32_t cy=(lo[1]-1)/c;cy<=(hi[1]+1)/c;cy++) for(int32_t cz=(lo[2]-1)/c;cz<=(hi[2]+1)/c;cz++)
        for(int32_t cx=(lo[0]-1)/c;cx<=(hi[0]+1)/c;cx++) mark_chunk(w,cx,cy,cz);
}
int athena_voxel_fill(AthenaVoxelWorld *w,const int32_t mn[3],const int32_t mx[3],uint8_t type) {
    if(!w||!mn||!mx) return ATHENA_VOXEL_EINVAL;
    int32_t lo[3],hi[3];
    for(int i=0;i<3;i++) {
        lo[i]=mn[i]<0?0:mn[i]; hi[i]=mx[i]>=(int32_t)w->size[i]?(int32_t)w->size[i]-1:mx[i];
        if(lo[i]>hi[i]) return 0;
    }
    int changed=0;
    for(int32_t y=lo[1];y<=hi[1];y++) for(int32_t z=lo[2];z<=hi[2];z++) {
        uint8_t *row=&w->blocks[at(w,lo[0],y,z)];
        for(int32_t x=0;x<=hi[0]-lo[0];x++) if(row[x]!=type) { row[x]=type; changed++; }
    }
    if(changed) mark_region(w,lo,hi);
    return changed;
}
static int region_ok(const AthenaVoxelWorld *w,const int32_t o[3],const uint32_t s[3]) {
    for(int i=0;i<3;i++) if(o[i]<0||!s[i]||(uint64_t)o[i]+s[i]>w->size[i]) return 0;
    return 1;
}
int athena_voxel_read(const AthenaVoxelWorld *w,const int32_t o[3],const uint32_t s[3],uint8_t *out) {
    if(!w||!o||!s||!out||!region_ok(w,o,s)) return ATHENA_VOXEL_EINVAL;
    for(uint32_t y=0;y<s[1];y++) for(uint32_t z=0;z<s[2];z++)
        memcpy(out+((size_t)y*s[2]+z)*s[0],&w->blocks[at(w,o[0],o[1]+(int32_t)y,o[2]+(int32_t)z)],s[0]);
    return 0;
}
int athena_voxel_write(AthenaVoxelWorld *w,const int32_t o[3],const uint32_t s[3],const uint8_t *data) {
    if(!w||!o||!s||!data||!region_ok(w,o,s)) return ATHENA_VOXEL_EINVAL;
    for(uint32_t y=0;y<s[1];y++) for(uint32_t z=0;z<s[2];z++)
        memcpy(&w->blocks[at(w,o[0],o[1]+(int32_t)y,o[2]+(int32_t)z)],data+((size_t)y*s[2]+z)*s[0],s[0]);
    int32_t hi[3]={o[0]+(int32_t)s[0]-1,o[1]+(int32_t)s[1]-1,o[2]+(int32_t)s[2]-1};
    mark_region(w,o,hi);
    return 0;
}

/* --- Terrain: gradient noise with a seeded permutation ----------------- */
typedef struct { uint8_t perm[512]; } Noise;
static void noise_init(Noise *n,uint32_t seed) {
    uint32_t s=seed?seed:0x9E3779B9u;
    for(int i=0;i<256;i++) n->perm[i]=(uint8_t)i;
    for(int i=255;i>0;i--) {
        s^=s<<13; s^=s>>17; s^=s<<5;
        int j=(int)(s%(uint32_t)(i+1)); uint8_t t=n->perm[i]; n->perm[i]=n->perm[j]; n->perm[j]=t;
    }
    for(int i=0;i<256;i++) n->perm[256+i]=n->perm[i];
}
static inline float fade(float t) { return t*t*t*(t*(t*6-15)+10); }
static inline float lerp(float a,float b,float t) { return a+(b-a)*t; }
static inline float grad2(uint8_t h,float x,float y) {
    switch(h&7) { case 0: return x+y; case 1: return x-y; case 2: return -x+y; case 3: return -x-y;
        case 4: return x; case 5: return -x; case 6: return y; default: return -y; }
}
static float noise2(const Noise *n,float x,float y) {
    float fx=floorf(x),fy=floorf(y); int X=(int)fx&255,Y=(int)fy&255;
    x-=fx; y-=fy;
    float u=fade(x),v=fade(y);
    const uint8_t *p=n->perm;
    int a=p[X]+Y,b=p[X+1]+Y;
    return lerp(lerp(grad2(p[a],x,y),grad2(p[b],x-1,y),u),lerp(grad2(p[a+1],x,y-1),grad2(p[b+1],x-1,y-1),u),v)*.7071f;
}
static inline float grad3(uint8_t h,float x,float y,float z) {
    int k=h&15; float u=k<8?x:y,v=k<4?y:(k==12||k==14)?x:z;
    return ((k&1)?-u:u)+((k&2)?-v:v);
}
static float noise3(const Noise *n,float x,float y,float z) {
    float fx=floorf(x),fy=floorf(y),fz=floorf(z); int X=(int)fx&255,Y=(int)fy&255,Z=(int)fz&255;
    x-=fx; y-=fy; z-=fz;
    float u=fade(x),v=fade(y),w=fade(z);
    const uint8_t *p=n->perm;
    int A=p[X]+Y,AA=p[A]+Z,AB=p[A+1]+Z,B=p[X+1]+Y,BA=p[B]+Z,BB=p[B+1]+Z;
    return lerp(lerp(lerp(grad3(p[AA],x,y,z),grad3(p[BA],x-1,y,z),u),lerp(grad3(p[AB],x,y-1,z),grad3(p[BB],x-1,y-1,z),u),v),
        lerp(lerp(grad3(p[AA+1],x,y,z-1),grad3(p[BA+1],x-1,y,z-1),u),lerp(grad3(p[AB+1],x,y-1,z-1),grad3(p[BB+1],x-1,y-1,z-1),u),v),w);
}
void athena_voxel_terrain_default(AthenaVoxelTerrain *t) {
    memset(t,0,sizeof(*t));
    t->seed=1; t->base_height=-1; t->amplitude=-1; t->frequency=1.0f/48; t->octaves=4;
    t->top=1; t->filler=2; t->stone=3; t->filler_depth=3; t->caves=0; t->cave_frequency=1.0f/24; t->water=0; t->water_level=0;
}
/* Cave noise on a coarse grid (every CAVE_STEP blocks), interpolated: two
 * noises per sample instead of per block. */
#define CAVE_STEP 4
int athena_voxel_generate(AthenaVoxelWorld *w,const AthenaVoxelTerrain *t) {
    if(!w||!t||!t->octaves||t->octaves>8||!athena_float_isfinite(t->frequency)||t->frequency<=0||
        !athena_float_isfinite(t->caves)||t->caves<0||t->caves>=1||!athena_float_isfinite(t->cave_frequency)||
        t->cave_frequency<=0||!athena_float_isfinite(t->water_level)) return ATHENA_VOXEL_EINVAL;
    float base=t->base_height<0?(float)w->size[1]*.45f:t->base_height;
    float amp=t->amplitude<0?(float)w->size[1]*.2f:t->amplitude;
    if(!athena_float_isfinite(base)||!athena_float_isfinite(amp)) return ATHENA_VOXEL_EINVAL;
    Noise noise,cave_a,cave_b;
    noise_init(&noise,t->seed); noise_init(&cave_a,t->seed*2654435761u+1); noise_init(&cave_b,t->seed*2246822519u+7);
    const uint32_t sx=w->size[0],sy=w->size[1],sz=w->size[2];
    memset(w->blocks,0,(size_t)sx*sy*sz);
    int32_t *height=malloc((size_t)sx*sz*sizeof(int32_t)); if(!height) return ATHENA_VOXEL_ENOMEM;
    for(uint32_t z=0;z<sz;z++) for(uint32_t x=0;x<sx;x++) {
        float f=t->frequency,a=1,sum=0,norm=0;
        for(uint32_t o=0;o<t->octaves;o++) { sum+=noise2(&noise,(float)x*f,(float)z*f)*a; norm+=a; a*=.5f; f*=2; }
        int32_t h=(int32_t)lroundf(base+amp*sum/norm);
        if(h<1) h=1;
        if(h>(int32_t)sy) h=(int32_t)sy;
        height[z*sx+x]=h;
        for(int32_t y=0;y<h;y++) {
            int32_t depth=h-1-y;
            uint8_t type=depth==0?t->top:depth<=(int32_t)t->filler_depth?t->filler:t->stone;
            if(!type) type=t->stone?t->stone:t->filler?t->filler:t->top;
            w->blocks[at(w,(int32_t)x,y,(int32_t)z)]=type;
        }
    }
    if(t->caves>0) {
        uint32_t gx=sx/CAVE_STEP+2,gy=sy/CAVE_STEP+2,gz=sz/CAVE_STEP+2;
        float *ga=malloc((size_t)gx*gy*gz*sizeof(float)*2);
        if(!ga) { free(height); return ATHENA_VOXEL_ENOMEM; }
        float *gb=ga+(size_t)gx*gy*gz,f=t->cave_frequency;
        for(uint32_t y=0;y<gy;y++) for(uint32_t z=0;z<gz;z++) for(uint32_t x=0;x<gx;x++) {
            float px=(float)(x*CAVE_STEP)*f,py=(float)(y*CAVE_STEP)*f*1.6f,pz=(float)(z*CAVE_STEP)*f;
            size_t i=((size_t)y*gz+z)*gx+x;
            ga[i]=noise3(&cave_a,px,py,pz); gb[i]=noise3(&cave_b,px+31.7f,py,pz-17.3f);
        }
        float r=t->caves*.22f;
        for(uint32_t z=0;z<sz;z++) for(uint32_t x=0;x<sx;x++) {
            int32_t top=height[z*sx+x]-2;   /* keep a crust, mostly */
            for(int32_t y=1;y<top;y++) {
                uint32_t X=x/CAVE_STEP,Y=(uint32_t)y/CAVE_STEP,Z=z/CAVE_STEP;
                float fx=(float)(x%CAVE_STEP)/CAVE_STEP,fy=(float)((uint32_t)y%CAVE_STEP)/CAVE_STEP,fz=(float)(z%CAVE_STEP)/CAVE_STEP;
                #define G(arr,dx,dy,dz) arr[((size_t)(Y+dy)*gz+(Z+dz))*gx+(X+dx)]
                #define TRI(arr) lerp(lerp(lerp(G(arr,0,0,0),G(arr,1,0,0),fx),lerp(G(arr,0,0,1),G(arr,1,0,1),fx),fz), \
                    lerp(lerp(G(arr,0,1,0),G(arr,1,1,0),fx),lerp(G(arr,0,1,1),G(arr,1,1,1),fx),fz),fy)
                float a=TRI(ga);
                if(fabsf(a)>=r) continue;
                float b=TRI(gb);
                if(fabsf(b)<r) w->blocks[at(w,(int32_t)x,y,(int32_t)z)]=0;
                #undef TRI
                #undef G
            }
        }
        free(ga);
    }
    if(t->water) {
        int32_t level=(int32_t)floorf(t->water_level);
        if(level>(int32_t)sy) level=(int32_t)sy;
        for(uint32_t z=0;z<sz;z++) for(uint32_t x=0;x<sx;x++)
            for(int32_t y=height[z*sx+x];y<level;y++) w->blocks[at(w,(int32_t)x,y,(int32_t)z)]=t->water;
    }
    free(height);
    mark_all(w);
    return 0;
}
int32_t athena_voxel_surface(const AthenaVoxelWorld *w,int32_t x,int32_t z) {
    if(!w||!inside(w,x,0,z)) return 0;
    for(int32_t y=(int32_t)w->size[1]-1;y>=0;y--) if(w->materials[w->blocks[at(w,x,y,z)]].solid) return y+1;
    return 0;
}

/* --- Meshing ------------------------------------------------------------ */
/* Faces as in meshbuilder: normal, u, v with u x v = normal. */
static const int8_t FACE[6][3][3]={
    {{1,0,0},{0,0,-1},{0,1,0}}, {{-1,0,0},{0,0,1},{0,1,0}},
    {{0,1,0},{1,0,0},{0,0,-1}}, {{0,-1,0},{1,0,0},{0,0,1}},
    {{0,0,1},{1,0,0},{0,1,0}},  {{0,0,-1},{-1,0,0},{0,1,0}}};
static const uint8_t FACE_GROUP[6]={2,2,0,1,2,2};
static const float FACE_SHADE[6]={.8f,.8f,1.0f,.5f,.65f,.65f};
static const int8_t CORNER[4][2]={{-1,-1},{1,-1},{1,1},{-1,1}};
static const float CORNER_UV[4][2]={{0,1},{1,1},{1,0},{0,0}};
static const float AO_LEVEL[4]={0,.2f,.35f,.5f};

/* Per type flags, refreshed before meshing: OPAQUE hides neighbours' faces
 * and occludes (solid and visible), VISIBLE is drawn. */
enum { FLAG_OPAQUE=1, FLAG_VISIBLE=2 };
static void type_flags(const AthenaVoxelWorld *w,uint8_t flags[ATHENA_VOXEL_TYPES]) {
    for(uint32_t t=0;t<ATHENA_VOXEL_TYPES;t++) {
        const AthenaVoxelMaterial *m=&w->materials[t];
        flags[t]=(uint8_t)((m->solid&&m->visible?FLAG_OPAQUE:0)|(m->visible&&t?FLAG_VISIBLE:0));
    }
}
/* The chunk plus a one-block border (outside the world reads air), so the
 * mesher reaches every neighbour by a fixed offset with no bounds checks. */
typedef struct { uint8_t *b; int32_t P,lo[3]; } Padded;
static void pad_chunk(const AthenaVoxelWorld *w,Padded *pd,const int32_t lo[3],const int32_t hi[3]) {
    int32_t P=pd->P;
    memcpy(pd->lo,lo,sizeof(pd->lo));
    for(int32_t y=-1;y<=hi[1]-lo[1];y++) for(int32_t z=-1;z<=hi[2]-lo[2];z++) {
        uint8_t *row=&pd->b[((y+1)*P+(z+1))*P];
        int32_t wy=lo[1]+y,wz=lo[2]+z;
        int in_yz=wy>=0&&wz>=0&&(uint32_t)wy<w->size[1]&&(uint32_t)wz<w->size[2];
        for(int32_t x=-1;x<=hi[0]-lo[0];x++) {
            int32_t wx=lo[0]+x;
            row[x+1]=in_yz&&wx>=0&&(uint32_t)wx<w->size[0]?w->blocks[at(w,wx,wy,wz)]:0;
        }
    }
}
static void face_color(const AthenaVoxelWorld *w,uint8_t type,int f,float out[4]) {
    const float *c=w->materials[type].color[FACE_GROUP[f]];
    float s=w->style.baked_light?FACE_SHADE[f]:1;
    out[0]=c[0]*s; out[1]=c[1]*s; out[2]=c[2]*s; out[3]=c[3];
}
/* UV rectangle of the type's atlas tile for face f (the whole 0..1 square without one). */
static void tile_rect(AthenaVoxelWorld *w,uint8_t type,int f,uint32_t cols,uint32_t rows) {
    int16_t tile=w->materials[type].tile[FACE_GROUP[f]];
    if(!cols||tile<0||(uint32_t)tile>=cols*rows) { athena_meshbuilder_set_uv_rect(w->builder,0,0,1,1); return; }
    float tw=1.0f/(float)cols,th=1.0f/(float)rows,inset=.5f/(float)w->style.tile_size;
    float u=(float)((uint32_t)tile%cols)*tw,v=(float)((uint32_t)tile/cols)*th;
    athena_meshbuilder_set_uv_rect(w->builder,u+inset*tw,v+inset*th,u+tw-inset*tw,v+th-inset*th);
}
static void grow_bounds(Chunk *c,const float p[3]) {
    for(int i=0;i<3;i++) { if(p[i]<c->min[i]) c->min[i]=p[i]; if(p[i]>c->max[i]) c->max[i]=p[i]; }
}
/* Space reserved by the caller. flip splits the quad along its 1-3 diagonal. */
static void emit_quad(AthenaVoxelWorld *w,Chunk *c,float p[4][3],const float n[3],float col[4][4],int flip) {
    AthenaMeshBuilder *b=w->builder;
    uint32_t i[4];
    for(int k=0;k<4;k++) { i[k]=athena_meshbuilder_put_unchecked(b,p[k],n,col[k],CORNER_UV[k][0],CORNER_UV[k][1]); grow_bounds(c,p[k]); }
    if(flip) { athena_meshbuilder_triangle_unchecked(b,i[1],i[2],i[3]); athena_meshbuilder_triangle_unchecked(b,i[1],i[3],i[0]); }
    else { athena_meshbuilder_triangle_unchecked(b,i[0],i[1],i[2]); athena_meshbuilder_triangle_unchecked(b,i[0],i[2],i[3]); }
    c->faces++;
}
/* Faces at most per block: all six. Reserving per block keeps reserve() cheap. */
#define BLOCK_VERTICES 24u
#define BLOCK_INDICES 36u
static int mesh_naive(AthenaVoxelWorld *w,Chunk *c,const Padded *pd,const int32_t lo[3],const int32_t hi[3],const uint8_t *flags) {
    const float ao=w->style.ambient_occlusion;
    const int32_t P=pd->P,stride[3]={1,P*P,P};   /* x, y, z steps in the padded array */
    int32_t off[6][3];
    for(int f=0;f<6;f++) for(int k=0;k<3;k++)
        off[f][k]=FACE[f][k][0]*stride[0]+FACE[f][k][1]*stride[1]+FACE[f][k][2]*stride[2];
    uint32_t cols=0,rows=0;
    if(w->style.atlas) { AthenaTexture3DPixels px; athena_texture3d_view(w->style.atlas,&px); cols=px.width/w->style.tile_size; rows=px.height/w->style.tile_size; }
    for(int32_t y=lo[1];y<hi[1];y++) for(int32_t z=lo[2];z<hi[2];z++) {
        const uint8_t *row=&pd->b[((y-lo[1]+1)*P+(z-lo[2]+1))*P+1];
        for(int32_t x=lo[0];x<hi[0];x++) {
            const uint8_t *cell=row+(x-lo[0]);
            uint8_t type=*cell;
            if(!(flags[type]&FLAG_VISIBLE)) continue;
            if(athena_meshbuilder_reserve(w->builder,BLOCK_VERTICES,BLOCK_INDICES)) return ATHENA_VOXEL_ENOMEM;
            for(int f=0;f<6;f++) {
                const uint8_t *nb=cell+off[f][0];
                if(*nb==type||(flags[*nb]&FLAG_OPAQUE)) continue;
                const int8_t *n=FACE[f][0],*u=FACE[f][1],*v=FACE[f][2];
                float base[4],col[4][4],p[4][3],bright[4]={1,1,1,1},normal[3]={n[0],n[1],n[2]};
                face_color(w,type,f,base);
                if(cols) tile_rect(w,type,f,cols,rows);
                for(int k=0;k<4;k++) {
                    int su=CORNER[k][0],sv=CORNER[k][1];
                    if(ao>0) {
                        int du=off[f][1]*su,dv=off[f][2]*sv;
                        int s1=flags[nb[du]]&FLAG_OPAQUE,s2=flags[nb[dv]]&FLAG_OPAQUE,cc=flags[nb[du+dv]]&FLAG_OPAQUE;
                        int occ=s1&&s2?3:s1+s2+cc;
                        bright[k]=1-ao*AO_LEVEL[occ]*2;
                    }
                    col[k][0]=base[0]*bright[k]; col[k][1]=base[1]*bright[k]; col[k][2]=base[2]*bright[k]; col[k][3]=base[3];
                    p[k][0]=(float)x+.5f+.5f*(float)(n[0]+u[0]*su+v[0]*sv);
                    p[k][1]=(float)y+.5f+.5f*(float)(n[1]+u[1]*su+v[1]*sv);
                    p[k][2]=(float)z+.5f+.5f*(float)(n[2]+u[2]*su+v[2]*sv);
                }
                emit_quad(w,c,p,normal,col,bright[0]+bright[2]<bright[1]+bright[3]);
            }
        }
    }
    return 0;
}
/* Greedy: per face direction and slice, merge equal faces into rectangles. */
static int mesh_greedy(AthenaVoxelWorld *w,Chunk *c,const Padded *pd,const int32_t lo[3],const int32_t hi[3],const uint8_t *flags) {
    uint32_t cs=w->chunk;
    const int32_t P=pd->P,stride[3]={1,P*P,P};
    uint8_t *mask=malloc((size_t)cs*cs); if(!mask) return ATHENA_VOXEL_ENOMEM;
    for(int f=0;f<6;f++) {
        const int8_t *n=FACE[f][0];
        int a=n[0]?0:n[1]?1:2,ua=(a+1)%3,va=(a+2)%3;
        int32_t du=hi[ua]-lo[ua],dv=hi[va]-lo[va],noff=n[0]*stride[0]+n[1]*stride[1]+n[2]*stride[2];
        for(int32_t s=lo[a];s<hi[a];s++) {
            memset(mask,0,(size_t)cs*cs);
            int any=0;
            for(int32_t j=0;j<dv;j++) for(int32_t i=0;i<du;i++) {
                int32_t q[3]; q[a]=s-lo[a]; q[ua]=i; q[va]=j;
                const uint8_t *cell=&pd->b[((q[1]+1)*P+(q[2]+1))*P+(q[0]+1)];
                uint8_t type=*cell,nb=cell[noff];
                if(!(flags[type]&FLAG_VISIBLE)||nb==type||(flags[nb]&FLAG_OPAQUE)) continue;
                mask[j*cs+i]=type; any=1;
            }
            if(!any) continue;
            for(int32_t j=0;j<dv;j++) for(int32_t i=0;i<du;) {
                uint8_t type=mask[j*cs+i];
                if(!type) { i++; continue; }
                int32_t wdt=1,hgt=1;
                while(i+wdt<du&&mask[j*cs+i+wdt]==type) wdt++;
                for(;j+hgt<dv;hgt++) {
                    int32_t k=0;
                    while(k<wdt&&mask[(j+hgt)*cs+i+k]==type) k++;
                    if(k<wdt) break;
                }
                for(int32_t y2=0;y2<hgt;y2++) memset(&mask[(j+y2)*cs+i],0,(size_t)wdt);
                if(athena_meshbuilder_reserve(w->builder,4,6)) { free(mask); return ATHENA_VOXEL_ENOMEM; }
                float base[4],col[4][4],p[4][3],normal[3]={n[0],n[1],n[2]};
                face_color(w,type,f,base);
                float plane=(float)s+(n[a]>0?1.0f:0.0f);
                float u0=(float)(lo[ua]+i),u1=u0+(float)wdt,v0=(float)(lo[va]+j),v1=v0+(float)hgt;
                const float cu[4]={u0,u1,u1,u0},cv[4]={v0,v0,v1,v1};
                for(int k=0;k<4;k++) { p[k][a]=plane; p[k][ua]=cu[k]; p[k][va]=cv[k]; col[k][0]=base[0]; col[k][1]=base[1]; col[k][2]=base[2]; col[k][3]=base[3]; }
                /* Order the corners counter-clockwise around the normal. */
                float e1[3],e2[3];
                for(int k=0;k<3;k++) { e1[k]=p[1][k]-p[0][k]; e2[k]=p[2][k]-p[0][k]; }
                float cross=(e1[1]*e2[2]-e1[2]*e2[1])*normal[0]+(e1[2]*e2[0]-e1[0]*e2[2])*normal[1]+(e1[0]*e2[1]-e1[1]*e2[0])*normal[2];
                if(cross<0) { float t[3]; memcpy(t,p[1],12); memcpy(p[1],p[3],12); memcpy(p[3],t,12); }
                emit_quad(w,c,p,normal,col,0);
                i+=wdt;
            }
        }
    }
    free(mask);
    return 0;
}
static int rebuild_chunk(AthenaVoxelWorld *w,uint32_t index) {
    Chunk *c=&w->chunk_list[index];
    uint32_t cx=index%w->chunks[0],cz=(index/w->chunks[0])%w->chunks[2],cy=index/(w->chunks[0]*w->chunks[2]);
    int32_t lo[3]={(int32_t)(cx*w->chunk),(int32_t)(cy*w->chunk),(int32_t)(cz*w->chunk)},hi[3];
    for(int i=0;i<3;i++) { hi[i]=lo[i]+(int32_t)w->chunk; if(hi[i]>(int32_t)w->size[i]) hi[i]=(int32_t)w->size[i]; }
    athena_meshbuilder_clear(w->builder);
    free_chunk(c);
    for(int i=0;i<3;i++) { c->min[i]=3.0e38f; c->max[i]=-3.0e38f; }
    uint32_t t0=ticks();
    uint8_t flags[ATHENA_VOXEL_TYPES]; type_flags(w,flags);
    Padded pd={w->padded,(int32_t)w->chunk+2,{0,0,0}};
    pad_chunk(w,&pd,lo,hi);
    int code=w->style.meshing==ATHENA_VOXEL_MESH_GREEDY?mesh_greedy(w,c,&pd,lo,hi,flags):mesh_naive(w,c,&pd,lo,hi,flags);
    uint32_t t1=ticks(); w->mesh_ticks+=t1-t0;
    if(code<0) { free_chunk(c); return code==ATHENA_MESHBUILDER_ENOMEM||code==ATHENA_VOXEL_ENOMEM?ATHENA_VOXEL_ENOMEM:ATHENA_VOXEL_EINVAL; }
    if(c->faces) {
        AthenaMaterial3D material; athena_material3d_default(&material);
        material.shading=w->style.shading; material.texture=w->style.atlas;
        uint32_t parts=athena_meshbuilder_part_count(w->builder);
        c->meshes=calloc(parts,sizeof(*c->meshes)); if(!c->meshes) { c->faces=0; return ATHENA_VOXEL_ENOMEM; }
        int n=athena_meshbuilder_build(w->builder,&material,c->meshes,parts);
        if(n<0) { free(c->meshes); c->meshes=NULL; c->faces=0; return n==ATHENA_MESHBUILDER_ENOMEM?ATHENA_VOXEL_ENOMEM:ATHENA_VOXEL_EINVAL; }
        c->mesh_count=(uint32_t)n;
    }
    w->build_ticks+=ticks()-t1;
    c->dirty=0; w->dirty--;
    return 0;
}
int athena_voxel_rebuild(AthenaVoxelWorld *w,float budget_ms,const float focus[3]) {
    if(!w||!athena_float_isfinite(budget_ms)||(focus&&(!athena_float_isfinite(focus[0])||!athena_float_isfinite(focus[1])||
        !athena_float_isfinite(focus[2])))) return ATHENA_VOXEL_EINVAL;
    uint32_t start=ticks(); int rebuilt=0;
    w->mesh_ticks=w->build_ticks=0;
    while(w->dirty) {
        uint32_t best=UINT32_MAX; float best_d=3.0e38f;
        for(uint32_t i=0;i<w->chunk_count;i++) {
            if(!w->chunk_list[i].dirty) continue;
            if(!focus) { best=i; break; }
            float cs=(float)w->chunk;
            float cx=((float)(i%w->chunks[0])+.5f)*cs,cz=((float)((i/w->chunks[0])%w->chunks[2])+.5f)*cs,
                cy=((float)(i/(w->chunks[0]*w->chunks[2]))+.5f)*cs;
            float d=(cx-focus[0])*(cx-focus[0])+(cy-focus[1])*(cy-focus[1])+(cz-focus[2])*(cz-focus[2]);
            if(d<best_d) { best_d=d; best=i; }
        }
        if(best==UINT32_MAX) break;
        int code=rebuild_chunk(w,best);
        if(code<0) return code;
        rebuilt++;
        if(budget_ms>0&&(float)(uint32_t)(ticks()-start)/TICKS_PER_MS>=budget_ms) break;
    }
    if(rebuilt) {
        w->last_rebuild_ms=(float)(uint32_t)(ticks()-start)/TICKS_PER_MS;
        w->last_mesh_ms=(float)w->mesh_ticks/TICKS_PER_MS; w->last_build_ms=(float)w->build_ticks/TICKS_PER_MS;
    }
    return rebuilt;
}
uint32_t athena_voxel_dirty_count(const AthenaVoxelWorld *w) { return w?w->dirty:0; }
void athena_voxel_stats(const AthenaVoxelWorld *w,AthenaVoxelStats *s) {
    memset(s,0,sizeof(*s));
    if(!w) return;
    s->chunks=w->chunk_count; s->last_rebuild_ms=w->last_rebuild_ms;
    s->last_mesh_ms=w->last_mesh_ms; s->last_build_ms=w->last_build_ms;
    uint32_t per_vertex=12+4+(w->style.shading==ATHENA_MATERIAL3D_DIFFUSE?12:0)+(w->style.atlas?8:0);
    for(uint32_t i=0;i<w->chunk_count;i++) {
        const Chunk *c=&w->chunk_list[i];
        if(c->mesh_count) s->meshed_chunks++;
        s->meshes+=c->mesh_count; s->faces+=c->faces;
        s->mesh_bytes+=(uint64_t)c->faces*(4*per_vertex+6);
    }
}

/* --- Queries -------------------------------------------------------------- */
int athena_voxel_raycast(const AthenaVoxelWorld *w,const float o[3],const float dir[3],float max_d,AthenaVoxelHit *hit) {
    if(!w||!o||!dir||!hit||!athena_float_isfinite(max_d)||max_d<0) return ATHENA_VOXEL_EINVAL;
    float d[3],len=0;
    for(int i=0;i<3;i++) { if(!athena_float_isfinite(o[i])||!athena_float_isfinite(dir[i])) return ATHENA_VOXEL_EINVAL; len+=dir[i]*dir[i]; }
    len=sqrtf(len); if(!(len>1e-20f)) return ATHENA_VOXEL_EINVAL;
    for(int i=0;i<3;i++) d[i]=dir[i]/len;
    int32_t p[3],step[3]; float tmax[3],tdelta[3];
    for(int i=0;i<3;i++) {
        p[i]=(int32_t)floorf(o[i]);
        if(d[i]>0) { step[i]=1; tdelta[i]=1/d[i]; tmax[i]=((float)p[i]+1-o[i])/d[i]; }
        else if(d[i]<0) { step[i]=-1; tdelta[i]=-1/d[i]; tmax[i]=((float)p[i]-o[i])/d[i]; }
        else { step[i]=0; tdelta[i]=3.0e38f; tmax[i]=3.0e38f; }
    }
    int32_t normal[3]={0,0,0}; float t=0;
    for(int guard=0;guard<100000;guard++) {
        uint8_t type=athena_voxel_get(w,p[0],p[1],p[2]);
        if(w->materials[type].solid) {
            memcpy(hit->block,p,sizeof(p)); memcpy(hit->normal,normal,sizeof(normal));
            hit->type=type; hit->distance=t;
            for(int i=0;i<3;i++) hit->point[i]=o[i]+d[i]*t;
            return 1;
        }
        int axis=tmax[0]<tmax[1]?(tmax[0]<tmax[2]?0:2):(tmax[1]<tmax[2]?1:2);
        t=tmax[axis];
        if(t>max_d) return 0;
        p[axis]+=step[axis]; tmax[axis]+=tdelta[axis];
        normal[0]=normal[1]=normal[2]=0; normal[axis]=-step[axis];
        /* Leaving the world's box for good ends the walk. */
        if((p[axis]<0&&step[axis]<0)||(p[axis]>=(int32_t)w->size[axis]&&step[axis]>0)) return 0;
    }
    return 0;
}
#define SKIN 1e-3f
static int solid_in(const AthenaVoxelWorld *w,int axis,int32_t layer,const int32_t lo[3],const int32_t hi[3]) {
    int a1=(axis+1)%3,a2=(axis+2)%3;
    for(int32_t i=lo[a1];i<=hi[a1];i++) for(int32_t j=lo[a2];j<=hi[a2];j++) {
        int32_t p[3]; p[axis]=layer; p[a1]=i; p[a2]=j;
        if(w->materials[athena_voxel_get(w,p[0],p[1],p[2])].solid) return 1;
    }
    return 0;
}
int athena_voxel_move_box(const AthenaVoxelWorld *w,const float mn[3],const float mx[3],const float delta[3],AthenaVoxelMove *out) {
    if(!w||!mn||!mx||!delta||!out) return ATHENA_VOXEL_EINVAL;
    float lo[3],hi[3];
    for(int i=0;i<3;i++) {
        if(!athena_float_isfinite(mn[i])||!athena_float_isfinite(mx[i])||!athena_float_isfinite(delta[i])||mn[i]>mx[i]||
            mx[i]-mn[i]>64||fabsf(delta[i])>256) return ATHENA_VOXEL_EINVAL;
        lo[i]=mn[i]; hi[i]=mx[i];
    }
    memset(out,0,sizeof(*out));
    static const int order[3]={1,0,2};
    for(int k=0;k<3;k++) {
        int a=order[k]; float d=delta[a];
        if(d==0) continue;
        int32_t rlo[3],rhi[3];
        for(int i=0;i<3;i++) { rlo[i]=(int32_t)floorf(lo[i]+SKIN*.5f); rhi[i]=(int32_t)floorf(hi[i]-SKIN*.5f); }
        if(d>0) {
            int32_t first=(int32_t)floorf(hi[a]-SKIN*.5f)+1,last=(int32_t)floorf(hi[a]+d);
            for(int32_t layer=first;layer<=last;layer++) if(solid_in(w,a,layer,rlo,rhi)) {
                float allowed=(float)layer-hi[a]-SKIN;
                d=allowed<0?0:allowed; out->hit[a]=1; break;
            }
        } else {
            int32_t first=(int32_t)floorf(lo[a]+SKIN*.5f)-1,last=(int32_t)floorf(lo[a]+d);
            for(int32_t layer=first;layer>=last;layer--) if(solid_in(w,a,layer,rlo,rhi)) {
                float allowed=(float)(layer+1)-lo[a]+SKIN;
                d=allowed>0?0:allowed; out->hit[a]=1; if(a==1) out->on_ground=1; break;
            }
        }
        lo[a]+=d; hi[a]+=d; out->moved[a]=d;
    }
    return 0;
}
int athena_voxel_box_solid(const AthenaVoxelWorld *w,const float mn[3],const float mx[3]) {
    if(!w||!mn||!mx) return ATHENA_VOXEL_EINVAL;
    int32_t lo[3],hi[3];
    for(int i=0;i<3;i++) {
        if(!athena_float_isfinite(mn[i])||!athena_float_isfinite(mx[i])||mn[i]>mx[i]||mx[i]-mn[i]>256) return ATHENA_VOXEL_EINVAL;
        lo[i]=(int32_t)floorf(mn[i]); hi[i]=(int32_t)floorf(mx[i]);
    }
    for(int32_t y=lo[1];y<=hi[1];y++) for(int32_t z=lo[2];z<=hi[2];z++) for(int32_t x=lo[0];x<=hi[0];x++)
        if(w->materials[athena_voxel_get(w,x,y,z)].solid) return 1;
    return 0;
}
int athena_voxel_draw(AthenaVoxelWorld *w,AthenaCamera3D *camera,const AthenaLights *lights,AthenaRender3DCull cull,
    float distance,AthenaRender3DStats *stats,int *render_error) {
    if(!w||!camera||!stats||!athena_float_isfinite(distance)) return ATHENA_VOXEL_EINVAL;
    memset(stats,0,sizeof(*stats));
    if(!athena_camera3d_update(camera)) return ATHENA_VOXEL_EINVAL;
    AthenaMatrix4 identity; ath_matrix4_identity(&identity);
    const float eye[3]={camera->position.x,camera->position.y,camera->position.z};
    int owned=athena_render3d_group_begin()==0,result=0;
    for(uint32_t i=0;i<w->chunk_count&&!result;i++) {
        Chunk *c=&w->chunk_list[i];
        if(!c->mesh_count) continue;
        if(distance>0) {
            float d2=0;
            for(int k=0;k<3;k++) { float v=eye[k]<c->min[k]?c->min[k]-eye[k]:eye[k]>c->max[k]?eye[k]-c->max[k]:0; d2+=v*v; }
            if(d2>distance*distance) continue;
        }
        int relation=athena_camera3d_box_relation(camera,NULL,c->min,c->max);
        if(relation<0) { result=ATHENA_VOXEL_EINVAL; break; }
        if(relation==ATHENA_FRUSTUM3D_OUTSIDE) { stats->submitted_objects+=c->mesh_count; stats->culled_objects+=c->mesh_count; continue; }
        for(uint32_t m=0;m<c->mesh_count;m++) {
            int code=relation==ATHENA_FRUSTUM3D_INSIDE?
                athena_render3d_draw_mesh_contained(c->meshes[m],&identity,camera,lights,cull,stats):
                athena_render3d_draw_mesh(c->meshes[m],&identity,camera,lights,cull,stats);
            if(code<0) { if(render_error) *render_error=code; result=ATHENA_VOXEL_ERENDER; break; }
        }
    }
    if(owned) athena_render3d_group_end();
    return result;
}
