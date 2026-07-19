/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/**
 * @file net_matchmaking.c
 *     Matchmaking client for the KeeperFX lobby server.
 * @par Purpose:
 *     Manages a WebSocket connection to the matchmaking server.
 *     Hosts register their lobby; clients list and join via hole-punch relay.
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
#include "net_matchmaking.h"
#include "bflib_basics.h"
#include "net_lan.h"
#include "net_portforward.h"
#include "ver_defs.h"

#include <SDL2/SDL.h>
#ifndef _WIN32
#include <sys/select.h>
#endif
#include <curl/curl.h>
#include <curl/websockets.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "post_inc.h"

#define STR_(x) #x
#define STR(x) STR_(x)
#define MATCHMAKING_VERSION STR(VER_MAJOR) "." STR(VER_MINOR) "." STR(VER_RELEASE) "." STR(VER_BUILD)

#define WEBSOCKET_BUFFER_SIZE         32768
#define WEBSOCKET_RECEIVE_TIMEOUT_MS  3000
#define SEND_BUFFER_SIZE              512
#define CONNECT_TIMEOUT_MS            5000
#define JSON_KEY_PATTERN_SIZE         128
#define CLAIM_RESPONSE_SIZE           256
#define RECONNECT_MAX_DELAY_MS        30000
#define CLAIM_REFRESH_MS              300000
#define HEARTBEAT_INTERVAL_MS         15000
#define MATCHMAKING_PROTOCOL_VERSION  2

static CURL *curl_handle = NULL;
static char hosted_lobby_id[MATCHMAKING_ID_MAX] = {0};
char join_lobby_id[MATCHMAKING_ID_MAX] = {0};
static SDL_mutex *mutex = NULL;
static SDL_atomic_t connect_thread_active = {0};
static SDL_atomic_t ips_resolved = {0};
static SDL_atomic_t ips_resolving = {0};
static int matchmaking_enabled = 0;
static int reconnect_attempt = 0;
static Uint32 reconnect_at = 0;
static Uint32 claims_resolved_at = 0;
static char local_ipv4[MATCHMAKING_IP_MAX] = {0};
static char local_ipv6[MATCHMAKING_IP_MAX] = {0};
static char ipv4_claim[MATCHMAKING_REQUEST_ID_MAX] = {0};
static char ipv6_claim[MATCHMAKING_REQUEST_ID_MAX] = {0};
static char requested_host_name[MATCHMAKING_NAME_MAX] = {0};
static int requested_ipv4_port = 0;
static int requested_ipv6_port = 0;

struct TbNetworkSessionNameEntry matchmaking_sessions[MATCHMAKING_SESSIONS_MAX];
int matchmaking_session_count = 0;

static void matchmaking_init(void);
static const char *json_parse_string(const char *json, const char *key, char *output, size_t output_buffer_size);

static size_t write_to_buffer(char *data, size_t element_size, size_t element_count, void *userdata)
{
    char *buffer = userdata;
    size_t incoming_size = element_size * element_count;
    size_t existing_size = strlen(buffer);
    if (existing_size + incoming_size >= CLAIM_RESPONSE_SIZE - 1) {
        LbNetLog("Matchmaking: write_to_buffer response too large, aborting\n");
        return 0;
    }
    memcpy(buffer + existing_size, data, incoming_size);
    buffer[existing_size + incoming_size] = '\0';
    return incoming_size;
}

static void resolve_public_address(int32_t address_family, char *output_ip, char *output_claim)
{
    char response[CLAIM_RESPONSE_SIZE] = {0};
    output_ip[0] = '\0';
    output_claim[0] = '\0';
    CURL *handle = curl_easy_init();
    if (!handle) return;
    curl_easy_setopt(handle, CURLOPT_URL, MATCHMAKING_CLAIM_URL);
    curl_easy_setopt(handle, CURLOPT_IPRESOLVE, address_family);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, (long)CONNECT_TIMEOUT_MS);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, (long)CONNECT_TIMEOUT_MS);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_to_buffer);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, (void *)response);
    CURLcode result = curl_easy_perform(handle);
    if (result == CURLE_OK) {
        json_parse_string(response, "ip", output_ip, MATCHMAKING_IP_MAX);
        json_parse_string(response, "claim", output_claim, MATCHMAKING_REQUEST_ID_MAX);
    } else {
        LbNetLog("Matchmaking: resolve_public_address failed (%s)\n", curl_easy_strerror(result));
    }
    curl_easy_cleanup(handle);
}

struct PublicAddressResolveTask {
    int32_t address_family;
    char *output_ip;
    char *output_claim;
};

static int resolve_public_address_thread(void *userdata)
{
    struct PublicAddressResolveTask *task = userdata;
    resolve_public_address(task->address_family, task->output_ip, task->output_claim);
    return 0;
}

static void wait_for_public_ip_resolution(void)
{
    while (SDL_AtomicGet(&ips_resolving)) {
        SDL_Delay(10);
    }
}

static int resolve_public_ips_thread(void *userdata)
{
    (void)userdata;
    char resolved_ipv4[MATCHMAKING_IP_MAX] = {0};
    char resolved_ipv6[MATCHMAKING_IP_MAX] = {0};
    char resolved_ipv4_claim[MATCHMAKING_REQUEST_ID_MAX] = {0};
    char resolved_ipv6_claim[MATCHMAKING_REQUEST_ID_MAX] = {0};
    struct PublicAddressResolveTask ipv6_task = { CURL_IPRESOLVE_V6, resolved_ipv6, resolved_ipv6_claim };
    SDL_Thread *ipv6_thread = SDL_CreateThread(resolve_public_address_thread, "resolve_ipv6", &ipv6_task);
    resolve_public_address(CURL_IPRESOLVE_V4, resolved_ipv4, resolved_ipv4_claim);
    if (ipv6_thread) {
        SDL_WaitThread(ipv6_thread, NULL);
    } else {
        resolve_public_address(CURL_IPRESOLVE_V6, resolved_ipv6, resolved_ipv6_claim);
    }
    SDL_LockMutex(mutex);
    if (resolved_ipv4[0] != '\0')
        snprintf(local_ipv4, sizeof(local_ipv4), "%s", resolved_ipv4);
    if (resolved_ipv6[0] != '\0')
        snprintf(local_ipv6, sizeof(local_ipv6), "%s", resolved_ipv6);
    snprintf(ipv4_claim, sizeof(ipv4_claim), "%s", resolved_ipv4_claim);
    snprintf(ipv6_claim, sizeof(ipv6_claim), "%s", resolved_ipv6_claim);
    claims_resolved_at = SDL_GetTicks();
    LbNetLog("Matchmaking: public IPs: ipv4=%s ipv6=%s\n", local_ipv4, local_ipv6);
    SDL_AtomicSet(&ips_resolved, 1);
    SDL_UnlockMutex(mutex);
    SDL_AtomicSet(&ips_resolving, 0);
    return 0;
}

static void resolve_public_ips_async(void)
{
    if (SDL_AtomicGet(&ips_resolved) && SDL_GetTicks() - claims_resolved_at < CLAIM_REFRESH_MS)
        return;
    if (SDL_AtomicCAS(&ips_resolving, 0, 1) == SDL_FALSE)
        return;
    SDL_Thread *thread = SDL_CreateThread(resolve_public_ips_thread, "resolve_ips", NULL);
    if (thread) {
        SDL_DetachThread(thread);
    } else {
        SDL_AtomicSet(&ips_resolving, 0);
    }
}

static void websocket_cleanup(void)
{
    if (curl_handle)
        curl_easy_cleanup(curl_handle);
    curl_handle = NULL;
    hosted_lobby_id[0] = '\0';
    matchmaking_session_count = 0;
    if (matchmaking_enabled) {
        int delay = 1000 << min(reconnect_attempt, 5);
        reconnect_at = SDL_GetTicks() + min(delay, RECONNECT_MAX_DELAY_MS);
        reconnect_attempt++;
    }
}

static int websocket_send(const char *request)
{
    size_t request_size = strlen(request);
    size_t offset = 0;
    Uint32 deadline = SDL_GetTicks() + WEBSOCKET_RECEIVE_TIMEOUT_MS;
    while (offset < request_size) {
        size_t bytes_sent = 0;
        CURLcode curl_result = curl_ws_send(curl_handle, request + offset, request_size - offset, &bytes_sent, 0, CURLWS_TEXT);
        offset += bytes_sent;
        if (curl_result == CURLE_OK)
            continue;
        if (curl_result == CURLE_AGAIN) {
            curl_socket_t raw_socket = CURL_SOCKET_BAD;
            if (curl_easy_getinfo(curl_handle, CURLINFO_ACTIVESOCKET, &raw_socket) != CURLE_OK || raw_socket == CURL_SOCKET_BAD)
                break;
            fd_set writable_sockets;
            FD_ZERO(&writable_sockets);
            FD_SET(raw_socket, &writable_sockets);
            int remaining = (int)(deadline - SDL_GetTicks());
            if (remaining <= 0)
                break;
            struct timeval timeout_value = { remaining / 1000, (remaining % 1000) * 1000 };
            if (select((int)raw_socket + 1, NULL, &writable_sockets, NULL, &timeout_value) > 0)
                continue;
        }
        LbNetLog("Matchmaking: websocket_send failed (%s)\n", curl_easy_strerror(curl_result));
        break;
    }
    if (offset == request_size)
        return 0;
    websocket_cleanup();
    return -1;
}

static int websocket_receive(char *response_buffer, size_t buffer_size, int timeout_ms)
{
    curl_socket_t raw_socket = CURL_SOCKET_BAD;
    if (curl_easy_getinfo(curl_handle, CURLINFO_ACTIVESOCKET, &raw_socket) != CURLE_OK || raw_socket == CURL_SOCKET_BAD) {
        LbNetLog("Matchmaking: websocket_receive failed to get active socket\n");
        return -1;
    }
    Uint32 timeout_deadline = SDL_GetTicks() + timeout_ms;
    size_t total_received = 0;
    while (total_received < buffer_size - 1) {
        size_t bytes_received = 0;
        const struct curl_ws_frame *websocket_frame = NULL;
        CURLcode curl_result = curl_ws_recv(curl_handle, response_buffer + total_received, buffer_size - total_received - 1, &bytes_received, &websocket_frame);
        total_received += bytes_received;
        if (curl_result == CURLE_AGAIN) {
            if (timeout_ms <= 0) {
                if (!total_received)
                    return 0;
                timeout_ms = WEBSOCKET_RECEIVE_TIMEOUT_MS;
                timeout_deadline = SDL_GetTicks() + timeout_ms;
            }
            int time_remaining = (int)(timeout_deadline - SDL_GetTicks());
            if (time_remaining <= 0)
                return 0;
            fd_set readable_sockets;
            FD_ZERO(&readable_sockets);
            FD_SET(raw_socket, &readable_sockets);
            struct timeval timeout_value = { time_remaining / 1000, (time_remaining % 1000) * 1000 };
            if (select((int)raw_socket + 1, &readable_sockets, NULL, NULL, &timeout_value) <= 0)
                return 0;
            continue;
        }
        if (curl_result != CURLE_OK) {
            LbNetLog("Matchmaking: websocket_receive failed (%s)\n", curl_easy_strerror(curl_result));
            websocket_cleanup();
            return -1;
        }
        if (!websocket_frame)
            continue;
        if (websocket_frame->flags & CURLWS_CLOSE) {
            websocket_cleanup();
            return -1;
        }
        if (websocket_frame->flags & (CURLWS_PING | CURLWS_PONG)) {
            total_received -= bytes_received;
            continue;
        }
        if (websocket_frame->bytesleft > 0 || websocket_frame->flags & CURLWS_CONT)
            continue;
        response_buffer[total_received] = '\0';
        return (int)total_received;
    }
    LbNetLog("Matchmaking: websocket message too large\n");
    websocket_cleanup();
    return -1;
}

static int websocket_exchange(const char *request, char *response_buffer, size_t buffer_size)
{
    if (!curl_handle) return -1;
    if (websocket_send(request) != 0) return -1;
    return websocket_receive(response_buffer, buffer_size, WEBSOCKET_RECEIVE_TIMEOUT_MS);
}

int matchmaking_request_list(void)
{
    SDL_LockMutex(mutex);
    if (!curl_handle) {
        SDL_UnlockMutex(mutex);
        return -1;
    }
    matchmaking_session_count = 0;
    int result = websocket_send("{\"action\":\"list\",\"protocolVersion\":2,\"version\":\"" MATCHMAKING_VERSION "\"}");
    SDL_UnlockMutex(mutex);
    return result;
}

static const char *json_parse_string(const char *json, const char *key, char *output, size_t output_buffer_size)
{
    char key_pattern[JSON_KEY_PATTERN_SIZE];
    snprintf(key_pattern, sizeof(key_pattern), "\"%s\":\"", key);
    const char *json_cursor = strstr(json, key_pattern);
    if (!json_cursor)
        return NULL;
    json_cursor += strlen(key_pattern);
    size_t output_length = 0;
    while (*json_cursor && *json_cursor != '"' && output_length < output_buffer_size - 1) {
        if (*json_cursor == '\\' && json_cursor[1] != '\0')
            json_cursor++;
        output[output_length++] = *json_cursor++;
    }
    output[output_length] = '\0';
    if (*json_cursor != '"')
        return NULL;
    return json_cursor + 1;
}

static void create_request_id(char *output)
{
    static unsigned int counter = 0;
    counter++;
    snprintf(output, MATCHMAKING_REQUEST_ID_MAX, "%08x%08x", (unsigned int)SDL_GetTicks(), counter);
}

static int json_parse_int(const char *json, const char *key, int *output)
{
    char key_pattern[JSON_KEY_PATTERN_SIZE];
    snprintf(key_pattern, sizeof(key_pattern), "\"%s\":", key);
    const char *json_cursor = strstr(json, key_pattern);
    if (!json_cursor)
        return 0;
    json_cursor += strlen(key_pattern);
    *output = atoi(json_cursor);
    return 1;
}

static void parse_punch_addresses(const char *json, PunchAddresses *output)
{
    *output = (PunchAddresses){0};
    json_parse_string(json, "peerIpv4", output->ipv4, MATCHMAKING_IP_MAX);
    json_parse_string(json, "peerIpv6", output->ipv6, MATCHMAKING_IP_MAX);
    json_parse_int(json, "peerIpv4Port", &output->ipv4_port);
    output->ipv6_port = output->ipv4_port;
    json_parse_int(json, "peerIpv6Port", &output->ipv6_port);
    json_parse_string(json, "requestId", output->request_id, sizeof(output->request_id));
    json_parse_string(json, "punch", output->punch, sizeof(output->punch));
}

static int punch_addresses_valid(const PunchAddresses *addresses)
{
    return (addresses->ipv4_port && addresses->ipv4[0] != '\0') || (addresses->ipv6_port && addresses->ipv6[0] != '\0');
}

static void matchmaking_init(void)
{
    static int s_initialized = 0;
    if (s_initialized)
        return;
    s_initialized = 1;
    mutex = SDL_CreateMutex();
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

static void load_published_claims(int udp_ipv4_port, int udp_ipv6_port, char *published_ipv4_claim, char *published_ipv6_claim)
{
    wait_for_public_ip_resolution();
    published_ipv4_claim[0] = '\0';
    published_ipv6_claim[0] = '\0';
    SDL_LockMutex(mutex);
    if (udp_ipv4_port > 0)
        snprintf(published_ipv4_claim, MATCHMAKING_REQUEST_ID_MAX, "%s", ipv4_claim);
    if (udp_ipv6_port > 0)
        snprintf(published_ipv6_claim, MATCHMAKING_REQUEST_ID_MAX, "%s", ipv6_claim);
    SDL_UnlockMutex(mutex);
}

static int matchmaking_connect_thread(void *)
{
    if (matchmaking_connect() == 0) {
        matchmaking_request_list();
        SDL_LockMutex(mutex);
        int recreate_lobby = requested_host_name[0] != '\0' && hosted_lobby_id[0] == '\0';
        char host_name[MATCHMAKING_NAME_MAX];
        snprintf(host_name, sizeof(host_name), "%s", requested_host_name);
        int ipv4_port = requested_ipv4_port;
        int ipv6_port = requested_ipv6_port;
        SDL_UnlockMutex(mutex);
        if (recreate_lobby)
            matchmaking_create(host_name, ipv4_port, ipv6_port);
    }
    SDL_AtomicSet(&connect_thread_active, 0);
    return 0;
}

void matchmaking_connect_async(void)
{
    matchmaking_init();
    SDL_LockMutex(mutex);
    matchmaking_enabled = 1;
    SDL_UnlockMutex(mutex);
    if (SDL_AtomicCAS(&connect_thread_active, 0, 1) == SDL_FALSE)
        return;
    resolve_public_ips_async();
    SDL_Thread *thread = SDL_CreateThread(matchmaking_connect_thread, "matchmaking", NULL);
    if (thread) {
        SDL_DetachThread(thread);
    } else {
        SDL_AtomicSet(&connect_thread_active, 0);
    }
}

int matchmaking_connect(void)
{
    SDL_LockMutex(mutex);
    if (curl_handle) {
        SDL_UnlockMutex(mutex);
        return 0;
    }
    if (!matchmaking_enabled) {
        SDL_UnlockMutex(mutex);
        return -1;
    }
    curl_handle = curl_easy_init();
    if (!curl_handle) {
        SDL_UnlockMutex(mutex);
        return -1;
    }
    LbNetLog("Matchmaking: connecting to %s\n", MATCHMAKING_URL);
    curl_easy_setopt(curl_handle, CURLOPT_URL, MATCHMAKING_URL);
    curl_easy_setopt(curl_handle, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT_MS, (long)CONNECT_TIMEOUT_MS);
    CURLcode curl_result = curl_easy_perform(curl_handle);
    if (curl_result != CURLE_OK) {
        LbNetLog("Matchmaking: connect to %s failed: %s\n", MATCHMAKING_URL, curl_easy_strerror(curl_result));
        websocket_cleanup();
        SDL_UnlockMutex(mutex);
        return -1;
    }
    reconnect_attempt = 0;
    reconnect_at = 0;
    LbNetLog("Matchmaking: connected\n");
    SDL_UnlockMutex(mutex);
    return 0;
}

void matchmaking_disconnect(void)
{
    if (mutex == NULL) {
        return;
    }
    Uint32 wait_deadline = SDL_GetTicks() + CONNECT_TIMEOUT_MS * 3;
    while (SDL_AtomicGet(&connect_thread_active) && SDL_GetTicks() < wait_deadline) {
        SDL_Delay(10);
    }
    wait_for_public_ip_resolution();
    matchmaking_enabled = 0;
    matchmaking_close_lobby();
    SDL_LockMutex(mutex);
    SDL_AtomicSet(&ips_resolved, 0);
    if (curl_handle) {
        websocket_cleanup();
        LbNetLog("Matchmaking: disconnected\n");
    }
    SDL_UnlockMutex(mutex);
}

void matchmaking_close_lobby(void)
{
    if (mutex == NULL) {
        return;
    }
    SDL_LockMutex(mutex);
    matchmaking_enabled = 0;
    requested_host_name[0] = '\0';
    requested_ipv4_port = 0;
    requested_ipv6_port = 0;
    if (curl_handle) {
        if (hosted_lobby_id[0] != '\0') {
            char delete_message[SEND_BUFFER_SIZE];
            snprintf(delete_message, sizeof(delete_message), "{\"action\":\"delete\",\"id\":\"%s\"}", hosted_lobby_id);
            websocket_send(delete_message);
        }
        if (curl_handle) {
            websocket_cleanup();
        }
    }
    hosted_lobby_id[0] = '\0';
    lan_set_lobby_id("");
    SDL_UnlockMutex(mutex);
}

void matchmaking_refresh_sessions(void)
{
    if (!mutex)
        return;
    if (SDL_TryLockMutex(mutex) != 0)
        return;
    if (!curl_handle) {
        int reconnect = matchmaking_enabled && SDL_GetTicks() >= reconnect_at;
        SDL_UnlockMutex(mutex);
        if (reconnect)
            matchmaking_connect_async();
        return;
    }
    char response_buffer[WEBSOCKET_BUFFER_SIZE];
    int bytes_received = websocket_receive(response_buffer, sizeof(response_buffer), 0);
    if (bytes_received > 0)
        LbNetLog("Matchmaking: list response (%d bytes): %s\n", bytes_received, response_buffer);
    if (bytes_received > 0 && strstr(response_buffer, "\"lobbies\"")) {
        int count = 0;
        const char *json_cursor = response_buffer;
        while (count < MATCHMAKING_SESSIONS_MAX) {
            char id[MATCHMAKING_ID_MAX];
            char name[MATCHMAKING_NAME_MAX];
            json_cursor = json_parse_string(json_cursor, "id", id, sizeof(id));
            if (!json_cursor) break;
            json_cursor = json_parse_string(json_cursor, "name", name, sizeof(name));
            if (!json_cursor) break;
            if (hosted_lobby_id[0] != '\0' && strcmp(id, hosted_lobby_id) == 0)
                continue;
            struct TbNetworkSessionNameEntry *session = &matchmaking_sessions[count++];
            memset(session, 0, sizeof(*session));
            session->joinable = 1;
            session->in_use = 1;
            session->id = (unsigned long)count;
            snprintf(session->text, SESSION_NAME_MAX_LEN, "%s", name);
            snprintf(session->join_address, SESSION_LOBBY_ID_MAX_LEN, "%s", id);
            snprintf(session->lobby_id, SESSION_LOBBY_ID_MAX_LEN, "%s", id);
        }
        matchmaking_session_count = count;
        LbNetLog("Matchmaking: parsed %d session(s)\n", count);
    }
    SDL_UnlockMutex(mutex);
}

int matchmaking_create(const char *name, int udp_ipv4_port, int udp_ipv6_port)
{
    char escaped_lobby_name[MATCHMAKING_NAME_MAX * 2];
    char request_message[SEND_BUFFER_SIZE];
    char response_buffer[WEBSOCKET_BUFFER_SIZE];
    char published_ipv4_claim[MATCHMAKING_REQUEST_ID_MAX];
    char published_ipv6_claim[MATCHMAKING_REQUEST_ID_MAX];
    char request_id[MATCHMAKING_REQUEST_ID_MAX];
    load_published_claims(udp_ipv4_port, udp_ipv6_port, published_ipv4_claim, published_ipv6_claim);
    char published_ipv6[MATCHMAKING_IP_MAX];
    SDL_LockMutex(mutex);
    snprintf(published_ipv6, sizeof(published_ipv6), "%s", local_ipv6);
    SDL_UnlockMutex(mutex);
    if (udp_ipv6_port > 0)
        port_forward_add_ipv6_pinhole(published_ipv6, (uint16_t)udp_ipv6_port);
    create_request_id(request_id);
    SDL_LockMutex(mutex);
    snprintf(requested_host_name, sizeof(requested_host_name), "%s", name);
    requested_ipv4_port = udp_ipv4_port;
    requested_ipv6_port = udp_ipv6_port;
    matchmaking_enabled = 1;
    if (!curl_handle) {
        LbNetLog("Matchmaking: not connected to server, lobby won't be listed online\n");
        SDL_UnlockMutex(mutex);
        matchmaking_connect_async();
        return -1;
    }
    int write_position = 0;
    for (int i = 0; name[i] && write_position < (int)sizeof(escaped_lobby_name) - 2; i++) {
        if (name[i] == '"' || name[i] == '\\')
            escaped_lobby_name[write_position++] = '\\';
        escaped_lobby_name[write_position++] = name[i];
    }
    escaped_lobby_name[write_position] = '\0';
    snprintf(request_message, sizeof(request_message),
        "{\"action\":\"create\",\"protocolVersion\":%d,\"requestId\":\"%s\",\"name\":\"%s\",\"ipv4Port\":%d,\"ipv6Port\":%d,\"version\":\"%s\",\"ipv4Claim\":\"%s\",\"ipv6Claim\":\"%s\"}",
        MATCHMAKING_PROTOCOL_VERSION, request_id, escaped_lobby_name, udp_ipv4_port, udp_ipv6_port, MATCHMAKING_VERSION, published_ipv4_claim, published_ipv6_claim);
    int bytes_received = websocket_exchange(request_message, response_buffer, sizeof(response_buffer));
    // Skip stale lobby responses
    while (bytes_received > 0 && strstr(response_buffer, "\"lobbies\"")) {
        bytes_received = websocket_receive(response_buffer, sizeof(response_buffer), WEBSOCKET_RECEIVE_TIMEOUT_MS);
    }
    if (bytes_received > 0) {
        LbNetLog("Matchmaking: create response (%d bytes): %s\n", bytes_received, response_buffer);
    }
    if (bytes_received <= 0) {
        SDL_UnlockMutex(mutex);
        return -1;
    }
    char response_request_id[MATCHMAKING_REQUEST_ID_MAX];
    if (!strstr(response_buffer, "\"created\"") || !json_parse_string(response_buffer, "requestId", response_request_id, sizeof(response_request_id)) || strcmp(request_id, response_request_id) != 0 || !json_parse_string(response_buffer, "id", hosted_lobby_id, MATCHMAKING_ID_MAX)) {
        LbNetLog("Matchmaking: create failed - unexpected response\n");
        SDL_UnlockMutex(mutex);
        return -1;
    }
    LbNetLog("Matchmaking: created lobby id=%s\n", hosted_lobby_id);
    lan_set_lobby_id(hosted_lobby_id);
    SDL_UnlockMutex(mutex);
    return 0;
}

int matchmaking_punch(const char *lobby_id, int udp_ipv4_port, int udp_ipv6_port, PunchAddresses *output)
{
    char request_message[SEND_BUFFER_SIZE];
    char response_buffer[WEBSOCKET_BUFFER_SIZE];
    char published_ipv4_claim[MATCHMAKING_REQUEST_ID_MAX];
    char published_ipv6_claim[MATCHMAKING_REQUEST_ID_MAX];
    char request_id[MATCHMAKING_REQUEST_ID_MAX];
    load_published_claims(udp_ipv4_port, udp_ipv6_port, published_ipv4_claim, published_ipv6_claim);
    create_request_id(request_id);
    SDL_LockMutex(mutex);
    if (!curl_handle) {
        LbNetLog("Matchmaking: not connected to server, UDP hole punching unavailable\n");
        SDL_UnlockMutex(mutex);
        return -1;
    }
    snprintf(request_message, sizeof(request_message),
        "{\"action\":\"punch\",\"protocolVersion\":%d,\"requestId\":\"%s\",\"lobbyId\":\"%s\",\"myIpv4Port\":%d,\"myIpv6Port\":%d,\"ipv4Claim\":\"%s\",\"ipv6Claim\":\"%s\"}",
        MATCHMAKING_PROTOCOL_VERSION, request_id, lobby_id, udp_ipv4_port, udp_ipv6_port, published_ipv4_claim, published_ipv6_claim);
    if (websocket_send(request_message) != 0) {
        SDL_UnlockMutex(mutex);
        return -1;
    }
    int bytes_received;
    Uint32 timeout_deadline = SDL_GetTicks() + WEBSOCKET_RECEIVE_TIMEOUT_MS;
    while (1) {
        int time_remaining = (int)(timeout_deadline - SDL_GetTicks());
        if (time_remaining <= 0) {
            LbNetLog("Matchmaking: punch failed - timeout\n");
            SDL_UnlockMutex(mutex);
            return -1;
        }
        bytes_received = websocket_receive(response_buffer, sizeof(response_buffer), time_remaining);
        if (bytes_received > 0)
            LbNetLog("Matchmaking: punch response (%d bytes): %s\n", bytes_received, response_buffer);
        if (bytes_received <= 0) {
            SDL_UnlockMutex(mutex);
            return -1;
        }
        if (strstr(response_buffer, "\"punch\"")) {
            char response_request_id[MATCHMAKING_REQUEST_ID_MAX];
            if (json_parse_string(response_buffer, "requestId", response_request_id, sizeof(response_request_id)) && strcmp(request_id, response_request_id) == 0)
                break;
            continue;
        }
        if (!strstr(response_buffer, "\"lobbies\"")) {
            LbNetLog("Matchmaking: punch failed - server error response\n");
            SDL_UnlockMutex(mutex);
            return -1;
        }
    }
    parse_punch_addresses(response_buffer, output);
    if (!punch_addresses_valid(output)) {
        LbNetLog("Matchmaking: punch failed - unexpected response\n");
        SDL_UnlockMutex(mutex);
        return -1;
    }
    LbNetLog("Matchmaking: punch relay -> ipv4=%s ipv6=%s ipv4_port=%d ipv6_port=%d\n", output->ipv4, output->ipv6, output->ipv4_port, output->ipv6_port);
    SDL_UnlockMutex(mutex);
    return 0;
}

void matchmaking_host_heartbeat(void)
{
    static Uint32 next_heartbeat = 0;
    if (!mutex)
        return;
    if (SDL_TryLockMutex(mutex) != 0)
        return;
    if (!curl_handle) {
        int reconnect = matchmaking_enabled && SDL_GetTicks() >= reconnect_at;
        SDL_UnlockMutex(mutex);
        if (reconnect)
            matchmaking_connect_async();
        return;
    }
    Uint32 now = SDL_GetTicks();
    if (now < next_heartbeat) {
        SDL_UnlockMutex(mutex);
        return;
    }
    if (hosted_lobby_id[0] != '\0' && websocket_send("{\"action\":\"heartbeat\"}") == 0)
        next_heartbeat = now + HEARTBEAT_INTERVAL_MS;
    SDL_UnlockMutex(mutex);
}

void matchmaking_update_ports(int udp_ipv4_port, int udp_ipv6_port)
{
    if (!mutex || SDL_TryLockMutex(mutex) != 0)
        return;
    if (curl_handle && hosted_lobby_id[0] != '\0') {
        char request[SEND_BUFFER_SIZE];
        snprintf(request, sizeof(request), "{\"action\":\"update\",\"ipv4Port\":%d,\"ipv6Port\":%d}", udp_ipv4_port, udp_ipv6_port);
        websocket_send(request);
    }
    SDL_UnlockMutex(mutex);
}

int matchmaking_poll_punch(PunchAddresses *output)
{
    if (!curl_handle || !mutex || SDL_TryLockMutex(mutex) != 0)
        return 0;
    char response_buffer[WEBSOCKET_BUFFER_SIZE];
    int bytes_received = websocket_receive(response_buffer, sizeof(response_buffer), 0);
    int punch_was_received = 0;
    if (bytes_received > 0 && strstr(response_buffer, "\"punch\"")) {
        parse_punch_addresses(response_buffer, output);
        punch_was_received = punch_addresses_valid(output);
        if (punch_was_received)
            LbNetLog("Matchmaking: poll_punch -> ipv4=%s ipv6=%s ipv4_port=%d ipv6_port=%d\n", output->ipv4, output->ipv6, output->ipv4_port, output->ipv6_port);
        else
            LbNetLog("Matchmaking: poll_punch parse failed\n");
    }
    SDL_UnlockMutex(mutex);
    return punch_was_received;
}
