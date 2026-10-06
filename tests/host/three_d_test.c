#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <athena/render3d.h>
static void closef(float a,float b) { assert(fabsf(a-b)<0.0001f); }
int main(void) {
    AthenaMatrix4 t,s,m;
    ath_matrix4_identity(&t); t.value[12]=10;
    ath_matrix4_identity(&s); s.value[0]=2;
    ath_matrix4_multiply(&m,&t,&s);
    AthenaVector4 point={1,0,0,1},v;
    ath_matrix4_apply(&v,&m,&point); closef(v.x,12);
    ath_matrix4_multiply(&s,&t,&s); assert(ath_matrix4_equals(&s,&m));
    ath_matrix4_multiply(&t,&t,&s); closef(t.value[12],20);
    AthenaQuaternion q,neg,half,before;
    athena_quaternion_identity(&q); neg=(AthenaQuaternion){0,0,0,-1};
    assert(athena_quaternion_slerp(&half,&q,&neg,0.5f)); closef(half.w,1);
    before=half; assert(!athena_quaternion_axis_angle(&half,0,0,0,1));
    assert(!memcmp(&before,&half,sizeof(half)));
    /* Float normalization scales by the largest component: extreme but finite
     * inputs neither overflow nor underflow; lengths up to 1e-20 are rejected. */
    AthenaQuaternion big={3e38f,0,0,-3e38f},tiny={4e-20f,0,3e-20f,0};
    assert(athena_quaternion_normalize(&q,&big)); closef(q.x,(float)M_SQRT1_2); closef(q.w,-(float)M_SQRT1_2);
    assert(athena_quaternion_normalize(&q,&tiny)); closef(q.x,.8f); closef(q.z,.6f);
    before=q; tiny=(AthenaQuaternion){1e-21f,0,0,0};
    assert(!athena_quaternion_normalize(&q,&tiny) && !memcmp(&before,&q,sizeof(q)));
    assert(athena_quaternion_axis_angle(&q,0,3e38f,3e38f,3.14159265358979323846f));
    closef(q.y,(float)M_SQRT1_2); closef(q.z,(float)M_SQRT1_2); assert(fabsf(q.w)<1e-6f);
    assert(athena_quaternion_axis_angle(&q,0,0,1,3.14159265358979323846f/2));
    AthenaVector4 pos={10,20,30,1},scale={2,3,4,0};
    assert(athena_quaternion_trs(&m,&pos,&q,&scale));
    ath_matrix4_apply(&v,&m,&point); closef(v.x,10); closef(v.y,22); closef(v.z,30);
    AthenaCamera3D c,saved;
    athena_camera3d_init(&c);
    assert(athena_camera3d_set_projection(&c,60,1,1,10)); saved=c;
    assert(!athena_camera3d_set_projection(&c,60,0,1,10));
    assert(!memcmp(&saved,&c,sizeof(c)));
    assert(!athena_camera3d_set_position(&c,0,0,0));
    assert(!athena_camera3d_set_up(&c,0,0,1));
    point=(AthenaVector4){0,0,-1,1}; ath_matrix4_apply(&v,&c.projection,&point); closef(v.z/v.w,1);
    point.z=-10; ath_matrix4_apply(&v,&c.projection,&point); closef(v.z/v.w,-1);
    float xyz[]={-1,-1,0, 1,-1,0, 0,1,0};
    float rgba[]={1,0,0,1, 0,1,0,1, 0,0,1,1};
    uint32_t idx[]={2,0,1};
    AthenaGeometry3D g={.positions=xyz,.vertex_count=3,.colors=rgba,.color_count=3,.indices=idx,.index_count=3};
    AthenaMesh3D *mesh=NULL,*invalid=(void *)1;
    uint32_t invalid_offset=99;
    assert(athena_geometry3d_validate(&g,&invalid_offset)==ATHENA_GEOMETRY3D_VALID && invalid_offset==0);
    assert(athena_mesh3d_create(&g,&mesh)==0);
    AthenaMesh3DView view; athena_mesh3d_view(mesh,&view);
    closef(view.positions[0].y,1); assert(view.colors[0].b==255);
    idx[0]=3; assert(athena_geometry3d_validate(&g,&invalid_offset)==ATHENA_GEOMETRY3D_INDEX && invalid_offset==0);
    assert(athena_mesh3d_create(&g,&invalid)==ATHENA_MODEL3D_EINVAL); assert(!invalid);
    idx[0]=2; xyz[0]=NAN; assert(athena_mesh3d_create(&g,&invalid)==ATHENA_MODEL3D_EINVAL);
    xyz[0]=-1; rgba[0]=2; assert(athena_mesh3d_create(&g,&invalid)==ATHENA_MODEL3D_EINVAL);
    rgba[0]=1;
    const uint32_t invalid_bits[]={0x7f800000u,0xff800000u,0x7fc00000u,0x7f800001u};
    for(unsigned k=0;k<sizeof(invalid_bits)/sizeof(invalid_bits[0]);k++) {
        memcpy(&xyz[4],&invalid_bits[k],sizeof(float));
        assert(athena_geometry3d_validate(&g,&invalid_offset)==ATHENA_GEOMETRY3D_POSITION && invalid_offset==4);
        assert(athena_mesh3d_create(&g,&invalid)==ATHENA_MODEL3D_EINVAL && !invalid);
        xyz[4]=-1;
        memcpy(&rgba[6],&invalid_bits[k],sizeof(float));
        assert(athena_geometry3d_validate(&g,&invalid_offset)==ATHENA_GEOMETRY3D_COLOR && invalid_offset==6);
        assert(athena_mesh3d_create(&g,&invalid)==ATHENA_MODEL3D_EINVAL && !invalid);
        rgba[6]=0;
    }
    /* Inputs are copied; instances and batches retain independent references. */
    xyz[1]=100; athena_mesh3d_view(mesh,&view); closef(view.minimum[1],-1);
    AthenaInstance3D *i=athena_instance3d_create(mesh),*other=athena_instance3d_create(mesh);
    athena_mesh3d_release(mesh);
    assert(athena_instance3d_set_position(other,1000,0,0));
    AthenaBatch3D *batch=athena_batch3d_create();
    assert(athena_batch3d_add(batch,i)==0); assert(athena_batch3d_add(batch,other)==0);
    athena_instance3d_release(i); athena_instance3d_release(other);
    AthenaRender3DStats stats;
    assert(athena_batch3d_draw(batch,&c,ATHENA_RENDER3D_CULL_NONE,&stats)==0);
    assert(stats.submitted_objects==2 && stats.culled_objects==1 && stats.triangles==1);
    athena_batch3d_clear(batch); assert(athena_batch3d_size(batch)==0);
    assert(athena_batch3d_draw(batch,&c,(AthenaRender3DCull)7,&stats)==-1);
    athena_batch3d_destroy(batch);
    assert(athena_mesh3d_load("tests/host/3d/triangle.obj",&mesh)==0); athena_mesh3d_release(mesh);
    assert(athena_mesh3d_load("tests/host/3d/triangle.gltf",&mesh)==0); athena_mesh3d_release(mesh);
    assert(athena_mesh3d_load("tests/host/3d/triangle.glb",&mesh)==0); athena_mesh3d_release(mesh);
    assert(athena_mesh3d_load("tests/host/3d/invalid.gltf",&mesh)==ATHENA_MODEL3D_EFORMAT && !mesh);
    assert(athena_mesh3d_load("tests/host/3d/transformed.gltf",&mesh)==ATHENA_MODEL3D_EUNSUPPORTED && !mesh);
    assert(athena_mesh3d_load("tests/host/3d/required-extension.gltf",&mesh)==ATHENA_MODEL3D_EUNSUPPORTED && !mesh);
    assert(athena_mesh3d_load("tests/host/3d/quad.obj",&mesh)==ATHENA_MODEL3D_EUNSUPPORTED && !mesh);
    assert(athena_mesh3d_load("tests/host/3d/missing.obj",&mesh)==ATHENA_MODEL3D_EIO && !mesh);
    assert(athena_mesh3d_load("triangle.fbx",&mesh)==ATHENA_MODEL3D_EUNSUPPORTED && !mesh);
    puts("3D native tests passed"); return 0;
}
