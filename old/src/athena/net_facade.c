#include <string.h>

#include <athena/net.h>
#include <network.h>
#include <dbgprintf.h>

int athena_net_init(const char *ip, const char *netmask, const char *gateway, const char *dns)
{
    struct ip4_addr IP, NM, GW, DNS;

    if (ethApplyNetIFConfig(NETMAN_NETIF_ETH_LINK_MODE_AUTO) != 0)
        return -1;

    IP.addr = ipaddr_addr(ip);
    NM.addr = ipaddr_addr(netmask);
    GW.addr = ipaddr_addr(gateway);
    DNS.addr = ipaddr_addr(dns);

    ps2ipInit(&IP, &NM, &GW);
    ethApplyIPConfig(0, &IP, &NM, &GW, &DNS);

    dbgprintf("Waiting for connection...\n");
    if (ethWaitValidNetIFLinkState() != 0)
        return -1;

    return 0;
}

int athena_net_init_dhcp(void)
{
    struct ip4_addr IP, NM, GW, DNS;

    if (ethApplyNetIFConfig(NETMAN_NETIF_ETH_LINK_MODE_AUTO) != 0)
        return -1;

    ip4_addr_set_zero(&IP);
    ip4_addr_set_zero(&NM);
    ip4_addr_set_zero(&GW);
    ip4_addr_set_zero(&DNS);

    ps2ipInit(&IP, &NM, &GW);
    ethApplyIPConfig(1, &IP, &NM, &GW, &DNS);

    dbgprintf("Waiting for connection...\n");
    if (ethWaitValidNetIFLinkState() != 0)
        return -1;

    dbgprintf("Waiting for DHCP lease...\n");
    if (ethWaitValidDHCPState() != 0)
        return -1;

    dbgprintf("DHCP Connected.\n");
    return 0;
}

int athena_net_get_config(AthenaNetConfig *config)
{
    t_ip_info ip_info;

    if (!config)
        return -1;

    if (ps2ip_getconfig("sm0", &ip_info) < 0)
        return -1;

    strncpy(config->ip, ip4addr_ntoa(&ip_info.ipaddr), sizeof(config->ip) - 1);
    strncpy(config->netmask, ip4addr_ntoa(&ip_info.netmask), sizeof(config->netmask) - 1);
    strncpy(config->gateway, ip4addr_ntoa(&ip_info.gw), sizeof(config->gateway) - 1);
    strncpy(config->dns, ip4addr_ntoa(dns_getserver(0)), sizeof(config->dns) - 1);
    return 0;
}

int athena_net_resolve(const char *host, char *ip_out, size_t ip_out_size)
{
    struct hostent *host_address;

    if (!host || !ip_out || ip_out_size == 0)
        return -1;

    host_address = gethostbyname(host);
    if (!host_address)
        return -1;

    strncpy(ip_out, ip4addr_ntoa((struct in_addr *)host_address->h_addr), ip_out_size - 1);
    ip_out[ip_out_size - 1] = '\0';
    return 0;
}

void athena_net_deinit(void)
{
    ps2ipDeinit();
}
