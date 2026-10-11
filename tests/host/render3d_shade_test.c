#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "render3d_clip.h"
static int emitted;
static int capture(const AthenaVector4 *positions,const AthenaColor3D *colors,uint32_t count,void *opaque) {
    assert(count==6);
    unsigned gray=0,black=0;
    for(unsigned i=0;i<count;i++) {
        assert(positions[i].w>=1);
        if(opaque) {
            assert(colors[i].r==colors[i].g&&colors[i].g==colors[i].b);
            if(colors[i].r==128) gray++;
            else { assert(colors[i].r==0); black++; }
            continue;
        }
        /* Uniform input lighting must survive a near-plane split. */
        assert(colors[i].r==128&&colors[i].g==128&&colors[i].b==128&&colors[i].a==255);
    }
    if(opaque) assert(gray==3&&black==3);
    emitted++; return 0;
}
int main(void) {
    AthenaLights *lights=athena_lights_create(); assert(lights);
    AthenaLightsView view,before;
    athena_lights_view(lights,&view); assert(!view.count&&!view.ambient[0]);
    assert(athena_lights_set_ambient(lights,.2f,.3f,.4f));
    assert(athena_lights_set_directional(lights,3,0,0,10,.8f,.7f,.6f));
    athena_lights_view(lights,&before);
    assert(before.count==1&&before.direction[0][2]==1&&before.diffuse[0][0]==.8f);
    assert(!athena_lights_set_ambient(lights,NAN,0,0));
    assert(!athena_lights_set_directional(lights,4,0,0,1,1,1,1));
    assert(!athena_lights_set_directional(lights,1,0,0,0,1,1,1));
    assert(!athena_lights_set_directional(lights,1,INFINITY,0,1,1,1,1));
    assert(athena_lights_set_ambient(lights,.2f,.3f,.4f));
    assert(athena_lights_set_directional(lights,3,0,0,10,.8f,.7f,.6f));
    athena_lights_view(lights,&view); assert(!memcmp(&view,&before,sizeof(view)));
    AthenaMaterial3D material; athena_material3d_default(&material);
    material.shading=ATHENA_MATERIAL3D_DIFFUSE;
    float positions[]={-.1f,-.1f,-.5f,.3f,-.1f,-2,-.1f,.3f,-2};
    float normals[]={1,0,1,1,0,1,1,0,1};
    AthenaGeometry3D geometry={.positions=positions,.vertex_count=3,.normals=normals,.normal_count=3,.material=&material};
    AthenaMesh3D *mesh=NULL; assert(!athena_mesh3d_create(&geometry,&mesh));
    AthenaMesh3DView model; athena_mesh3d_view(mesh,&model);
    assert(fabs(model.normals[0].x-sqrt(.5))<1e-6);
    /* Both streams and the descriptor are copied. */
    normals[0]=NAN; material.shading=ATHENA_MATERIAL3D_UNLIT;
    athena_mesh3d_view(mesh,&model);
    assert(model.material.shading==ATHENA_MATERIAL3D_DIFFUSE&&model.normals[0].x>0);
    assert(athena_geometry3d_validate(&geometry,NULL)==ATHENA_GEOMETRY3D_NORMAL);
    normals[0]=1; geometry.normal_count=2;
    assert(athena_geometry3d_validate(&geometry,NULL)==ATHENA_GEOMETRY3D_NORMAL_COUNT);
    geometry.normal_count=3;
    AthenaMatrix4 matrix; ath_matrix4_identity(&matrix); matrix.value[0]=2;
    AthenaShade3D shade; assert(athena_render3d_shade_prepare(&shade,&model,&matrix,lights));
    float result[4]; AthenaColor3D white={255,255,255,255};
    athena_render3d_shade_color(&shade,&model.normals[0],&white,result);
    /* inverse transpose diag(.5,1,1) gives n.z = 2/sqrt(5), whereas
     * transforming by the object matrix would incorrectly give 1/sqrt(5). */
    assert(fabs(result[0]/255-(.2+.8*2/sqrt(5)))<1e-6);
    matrix.value[0]=-2; assert(athena_render3d_shade_prepare(&shade,&model,&matrix,lights));
    athena_render3d_shade_color(&shade,&model.normals[0],&white,result);
    assert(fabs(result[0]/255-(.2+.8*2/sqrt(5)))<1e-6); /* reflection preserves z */
    matrix.value[0]=0; assert(!athena_render3d_shade_prepare(&shade,&model,&matrix,lights));
    ath_matrix4_identity(&matrix);
    athena_lights_clear(lights);
    assert(athena_lights_set_ambient(lights,1,1,1));
    assert(athena_lights_set_directional(lights,0,1,0,1,1,1,1));
    assert(athena_render3d_shade_prepare(&shade,&model,&matrix,lights));
    athena_render3d_shade_color(&shade,&model.normals[0],&white,result);
    assert(result[0]==255&&result[1]==255&&result[2]==255&&result[3]==255);
    AthenaColor3D gray_color={64,64,64,255};
    athena_render3d_shade_color(&shade,&model.normals[0],&gray_color,result);
    assert(result[0]>127&&result[0]<129); /* accumulation before color clamp */
    /* Point lights: (1 - d^2/range^2)^2 * max(n.l, 0), at the world position
     * of the vertex; nothing beyond the range, nothing for shade_color(). */
    athena_lights_clear(lights);
    assert(athena_lights_set_point(lights,2,-.1f+2,-.1f,-.5f+2,1,.5f,0,4)); /* d = sqrt(8) */
    assert(!athena_lights_set_point(lights,4,0,0,0,1,1,1,1)&&!athena_lights_set_point(lights,0,0,0,0,1,1,1,0));
    assert(athena_render3d_shade_prepare(&shade,&model,&matrix,lights));
    assert(shade.lights.point_count==1);
    athena_render3d_shade_color_at(&shade,&model.positions[0],&model.normals[0],&white,result);
    {   /* n = (1,0,1)/sqrt2, l = (1,0,1)/sqrt2: n.l = 1; fade = (1 - 8/16)^2 = .25 */
        float expected=255*.25f;
        assert(fabsf(result[0]-expected)<.01f&&fabsf(result[1]-expected*.5f)<.01f&&result[2]==0);
    }
    athena_render3d_shade_color(&shade,&model.normals[0],&white,result);
    assert(result[0]==0); /* ambient and directional only */
    assert(athena_lights_set_point(lights,2,-.1f+2,-.1f,-.5f+2,1,.5f,0,2)); /* d > range */
    assert(athena_render3d_shade_prepare(&shade,&model,&matrix,lights));
    athena_render3d_shade_color_at(&shade,&model.positions[0],&model.normals[0],&white,result);
    assert(result[0]==0);
    assert(athena_lights_disable_point(lights,2)); athena_lights_view(lights,&shade.lights);
    assert(shade.lights.point_count==0);
    assert(athena_lights_disable(lights,0)); athena_lights_clear(lights);
    assert(athena_lights_set_ambient(lights,.5f,.5f,.5f));
    assert(athena_render3d_shade_prepare(&shade,&model,&matrix,lights));
    memset(&matrix,0,sizeof(matrix));
    matrix.value[0]=matrix.value[5]=sqrt(3);
    matrix.value[10]=11.0f/9; matrix.value[14]=20.0f/9; matrix.value[11]=-1;
    AthenaRender3DStats stats={0};
    assert(!athena_render3d_clip_mesh_lit(&model,&matrix,&shade,capture,NULL,&stats));
    assert(emitted==1&&stats.clipped_triangles==1&&stats.triangles==2);
    athena_mesh3d_release(mesh);
    /* Right-plane intersection with differing normals. Interpolate the lit
     * endpoints (0 and 255 -> 128), rather than re-lighting an interpolated
     * normal (which would incorrectly yield roughly 180). */
    const float edge_positions[]={2,0,0,0,-.5f,0,0,.5f,0};
    const float edge_normals[]={0,0,1,1,0,0,0,0,-1};
    AthenaMaterial3D edge_material={.shading=ATHENA_MATERIAL3D_DIFFUSE,.base_color={1,1,1,1}};
    AthenaGeometry3D edge={.positions=edge_positions,.vertex_count=3,.normals=edge_normals,.normal_count=3,.material=&edge_material};
    assert(!athena_mesh3d_create(&edge,&mesh)); athena_mesh3d_view(mesh,&model);
    athena_lights_clear(lights); assert(athena_lights_set_directional(lights,0,0,0,1,1,1,1));
    ath_matrix4_identity(&matrix); assert(athena_render3d_shade_prepare(&shade,&model,&matrix,lights));
    stats=(AthenaRender3DStats){0};
    assert(!athena_render3d_clip_mesh_lit(&model,&matrix,&shade,capture,(void *)1,&stats));
    assert(emitted==2&&stats.clipped_triangles==1&&stats.triangles==2); athena_mesh3d_release(mesh);
    material.shading=ATHENA_MATERIAL3D_DIFFUSE; material.base_color[0]=.25f;
    geometry.normals=NULL; geometry.normal_count=0;
    assert(!athena_mesh3d_create(&geometry,&mesh)); athena_mesh3d_view(mesh,&model);
    assert(model.normals&&model.colors[0].r==64); athena_mesh3d_release(mesh);
    memset(positions,0,sizeof(positions));
    assert(athena_geometry3d_validate(&geometry,NULL)==ATHENA_GEOMETRY3D_NORMAL);
    /* Explicit normals permit intentionally degenerate topology; unlit legacy
     * geometry also keeps its old behavior. */
    material.shading=ATHENA_MATERIAL3D_UNLIT;
    assert(!athena_mesh3d_create(&geometry,&mesh)); athena_mesh3d_release(mesh);
    material.shading=ATHENA_MATERIAL3D_DIFFUSE;
    assert(!athena_mesh3d_load_with_material("bin/models/lit_cube.glb",&material,&mesh));
    athena_mesh3d_view(mesh,&model); assert(model.vertex_count==36&&model.normals);
    assert(model.normals[0].z==-1&&model.normals[6].z==1);
    athena_mesh3d_release(mesh);
    assert(!athena_mesh3d_load_with_material("tests/host/3d/triangle.obj",&material,&mesh));
    athena_mesh3d_view(mesh,&model); assert(model.normals); athena_mesh3d_release(mesh);
    athena_lights_destroy(lights);
    puts("3D materials, lights, inverse-transpose and lit clipping tests passed"); return 0;
}
