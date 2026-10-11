/* MeshBuilder extrusion: closed prisms have the exact volume with outward (counter-clockwise) winding for
 * either input winding, concave caps cover the outline exactly, normals agree with the faces, and every
 * invalid contour is rejected with the builder untouched. The source is included to inspect the buffers. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/modules/meshbuilder/native/meshbuilder.c"

static const float SQUARE[8]={0,0, 4,0, 4,3, 0,3};          /* counter-clockwise seen from +Y */
static const float L_SHAPE[12]={0,0, 4,0, 4,1, 1,1, 1,3, 0,3};
static void reversed(const float *in,float *out,uint32_t n) { for(uint32_t i=0;i<n;i++) { out[i*2]=in[(n-1-i)*2]; out[i*2+1]=in[(n-1-i)*2+1]; } }
static double polygon_area(const float *xz,uint32_t n) {
    double a=0; for(uint32_t i=0;i<n;i++) { uint32_t j=(i+1)%n; a+=(double)xz[i*2]*xz[j*2+1]-(double)xz[j*2]*xz[i*2+1]; } return fabs(a)/2;
}
/* Signed volume by the divergence theorem: positive when the faces point outward. */
static double volume(const AthenaMeshBuilder *b) {
    double v=0;
    for(uint32_t t=0;t<b->icount;t+=3) {
        const float *p=&b->pos[b->idx[t]*3],*q=&b->pos[b->idx[t+1]*3],*r=&b->pos[b->idx[t+2]*3];
        v+=((double)p[0]*((double)q[1]*r[2]-(double)q[2]*r[1])-(double)p[1]*((double)q[0]*r[2]-(double)q[2]*r[0])+
            (double)p[2]*((double)q[0]*r[1]-(double)q[1]*r[0]))/6.0;
    }
    return v;
}
/* Every stored normal must lie on the side the winding faces (flat: equal to it). */
static void normals_follow_faces(const AthenaMeshBuilder *b,int flat) {
    for(uint32_t t=0;t<b->icount;t+=3) {
        const float *p=&b->pos[b->idx[t]*3],*q=&b->pos[b->idx[t+1]*3],*r=&b->pos[b->idx[t+2]*3];
        double e1[3]={q[0]-p[0],q[1]-p[1],q[2]-p[2]},e2[3]={r[0]-p[0],r[1]-p[1],r[2]-p[2]};
        double n[3]={e1[1]*e2[2]-e1[2]*e2[1],e1[2]*e2[0]-e1[0]*e2[2],e1[0]*e2[1]-e1[1]*e2[0]},l=sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        assert(l>0);
        for(int k=0;k<3;k++) n[k]/=l;
        for(int c=0;c<3;c++) {
            const float *s=&b->nrm[b->idx[t+c]*3];
            double d=n[0]*s[0]+n[1]*s[1]+n[2]*s[2];
            assert(flat?d>0.999:d>0.05);
            assert(fabsf(s[0]*s[0]+s[1]*s[1]+s[2]*s[2]-1)<1e-4f);
        }
    }
}
static void all_indices_valid(const AthenaMeshBuilder *b) { for(uint32_t i=0;i<b->icount;i++) assert(b->idx[i]<b->vcount); }
static AthenaMeshBuilder *make(const float *xz,uint32_t n,float y,const float ext[3],uint32_t flags,int *code) {
    AthenaMeshBuilder *b=athena_meshbuilder_create(); assert(b);
    *code=athena_meshbuilder_extrude(b,xz,n,y,ext,flags); return b;
}
static void rejected(const char *name,const float *xz,uint32_t n,const float ext[3]) {
    AthenaMeshBuilder *b=athena_meshbuilder_create();
    const float c[4]={.2f,.4f,.6f,1}; athena_meshbuilder_set_color(b,c);
    athena_meshbuilder_box(b,(float[]){0,0,0},(float[]){1,1,1});
    uint32_t v=b->vcount,i=b->icount;
    int code=athena_meshbuilder_extrude(b,xz,n,0,ext,ATHENA_EXTRUDE_CAP_START|ATHENA_EXTRUDE_CAP_END);
    if(code!=ATHENA_MESHBUILDER_EINVAL) { fprintf(stderr,"%s not rejected (%d)\n",name,code); abort(); }
    assert(athena_meshbuilder_error_detail()[0]);
    assert(b->vcount==v&&b->icount==i&&b->color[0]==.2f);   /* nothing added, state intact */
    athena_meshbuilder_destroy(b);
}
int main(void) {
    const float up[3]={0,2,0};
    const uint32_t both=ATHENA_EXTRUDE_CAP_START|ATHENA_EXTRUDE_CAP_END;
    int code;
    /* Counts, volume and normals of the closed prism, in both input windings. */
    for(int rev=0;rev<2;rev++) {
        float rv[8]; reversed(SQUARE,rv,4);
        AthenaMeshBuilder *b=make(rev?rv:SQUARE,4,1,up,both,&code);
        assert(!code&&b->vcount==16+8&&b->icount==(8+4)*3);
        assert(fabs(volume(b)-12*2)<1e-4); normals_follow_faces(b,1); all_indices_valid(b);
        for(uint32_t i=0;i<b->vcount;i++) assert(b->pos[i*3+1]>=1-1e-6f&&b->pos[i*3+1]<=3+1e-6f);
        athena_meshbuilder_destroy(b);
    }
    /* Downward extrusion and a slanted vector keep the faces outward; the volume is area * |ey|. */
    { const float down[3]={0,-1.5f,0},slant[3]={1.5f,2,-1};
      AthenaMeshBuilder *b=make(SQUARE,4,0,down,both,&code);
      assert(!code&&fabs(volume(b)-12*1.5)<1e-4); normals_follow_faces(b,1); athena_meshbuilder_destroy(b);
      b=make(SQUARE,4,0,slant,both,&code);
      assert(!code&&fabs(volume(b)-12*2)<1e-4); athena_meshbuilder_destroy(b);
      float rv[8]; reversed(SQUARE,rv,4);
      b=make(rv,4,0,slant,both,&code); assert(!code&&fabs(volume(b)-12*2)<1e-4); athena_meshbuilder_destroy(b);
      b=make(rv,4,0,down,both,&code); assert(!code&&fabs(volume(b)-12*1.5)<1e-4); athena_meshbuilder_destroy(b); }
    /* Concave outline: the caps tile it exactly and the prism has the right volume. */
    for(int rev=0;rev<2;rev++) {
        float rv[12]; reversed(L_SHAPE,rv,6);
        AthenaMeshBuilder *b=make(rev?rv:L_SHAPE,6,0,up,both,&code);
        double area=polygon_area(L_SHAPE,6);
        assert(!code&&fabs(area-6)<1e-9&&fabs(volume(b)-area*2)<1e-4); normals_follow_faces(b,1); all_indices_valid(b);
        assert(b->icount==(6*2+2*4)*3);                       /* 6 walls, 2 caps of 4 ear triangles */
        athena_meshbuilder_destroy(b);
    }
    /* Flags: walls only, one cap, smooth walls. */
    { AthenaMeshBuilder *b=make(SQUARE,4,0,up,0,&code); assert(!code&&b->vcount==16&&b->icount==24); athena_meshbuilder_destroy(b);
      b=make(SQUARE,4,0,up,ATHENA_EXTRUDE_CAP_END,&code); assert(!code&&b->vcount==20&&b->icount==30);
      for(uint32_t i=16;i<20;i++) assert(b->nrm[i*3+1]==1&&fabsf(b->pos[i*3+1]-2)<1e-6f);   /* end cap faces along the extrusion */
      athena_meshbuilder_destroy(b);
      b=make(SQUARE,4,0,up,ATHENA_EXTRUDE_CAP_START,&code); assert(!code);
      for(uint32_t i=16;i<20;i++) assert(b->nrm[i*3+1]==-1&&b->pos[i*3+1]==0);   /* start cap faces away from it */
      athena_meshbuilder_destroy(b);
      b=make(SQUARE,4,0,up,ATHENA_EXTRUDE_SMOOTH|both,&code);
      assert(!code&&b->vcount==10+8&&b->icount==36&&fabs(volume(b)-24)<1e-4); normals_follow_faces(b,0);
      /* shared corner normals are the average of the two walls (diagonal) */
      assert(fabsf(fabsf(b->nrm[0])-0.70710678f)<1e-5f&&fabsf(fabsf(b->nrm[2])-0.70710678f)<1e-5f);
      /* seam duplicated: first and last pair share position but u runs 0..1 */
      assert(b->pos[0]==b->pos[8*3]&&b->pos[2]==b->pos[8*3+2]&&b->uv[0]==0&&fabsf(b->uv[8*2]-1)<1e-6f);
      athena_meshbuilder_destroy(b); }
    /* Wall UVs: u by edge length (0..1), v 1 at the contour plane and 0 at the far end. */
    { AthenaMeshBuilder *b=make(SQUARE,4,0,up,0,&code);
      float total=14; assert(b->uv[0]==0&&fabsf(b->uv[2]-4/total)<1e-6f&&b->uv[1]==1&&b->uv[5]==0);
      assert(fabsf(b->uv[(3*4+1)*2]-1)<1e-6f);               /* last edge ends at u = 1 */
      athena_meshbuilder_destroy(b);
      b=make(SQUARE,4,0,up,ATHENA_EXTRUDE_CAP_START,&code);   /* cap UVs: bounding box 0..1 */
      for(uint32_t i=0;i<4;i++) { float u=b->uv[(16+i)*2],v=b->uv[(16+i)*2+1]; assert(u>=0&&u<=1&&v>=0&&v<=1); }
      for(uint32_t i=16;i<20;i++) assert(fabsf(b->uv[i*2]-b->pos[i*3]/4)<1e-6f&&fabsf(b->uv[i*2+1]-b->pos[i*3+2]/3)<1e-6f);
      athena_meshbuilder_destroy(b); }
    /* The pen applies: color and transform. */
    { AthenaMeshBuilder *b=athena_meshbuilder_create(); const float c[4]={1,0,0,1}; athena_meshbuilder_set_color(b,c);
      AthenaMatrix4 m; memset(&m,0,sizeof m); for(int i=0;i<4;i++) m.value[i*4+i]=1; m.value[12]=10;
      athena_meshbuilder_set_transform(b,&m);
      assert(!athena_meshbuilder_extrude(b,SQUARE,4,0,up,both));
      for(uint32_t i=0;i<b->vcount;i++) assert(b->col[i*4]==1&&b->col[i*4+1]==0&&b->pos[i*3]>=10&&b->pos[i*3]<=14);
      athena_meshbuilder_destroy(b); }
    /* Collinear points stay in the walls but not in the caps. */
    { const float with_mid[10]={0,0, 2,0, 4,0, 4,3, 0,3};
      AthenaMeshBuilder *b=make(with_mid,5,0,up,both,&code);
      assert(!code&&b->vcount==20+10&&b->icount==(10+4)*3&&fabs(volume(b)-24)<1e-4); athena_meshbuilder_destroy(b); }
    /* The largest contour works: a 512-gon. */
    { float *c=malloc(512*2*sizeof(float)); for(int i=0;i<512;i++) { c[i*2]=cosf((float)i*6.2831853f/512)*5; c[i*2+1]=sinf((float)i*6.2831853f/512)*5; }
      AthenaMeshBuilder *b=make(c,512,0,up,both,&code);
      assert(!code&&b->icount==(512*2+2*510)*3);
      double area=polygon_area(c,512); assert(fabs(volume(b)-area*2)<1e-2*area); all_indices_valid(b);
      athena_meshbuilder_destroy(b); free(c); }
    /* A comb: many concave ears. */
    { float comb[2*16]; int n=0;
      for(int t=0;t<4;t++) { comb[n++]=(float)(t*2); comb[n++]=0; comb[n++]=(float)(t*2); comb[n++]=3; comb[n++]=(float)(t*2+1); comb[n++]=3; comb[n++]=(float)(t*2+1); comb[n++]=1; }
      /* teeth run up from a base: close the outline through the bottom edge */
      AthenaMeshBuilder *b=athena_meshbuilder_create();
      float shape[2*10]={0,0, 8,0, 8,3, 7,3, 7,1, 5,1, 5,3, 4,3, 4,1, 0,1};
      assert(!athena_meshbuilder_extrude(b,shape,10,0,up,both));
      assert(fabs(volume(b)-polygon_area(shape,10)*2)<1e-4); all_indices_valid(b); normals_follow_faces(b,1);
      athena_meshbuilder_destroy(b); (void)comb; }
    /* Rejections: nothing added, state untouched, a reason is given. */
    { const float flat[3]={1,0,0},zero[3]={0,0,0},nanv[3]={0,NAN,0};
      rejected("two points",SQUARE,2,up);
      rejected("null extrusion",SQUARE,4,zero);
      rejected("extrusion parallel to the plane",SQUARE,4,flat);
      rejected("nan extrusion",SQUARE,4,nanv);
      const float bowtie[8]={0,0, 2,2, 2,0, 0,2}; rejected("bowtie",bowtie,4,up);
      const float repeated[10]={0,0, 4,0, 4,0, 4,3, 0,3}; rejected("repeated point",repeated,5,up);
      const float closed[10]={0,0, 4,0, 4,3, 0,3, 0,0}; rejected("first point repeated at the end",closed,5,up);
      const float spike[10]={0,0, 4,0, 4,3, 6,3, 4,3}; rejected("spike",spike,5,up);
      const float spike2[12]={0,0, 4,0, 4,3, 2,3, 6,3, 0,3}; rejected("doubling back",spike2,6,up);
      const float line[6]={0,0, 1,0, 2,0}; rejected("collinear (zero area)",line,3,up);
      const float vline[6]={0,0, 0,1, 0,2}; rejected("collinear along z",vline,3,up);
      const float nanp[8]={0,0, 4,NAN, 4,3, 0,3}; rejected("nan point",nanp,4,up);
      const float infp[8]={0,0, 4,0, INFINITY,3, 0,3}; rejected("inf point",infp,4,up);
      const float touch[16]={0,0, 4,0, 4,4, 2,4, 2,2, 3,2, 3,3, 2,2}; rejected("touching itself",touch,8,up);
      const float vtouch[14]={0,0, 4,0, 4,4, 0,4, 2,2, 2,5, 1,5}; rejected("edge through a vertex",vtouch,7,up);
      float *big=malloc(513*2*sizeof(float)); for(int i=0;i<513;i++) { big[i*2]=cosf((float)i*6.2831853f/513); big[i*2+1]=sinf((float)i*6.2831853f/513); }
      rejected("too many points",big,513,up); free(big);
      const float tiny[8]={0,0, 1e-30f,0, 1e-30f,1e-30f, 0,1e-30f}; (void)tiny; }
    /* Unknown flags and NULL arguments. */
    { AthenaMeshBuilder *b=athena_meshbuilder_create();
      assert(athena_meshbuilder_extrude(b,SQUARE,4,0,up,8)==ATHENA_MESHBUILDER_EINVAL);
      assert(athena_meshbuilder_extrude(b,NULL,4,0,up,0)==ATHENA_MESHBUILDER_EINVAL);
      assert(athena_meshbuilder_extrude(b,SQUARE,4,0,NULL,0)==ATHENA_MESHBUILDER_EINVAL);
      assert(athena_meshbuilder_extrude(NULL,SQUARE,4,0,up,0)==ATHENA_MESHBUILDER_EINVAL);
      assert(b->vcount==0&&b->icount==0);
      /* A successful call after a rejection clears the reason. */
      assert(!athena_meshbuilder_extrude(b,SQUARE,4,0,up,both)&&!athena_meshbuilder_error_detail()[0]);
      athena_meshbuilder_destroy(b); }
    /* Part splitting: many prisms still build (the 65532 limit is the builder's job, not the extruder's). */
    { AthenaMeshBuilder *b=athena_meshbuilder_create();
      for(int i=0;i<3000;i++) { AthenaMatrix4 m; memset(&m,0,sizeof m); for(int k=0;k<4;k++) m.value[k*4+k]=1; m.value[12]=(float)i*5; athena_meshbuilder_set_transform(b,&m);
          assert(!athena_meshbuilder_extrude(b,L_SHAPE,6,0,up,both)); }
      assert(b->vcount==3000*(24+12)&&athena_meshbuilder_part_count(b)>=2);
      athena_meshbuilder_destroy(b); }
    puts("MeshBuilder extrusion host test passed");
    return 0;
}
