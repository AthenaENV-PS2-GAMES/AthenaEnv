#ifdef ATHENA_REMOTE

#include <stdlib.h>

#include <athena/remote.h>
#include <remote.h>

int athena_remote_open(void)
{
    return remote_module_init();
}

int athena_remote_close(void)
{
    return remote_module_end();
}

AthenaRemote *athena_remote_get(int port)
{
    AthenaRemote *remote;

    if (!remote_open_port(port))
        return NULL;

    remote = calloc(1, sizeof(*remote));
    if (!remote)
        return NULL;

    remote->port = port;
    athena_remote_update(remote);

    return remote;
}

void athena_remote_free(AthenaRemote *remote)
{
    free(remote);
}

void athena_remote_update(AthenaRemote *remote)
{
    struct remote_data data;

    remote_read_port(remote->port, &data);

    remote->old_button = remote->button;
    remote->status = data.status;
    remote->button = data.button;
}

bool athena_remote_pressed(const AthenaRemote *remote, uint32_t button)
{
    return remote->status == RM_KEYPRESSED && remote->button == button && button != RM_RELEASED;
}

bool athena_remote_just_pressed(const AthenaRemote *remote, uint32_t button)
{
    return athena_remote_pressed(remote, button) && remote->old_button != button;
}

bool athena_remote_released(const AthenaRemote *remote)
{
    return remote->button == RM_RELEASED;
}

bool athena_remote_is_active(int port)
{
    struct remote_data data;

    remote_read_port(port, &data);

    return data.status != RM_NOREMOTE;
}

#endif /* ATHENA_REMOTE */
