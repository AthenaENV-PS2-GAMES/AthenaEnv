/* Shared host/EE contract tests for borrowed GLB memory and failure cleanup. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <athena/gltf3d.h>
#ifdef _EE
#include <kernel.h>
#endif

#ifdef ATHENA_GLTF_MEMORY_OOM
static int fail_at=-1, attempts;
void *__real_malloc(size_t); void *__real_calloc(size_t,size_t); void *__real_realloc(void *,size_t);
static int fail(void) { return fail_at>=0 && attempts++==fail_at; }
void *__wrap_malloc(size_t n) { return fail()?NULL:__real_malloc(n); }
void *__wrap_calloc(size_t n,size_t s) { return fail()?NULL:__real_calloc(n,s); }
void *__wrap_realloc(void *p,size_t n) { return fail()?NULL:__real_realloc(p,n); }
#endif

static unsigned char *read_bytes(const char *name,size_t *size) {
    FILE *f=fopen(name,"rb"); assert(f);
    assert(!fseek(f,0,SEEK_END)); long n=ftell(f); assert(n>0); rewind(f);
    #ifdef ATHENA_GLTF_MEMORY_OOM
    unsigned char *bytes=__real_malloc((size_t)n+1); /* test fixtures never fail */
#else
    unsigned char *bytes=malloc((size_t)n+1);
#endif
    assert(bytes);
    assert(fread(bytes,1,(size_t)n,f)==(size_t)n); fclose(f);
    *size=(size_t)n; return bytes;
}
static void equal_mesh(const AthenaMesh3D *a,const AthenaMesh3D *b) {
    assert((a!=NULL)==(b!=NULL)); if(!a) return;
    AthenaMesh3DView x,y; athena_mesh3d_view(a,&x); athena_mesh3d_view(b,&y);
    assert(x.vertex_count==y.vertex_count && x.stream_vertex_count==y.stream_vertex_count);
    assert(x.joint_count==y.joint_count && x.target_count==y.target_count);
    for(uint32_t i=0;i<x.vertex_count;i++) {
        uint32_t j=athena_mesh3d_corner(&x,i),k=athena_mesh3d_corner(&y,i);
        assert(!memcmp(&x.positions[j],&y.positions[k],sizeof(x.positions[j])));
        if(x.normals) assert(!memcmp(&x.normals[j],&y.normals[k],sizeof(x.normals[j])));
        if(x.texcoords) assert(!memcmp(&x.texcoords[j],&y.texcoords[k],sizeof(x.texcoords[j])));
    }
    assert((x.material.texture!=NULL)==(y.material.texture!=NULL));
}

/* Runs the resumable loader to the end; returns the step count or a negative
 * code. phases must be non-decreasing and the progress monotonic. */
static int run_steps(AthenaGltf3DLoader *l,uint32_t stop_after,AthenaGltf3D **out) {
    int last=-1,steps=0; uint32_t previous=0,done,total;
    for(;;) {
        int code=athena_gltf3d_loader_step(l);
        if(code<0) return code;
        steps++;
        int phase=athena_gltf3d_loader_last_phase(l); assert(phase>=last); last=phase;
        athena_gltf3d_loader_progress(l,&done,&total); assert(done>=previous&&(!total||done<=total)); previous=done;
        if(code==1) { assert(done==total); break; }
        if(stop_after&&(uint32_t)steps>=stop_after) return 0;
    }
    return athena_gltf3d_loader_take(l,out)<0?-1:steps;
}
static void same_scene(const AthenaGltf3D *a,const AthenaGltf3D *b) {
    assert(athena_gltf3d_node_count(a)==athena_gltf3d_node_count(b));
    assert(athena_gltf3d_clip_count(a)==athena_gltf3d_clip_count(b));
    for(uint32_t i=0;i<athena_gltf3d_node_count(a);i++) {
        assert(!strcmp(athena_gltf3d_node_name(a,i),athena_gltf3d_node_name(b,i)));
        equal_mesh(athena_node3d_mesh(athena_gltf3d_node(a,i)),athena_node3d_mesh(athena_gltf3d_node(b,i)));
        assert(athena_node3d_child_count(athena_gltf3d_node(a,i))==athena_node3d_child_count(athena_gltf3d_node(b,i)));
    }
    for(uint32_t i=0;i<athena_gltf3d_clip_count(a);i++) {
        assert(!strcmp(athena_gltf3d_clip_name(a,i),athena_gltf3d_clip_name(b,i)));
        assert(athena_clip3d_duration(athena_gltf3d_clip(a,i))==athena_clip3d_duration(athena_gltf3d_clip(b,i)));
    }
}
static void incremental(const char *file,unsigned char *bytes,size_t size) {
    AthenaGltf3D *whole=NULL,*stepped=NULL; AthenaGltf3DLoader *l=NULL;
    assert(!athena_gltf3d_load_memory(bytes,size,file,NULL,&whole));
    /* The source is read by the copy steps only; afterwards the loader owns a snapshot. */
    unsigned char *scratch=malloc(size); assert(scratch); memcpy(scratch,bytes,size);
    assert(!athena_gltf3d_loader_begin_memory(scratch,size,file,NULL,&l));
    assert(athena_gltf3d_loader_phase(l)==ATHENA_GLTF3D_PHASE_COPY&&athena_gltf3d_loader_source_needed(l));
    while(athena_gltf3d_loader_phase(l)==ATHENA_GLTF3D_PHASE_COPY) assert(athena_gltf3d_loader_step(l)==0);
    assert(!athena_gltf3d_loader_source_needed(l));
    memset(scratch,0xa5,size); free(scratch);
    AthenaGltf3D *early=(AthenaGltf3D *)1;
    assert(athena_gltf3d_loader_take(l,&early)<0&&!early);
    int steps=run_steps(l,0,&stepped);
    assert(steps>=3); same_scene(whole,stepped);
    AthenaGltf3D *again=(AthenaGltf3D *)1; assert(athena_gltf3d_loader_take(l,&again)<0&&!again);
    athena_gltf3d_loader_destroy(l);
    athena_gltf3d_release(stepped);
    /* Abandon after every possible step count (ASan checks the leaks). */
    for(int stop=1;stop<steps;stop++) {
        assert(!athena_gltf3d_loader_begin_memory(bytes,size,file,NULL,&l));
        assert(run_steps(l,(uint32_t)stop,&stepped)==0); athena_gltf3d_loader_destroy(l);
    }
    /* Failed steps stay failed. */
    /* Zero-copy loads and time-budgeted runs. */
    {
        uint32_t ran; int rc; unsigned calls=0; AthenaGltf3D *borrowed=NULL;
        assert(!athena_gltf3d_loader_begin(bytes,size,file,NULL,ATHENA_LOADER_BORROW,&l));
        assert(athena_gltf3d_loader_phase(l)==ATHENA_GLTF3D_PHASE_PARSE&&athena_gltf3d_loader_source_needed(l));
        while((rc=athena_gltf3d_loader_run(l,0,&ran))==0) { assert(ran==1); calls++; }
        assert(rc==1&&ran==1&&calls+1>=3);
        assert(athena_gltf3d_loader_source_needed(l)); /* borrowed until take/destroy */
        assert(!athena_gltf3d_loader_take(l,&borrowed)); same_scene(whole,borrowed);
        uint32_t units_total=0; for(int ph=0;ph<=ATHENA_GLTF3D_PHASE_DONE;ph++) { AthenaLoaderPhaseStats st; athena_gltf3d_loader_phase_stats(l,ph,&st); units_total+=st.units; assert(st.units?st.max_ms<=st.total_ms+1e-3f:!st.total_ms); }
        assert(units_total>=calls+1);
        athena_gltf3d_loader_destroy(l); athena_gltf3d_release(borrowed);
        assert(!athena_gltf3d_loader_begin(bytes,size,file,NULL,ATHENA_LOADER_BORROW,&l));
        assert((rc=athena_gltf3d_loader_run(l,1e6f,&ran))==1&&ran>=3); /* a huge budget finishes in one call */
        assert(!athena_gltf3d_loader_take(l,&borrowed)); same_scene(whole,borrowed);
        athena_gltf3d_loader_destroy(l); athena_gltf3d_release(borrowed);
        unsigned char *odd=malloc(size+1); assert(odd); memcpy(odd+1,bytes,size);
        assert(!athena_gltf3d_loader_begin(odd+1,size,file,NULL,ATHENA_LOADER_BORROW,&l));
        assert(athena_gltf3d_loader_phase(l)==ATHENA_GLTF3D_PHASE_COPY); /* unaligned: snapshot copy */
        while(athena_gltf3d_loader_phase(l)==ATHENA_GLTF3D_PHASE_COPY) assert(athena_gltf3d_loader_step(l)==0);
        assert(!athena_gltf3d_loader_source_needed(l)); memset(odd,0xa5,size+1); free(odd);
        assert(athena_gltf3d_loader_run(l,1e6f,&ran)==1); assert(!athena_gltf3d_loader_take(l,&borrowed)); same_scene(whole,borrowed);
        athena_gltf3d_loader_destroy(l); athena_gltf3d_release(borrowed);
    }
    unsigned char *bad=malloc(size); assert(bad); memcpy(bad,bytes,size); bad[0]^=0xff;
    assert(!athena_gltf3d_loader_begin_memory(bad,size,file,NULL,&l));
    int failed; while((failed=athena_gltf3d_loader_step(l))==0); assert(failed<0);
    assert(athena_gltf3d_loader_step(l)==failed);
    stepped=(AthenaGltf3D *)1; assert(athena_gltf3d_loader_take(l,&stepped)<0&&!stepped);
    athena_gltf3d_loader_destroy(l); free(bad);
    assert(athena_gltf3d_loader_begin_memory(NULL,10,file,NULL,&l)==ATHENA_MODEL3D_EINVAL&&!l);
    athena_gltf3d_loader_destroy(NULL);
    athena_gltf3d_release(whole);
#ifdef ATHENA_GLTF_MEMORY_OOM
    int exhausted=0;
    for(int i=0;i<1500;i++) {
        fail_at=i; attempts=0; stepped=NULL;
        int code=athena_gltf3d_loader_begin_memory(bytes,size,file,NULL,&l);
        if(!code) { code=run_steps(l,0,&stepped); if(code<0) { assert(athena_gltf3d_loader_step(l)==code); assert(!stepped); } athena_gltf3d_loader_destroy(l); }
        fail_at=-1;
        if(code>=0) {
            athena_gltf3d_release(stepped);
            if(attempts<=i) { exhausted=1; printf("GLB incremental OOM sweep: %s, %d allocation sites\n",file,i); break; }
        } else assert(!stepped);
    }
    assert(exhausted);
#endif
}

/* Resumable mesh loader: same mesh as athena_mesh3d_load_memory; abandoned at every step. */
static int mesh_steps(AthenaMesh3DLoader *l,unsigned stop,AthenaMesh3D **out) {
    int last=-1; unsigned steps=0; uint32_t previous=0,done,total;
    for(;;) {
        int code=athena_mesh3d_loader_step(l,7);
        if(code<0) return code;
        steps++;
        int phase=athena_mesh3d_loader_last_phase(l); assert(phase>=last); last=phase;
        athena_mesh3d_loader_progress(l,&done,&total); assert(done>=previous&&done<=total); previous=done;
        if(code==1) break;
        if(stop&&steps>=stop) return 0;
    }
    return athena_mesh3d_loader_take(l,out)<0?-1:(int)steps;
}
static void mesh_loader(const char *file,unsigned char *bytes,size_t size) {
    AthenaMesh3D *whole=NULL,*stepped=NULL; AthenaMesh3DLoader *l=NULL;
    assert(!athena_mesh3d_load_memory(bytes,size,file,NULL,&whole));
    unsigned char *scratch=malloc(size); assert(scratch); memcpy(scratch,bytes,size);
    assert(!athena_mesh3d_loader_begin_memory(scratch,size,file,NULL,&l));
    assert(athena_mesh3d_loader_phase(l)==ATHENA_MESH3D_LOADER_COPY&&athena_mesh3d_loader_source_needed(l));
    while(athena_mesh3d_loader_phase(l)==ATHENA_MESH3D_LOADER_COPY) assert(athena_mesh3d_loader_step(l,7)==0);
    assert(!athena_mesh3d_loader_source_needed(l)); memset(scratch,0xa5,size); free(scratch);
    int steps=mesh_steps(l,0,&stepped); assert(steps>=4);
    equal_mesh(whole,stepped);
    AthenaMesh3D *again=(AthenaMesh3D *)1; assert(athena_mesh3d_loader_take(l,&again)<0&&!again);
    athena_mesh3d_loader_destroy(l); athena_mesh3d_release(stepped);
    for(int stop=1;stop<steps;stop++) {
        assert(!athena_mesh3d_loader_begin_memory(bytes,size,file,NULL,&l));
        assert(mesh_steps(l,(unsigned)stop,&stepped)==0); athena_mesh3d_loader_destroy(l);
    }
    {   /* zero-copy and time-budgeted run */
        uint32_t ran; AthenaMesh3D *b=NULL; int rc;
        assert(!athena_mesh3d_loader_begin(bytes,size,file,NULL,ATHENA_LOADER_BORROW,&l));
        assert(athena_mesh3d_loader_phase(l)==ATHENA_MESH3D_LOADER_PARSE&&athena_mesh3d_loader_source_needed(l));
        assert((rc=athena_mesh3d_loader_run(l,0,&ran))==0&&ran==1);
        assert((rc=athena_mesh3d_loader_run(l,1e6f,&ran))==1&&ran>=2);
        assert(!athena_mesh3d_loader_take(l,&b)); equal_mesh(whole,b);
        AthenaLoaderPhaseStats st; athena_mesh3d_loader_phase_stats(l,ATHENA_MESH3D_LOADER_BUILD,&st); assert(st.units>=1&&st.max_ms<=st.total_ms+1e-3f);
        athena_mesh3d_loader_destroy(l); athena_mesh3d_release(b);
    }
    unsigned char *bad=malloc(size); assert(bad); memcpy(bad,bytes,size); bad[0]^=0xff;
    assert(!athena_mesh3d_loader_begin_memory(bad,size,file,NULL,&l));
    int failed; while((failed=athena_mesh3d_loader_step(l,0))==0); assert(failed<0&&athena_mesh3d_loader_step(l,0)==failed);
    athena_mesh3d_loader_destroy(l); free(bad); athena_mesh3d_release(whole);
    assert(athena_mesh3d_loader_begin_memory(NULL,10,file,NULL,&l)==ATHENA_MODEL3D_EINVAL&&!l);
#ifdef ATHENA_GLTF_MEMORY_OOM
    int exhausted=0;
    for(int i=0;i<1500;i++) {
        fail_at=i; attempts=0; stepped=NULL;
        int code=athena_mesh3d_loader_begin_memory(bytes,size,file,NULL,&l);
        if(!code) { code=mesh_steps(l,0,&stepped); athena_mesh3d_loader_destroy(l); }
        fail_at=-1;
        if(code>=0) { athena_mesh3d_release(stepped); if(attempts<=i) { exhausted=1; break; } }
        else assert(!stepped);
    }
    assert(exhausted);
#endif
}
static void equivalent(const char *file) {
    size_t size; unsigned char *bytes=read_bytes(file,&size),*unaligned=malloc(size+1); assert(unaligned);
    memcpy(unaligned+1,bytes,size);
    AthenaGltf3D *disk=NULL,*memory=NULL;
    assert(!athena_gltf3d_load(file,NULL,&disk));
    assert(!athena_gltf3d_load_memory(unaligned+1,size,"unavailable:/origin.glb",NULL,&memory));
    // Source is no longer needed by geometry, skin, morph, clips or pixels.
    memset(unaligned,0xa5,size+1); free(unaligned);
    uint32_t count=athena_gltf3d_node_count(disk);
    assert(count==athena_gltf3d_node_count(memory));
    assert(athena_gltf3d_clip_count(disk)==athena_gltf3d_clip_count(memory));
    for(uint32_t i=0;i<count;i++) {
        assert(!strcmp(athena_gltf3d_node_name(disk,i),athena_gltf3d_node_name(memory,i)));
        equal_mesh(athena_node3d_mesh(athena_gltf3d_node(disk,i)),athena_node3d_mesh(athena_gltf3d_node(memory,i)));
    }
    for(uint32_t i=0;i<athena_gltf3d_clip_count(memory);i++) {
        assert(!strcmp(athena_gltf3d_clip_name(disk,i),athena_gltf3d_clip_name(memory,i)));
        assert(athena_clip3d_duration(athena_gltf3d_clip(disk,i))==athena_clip3d_duration(athena_gltf3d_clip(memory,i)));
        AthenaNode3D *nodes[16]; assert(count<=16);
        for(uint32_t n=0;n<count;n++) nodes[n]=athena_gltf3d_node(memory,n);
        AthenaPlayer3D *player=athena_player3d_create(athena_gltf3d_clip(memory,i),nodes,count); assert(player);
        assert(!athena_player3d_set_time(player,.5f)); athena_player3d_release(player);
    }
    athena_gltf3d_release(disk); athena_gltf3d_release(memory);
    if(strstr(file,"lit_cube")) {
        AthenaMesh3D *a=NULL,*b=NULL;
        assert(!athena_mesh3d_load(file,&a));
        assert(!athena_mesh3d_load_memory(bytes,size,file,NULL,&b)); equal_mesh(a,b);
        athena_mesh3d_release(a); athena_mesh3d_release(b);
    }
    for(size_t n=0;n<size;n+=size/13+1) {
        memory=(AthenaGltf3D *)1;
        assert(athena_gltf3d_load_memory(bytes,n,file,NULL,&memory)<0&&!memory);
    }
    assert(athena_gltf3d_load_memory(bytes,size-1,file,NULL,&memory)<0&&!memory);
#ifdef ATHENA_GLTF_MEMORY_OOM
    int exhausted=0;
    for(int i=0;i<1500;i++) {
        fail_at=i; attempts=0;
        int code=athena_gltf3d_load_memory(bytes,size,file,NULL,&memory);
        fail_at=-1;
        if(!code) {
            athena_gltf3d_release(memory);
            if(attempts<=i) { exhausted=1; printf("GLB OOM sweep: %s, %d allocation sites\n",file,i); break; }
            continue; /* Some optional allocations can fail without rejecting the asset. */
        }
        assert(code<0 && !memory);
    }
    assert(exhausted);
#endif
    incremental(file,bytes,size);
    if(strstr(file,"lit_cube")) mesh_loader(file,bytes,size);
    free(bytes);
}

/* Explicit resolver: .gltf/.glb with external files, read by the caller. */
#define MAX_HELD 8
static unsigned char *held[MAX_HELD]; static unsigned held_count;
static void release_held(void) { while(held_count) free(held[--held_count]); }
static void provide_gltf(AthenaGltf3DLoader *l,const char *dir,uint32_t only_first) {
    for(uint32_t i=0;i<athena_gltf3d_loader_dependency_count(l)&&i<only_first;i++) {
        if(athena_gltf3d_loader_dependency_provided(l,i)) continue;
        char path[512]; snprintf(path,sizeof(path),"%s%s",dir,athena_gltf3d_loader_dependency_uri(l,i));
        size_t size; assert(held_count<MAX_HELD); held[held_count]=read_bytes(path,&size);
        assert(!athena_gltf3d_loader_provide(l,i,held[held_count++],size));
        assert(athena_gltf3d_loader_provide(l,i,held[held_count-1],size)<0); /* once only */
    }
}
static int run_external(AthenaGltf3DLoader *l,const char *dir,AthenaGltf3D **out) {
    for(;;) {
        int code=athena_gltf3d_loader_step(l);
        if(code==ATHENA_GLTF3D_WAITING) { provide_gltf(l,dir,UINT32_MAX); continue; }
        if(code<0) return code;
        if(code==1) break;
    }
    return athena_gltf3d_loader_take(l,out);
}
static void external_gltf(const char *dir,const char *name,uint32_t expect_dependencies) {
    char path[512]; snprintf(path,sizeof(path),"%s%s",dir,name);
    size_t size; unsigned char *bytes=read_bytes(path,&size);
    AthenaGltf3D *disk=NULL,*ext=NULL; AthenaGltf3DLoader *l=NULL;
    assert(!athena_gltf3d_load(path,NULL,&disk));
    assert(!athena_gltf3d_loader_begin_external(bytes,size,path,NULL,&l));
    int code; while(athena_gltf3d_loader_phase(l)!=ATHENA_GLTF3D_PHASE_DEPENDENCIES&&!(code=athena_gltf3d_loader_step(l)));
    assert(athena_gltf3d_loader_dependency_count(l)==expect_dependencies);
    assert(athena_gltf3d_loader_step(l)==ATHENA_GLTF3D_WAITING&&athena_gltf3d_loader_step(l)==ATHENA_GLTF3D_WAITING);
    assert(athena_gltf3d_loader_provide(l,99,bytes,size)<0&&!athena_gltf3d_loader_dependency_provided(l,0));
    assert(athena_gltf3d_loader_take(l,&ext)<0);
    provide_gltf(l,dir,1); /* partially provided: still waiting */
    if(expect_dependencies>1) assert(athena_gltf3d_loader_step(l)==ATHENA_GLTF3D_WAITING);
    assert(!run_external(l,dir,&ext)); same_scene(disk,ext);
    athena_gltf3d_loader_destroy(l); athena_gltf3d_release(ext); athena_gltf3d_release(disk); release_held();
    /* Abandoned while waiting and mid-build. */
    for(int stop=0;stop<12;stop++) {
        assert(!athena_gltf3d_loader_begin_external(bytes,size,path,NULL,&l));
        for(int i=0;i<stop;i++) { int c=athena_gltf3d_loader_step(l); if(c==ATHENA_GLTF3D_WAITING) provide_gltf(l,dir,UINT32_MAX); else if(c) break; }
        athena_gltf3d_loader_destroy(l); release_held();
    }
    /* One file missing: the load fails at validation, not silently. */
    assert(!athena_gltf3d_loader_begin_external(bytes,size,path,NULL,&l));
    while(athena_gltf3d_loader_phase(l)!=ATHENA_GLTF3D_PHASE_DEPENDENCIES) assert(athena_gltf3d_loader_step(l)==0);
    uint32_t n=athena_gltf3d_loader_dependency_count(l); assert(n>0);
    unsigned char tiny[1]={0};
    for(uint32_t i=0;i<n;i++) assert(!athena_gltf3d_loader_provide(l,i,tiny,1));
    while((code=athena_gltf3d_loader_step(l))==0) { }
    assert(code<0);
    athena_gltf3d_loader_destroy(l);
#ifdef ATHENA_GLTF_MEMORY_OOM
    int exhausted=0;
    for(int i=0;i<2500;i++) {
        fail_at=i; attempts=0; ext=NULL;
        code=athena_gltf3d_loader_begin_external(bytes,size,path,NULL,&l);
        if(!code) { code=run_external(l,dir,&ext); athena_gltf3d_loader_destroy(l); }
        fail_at=-1; release_held();
        if(code>=0) { athena_gltf3d_release(ext); if(attempts<=i) { exhausted=1; printf("GLB external OOM sweep: %s, %d allocation sites\n",name,i); break; } }
        else assert(!ext);
    }
    assert(exhausted);
#endif
    free(bytes);
}
static void external_rules(const char *dir) {
    char path[512]; snprintf(path,sizeof(path),"%sext_textured.gltf",dir);
    size_t size; unsigned char *bytes=read_bytes(path,&size);
    const char *bad[]={"../ext_textured.bin","/etc/x.bin","C:x.bin","http://x/y.bin","a/../../b.bin"};
    for(unsigned k=0;k<5;k++) {
        char *text=malloc(size+64); assert(text); memcpy(text,bytes,size); text[size]=0;
        char *at=strstr(text,"ext_textured.bin"); assert(at);
        size_t tail=strlen(at+16)+1; memmove(at+strlen(bad[k]),at+16,tail); memcpy(at,bad[k],strlen(bad[k]));
        AthenaGltf3DLoader *l=NULL; assert(!athena_gltf3d_loader_begin_external(text,strlen(text),path,NULL,&l));
        int code; while((code=athena_gltf3d_loader_step(l))==0&&athena_gltf3d_loader_phase(l)!=ATHENA_GLTF3D_PHASE_DEPENDENCIES);
        assert(code==ATHENA_MODEL3D_EUNSUPPORTED&&strstr(athena_model3d_detail(),"uri"));
        athena_gltf3d_loader_destroy(l); free(text);
    }
    /* The strict memory loader still refuses external files. */
    AthenaGltf3D *g=NULL; assert(athena_gltf3d_load_memory(bytes,size,path,NULL,&g)<0&&!g);
    free(bytes);
}
static void external_mesh(const char *dir) {
    char path[512]; snprintf(path,sizeof(path),"%sext_textured.gltf",dir);
    size_t size; unsigned char *bytes=read_bytes(path,&size);
    AthenaMesh3D *disk=NULL,*ext=NULL; AthenaMesh3DLoader *l=NULL;
    assert(!athena_mesh3d_load(path,&disk));
    assert(!athena_mesh3d_loader_begin_external(bytes,size,path,NULL,&l));
    int code;
    for(;;) {
        code=athena_mesh3d_loader_step(l,0);
        if(code==ATHENA_MESH3D_WAITING) {
            assert(athena_mesh3d_loader_dependency_count(l)==2);
            for(uint32_t i=0;i<2;i++) {
                char p[512]; snprintf(p,sizeof(p),"%s%s",dir,athena_mesh3d_loader_dependency_uri(l,i));
                size_t n; assert(held_count<MAX_HELD); held[held_count]=read_bytes(p,&n);
                assert(!athena_mesh3d_loader_provide(l,i,held[held_count++],n));
            }
            continue;
        }
        assert(code>=0); if(code==1) break;
    }
    assert(!athena_mesh3d_loader_take(l,&ext)); equal_mesh(disk,ext);
    athena_mesh3d_loader_destroy(l); athena_mesh3d_release(ext); athena_mesh3d_release(disk); release_held(); free(bytes);
}
#ifdef _EE
int athena_main(int argc,char **argv) {
    (void)argc; (void)argv;
#else
int main(void) {
#endif
#ifdef _EE
    const char *prefix="3d/";
#else
    const char *prefix="tests/host/3d/";
#endif
    const char *models[]={"models/lit_cube.glb","models/arm.glb","models/bend.glb","models/morph.glb"};
    char path[256];
    for(unsigned i=0;i<4;i++) {
#ifdef _EE
        snprintf(path,sizeof(path),"%s",models[i]);
#else
        snprintf(path,sizeof(path),"bin/%s",models[i]);
#endif
        equivalent(path);
    }
    external_gltf(prefix,"ext_textured.gltf",2); external_gltf(prefix,"ext_bend.gltf",1);
    external_gltf(prefix,"memory_external_image.glb",1); external_rules(prefix); external_mesh(prefix);
    snprintf(path,sizeof(path),"%smemory_shared.glb",prefix); equivalent(path);
    size_t size; unsigned char *bytes=read_bytes(path,&size);
    AthenaGltf3D *g=NULL; assert(!athena_gltf3d_load_memory(bytes,size,path,NULL,&g));
    AthenaNode3D *node=athena_gltf3d_node(g,0); AthenaMesh3DView x,y;
    athena_mesh3d_view(athena_node3d_mesh(node),&x);
    athena_mesh3d_view(athena_node3d_mesh(athena_node3d_child(node,0)),&y);
    assert(x.material.texture && x.material.texture==y.material.texture);
    athena_gltf3d_release(g); free(bytes);
    for(unsigned i=0;i<2;i++) {
        snprintf(path,sizeof(path),"%smemory_external_%s.glb",prefix,i?"buffer":"image");
        bytes=read_bytes(path,&size);
        assert(athena_gltf3d_load_memory(bytes,size,path,NULL,&g)==ATHENA_MODEL3D_EUNSUPPORTED&&!g);
        assert(strstr(athena_model3d_detail(),i?"URI":"external")); free(bytes);
    }
    assert(athena_gltf3d_load_memory(NULL,100,"test.glb",NULL,&g)==ATHENA_MODEL3D_EINVAL&&!g);
    puts("GLB memory native test passed: disk/memory, unaligned, borrowed lifetime, truncation, skin/morph/clips, shared textures, no external I/O");
    athena_model3d_module_shutdown();
#ifdef _EE
    SleepThread();
#endif
    return 0;
}
