#include <assert.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <athena/nav.h>

/* Count allocator calls inside searches, not grid/crowd construction. */
void *__real_malloc(size_t size);
void *__real_calloc(size_t count,size_t size);
void *__real_realloc(void *ptr,size_t size);
static int measuring;
static unsigned allocations;
static int fail_after=-1;
static int fail_allocation(void) { return fail_after>=0&&fail_after--==0; }
void *__wrap_malloc(size_t size) { if(measuring) allocations++; return fail_allocation()?NULL:__real_malloc(size); }
void *__wrap_calloc(size_t count,size_t size) { if(measuring) allocations++; return fail_allocation()?NULL:__real_calloc(count,size); }
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

static AthenaNavRequestInfo info(AthenaNavRequest *r) {
    AthenaNavRequestInfo i;
    assert(athena_nav_request_info(r,&i)==0); return i;
}
static void finish(AthenaNavGrid *g,AthenaNavRequest *r,uint32_t work) {
    AthenaNavBudget budget={work,0};
    unsigned frames=0;
    while(info(r).state==ATHENA_NAV_PENDING) {
        measuring=1;
        int done=athena_nav_step_requests(g,&budget);
        measuring=0;
        assert(done>0&&(uint32_t)done<=work);
        assert(++frames<2000000);
    }
}
static void incremental_tests(AthenaNavGrid *g,AthenaNavCrowd *crowd,int agent) {
    float reference[ATHENA_NAV_MAX_PATH*2],output[ATHENA_NAV_MAX_PATH*2];
    const uint32_t limits[]={1,2,7,128,4096};
    for(int diagonal=0;diagonal<=1;diagonal++) for(int smooth=0;smooth<=1;smooth++) {
        AthenaNavQuery query={diagonal,smooth,0};
        int n=athena_nav_find_path(g,1.25f,1.75f,60.75f,1.25f,&query,reference,ATHENA_NAV_MAX_PATH);
        assert(n>2);
        for(unsigned j=0;j<sizeof(limits)/sizeof(limits[0]);j++) {
            int error;
            AthenaNavRequest *r=athena_nav_request_create(g,1.25f,1.75f,60.75f,1.25f,&query,ATHENA_NAV_MAX_PATH,&error);
            assert(r&&error==0&&info(r).state==ATHENA_NAV_PENDING);
            assert(athena_nav_find_path(g,1,1,2,2,NULL,output,ATHENA_NAV_MAX_PATH)==ATHENA_NAV_EBUSY);
            AthenaNavAgentInfo before,after;
            assert(athena_nav_agent_info(crowd,agent,&before)==0);
            assert(athena_nav_agent_move_to(crowd,agent,5,5)==ATHENA_NAV_EBUSY);
            assert(athena_nav_agent_info(crowd,agent,&after)==0&&before.waypoints==after.waypoints&&before.state==after.state);
            finish(g,r,limits[j]);
            assert(info(r).state==ATHENA_NAV_FOUND&&info(r).points==(uint32_t)n&&info(r).work>info(r).expanded);
            assert(athena_nav_request_copy(r,output,ATHENA_NAV_MAX_PATH)==n);
            for(int k=0;k<n*2;k++) assert(reference[k]==output[k]);
            uint32_t points; const float *borrowed=athena_nav_request_path(r,&points);
            assert(points==(uint32_t)n&&borrowed&&borrowed[0]==1.25f&&borrowed[1]==1.75f);
            float small[6]={11,22,33,44,123,456};
            assert(athena_nav_request_copy(r,small,2)==ATHENA_NAV_EOUTPUT);
            assert(small[0]==11&&small[4]==123&&small[5]==456);
            athena_nav_request_release(r);
        }
    }
    assert(allocations==0);
    /* FIFO completion, cancellation of queued/active entries, bounded queue. */
    AthenaNavRequest *queue[ATHENA_NAV_MAX_PENDING]; int error;
    for(unsigned j=0;j<ATHENA_NAV_MAX_PENDING;j++) {
        queue[j]=athena_nav_request_create(g,1.5f,1.5f,60.5f,1.5f,NULL,ATHENA_NAV_MAX_PATH,&error);
        assert(queue[j]);
    }
    assert(!athena_nav_request_create(g,1,1,2,2,NULL,4,&error)&&error==ATHENA_NAV_EFULL);
    assert(athena_nav_pending_requests(g)==ATHENA_NAV_MAX_PENDING);
    assert(athena_nav_step_requests(g,&(AthenaNavBudget){2,0})==2);
    assert(info(queue[0]).expanded==1&&info(queue[1]).work==0);
    athena_nav_request_cancel(queue[5]);
    assert(info(queue[5]).state==ATHENA_NAV_CANCELLED&&athena_nav_pending_requests(g)==ATHENA_NAV_MAX_PENDING-1);
    finish(g,queue[0],31);
    assert(info(queue[1]).state==ATHENA_NAV_PENDING); /* The last batch may start it but cannot finish this maze. */
    athena_nav_request_release(queue[1]); queue[1]=NULL;
    athena_nav_cancel_requests(g);
    assert(!athena_nav_pending_requests(g));
    for(unsigned j=0;j<ATHENA_NAV_MAX_PENDING;j++) athena_nav_request_release(queue[j]);
    /* Mutation invalidates all unfinished phases, but no-op writes do not. */
    AthenaNavRequest *r=athena_nav_request_create(g,1,1,2,2,NULL,4,&error);
    uint32_t revision=athena_nav_grid_revision(g);
    athena_nav_set_cost(g,1,1,1);
    assert(athena_nav_grid_revision(g)==revision&&info(r).state==ATHENA_NAV_PENDING);
    athena_nav_set_cost(g,1,1,9);
    assert(info(r).state==ATHENA_NAV_ERROR&&info(r).error==ATHENA_NAV_EREVISION);
    athena_nav_request_release(r); athena_nav_set_cost(g,1,1,1);
    r=athena_nav_request_create(g,1,1,60,1,NULL,ATHENA_NAV_MAX_PATH,&error);
    assert(r); finish(g,r,64);
    athena_nav_fill(g,0,0,2,2,5);
    assert(info(r).state==ATHENA_NAV_FOUND); /* immutable completed output */
    athena_nav_request_release(r); athena_nav_fill(g,0,0,2,2,1);
    r=athena_nav_request_create(g,1,1,60,1,&(AthenaNavQuery){1,0,0},2,&error);
    finish(g,r,1);
    assert(info(r).state==ATHENA_NAV_ERROR&&info(r).error==ATHENA_NAV_EOUTPUT&&info(r).points==0);
    athena_nav_request_release(r);
    r=athena_nav_request_create(g,1,1,60,1,&(AthenaNavQuery){1,1,1},512,&error);
    finish(g,r,1);
    assert(info(r).state==ATHENA_NAV_ERROR&&info(r).error==ATHENA_NAV_EITERATIONS);
    athena_nav_request_release(r);
    /* Small time budgets still make progress, and the last phase is bounded. */
    r=athena_nav_request_create(g,1.5f,1.5f,60.5f,1.5f,NULL,512,&error);
    unsigned frames=0;
    measuring=1;
    while(info(r).state==ATHENA_NAV_PENDING) {
        int done=athena_nav_step_requests(g,&(AthenaNavBudget){32,.000001f});
        assert(done>0&&done<=32&&++frames<2000000);
    }
    measuring=0;
    assert(info(r).state==ATHENA_NAV_FOUND&&frames>100&&allocations==0);
    athena_nav_request_release(r);
    assert(!athena_nav_request_create(g,FLT_MAX,FLT_MAX,1,1,NULL,512,&error)&&error==ATHENA_NAV_EINVAL);
    r=athena_nav_request_create(g,-1,-1,1,1,NULL,512,&error);
    finish(g,r,1); assert(info(r).state==ATHENA_NAV_NO_PATH); athena_nav_request_release(r);
    assert(athena_nav_step_requests(g,&(AthenaNavBudget){0,0})==ATHENA_NAV_EINVAL);
    assert(athena_nav_step_requests(g,&(AthenaNavBudget){1,-1})==ATHENA_NAV_EINVAL);
    for(int fail=0;fail<2;fail++) {
        fail_after=fail;
        assert(!athena_nav_request_create(g,1,1,2,2,NULL,4,&error)&&error==ATHENA_NAV_ENOMEM);
        fail_after=-1;
        assert(!athena_nav_pending_requests(g));
    }
    /* An independent live request owns the native grid after the caller drops it. */
    AthenaNavGrid *tiny=athena_nav_grid_create(2,2,1,0,0);
    r=athena_nav_request_create(tiny,.5f,.5f,1.5f,1.5f,NULL,2,&error);
    athena_nav_grid_release(tiny);
    finish(tiny,r,1); assert(info(r).state==ATHENA_NAV_FOUND);
    athena_nav_request_release(r);
}

static AthenaNavAgentInfo agent_info(AthenaNavCrowd *c,int id) {
    AthenaNavAgentInfo a; assert(athena_nav_agent_info(c,id,&a)==0); return a;
}
static void crowd_requests(void) {
    AthenaNavGrid *g=athena_nav_grid_create(32,32,1,0,0);
    athena_nav_fill(g,16,0,16,27,0);
    AthenaNavCrowd *c=athena_nav_crowd_create(g),*other=athena_nav_crowd_create(g);
    AthenaNavAgentDesc desc={10,1,0};
    int id=athena_nav_agent_add(c,1.5f,2,1.5f,&desc);
    int neighbour=athena_nav_agent_add(c,1.5f,2,1.5f,&desc),error;
    assert(agent_info(c,id).planning_state==ATHENA_NAV_NOT_REQUESTED);
    assert(athena_nav_agent_move_to(c,id,30.5f,1.5f)==1);
    AthenaNavAgentInfo before=agent_info(c,id);
    assert(!athena_nav_agent_request_move_to(c,id,FLT_MAX,FLT_MAX,&error)&&error==ATHENA_NAV_EINVAL);
    assert(agent_info(c,id).state==before.state&&agent_info(c,id).waypoints==before.waypoints);
    float reference[1024];
    int n=athena_nav_find_path(g,1.5f,1.5f,30.5f,1.5f,NULL,reference,512);
    AthenaNavRequest *r=athena_nav_agent_request_move_to(c,id,30.5f,1.5f,&error);
    assert(r&&agent_info(c,id).state==ATHENA_NAV_PLANNING&&agent_info(c,id).planning_state==ATHENA_NAV_PENDING);
    assert(athena_nav_crowd_update(c,1)==0&&agent_info(c,id).x==1.5f&&agent_info(c,id).z==1.5f);
    assert(agent_info(c,neighbour).x==1.5f); /* Separation cannot move the search start. */
    finish(g,r,1);
    assert(agent_info(c,id).state==ATHENA_NAV_MOVING&&agent_info(c,id).waypoints==(uint32_t)n);
    float path[1024]; assert(athena_nav_request_copy(r,path,512)==n&&!memcmp(reference,path,(size_t)n*8));
    assert(agent_info(c,id).planning_state==ATHENA_NAV_FOUND);
    athena_nav_request_release(r);
    athena_nav_agent_remove(c,neighbour);
    for(unsigned frames=0;agent_info(c,id).state!=ATHENA_NAV_ARRIVED;frames++) {
        assert(frames<1000); athena_nav_crowd_update(c,.1f);
    }
    assert(agent_info(c,id).x==30.5f&&agent_info(c,id).y==2&&agent_info(c,id).z==1.5f);
    athena_nav_agent_set_position(c,id,1.5f,2,1.5f);
    /* All terminal outcomes are adopted during the bounded step. */
    r=athena_nav_agent_request_move_to(c,id,16.5f,1.5f,&error);
    finish(g,r,1); assert(info(r).state==ATHENA_NAV_NO_PATH&&agent_info(c,id).state==ATHENA_NAV_IDLE);
    athena_nav_request_release(r);
    athena_nav_crowd_set_query(c,&(AthenaNavQuery){1,1,1});
    r=athena_nav_agent_request_move_to(c,id,30.5f,1.5f,&error);
    finish(g,r,1); assert(agent_info(c,id).planning_error==ATHENA_NAV_EITERATIONS);
    athena_nav_request_release(r); athena_nav_crowd_set_query(c,&(AthenaNavQuery){1,1,0});
    r=athena_nav_agent_request_move_to(c,id,30.5f,1.5f,&error);
    athena_nav_set_cost(g,0,0,2);
    assert(info(r).error==ATHENA_NAV_EREVISION&&agent_info(c,id).planning_error==ATHENA_NAV_EREVISION);
    athena_nav_request_release(r);
    /* Failed replacement is atomic; successful replacement goes to the FIFO tail. */
    r=athena_nav_agent_request_move_to(c,id,30.5f,1.5f,&error);
    for(int fail=0;fail<2;fail++) {
        fail_after=fail;
        assert(!athena_nav_agent_request_move_to(c,id,2.5f,2.5f,&error)&&error==ATHENA_NAV_ENOMEM);
        fail_after=-1; assert(info(r).state==ATHENA_NAV_PENDING&&agent_info(c,id).state==ATHENA_NAV_PLANNING);
    }
    AthenaNavRequest *queue[15];
    for(unsigned i=0;i<15;i++) { queue[i]=athena_nav_request_create(g,1,1,2,2,NULL,512,&error); assert(queue[i]); }
    int oid=athena_nav_agent_add(other,1,0,1,&desc);
    assert(!athena_nav_agent_request_move_to(other,oid,2,2,&error)&&error==ATHENA_NAV_EFULL);
    AthenaNavRequest *replacement=athena_nav_agent_request_move_to(c,id,2.5f,2.5f,&error);
    assert(replacement&&info(r).state==ATHENA_NAV_CANCELLED&&athena_nav_pending_requests(g)==16);
    assert(athena_nav_step_requests(g,&(AthenaNavBudget){1,0})==1&&info(replacement).work==0&&info(queue[0]).work==1);
    athena_nav_request_release(r);
    athena_nav_crowd_cancel_requests(c);
    assert(info(replacement).state==ATHENA_NAV_CANCELLED&&athena_nav_pending_requests(g)==15);
    athena_nav_request_release(replacement);
    for(unsigned i=0;i<15;i++) athena_nav_request_release(queue[i]);
    for(int action=0;action<4;action++) {
        r=athena_nav_agent_request_move_to(c,id,30.5f,1.5f,&error);
        assert(athena_nav_step_requests(g,&(AthenaNavBudget){2,0})==2);
        if(action==0) athena_nav_agent_stop(c,id);
        if(action==1) athena_nav_agent_set_position(c,id,1.5f,2,1.5f);
        if(action==2) athena_nav_request_cancel(r);
        if(action==3) {
            athena_nav_agent_remove(c,id);
            int reused=athena_nav_agent_add(c,1.5f,2,1.5f,&desc);
            assert(reused==id&&agent_info(c,id).planning_state==ATHENA_NAV_NOT_REQUESTED);
        }
        assert(info(r).state==ATHENA_NAV_CANCELLED&&!athena_nav_pending_requests(g));
        athena_nav_request_release(r);
    }
    /* Dropping the observer keeps planning alive; completion frees owned state
     * and output inside step, with no allocation and no persistent owner cycle. */
    r=athena_nav_agent_request_move_to(c,id,2.5f,2.5f,&error); athena_nav_request_release(r);
    measuring=1;
    while(athena_nav_pending_requests(g)) assert(athena_nav_step_requests(g,&(AthenaNavBudget){1,0})==1);
    measuring=0; assert(agent_info(c,id).state==ATHENA_NAV_MOVING&&allocations==0);
    r=athena_nav_agent_request_move_to(c,id,30.5f,1.5f,&error);
    AthenaNavRequest *other_r=athena_nav_agent_request_move_to(other,oid,2,2,&error);
    athena_nav_crowd_release(c); /* Live result observers must not retain the crowd. */
    assert(info(r).state==ATHENA_NAV_CANCELLED&&info(other_r).state==ATHENA_NAV_PENDING);
    athena_nav_request_release(r); finish(g,other_r,3); athena_nav_request_release(other_r);
    athena_nav_crowd_release(other); athena_nav_grid_release(g);
    /* Agent path capacity and snapshot of the crowd's query are explicit. */
    g=athena_nav_grid_create(1,600,1,0,0); c=athena_nav_crowd_create(g);
    id=athena_nav_agent_add(c,.5f,0,.5f,&desc);
    athena_nav_crowd_set_query(c,&(AthenaNavQuery){1,0,0});
    r=athena_nav_agent_request_move_to(c,id,.5f,599.5f,&error);
    athena_nav_crowd_set_query(c,&(AthenaNavQuery){1,1,0});
    finish(g,r,7);
    assert(info(r).state==ATHENA_NAV_ERROR&&info(r).error==ATHENA_NAV_EOUTPUT);
    assert(agent_info(c,id).planning_error==ATHENA_NAV_EOUTPUT&&agent_info(c,id).waypoints==0);
    athena_nav_request_release(r);
    r=athena_nav_agent_request_move_to(c,id,.5f,599.5f,&error);
    finish(g,r,4096); assert(info(r).state==ATHENA_NAV_FOUND&&info(r).points==2&&agent_info(c,id).waypoints==2);
    athena_nav_request_release(r); athena_nav_crowd_release(c); athena_nav_grid_release(g);
    assert(allocations==0);
}

static void varied_paths(void) {
    AthenaNavGrid *g=athena_nav_grid_create(16,16,.5f,-4,-8);
    float reference[514],output[514]; uint8_t costs[256]; uint32_t seed=1234567;
    /* Fixed weighted/blocked maps exercise both reconstruction and LOS with
     * many tie orders, fractional endpoints and transformed cell coordinates. */
    for(unsigned map=0;map<30;map++) {
        for(unsigned i=0;i<256;i++) { seed=seed*1664525u+1013904223u; costs[i]=(seed>>24)%5; }
        costs[0]=costs[255]=1;
        athena_nav_set_costs(g,costs,256);
        for(int diagonal=0;diagonal<2;diagonal++) for(int smooth=0;smooth<2;smooth++) {
            AthenaNavQuery q={diagonal,smooth,0};
            int n=athena_nav_find_path(g,-3.9f,-7.9f,3.8f,-.2f,&q,reference,257),error;
            assert(n>=0);
            AthenaNavRequest *r=athena_nav_request_create(g,-3.9f,-7.9f,3.8f,-.2f,&q,257,&error);
            assert(r); finish(g,r,1+map%11);
            assert(info(r).state==(n?ATHENA_NAV_FOUND:ATHENA_NAV_NO_PATH));
            assert(athena_nav_request_copy(r,output,257)==n);
            assert(!memcmp(reference,output,(size_t)n*8));
            athena_nav_request_release(r);
        }
    }
#ifdef ATHENA_NAV_TEST_HOOKS
    void athena_nav_test_search_wrap(AthenaNavGrid *);
    athena_nav_fill(g,0,0,15,15,1);
    int n=athena_nav_find_path(g,-3.9f,-7.9f,3.8f,-.2f,NULL,reference,257),error;
    assert(n>0);
    for(int sync=0;sync<2;sync++) {
        athena_nav_test_search_wrap(g);
        AthenaNavRequest *r=athena_nav_request_create(g,-3.9f,-7.9f,3.8f,-.2f,NULL,257,&error);
        assert(athena_nav_step_requests(g,&(AthenaNavBudget){18,0})==18);
        assert(info(r).phase==ATHENA_NAV_CLEAR_STAMPS&&info(r).expanded==0);
        athena_nav_request_release(r); /* Cancel part way through stamp clearing. */
        if(sync) assert(athena_nav_find_path(g,-3.9f,-7.9f,3.8f,-.2f,NULL,output,257)==n);
        else {
            r=athena_nav_request_create(g,-3.9f,-7.9f,3.8f,-.2f,NULL,257,&error);
            finish(g,r,1); assert(athena_nav_request_copy(r,output,257)==n);
            athena_nav_request_release(r);
        }
        assert(!memcmp(reference,output,(size_t)n*8));
    }
#endif
    athena_nav_grid_release(g);
    assert(allocations==0);
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
    assert(athena_nav_agent_move_to(crowd,agent,FLT_MAX,FLT_MAX)==0);
    assert(athena_nav_agent_move_to(crowd,agent,60.5f,1.5f)==1);
    int32_t cx,cz;
    assert(athena_nav_world_to_cell(g,FLT_MAX,FLT_MAX,&cx,&cz)==-1);
    incremental_tests(g,crowd,agent);
    athena_nav_crowd_release(crowd);
    athena_nav_grid_release(g);
    varied_paths();
    crowd_requests();
    puts("Nav native searches and incremental FIFO: zero step allocations, exact paths, budgets, lifecycle, revisions and output bounds passed");
    return 0;
}
