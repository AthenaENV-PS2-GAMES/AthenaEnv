/* Use pre-laid-out atlas quads to exercise the production fntsys renderer
 * independently of FreeType rasterization. No copy of renderer/batch logic. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "draw_packet_harness.h"
#include "../../src/modules/font/native/fntsys.c"
#define MAX_GLYPHS 2300
static fnt_quad_t quads[MAX_GLYPHS];
static unsigned seen[5][MAX_GLYPHS],last[5];
static unsigned glyph_count,pass_count,vertices_per_glyph,cases;
static float offsets[5][2];
static uint64_t pass_colors[5];
static uint64_t xy_fixed(float x,float y) {
    volatile float sx=athena_view_matrix.xx*x+athena_view_matrix.xy*y+athena_view_matrix.tx;
    volatile float sy=athena_view_matrix.yx*x+athena_view_matrix.yy*y+athena_view_matrix.ty;
    int xx=(int)(sx*16)+32768,yy=(int)(sy*16)+32768;
    assert(xx>=0 && xx<65536 && yy>=0 && yy<65536);
    return (uint64_t)xx|((uint64_t)yy<<16);
}
static void vertex(uint64_t xy,uint64_t uv,uint64_t color,unsigned prim) {
    unsigned u=uv&0x3fff,v=uv>>16&0x3fff;
    unsigned id=(v/32)*64+u/32;
    assert(id<glyph_count);
    const fnt_quad_t *q=&quads[id];
    unsigned corner_seq[]={0,2,1,1,2,3};
    unsigned matches=0,match=0;
    for(unsigned p=0;p<pass_count;p++) {
        unsigned n=seen[p][id];
        if(n==vertices_per_glyph || color!=pass_colors[p]) continue;
        unsigned corner=vertices_per_glyph==2?n*3:corner_seq[n];
        float x=(corner&1)?q->x2:q->x1,y=(corner&2)?q->y2:q->y1;
        uint64_t expect_uv=((corner&1)?q->uv2:q->uv1)&0xffff;
        expect_uv|=((corner&2)?q->uv2:q->uv1)&0xffff0000u;
        if(uv==expect_uv && xy==xy_fixed(x+10+offsets[p][0],y+20+offsets[p][1])) {
            matches++;match=p;
        }
    }
    assert(matches==1);
    assert(prim==(vertices_per_glyph==2?6u:3u));
    /* Monotonic glyph order for each pass, including across atlas boundaries. */
    if(!seen[match][id]) {assert(id==last[match]);last[match]++;}
    seen[match][id]++;
}
static void run(unsigned ring,unsigned count,unsigned atlases,unsigned passes,
    int marker,AthenaViewKind view,bool prefill) {
    test_view(view);test_bind_result=marker?1:-2;test_upload_once=marker==2;
    test_packet_begin(ring,vertex,prefill);
    memset(seen,0,sizeof(seen));memset(last,0,sizeof(last));
    glyph_count=count;pass_count=passes;
    vertices_per_glyph=view==ATHENA_VIEW_ROTATED?6:2;
    float outline=passes==5?1:0,shadow=passes==2?2:0;
    const uint64_t color=0x80503010,outline_color=0x80101010,shadow_color=0x80080808;
    for(unsigned p=0;p<passes;p++) {
        offsets[p][0]=offsets[p][1]=0;pass_colors[p]=color;
        if(passes==5 && p<4) {
            offsets[p][0]=p<2?1:-1;offsets[p][1]=(p&1)?-1:1;
            pass_colors[p]=outline_color;
        } else if(passes==2 && p==0) {
            offsets[p][0]=offsets[p][1]=2;pass_colors[p]=shadow_color;
        }
    }
    atlas_t atlas[2]={0};
    for(unsigned i=0;i<atlases;i++) atlas[i].surface=(GSSURFACE){.Width=256,.Height=256,.TBW=4};
    for(unsigned i=0;i<count;i++) {
        float x=(i%50)*8,y=(i/50)*9;
        unsigned u=(i%64)*32,v=(i/64)*32;
        quads[i]=(fnt_quad_t){x,y,x+4,y+6,
            (uint64_t)u|((uint64_t)v<<16),(uint64_t)(u+8)|((uint64_t)(v+8)<<16),0};
    }
    fnt_layout_t layout={.quads=quads,.count=count,.valid=1,.runCount=atlases,
        .x0=0,.y0=0,.x1=400,.y1=420};
    for(unsigned i=0;i<atlases;i++) {
        unsigned start=i*count/atlases,end=(i+1)*count/atlases;
        layout.runs[i].atlas=&atlas[i];layout.runs[i].start=start;layout.runs[i].count=end-start;
    }
    fntLayoutDraw(NULL,10,20,color,outline,outline_color,shadow,shadow_color);
    layout.valid=0;fntLayoutDraw(&layout,10,20,color,outline,outline_color,shadow,shadow_color);
    layout.valid=1;layout.count=0;
    fntLayoutDraw(&layout,10,20,color,outline,outline_color,shadow,shadow_color);
    layout.count=count;assert(!test_queries && !test_bind_calls);
    test_bind_result=GRAPHICS_BIND_ERROR;
    fntLayoutDraw(&layout,10,20,color,outline,outline_color,shadow,shadow_color);
    assert(!test_queries && !test_vertices);
    test_bind_calls=0;test_bind_result=marker?1:-2;
    fntLayoutDraw(&layout,10,20,color,outline,outline_color,shadow,shadow_color);
    test_packet_end();
    assert(test_vertices==count*passes*vertices_per_glyph);
    for(unsigned p=0;p<passes;p++) for(unsigned i=0;i<count;i++) assert(seen[p][i]==vertices_per_glyph);
    assert(test_marks==test_upload_bind_calls);
    /* Whole-text culling leaves no packet or texture binding behind. */
    if(view!=ATHENA_VIEW_IDENTITY) {
        test_packet_begin(ring,vertex,false);
        fntLayoutDraw(&layout,-10000,-10000,color,outline,outline_color,shadow,shadow_color);
        test_packet_end();assert(!test_queries && !test_bind_calls && !test_vertices && athena_view_culled==1);
    }
    cases++;
}
int main(void) {
    for(unsigned ring=2048;ring<=8192;ring*=2)
        for(unsigned passes=1;passes<=5;passes+=passes==1?1:3) {
            unsigned capacity=(ring/2-11-passes*3)/(passes*2);
            const unsigned counts[]={1,capacity-1,capacity,capacity+1,MAX_GLYPHS};
            for(int marker=0;marker<=2;marker++)
                for(unsigned atlas=1;atlas<=2;atlas++)
                    for(AthenaViewKind view=ATHENA_VIEW_IDENTITY;view<=ATHENA_VIEW_ROTATED;view++)
                        for(unsigned i=0;i<sizeof(counts)/sizeof(counts[0]);i++)
                            run(ring,counts[i],atlas,passes,marker,view,i&1);
        }
    printf("atlas_font: %u cases, ring boundaries, 1/2/5 passes, 1/2 atlases, UV/order/colors, rotation, culling and upload passed\n",cases);
}
