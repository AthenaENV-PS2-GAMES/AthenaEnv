/* Particles3D emission, integration and billboard records on the host. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <athena/particles3d.h>
#include <athena/loop.h>
#include "../../src/modules/particles3d/native/particles3d_backend.h"
static void closef(float a,float b) { assert(fabsf(a-b)<0.001f); }
static AthenaParticle3DRecord records[1024]; static int record_count;
int athena_particles3d_submit(AthenaCamera3D *camera,AthenaImage *image,const float rect[4],
    const AthenaParticle3DRecord *list,uint32_t count) {
    (void)camera; (void)image; (void)rect; assert(count<=ATHENA_PARTICLES3D_BATCH*8);
    memcpy(&records[record_count],list,count*sizeof(*list)); record_count+=count; return 0;
}
int main(void) {
    AthenaImage image={.width=16,.height=16};
    AthenaCamera3D camera; athena_camera3d_init(&camera); /* eye (0, 0, 5) looking at the origin */
    assert(athena_camera3d_set_projection(&camera,60,1,1,20));
    AthenaEmitter3DDesc d; athena_emitter3d_defaults(&d);
    AthenaEmitter3D *e=NULL;
    d.direction[0]=d.direction[1]=d.direction[2]=0; assert(athena_emitter3d_create(&d,&e)==ATHENA_PARTICLES3D_EINVAL);
    athena_emitter3d_defaults(&d); d.spread=4; assert(athena_emitter3d_create(&d,&e)==ATHENA_PARTICLES3D_EINVAL);
    /* Straight up at 10 u/s with gravity, then the records. */
    athena_emitter3d_defaults(&d); d.capacity=32; d.speed_min=d.speed_max=10; d.gravity[1]=-20;
    d.size_start=2; d.size_end=0; d.color_start=0x80ffffffu; d.color_end=0;
    assert(!athena_emitter3d_create(&d,&e)); assert(!athena_emitter3d_set_position(e,1,0,0));
    assert(athena_emitter3d_emit(e,40)==32);
    assert(!athena_emitter3d_update(e,.5f));
    record_count=0; assert(athena_emitter3d_draw(e,&camera,&image)==32 && record_count==32);
    /* vy = 10 - 20*.5 = 0 after the step: y = 0; half size at half life = .5. */
    closef(records[0].x,1); closef(records[0].y,0); closef(records[0].z,0); closef(records[0].half,.5f);
    assert(records[0].r==128&&records[0].a==64);
    /* Behind the camera or beyond far: dropped on the EE. */
    athena_emitter3d_clear(e); d.speed_min=d.speed_max=0; d.gravity[1]=0; assert(!athena_emitter3d_configure(e,&d));
    assert(!athena_emitter3d_set_position(e,0,0,10)); athena_emitter3d_emit(e,3);  /* behind the eye */
    assert(!athena_emitter3d_set_position(e,0,0,-30)); athena_emitter3d_emit(e,4); /* beyond far */
    assert(!athena_emitter3d_set_position(e,0,0,4.5f)); athena_emitter3d_emit(e,2); /* nearer than near */
    assert(!athena_emitter3d_set_position(e,0,0,-2)); athena_emitter3d_emit(e,5);   /* visible */
    record_count=0; assert(athena_emitter3d_draw(e,&camera,&image)==5);
    /* Cone: every direction within the spread of the axis. */
    athena_emitter3d_clear(e); d.direction[0]=1; d.direction[1]=1; d.direction[2]=0; d.spread=.3f;
    d.speed_min=d.speed_max=1; d.capacity=512; assert(!athena_emitter3d_configure(e,&d));
    assert(!athena_emitter3d_set_position(e,0,0,0)); athena_emitter3d_emit(e,512);
    athena_emitter3d_update(e,1e-3f);
    float sumx=0;
    record_count=0; athena_emitter3d_draw(e,&camera,&image);
    for(int i=0;i<record_count;i++) {
        float x=records[i].x,y=records[i].y,z=records[i].z,len=sqrtf(x*x+y*y+z*z);
        float c=(x+y)/sqrtf(2)/len; assert(c>=cosf(.3f)-1e-3f); sumx+=x/len;
    }
    assert(record_count==512 && sumx/record_count>.6f); /* centred on the axis */
    /* Module update and Loop. */
    athena_emitter3d_clear(e); d.rate=100; assert(!athena_emitter3d_configure(e,&d));
    assert(!athena_particles3d_update(.1f) && athena_emitter3d_count(e)==10);
    int id=athena_particles3d_attach_loop(ATHENA_PARTICLES3D_LOOP_PRIORITY,&id); assert(id>0);
    assert(athena_loop_systems_run(ATHENA_LOOP_POST_UPDATE,.1f,.1f,NULL)==0 && athena_emitter3d_count(e)==20);
    athena_particles3d_detach_owner(&id); assert(!athena_particles3d_loop_system());
    assert(athena_emitter3d_draw(e,NULL,&image)==ATHENA_PARTICLES3D_EINVAL);
    athena_emitter3d_release(e);
    puts("Particles3D host tests passed");
    return 0;
}
