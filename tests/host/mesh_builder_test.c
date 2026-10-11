/* Resumable Mesh3D builder: any step budget yields the one-shot mesh exactly,
 * every stage honors its budget, invalid geometry fails sticky, and an
 * abandoned builder frees everything (ASan). */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <athena/model3d.h>

typedef struct { float *xyz,*rgba,*normals,*uv,*weights,*tp,*tn; uint32_t *idx; uint16_t *joints; AthenaGeometry3D g; AthenaMaterial3D m; } Scene;
static float rnd(uint32_t *s) { *s=*s*1664525u+1013904223u; return (float)(*s>>8)/(float)(1u<<24); }
/* A strip of quads, indexed so vertices repeat across batches. */
static void make(Scene *s,int indexed,int normals,int uv,int skin,int targets,int diffuse,uint32_t cols) {
    uint32_t seed=cols*7919u+indexed*3+normals*5+uv*11+skin*13+targets*17+diffuse*19, rows=3, n=(cols+1)*(rows+1);
    memset(s,0,sizeof(*s));
    s->xyz=malloc(n*3*sizeof(float)); s->rgba=malloc(n*4*sizeof(float));
    for(uint32_t i=0;i<n;i++) {
        s->xyz[i*3]=(float)(i%(cols+1)); s->xyz[i*3+1]=(float)(i/(cols+1)); s->xyz[i*3+2]=rnd(&seed)*.25f;
        for(int k=0;k<4;k++) s->rgba[i*4+k]=rnd(&seed);
    }
    uint32_t quads=cols*rows,corners=quads*6;
    uint32_t *idx=malloc(corners*sizeof(*idx)),o=0;
    for(uint32_t r=0;r<rows;r++) for(uint32_t c=0;c<cols;c++) {
        uint32_t a=r*(cols+1)+c,b=a+1,d=a+cols+1,e=d+1;
        const uint32_t q[6]={a,b,d,b,e,d}; for(int k=0;k<6;k++) idx[o++]=q[k];
    }
    AthenaGeometry3D g={.vertex_count=n,.colors=s->rgba,.color_count=n,.material=&s->m};
    athena_material3d_default(&s->m); if(diffuse) s->m.shading=ATHENA_MATERIAL3D_DIFFUSE;
    float *pos=s->xyz,*expanded=NULL;
    if(indexed) { s->idx=idx; g.indices=idx; g.index_count=corners; }
    else {
        /* Triangle soup: expand every attribute per corner. */
        expanded=malloc(corners*3*sizeof(float)); float *erg=malloc(corners*4*sizeof(float));
        for(uint32_t i=0;i<corners;i++) { memcpy(&expanded[i*3],&pos[idx[i]*3],12); memcpy(&erg[i*4],&s->rgba[idx[i]*4],16); }
        free(s->rgba); s->rgba=erg; free(s->xyz); s->xyz=pos=expanded; free(idx); n=corners;
        g.vertex_count=n; g.colors=erg; g.color_count=n;
    }
    g.positions=pos;
    if(normals) {
        s->normals=malloc(n*3*sizeof(float));
        for(uint32_t i=0;i<n;i++) { s->normals[i*3]=rnd(&seed)-.5f; s->normals[i*3+1]=rnd(&seed)-.5f; s->normals[i*3+2]=1; }
        g.normals=s->normals; g.normal_count=n;
    }
    if(uv) { s->uv=malloc(n*2*sizeof(float)); for(uint32_t i=0;i<n*2;i++) s->uv[i]=rnd(&seed)*4; g.texcoords=s->uv; g.texcoord_count=n; }
    if(skin) {
        s->joints=malloc(n*4*sizeof(uint16_t)); s->weights=malloc(n*4*sizeof(float));
        for(uint32_t i=0;i<n;i++) for(int k=0;k<4;k++) { s->joints[i*4+k]=(uint16_t)(rnd(&seed)*5); s->weights[i*4+k]=rnd(&seed)+.01f; }
        g.joints=s->joints; g.weights=s->weights; g.skin_count=n;
    }
    if(targets) {
        s->tp=malloc((size_t)targets*n*3*sizeof(float));
        for(size_t i=0;i<(size_t)targets*n*3;i++) s->tp[i]=rnd(&seed)-.5f;
        g.target_positions=s->tp; g.target_count=targets;
        if(normals) { s->tn=malloc((size_t)targets*n*3*sizeof(float)); for(size_t i=0;i<(size_t)targets*n*3;i++) s->tn[i]=rnd(&seed)-.5f; g.target_normals=s->tn; }
    }
    s->g=g;
}
static void drop(Scene *s) { free(s->xyz); free(s->rgba); free(s->normals); free(s->uv); free(s->weights); free(s->joints); free(s->tp); free(s->tn); free(s->idx); }
#define SAME(a,b,bytes) do { assert((a!=NULL)==(b!=NULL)); if(a) assert(!memcmp(a,b,bytes)); } while(0)
static void equal(const AthenaMesh3D *a,const AthenaMesh3D *b) {
    AthenaMesh3DView x,y; athena_mesh3d_view(a,&x); athena_mesh3d_view(b,&y);
    assert(x.vertex_count==y.vertex_count&&x.stream_vertex_count==y.stream_vertex_count&&x.chunk_count==y.chunk_count);
    uint32_t n=athena_mesh3d_stream_count(&x)+4;
    SAME(x.positions,y.positions,n*sizeof(*x.positions)); SAME(x.colors,y.colors,n*sizeof(*x.colors));
    SAME(x.normals,y.normals,n*sizeof(*x.normals)); SAME(x.texcoords,y.texcoords,n*sizeof(*x.texcoords));
    SAME(x.joints,y.joints,n*4); SAME(x.weights8,y.weights8,n*4); SAME(x.weights,y.weights,(n-4)*4*sizeof(float));
    assert(x.joint_count==y.joint_count&&x.target_count==y.target_count&&x.flat_normals==y.flat_normals);
    SAME(x.indices,y.indices,x.vertex_count*sizeof(*x.indices));
    SAME(x.chunks,y.chunks,x.chunk_count*sizeof(*x.chunks));
    SAME(x.chunk_indices,y.chunk_indices,x.chunk_count*ATHENA_MESH3D_INDEXED_INDEX_BYTES);
    SAME(x.deform_reuse,y.deform_reuse,(n-4)*sizeof(*x.deform_reuse));
    SAME(x.target_positions,y.target_positions,(size_t)x.target_count*(n-4)*sizeof(*x.target_positions)+(x.target_count?4*sizeof(*x.target_positions):0));
    SAME(x.target_normals,y.target_normals,(size_t)x.target_count*(n-4)*sizeof(*x.target_normals)+(x.target_count?4*sizeof(*x.target_normals):0));
    assert(!memcmp(x.minimum,y.minimum,sizeof(x.minimum))&&!memcmp(x.maximum,y.maximum,sizeof(x.maximum)));
    assert(!memcmp(x.target_minimum,y.target_minimum,x.target_count*3*sizeof(float))&&
        !memcmp(x.target_maximum,y.target_maximum,x.target_count*3*sizeof(float)));
}
static unsigned build(const AthenaGeometry3D *g,uint32_t budget,AthenaMesh3D **out) {
    AthenaMesh3DBuilder *b=NULL; assert(!athena_mesh3d_builder_begin(g,&b));
    unsigned steps=0; int code;
    while(!(code=athena_mesh3d_builder_step(b,budget))) { steps++; assert(steps<4000000); }
    assert(code==1); steps++;
    assert(athena_mesh3d_builder_step(b,budget)==1);
    assert(!athena_mesh3d_builder_take(b,out)&&*out);
    AthenaMesh3D *again=(AthenaMesh3D *)1; assert(athena_mesh3d_builder_take(b,&again)<0&&!again);
    athena_mesh3d_builder_destroy(b); return steps;
}
int main(void) {
    static const struct { int indexed,normals,uv,skin,targets,diffuse; const char *name; } variants[]={
        {1,1,1,1,2,0,"indexed skinned morph"},{1,1,1,0,0,1,"indexed lit"},{1,0,0,0,0,1,"indexed flat normals"},
        {1,0,0,1,3,0,"indexed skin, targets, no normals"},{0,1,1,1,1,0,"soup skinned morph"},
        {0,0,1,0,0,1,"soup flat normals"},{0,0,0,0,0,0,"soup plain"}};
    uint32_t budgets[]={1,3,7,64};
    for(unsigned v=0;v<sizeof(variants)/sizeof(*variants);v++) for(uint32_t cols=1;cols<=6;cols+=5) {
        Scene s; make(&s,variants[v].indexed,variants[v].normals,variants[v].uv,variants[v].skin,variants[v].targets,variants[v].diffuse,cols);
        AthenaMesh3D *whole=NULL,*one=NULL; assert(!athena_mesh3d_create(&s.g,&whole));
        unsigned unlimited=build(&s.g,UINT32_MAX,&one); equal(whole,one); athena_mesh3d_release(one);
        assert(unlimited==1); /* an unbounded budget finishes in one call */
        unsigned previous=0;
        for(int k=3;k>=0;k--) {
            AthenaMesh3D *m=NULL; unsigned steps=build(&s.g,budgets[k],&m); equal(whole,m); athena_mesh3d_release(m);
            assert(steps>=previous); /* smaller budgets never take fewer steps */
            previous=steps;
        }
        /* Abandon at every step count: nothing leaks, nothing is delivered. */
        for(unsigned stop=0;stop<unlimited+6;stop++) {
            AthenaMesh3DBuilder *b=NULL; assert(!athena_mesh3d_builder_begin(&s.g,&b));
            for(unsigned i=0;i<stop;i++) if(athena_mesh3d_builder_step(b,17)) break;
            athena_mesh3d_builder_destroy(b);
        }
        athena_mesh3d_release(whole); drop(&s);
        printf("builder %s, %u columns: %u unlimited steps\n",variants[v].name,(unsigned)cols,unlimited);
    }
    /* Invalid geometry: rejected in some step, sticky, nothing delivered; the issue matches validate(). */
    Scene s; make(&s,1,1,1,1,1,0,4);
    s.xyz[17]=NAN;
    AthenaMesh3DBuilder *b=NULL; assert(!athena_mesh3d_builder_begin(&s.g,&b));
    int code; unsigned guard=0; while(!(code=athena_mesh3d_builder_step(b,5))) assert(++guard<100000);
    assert(code==ATHENA_MODEL3D_EINVAL&&athena_mesh3d_builder_step(b,5)==ATHENA_MODEL3D_EINVAL);
    AthenaMesh3D *none=(AthenaMesh3D *)1; assert(athena_mesh3d_builder_take(b,&none)<0&&!none);
    uint32_t offset; assert(athena_geometry3d_validate(&s.g,&offset)==ATHENA_GEOMETRY3D_POSITION&&offset==17);
    athena_mesh3d_builder_destroy(b); drop(&s);
    assert(athena_mesh3d_builder_begin(NULL,&b)==0); assert(athena_mesh3d_builder_step(b,0)==ATHENA_MODEL3D_EINVAL); athena_mesh3d_builder_destroy(b);
    assert(athena_mesh3d_builder_begin(NULL,NULL)<0); athena_mesh3d_builder_destroy(NULL);
    puts("Mesh3D resumable builder test passed: budgets 1/3/7/64/unbounded bit-exact, abandonment, invalid geometry");
    return 0;
}
