/* Native-runtime consumer: build in an isolated configured checkout with
 * make DEBUG=1 RUNTIME=native APP_SRCS=tests/ee/nav_incremental.c
 * Uses only declared Nav dependencies (no Profiler/Bench requirement). */
#include <assert.h>
#include <stdio.h>
#include <kernel.h>
#include <athena/nav.h>

int athena_main(int argc,char **argv) {
    (void)argc; (void)argv;
    AthenaNavGrid *grid=athena_nav_grid_create(64,64,1,0,0);
    assert(grid);
    athena_nav_fill(grid,30,0,30,55,0);
    float expected[1024],output[1024];
    int points=athena_nav_find_path(grid,1.5f,1.5f,60.5f,1.5f,NULL,expected,512),error;
    assert(points>2);
    AthenaNavRequest *request=athena_nav_request_create(grid,1.5f,1.5f,60.5f,1.5f,NULL,512,&error);
    assert(request&&error==0);
    assert(athena_nav_find_path(grid,1,1,2,2,NULL,output,512)==ATHENA_NAV_EBUSY);
    AthenaNavRequestInfo info;
    unsigned steps=0;
    do {
        int work=athena_nav_step_requests(grid,&(AthenaNavBudget){7,.1f});
        assert(work>0&&work<=7&&++steps<100000);
        assert(athena_nav_request_info(request,&info)==0);
    } while(info.state==ATHENA_NAV_PENDING);
    assert(info.state==ATHENA_NAV_FOUND&&info.points==(unsigned)points);
    assert(athena_nav_request_copy(request,output,512)==points);
    for(int i=0;i<points*2;i++) assert(output[i]==expected[i]);
    printf("[Nav native] %u steps, %llu work units, %u bytes/request\n",steps,
        (unsigned long long)info.work,(unsigned)info.storage_bytes);
    athena_nav_request_release(request);
    request=athena_nav_request_create(grid,1,1,2,2,NULL,4,&error);
    athena_nav_set_cost(grid,1,1,0);
    assert(athena_nav_request_info(request,&info)==0&&info.error==ATHENA_NAV_EREVISION);
    athena_nav_request_release(request);
    AthenaNavCrowd *crowd=athena_nav_crowd_create(grid);
    int id=athena_nav_agent_add(crowd,1.5f,2,1.5f,&(AthenaNavAgentDesc){10,0,0});
    athena_nav_set_cost(grid,1,1,1);
    request=athena_nav_agent_request_move_to(crowd,id,60.5f,1.5f,&error);
    assert(request);
    athena_nav_request_release(request); /* Agent keeps ownership without an observer. */
    while(athena_nav_pending_requests(grid)) assert(athena_nav_step_requests(grid,&(AthenaNavBudget){7,.1f})>0);
    AthenaNavAgentInfo agent;
    assert(athena_nav_agent_info(crowd,id,&agent)==0&&agent.state==ATHENA_NAV_MOVING&&agent.planning_state==ATHENA_NAV_FOUND);
    for(unsigned frames=0;agent.state!=ATHENA_NAV_ARRIVED;frames++) {
        assert(frames<1000); athena_nav_crowd_update(crowd,.1f); athena_nav_agent_info(crowd,id,&agent);
    }
    assert(agent.x==60.5f&&agent.z==1.5f&&agent.y==2);
    request=athena_nav_agent_request_move_to(crowd,id,1.5f,1.5f,&error);
    athena_nav_crowd_release(crowd);
    assert(athena_nav_request_info(request,&info)==0&&info.state==ATHENA_NAV_CANCELLED);
    athena_nav_request_release(request);
    athena_nav_grid_release(grid);
    puts("Nav native incremental EE test passed");
    SleepThread();
    return 0;
}
