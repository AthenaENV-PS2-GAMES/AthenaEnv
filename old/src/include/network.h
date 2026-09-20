#ifndef ATHENA_NETWORK_H
#define ATHENA_NETWORK_H

#include <stdbool.h>
#include <time.h>

#include <netman.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <loadfile.h>
#include <pthread.h>

int ethApplyNetIFConfig(int mode);
int ethWaitValidNetIFLinkState(void);
int ethWaitValidDHCPState(void);
int ethApplyIPConfig(int use_dhcp, const struct ip4_addr *ip, const struct ip4_addr *netmask, const struct ip4_addr *gateway, const struct ip4_addr *dns);

#endif
