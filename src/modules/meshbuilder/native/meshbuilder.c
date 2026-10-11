#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <athena/meshbuilder.h>
#include <athena/float_bits.h>
/* Per mesh: indices and vertices both stay within Model3D's limit. */
#define PART_LIMIT ATHENA_MODEL3D_MAX_VERTICES
struct AthenaMeshBuilder {
    float *pos,*nrm,*col,*uv;   /* 3, 3, 4, 2 floats per vertex */
    uint32_t *idx;
    uint32_t vcount,vcap,icount,icap;
    float color[4];
    AthenaMatrix4 m; float nm[9]; int identity;
    float uv0[2],uv1[2];
};
AthenaMeshBuilder *athena_meshbuilder_create(void) {
    AthenaMeshBuilder *b=calloc(1,sizeof(*b)); if(!b) return NULL;
    b->color[0]=b->color[1]=b->color[2]=b->color[3]=1;
    athena_meshbuilder_set_transform(b,NULL);
    b->uv1[0]=b->uv1[1]=1;
    return b;
}
void athena_meshbuilder_destroy(AthenaMeshBuilder *b) {
    if(!b) return;
    free(b->pos); free(b->nrm); free(b->col); free(b->uv); free(b->idx); free(b);
}
void athena_meshbuilder_clear(AthenaMeshBuilder *b) { if(b) b->vcount=b->icount=0; }
static int finite_n(const float *v,int n) { for(int i=0;i<n;i++) if(!athena_float_isfinite(v[i])) return 0; return 1; }
int athena_meshbuilder_set_color(AthenaMeshBuilder *b,const float c[4]) {
    if(!b||!c||!finite_n(c,4)) return ATHENA_MESHBUILDER_EINVAL;
    for(int i=0;i<4;i++) if(c[i]<0||c[i]>1) return ATHENA_MESHBUILDER_EINVAL;
    memcpy(b->color,c,sizeof(b->color)); return 0;
}
int athena_meshbuilder_set_transform(AthenaMeshBuilder *b,const AthenaMatrix4 *m) {
    if(!b) return ATHENA_MESHBUILDER_EINVAL;
    if(!m) {
        ath_matrix4_identity(&b->m); b->identity=1;
        static const float id[9]={1,0,0,0,1,0,0,0,1}; memcpy(b->nm,id,sizeof(id)); return 0;
    }
    if(!finite_n(m->value,16)) return ATHENA_MESHBUILDER_EINVAL;
    /* Inverse transpose of the linear part (column-major a[c*4+r]). */
    const float *a=m->value;
    float c00=a[5]*a[10]-a[9]*a[6],c01=a[9]*a[2]-a[1]*a[10],c02=a[1]*a[6]-a[5]*a[2];
    float det=a[0]*c00+a[4]*c01+a[8]*c02;
    if(!(fabsf(det)>1e-20f)||!athena_float_isfinite(det)) return ATHENA_MESHBUILDER_EINVAL;
    /* Cofactor matrix / det = inverse transpose. nm row-major: n' = nm * n. */
    float inv=1/det;
    b->nm[0]=c00*inv; b->nm[1]=(a[6]*a[8]-a[4]*a[10])*inv; b->nm[2]=(a[4]*a[9]-a[8]*a[5])*inv;
    b->nm[3]=c01*inv; b->nm[4]=(a[0]*a[10]-a[8]*a[2])*inv; b->nm[5]=(a[8]*a[1]-a[0]*a[9])*inv;
    b->nm[6]=c02*inv; b->nm[7]=(a[4]*a[2]-a[0]*a[6])*inv; b->nm[8]=(a[0]*a[5]-a[4]*a[1])*inv;
    b->m=*m; b->identity=0; return 0;
}
int athena_meshbuilder_set_uv_rect(AthenaMeshBuilder *b,float u0,float v0,float u1,float v1) {
    float r[4]={u0,v0,u1,v1};
    if(!b||!finite_n(r,4)) return ATHENA_MESHBUILDER_EINVAL;
    for(int i=0;i<4;i++) if(fabsf(r[i])>ATHENA_MODEL3D_UV_LIMIT) return ATHENA_MESHBUILDER_EINVAL;
    b->uv0[0]=u0; b->uv0[1]=v0; b->uv1[0]=u1; b->uv1[1]=v1; return 0;
}
/* Doubling capacity for need elements: cap when it already fits, 0 on overflow. */
static uint32_t next_cap(uint32_t cap,uint32_t need) {
    if(need<=cap) return cap;
    uint32_t n=cap?cap:256;
    while(n<need) { if(n>0x40000000u) return 0; n*=2; }
    return n;
}
static int reserve_vertices(AthenaMeshBuilder *b,uint32_t extra) {
    if(extra>0x7FFFFFFFu-b->vcount) return 0;
    uint32_t cap=next_cap(b->vcap,b->vcount+extra);
    if(!cap) return 0;
    if(cap==b->vcap) return 1;
    /* A failure leaves vcap as it was: the larger arrays are kept and reused. */
    float *p=realloc(b->pos,(size_t)cap*12); if(!p) return 0; b->pos=p;
    p=realloc(b->nrm,(size_t)cap*12); if(!p) return 0; b->nrm=p;
    p=realloc(b->col,(size_t)cap*16); if(!p) return 0; b->col=p;
    p=realloc(b->uv,(size_t)cap*8); if(!p) return 0; b->uv=p;
    b->vcap=cap; return 1;
}
static int reserve_indices(AthenaMeshBuilder *b,uint32_t extra) {
    if(extra>0x7FFFFFFFu-b->icount) return 0;
    uint32_t cap=next_cap(b->icap,b->icount+extra);
    if(!cap) return 0;
    if(cap==b->icap) return 1;
    uint32_t *p=realloc(b->idx,(size_t)cap*4); if(!p) return 0;
    b->idx=p; b->icap=cap; return 1;
}
/* Appends without checks (space reserved). */
static uint32_t put(AthenaMeshBuilder *b,const float p[3],const float n[3],const float c[4],float u,float v) {
    uint32_t i=b->vcount++;
    float *dp=&b->pos[i*3],*dn=&b->nrm[i*3];
    /* Plain stores: small memcpy() calls are not inlined on the EE. */
    if(b->identity) {
        dp[0]=p[0]; dp[1]=p[1]; dp[2]=p[2];
        if(n) { dn[0]=n[0]; dn[1]=n[1]; dn[2]=n[2]; } else { dn[0]=0; dn[1]=1; dn[2]=0; }
    }
    else {
        const float *m=b->m.value;
        for(int r=0;r<3;r++) dp[r]=m[r]*p[0]+m[4+r]*p[1]+m[8+r]*p[2]+m[12+r];
        float src[3]={0,1,0}; if(n) { src[0]=n[0]; src[1]=n[1]; src[2]=n[2]; }
        float len=0;
        for(int r=0;r<3;r++) { dn[r]=b->nm[r*3]*src[0]+b->nm[r*3+1]*src[1]+b->nm[r*3+2]*src[2]; len+=dn[r]*dn[r]; }
        len=sqrtf(len);
        if(len>0) for(int r=0;r<3;r++) dn[r]/=len; else { dn[0]=0; dn[1]=1; dn[2]=0; }
    }
    float *dc=&b->col[i*4]; dc[0]=c[0]; dc[1]=c[1]; dc[2]=c[2]; dc[3]=c[3];
    b->uv[i*2]=b->uv0[0]+(b->uv1[0]-b->uv0[0])*u;
    b->uv[i*2+1]=b->uv0[1]+(b->uv1[1]-b->uv0[1])*v;
    return i;
}
static void tri(AthenaMeshBuilder *b,uint32_t a,uint32_t c,uint32_t d) {
    b->idx[b->icount++]=a; b->idx[b->icount++]=c; b->idx[b->icount++]=d;
}
int athena_meshbuilder_reserve(AthenaMeshBuilder *b,uint32_t vertices,uint32_t indices) {
    return reserve_vertices(b,vertices)&&reserve_indices(b,indices)?0:ATHENA_MESHBUILDER_ENOMEM;
}
uint32_t athena_meshbuilder_put_unchecked(AthenaMeshBuilder *b,const float p[3],const float n[3],const float rgba[4],float u,float v) {
    return put(b,p,n,rgba,u,v);
}
void athena_meshbuilder_triangle_unchecked(AthenaMeshBuilder *b,uint32_t i0,uint32_t i1,uint32_t i2) { tri(b,i0,i1,i2); }
int64_t athena_meshbuilder_vertex_color(AthenaMeshBuilder *b,const float p[3],const float n[3],const float c[4],float u,float v) {
    if(!b||!p||!c||!finite_n(p,3)||(n&&!finite_n(n,3))||!finite_n(c,4)||!athena_float_isfinite(u)||!athena_float_isfinite(v))
        return ATHENA_MESHBUILDER_EINVAL;
    if(!reserve_vertices(b,1)) return ATHENA_MESHBUILDER_ENOMEM;
    float cc[4]; for(int i=0;i<4;i++) cc[i]=c[i]<0?0:c[i]>1?1:c[i];
    return put(b,p,n,cc,u,v);
}
int64_t athena_meshbuilder_vertex(AthenaMeshBuilder *b,const float p[3],const float n[3],float u,float v) {
    if(!b) return ATHENA_MESHBUILDER_EINVAL;
    return athena_meshbuilder_vertex_color(b,p,n,b->color,u,v);
}
int athena_meshbuilder_triangle(AthenaMeshBuilder *b,uint32_t i0,uint32_t i1,uint32_t i2) {
    if(!b||i0>=b->vcount||i1>=b->vcount||i2>=b->vcount) return ATHENA_MESHBUILDER_EINVAL;
    if(!reserve_indices(b,3)) return ATHENA_MESHBUILDER_ENOMEM;
    tri(b,i0,i1,i2); return 0;
}
static int reserve(AthenaMeshBuilder *b,uint32_t v,uint32_t i) {
    return reserve_vertices(b,v)&&reserve_indices(b,i)?0:ATHENA_MESHBUILDER_ENOMEM;
}
int athena_meshbuilder_quad(AthenaMeshBuilder *b,const float p0[3],const float p1[3],const float p2[3],const float p3[3]) {
    if(!b||!p0||!p1||!p2||!p3||!finite_n(p0,3)||!finite_n(p1,3)||!finite_n(p2,3)||!finite_n(p3,3)) return ATHENA_MESHBUILDER_EINVAL;
    if(reserve(b,4,6)) return ATHENA_MESHBUILDER_ENOMEM;
    float e1[3],e2[3],n[3];
    for(int i=0;i<3;i++) { e1[i]=p1[i]-p0[i]; e2[i]=p2[i]-p0[i]; }
    n[0]=e1[1]*e2[2]-e1[2]*e2[1]; n[1]=e1[2]*e2[0]-e1[0]*e2[2]; n[2]=e1[0]*e2[1]-e1[1]*e2[0];
    float len=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
    if(len>0) for(int i=0;i<3;i++) n[i]/=len; else { n[0]=0; n[1]=1; n[2]=0; }
    uint32_t a=put(b,p0,n,b->color,0,1),c=put(b,p1,n,b->color,1,1),d=put(b,p2,n,b->color,1,0),e=put(b,p3,n,b->color,0,0);
    tri(b,a,c,d); tri(b,a,d,e); return 0;
}
/* Face frames: normal, u, v with u x v = normal (CCW seen from outside). */
static const float FACES[6][3][3]={
    {{1,0,0},{0,0,-1},{0,1,0}}, {{-1,0,0},{0,0,1},{0,1,0}},
    {{0,1,0},{1,0,0},{0,0,-1}}, {{0,-1,0},{1,0,0},{0,0,1}},
    {{0,0,1},{1,0,0},{0,1,0}},  {{0,0,-1},{-1,0,0},{0,1,0}}};
int athena_meshbuilder_box(AthenaMeshBuilder *b,const float lo[3],const float hi[3]) {
    if(!b||!lo||!hi||!finite_n(lo,3)||!finite_n(hi,3)) return ATHENA_MESHBUILDER_EINVAL;
    for(int i=0;i<3;i++) if(lo[i]>hi[i]) return ATHENA_MESHBUILDER_EINVAL;
    if(reserve(b,24,36)) return ATHENA_MESHBUILDER_ENOMEM;
    float mid[3],half[3];
    for(int i=0;i<3;i++) { mid[i]=(lo[i]+hi[i])*.5f; half[i]=(hi[i]-lo[i])*.5f; }
    static const float corner[4][2]={{-1,-1},{1,-1},{1,1},{-1,1}},uvs[4][2]={{0,1},{1,1},{1,0},{0,0}};
    for(int f=0;f<6;f++) {
        const float *n=FACES[f][0],*u=FACES[f][1],*v=FACES[f][2];
        uint32_t first=b->vcount;
        for(int k=0;k<4;k++) {
            float p[3];
            for(int i=0;i<3;i++) p[i]=mid[i]+(n[i]+u[i]*corner[k][0]+v[i]*corner[k][1])*half[i];
            put(b,p,n,b->color,uvs[k][0],uvs[k][1]);
        }
        tri(b,first,first+1,first+2); tri(b,first,first+2,first+3);
    }
    return 0;
}
int athena_meshbuilder_sphere(AthenaMeshBuilder *b,const float c[3],float radius,uint32_t segments,uint32_t rings) {
    if(!b||!c||!finite_n(c,3)||!athena_float_isfinite(radius)||radius<0||segments<3||rings<2||
        segments>ATHENA_MESHBUILDER_MAX_SEGMENTS||rings>ATHENA_MESHBUILDER_MAX_SEGMENTS) return ATHENA_MESHBUILDER_EINVAL;
    if(reserve(b,(segments+1)*(rings+1),segments*rings*6)) return ATHENA_MESHBUILDER_ENOMEM;
    uint32_t first=b->vcount;
    for(uint32_t i=0;i<=rings;i++) {
        float t=3.14159265f*(float)i/(float)rings,st=sinf(t),ct=cosf(t);
        for(uint32_t j=0;j<=segments;j++) {
            float p=6.28318531f*(float)j/(float)segments;
            float n[3]={st*cosf(p),ct,st*sinf(p)},q[3]={c[0]+radius*n[0],c[1]+radius*n[1],c[2]+radius*n[2]};
            put(b,q,n,b->color,(float)j/(float)segments,(float)i/(float)rings);
        }
    }
    for(uint32_t i=0;i<rings;i++) for(uint32_t j=0;j<segments;j++) {
        uint32_t a=first+i*(segments+1)+j,d=a+segments+1;
        tri(b,a,a+1,d); tri(b,a+1,d+1,d);
    }
    return 0;
}
int athena_meshbuilder_cylinder(AthenaMeshBuilder *b,const float base[3],float radius,float height,uint32_t segments,int caps) {
    if(!b||!base||!finite_n(base,3)||!athena_float_isfinite(radius)||!athena_float_isfinite(height)||radius<0||
        segments<3||segments>ATHENA_MESHBUILDER_MAX_SEGMENTS) return ATHENA_MESHBUILDER_EINVAL;
    uint32_t verts=(segments+1)*2+(caps?2*(segments+2):0),indices=segments*6+(caps?segments*6:0);
    if(reserve(b,verts,indices)) return ATHENA_MESHBUILDER_ENOMEM;
    uint32_t first=b->vcount;
    for(uint32_t j=0;j<=segments;j++) {
        float p=6.28318531f*(float)j/(float)segments,cx=cosf(p),sz=sinf(p);
        float n[3]={cx,0,sz};
        float lo[3]={base[0]+radius*cx,base[1],base[2]+radius*sz},hi[3]={lo[0],base[1]+height,lo[2]};
        put(b,lo,n,b->color,(float)j/(float)segments,1); put(b,hi,n,b->color,(float)j/(float)segments,0);
    }
    for(uint32_t j=0;j<segments;j++) {
        uint32_t lo=first+j*2,hi=lo+1,lo2=lo+2,hi2=lo+3;
        tri(b,lo,hi,lo2); tri(b,lo2,hi,hi2);
    }
    if(caps) for(int top=0;top<2;top++) {
        float n[3]={0,top?1.0f:-1.0f,0},y=base[1]+(top?height:0);
        float centre[3]={base[0],y,base[2]};
        uint32_t c=put(b,centre,n,b->color,.5f,.5f),ring=b->vcount;
        for(uint32_t j=0;j<=segments;j++) {
            float p=6.28318531f*(float)j/(float)segments,cx=cosf(p),sz=sinf(p);
            float q[3]={base[0]+radius*cx,y,base[2]+radius*sz};
            put(b,q,n,b->color,.5f+.5f*cx,.5f+.5f*sz);
        }
        for(uint32_t j=0;j<segments;j++) {
            if(top) tri(b,c,ring+j+1,ring+j); else tri(b,c,ring+j,ring+j+1);
        }
    }
    return 0;
}
int athena_meshbuilder_plane(AthenaMeshBuilder *b,const float c[3],float width,float depth,uint32_t nx,uint32_t nz) {
    if(!b||!c||!finite_n(c,3)||!athena_float_isfinite(width)||!athena_float_isfinite(depth)||width<0||depth<0||
        !nx||!nz||nx>ATHENA_MESHBUILDER_MAX_SEGMENTS||nz>ATHENA_MESHBUILDER_MAX_SEGMENTS) return ATHENA_MESHBUILDER_EINVAL;
    if(reserve(b,(nx+1)*(nz+1),nx*nz*6)) return ATHENA_MESHBUILDER_ENOMEM;
    static const float up[3]={0,1,0};
    uint32_t first=b->vcount;
    for(uint32_t k=0;k<=nz;k++) for(uint32_t i=0;i<=nx;i++) {
        float p[3]={c[0]-width*.5f+width*(float)i/(float)nx,c[1],c[2]-depth*.5f+depth*(float)k/(float)nz};
        put(b,p,up,b->color,(float)i/(float)nx,(float)k/(float)nz);
    }
    for(uint32_t k=0;k<nz;k++) for(uint32_t i=0;i<nx;i++) {
        uint32_t d=first+k*(nx+1)+i,cc=d+1,a=d+nx+1,bb=a+1;
        tri(b,a,bb,cc); tri(b,a,cc,d);
    }
    return 0;
}
int athena_meshbuilder_heightmap(AthenaMeshBuilder *b,const float *h,uint32_t w,uint32_t d,float cell,float scale,
    const float o[3],const float low[4],const float high[4]) {
    if(!b||!h||!o||w<2||d<2||w>1024||d>1024||!athena_float_isfinite(cell)||cell<=0||!athena_float_isfinite(scale)||
        !finite_n(o,3)||(!low)!=(!high)||(low&&(!finite_n(low,4)||!finite_n(high,4)))) return ATHENA_MESHBUILDER_EINVAL;
    float hmin=h[0],hmax=h[0];
    for(uint32_t i=0;i<w*d;i++) {
        if(!athena_float_isfinite(h[i])) return ATHENA_MESHBUILDER_EINVAL;
        if(h[i]<hmin) hmin=h[i];
        if(h[i]>hmax) hmax=h[i];
    }
    if(reserve(b,w*d,(w-1)*(d-1)*6)) return ATHENA_MESHBUILDER_ENOMEM;
    uint32_t first=b->vcount;
    for(uint32_t k=0;k<d;k++) for(uint32_t i=0;i<w;i++) {
        float hl=h[k*w+(i?i-1:i)],hr=h[k*w+(i+1<w?i+1:i)],hu=h[(k?k-1:k)*w+i],hd=h[(k+1<d?k+1:k)*w+i];
        float sx=(float)((i+1<w?i+1:i)-(i?i-1:i))*cell,sz=(float)((k+1<d?k+1:k)-(k?k-1:k))*cell;
        float n[3]={-(hr-hl)*scale/sx,1,-(hd-hu)*scale/sz},len=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        for(int r=0;r<3;r++) n[r]/=len;
        float p[3]={o[0]+(float)i*cell,o[1]+h[k*w+i]*scale,o[2]+(float)k*cell};
        float col[4];
        if(low) {
            float t=hmax>hmin?(h[k*w+i]-hmin)/(hmax-hmin):0;
            for(int r=0;r<4;r++) { col[r]=low[r]+(high[r]-low[r])*t; col[r]=col[r]<0?0:col[r]>1?1:col[r]; }
        } else memcpy(col,b->color,16);
        put(b,p,n,col,(float)i/(float)(w-1),(float)k/(float)(d-1));
    }
    for(uint32_t k=0;k+1<d;k++) for(uint32_t i=0;i+1<w;i++) {
        uint32_t dd=first+k*w+i,cc=dd+1,a=dd+w,bb=a+1;
        tri(b,a,bb,cc); tri(b,a,cc,dd);
    }
    return 0;
}

/* ---- Extrusion ---- */
static const char *extrude_detail="";
const char *athena_meshbuilder_error_detail(void) { return extrude_detail; }
static int reject(const char *why) { extrude_detail=why; return ATHENA_MESHBUILDER_EINVAL; }
/* y component of (b - a) x (c - a): positive when a, b, c turn counter-clockwise seen from +Y. */
static float orient2(const float *a,const float *b,const float *c) {
    return (b[1]-a[1])*(c[0]-a[0])-(b[0]-a[0])*(c[1]-a[1]);
}
static int on_segment(const float *a,const float *b,const float *p) {   /* p collinear with a-b */
    return p[0]>=fminf(a[0],b[0])&&p[0]<=fmaxf(a[0],b[0])&&p[1]>=fminf(a[1],b[1])&&p[1]<=fmaxf(a[1],b[1]);
}
static int segments_touch(const float *a,const float *b,const float *c,const float *d) {
    float o1=orient2(a,b,c),o2=orient2(a,b,d),o3=orient2(c,d,a),o4=orient2(c,d,b);
    if(((o1>0&&o2<0)||(o1<0&&o2>0))&&((o3>0&&o4<0)||(o3<0&&o4>0))) return 1;
    if(o1==0&&on_segment(a,b,c)) return 1;
    if(o2==0&&on_segment(a,b,d)) return 1;
    if(o3==0&&on_segment(c,d,a)) return 1;
    if(o4==0&&on_segment(c,d,b)) return 1;
    return 0;
}
/* Points are stored as (x, z) pairs, so "2D" below is the (x, z) plane and orient2 is the y of the 3D cross product. */
static int in_triangle(const float *a,const float *b,const float *c,const float *p) {
    return orient2(a,b,p)>=0&&orient2(b,c,p)>=0&&orient2(c,a,p)>=0;
}
int athena_meshbuilder_extrude(AthenaMeshBuilder *b,const float *xz,uint32_t count,float y,const float ext[3],uint32_t flags) {
    extrude_detail="";
    if(!b||!xz||!ext) return reject("missing argument");
    if(count<3) return reject("a contour needs at least 3 points");
    if(count>ATHENA_MESHBUILDER_MAX_CONTOUR) return reject("too many contour points");
    if(flags&~(ATHENA_EXTRUDE_CAP_START|ATHENA_EXTRUDE_CAP_END|ATHENA_EXTRUDE_SMOOTH)) return reject("unknown flags");
    if(!athena_float_isfinite(y)||!finite_n(ext,3)||!finite_n(xz,(int)(count*2))) return reject("non-finite value");
    float elen=sqrtf(ext[0]*ext[0]+ext[1]*ext[1]+ext[2]*ext[2]);
    if(!(fabsf(ext[1])>1e-6f*elen)||!(elen>0)||!athena_float_isfinite(elen)) return reject("the extrusion must leave the contour plane (y component not zero)");
    float minx=xz[0],maxx=xz[0],minz=xz[1],maxz=xz[1];
    for(uint32_t i=1;i<count;i++) {
        if(xz[i*2]<minx) minx=xz[i*2];
        if(xz[i*2]>maxx) maxx=xz[i*2];
        if(xz[i*2+1]<minz) minz=xz[i*2+1];
        if(xz[i*2+1]>maxz) maxz=xz[i*2+1];
    }
    float width=maxx-minx,depth=maxz-minz,scale=width>depth?width:depth;
    if(!(scale>0)||!(width>0)||!(depth>0)) return reject("degenerate contour (zero width or depth)");
    if(!athena_float_isfinite(scale)) return reject("non-finite value");
    float eps=scale*1e-6f;
    /* Counter-clockwise (normal +Y) working copy, via Newell's area (accumulated in double: the sum cancels). */
    double area=0;
    for(uint32_t i=0;i<count;i++) { uint32_t j=(i+1)%count; area+=((double)xz[i*2+1]-xz[j*2+1])*((double)xz[i*2]+xz[j*2]); }
    if(!(fabs(area)>1e-9*(double)scale*scale)) return reject("zero area");
    float *pts=malloc((size_t)count*2*sizeof(float)); uint32_t *idx=malloc((size_t)count*sizeof(uint32_t)*4);
    if(!pts||!idx) { free(pts); free(idx); return ATHENA_MESHBUILDER_ENOMEM; }
    int ccw=area>0;
    for(uint32_t i=0;i<count;i++) { uint32_t k=ccw?i:count-1-i; pts[i*2]=xz[k*2]; pts[i*2+1]=xz[k*2+1]; }
    int code=0;
    /* Repeated points and spikes. */
    for(uint32_t i=0;i<count&&!code;i++) {
        const float *a=&pts[((i+count-1)%count)*2],*c=&pts[i*2],*d=&pts[((i+1)%count)*2];
        float abx=c[0]-a[0],abz=c[1]-a[1],bcx=d[0]-c[0],bcz=d[1]-c[1];
        float lab=sqrtf(abx*abx+abz*abz),lbc=sqrtf(bcx*bcx+bcz*bcz);
        if(lbc<=eps) { code=reject("repeated consecutive points (do not repeat the first point at the end)"); break; }
        if(lab>eps&&fabsf(abx*bcz-abz*bcx)<=1e-6f*lab*lbc&&abx*bcx+abz*bcz<0) code=reject("the outline doubles back on itself (a spike)");
    }
    /* Edges may not cross or touch, except neighbours at their shared point. */
    for(uint32_t i=0;i<count&&!code;i++) for(uint32_t j=i+2;j<count;j++) {
        if(i==0&&j==count-1) continue;
        if(segments_touch(&pts[i*2],&pts[((i+1)%count)*2],&pts[j*2],&pts[((j+1)%count)*2])) { code=reject("the outline crosses or touches itself"); break; }
    }
    uint32_t *cap=idx,*tris=idx+count,ntris=0,ncap=0;   /* tris: up to 3 * (count - 2) indices */
    if(!code) {
        for(uint32_t i=0;i<count;i++) {
            const float *a=&pts[((i+count-1)%count)*2],*c=&pts[i*2],*d=&pts[((i+1)%count)*2];
            float abx=c[0]-a[0],abz=c[1]-a[1],bcx=d[0]-c[0],bcz=d[1]-c[1];
            float lab=sqrtf(abx*abx+abz*abz),lbc=sqrtf(bcx*bcx+bcz*bcz);
            if(fabsf(abx*bcz-abz*bcx)<=1e-6f*lab*lbc) continue;   /* on a straight edge: not part of the caps */
            cap[ncap++]=i;
        }
        if(ncap<3) code=reject("zero area");
    }
    if(!code) {
        /* Ear clipping on the remaining ring (cap[] shrinks as ears are cut). */
        uint32_t n=ncap,guard=0;
        uint32_t i=0;
        while(n>3&&guard<=n*n+8) {
            uint32_t ip=(i+n-1)%n,in=(i+1)%n;
            const float *a=&pts[cap[ip]*2],*c=&pts[cap[i]*2],*d=&pts[cap[in]*2];
            int ear=orient2(a,c,d)>1e-9f*scale*scale;
            for(uint32_t k=0;ear&&k<n;k++) {
                if(k==ip||k==i||k==in) continue;
                if(in_triangle(a,c,d,&pts[cap[k]*2])) ear=0;
            }
            if(ear) {
                tris[ntris++]=cap[ip]; tris[ntris++]=cap[i]; tris[ntris++]=cap[in];
                for(uint32_t k=i;k+1<n;k++) cap[k]=cap[k+1];
                n--; guard=0; if(i>=n) i=0;
            } else { i=(i+1)%n; guard++; }
        }
        if(n==3) { tris[ntris++]=cap[0]; tris[ntris++]=cap[1]; tris[ntris++]=cap[2]; }
        else code=reject("the outline could not be triangulated");
    }
    if(code) { free(pts); free(idx); return code; }
    int smooth=(flags&ATHENA_EXTRUDE_SMOOTH)!=0,cap_start=(flags&ATHENA_EXTRUDE_CAP_START)!=0,cap_end=(flags&ATHENA_EXTRUDE_CAP_END)!=0;
    uint32_t side_verts=smooth?(count+1)*2:count*4,verts=side_verts+((cap_start?1:0)+(cap_end?1:0))*count;
    uint32_t indices=count*6+((cap_start?1:0)+(cap_end?1:0))*ntris;
    if(reserve(b,verts,indices)) { free(pts); free(idx); return ATHENA_MESHBUILDER_ENOMEM; }
    /* Edge frames: wall normals pointing away from the interior, and u along the contour. */
    float *wn=malloc((size_t)count*3*sizeof(float)),*cum=malloc((size_t)(count+1)*sizeof(float));
    if(!wn||!cum) { free(wn); free(cum); free(pts); free(idx); return ATHENA_MESHBUILDER_ENOMEM; }
    cum[0]=0;
    for(uint32_t i=0;i<count;i++) {
        const float *a=&pts[i*2],*c=&pts[((i+1)%count)*2];
        float d[3]={c[0]-a[0],0,c[1]-a[1]};
        float o[3]={-d[2],0,d[0]};                               /* d x (0,1,0): outward for a counter-clockwise outline */
        float n[3]={d[1]*ext[2]-d[2]*ext[1],d[2]*ext[0]-d[0]*ext[2],d[0]*ext[1]-d[1]*ext[0]},len=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        for(int k=0;k<3;k++) n[k]/=len;
        if(n[0]*o[0]+n[1]*o[1]+n[2]*o[2]<0) for(int k=0;k<3;k++) n[k]=-n[k];
        memcpy(&wn[i*3],n,sizeof(n));
        cum[i+1]=cum[i]+sqrtf(d[0]*d[0]+d[2]*d[2]);
    }
    float total=cum[count];
    uint32_t first;
    /* A wall wound so that its front faces outward whichever way the extrusion points. */
    #define WALL_TRI(p,q,r,flip) do { if(flip) tri(b,p,r,q); else tri(b,p,q,r); } while(0)
    if(smooth) {
        first=b->vcount;
        for(uint32_t i=0;i<=count;i++) {
            uint32_t k=i%count,pk=(k+count-1)%count;
            float n[3]={wn[k*3]+wn[pk*3],wn[k*3+1]+wn[pk*3+1],wn[k*3+2]+wn[pk*3+2]},len=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
            if(len>1e-6f) for(int r=0;r<3;r++) n[r]/=len; else memcpy(n,&wn[k*3],sizeof(n));
            float lo[3]={pts[k*2],y,pts[k*2+1]},hi[3]={lo[0]+ext[0],lo[1]+ext[1],lo[2]+ext[2]},u=cum[i]/total;
            put(b,lo,n,b->color,u,1); put(b,hi,n,b->color,u,0);
        }
        for(uint32_t i=0;i<count;i++) {
            const float *a=&pts[i*2],*c=&pts[((i+1)%count)*2];
            float d[3]={c[0]-a[0],0,c[1]-a[1]},cr[3]={d[1]*ext[2]-d[2]*ext[1],d[2]*ext[0]-d[0]*ext[2],d[0]*ext[1]-d[1]*ext[0]};
            int flip=cr[0]*(-d[2])+cr[2]*d[0]<0;
            uint32_t a0=first+i*2,a1=a0+1,b0=a0+2,b1=a0+3;       /* base/far of the edge's start and end */
            WALL_TRI(a0,b0,b1,flip); WALL_TRI(a0,b1,a1,flip);
        }
    } else {
        for(uint32_t i=0;i<count;i++) {
            const float *a=&pts[i*2],*c=&pts[((i+1)%count)*2];
            float la[3]={a[0],y,a[1]},lb[3]={c[0],y,c[1]};
            float ha[3]={la[0]+ext[0],la[1]+ext[1],la[2]+ext[2]},hb[3]={lb[0]+ext[0],lb[1]+ext[1],lb[2]+ext[2]};
            float d[3]={c[0]-a[0],0,c[1]-a[1]},cr[3]={d[1]*ext[2]-d[2]*ext[1],d[2]*ext[0]-d[0]*ext[2],d[0]*ext[1]-d[1]*ext[0]};
            int flip=cr[0]*(-d[2])+cr[2]*d[0]<0;
            first=b->vcount; const float *n=&wn[i*3];
            put(b,la,n,b->color,cum[i]/total,1); put(b,lb,n,b->color,cum[i+1]/total,1);
            put(b,hb,n,b->color,cum[i+1]/total,0); put(b,ha,n,b->color,cum[i]/total,0);
            WALL_TRI(first,first+1,first+2,flip); WALL_TRI(first,first+2,first+3,flip);
        }
    }
    #undef WALL_TRI
    float up=ext[1]>0?1.0f:-1.0f;
    for(int end=0;end<2;end++) {
        if(!(end?cap_end:cap_start)) continue;
        float n[3]={0,end?up:-up,0},oy=end?ext[1]:0;
        first=b->vcount;
        for(uint32_t i=0;i<count;i++) {
            float p[3]={pts[i*2]+(end?ext[0]:0),y+oy,pts[i*2+1]+(end?ext[2]:0)};
            put(b,p,n,b->color,(pts[i*2]-minx)/width,(pts[i*2+1]-minz)/depth);
        }
        /* tris are counter-clockwise from +Y: keep them when this cap faces +Y, reverse otherwise. */
        int faces_up=n[1]>0;
        for(uint32_t t=0;t<ntris;t+=3) { if(faces_up) tri(b,first+tris[t],first+tris[t+1],first+tris[t+2]); else tri(b,first+tris[t],first+tris[t+2],first+tris[t+1]); }
    }
    free(wn); free(cum); free(pts); free(idx);
    return 0;
}
static int merge_mesh(AthenaMeshBuilder *b,const AthenaMesh3D *mesh);
int athena_meshbuilder_merge(AthenaMeshBuilder *b,const AthenaMesh3D *mesh,const AthenaMatrix4 *model) {
    if(!b||!mesh) return ATHENA_MESHBUILDER_EINVAL;
    if(!model) return merge_mesh(b,mesh);
    AthenaMatrix4 saved=b->m,composed; float saved_nm[9]; int saved_identity=b->identity;
    memcpy(saved_nm,b->nm,sizeof(saved_nm));
    ath_matrix4_multiply(&composed,&b->m,model);
    int code=athena_meshbuilder_set_transform(b,&composed);
    if(!code) code=merge_mesh(b,mesh);
    b->m=saved; memcpy(b->nm,saved_nm,sizeof(saved_nm)); b->identity=saved_identity;
    return code;
}
static int merge_mesh(AthenaMeshBuilder *b,const AthenaMesh3D *mesh) {
    AthenaMesh3DView v; athena_mesh3d_view(mesh,&v);
    uint32_t streams=athena_mesh3d_stream_count(&v);
    if(reserve(b,streams,v.vertex_count)) return ATHENA_MESHBUILDER_ENOMEM;
    uint32_t first=b->vcount;
    float saved0[2]={b->uv0[0],b->uv0[1]},saved1[2]={b->uv1[0],b->uv1[1]};
    b->uv0[0]=b->uv0[1]=0; b->uv1[0]=b->uv1[1]=1;    /* the mesh's own UVs */
    for(uint32_t i=0;i<streams;i++) {
        float p[3]={v.positions[i].x,v.positions[i].y,v.positions[i].z},n[3],c[4]={1,1,1,1};
        if(v.normals) { n[0]=v.normals[i].x; n[1]=v.normals[i].y; n[2]=v.normals[i].z; }
        if(v.colors) { c[0]=v.colors[i].r/255.0f; c[1]=v.colors[i].g/255.0f; c[2]=v.colors[i].b/255.0f; c[3]=v.colors[i].a/255.0f; }
        put(b,p,v.normals?n:NULL,c,v.texcoords?v.texcoords[i].u:0,v.texcoords?v.texcoords[i].v:0);
    }
    b->uv0[0]=saved0[0]; b->uv0[1]=saved0[1]; b->uv1[0]=saved1[0]; b->uv1[1]=saved1[1];
    for(uint32_t c=0;c+2<v.vertex_count;c+=3)
        tri(b,first+athena_mesh3d_corner(&v,c),first+athena_mesh3d_corner(&v,c+1),first+athena_mesh3d_corner(&v,c+2));
    return 0;
}
uint32_t athena_meshbuilder_vertex_count(const AthenaMeshBuilder *b) { return b?b->vcount:0; }
uint32_t athena_meshbuilder_triangle_count(const AthenaMeshBuilder *b) { return b?b->icount/3:0; }

/* Parts: consecutive triangles while both the indices and the distinct
 * vertices they use fit PART_LIMIT. stamp[] marks the vertices of the part
 * being built (part number + 1) and slot[] their index in it. */
typedef struct { uint32_t first,end; } Part;
static uint32_t split(const AthenaMeshBuilder *b,uint32_t *stamp,Part *parts,uint32_t max) {
    uint32_t count=0,t=0,part=0,used=0,start=0;
    while(t<b->icount) {
        uint32_t fresh=0;
        for(int k=0;k<3;k++) if(stamp[b->idx[t+k]]!=part+1) fresh++;
        /* One vertex could repeat within the triangle: fresh overcounts, safely. */
        if(t>start&&(t-start+3>PART_LIMIT||used+fresh>PART_LIMIT)) {
            if(count<max) parts[count]=(Part){start,t};
            count++; part++; used=0; start=t;
            continue;
        }
        for(int k=0;k<3;k++) { uint32_t v=b->idx[t+k]; if(stamp[v]!=part+1) { stamp[v]=part+1; used++; } }
        t+=3;
    }
    if(t>start) { if(count<max) parts[count]=(Part){start,t}; count++; }
    return count;
}
uint32_t athena_meshbuilder_part_count(const AthenaMeshBuilder *b) {
    if(!b||!b->icount) return 0;
    uint32_t *stamp=calloc(b->vcount,4); if(!stamp) return 0;
    uint32_t n=split(b,stamp,NULL,0);
    free(stamp); return n;
}
/* Welding: vertices equal in every attribute the mesh keeps (position and
 * color, plus normal for DIFFUSE and UV when textured) share one index
 * within a part. A part's open-addressing table maps a hash to the slot. */
typedef union { float f; uint32_t u; } Word;
static inline uint32_t bits(float f) { Word w; w.f=f; return w.u; }
static uint32_t vertex_hash(const AthenaMeshBuilder *b,uint32_t v,int normals,int uvs) {
    const float *p=&b->pos[v*3],*c=&b->col[v*4];
    uint32_t h=2166136261u;
    h=(h^bits(p[0]))*16777619u; h=(h^bits(p[1]))*16777619u; h=(h^bits(p[2]))*16777619u;
    h=(h^bits(c[0]))*16777619u; h=(h^bits(c[1]))*16777619u; h=(h^bits(c[2]))*16777619u; h=(h^bits(c[3]))*16777619u;
    if(normals) { const float *n=&b->nrm[v*3]; h=(h^bits(n[0]))*16777619u; h=(h^bits(n[1]))*16777619u; h=(h^bits(n[2]))*16777619u; }
    if(uvs) { const float *t=&b->uv[v*2]; h=(h^bits(t[0]))*16777619u; h=(h^bits(t[1]))*16777619u; }
    return h^(h>>15);
}
/* Bitwise equality (as the hash): -0 and 0 differ, which only costs a weld. */
static int same_vertex(const AthenaMeshBuilder *b,uint32_t v,uint32_t w,int normals,int uvs) {
    const float *a=&b->pos[v*3],*c=&b->pos[w*3];
    if(bits(a[0])!=bits(c[0])||bits(a[1])!=bits(c[1])||bits(a[2])!=bits(c[2])) return 0;
    a=&b->col[v*4]; c=&b->col[w*4];
    if(bits(a[0])!=bits(c[0])||bits(a[1])!=bits(c[1])||bits(a[2])!=bits(c[2])||bits(a[3])!=bits(c[3])) return 0;
    if(normals) { a=&b->nrm[v*3]; c=&b->nrm[w*3]; if(bits(a[0])!=bits(c[0])||bits(a[1])!=bits(c[1])||bits(a[2])!=bits(c[2])) return 0; }
    if(uvs) { a=&b->uv[v*2]; c=&b->uv[w*2]; if(bits(a[0])!=bits(c[0])||bits(a[1])!=bits(c[1])) return 0; }
    return 1;
}
int athena_meshbuilder_build(const AthenaMeshBuilder *b,const AthenaMaterial3D *material,AthenaMesh3D **out,uint32_t max) {
    if(!b||(max&&!out)) return ATHENA_MESHBUILDER_EINVAL;
    if(!b->icount) return 0;
    AthenaMaterial3D def; if(!material) { athena_material3d_default(&def); material=&def; }
    uint32_t *stamp=calloc(b->vcount,4),*slot=malloc((size_t)b->vcount*4);
    if(!stamp||!slot) { free(stamp); free(slot); return ATHENA_MESHBUILDER_ENOMEM; }
    uint32_t total=split(b,stamp,NULL,0);
    if(total>max) { free(stamp); free(slot); return (int)total; }
    Part *parts=malloc(total*sizeof(*parts));
    float *pos=malloc((size_t)PART_LIMIT*12),*nrm=malloc((size_t)PART_LIMIT*12),*col=malloc((size_t)PART_LIMIT*16),
        *uv=malloc((size_t)PART_LIMIT*8);
    uint32_t *idx=malloc((size_t)PART_LIMIT*4);
    int result=(int)total;
    if(!parts||!pos||!nrm||!col||!uv||!idx) { result=ATHENA_MESHBUILDER_ENOMEM; goto done; }
    memset(stamp,0,(size_t)b->vcount*4);
    split(b,stamp,parts,total);
    memset(stamp,0,(size_t)b->vcount*4);
    int normals=material->shading==ATHENA_MATERIAL3D_DIFFUSE,textured=material->texture!=NULL;
    /* Table of at least twice the largest part's vertices (a power of two). */
    uint32_t table_size=1024;
    while(table_size<2*(b->vcount<PART_LIMIT?b->vcount:PART_LIMIT)) table_size*=2;
    uint32_t *table=malloc((size_t)table_size*4),*rep=malloc((size_t)PART_LIMIT*4);
    if(!table||!rep) { free(table); free(rep); result=ATHENA_MESHBUILDER_ENOMEM; goto done; }
    for(uint32_t p=0;p<total;p++) {
        uint32_t vc=0,ic=0;
        memset(table,0,(size_t)table_size*4);
        for(uint32_t t=parts[p].first;t<parts[p].end;t++) {
            uint32_t v=b->idx[t];
            if(stamp[v]!=p+1) {
                stamp[v]=p+1;
                uint32_t h=vertex_hash(b,v,normals,textured)&(table_size-1),found=UINT32_MAX;
                while(table[h]) {
                    uint32_t s=table[h]-1;
                    if(same_vertex(b,rep[s],v,normals,textured)) { found=s; break; }
                    h=(h+1)&(table_size-1);
                }
                if(found!=UINT32_MAX) slot[v]=found;
                else {
                    table[h]=vc+1; rep[vc]=v; slot[v]=vc;
                    const float *sp=&b->pos[v*3],*sn=&b->nrm[v*3],*sc=&b->col[v*4],*st=&b->uv[v*2];
                    float *dp=&pos[vc*3],*dn=&nrm[vc*3],*dc=&col[vc*4],*dt=&uv[vc*2];
                    dp[0]=sp[0]; dp[1]=sp[1]; dp[2]=sp[2];
                    if(normals) { dn[0]=sn[0]; dn[1]=sn[1]; dn[2]=sn[2]; }
                    dc[0]=sc[0]; dc[1]=sc[1]; dc[2]=sc[2]; dc[3]=sc[3];
                    if(textured) { dt[0]=st[0]; dt[1]=st[1]; }
                    vc++;
                }
            }
            idx[ic++]=slot[v];
        }
        AthenaGeometry3D g={0};
        g.positions=pos; g.vertex_count=vc; g.colors=col; g.color_count=vc;
        g.indices=idx; g.index_count=ic; g.material=material;
        if(normals) { g.normals=nrm; g.normal_count=vc; }
        if(textured) { g.texcoords=uv; g.texcoord_count=vc; }
        int code=athena_mesh3d_create(&g,&out[p]);
        if(code<0) {
            for(uint32_t q=0;q<p;q++) { athena_mesh3d_release(out[q]); out[q]=NULL; }
            result=code==ATHENA_MODEL3D_ENOMEM?ATHENA_MESHBUILDER_ENOMEM:ATHENA_MESHBUILDER_EINVAL;
            break;
        }
    }
    free(table); free(rep);
done:
    free(stamp); free(slot); free(parts); free(pos); free(nrm); free(col); free(uv); free(idx);
    return result;
}
