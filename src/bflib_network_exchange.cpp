/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file bflib_network_exchange.cpp
 *     Network data exchange for Dungeon Keeper multiplayer.
 * @par Purpose:
 *     Network data exchange routines for multiplayer games.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     11 Apr 2009 - 13 May 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "bflib_network_exchange.h"
#include "bflib_network.h"
#include "bflib_network_internal.h"
#include "bflib_datetm.h"
#include "bflib_sound.h"
#include "globals.h"
#include "player_data.h"
#include "net_game.h"
#include "front_landview.h"
#include "game_legacy.h"
#include "packets.h"
#include "keeperfx.hpp"
#include <stdlib.h>
#include <zlib.h>
#include "post_inc.h"

#ifdef __cplusplus
extern "C" void network_yield_draw_gameplay();
extern "C" void network_yield_waiting_gameplay_packets();
extern "C" void network_yield_draw_frontend();
extern "C" void LbNetwork_TimesyncBarrier(void);
extern "C" TbBool keeper_screen_redraw(void);
extern "C" TbResult LbScreenSwap(void);
#endif

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

#define SEND_DUPLICATE_PACKETS 3
#define PACKET_HISTORY_SIZE 32
#define PROACTIVE_HISTORY_SEND_MS 2000
#define REDUNDANT_PACKET_BUNDLE 2

struct ReceivedPacketEntry {
    GameTurn turn;
    PlayerNumber player;
    TbBool valid;
    struct Packet packet;
};

struct PacketHistory {
    struct Packet packets[PACKET_HISTORY_SIZE];
    unsigned char write_index;
    unsigned char valid_count;
};

#pragma pack(1)
struct RedundantPacketBundle {
    unsigned char valid_count;
    struct Packet packets[PACKET_HISTORY_SIZE];
};

struct GameplayPacketHeader {
    PlayerNumber player;
    unsigned int compressed_length;
    unsigned int original_length;
    unsigned int data_checksum;
};
#pragma pack()

static struct {
    struct ReceivedPacketEntry entries[PACKET_HISTORY_SIZE * PACKETS_COUNT];
    int head;
} received_history;

static struct PacketHistory packet_history[MAX_N_USERS];

static TbBool has_received_player_packet(GameTurn turn, PlayerNumber player);

char* begin_message(enum NetMessageType msg_type)
{
    char* msg_pos = netstate.msg_buffer;
    *msg_pos = msg_type;
    return msg_pos + 1;
}

// Clients send directly only to the host; the host relays frames to everyone else.
static TbBool can_send_peer(NetUserId peer_id)
{
    return (peer_id != netstate.my_id) &&
        (netstate.users[peer_id].progress != USER_UNUSED) &&
        (my_player_number == get_host_player_id() || peer_id == SERVER_ID);
}

static TbBool is_history_player_valid(PlayerNumber player)
{
    return player >= 0 && player < MAX_N_USERS;
}

static size_t packet_list_size(unsigned char valid_count)
{
    return sizeof(unsigned char) + valid_count * sizeof(struct Packet);
}

static void store_packet_history(PlayerNumber player, const struct Packet* packet)
{
    if (!is_history_player_valid(player)) {
        return;
    }
    struct PacketHistory* history = &packet_history[player];
    for (unsigned char i = 0; i < history->valid_count; i += 1) {
        if (history->packets[i].turn == packet->turn) {
            history->packets[i] = *packet;
            return;
        }
    }
    history->packets[history->write_index] = *packet;
    history->write_index = (history->write_index + 1) % PACKET_HISTORY_SIZE;
    if (history->valid_count < PACKET_HISTORY_SIZE) {
        history->valid_count += 1;
    }
}

static size_t build_packet_bundle(PlayerNumber player, struct RedundantPacketBundle* packet_bundle, unsigned char packet_count, const struct Packet* first_packet)
{
    const struct PacketHistory* history;
    packet_bundle->valid_count = 0;
    if (!is_history_player_valid(player) || packet_count < 1) {
        return 0;
    }
    history = &packet_history[player];
    if (first_packet != NULL) {
        packet_bundle->packets[0] = *first_packet;
        packet_bundle->valid_count = 1;
    }
    unsigned char copied_count = 0;
    unsigned char remaining_packets = packet_count - packet_bundle->valid_count;
    for (unsigned char i = 0; i < history->valid_count && copied_count < remaining_packets; i += 1) {
        int32_t history_index = (history->write_index + PACKET_HISTORY_SIZE - i - 1) % PACKET_HISTORY_SIZE;
        if (first_packet != NULL && history->packets[history_index].turn == first_packet->turn) {
            continue;
        }
        packet_bundle->packets[packet_bundle->valid_count + copied_count] = history->packets[history_index];
        copied_count += 1;
    }
    packet_bundle->valid_count += copied_count;
    if (first_packet != NULL) {
        store_packet_history(player, first_packet);
    }
    if (packet_bundle->valid_count < 1) {
        return 0;
    }
    return packet_list_size(packet_bundle->valid_count);
}

static TbBool unpack_packet_bundle(const char* packet_bundle_buffer, size_t packet_bundle_buffer_size, PlayerNumber source_player, void* out_packet, size_t packet_size)
{
    if (packet_size != sizeof(struct Packet)) {
        WARNLOG("Gameplay packet bundle size mismatch (%u != %u)", (unsigned)packet_size, (unsigned)sizeof(struct Packet));
        return false;
    }
    if (!is_history_player_valid(source_player) || packet_bundle_buffer_size < sizeof(unsigned char)) {
        return false;
    }
    const struct RedundantPacketBundle* packet_bundle = (const struct RedundantPacketBundle*)packet_bundle_buffer;
    if (packet_bundle->valid_count < 1 || packet_bundle->valid_count > PACKET_HISTORY_SIZE) {
        return false;
    }
    if (packet_bundle_buffer_size < packet_list_size(packet_bundle->valid_count)) {
        return false;
    }
    for (unsigned char i = 0; i < packet_bundle->valid_count; i += 1) {
        const struct Packet* packet = &packet_bundle->packets[i];
        store_packet_history(source_player, packet);
        if (!has_received_player_packet(packet->turn, source_player)) {
            if (is_packet_empty(packet)) {
                MULTIPLAYER_LOG("unpack_packet_bundle: Skipping empty packet for player %d turn %lu", source_player, (unsigned long)packet->turn);
                continue;
            }
            int index = received_history.head;
            struct ReceivedPacketEntry* entry = &received_history.entries[index];
            entry->turn = packet->turn;
            entry->player = source_player;
            entry->valid = true;
            memcpy(&entry->packet, packet, sizeof(struct Packet));
            received_history.head = (index + 1) % (PACKET_HISTORY_SIZE * PACKETS_COUNT);
        }
    }
    memcpy(out_packet, &packet_bundle->packets[0], packet_size);
    return true;
}

void send_buffered_message(NetUserId dest, const char* end_ptr)
{
    size_t msg_size = end_ptr - netstate.msg_buffer;
    netstate.sp->sendmsg_single(dest, netstate.msg_buffer, msg_size);
}

static void send_network_message(NetUserId destination, const char* buffer, size_t msg_size, TbBool unsequenced)
{
    if (!unsequenced) {
        netstate.sp->sendmsg_single(destination, buffer, msg_size);
        return;
    }

    for (int i = 0; i < SEND_DUPLICATE_PACKETS; i += 1) {
        netstate.sp->sendmsg_single_unsequenced(destination, buffer, msg_size);
    }
}

static void send_to_active_peers(NetUserId skip_first, NetUserId skip_second, const char* buffer, size_t msg_size, TbBool unsequenced)
{
    for (NetUserId id = 0; id < netstate.max_players; id += 1) {
        if (id == skip_first || id == skip_second || !IsUserActive(id)) {
            continue;
        }
        send_network_message(id, buffer, msg_size, unsequenced);
    }
}

static void send_to_host_or_peers(size_t msg_size)
{
    if (netstate.my_id != SERVER_ID) {
        if (IsUserActive(SERVER_ID)) {
            netstate.sp->sendmsg_single(SERVER_ID, netstate.msg_buffer, msg_size);
        }
        return;
    }
    send_to_active_peers(netstate.my_id, INVALID_USER_ID, netstate.msg_buffer, msg_size, false);
}

static TbError exchange_frame_data(void *send_buf, void *server_buf, size_t client_frame_size, enum NetMessageType msg_type)
{
    if (netstate.my_id < 0 || netstate.my_id >= netstate.max_players) {
        ERRORLOG("Invalid my_id %i in network exchange (disconnected?)", netstate.my_id);
        return Lb_FAIL;
    }
    netstate.sp->update(OnNewUser);
    memcpy(((char*)server_buf) + netstate.my_id * client_frame_size, send_buf, client_frame_size);
    char* msg_pos = begin_message(msg_type);
    *msg_pos = netstate.my_id;
    msg_pos += 1;
    *(int*)msg_pos = netstate.seq_nbr;
    msg_pos += 4;
    if (msg_type == NETMSG_GAMEPLAY_UNSEQUENCED) {
        struct RedundantPacketBundle* packet_bundle = (struct RedundantPacketBundle*)msg_pos;
        size_t packet_bundle_size = build_packet_bundle((PlayerNumber)netstate.my_id, packet_bundle, REDUNDANT_PACKET_BUNDLE, (const struct Packet*)send_buf);
        if (packet_bundle_size > 0) {
            msg_pos += packet_bundle_size;
        }
    } else {
        memcpy(msg_pos, send_buf, client_frame_size);
        msg_pos += client_frame_size;
    }
    size_t msg_size = msg_pos - netstate.msg_buffer;
    TbBool unsequenced = (msg_type == NETMSG_GAMEPLAY_UNSEQUENCED);
    for (NetUserId peer_id = 0; peer_id < netstate.max_players; peer_id += 1) {
        if (peer_id == netstate.my_id) {
            continue;
        }
        if (!can_send_peer(peer_id)) {
            continue;
        }
        send_network_message(peer_id, netstate.msg_buffer, msg_size, unsequenced);
    }
    return Lb_OK;
}

static TbError process_message(NetUserId source, void* server_buf, size_t frame_size)
{
    size_t msg_size = netstate.sp->readmsg(source, netstate.msg_buffer, sizeof(netstate.msg_buffer));
    if (msg_size <= 0) {
        ERRORLOG("Problem reading message from %u", source);
        return Lb_FAIL;
    }
    char *msg_pos = netstate.msg_buffer;
    enum NetMessageType message_type = (enum NetMessageType)*msg_pos;
    msg_pos += 1;
    if (message_type == NETMSG_LOGIN) {
        if (source == SERVER_ID) {
            netstate.my_id = (NetUserId)*msg_pos;
            msg_pos += 1;
            netstate.users[netstate.my_id].version = net_current_version;
            netstate.users[SERVER_ID].version = *(const struct GameVersionPacket *)msg_pos;
            return Lb_OK;
        }
        if (netstate.users[source].progress != USER_CONNECTED) {
            NETMSG("Peer was not in connected state");
            return Lb_OK;
        }
        const char* password;
        {
            size_t max_read = sizeof(netstate.msg_buffer) - (msg_pos - netstate.msg_buffer);
            size_t len = strnlen(msg_pos, max_read);
            if (len >= max_read || len > sizeof(netstate.password)) {
                NETDBG(6, "Connected peer sent invalid password");
                netstate.sp->drop_user(source);
                return Lb_OK;
            }
            password = msg_pos;
            msg_pos += len + 1;
        }
        if (netstate.password[0] != 0 && strncmp(password, netstate.password, sizeof(netstate.password)) != 0) {
            NETMSG("Peer chose wrong password");
            return Lb_OK;
        }
        const char* name;
        {
            size_t max_read = sizeof(netstate.msg_buffer) - (msg_pos - netstate.msg_buffer);
            size_t len = strnlen(msg_pos, max_read);
            if (len >= max_read || len > sizeof(netstate.users[source].name) - 1 || len == 0) {
                NETDBG(6, "Connected peer sent invalid name");
                netstate.sp->drop_user(source);
                return Lb_OK;
            }
            name = msg_pos;
            msg_pos += len + 1;
        }
        snprintf(netstate.users[source].name, sizeof(netstate.users[source].name), "%s", name);
        if (!isalnum(netstate.users[source].name[0])) {
            NETDBG(6, "Connected peer had bad name starting with %c", netstate.users[source].name[0]);
            netstate.sp->drop_user(source);
            return Lb_OK;
        }
        netstate.users[source].version = *(const struct GameVersionPacket *)msg_pos;
        NETMSG("User %s successfully logged in", netstate.users[source].name);
        netstate.users[source].progress = USER_LOGGEDIN;
        play_non_3d_sample(76);
        char *reply_pos = begin_message(NETMSG_LOGIN);
        *reply_pos = source;
        reply_pos += 1;
        memcpy(reply_pos, &netstate.users[SERVER_ID].version, sizeof(netstate.users[SERVER_ID].version));
        reply_pos += sizeof(netstate.users[SERVER_ID].version);
        send_buffered_message(source, reply_pos);
        for (NetUserId user_id = 0; user_id < netstate.max_players; user_id += 1) {
            if (netstate.users[user_id].progress == USER_UNUSED) {
                continue;
            }
            SendUserUpdate(source, user_id);
            if (user_id != netstate.my_id && user_id != source) {
                SendUserUpdate(user_id, source);
            }
        }
        UpdateLocalPlayerInfo(source);
        return Lb_OK;
    }
    if (message_type == NETMSG_USERUPDATE) {
        if (source != SERVER_ID) {
            WARNLOG("Unexpected USERUPDATE");
            return Lb_OK;
        }
        NetUserId user_id = (NetUserId)*msg_pos;
        if (user_id < 0 || user_id >= netstate.max_players) {
            ERRORLOG("Critical error: Out of range user ID %i received from server, could be used for buffer overflow attack", user_id);
            abort();
        }
        msg_pos += 1;
        netstate.users[user_id].progress = (enum NetUserProgress)*msg_pos;
        msg_pos += 1;
        const char* name;
        {
            size_t max_read = sizeof(netstate.msg_buffer) - (msg_pos - netstate.msg_buffer);
            size_t len = strnlen(msg_pos, max_read);
            if (len >= max_read || len > sizeof(netstate.msg_buffer)) {
                ERRORLOG("Critical error: Unterminated name in USERUPDATE");
                abort();
            }
            name = msg_pos;
            msg_pos += len + 1;
        }
        snprintf(netstate.users[user_id].name, sizeof(netstate.users[user_id].name), "%s", name);
        UpdateLocalPlayerInfo(user_id);
        return Lb_OK;
    }
    if (message_type == NETMSG_FRONTEND || message_type == NETMSG_STARTUP_SYNC || message_type == NETMSG_GAMEPLAY_UNSEQUENCED) {
        NetUserId peer_id = (NetUserId)*msg_pos;
        if (peer_id < 0 || peer_id >= netstate.max_players) {
            ERRORLOG("Critical error: Out of range peer ID %i received, could be used for buffer overflow attack", peer_id);
            abort();
        }
        if (source != SERVER_ID && source != peer_id) {
            WARNLOG("Peer %i tried to send gameplay frame for peer %i", source, peer_id);
            return Lb_OK;
        }
        char* peer_frame = ((char*)server_buf) + peer_id * frame_size;
        msg_pos += 1;
        netstate.users[peer_id].ack = *(int*)msg_pos;
        msg_pos += 4;
        if (message_type == NETMSG_GAMEPLAY_UNSEQUENCED) {
            size_t payload_size = msg_size - (msg_pos - netstate.msg_buffer);
            if (!unpack_packet_bundle(msg_pos, payload_size, (PlayerNumber)peer_id, peer_frame, frame_size)) {
                WARNLOG("Invalid gameplay packet bundle from peer %i (%u bytes)", peer_id, (unsigned)payload_size);
                return Lb_OK;
            }
        } else {
            memcpy(peer_frame, msg_pos, frame_size);
        }
        if (netstate.my_id == SERVER_ID) {
            TbBool unsequenced = (message_type == NETMSG_GAMEPLAY_UNSEQUENCED);
            send_to_active_peers(netstate.my_id, peer_id, netstate.msg_buffer, msg_size, unsequenced);
        }
        return Lb_OK;
    }
    if (message_type == NETMSG_UNPAUSE) {
        if ((game.operation_flags & GOF_Paused) == 0) {
            MULTIPLAYER_LOG("ProcessMessage NETMSG_UNPAUSE: ignoring, not paused");
            return Lb_OK;
        }
        MULTIPLAYER_LOG("ProcessMessage NETMSG_UNPAUSE: initiating timesync");
        unpausing_in_progress = 1;
        keeper_screen_redraw();
        LbScreenSwap();
        if (my_player_number == get_host_player_id()) {
            LbNetwork_BroadcastUnpauseTimesync();
        }
        LbNetwork_TimesyncBarrier();
        process_pause_packet(0, 0);
        unpausing_in_progress = 0;
        return Lb_OK;
    }
    if (message_type == NETMSG_CHATMESSAGE) {
        int player_id = (int)*msg_pos;
        msg_pos += 1;
        const char* message;
        {
            size_t max_read = sizeof(netstate.msg_buffer) - (msg_pos - netstate.msg_buffer);
            size_t len = strnlen(msg_pos, max_read);
            if (len >= max_read || len > sizeof(netstate.msg_buffer)) {
                ERRORLOG("Chat message too long or not null-terminated");
                return Lb_OK;
            }
            message = msg_pos;
            msg_pos += len + 1;
        }
        process_chat_message_end(player_id, message);
        if (netstate.my_id == SERVER_ID && source != SERVER_ID) {
            send_to_active_peers(netstate.my_id, source, netstate.msg_buffer, msg_size, false);
        }
        return Lb_OK;
    }
    if (message_type == NETMSG_GAMEPLAY_PACKET_HISTORY) {
        TbBool from_server = (source == SERVER_ID);
        size_t payload_size = msg_size - (msg_pos - netstate.msg_buffer);
        if (payload_size < sizeof(struct GameplayPacketHeader)) {
            WARNLOG("Gameplay packet history from peer %i was too small (%u bytes)", source, (unsigned)payload_size);
            return Lb_OK;
        }
        struct GameplayPacketHeader header;
        memcpy(&header, msg_pos, sizeof(header));
        if (header.player < 0 || header.player >= netstate.max_players) {
            WARNLOG("Gameplay packet history from peer %i had invalid player %d", source, (int)header.player);
            return Lb_OK;
        }
        if (!from_server && source != header.player) {
            WARNLOG("Peer %i tried to send gameplay packet history for peer %i", source, (int)header.player);
            return Lb_OK;
        }
        size_t expected_size = sizeof(struct GameplayPacketHeader) + header.compressed_length;
        if (payload_size != expected_size) {
            WARNLOG("Gameplay packet history from peer %i had size mismatch (%u != %u + %u)",
                source, (unsigned)payload_size, (unsigned)sizeof(struct GameplayPacketHeader), header.compressed_length);
            return Lb_OK;
        }
        if (header.original_length > sizeof(struct RedundantPacketBundle) || header.original_length < sizeof(unsigned char)) {
            WARNLOG("Gameplay packet history from peer %i had invalid original length %u", source, header.original_length);
            return Lb_OK;
        }

        char packet_history_buffer[sizeof(struct RedundantPacketBundle)];
        const char* compressed_buffer = msg_pos + sizeof(struct GameplayPacketHeader);
        uLongf packet_history_size = header.original_length;
        int uncompress_result = uncompress((Bytef*)packet_history_buffer, &packet_history_size, (const Bytef*)compressed_buffer, header.compressed_length);
        if (uncompress_result != Z_OK || packet_history_size != header.original_length) {
            WARNLOG("Gameplay packet history decompression from peer %i failed: zlib error %d", source, uncompress_result);
            return Lb_OK;
        }
        uLong verify_crc = crc32(0L, Z_NULL, 0);
        verify_crc = crc32(verify_crc, (const Bytef*)packet_history_buffer, packet_history_size);
        if ((unsigned int)verify_crc != header.data_checksum) {
            WARNLOG("Gameplay packet history checksum mismatch from peer %i", source);
            return Lb_OK;
        }

        char* peer_frame = ((char*)server_buf) + header.player * frame_size;
        if (!unpack_packet_bundle(packet_history_buffer, header.original_length, header.player, peer_frame, frame_size)) {
            WARNLOG("Gameplay packet history from peer %i could not be stored for player %d", source, (int)header.player);
        }
        return Lb_OK;
    }
    return Lb_OK;
}

void initialize_packet_history(void)
{
    memset(&received_history, 0, sizeof(received_history));
    memset(packet_history, 0, sizeof(packet_history));
}

const struct Packet* get_received_turn_packets(GameTurn turn)
{
    static struct Packet packets_for_turn[PACKETS_COUNT];
    TbBool found = false;
    memset(packets_for_turn, 0, sizeof(packets_for_turn));

    for (int i = 0; i < PACKET_HISTORY_SIZE * PACKETS_COUNT; i += 1) {
        const struct ReceivedPacketEntry* entry = &received_history.entries[i];
        if (entry->valid && entry->turn == turn && entry->player >= 0 && entry->player < PACKETS_COUNT) {
            packets_for_turn[entry->player] = entry->packet;
            found = true;
        }
    }

    if (!found) {
        return NULL;
    }
    return packets_for_turn;
}

static TbBool has_received_player_packet(GameTurn turn, PlayerNumber player)
{
    for (int i = 0; i < PACKET_HISTORY_SIZE * PACKETS_COUNT; i += 1) {
        const struct ReceivedPacketEntry* entry = &received_history.entries[i];
        if (entry->valid && entry->turn == turn && entry->player == player) {
            return true;
        }
    }
    return false;
}

TbBool have_received_all_packets(PlayerNumber local_packet_num)
{
    GameTurn expected_turn = get_gameturn() - game.input_lag_turns;
    for (PlayerNumber player = 0; player < NET_PLAYERS_COUNT; player += 1) {
        if (player != local_packet_num && network_player_active(player) && !has_received_player_packet(expected_turn, player)) {
            return false;
        }
    }
    return true;
}

void sync_packet_history(void)
{
    static TbClockMSec last_packet_history_send = -PROACTIVE_HISTORY_SEND_MS;

    TbClockMSec current_time = LbTimerClock();
    TbClockMSec history_send_interval = PROACTIVE_HISTORY_SEND_MS;
    NetUserId destination_peer = SERVER_ID;
    if (netstate.my_id == SERVER_ID && netstate.max_players > 1) {
        history_send_interval = PROACTIVE_HISTORY_SEND_MS / (netstate.max_players - 1);
        destination_peer = 1 + ((current_time / history_send_interval) % (netstate.max_players - 1));
    }
    if (current_time - last_packet_history_send < history_send_interval || !can_send_peer(destination_peer)) {
        return;
    }

    last_packet_history_send = current_time;
    for (PlayerNumber player = 0; player < netstate.max_players; player += 1) {
        if (player == destination_peer ||
            (netstate.my_id != SERVER_ID && player != netstate.my_id) ||
            (player != netstate.my_id && !network_player_active(player))) {
            continue;
        }
        char packet_history_buffer[sizeof(struct RedundantPacketBundle)];
        size_t packet_history_size = build_packet_bundle(player, (struct RedundantPacketBundle*)packet_history_buffer, PACKET_HISTORY_SIZE, NULL);
        if (packet_history_size == 0) {
            continue;
        }
        uLong data_crc = crc32(0L, Z_NULL, 0);
        data_crc = crc32(data_crc, (const Bytef*)packet_history_buffer, packet_history_size);
        uLongf compressed_size = compressBound(packet_history_size);
        size_t message_capacity = sizeof(unsigned char) + sizeof(struct GameplayPacketHeader) + compressed_size;
        char* message_buffer = (char*)malloc(message_capacity);
        if (message_buffer == NULL) {
            ERRORLOG("Failed to allocate gameplay packet history message buffer");
            continue;
        }
        char* compressed_buffer = message_buffer + sizeof(unsigned char) + sizeof(struct GameplayPacketHeader);
        int compress_result = compress((Bytef*)compressed_buffer, &compressed_size, (const Bytef*)packet_history_buffer, packet_history_size);
        if (compress_result != Z_OK) {
            ERRORLOG("Gameplay packet history compression failed for player %d: zlib error %d", (int)player, compress_result);
            free(message_buffer);
            continue;
        }
        struct GameplayPacketHeader header = {(PlayerNumber)player, (unsigned int)compressed_size, (unsigned int)packet_history_size, (unsigned int)data_crc};
        message_buffer[0] = NETMSG_GAMEPLAY_PACKET_HISTORY;
        memcpy(message_buffer + sizeof(unsigned char), &header, sizeof(header));
        MULTIPLAYER_LOG("Sending compressed gameplay packet history for player=%d to peer=%d (%lu -> %lu bytes)",
            (int)player, (int)destination_peer, (unsigned long)packet_history_size, (unsigned long)compressed_size);
        netstate.sp->sendmsg_single(destination_peer, message_buffer, sizeof(unsigned char) + sizeof(struct GameplayPacketHeader) + compressed_size);
        free(message_buffer);
    }
}

TbError LbNetwork_ExchangeLogin(char *player_name)
{
    NETMSG("Logging in as %s", player_name);
    if (1 + strlen(netstate.password) + 1 + strlen(player_name) + 1 + sizeof(net_current_version) >= sizeof(netstate.msg_buffer)) {
        ERRORLOG("Login credentials too long");
        return Lb_FAIL;
    }
    char *msg_pos = begin_message(NETMSG_LOGIN);
    strcpy(msg_pos, netstate.password);
    msg_pos += strlen(netstate.password) + 1;
    strcpy(msg_pos, player_name);
    msg_pos += strlen(player_name) + 1;
    memcpy(msg_pos, &net_current_version, sizeof(net_current_version));
    msg_pos += sizeof(net_current_version);
    send_buffered_message(SERVER_ID, msg_pos);
    TbClockMSec wait_start = LbTimerClock();
    while (true) {
        TbClockMSec elapsed = LbTimerClock() - wait_start;
        if (elapsed >= TIMEOUT_JOIN_LOBBY) {
            NETMSG("ExchangeLogin: timed out waiting for login response (%dms)", (int)TIMEOUT_JOIN_LOBBY);
            break;
        }
        unsigned wait_ms = (unsigned)(TIMEOUT_JOIN_LOBBY - elapsed);
        if (!netstate.sp->msgready(SERVER_ID, wait_ms)) {
            NETMSG("ExchangeLogin: msgready returned false");
            break;
        }
        if (process_message(SERVER_ID, &net_screen_packet, sizeof(struct ScreenPacket)) == Lb_FAIL) {
            NETMSG("ExchangeLogin: process_message failed");
            break;
        }
        if (netstate.msg_buffer[0] == NETMSG_LOGIN) {
            break;
        }
    }
    if (netstate.msg_buffer[0] != NETMSG_LOGIN) {
        NETMSG("ExchangeLogin: login rejected (msg_buffer[0]=%d)", (int)netstate.msg_buffer[0]);
        return Lb_FAIL;
    }
    if (netstate.sp->msgready(SERVER_ID, TIMEOUT_JOIN_LOBBY)) {
        process_message(SERVER_ID, &net_screen_packet, sizeof(struct ScreenPacket));
    }
    if (netstate.my_id == INVALID_USER_ID) {
        NETMSG("ExchangeLogin: login unsuccessful, still INVALID_USER_ID");
        return Lb_FAIL;
    }
    return Lb_OK;
}

TbError LbNetwork_ExchangeGameplay(void *send_buf, void *server_buf, size_t client_frame_size)
{
    if (exchange_frame_data(send_buf, server_buf, client_frame_size, NETMSG_GAMEPLAY_UNSEQUENCED) != Lb_OK) {
        return Lb_FAIL;
    }

    for (NetUserId peer_id = 0; peer_id < netstate.max_players; peer_id += 1) {
        if (can_send_peer(peer_id)) {
            while (netstate.sp->msgready(peer_id, 0)) {
                process_message(peer_id, server_buf, client_frame_size);
            }
        }
    }
    sync_packet_history();
    if (game.skip_initial_input_turns <= 0) {
        struct PlayerInfo* my_player = get_my_player();
        if (player_exists(my_player)) {
            PlayerNumber local_packet_player = my_player->packet_num;
            if (!have_received_all_packets(local_packet_player)) {
                GameTurn expected_turn = get_gameturn() - game.input_lag_turns;
                TbClockMSec wait_start = LbTimerClock();
                MULTIPLAYER_LOG("LbNetwork_ExchangeGameplay: Missing packets for turn=%lu, collecting...", (unsigned long)expected_turn);
                sync_packet_history();

                while (!have_received_all_packets(local_packet_player)) {
                    netstate.sp->update(OnNewUser);
                    if (netstate.my_id != SERVER_ID && netstate.users[SERVER_ID].progress == USER_UNUSED) {
                        MULTIPLAYER_LOG("LbNetwork_ExchangeGameplay: Host disconnected while waiting for turn=%lu", (unsigned long)expected_turn);
                        netstate.seq_nbr += 1;
                        return Lb_OK;
                    }
                    for (NetUserId peer_id = 0; peer_id < netstate.max_players; peer_id += 1) {
                        if (!can_send_peer(peer_id)) {
                            continue;
                        }
                        if (netstate.my_id == SERVER_ID && has_received_player_packet(expected_turn, (PlayerNumber)peer_id)) {
                            continue;
                        }
                        while (netstate.sp->msgready(peer_id, 0)) {
                            process_message(peer_id, server_buf, client_frame_size);
                        }
                        if (netstate.my_id != SERVER_ID && netstate.users[SERVER_ID].progress == USER_UNUSED) {
                            MULTIPLAYER_LOG("LbNetwork_ExchangeGameplay: Host disconnected while collecting turn=%lu", (unsigned long)expected_turn);
                            netstate.seq_nbr += 1;
                            return Lb_OK;
                        }
                        if (have_received_all_packets(local_packet_player)) {
                            int32_t elapsed = LbTimerClock() - wait_start;
                            MULTIPLAYER_LOG("LbNetwork_ExchangeGameplay: Completed wait for turn=%lu after %dms", (unsigned long)expected_turn, elapsed);
                            break;
                        }
                    }
                    if (have_received_all_packets(local_packet_player)) {
                        break;
                    }
                    if ((LbTimerClock() - wait_start) >= TIMEOUT_GAMEPLAY_MISSING_PACKET) {
                        MULTIPLAYER_LOG("LbNetwork_ExchangeGameplay: Missing packets remained for turn=%lu after collection", (unsigned long)expected_turn);
                        break;
                    }
                    sync_packet_history();
                    network_yield_waiting_gameplay_packets();
                    if (quit_game || exit_keeper) {
                        netstate.seq_nbr += 1;
                        return Lb_OK;
                    }
                }
            }
        }
    }
    netstate.seq_nbr += 1;
    return Lb_OK;
}

TbError LbNetwork_ExchangeWithWait(enum NetMessageType msg_type, void *send_buf, void *server_buf, size_t client_frame_size)
{
    if (exchange_frame_data(send_buf, server_buf, client_frame_size, msg_type) != Lb_OK) {
        return Lb_FAIL;
    }

    TbBool stop_waiting = false;
    if (msg_type == NETMSG_FRONTEND && client_frame_size == sizeof(struct ScreenPacket)) {
        const struct ScreenPacket* host_packet = &((const struct ScreenPacket*)server_buf)[get_host_player_id()];
        stop_waiting = ((host_packet->networkstatus_flags & NetStat_PlayerConnected) != 0)
            && (screen_packet_action(host_packet) == NetAct_HostStartLevel)
            && (host_packet->action_par1 > 0);
    }
    TbBool received_frames[MAX_N_USERS] = {false};
    for (NetUserId peer_id = 0; peer_id < netstate.max_players; peer_id += 1) {
        if (!can_send_peer(peer_id)) {
            continue;
        }

        TbClockMSec start = LbTimerClock();
        while (netstate.users[peer_id].progress != USER_UNUSED && !stop_waiting) {
            TbClockMSec elapsed = LbTimerClock() - start;
            if (elapsed >= TIMEOUT_LOBBY_EXCHANGE) {
                break;
            }

            unsigned wait_ms = min(TIMEOUT_LOBBY_EXCHANGE - elapsed, (TbClockMSec)16);
            if (netstate.sp->msgready(peer_id, wait_ms)) {
                while (netstate.sp->msgready(peer_id, 0)) {
                    process_message(peer_id, server_buf, client_frame_size);
                    if ((enum NetMessageType)netstate.msg_buffer[0] == msg_type) {
                        NetUserId frame_peer_id = (unsigned char)netstate.msg_buffer[1];
                        if (frame_peer_id < netstate.max_players) {
                            received_frames[frame_peer_id] = true;
                        }
                    }
                }
                if (msg_type == NETMSG_FRONTEND && client_frame_size == sizeof(struct ScreenPacket)) {
                    const struct ScreenPacket* host_packet = &((const struct ScreenPacket*)server_buf)[get_host_player_id()];
                    stop_waiting = ((host_packet->networkstatus_flags & NetStat_PlayerConnected) != 0)
                        && (screen_packet_action(host_packet) == NetAct_HostStartLevel)
                        && (host_packet->action_par1 > 0);
                }
            }

            TbBool received_frame = received_frames[peer_id];
            if (my_player_number != get_host_player_id()) {
                received_frame = true;
                for (NetUserId id = 0; id < netstate.max_players; id += 1) {
                    if (id == netstate.my_id || !IsUserActive(id)) {
                        continue;
                    }
                    if (!received_frames[id]) {
                        received_frame = false;
                        break;
                    }
                }
            }
            if (received_frame) {
                break;
            }

            if (msg_type == NETMSG_FRONTEND) {
                network_yield_draw_frontend();
            } else {
                network_yield_draw_gameplay();
            }
        }
        if (stop_waiting) {
            break;
        }
    }
    netstate.seq_nbr += 1;
    return Lb_OK;
}

void LbNetwork_SendChatMessageImmediate(int player_id, const char *message)
{
    char* msg_pos = begin_message(NETMSG_CHATMESSAGE);
    *msg_pos = player_id;
    msg_pos += 1;
    strcpy(msg_pos, message);
    msg_pos += strlen(message) + 1;
    send_to_host_or_peers(msg_pos - netstate.msg_buffer);
}

void LbNetwork_BroadcastUnpauseTimesync(void)
{
    MULTIPLAYER_LOG("LbNetwork_BroadcastUnpauseTimesync");
    begin_message(NETMSG_UNPAUSE);
    send_to_host_or_peers(1);
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
