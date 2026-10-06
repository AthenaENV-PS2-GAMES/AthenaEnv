#include <math.h>
#include <string.h>
#include <athena/float_bits.h>
#include "render3d_shade.h"
int athena_render3d_shade_prepare(AthenaShade3D *out,const AthenaMesh3DView *mesh,
    const AthenaMatrix4 *model,const AthenaLights *lights) {
    if(!out||!mesh||!model) return 0;
    memset(out,0,sizeof(*out));
    if(mesh->material.shading==ATHENA_MATERIAL3D_UNLIT) return 1;
    if(!mesh->normals) return 0;
    /* Float: the R5900 emulates double in software. The cofactor matrix is
     * scale invariant once divided by its largest entry, so scaling the input
     * by its largest entry first keeps every product near 1. */
    float a[3][3],largest=0;
    for(unsigned c=0;c<3;c++) for(unsigned r=0;r<3;r++) {
        float value=model->value[c*4+r]; if(!athena_float_isfinite(value)) return 0;
        a[c][r]=value; if(fabsf(value)>largest) largest=fabsf(value);
    }
    if(largest==0) return 0;
    for(unsigned c=0;c<3;c++) for(unsigned r=0;r<3;r++) a[c][r]/=largest;
    float cofactor[3][3];
    for(unsigned c=0;c<3;c++) {
        unsigned u=(c+1)%3,v=(c+2)%3;
        cofactor[c][0]=a[u][1]*a[v][2]-a[u][2]*a[v][1];
        cofactor[c][1]=a[u][2]*a[v][0]-a[u][0]*a[v][2];
        cofactor[c][2]=a[u][0]*a[v][1]-a[u][1]*a[v][0];
    }
    float det=a[0][0]*cofactor[0][0]+a[0][1]*cofactor[0][1]+a[0][2]*cofactor[0][2];
    if(det==0) return 0;
    float maximum=0;
    for(unsigned c=0;c<3;c++) for(unsigned r=0;r<3;r++)
        if(fabsf(cofactor[c][r])>maximum) maximum=fabsf(cofactor[c][r]);
    if(maximum==0) return 0;
    /* A positive uniform scale cancels when normals are normalized. Preserve
     * determinant sign, including reflected instances with negative scales. */
    float divisor=det<0?-maximum:maximum;
    for(unsigned c=0;c<3;c++) for(unsigned r=0;r<3;r++)
        out->normal_matrix.value[c*4+r]=cofactor[c][r]/divisor;
    /* Prevent VU1 rsqrt from underflowing. TRS columns are orthogonal. */
    for(unsigned c=0;c<3;c++) {
        float squared=0; for(unsigned r=0;r<3;r++) { float v=out->normal_matrix.value[c*4+r]; squared+=v*v; }
        if(squared<1e-12f) return 0;
    }
    athena_lights_view(lights,&out->lights); out->enabled=1; return 1;
}
void athena_render3d_shade_color(const AthenaShade3D *shade,const AthenaPosition3D *normal,
    const AthenaColor3D *color,float out[4]) {
    out[0]=color->r; out[1]=color->g; out[2]=color->b; out[3]=color->a;
    if(!shade||!shade->enabled) return;
    /* The normal matrix columns are at most 1 in magnitude (shade_prepare),
     * so float stays in range for unit-length normals. */
    const float *m=shade->normal_matrix.value;
    float x=m[0]*normal->x+m[4]*normal->y+m[8]*normal->z;
    float y=m[1]*normal->x+m[5]*normal->y+m[9]*normal->z;
    float z=m[2]*normal->x+m[6]*normal->y+m[10]*normal->z;
    float inverse=1/sqrtf(x*x+y*y+z*z); x*=inverse; y*=inverse; z*=inverse;
    float intensity[3]={shade->lights.ambient[0],shade->lights.ambient[1],shade->lights.ambient[2]};
    for(unsigned i=0;i<shade->lights.count;i++) {
        const float *d=shade->lights.direction[i],*rgb=shade->lights.diffuse[i];
        float dot=x*d[0]+y*d[1]+z*d[2];
        if(dot>0) for(unsigned j=0;j<3;j++) intensity[j]+=dot*rgb[j];
    }
    for(unsigned j=0;j<3;j++) { float v=out[j]*intensity[j]; out[j]=v<255?v:255; }
}
