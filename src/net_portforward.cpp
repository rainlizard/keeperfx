/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
#include "pre_inc.h"
#include "net_portforward.h"
#include "bflib_basics.h"
#include "bflib_datetm.h"

#define MINIUPNP_STATICLIB
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>

#if defined(__MINGW32__)
struct LPMSG;
#endif

#define NATPMP_STATICLIB
#ifdef __WIN32__
#include <natpmp/natpmp.h>
#else
#include <natpmp.h>
#endif

#include <cstdio>
#include <random>

#ifdef __WIN32__
#define PCP_SOCKET SOCKET
#define PCP_INVALID_SOCKET INVALID_SOCKET
#define PCP_CLOSE_SOCKET closesocket
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#define PCP_SOCKET int
#define PCP_INVALID_SOCKET -1
#define PCP_CLOSE_SOCKET close
#endif

#define NATPMP_TIMEOUT_MS 1200
#define UPNP_TIMEOUT_MS 3000
#define MAPPING_LIFETIME_SECONDS 3600
#define PCP_PORT 5351
#define PCP_MAP_OPCODE 1
#define PCP_UDP_PROTOCOL 17
#define PCP_MESSAGE_SIZE 60

#include "post_inc.h"

enum PortForwardMethod {
    PORT_FORWARD_NONE = 0,
    PORT_FORWARD_PCP,
    PORT_FORWARD_NATPMP,
    PORT_FORWARD_UPNP
};

static enum PortForwardMethod active_method = PORT_FORWARD_NONE;
static uint16_t mapped_port = 0;
static uint16_t public_port = 0;
static TbClockMSec renew_at = 0;
static struct UPNPUrls upnp_urls;
static struct IGDdatas upnp_data;
static char upnp_lanaddr[64];
static struct UPNPUrls ipv6_urls;
static struct IGDdatas ipv6_data;
static char ipv6_pinhole_id[16];
static TbClockMSec ipv6_renew_at = 0;

static uint16_t pcp_map(uint16_t private_port, uint16_t requested_public_port, uint32_t lifetime)
{
    natpmp_t gateway_lookup;
    if (initnatpmp(&gateway_lookup, 0, 0) < 0)
        return 0;
    struct sockaddr_in gateway = {};
    gateway.sin_family = AF_INET;
    gateway.sin_port = htons(PCP_PORT);
    gateway.sin_addr.s_addr = gateway_lookup.gateway;
    closenatpmp(&gateway_lookup);
    PCP_SOCKET pcp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (pcp_socket == PCP_INVALID_SOCKET)
        return 0;
    if (connect(pcp_socket, (struct sockaddr *)&gateway, sizeof(gateway)) < 0) {
        PCP_CLOSE_SOCKET(pcp_socket);
        return 0;
    }
    struct sockaddr_in local = {};
#ifdef __WIN32__
    int local_size = sizeof(local);
#else
    socklen_t local_size = sizeof(local);
#endif
    if (getsockname(pcp_socket, (struct sockaddr *)&local, &local_size) < 0) {
        PCP_CLOSE_SOCKET(pcp_socket);
        return 0;
    }
    unsigned char request[PCP_MESSAGE_SIZE] = {0};
    request[0] = 2;
    request[1] = PCP_MAP_OPCODE;
    uint32_t network_lifetime = htonl(lifetime);
    memcpy(request + 4, &network_lifetime, sizeof(network_lifetime));
    request[18] = 0xFF;
    request[19] = 0xFF;
    memcpy(request + 20, &local.sin_addr.s_addr, sizeof(local.sin_addr.s_addr));
    std::random_device random;
    for (int i = 0; i < 12; i++)
        request[24 + i] = (unsigned char)random();
    request[36] = PCP_UDP_PROTOCOL;
    uint16_t network_private_port = htons(private_port);
    uint16_t network_public_port = htons(requested_public_port);
    memcpy(request + 40, &network_private_port, sizeof(network_private_port));
    memcpy(request + 42, &network_public_port, sizeof(network_public_port));
    if (send(pcp_socket, (const char *)request, sizeof(request), 0) != sizeof(request)) {
        PCP_CLOSE_SOCKET(pcp_socket);
        return 0;
    }
    fd_set sockets;
    FD_ZERO(&sockets);
    FD_SET(pcp_socket, &sockets);
    struct timeval timeout = { 1, 200000 };
    if (select((int)pcp_socket + 1, &sockets, NULL, NULL, &timeout) <= 0) {
        PCP_CLOSE_SOCKET(pcp_socket);
        return 0;
    }
    unsigned char response[PCP_MESSAGE_SIZE];
    int response_size = recv(pcp_socket, (char *)response, sizeof(response), 0);
    PCP_CLOSE_SOCKET(pcp_socket);
    if (response_size < PCP_MESSAGE_SIZE || response[0] != 2 || response[1] != 0x81 || response[3] != 0 || memcmp(request + 24, response + 24, 12) != 0 || response[36] != PCP_UDP_PROTOCOL)
        return 0;
    memcpy(&network_lifetime, response + 4, sizeof(network_lifetime));
    memcpy(&network_public_port, response + 42, sizeof(network_public_port));
    uint32_t granted_lifetime = ntohl(network_lifetime);
    if (granted_lifetime < 60)
        return 0;
    renew_at = LbTimerClock() + granted_lifetime * 500;
    return ntohs(network_public_port);
}

static int wait_for_natpmp(natpmp_t *natpmp, natpmpresp_t *response)
{
    TbClockMSec deadline = LbTimerClock() + NATPMP_TIMEOUT_MS;
    int result;
    do {
        TbClockMSec remaining = deadline - LbTimerClock();
        if (remaining <= 0)
            return 0;
        fd_set sockets;
        FD_ZERO(&sockets);
        FD_SET(natpmp->s, &sockets);
        struct timeval timeout;
        getnatpmprequesttimeout(natpmp, &timeout);
        int32_t timeout_ms = (int32_t)(timeout.tv_sec * 1000 + timeout.tv_usec / 1000);
        if (timeout_ms > remaining) {
            timeout.tv_sec = remaining / 1000;
            timeout.tv_usec = remaining % 1000 * 1000;
        }
        select(FD_SETSIZE, &sockets, NULL, NULL, &timeout);
        result = readnatpmpresponseorretry(natpmp, response);
    } while (result == NATPMP_TRYAGAIN);
    return result == 0;
}

static uint16_t natpmp_map(uint16_t private_port, uint16_t requested_public_port, uint32_t lifetime)
{
    natpmp_t natpmp;
    if (initnatpmp(&natpmp, 0, 0) < 0)
        return 0;
    if (sendnewportmappingrequest(&natpmp, NATPMP_PROTOCOL_UDP, private_port, requested_public_port, lifetime) < 0) {
        closenatpmp(&natpmp);
        return 0;
    }
    natpmpresp_t response;
    int success = wait_for_natpmp(&natpmp, &response);
    closenatpmp(&natpmp);
    if (!success)
        return 0;
    if (response.pnu.newportmapping.lifetime < 60 && lifetime)
        return 0;
    renew_at = LbTimerClock() + response.pnu.newportmapping.lifetime * 500;
    return response.pnu.newportmapping.mappedpublicport;
}

static int upnp_map(uint16_t private_port, uint16_t requested_public_port)
{
    char private_port_string[16];
    char public_port_string[16];
    snprintf(private_port_string, sizeof(private_port_string), "%u", private_port);
    snprintf(public_port_string, sizeof(public_port_string), "%u", requested_public_port);
    int result = UPNP_AddPortMapping(upnp_urls.controlURL, upnp_data.first.servicetype, public_port_string, private_port_string, upnp_lanaddr, "KeeperFX", "UDP", "", "3600");
    if (result != UPNPCOMMAND_SUCCESS) {
        char reserved_port[16] = {0};
        result = UPNP_AddAnyPortMapping(upnp_urls.controlURL, upnp_data.first.servicetype, public_port_string, private_port_string, upnp_lanaddr, "KeeperFX", "UDP", "", "3600", reserved_port);
        if (result == UPNPCOMMAND_SUCCESS)
            public_port = (uint16_t)strtoul(reserved_port, NULL, 10);
    }
    if (result != UPNPCOMMAND_SUCCESS)
        return 0;
    if (!public_port)
        public_port = requested_public_port;
    renew_at = LbTimerClock() + MAPPING_LIFETIME_SECONDS * 500;
    return 1;
}

int port_forward_add_mapping(uint16_t port)
{
    port_forward_remove_mapping();
    public_port = pcp_map(port, port, MAPPING_LIFETIME_SECONDS);
    if (public_port) {
        mapped_port = port;
        active_method = PORT_FORWARD_PCP;
        LbNetLog("PCP: port forwarding active on public port %u\n", public_port);
        return public_port;
    }
    public_port = natpmp_map(port, port, MAPPING_LIFETIME_SECONDS);
    if (public_port) {
        mapped_port = port;
        active_method = PORT_FORWARD_NATPMP;
        LbNetLog("NAT-PMP: port forwarding active on public port %u\n", public_port);
        return public_port;
    }
    int error = 0;
    struct UPNPDev *devices = upnpDiscover(UPNP_TIMEOUT_MS, NULL, NULL, 0, 0, 2, &error);
    if (!devices)
        return 0;
#if (MINIUPNPC_API_VERSION >= 18)
    int valid = UPNP_GetValidIGD(devices, &upnp_urls, &upnp_data, upnp_lanaddr, sizeof(upnp_lanaddr), NULL, 0);
#else
    int valid = UPNP_GetValidIGD(devices, &upnp_urls, &upnp_data, upnp_lanaddr, sizeof(upnp_lanaddr));
#endif
    freeUPNPDevlist(devices);
    if (!valid) {
        FreeUPNPUrls(&upnp_urls);
        return 0;
    }
    mapped_port = port;
    public_port = port;
    if (!upnp_map(mapped_port, public_port)) {
        mapped_port = 0;
        public_port = 0;
        FreeUPNPUrls(&upnp_urls);
        return 0;
    }
    active_method = PORT_FORWARD_UPNP;
    LbNetLog("UPnP: port forwarding active on public port %u\n", public_port);
    return public_port;
}

int port_forward_add_ipv6_pinhole(const char *address, uint16_t port)
{
    if (!address || !address[0] || !port)
        return 0;
    if (ipv6_pinhole_id[0]) {
        UPNP_DeletePinhole(ipv6_urls.controlURL_6FC, ipv6_data.IPv6FC.servicetype, ipv6_pinhole_id);
        FreeUPNPUrls(&ipv6_urls);
        ipv6_pinhole_id[0] = '\0';
    }
    int error = 0;
    struct UPNPDev *devices = upnpDiscover(UPNP_TIMEOUT_MS, NULL, NULL, 0, 1, 2, &error);
    if (!devices)
        return 0;
    char lan_address[64];
#if (MINIUPNPC_API_VERSION >= 18)
    int valid = UPNP_GetValidIGD(devices, &ipv6_urls, &ipv6_data, lan_address, sizeof(lan_address), NULL, 0);
#else
    int valid = UPNP_GetValidIGD(devices, &ipv6_urls, &ipv6_data, lan_address, sizeof(lan_address));
#endif
    freeUPNPDevlist(devices);
    if (!valid || !ipv6_urls.controlURL_6FC || !ipv6_data.IPv6FC.servicetype[0]) {
        if (valid)
            FreeUPNPUrls(&ipv6_urls);
        return 0;
    }
    char port_string[16];
    snprintf(port_string, sizeof(port_string), "%u", port);
    int result = UPNP_AddPinhole(ipv6_urls.controlURL_6FC, ipv6_data.IPv6FC.servicetype, "", "0", address, port_string, "17", "3600", ipv6_pinhole_id);
    if (result != UPNPCOMMAND_SUCCESS) {
        FreeUPNPUrls(&ipv6_urls);
        ipv6_pinhole_id[0] = '\0';
        return 0;
    }
    ipv6_renew_at = LbTimerClock() + MAPPING_LIFETIME_SECONDS * 500;
    LbNetLog("UPnP: IPv6 firewall pinhole active on port %u\n", port);
    return 1;
}

uint16_t port_forward_update(void)
{
    if (ipv6_pinhole_id[0] && LbTimerClock() >= ipv6_renew_at) {
        if (UPNP_UpdatePinhole(ipv6_urls.controlURL_6FC, ipv6_data.IPv6FC.servicetype, ipv6_pinhole_id, "3600") == UPNPCOMMAND_SUCCESS) {
            ipv6_renew_at = LbTimerClock() + MAPPING_LIFETIME_SECONDS * 500;
        } else {
            FreeUPNPUrls(&ipv6_urls);
            ipv6_pinhole_id[0] = '\0';
        }
    }
    if (active_method == PORT_FORWARD_NONE || LbTimerClock() < renew_at)
        return public_port;
    if (active_method == PORT_FORWARD_PCP) {
        uint16_t renewed_port = pcp_map(mapped_port, public_port, MAPPING_LIFETIME_SECONDS);
        if (renewed_port) {
            public_port = renewed_port;
            return public_port;
        }
    }
    if (active_method == PORT_FORWARD_NATPMP) {
        uint16_t renewed_port = natpmp_map(mapped_port, public_port, MAPPING_LIFETIME_SECONDS);
        if (renewed_port) {
            public_port = renewed_port;
            return public_port;
        }
    }
    if (active_method == PORT_FORWARD_UPNP && upnp_map(mapped_port, public_port))
        return public_port;
    LbNetLog("Port forwarding: mapping renewal failed\n");
    if (active_method == PORT_FORWARD_UPNP)
        FreeUPNPUrls(&upnp_urls);
    active_method = PORT_FORWARD_NONE;
    mapped_port = 0;
    public_port = 0;
    return 0;
}

void port_forward_remove_mapping(void)
{
    if (active_method != PORT_FORWARD_NONE && mapped_port) {
        if (active_method == PORT_FORWARD_PCP) {
            pcp_map(mapped_port, public_port, 0);
        } else if (active_method == PORT_FORWARD_NATPMP) {
            natpmp_map(mapped_port, public_port, 0);
        } else {
            char port_string[16];
            snprintf(port_string, sizeof(port_string), "%u", public_port);
            UPNP_DeletePortMapping(upnp_urls.controlURL, upnp_data.first.servicetype, port_string, "UDP", NULL);
            FreeUPNPUrls(&upnp_urls);
        }
    }
    if (ipv6_pinhole_id[0]) {
        UPNP_DeletePinhole(ipv6_urls.controlURL_6FC, ipv6_data.IPv6FC.servicetype, ipv6_pinhole_id);
        FreeUPNPUrls(&ipv6_urls);
        ipv6_pinhole_id[0] = '\0';
    }
    active_method = PORT_FORWARD_NONE;
    mapped_port = 0;
    public_port = 0;
    renew_at = 0;
}
