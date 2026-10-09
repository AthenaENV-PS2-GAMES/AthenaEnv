#include <stddef.h>
#include <athena/sky.h>
#include <athena/graphics.h>
#include <athena/graphics/view.h>
#define MAX_BANDS 64u
static Color rgb(const float c[3],float scale) {
    unsigned v[3];
    for(int i=0;i<3;i++) { float x=c[i]*scale*255.0f+.5f; v[i]=x<0?0:x>255?255:(unsigned)x; }
    return (Color)(v[0]|(v[1]<<8)|(v[2]<<16)|(0x80u<<24));
}
int athena_sky_draw(AthenaCamera3D *camera,uint32_t bands) {
    if(!camera||!getGSGLOBAL()) return -1;
    if(bands<1) bands=1;
    if(bands>MAX_BANDS) bands=MAX_BANDS;
    int width,height; athena_view_screen_size(&width,&height);
    AthenaSkyBand list[MAX_BANDS];
    int n=athena_sky_bands(camera,(float)width,(float)height,list,bands);
    if(n<0) return -1;
    AthenaAffine2D saved; athena_view_get(&saved); athena_view_set(NULL);
    for(int i=0;i<n;i++) {
        Color t=rgb(list[i].top,1),b=rgb(list[i].bottom,1);
        draw_quad_gouraud(0,list[i].y0,(float)width,list[i].y0,0,list[i].y1,(float)width,list[i].y1,t,t,b,b);
    }
    AthenaSkyState s; athena_sky_get(&s);
    float sun[2];
    if(s.sun_size>0&&(s.sun_color[0]>0||s.sun_color[1]>0||s.sun_color[2]>0)&&
        athena_sky_sun_screen(camera,(float)width,(float)height,sun)==1) {
        draw_circle(sun[0],sun[1],s.sun_size*1.8f,rgb(s.sun_color,.55f),1);
        draw_circle(sun[0],sun[1],s.sun_size,rgb(s.sun_color,1),1);
    }
    athena_view_set(&saved);
    return n;
}
