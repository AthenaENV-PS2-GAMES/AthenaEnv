#ifndef ATHENA_CAMERA_H
#define ATHENA_CAMERA_H

#ifdef ATHENA_CAMERA

#include <ps2cam_rpc.h>

void athena_camera_startup(int mode);
int athena_camera_device_count(void);
int athena_camera_open(int index);
int athena_camera_close(int devid);
int athena_camera_status(int devid);
int athena_camera_set_bandwidth(int devid, int bandwidth);
int athena_camera_read_packet(int devid);
int athena_camera_set_led(int devid, int mode);
void athena_camera_set_config(int devid, PS2CAM_DEVICE_CONFIG *cfg);

#endif /* ATHENA_CAMERA */

#endif /* ATHENA_CAMERA_H */
