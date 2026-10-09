#include <assert.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <athena/nav.h>

/* Count allocator calls inside searches, not grid/crowd construction. */
void *__real_malloc(size_t size);
void *__real_calloc(size_t count,size_t size);
void *__real_realloc(void *ptr,size_t size);
static int measuring;
static unsigned allocations;
void *__wrap_malloc(size_t size) { if(measuring) allocations++; return __real_malloc(size); }
void *__wrap_calloc(size_t count,size_t size) { if(measuring) allocations++; return __real_calloc(count,size); }
void *__wrap_realloc(void *ptr,size_t size) { if(measuring) allocations++; return __real_realloc(ptr,size); }

/* Node binding is covered by nav_test.js with the real Scene3D module. */
void athena_node3d_retain(AthenaNode3D *node) { assert(!node); }
void athena_node3d_release(AthenaNode3D *node) { assert(!node); }
int athena_node3d_set_position(AthenaNode3D *node,float x,float y,float z) {
    (void)x; (void)y; (void)z; assert(!node); return 0;
}
int athena_node3d_set_euler(AthenaNode3D *node,float x,float y,float z) {
    (void)x; (void)y; (void)z; assert(!node); return 0;
}

int main(void) {
    AthenaNavGrid *g=athena_nav_grid_create(64,64,1,0,0);
    assert(g);
    assert(athena_nav_fill(g,30,0,30,55,0)==56);
    AthenaNavCrowd *crowd=athena_nav_crowd_create(g);
    AthenaNavAgentDesc desc={3,.4f,0};
    int agent=athena_nav_agent_add(crowd,1.5f,0,1.5f,&desc);
    assert(crowd&&agent>=0);
    float path[ATHENA_NAV_MAX_PATH*2];
    measuring=1;
    for(unsigned i=0;i<100;i++) {
        int n=athena_nav_find_path(g,1.5f,1.5f,60.5f,1.5f,NULL,path,ATHENA_NAV_MAX_PATH);
        assert(n>2);
        for(int k=1;k<n;k++) assert(athena_nav_line_of_sight(g,path[(k-1)*2],path[(k-1)*2+1],path[k*2],path[k*2+1])==1);
        assert(athena_nav_agent_move_to(crowd,agent,60.5f,1.5f)==1);
    }
    measuring=0;
    assert(allocations==0);
    /* A truncated output never writes beyond capacity and a subsequent
     * search may reuse the same grid scratch normally. */
    AthenaNavQuery raw={1,0,0};
    float small[6]={0,0,0,0,123,456};
    assert(athena_nav_find_path(g,1.5f,1.5f,60.5f,1.5f,&raw,small,2)==-2);
    assert(small[4]==123&&small[5]==456);
    assert(athena_nav_find_path(g,1.5f,1.5f,60.5f,1.5f,NULL,path,ATHENA_NAV_MAX_PATH)>2);
    assert(athena_nav_line_of_sight(g,FLT_MAX,FLT_MAX,1,1)==-1);
    int32_t cx,cz;
    assert(athena_nav_world_to_cell(g,FLT_MAX,FLT_MAX,&cx,&cz)==-1);
    athena_nav_crowd_release(crowd);
    athena_nav_grid_release(g);
    puts("Nav native searches: zero allocations, reusable scratch, output bounds and extreme coordinates passed");
    return 0;
}
