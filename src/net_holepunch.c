/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/**
 * @file net_holepunch.cpp
 *     UDP hole punching via STUN.
 * @par Purpose:
 *     holepunch_stun_query() sends a STUN Binding Request (RFC 5389) from the
 *     ENet host's own socket.  This serves two purposes:
 *       1. Creates a NAT table entry so incoming ENet connections reach us even
 *          when UPnP/NAT-PMP is unavailable.
 *       2. Logs our external IP:port so the host can share it with others.
 *
 *     holepunch_punch_to() sends a burst of small UDP datagrams to the server
 *     before enet_host_connect() is called.  This opens a mapping in the
 *     client's cone NAT and primes port-restricted cone NATs on the server.
 * @author   KeeperFX Team
 * @date     06 Mar 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "net_holepunch.h"
#include "bflib_basics.h"

#include <SDL2/SDL.h>
#include <enet6/enet.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "post_inc.h"

#define STUN_PORT 19302
#define STUN_TIMEOUT_MS 1200
#define STUN_RETRANSMIT_MS 500
#define STUN_MAGIC_COOKIE 0x2112A442U
#define STUN_BINDING_REQUEST 0x0001U
#define STUN_BINDING_INDICATION 0x0011U
#define STUN_BINDING_SUCCESS 0x0101U
#define STUN_ATTRIBUTE_XOR_MAPPED 0x0020U
#define STUN_RESPONSE_BUFFER_SIZE 512
#define HOLE_PUNCH_COUNT 4
#define HOLE_PUNCH_PAYLOAD_SIZE 8
#define HOLE_PUNCH_LOG_INTERVAL_MS 1000

static const char *stun_servers[] = { "stun.l.google.com", "stun1.l.google.com" };
static ENetAddress keepalive_address;
static int keepalive_available = 0;

#pragma pack(push, 1)
struct StunHeader {
    uint16_t type;
    uint16_t length;
    uint32_t magic;
    uint8_t  transaction_id[12];
};

struct StunAttrHeader {
    uint16_t type;
    uint16_t length;
};
#pragma pack(pop)

static void create_stun_header(ENetHost *host, struct StunHeader *header, uint16_t type)
{
    *header = (struct StunHeader){htons(type), htons(0), htonl(STUN_MAGIC_COOKIE), {0}};
    for (int i = 0; i < 3; i++) {
        uint32_t random_value = enet_host_random(host);
        memcpy(header->transaction_id + i * sizeof(random_value), &random_value, sizeof(random_value));
    }
}

static uint16_t query_stun_server(ENetHost *host, const char *server, char *output_ip, size_t output_ip_buffer_size)
{
    ENetAddress stun_server_address;
    if (enet_address_set_host(&stun_server_address, ENET_ADDRESS_TYPE_IPV4, server) < 0) {
        LbNetLog("STUN: failed to resolve %s\n", server);
        return 0;
    }
    stun_server_address.port = STUN_PORT;
    struct StunHeader stun_request;
    create_stun_header(host, &stun_request, STUN_BINDING_REQUEST);
    ENetBuffer send_buffer = {.data = &stun_request, .dataLength = sizeof(stun_request)};
    if (enet_socket_send(host->socket, &stun_server_address, &send_buffer, 1) < 0) {
        ENetAddress mapped_stun_address = stun_server_address;
        enet_address_convert_ipv6(&mapped_stun_address);
        if (enet_socket_send(host->socket, &mapped_stun_address, &send_buffer, 1) < 0)
            return 0;
        stun_server_address = mapped_stun_address;
    }
    Uint32 timeout_deadline = SDL_GetTicks() + STUN_TIMEOUT_MS;
    Uint32 retransmit_at = SDL_GetTicks() + STUN_RETRANSMIT_MS;
    for (;;) {
        Uint32 now = SDL_GetTicks();
        if (now >= timeout_deadline)
            break;
        enet_uint32 socket_wait_flags = ENET_SOCKET_WAIT_RECEIVE;
        Uint32 wait_time = min(timeout_deadline, retransmit_at) - now;
        if (enet_socket_wait(host->socket, &socket_wait_flags, wait_time) < 0)
            break;
        if (!(socket_wait_flags & ENET_SOCKET_WAIT_RECEIVE)) {
            enet_socket_send(host->socket, &stun_server_address, &send_buffer, 1);
            retransmit_at = timeout_deadline;
            continue;
        }
        uint8_t response_buffer[STUN_RESPONSE_BUFFER_SIZE];
        ENetBuffer receive_buffer = {.data = response_buffer, .dataLength = sizeof(response_buffer)};
        ENetAddress response_source;
        int bytes_received = enet_socket_receive(host->socket, &response_source, &receive_buffer, 1);
        if (bytes_received <= 0)
            continue;
        if (!enet_address_equal(&response_source, &stun_server_address))
            continue;
        if (bytes_received < (int)sizeof(struct StunHeader))
            continue;
        const struct StunHeader *stun_response_header = (const struct StunHeader *)response_buffer;
        if (ntohs(stun_response_header->type) != STUN_BINDING_SUCCESS || ntohl(stun_response_header->magic) != STUN_MAGIC_COOKIE || memcmp(stun_response_header->transaction_id, stun_request.transaction_id, sizeof(stun_response_header->transaction_id)) != 0)
            continue;
        int attribute_offset = (int)sizeof(struct StunHeader);
        int attributes_end = attribute_offset + (int)ntohs(stun_response_header->length);
        if (attributes_end > bytes_received)
            continue;
        char mapped_ip[64] = {0};
        uint16_t external_port = 0;
        while (attribute_offset + 4 <= attributes_end) {
            const struct StunAttrHeader *stun_attribute = (const struct StunAttrHeader *)(response_buffer + attribute_offset);
            uint16_t attribute_type = ntohs(stun_attribute->type);
            uint16_t attribute_length = ntohs(stun_attribute->length);
            attribute_offset += 4;
            if (attribute_type == STUN_ATTRIBUTE_XOR_MAPPED && attribute_length >= 8 && attribute_offset + attribute_length <= attributes_end && response_buffer[attribute_offset + 1] == 0x01) {
                uint16_t xor_encoded_port = ((uint16_t)response_buffer[attribute_offset + 2] << 8) | response_buffer[attribute_offset + 3];
                external_port = xor_encoded_port ^ (uint16_t)(STUN_MAGIC_COOKIE >> 16);
                uint32_t xor_encoded_address;
                memcpy(&xor_encoded_address, response_buffer + attribute_offset + 4, 4);
                uint32_t decoded_address = ntohl(xor_encoded_address) ^ STUN_MAGIC_COOKIE;
                snprintf(mapped_ip, sizeof(mapped_ip), "%u.%u.%u.%u",
                    (decoded_address >> 24) & 0xFFu, (decoded_address >> 16) & 0xFFu,
                    (decoded_address >> 8) & 0xFFu, decoded_address & 0xFFu);
                break;
            }
            attribute_offset += (attribute_length + 3) & ~3;
        }
        if (!external_port)
            continue;
        LbNetLog("STUN: external address %s:%u\n", mapped_ip, (unsigned)external_port);
        if (output_ip && output_ip_buffer_size > 0)
            snprintf(output_ip, output_ip_buffer_size, "%s", mapped_ip);
        keepalive_address = stun_server_address;
        keepalive_available = 1;
        return external_port;
    }
    return 0;
}

uint16_t holepunch_stun_query(ENetHost *host, char *output_ip, size_t output_ip_buffer_size)
{
    keepalive_available = 0;
    for (unsigned int i = 0; i < sizeof(stun_servers) / sizeof(stun_servers[0]); i++) {
        uint16_t port = query_stun_server(host, stun_servers[i], output_ip, output_ip_buffer_size);
        if (port)
            return port;
    }
    LbNetLog("STUN: failed to obtain mapped address\n");
    return 0;
}

void holepunch_stun_keepalive(ENetHost *host)
{
    if (!keepalive_available)
        return;
    struct StunHeader stun_request;
    create_stun_header(host, &stun_request, STUN_BINDING_INDICATION);
    ENetBuffer send_buffer = {.data = &stun_request, .dataLength = sizeof(stun_request)};
    enet_socket_send(host->socket, &keepalive_address, &send_buffer, 1);
}

static int send_and_burst(ENetSocket socket_handle, const ENetAddress *address, ENetBuffer *buffer)
{
    if (enet_socket_send(socket_handle, address, buffer, 1) < 0)
        return 0;
    for (int i = 1; i < HOLE_PUNCH_COUNT; i++)
        enet_socket_send(socket_handle, address, buffer, 1);
    return 1;
}

void holepunch_punch_to(ENetHost *host, const ENetAddress *target)
{
    static const uint8_t punch_payload[HOLE_PUNCH_PAYLOAD_SIZE] = {0};
    static Uint32 last_failure_log_time = 0;
    ENetBuffer send_buffer = {.data = (void *)punch_payload, .dataLength = sizeof(punch_payload)};
    if (send_and_burst(host->socket, target, &send_buffer))
        return;
    if (target->type == ENET_ADDRESS_TYPE_IPV4) {
        ENetAddress mapped_target = *target;
        enet_address_convert_ipv6(&mapped_target);
        if (send_and_burst(host->socket, &mapped_target, &send_buffer))
            return;
    }
    Uint32 current_time = SDL_GetTicks();
    if (last_failure_log_time == 0 || SDL_TICKS_PASSED(current_time, last_failure_log_time + HOLE_PUNCH_LOG_INTERVAL_MS)) {
        last_failure_log_time = current_time;
        LbNetLog("Holepunch: send failed\n");
    }
}
