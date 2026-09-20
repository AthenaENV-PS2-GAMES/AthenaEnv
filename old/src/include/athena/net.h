#ifndef ATHENA_NET_H
#define ATHENA_NET_H

#include <stdbool.h>

typedef struct AthenaNetConfig {
    char ip[16];
    char netmask[16];
    char gateway[16];
    char dns[16];
} AthenaNetConfig;

int athena_net_init(const char *ip, const char *netmask, const char *gateway, const char *dns);
int athena_net_init_dhcp(void);
int athena_net_get_config(AthenaNetConfig *config);
int athena_net_resolve(const char *host, char *ip_out, size_t ip_out_size);
void athena_net_deinit(void);

#endif /* ATHENA_NET_H */
