#include <math.h>
#include <string.h>
#include <athena/audio3d.h>
#include <athena/float_bits.h>
void athena_audio3d_default_params(AthenaAudio3DParams *p) {
    p->min_distance=1; p->max_distance=30; p->rolloff=ATHENA_AUDIO3D_INVERSE; p->volume=100; p->pan_strength=.8f;
}
int athena_audio3d_validate(const AthenaAudio3DParams *p) {
    return p&&athena_float_isfinite(p->min_distance)&&athena_float_isfinite(p->max_distance)&&p->min_distance>0&&
        p->max_distance>p->min_distance&&(p->rolloff==ATHENA_AUDIO3D_INVERSE||p->rolloff==ATHENA_AUDIO3D_LINEAR)&&
        athena_float_isfinite(p->volume)&&p->volume>=0&&p->volume<=100&&athena_float_isfinite(p->pan_strength)&&
        p->pan_strength>=0&&p->pan_strength<=1;
}
int athena_audio3d_listener_from_camera(AthenaCamera3D *c,AthenaAudio3DListener *out) {
    if(!c||!out) return -1;
    const float *v=c->view.value;
    out->position[0]=c->position.x; out->position[1]=c->position.y; out->position[2]=c->position.z;
    out->right[0]=v[0]; out->right[1]=v[4]; out->right[2]=v[8];
    return 0;
}
float athena_audio3d_gain(const AthenaAudio3DParams *p,float d) {
    if(d<=p->min_distance) return 1;
    if(d>=p->max_distance) return 0;
    if(p->rolloff==ATHENA_AUDIO3D_LINEAR) return (p->max_distance-d)/(p->max_distance-p->min_distance);
    float g=p->min_distance/d,fade_start=p->max_distance*.9f;
    if(d>fade_start) g*=(p->max_distance-d)/(p->max_distance-fade_start);
    return g;
}
float athena_audio3d_levels(const AthenaAudio3DListener *l,const float s[3],const AthenaAudio3DParams *p,int *volume,int *pan) {
    float dx=s[0]-l->position[0],dy=s[1]-l->position[1],dz=s[2]-l->position[2],d=sqrtf(dx*dx+dy*dy+dz*dz);
    float g=athena_audio3d_gain(p,d),v=g*p->volume;
    float side=d>1e-4f?(dx*l->right[0]+dy*l->right[1]+dz*l->right[2])/d:0;
    float pn=side*100*p->pan_strength;
    if(!athena_float_isfinite(v)) v=0;
    if(!athena_float_isfinite(pn)) pn=0;
    int iv=(int)(v+.5f),ip=(int)(pn<0?pn-.5f:pn+.5f);
    *volume=iv<0?0:iv>100?100:iv;
    *pan=ip<-100?-100:ip>100?100:ip;
    return d;
}
