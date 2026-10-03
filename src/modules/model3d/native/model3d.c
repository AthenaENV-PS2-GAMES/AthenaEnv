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
        case ATHENA_MODEL3D_EFORMAT: return "Malformed model";
        case ATHENA_MODEL3D_EUNSUPPORTED: return "Model feature is not supported by the static color pipeline";
        default: return "OK";
    }
}
/* R5900 GCC assumes hardware floats cannot be NaN/Inf and folds isfinite()
 * to true. TypedArray input still carries arbitrary IEEE bits: inspect those. */

AthenaGeometry3DIssue athena_geometry3d_validate(const AthenaGeometry3D *g,uint32_t *offset) {
    if(offset) *offset=0;
    if(!g||!g->positions||g->vertex_count==0||g->vertex_count>ATHENA_MODEL3D_MAX_VERTICES)
        return ATHENA_GEOMETRY3D_VERTEX_COUNT;
    if((g->colors && g->color_count!=g->vertex_count)||(!g->colors && g->color_count))
        return ATHENA_GEOMETRY3D_COLOR_COUNT;
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
    return ATHENA_GEOMETRY3D_VALID;
}
int athena_mesh3d_create(const AthenaGeometry3D *g,AthenaMesh3D **out) {
    if(!out) return ATHENA_MODEL3D_EINVAL;
    *out=NULL;
    if(athena_geometry3d_validate(g,NULL)!=ATHENA_GEOMETRY3D_VALID) return ATHENA_MODEL3D_EINVAL;
    uint32_t count=g->indices?g->index_count:g->vertex_count;
    AthenaMesh3D *m=calloc(1,sizeof(*m)); if(!m) return ATHENA_MODEL3D_ENOMEM;
    m->positions=memalign(16,(count+4)*sizeof(*m->positions));
    m->colors=memalign(16,(count+4)*sizeof(*m->colors));
    if(!m->positions||!m->colors) { free(m->positions); free(m->colors); free(m); return ATHENA_MODEL3D_ENOMEM; }
    memset(m->positions,0,(count+4)*sizeof(*m->positions));
    memset(m->colors,0,(count+4)*sizeof(*m->colors));
    for(uint32_t i=0;i<count;i++) {
        uint32_t source=g->indices?g->indices[i]:i;
        const float *p=&g->positions[source*3];
        m->positions[i]=(AthenaPosition3D){p[0],p[1],p[2]};
        if(g->colors) {
            const float *c=&g->colors[source*4];
            m->colors[i]=(AthenaColor3D){lroundf(c[0]*255),lroundf(c[1]*255),lroundf(c[2]*255),lroundf(c[3]*255)};
        } else m->colors[i]=(AthenaColor3D){255,255,255,255};
        for(int j=0;j<3;j++) {
            if(i==0||p[j]<m->minimum[j]) m->minimum[j]=p[j];
            if(i==0||p[j]>m->maximum[j]) m->maximum[j]=p[j];
        }
    }
    m->refs=1; m->count=count; *out=m; return 0;
}
void athena_mesh3d_retain(AthenaMesh3D *m) { if(m) m->refs++; }
void athena_mesh3d_release(AthenaMesh3D *m) {
    if(m && --m->refs==0) { free(m->positions); free(m->colors); free(m); }
}
void athena_mesh3d_view(const AthenaMesh3D *m,AthenaMesh3DView *out) {
    *out=(AthenaMesh3DView){.positions=m->positions,.colors=m->colors,.vertex_count=m->count};
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
