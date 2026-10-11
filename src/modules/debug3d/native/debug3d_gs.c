#include <athena/debug3d.h>
#include <athena/graphics.h>
#include <athena/graphics/view.h>
/* Screen segments batched into copied GS packets. */
static void emit(void *opaque,const AthenaDebug3DSegment *s,uint32_t count) {
    (void)opaque;
    /* draw_line_list packs copied vertices/colors into the owned packet ring;
     * it never queues a DMA REF to these reusable CPU arrays. */
    draw_screen_line_list(s,(int)count);
}
int athena_debug3d_draw(AthenaCamera3D *camera) {
    if(!camera||!getGSGLOBAL()) return -1;
    int width,height; athena_view_screen_size(&width,&height);
    AthenaAffine2D saved; athena_view_get(&saved);
    athena_view_set(NULL);
    int drawn=athena_debug3d_project_batch(camera,(float)width,(float)height,emit,NULL);
    athena_view_set(&saved);
    return drawn;
}
