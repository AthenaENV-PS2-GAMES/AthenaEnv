#ifdef ATHENA_CAMERA

#include <athena/camera.h>

void athena_camera_startup(int mode)
{
    PS2CamInit(mode);
}

int athena_camera_device_count(void)
{
    return PS2CamGetDeviceCount();
}

int athena_camera_open(int index)
{
    return PS2CamOpenDevice(index);
}

int athena_camera_close(int devid)
{
    return PS2CamCloseDevice(devid);
}

int athena_camera_status(int devid)
{
    return PS2CamGetDeviceStatus(devid);
}

int athena_camera_set_bandwidth(int devid, int bandwidth)
{
    return PS2CamSetDeviceBandwidth(devid, bandwidth);
}

int athena_camera_read_packet(int devid)
{
    return PS2CamReadPacket(devid);
}

int athena_camera_set_led(int devid, int mode)
{
    return PS2CamSetLEDMode(devid, mode);
}

void athena_camera_set_config(int devid, PS2CAM_DEVICE_CONFIG *cfg)
{
    PS2CamSetDeviceConfig(devid, cfg);
}

#endif /* ATHENA_CAMERA */
