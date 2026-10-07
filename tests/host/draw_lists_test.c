/* Boundary tests compile owl_draw.c itself, then parse its VIF/GIF stream. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "draw_packet_harness.h"
enum Kind {POINT,LINE,LINE_GOURAUD,TRIANGLE,TRIANGLE_GOURAUD,TEX_TRIANGLE,
    TEX_TRIANGLE_GOURAUD,IMAGE,RECT,KINDS};
typedef struct { uint64_t xy,uv,color; unsigned prim; bool textured,colored; } Vertex;
static Vertex expected[32768];
static unsigned expected_count,observed,cases;
static GSSURFACE texture={.Width=256,.Height=256,.TBW=4};
static uint64_t packed_xy(float x,float y) {
    int xx=(int)(x*16)+32768,yy=(int)(y*16)+32768;
    if(xx<0)xx=0;
    if(yy<0)yy=0;
    if(xx>65535)xx=65535;
    if(yy>65535)yy=65535;
    return (uint64_t)xx|((uint64_t)yy<<16);
}
static void expect(float x,float y,float u,float v,uint64_t color,unsigned prim,
    bool textured,bool colored,int packing) {
    volatile float sx=athena_view_matrix.xx*x+athena_view_matrix.xy*y+athena_view_matrix.tx;
    volatile float sy=athena_view_matrix.yx*x+athena_view_matrix.yy*y+athena_view_matrix.ty;
    /* List primitives round; textured list primitives truncate; rects retain
     * fractions until the GS 12.4 conversion. */
    if(packing==0) {sx=lroundf(sx);sy=lroundf(sy);}
    if(packing==1) {sx=(int)sx;sy=(int)sy;u=(int)u;v=(int)v;}
    assert(expected_count<sizeof(expected)/sizeof(expected[0]));
    expected[expected_count++]=(Vertex){packed_xy(sx,sy),
        (uint64_t)(int)(u*16)|((uint64_t)(int)(v*16)<<16),color,prim,textured,colored};
}
static void vertex(uint64_t xy,uint64_t uv,uint64_t color,unsigned prim) {
    assert(observed<expected_count);
    Vertex *e=&expected[observed++];
    if(xy!=e->xy) fprintf(stderr,"vertex %u: XY %llx expected %llx\n",observed,
        (unsigned long long)xy,(unsigned long long)e->xy);
    assert(xy==e->xy && prim==e->prim);
    if(e->textured) assert(uv==e->uv);
    if(e->colored) assert(color==e->color);
}
static size_t item_size(enum Kind kind) {
    const size_t sizes[]={sizeof(prim_point),sizeof(prim_line),sizeof(prim_gouraud_line),
        sizeof(prim_triangle),sizeof(prim_gouraud_triangle),sizeof(prim_tex_triangle),
        sizeof(prim_tex_gouraud_triangle),sizeof(prim_tex_sprite),sizeof(prim_tex_rect)};
    return sizes[kind];
}
static void call(enum Kind kind,void *items,int n) {
    switch(kind) {
        case POINT:draw_point_list(2,3,items,n);break;
        case LINE:draw_line_list(2,3,items,n);break;
        case LINE_GOURAUD:draw_line_gouraud_list(2,3,items,n);break;
        case TRIANGLE:draw_triangle_list(2,3,items,n);break;
        case TRIANGLE_GOURAUD:draw_triangle_gouraud_list(2,3,items,n);break;
        case TEX_TRIANGLE:draw_tex_triangle_list(&texture,2,3,items,n);break;
        case TEX_TRIANGLE_GOURAUD:draw_tex_triangle_gouraud_list(&texture,2,3,items,n);break;
        case IMAGE:draw_image_list(&texture,2,3,items,n);break;
        case RECT:draw_tex_rect_list(&texture,items,n,0x80563412);break;
        default:abort();
    }
}
static void run(unsigned ring,enum Kind kind,unsigned count,int marker,AthenaViewKind view,bool prefill) {
    test_view(view);test_bind_result=marker?1:-2;test_upload_once=marker==2;
    test_packet_begin(ring,vertex,prefill);expected_count=observed=0;
    void *items=calloc(count?count:1,item_size(kind));assert(items);
    for(unsigned i=0;i<count;i++) {
        float x=(i%43)*3+10.25f,y=(i/43%30)*3+10.25f;
        float a=x+4,b=y+1,c=x+2,d=y+5;
        uint64_t color=0x80000000u+i*3+1,color2=color+1,color3=color+2;
        switch(kind) {
            case POINT:((prim_point *)items)[i]=(prim_point){x,y,color};break;
            case LINE:((prim_line *)items)[i]=(prim_line){x,y,a,b,color};break;
            case LINE_GOURAUD:((prim_gouraud_line *)items)[i]=(prim_gouraud_line){x,y,color,a,b,color2};break;
            case TRIANGLE:((prim_triangle *)items)[i]=(prim_triangle){x,y,a,b,c,d,color};break;
            case TRIANGLE_GOURAUD:((prim_gouraud_triangle *)items)[i]=(prim_gouraud_triangle){x,y,color,a,b,color2,c,d,color3};break;
            case TEX_TRIANGLE:((prim_tex_triangle *)items)[i]=(prim_tex_triangle){x,y,10,11,a,b,12,13,c,d,14,15,color};break;
            case TEX_TRIANGLE_GOURAUD:((prim_tex_gouraud_triangle *)items)[i]=(prim_tex_gouraud_triangle){x,y,10,11,color,a,b,12,13,color2,c,d,14,15,color3};break;
            case IMAGE:((prim_tex_sprite *)items)[i]=(prim_tex_sprite){x,y,10,11,4,5,14,15,color};break;
            case RECT:((prim_tex_rect *)items)[i]=(prim_tex_rect){x,y,x+4,y+5,10.25f,11.25f,14.25f,15.25f};break;
            default:abort();
        }
        if(kind==IMAGE || kind==RECT) {
            unsigned corners[6]={0,2,1,1,2,3};
            bool triangles=kind==RECT || view==ATHENA_VIEW_ROTATED;
            for(unsigned j=0;j<(triangles?6u:2u);j++) {
                unsigned q=triangles?corners[j]:j*3;
                float ox=kind==RECT?0:2,oy=kind==RECT?0:3;
                float delta=kind==RECT?0.25f:0;
                expect(x+ox+((q&1)?4:0),y+oy+((q&2)?5:0),
                    10+delta+((q&1)?4:0),11+delta+((q&2)?4:0),
                    kind==RECT?0x80563412:color,triangles?3:6,true,true,kind==RECT?2:1);
            }
        } else {
            bool textured=kind==TEX_TRIANGLE || kind==TEX_TRIANGLE_GOURAUD;
            unsigned vertices=kind==POINT?1:kind==LINE || kind==LINE_GOURAUD?2:3;
            expect(x+2,y+3,10,11,color,vertices==1?0:vertices==2?1:3,textured,kind!=TEX_TRIANGLE,textured?1:0);
            if(vertices>1) expect(a+2,b+3,12,13,
                kind==LINE_GOURAUD || kind==TRIANGLE_GOURAUD || kind==TEX_TRIANGLE_GOURAUD?color2:color,
                vertices==2?1:3,textured,kind!=TEX_TRIANGLE,textured?1:0);
            if(vertices>2) expect(c+2,d+3,14,15,
                kind==TRIANGLE_GOURAUD || kind==TEX_TRIANGLE_GOURAUD?color3:color,
                3,textured,kind!=TEX_TRIANGLE,textured?1:0);
        }
    }
    /* Empty and invalid lists must leave the ring and texture bindings alone. */
    call(kind,items,0);call(kind,items,-1);call(kind,NULL,1);
    assert(!test_queries && !test_bind_calls);
    if(kind>=TEX_TRIANGLE && count) {
        test_bind_result=GRAPHICS_BIND_ERROR;
        call(kind,items,1);assert(!test_queries && !test_vertices);
        test_bind_calls=0;test_bind_result=marker?1:-2;
    }
    call(kind,items,count);test_packet_end();
    assert(observed==expected_count && test_vertices==expected_count);
    if(kind>=TEX_TRIANGLE && count) {
        assert(test_bind_calls>0);
        assert(test_marks==test_upload_bind_calls);
    }
    free(items);cases++;
}
int main(void) {
    const unsigned overhead[]={5,3,5,5,5,11,11,10,12};
    const unsigned per_item[]={1,2,2,2,3,3,5,3,6};
    for(unsigned ring=2048;ring<=8192;ring*=2)
        for(enum Kind k=POINT;k<KINDS;k++) {
            unsigned capacity=(ring/2-overhead[k]-1)/per_item[k];
            const unsigned counts[]={0,1,capacity-1,capacity,capacity+1,capacity*3+7};
            for(int marker=0;marker<=2;marker++)
                for(AthenaViewKind view=ATHENA_VIEW_IDENTITY;view<=ATHENA_VIEW_ROTATED;view++)
                    for(unsigned n=0;n<sizeof(counts)/sizeof(counts[0]);n++)
                        run(ring,k,counts[n],marker,view,n&1);
        }
    printf("draw_lists: %u cases, all 9 writers, ring boundaries, vertices/UV/colors/order, upload and views passed\n",cases);
}
