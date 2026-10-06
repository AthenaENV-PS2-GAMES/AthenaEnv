#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <athena/render3d.h>
#include "render3d_clip.h"
#include "../../src/modules/model3d/native/texture3d_backend.h"
extern unsigned texture3d_host_created,texture3d_host_destroyed;
int main(void) {
    uint32_t pixels[4]={0x000000ff,0x0000ff00,0x00ff0000,0x00ffffff};
    AthenaTexture3DPixels input={.width=2,.height=2,.pixels=pixels,.pixel_count=4};
    AthenaTexture3D *texture=NULL;
    assert(!athena_texture3d_create(&input,&texture));
    pixels[0]=0; AthenaTexture3DPixels view; athena_texture3d_view(texture,&view);
    assert(view.pixels[0]==0x800000ff&&view.pixels[3]==0x80ffffff);
    assert(!((uintptr_t)view.pixels&127));
    AthenaTexture3D *invalid=(void *)1;
    input.width=3; assert(athena_texture3d_create(&input,&invalid)==ATHENA_MODEL3D_EINVAL&&!invalid);
    input.width=2; input.filter=2; assert(athena_texture3d_create(&input,&invalid)==ATHENA_MODEL3D_EINVAL);
    input.filter=0; input.pixel_count=3; assert(athena_texture3d_create(&input,&invalid)==ATHENA_MODEL3D_EINVAL);
    input.pixel_count=4; input.height=1024; assert(athena_texture3d_create(&input,&invalid)==ATHENA_MODEL3D_EINVAL);
    AthenaMaterial3D material; athena_material3d_default(&material); material.texture=texture;
    float positions[]={-.1,-.1,-3,.1,-.1,-3,0,.1,-3},uv[]={0,1,1,1,.5,0};
    uint32_t indices[]={2,0,1};
    AthenaGeometry3D geometry={.positions=positions,.vertex_count=3,.indices=indices,.index_count=3,.material=&material};
    AthenaMesh3D *mesh=NULL;
    assert(athena_mesh3d_create(&geometry,&mesh)==ATHENA_MODEL3D_EINVAL);
    geometry.texcoords=uv; geometry.texcoord_count=3;
    uv[0]=NAN; assert(athena_mesh3d_create(&geometry,&mesh)==ATHENA_MODEL3D_EINVAL);
    uv[0]=1.01f; assert(athena_mesh3d_create(&geometry,&mesh)==ATHENA_MODEL3D_EINVAL);
    uv[0]=0; assert(!athena_mesh3d_create(&geometry,&mesh));
    uv[4]=0; AthenaMesh3DView mv; athena_mesh3d_view(mesh,&mv);
    assert(mv.texcoords[0].u==.5f&&mv.texcoords[1].v==1&&mv.texcoords[3].u==0);
    AthenaTexture3DBinding binding; assert(!athena_texture3d_bind(texture,&binding));
    assert(texture3d_host_created==1&&texture3d_host_destroyed==0);
    AthenaInstance3D *instance=athena_instance3d_create(mesh); athena_mesh3d_release(mesh); athena_texture3d_release(texture);
    AthenaBatch3D *batch=athena_batch3d_create(); assert(!athena_batch3d_add(batch,instance)); athena_instance3d_release(instance);
    AthenaCamera3D camera; athena_camera3d_init(&camera); AthenaRender3DStats stats;
    assert(!athena_batch3d_draw(batch,&camera,ATHENA_RENDER3D_CULL_NONE,&stats));
    assert(stats.geometry_bytes==96&&texture3d_host_destroyed==0);
    athena_batch3d_destroy(batch); assert(texture3d_host_destroyed==1);
    /* Clip before division: the x=w plane crosses A->B at t=2/5,
     * even though the endpoint W values differ. UVs interpolate with t. */
    AthenaClipVertex3D triangle[3]={
        {.position={3,0,0,1},.texcoord={1,0}},
        {.position={0,0,0,3},.texcoord={0,0}},
        {.position={0,1,0,3},.texcoord={0,1}}};
    AthenaClipVertex3D polygon[ATHENA_CLIP3D_MAX_VERTICES]; int clipped=0;
    int n=athena_render3d_clip_triangle(triangle,polygon,&clipped); assert(n==4&&clipped);
    unsigned intersections=0;
    for(int i=0;i<n;i++) if(fabs(polygon[i].position[0]-polygon[i].position[3])<1e-6) {
        assert(fabs(polygon[i].texcoord[0]-.6)<1e-6); intersections++;
    }
    assert(intersections==2);
    /* Explicit overrides import glTF UVs without invoking the host decoder. */
    input=(AthenaTexture3DPixels){.width=2,.height=2,.pixels=pixels,.pixel_count=4};
    assert(!athena_texture3d_create(&input,&texture)); material.texture=texture;
    assert(!athena_mesh3d_load_with_material("bin/models/textured_cube.glb",&material,&mesh));
    athena_mesh3d_view(mesh,&mv); assert(mv.vertex_count==36&&mv.texcoords&&mv.material.texture==texture);
    athena_mesh3d_release(mesh); athena_texture3d_release(texture);
    assert(!athena_mesh3d_load("bin/models/textured_cube.glb",&mesh));
    athena_mesh3d_view(mesh,&mv); assert(mv.material.texture&&mv.texcoords);
    athena_texture3d_view(mv.material.texture,&view); assert(view.width==64&&view.height==64);
    athena_mesh3d_release(mesh);
    /* Complete OBJ vt is expanded and V converted from bottom-left. */
    const char *obj_path="/tmp/athena-texture-uv.obj";
    FILE *obj=fopen(obj_path,"w"); assert(obj);
    fputs("v 0 0 -3\nv 1 0 -3\nv 0 1 -3\nvt 0 0\nvt 1 0\nvt 0 1\nf 1/1 2/2 3/3\n",obj); fclose(obj);
    assert(!athena_mesh3d_load(obj_path,&mesh)); athena_mesh3d_view(mesh,&mv);
    assert(mv.texcoords&&mv.texcoords[0].u==0&&mv.texcoords[0].v==1&&mv.texcoords[2].v==0);
    athena_mesh3d_release(mesh); remove(obj_path);
    puts("3D texture ownership, UV import and homogeneous interpolation tests passed"); return 0;
}
