#ifndef ATHENA_SOCKET_H
#define ATHENA_SOCKET_H

#include <stddef.h>
#include <stdint.h>

typedef struct AthenaSocket {
    int fd;
    int sin_family;
} AthenaSocket;

AthenaSocket *athena_socket_create(int sin_family, int protocol);
int athena_socket_connect(AthenaSocket *sock, const char *ip, int port);
int athena_socket_bind(AthenaSocket *sock, const char *ip, int port);
int athena_socket_listen(AthenaSocket *sock);
int athena_socket_send(AthenaSocket *sock, const void *data, size_t len);
int athena_socket_recv(AthenaSocket *sock, void *buf, size_t len, size_t *received);
void athena_socket_close(AthenaSocket *sock);
void athena_socket_destroy(AthenaSocket *sock);

#endif /* ATHENA_SOCKET_H */
