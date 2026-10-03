#include <stdlib.h>
#include <string.h>

#include <network.h>

#include <athena/socket.h>

AthenaSocket *athena_socket_create(int sin_family, int protocol)
{
    AthenaSocket *sock = calloc(1, sizeof(*sock));
    if (!sock)
        return NULL;

    sock->sin_family = sin_family;
    sock->fd = socket(sin_family, protocol, 0);
    return sock;
}

int athena_socket_connect(AthenaSocket *sock, const char *ip, int port)
{
    struct sockaddr_in addr;

    if (!sock || !ip)
        return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = sock->sin_family;
    addr.sin_addr.s_addr = ipaddr_addr(ip);
    addr.sin_port = htons(port);

    return connect(sock->fd, (struct sockaddr *)&addr, sizeof(addr));
}

int athena_socket_bind(AthenaSocket *sock, const char *ip, int port)
{
    struct sockaddr_in addr;

    if (!sock || !ip)
        return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = sock->sin_family;
    addr.sin_addr.s_addr = ipaddr_addr(ip);
    addr.sin_port = htons(port);

    return bind(sock->fd, (struct sockaddr *)&addr, sizeof(addr));
}

int athena_socket_listen(AthenaSocket *sock)
{
    if (!sock)
        return -1;
    return listen(sock->fd, 0);
}

int athena_socket_send(AthenaSocket *sock, const void *data, size_t len)
{
    if (!sock || !data)
        return -1;
    return send(sock->fd, data, len, MSG_DONTWAIT);
}

int athena_socket_recv(AthenaSocket *sock, void *buf, size_t len, size_t *received)
{
    int ret;

    if (!sock || !buf)
        return -1;

    ret = recv(sock->fd, buf, len, MSG_DONTWAIT);
    if (received) {
        if (ret > 0)
            *received = (size_t)ret;
        else
            *received = 0;
    }
    return ret;
}

void athena_socket_close(AthenaSocket *sock)
{
    if (!sock)
        return;
    if (sock->fd >= 0)
        lwip_close(sock->fd);
    sock->fd = -1;
}

void athena_socket_destroy(AthenaSocket *sock)
{
    if (!sock)
        return;
    athena_socket_close(sock);
    free(sock);
}
