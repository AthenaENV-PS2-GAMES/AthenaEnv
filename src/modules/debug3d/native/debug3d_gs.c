#include <athena/debug3d.h>
#include <athena/graphics.h>
#include <athena/graphics/view.h>
/* Screen segments batched into draw_line_list() packets. */
#define BATCH 256
typedef struct { prim_line items[BATCH]; int count; } Batch;
static void flush(Batch *b) {
    if(b->count) draw_line_list(0.0f,0.0f,b->items,b->count);
    b->count=0;
}
static void emit(void *opaque,const AthenaDebug3DSegment *s) {
    Batch *b=opaque;
    if(b->count==BATCH) flush(b);
    prim_line *l=&b->items[b->count++];
    l->x=s->x0; l->y=s->y0; l->x2=s->x1; l->y2=s->y1; l->rgba=(Color)s->color;
}
int athena_debug3d_draw(AthenaCamera3D *camera) {
    if(!camera||!getGSGLOBAL()) return -1;
    if(!athena_debug3d_count()) return 0;
    static Batch batch;
    int width,height; athena_view_screen_size(&width,&height);
    AthenaAffine2D saved; athena_view_get(&saved);
    athena_view_set(NULL);
    batch.count=0;
    int drawn=athena_debug3d_project(camera,(float)width,(float)height,emit,&batch);
    flush(&batch);
    athena_view_set(&saved);
    return drawn;
}
