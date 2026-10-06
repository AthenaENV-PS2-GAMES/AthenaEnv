#include <athena/float_bits.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include <athena/model3d.h>
struct AthenaMesh3D {
    uint64_t refs;
    uint32_t count;
    AthenaPosition3D *positions; AthenaColor3D *colors;
    AthenaPosition3D *normals;
    AthenaTexcoord3D *texcoords;
    AthenaMaterial3D material;
    float minimum[3],maximum[3];
};
struct AthenaInstance3D {
    uint64_t refs;
    AthenaMesh3D *mesh;
    AthenaVector4 position,scale;
    AthenaQuaternion rotation;
    AthenaMatrix4 transform;
    int dirty;
};
const char *athena_model3d_error(int result) {
    switch(result) {
        case ATHENA_MODEL3D_EINVAL: return "Invalid geometry or transform";
        case ATHENA_MODEL3D_ENOMEM: return "Out of memory";
        case ATHENA_MODEL3D_EIO: return "Cannot read model";
        case ATHENA_MODEL3D_EVRAM: return "Texture cannot be made resident in VRAM";
        case ATHENA_MODEL3D_EFORMAT: return "Malformed model";
        case ATHENA_MODEL3D_EUNSUPPORTED: return "Model feature is not supported by the static pipeline";
        default: return "OK";
    }
}
/* R5900 GCC assumes hardware floats cannot be NaN/Inf and folds isfinite()
 * to true. TypedArray input still carries arbitrary IEEE bits: inspect those. */

void athena_material3d_default(AthenaMaterial3D *m) {
    *m=(AthenaMaterial3D){.shading=ATHENA_MATERIAL3D_UNLIT,.base_color={1,1,1,1}};
}
int athena_material3d_validate(const AthenaMaterial3D *m) {
    if(!m||(m->shading!=ATHENA_MATERIAL3D_UNLIT&&m->shading!=ATHENA_MATERIAL3D_DIFFUSE)) return 0;
    for(unsigned j=0;j<4;j++) if(!athena_float_isfinite(m->base_color[j])||m->base_color[j]<0||m->base_color[j]>1) return 0;
    return 1;
}
static int normalize(AthenaPosition3D *out,double x,double y,double z) {
    double length=sqrt(x*x+y*y+z*z);
    if(length==0) return 0;
    *out=(AthenaPosition3D){x/length,y/length,z/length}; return 1;
}
AthenaGeometry3DIssue athena_geometry3d_validate(const AthenaGeometry3D *g,uint32_t *offset) {
    if(offset) *offset=0;
    if(!g||!g->positions||g->vertex_count==0||g->vertex_count>ATHENA_MODEL3D_MAX_VERTICES)
        return ATHENA_GEOMETRY3D_VERTEX_COUNT;
    if((g->colors && g->color_count!=g->vertex_count)||(!g->colors && g->color_count))
        return ATHENA_GEOMETRY3D_COLOR_COUNT;
    if((g->normals&&g->normal_count!=g->vertex_count)||(!g->normals&&g->normal_count))
        return ATHENA_GEOMETRY3D_NORMAL_COUNT;
    if((g->texcoords&&g->texcoord_count!=g->vertex_count)||(!g->texcoords&&g->texcoord_count)||
        (g->material&&g->material->texture&&!g->texcoords)) return ATHENA_GEOMETRY3D_TEXCOORD_COUNT;
    if(g->material&&!athena_material3d_validate(g->material)) return ATHENA_GEOMETRY3D_MATERIAL;
    if((g->indices && !g->index_count)||(!g->indices && g->index_count))
        return ATHENA_GEOMETRY3D_INDEX_COUNT;
    uint32_t count=g->indices?g->index_count:g->vertex_count;
    if(count==0||count>ATHENA_MODEL3D_MAX_VERTICES||count%3) return ATHENA_GEOMETRY3D_TRIANGLE_COUNT;
    for(uint32_t i=0;i<g->vertex_count*3;i++) if(!athena_float_isfinite(g->positions[i])) {
        if(offset) *offset=i;
        return ATHENA_GEOMETRY3D_POSITION;
    }
    if(g->colors) for(uint32_t i=0;i<g->color_count*4;i++)
        if(!athena_float_isfinite(g->colors[i])||g->colors[i]<0||g->colors[i]>1) {
            if(offset) *offset=i;
            return ATHENA_GEOMETRY3D_COLOR;
        }
    if(g->indices) for(uint32_t i=0;i<count;i++) if(g->indices[i]>=g->vertex_count) {
        if(offset) *offset=i;
        return ATHENA_GEOMETRY3D_INDEX;
    }
    if(g->texcoords) for(uint32_t i=0;i<g->texcoord_count*2;i++) {
        if(!athena_float_isfinite(g->texcoords[i])||g->texcoords[i]<0||g->texcoords[i]>1) {
            if(offset) *offset=i;
            return ATHENA_GEOMETRY3D_TEXCOORD;
        }
    }
    if(g->normals) for(uint32_t i=0;i<g->normal_count;i++) {
        const float *n=&g->normals[i*3];
        if(!athena_float_isfinite(n[0])||!athena_float_isfinite(n[1])||!athena_float_isfinite(n[2])||
            (n[0]==0&&n[1]==0&&n[2]==0)) {
            if(offset) *offset=i*3;
            return ATHENA_GEOMETRY3D_NORMAL;
        }
    }
    if(!g->normals&&g->material&&g->material->shading==ATHENA_MATERIAL3D_DIFFUSE) {
        for(uint32_t i=0;i<count;i+=3) {
            const float *a=&g->positions[(g->indices?g->indices[i]:i)*3];
            const float *b=&g->positions[(g->indices?g->indices[i+1]:i+1)*3];
            const float *c=&g->positions[(g->indices?g->indices[i+2]:i+2)*3];
            double u[3],v[3]; for(unsigned j=0;j<3;j++) { u[j]=(double)b[j]-a[j]; v[j]=(double)c[j]-a[j]; }
            if(u[1]*v[2]-u[2]*v[1]==0&&u[2]*v[0]-u[0]*v[2]==0&&u[0]*v[1]-u[1]*v[0]==0) {
                if(offset) *offset=i;
                return ATHENA_GEOMETRY3D_NORMAL;
            }
        }
    }
    return ATHENA_GEOMETRY3D_VALID;
}
int athena_mesh3d_create(const AthenaGeometry3D *g,AthenaMesh3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    if(athena_geometry3d_validate(g,NULL)!=ATHENA_GEOMETRY3D_VALID) return ATHENA_MODEL3D_EINVAL;
    uint32_t count=g->indices?g->index_count:g->vertex_count;
    AthenaMesh3D *m=calloc(1,sizeof(*m)); if(!m) return ATHENA_MODEL3D_ENOMEM;
    athena_material3d_default(&m->material); if(g->material) m->material=*g->material;
    m->positions=memalign(16,(count+4)*sizeof(*m->positions));
    m->colors=memalign(16,(count+4)*sizeof(*m->colors));
    if(g->normals||m->material.shading==ATHENA_MATERIAL3D_DIFFUSE) m->normals=memalign(16,(count+4)*sizeof(*m->normals));
    if(g->texcoords) m->texcoords=memalign(16,(count+4)*sizeof(*m->texcoords));
    if(!m->positions||!m->colors||((g->normals||m->material.shading==ATHENA_MATERIAL3D_DIFFUSE)&&!m->normals)||
        (g->texcoords&&!m->texcoords)) {
        free(m->positions); free(m->colors); free(m->normals); free(m->texcoords); free(m); return ATHENA_MODEL3D_ENOMEM;
    }
    memset(m->positions,0,(count+4)*sizeof(*m->positions));
    memset(m->colors,0,(count+4)*sizeof(*m->colors));
    if(m->normals) memset(m->normals,0,(count+4)*sizeof(*m->normals));
    if(m->texcoords) memset(m->texcoords,0,(count+4)*sizeof(*m->texcoords));
    for(uint32_t i=0;i<count;i++) {
        uint32_t source=g->indices?g->indices[i]:i;
        const float *p=&g->positions[source*3];
        m->positions[i]=(AthenaPosition3D){p[0],p[1],p[2]};
        if(g->texcoords) m->texcoords[i]=(AthenaTexcoord3D){g->texcoords[source*2],g->texcoords[source*2+1]};
        const float *c=g->colors?&g->colors[source*4]:NULL;
        uint8_t baked[4]; for(unsigned j=0;j<4;j++) baked[j]=lroundf((c?c[j]:1)*m->material.base_color[j]*255);
        m->colors[i]=(AthenaColor3D){baked[0],baked[1],baked[2],baked[3]};
        if(g->normals) {
            const float *n=&g->normals[source*3]; normalize(&m->normals[i],n[0],n[1],n[2]);
        }
        for(int j=0;j<3;j++) {
            if(i==0||p[j]<m->minimum[j]) m->minimum[j]=p[j];
            if(i==0||p[j]>m->maximum[j]) m->maximum[j]=p[j];
        }
    }
    if(m->normals&&!g->normals) for(uint32_t i=0;i<count;i+=3) {
        AthenaPosition3D a=m->positions[i],b=m->positions[i+1],c=m->positions[i+2],n;
        double ux=(double)b.x-a.x,uy=(double)b.y-a.y,uz=(double)b.z-a.z;
        double vx=(double)c.x-a.x,vy=(double)c.y-a.y,vz=(double)c.z-a.z;
        normalize(&n,uy*vz-uz*vy,uz*vx-ux*vz,ux*vy-uy*vx);
        m->normals[i]=m->normals[i+1]=m->normals[i+2]=n;
    }
    athena_texture3d_retain(m->material.texture);
    m->refs=1; m->count=count; *out=m; return 0;
}
void athena_mesh3d_retain(AthenaMesh3D *m) { if(m) m->refs++; }
void athena_mesh3d_release(AthenaMesh3D *m) {
    if(m && --m->refs==0) {
        athena_texture3d_release(m->material.texture);
        free(m->positions); free(m->colors); free(m->normals); free(m->texcoords); free(m);
    }
}
void athena_mesh3d_view(const AthenaMesh3D *m,AthenaMesh3DView *out) {
    *out=(AthenaMesh3DView){.positions=m->positions,.colors=m->colors,.vertex_count=m->count,
        .normals=m->normals,.material=m->material,.texcoords=m->texcoords};
    memcpy(out->minimum,m->minimum,sizeof(m->minimum)); memcpy(out->maximum,m->maximum,sizeof(m->maximum));
}
AthenaInstance3D *athena_instance3d_create(AthenaMesh3D *m) {
    if(!m) return NULL;
    AthenaInstance3D *i=memalign(16,sizeof(*i)); if(!i) return NULL;
    memset(i,0,sizeof(*i)); i->refs=1; i->mesh=m; athena_mesh3d_retain(m);
    i->position.w=1; i->scale=(AthenaVector4){1,1,1,0};
    athena_quaternion_identity(&i->rotation); i->dirty=1; return i;
}
void athena_instance3d_retain(AthenaInstance3D *i) { if(i) i->refs++; }
void athena_instance3d_release(AthenaInstance3D *i) {
    if(i&&--i->refs==0) { athena_mesh3d_release(i->mesh); free(i); }
}
static int finite3(float x,float y,float z) { return athena_float_isfinite(x)&&athena_float_isfinite(y)&&athena_float_isfinite(z); }
int athena_instance3d_set_position(AthenaInstance3D *i,float x,float y,float z) {
    if(!i||!finite3(x,y,z)) return 0;
    i->position=(AthenaVector4){x,y,z,1}; i->dirty=1; return 1;
}
int athena_instance3d_set_scale(AthenaInstance3D *i,float x,float y,float z) {
    if(!i||!finite3(x,y,z)) return 0;
    i->scale=(AthenaVector4){x,y,z,0}; i->dirty=1; return 1;
}
int athena_instance3d_set_rotation(AthenaInstance3D *i,float x,float y,float z,float w) {
    AthenaQuaternion q={x,y,z,w},n;
    if(!i||!athena_quaternion_normalize(&n,&q)) return 0;
    i->rotation=n; i->dirty=1; return 1;
}
int athena_instance3d_set_euler(AthenaInstance3D *i,float x,float y,float z) {
    if(!i||!finite3(x,y,z)) return 0;
    AthenaQuaternion qx,qy,qz,q;
    athena_quaternion_axis_angle(&qx,1,0,0,x);
    athena_quaternion_axis_angle(&qy,0,1,0,y);
    athena_quaternion_axis_angle(&qz,0,0,1,z);
    athena_quaternion_multiply(&q,&qy,&qx); athena_quaternion_multiply(&q,&qz,&q);
    i->rotation=q; i->dirty=1; return 1;
}
void athena_instance3d_get_position(const AthenaInstance3D *i,AthenaVector4 *out) { *out=i->position; }
const AthenaMatrix4 *athena_instance3d_transform(AthenaInstance3D *i) {
    if(i->dirty) {
        if(!athena_quaternion_trs(&i->transform,&i->position,&i->rotation,&i->scale)) return NULL;
        i->dirty=0;
    }
    return &i->transform;
}
const AthenaMesh3D *athena_instance3d_mesh(const AthenaInstance3D *i) { return i->mesh; }
