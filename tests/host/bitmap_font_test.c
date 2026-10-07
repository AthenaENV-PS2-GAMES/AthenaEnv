/* Compile the production bitmap renderer; inspect serialized glyph positions
 * across packet boundaries and exercise the camera/rotated-view paths. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <athena/graphics.h>
#include <athena/graphics/owl_packet.h>
static owl_qword memory[8192];
static GSCONTEXT context;
GSCONTEXT *gsGlobal=&context;
static unsigned glyphs,batches,rotated_glyphs;
static int expected_x,expected_y,upload,culling;
AthenaAffine2D athena_view_matrix={1,0,0,1,0,0};
AthenaViewKind athena_view_current_kind=ATHENA_VIEW_IDENTITY;
uint32_t athena_view_culled;
void athena_affine_bounds(const AthenaAffine2D *m,const AthenaRect2D *r,AthenaRect2D *out) {
    (void)m; *out=*r; /* Culler fixtures use the identity transform. */
}
int GetInterlacedFrameMode(void) { return 0; }
bool athena_view_culler_init(AthenaViewCuller *c) {
    if(!culling) return false;
    memset(c,0,sizeof(*c)); c->m=athena_view_matrix; return true;
}
void draw_tex_rect_list(GSSURFACE *s,const prim_tex_rect *p,int n,Color color) {
    (void)s;(void)p;(void)color; rotated_glyphs+=n;
}
int texture_manager_bind(GSCONTEXT *c,GSSURFACE *s,bool async) {
    (void)c;(void)s;(void)async; return upload ? 1 : -1;
}
void athena_set_tw_th(const GSSURFACE *s,int *w,int *h) { (void)s; *w=*h=8; }
uint32_t athena_test_dma_address(uintptr_t p) { (void)p;return 0; }
void SyncDCache(void *a,void *b) { (void)a;(void)b; }
void dmaKit_wait(owl_channel c,int n) { (void)c;(void)n; }
static void inspect(owl_qword *p,size_t count) {
    assert(count>0); batches++;
    /* Header is 8 QWs without a transfer marker or 12 QWs with one. */
    size_t header=upload?12:8;
    assert(count>=header && !((count-header)%2));
    for(size_t i=header;i<count;i+=2) {
        uint32_t xy=p[i].sword[2];
        int x=(xy&0xffff),y=xy>>16;
        assert(x==owl_coord_transform(expected_x-.5f,0));
        assert(y==owl_coord_transform(expected_y-.5f,0));
        glyphs++;
        expected_x+=9;
        if(glyphs%37==0) { expected_x=10; expected_y+=9; }
    }
}
void dmaKit_send_chain_ucab(owl_channel c,void *base) {
    assert(c==CHANNEL_VIF1);
    owl_qword *p=base; size_t count=(size_t)(p[0].dword[0]&0xffff)+1;
    assert(count<owl_get_controller()->size);
    inspect(p,count);
    assert((p[count].dword[0]>>28&7)==DMA_END);
}
static void run(unsigned ring,unsigned count,int marker) {
    owl_init(memory,ring); upload=marker; glyphs=batches=0; expected_x=10;expected_y=20;
    GSSURFACE texture={.Width=128,.Height=128}; short widths[256];
    for(unsigned i=0;i<256;i++) widths[i]=8;
    GSFONT font={.Type=FONT_TYPE_PNG_DAT,.Texture=&texture,.CharWidth=8,.CharHeight=8,.Additional=widths};
    char text[3000];unsigned n=0;
    for(unsigned i=0;i<count;i++) { text[n++]='A'; if((i+1)%37==0) text[n++]='\n'; }
    text[n]=0;
    athena_font_print_scaled(&context,&font,10,20,1,1,0xfffffffful,text);
    owl_flush_packet(); assert(glyphs==count);
    assert(count<=((ring/2-13)/2) || batches>1);
    glyphs=0;culling=1;
    athena_font_print_scaled(&context,&font,10,20,1,1,0xfffffffful,text);
    owl_flush_packet(); assert(!glyphs&&athena_view_culled); culling=0;
    rotated_glyphs=0;athena_view_current_kind=ATHENA_VIEW_ROTATED;
    athena_font_print_scaled(&context,&font,10,20,1,1,0xfffffffful,text);
    assert(rotated_glyphs==count);athena_view_current_kind=ATHENA_VIEW_IDENTITY;
}
int main(void) {
    for(unsigned ring=2048;ring<=8192;ring*=2)
        for(int marker=0;marker<=1;marker++) { run(ring,505,marker);run(ring,506,marker);run(ring,2300,marker); }
    puts("bitmap_font: long text, glyph order/newlines, transfer markers, culling and rotation passed");
    return 0;
}
