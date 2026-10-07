/* Particles2D emission, integration and draw lists on the host. Image draws
 * are captured here instead of reaching the GS. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <athena/particles2d.h>
#include "../../src/modules/particles2d/native/particles2d_backend.h"
#include <athena/loop.h>
static void closef(float a,float b) { assert(fabsf(a-b)<0.001f); }
static AthenaParticle2DRecord records[1024]; static int record_count,submit_calls; static float last_rect[4];
void athena_particles2d_submit(AthenaImage *image,const float rect[4],const AthenaParticle2DRecord *list,uint32_t count) {
    (void)image; assert(count<=ATHENA_PARTICLES2D_BATCH*8);
    memcpy(&records[record_count],list,count*sizeof(*list)); record_count+=count; submit_calls++;
    memcpy(last_rect,rect,sizeof(last_rect));
}
int main(void) {
    AthenaImage image={.width=32,.height=16};
    AthenaEmitter2DDesc d; athena_emitter2d_defaults(&d);
    AthenaEmitter2D *e=NULL;
    /* Validation. */
    d.capacity=0; assert(athena_emitter2d_create(&d,&e)==ATHENA_PARTICLES2D_EINVAL && !e);
    athena_emitter2d_defaults(&d); d.life_min=0; assert(athena_emitter2d_create(&d,&e)==ATHENA_PARTICLES2D_EINVAL);
    athena_emitter2d_defaults(&d); d.speed_min=2; d.speed_max=1; assert(athena_emitter2d_create(&d,&e)==ATHENA_PARTICLES2D_EINVAL);
    athena_emitter2d_defaults(&d); d.gravity_y=NAN; assert(athena_emitter2d_create(&d,&e)==ATHENA_PARTICLES2D_EINVAL);
    /* Burst: velocity along the angle, gravity and lifetime. */
    athena_emitter2d_defaults(&d);
    d.capacity=8; d.speed_min=d.speed_max=10; d.angle=0; d.gravity_y=20; d.life_min=d.life_max=1;
    assert(!athena_emitter2d_create(&d,&e));
    assert(!athena_emitter2d_set_position(e,100,50));
    assert(athena_emitter2d_emit(e,20)==8 && athena_emitter2d_count(e)==8); /* pool cap */
    assert(!athena_emitter2d_update(e,.5f));
    /* Draw: one record each, centred, at half life; no rotation: hs 0. */
    record_count=0; athena_emitter2d_draw(e,&image);
    assert(record_count==8);
    /* x = 100 + 10*.5; vy = 20*.5 = 10 after the step, y = 50 + 10*.5. */
    closef(records[0].x,105); closef(records[0].y,55);
    closef(records[0].hc,4); closef(records[0].hs,0); closef(last_rect[2],32); closef(last_rect[3],16);
    assert(athena_emitter2d_update(e,.6f)==0 && athena_emitter2d_count(e)==0); /* life over */
    /* Rate: 10/s for 0.25 s gives 2 particles plus a carried half. */
    d.rate=10; d.speed_min=d.speed_max=0; d.gravity_y=0; d.capacity=64;
    assert(!athena_emitter2d_configure(e,&d));
    athena_emitter2d_update(e,.25f); assert(athena_emitter2d_count(e)==2);
    athena_emitter2d_update(e,.25f); assert(athena_emitter2d_count(e)==5);
    athena_emitter2d_set_active(e,0); athena_emitter2d_update(e,.1f); assert(athena_emitter2d_count(e)==5);
    athena_emitter2d_clear(e); assert(athena_emitter2d_count(e)==0);
    /* Size and colour interpolate over life; birth order is kept. */
    d.rate=0; d.size_start=10; d.size_end=0; d.color_start=0x80ffffffu; d.color_end=0x00000000u;
    d.u1=4; d.v1=4; d.u2=12; d.v2=8;
    assert(!athena_emitter2d_configure(e,&d));
    athena_emitter2d_emit(e,1); athena_emitter2d_update(e,.5f); athena_emitter2d_emit(e,1);
    record_count=0; athena_emitter2d_draw(e,&image); assert(record_count==2);
    closef(records[0].hc,2.5f); /* older one first */
    assert(records[0].r==128&&records[0].g==128&&records[0].b==128&&records[0].a==64);
    closef(records[1].hc,5); assert(records[1].r==255&&records[1].a==128);
    closef(last_rect[0],4); closef(last_rect[3],8);
    /* Drag: v *= 1 / (1 + drag * dt). */
    athena_emitter2d_clear(e); d.drag=1; d.speed_min=d.speed_max=10; d.size_start=d.size_end=2;
    assert(!athena_emitter2d_configure(e,&d)); assert(!athena_emitter2d_set_position(e,0,0));
    athena_emitter2d_emit(e,1); athena_emitter2d_update(e,.5f);
    record_count=0; athena_emitter2d_draw(e,&image); closef(records[0].x,10/1.5f*.5f);
    /* Rotation turns the half extent the VU1 program builds the quad from. */
    athena_emitter2d_clear(e); d.drag=0; d.speed_min=d.speed_max=0;
    d.rotation_min=d.rotation_max=(float)M_PI/2; d.size_start=d.size_end=4;
    assert(!athena_emitter2d_configure(e,&d)); assert(!athena_emitter2d_set_position(e,10,10));
    athena_emitter2d_emit(e,3);
    record_count=0; athena_emitter2d_draw(e,&image); assert(record_count==3);
    /* 90 degrees: the rotated half extent points down, (0, 2), within the
     * fast sine's error. */
    assert(fabsf(records[0].hc)<.005f); assert(fabsf(records[0].hs-2)<.005f);
    /* Fast sine/cosine across angles, wrapped. */
    for(float angle=-20;angle<20;angle+=.37f) {
        athena_emitter2d_clear(e); d.rotation_min=d.rotation_max=angle; d.size_start=d.size_end=2;
        assert(!athena_emitter2d_configure(e,&d)); athena_emitter2d_emit(e,1);
        record_count=0; athena_emitter2d_draw(e,&image);
        assert(fabsf(records[0].hc-cosf(angle))<.002f && fabsf(records[0].hs-sinf(angle))<.002f);
    }
    /* Many particles split into chunks of 128. */
    athena_emitter2d_clear(e); d.rotation_min=d.rotation_max=0; d.capacity=300;
    assert(!athena_emitter2d_configure(e,&d)); athena_emitter2d_emit(e,300);
    record_count=submit_calls=0; athena_emitter2d_draw(e,&image); assert(record_count==300 && submit_calls==1);
    /* Shrinking the capacity drops the newest. */
    d.capacity=10; assert(!athena_emitter2d_configure(e,&d)); assert(athena_emitter2d_count(e)==10);
    /* Same seed, same particles. */
    AthenaEmitter2D *a=NULL,*b=NULL;
    athena_emitter2d_defaults(&d); d.seed=7; d.spread=6; d.speed_min=1; d.speed_max=50; d.area_width=20;
    assert(!athena_emitter2d_create(&d,&a) && !athena_emitter2d_create(&d,&b));
    athena_emitter2d_emit(a,5); athena_emitter2d_emit(b,5);
    athena_emitter2d_update(a,.1f); athena_emitter2d_update(b,.1f);
    record_count=0; athena_emitter2d_draw(a,&image); athena_emitter2d_draw(b,&image);
    for(int i=0;i<5;i++) closef(records[i].x,records[5+i].x);
    /* Module update and Loop system. */
    athena_emitter2d_clear(a); d.rate=100; assert(!athena_emitter2d_configure(a,&d));
    assert(!athena_particles2d_update(.1f) && athena_emitter2d_count(a)==10);
    int id=athena_particles2d_attach_loop(ATHENA_PARTICLES2D_LOOP_PRIORITY,&id); assert(id>0);
    assert(athena_particles2d_attach_loop(0,NULL)==ATHENA_PARTICLES2D_EINVAL);
    assert(athena_loop_systems_run(ATHENA_LOOP_POST_UPDATE,.1f,.1f,NULL)==0 && athena_emitter2d_count(a)==20);
    athena_particles2d_detach_owner(&id); assert(!athena_particles2d_loop_system());
    assert(athena_particles2d_update(-1)==ATHENA_PARTICLES2D_EINVAL);
    athena_emitter2d_release(a); athena_emitter2d_release(b); athena_emitter2d_release(e);
    puts("Particles2D host tests passed");
    return 0;
}
