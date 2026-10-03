#ifdef ATHENA_MOUSE

#include <athena/mouse.h>
#include <libmouse.h>

int athena_mouse_open(void)
{
    int ret = PS2MouseInit();
    PS2MouseReset();
    return ret;
}

int athena_mouse_read(AthenaMouseData *data)
{
    PS2MouseData raw;

    if (!data)
        return -1;

    PS2MouseRead(&raw);
    data->x = raw.x;
    data->y = raw.y;
    data->wheel = raw.wheel;
    data->buttons = raw.buttons;
    return 0;
}

int athena_mouse_set_boundary(int minx, int maxx, int miny, int maxy)
{
    return PS2MouseSetBoundary(minx, maxx, miny, maxy);
}

unsigned int athena_mouse_get_mode(void)
{
    return PS2MouseGetReadMode();
}

unsigned int athena_mouse_set_mode(unsigned int mode)
{
    return PS2MouseSetReadMode(mode);
}

float athena_mouse_get_accel(void)
{
    return PS2MouseGetAccel();
}

int athena_mouse_set_accel(float accel)
{
    return PS2MouseSetAccel(accel);
}

int athena_mouse_set_position(int x, int y)
{
    return PS2MouseSetPosition(x, y);
}

int athena_mouse_get_boundary(int *minx, int *maxx, int *miny, int *maxy)
{
    return PS2MouseGetBoundary(minx, maxx, miny, maxy);
}

unsigned int athena_mouse_get_threshold(void)
{
    return PS2MouseGetThres();
}

int athena_mouse_set_threshold(unsigned int thres)
{
    return PS2MouseSetThres(thres);
}

unsigned int athena_mouse_get_double_click_time(void)
{
    return PS2MouseGetDblClickTIme();
}

int athena_mouse_set_double_click_time(unsigned int msec)
{
    return PS2MouseSetDblClickTime(msec);
}

unsigned int athena_mouse_get_version(void)
{
    return PS2MouseGetVersion();
}

unsigned int athena_mouse_enumerate(void)
{
    return PS2MouseEnum();
}

int athena_mouse_reset(void)
{
    return PS2MouseReset();
}

#endif /* ATHENA_MOUSE */
