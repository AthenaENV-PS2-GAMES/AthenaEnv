#include <string.h>

#include <remote.h>

/* One 256-byte, 64-byte aligned buffer per port, as required by RMMan_Open(). */
static u8 remoteBuf[2][256] __attribute__((aligned(64)));
static int remote_port_opened[2] = {0, 0};

int remote_module_init(void)
{
    return RMMan_Init();
}

int remote_module_end(void)
{
    for (int port = 0; port < 2; port++) {
        if (remote_port_opened[port]) {
            RMMan_Close(port, 0);
            remote_port_opened[port] = 0;
        }
    }

    return RMMan_End();
}

int remote_open_port(int port)
{
    if (!remote_port_opened[port])
        remote_port_opened[port] = RMMan_Open(port, 0, remoteBuf[port]) != 0;

    return remote_port_opened[port];
}

void remote_read_port(int port, struct remote_data *data)
{
    memset(data, 0, sizeof(*data));

    if (remote_port_opened[port])
        RMMan_Read(port, 0, data);
}
