#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <tamtypes.h>

#include <athena/pad.h>
#include <pad.h>

#ifdef ATHENA_PADEMU
#include <libds34bt.h>
#include <libds34usb.h>
#endif

static int pad_initialized[2] = {0, 0};

static bool athena_pad_port_connected(int port)
{
    int state = padGetState(port, 0);
    bool connected = (state == PAD_STATE_STABLE) || (state == PAD_STATE_FINDCTP1);

#ifdef ATHENA_PADEMU
    if (!connected) {
        if (ds34bt_get_status(port) & DS34BT_STATE_RUNNING)
            connected = true;
        if (ds34usb_get_status(port) & DS34USB_STATE_RUNNING)
            connected = true;
    }
#endif

    return connected;
}

static bool athena_pad_read_buttons(int port, struct padButtonStatus *buttons, uint32_t *paddata)
{
    int state = padGetState(port, 0);
    bool valid_read = false;

    memset(buttons, 0, sizeof(*buttons));
    buttons->ljoy_h = 127;
    buttons->ljoy_v = 127;
    buttons->rjoy_h = 127;
    buttons->rjoy_v = 127;
    *paddata = 0;

    if ((state == PAD_STATE_STABLE) || (state == PAD_STATE_FINDCTP1)) {
        if (padRead(port, 0, buttons) != 0) {
            *paddata = 0xffff ^ buttons->btns;
            valid_read = true;
        }
    }

#ifdef ATHENA_PADEMU
    if (ds34bt_get_status(port) & DS34BT_STATE_RUNNING) {
        if (ds34bt_get_data(port, (u8 *)&buttons->btns) != 0) {
            *paddata |= 0xffff ^ buttons->btns;
            valid_read = true;
        }
    }

    if (ds34usb_get_status(port) & DS34USB_STATE_RUNNING) {
        if (ds34usb_get_data(port, (u8 *)&buttons->btns) != 0) {
            *paddata |= 0xffff ^ buttons->btns;
            valid_read = true;
        }
    }
#endif

    if (!valid_read) {
        buttons->ljoy_h = 127;
        buttons->ljoy_v = 127;
        buttons->rjoy_h = 127;
        buttons->rjoy_v = 127;
    }

    return valid_read;
}

static void athena_pad_apply_buttons(AthenaPad *pad, const struct padButtonStatus *buttons, uint32_t paddata)
{
    pad->btns = paddata;
    pad->lx = buttons->ljoy_h - 127;
    pad->ly = buttons->ljoy_v - 127;
    pad->rx = buttons->rjoy_h - 127;
    pad->ry = buttons->rjoy_v - 127;
}

void athena_pad_check_reconnect(int port)
{
    int current_state = padGetState(port, 0);

    if (current_state == PAD_STATE_DISCONN)
        pad_initialized[port] = 0;

    if ((current_state == PAD_STATE_STABLE || current_state == PAD_STATE_FINDCTP1) &&
        !pad_initialized[port]) {
        initializePad(port, 0);
        pad_initialized[port] = 1;
    }

}

AthenaPad *athena_pad_open(int port)
{
    AthenaPad *pad = calloc(1, sizeof(*pad));
    if (!pad)
        return NULL;

    pad->port = port;
    athena_pad_check_reconnect(port);

    struct padButtonStatus buttons;
    uint32_t paddata;
    athena_pad_read_buttons(port, &buttons, &paddata);
    athena_pad_apply_buttons(pad, &buttons, paddata);

    return pad;
}

void athena_pad_close(AthenaPad *pad)
{
    free(pad);
}

void athena_pad_update(AthenaPad *pad)
{
    struct padButtonStatus buttons;
    uint32_t paddata;

    athena_pad_check_reconnect(pad->port);

    pad->old_btns = pad->btns;
    pad->old_lx = pad->lx;
    pad->old_ly = pad->ly;
    pad->old_rx = pad->rx;
    pad->old_ry = pad->ry;

    athena_pad_read_buttons(pad->port, &buttons, &paddata);
    athena_pad_apply_buttons(pad, &buttons, paddata);
}

bool athena_pad_pressed(const AthenaPad *pad, int button)
{
    return (pad->btns & (uint32_t)button) != 0;
}

bool athena_pad_just_pressed(const AthenaPad *pad, int button)
{
    return athena_pad_pressed(pad, button) && !(pad->old_btns & (uint32_t)button);
}

unsigned char athena_pad_pressure(int port, uint32_t button)
{
    struct padButtonStatus pad;
    unsigned char pressure = 0;

    uint32_t unused;

    athena_pad_check_reconnect(port);
    athena_pad_read_buttons(port, &pad, &unused);

    switch (button) {
    case PAD_RIGHT:
        pressure = pad.right_p;
        break;
    case PAD_LEFT:
        pressure = pad.left_p;
        break;
    case PAD_UP:
        pressure = pad.up_p;
        break;
    case PAD_DOWN:
        pressure = pad.down_p;
        break;
    case PAD_TRIANGLE:
        pressure = pad.triangle_p;
        break;
    case PAD_CIRCLE:
        pressure = pad.circle_p;
        break;
    case PAD_CROSS:
        pressure = pad.cross_p;
        break;
    case PAD_SQUARE:
        pressure = pad.square_p;
        break;
    case PAD_L1:
        pressure = pad.l1_p;
        break;
    case PAD_R1:
        pressure = pad.r1_p;
        break;
    case PAD_L2:
        pressure = pad.l2_p;
        break;
    case PAD_R2:
        pressure = pad.r2_p;
        break;
    default:
        pressure = 0;
        break;
    }

    return pressure;
}

void athena_pad_rumble(int port, char act_align0, char act_align1)
{
    char act_align[6];

    athena_pad_check_reconnect(port);

    act_align[0] = act_align0;
    act_align[1] = act_align1;

    int state = padGetState(port, 0);
    if ((state == PAD_STATE_STABLE) || (state == PAD_STATE_FINDCTP1))
        padSetActDirect(port, 0, act_align);

#ifdef ATHENA_PADEMU
    if (ds34bt_get_status(port) & DS34BT_STATE_RUNNING)
        ds34bt_set_rumble(port, act_align[1], act_align[1]);
    if (ds34usb_get_status(port) & DS34USB_STATE_RUNNING)
        ds34usb_set_rumble(port, act_align[1], act_align[1]);
#endif
}

void athena_pad_set_led(int port, uint8_t r, uint8_t g, uint8_t b)
{
    u8 led[4];

    led[0] = r;
    led[1] = g;
    led[2] = b;
    led[3] = 0;

#ifdef ATHENA_PADEMU
    if (ds34bt_get_status(port) & DS34BT_STATE_RUNNING)
        ds34bt_set_led(port, led);
    if (ds34usb_get_status(port) & DS34USB_STATE_RUNNING)
        ds34usb_set_led(port, led);
#endif
}

int athena_pad_state(int port)
{
    return padGetState(port, 0);
}

int athena_pad_type(int port)
{
    athena_pad_check_reconnect(port);
    return padInfoMode(port, 0, PAD_MODETABLE, 0);
}

int athena_pad_active_type(int port)
{
    athena_pad_check_reconnect(port);
    return padInfoMode(port, 0, PAD_MODECURID, 0);
}

int athena_pad_set_mode(int port, int mode, bool lock)
{
    athena_pad_check_reconnect(port);

    int state = padGetState(port, 0);
    if (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1)
        return -1;

    int ret = padSetMainMode(port, 0, mode, lock ? PAD_MMODE_LOCK : PAD_MMODE_UNLOCK);
    waitPadReady(port, 0);

    return ret;
}

bool athena_pad_is_active(int port)
{
    return athena_pad_port_connected(port);
}

int athena_pad_connected_count(void)
{
    int count = 0;

    for (int port = 0; port < 2; port++) {
        if (athena_pad_port_connected(port))
            count++;
    }

    return count;
}
