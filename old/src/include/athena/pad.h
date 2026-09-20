#ifndef ATHENA_PAD_H
#define ATHENA_PAD_H

#include <stdbool.h>
#include <stdint.h>

#include <libpad.h>

typedef struct AthenaPad {
    int port;
    uint32_t btns;
    int lx;
    int ly;
    int rx;
    int ry;
    uint32_t old_btns;
    int old_lx;
    int old_ly;
    int old_rx;
    int old_ry;
} AthenaPad;

AthenaPad *athena_pad_open(int port);
void athena_pad_close(AthenaPad *pad);
void athena_pad_update(AthenaPad *pad);
bool athena_pad_pressed(const AthenaPad *pad, int button);
bool athena_pad_just_pressed(const AthenaPad *pad, int button);
unsigned char athena_pad_pressure(int port, uint32_t button);
void athena_pad_rumble(int port, char act_align0, char act_align1);
void athena_pad_set_led(int port, uint8_t r, uint8_t g, uint8_t b);
int athena_pad_state(int port);
int athena_pad_type(int port);
int athena_pad_active_type(int port);
int athena_pad_set_mode(int port, int mode, bool lock);
bool athena_pad_is_active(int port);
int athena_pad_connected_count(void);
void athena_pad_check_reconnect(int port);

#endif /* ATHENA_PAD_H */
