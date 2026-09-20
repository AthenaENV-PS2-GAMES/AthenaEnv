#ifndef ATHENA_MOUSE_H
#define ATHENA_MOUSE_H

#ifdef ATHENA_MOUSE

typedef struct AthenaMouseData {
    int x;
    int y;
    int wheel;
    unsigned int buttons;
} AthenaMouseData;

int athena_mouse_open(void);
int athena_mouse_read(AthenaMouseData *data);
int athena_mouse_set_boundary(int minx, int maxx, int miny, int maxy);
int athena_mouse_get_boundary(int *minx, int *maxx, int *miny, int *maxy);
unsigned int athena_mouse_get_mode(void);
unsigned int athena_mouse_set_mode(unsigned int mode);
float athena_mouse_get_accel(void);
int athena_mouse_set_accel(float accel);
int athena_mouse_set_position(int x, int y);
unsigned int athena_mouse_get_threshold(void);
int athena_mouse_set_threshold(unsigned int thres);
unsigned int athena_mouse_get_double_click_time(void);
int athena_mouse_set_double_click_time(unsigned int msec);
unsigned int athena_mouse_get_version(void);
unsigned int athena_mouse_enumerate(void);
int athena_mouse_reset(void);

#endif /* ATHENA_MOUSE */

#endif /* ATHENA_MOUSE_H */
