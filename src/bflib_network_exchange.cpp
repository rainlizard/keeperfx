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

#define PACKET_HISTORY_SIZE 32
#define PROACTIVE_HISTORY_SEND_MS 2000
#define REDUNDANT_PACKET_BUNDLE 2

struct ReceivedPacketEntry {
    GameTurn turn;
    PlayerNumber player;
    TbBool valid;
    struct Packet packet;
};

struct PlayerPacketHistory {
    struct Packet packets[PACKET_HISTORY_SIZE];
    unsigned char next_index;
    unsigned char valid_count;
};

#pragma pack(1)
struct RedundantPacketBundle {
    unsigned char valid_count;
    struct Packet packets[PACKET_HISTORY_SIZE];
};

struct PacketHistoryHeader {
    PlayerNumber player;
    unsigned int compressed_length;
    unsigned int original_length;
    unsigned int data_checksum;
};
#pragma pack()

static struct {
    struct ReceivedPacketEntry entries[PACKET_HISTORY_SIZE * PACKETS_COUNT];
    int next_index;
} received_packet_history;

static struct PlayerPacketHistory player_packet_history[MAX_N_USERS];

static TbBool has_received_player_packet(GameTurn turn, PlayerNumber player);

static TbBool is_history_player_valid(PlayerNumber player)
{
    return player >= 0 && player < MAX_N_USERS;
}

static size_t packet_bundle_size(unsigned char valid_count)
{
    return sizeof(unsigned char) + valid_count * sizeof(struct Packet);
}

static TbBool read_msg_text(char** read_pos, const char** text, size_t max_len)
{
    size_t max_read = sizeof(netstate.msg_buffer) - (*read_pos - netstate.msg_buffer);
    size_t len = strnlen(*read_pos, max_read);
    if (len >= max_read || len > max_len) {
        return false;
    }
    *text = *read_pos;
    *read_pos += len + 1;
    return true;
}

static TbBool host_started_level(const void* server_buf, size_t frame_size)
{
    if (frame_size != sizeof(struct ScreenPacket)) {
        return false;
    }
    const struct ScreenPacket* host_packet = &((const struct ScreenPacket*)server_buf)[get_host_player_id()];
    return ((host_packet->networkstatus_flags & NetStat_PlayerConnected) != 0)
        && (screen_packet_action(host_packet) == NetAct_HostStartLevel)
        && (host_packet->action_par1 > 0);
}

static void store_packet_history(PlayerNumber player, const struct Packet* packet)
{
    if (!is_history_player_valid(player)) {
        return;
    }
    struct PlayerPacketHistory* history = &player_packet_history[player];
    for (unsigned char i = 0; i < history->valid_count; i += 1) {
        if (history->packets[i].turn == packet->turn) {
            history->packets[i] = *packet;
            return;
        }
    }
    history->packets[history->next_index] = *packet;
    history->next_index = (history->next_index + 1) % PACKET_HISTORY_SIZE;
    if (history->valid_count < PACKET_HISTORY_SIZE) {
        history->valid_count += 1;
    }
}

static size_t build_history_bundle(PlayerNumber player, struct RedundantPacketBundle* packet_bundle, unsigned char packet_count, const struct Packet* first_packet)
{
    const struct PlayerPacketHistory* history;
    packet_bundle->valid_count = 0;
    if (!is_history_player_valid(player) || packet_count < 1) {
        return 0;
    }
    history = &player_packet_history[player];
    if (first_packet != NULL) {
        packet_bundle->packets[0] = *first_packet;
        packet_bundle->valid_count = 1;
    }
    unsigned char copied_count = 0;
    unsigned char remaining_packets = packet_count - packet_bundle->valid_count;
    for (unsigned char i = 0; i < history->valid_count && copied_count < remaining_packets; i += 1) {
        int32_t history_index = (history->next_index + PACKET_HISTORY_SIZE - i - 1) % PACKET_HISTORY_SIZE;
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
    return packet_bundle_size(packet_bundle->valid_count);
}

static TbBool unpack_history_bundle(const char* packet_bundle_buffer, size_t packet_bundle_buffer_size, PlayerNumber source_player, void* out_packet, size_t packet_size)
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
    if (packet_bundle_buffer_size < packet_bundle_size(packet_bundle->valid_count)) {
        return false;
    }
    for (unsigned char i = 0; i < packet_bundle->valid_count; i += 1) {
        const struct Packet* packet = &packet_bundle->packets[i];
        store_packet_history(source_player, packet);
        if (!has_received_player_packet(packet->turn, source_player)) {
            if (is_packet_empty(packet)) {
                MULTIPLAYER_LOG("unpack_history_bundle: Skipping empty packet for player %d turn %lu", source_player, (unsigned long)packet->turn);
                continue;
            }
            int entry_index = received_packet_history.next_index;
            struct ReceivedPacketEntry* entry = &received_packet_history.entries[entry_index];
            entry->turn = packet->turn;
            entry->player = source_player;
            entry->valid = true;
            memcpy(&entry->packet, packet, sizeof(struct Packet));
            received_packet_history.next_index = (entry_index + 1) % (PACKET_HISTORY_SIZE * PACKETS_COUNT);
        }
    }
    memcpy(out_packet, &packet_bundle->packets[0], packet_size);
    return true;
}

static TbError exchange_frame_message(void *send_buf, void *server_buf, size_t client_frame_size, enum NetMessageType msg_type)
{
    if (netstate.my_id < 0 || netstate.my_id >= netstate.max_players) {
        ERRORLOG("Invalid my_id %i in network exchange (disconnected?)", netstate.my_id);
        return Lb_FAIL;
    }
    netstate.sp->update(OnNewUser);
    memcpy(((char*)server_buf) + netstate.my_id * client_frame_size, send_buf, client_frame_size);
    char* write_pos = begin_net_message(msg_type);
    *write_pos = netstate.my_id;
    write_pos += 1;
    *(int*)write_pos = netstate.seq_nbr;
    write_pos += 4;
    if (msg_type == NETMSG_GAMEPLAY_UNSEQUENCED) {
        struct RedundantPacketBundle* packet_bundle = (struct RedundantPacketBundle*)write_pos;
        size_t bundle_size = build_history_bundle((PlayerNumber)netstate.my_id, packet_bundle, REDUNDANT_PACKET_BUNDLE, (const struct Packet*)send_buf);
        if (bundle_size > 0) {
            write_pos += bundle_size;
        }
    } else {
        memcpy(write_pos, send_buf, client_frame_size);
        write_pos += client_frame_size;
    }
    size_t message_size = write_pos - netstate.msg_buffer;
    TbBool unsequenced = (msg_type == NETMSG_GAMEPLAY_UNSEQUENCED);
    for (NetUserId peer_id = 0; peer_id < netstate.max_players; peer_id += 1) {
        if (peer_id == netstate.my_id) {
            continue;
        }
        if (!can_send_to_peer(peer_id)) {
            continue;
        }
        send_network_message(peer_id, netstate.msg_buffer, message_size, unsequenced);
    }
    return Lb_OK;
}

static TbError process_network_message(NetUserId source, void* server_buf, size_t frame_size)
{
    size_t message_size = netstate.sp->readmsg(source, netstate.msg_buffer, sizeof(netstate.msg_buffer));
    if (message_size <= 0) {
        ERRORLOG("Problem reading message from %u", source);
        return Lb_FAIL;
    }
    char *read_pos = netstate.msg_buffer;
    enum NetMessageType message_type = (enum NetMessageType)*read_pos;
    read_pos += 1;
    if (message_type == NETMSG_LOGIN) {
        if (source == SERVER_ID) {
            netstate.my_id = (NetUserId)*read_pos;
            read_pos += 1;
            netstate.users[netstate.my_id].version = net_current_version;
            netstate.users[SERVER_ID].version = *(const struct GameVersionPacket *)read_pos;
            return Lb_OK;
        }
        if (netstate.users[source].progress != USER_CONNECTED) {
            NETMSG("Peer was not in connected state");
            return Lb_OK;
        }
        const char* password;
        if (!read_msg_text(&read_pos, &password, sizeof(netstate.password))) {
            NETDBG(6, "Connected peer sent invalid password");
            netstate.sp->drop_user(source);
            return Lb_OK;
        }
        if (netstate.password[0] != 0 && strncmp(password, netstate.password, sizeof(netstate.password)) != 0) {
            NETMSG("Peer chose wrong password");
            return Lb_OK;
        }
        const char* name;
        if (!read_msg_text(&read_pos, &name, sizeof(netstate.users[source].name) - 1) || name[0] == '\0') {
            NETDBG(6, "Connected peer sent invalid name");
            netstate.sp->drop_user(source);
            return Lb_OK;
        }
        snprintf(netstate.users[source].name, sizeof(netstate.users[source].name), "%s", name);
        if (!isalnum(netstate.users[source].name[0])) {
            NETDBG(6, "Connected peer had bad name starting with %c", netstate.users[source].name[0]);
            netstate.sp->drop_user(source);
            return Lb_OK;
        }
        netstate.users[source].version = *(const struct GameVersionPacket *)read_pos;
        NETMSG("User %s successfully logged in", netstate.users[source].name);
        netstate.users[source].progress = USER_LOGGEDIN;
        play_non_3d_sample(76);
        char *reply_pos = begin_net_message(NETMSG_LOGIN);
        *reply_pos = source;
        reply_pos += 1;
        memcpy(reply_pos, &netstate.users[SERVER_ID].version, sizeof(netstate.users[SERVER_ID].version));
        reply_pos += sizeof(netstate.users[SERVER_ID].version);
        send_message_buffer(source, reply_pos);
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
        NetUserId user_id = (NetUserId)*read_pos;
        if (user_id < 0 || user_id >= netstate.max_players) {
            ERRORLOG("Critical error: Out of range user ID %i received from server, could be used for buffer overflow attack", user_id);
            abort();
        }
        read_pos += 1;
        netstate.users[user_id].progress = (enum NetUserProgress)*read_pos;
        read_pos += 1;
        const char* name;
        if (!read_msg_text(&read_pos, &name, sizeof(netstate.users[user_id].name) - 1)) {
            ERRORLOG("Critical error: Unterminated name in USERUPDATE");
            abort();
        }
        snprintf(netstate.users[user_id].name, sizeof(netstate.users[user_id].name), "%s", name);
        UpdateLocalPlayerInfo(user_id);
        return Lb_OK;
    }
    if (message_type == NETMSG_FRONTEND || message_type == NETMSG_STARTUP_SYNC || message_type == NETMSG_GAMEPLAY_UNSEQUENCED) {
        NetUserId peer_id = (NetUserId)*read_pos;
        if (peer_id < 0 || peer_id >= netstate.max_players) {
            ERRORLOG("Critical error: Out of range peer ID %i received, could be used for buffer overflow attack", peer_id);
            abort();
        }
        if (source != SERVER_ID && source != peer_id) {
            WARNLOG("Peer %i tried to send gameplay frame for peer %i", source, peer_id);
            return Lb_OK;
        }
        char* player_frame = ((char*)server_buf) + peer_id * frame_size;
        read_pos += 1;
        netstate.users[peer_id].ack = *(int*)read_pos;
        read_pos += 4;
        if (message_type == NETMSG_GAMEPLAY_UNSEQUENCED) {
            size_t payload_size = message_size - (read_pos - netstate.msg_buffer);
            if (!unpack_history_bundle(read_pos, payload_size, (PlayerNumber)peer_id, player_frame, frame_size)) {
                WARNLOG("Invalid gameplay packet bundle from peer %i (%u bytes)", peer_id, (unsigned)payload_size);
                return Lb_OK;
            }
        } else {
            memcpy(player_frame, read_pos, frame_size);
        }
        if (netstate.my_id == SERVER_ID) {
            TbBool unsequenced = (message_type == NETMSG_GAMEPLAY_UNSEQUENCED);
            send_to_active_peers(netstate.my_id, peer_id, netstate.msg_buffer, message_size, unsequenced);
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
        int player_id = (int)*read_pos;
        read_pos += 1;
        const char* message;
        if (!read_msg_text(&read_pos, &message, sizeof(netstate.msg_buffer) - 1)) {
            ERRORLOG("Chat message too long or not null-terminated");
            return Lb_OK;
        }
        process_chat_message_end(player_id, message);
        if (netstate.my_id == SERVER_ID && source != SERVER_ID) {
            send_to_active_peers(netstate.my_id, source, netstate.msg_buffer, message_size, false);
        }
        return Lb_OK;
    }
    if (message_type == NETMSG_GAMEPLAY_PACKET_HISTORY) {
        TbBool from_server = (source == SERVER_ID);
        size_t payload_size = message_size - (read_pos - netstate.msg_buffer);
        if (payload_size < sizeof(struct PacketHistoryHeader)) {
            WARNLOG("Gameplay packet history from peer %i was too small (%u bytes)", source, (unsigned)payload_size);
            return Lb_OK;
        }
        struct PacketHistoryHeader header;
        memcpy(&header, read_pos, sizeof(header));
        if (header.player < 0 || header.player >= netstate.max_players) {
            WARNLOG("Gameplay packet history from peer %i had invalid player %d", source, (int)header.player);
            return Lb_OK;
        }
        if (!from_server && source != header.player) {
            WARNLOG("Peer %i tried to send gameplay packet history for peer %i", source, (int)header.player);
            return Lb_OK;
        }
        size_t expected_size = sizeof(struct PacketHistoryHeader) + header.compressed_length;
        if (payload_size != expected_size) {
            WARNLOG("Gameplay packet history from peer %i had size mismatch (%u != %u + %u)",
                source, (unsigned)payload_size, (unsigned)sizeof(struct PacketHistoryHeader), header.compressed_length);
            return Lb_OK;
        }
        if (header.original_length > sizeof(struct RedundantPacketBundle) || header.original_length < sizeof(unsigned char)) {
            WARNLOG("Gameplay packet history from peer %i had invalid original length %u", source, header.original_length);
            return Lb_OK;
        }

        char packet_history_buffer[sizeof(struct RedundantPacketBundle)];
        const char* compressed_buffer = read_pos + sizeof(struct PacketHistoryHeader);
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

        char* player_frame = ((char*)server_buf) + header.player * frame_size;
        if (!unpack_history_bundle(packet_history_buffer, header.original_length, header.player, player_frame, frame_size)) {
            WARNLOG("Gameplay packet history from peer %i could not be stored for player %d", source, (int)header.player);
        }
        return Lb_OK;
    }
    return Lb_OK;
}

static void process_peer_msgs(NetUserId peer_id, void* server_buf, size_t frame_size)
{
    while (netstate.sp->msgready(peer_id, 0)) {
        process_network_message(peer_id, server_buf, frame_size);
    }
}

static void process_wait_msgs(NetUserId peer_id, void* server_buf, size_t frame_size, enum NetMessageType msg_type, TbBool* has_received_frame)
{
    while (netstate.sp->msgready(peer_id, 0)) {
        process_network_message(peer_id, server_buf, frame_size);
        if ((enum NetMessageType)netstate.msg_buffer[0] != msg_type) {
            continue;
        }
        NetUserId frame_peer_id = (unsigned char)netstate.msg_buffer[1];
        if (frame_peer_id < netstate.max_players) {
            has_received_frame[frame_peer_id] = true;
        }
    }
}

static TbBool have_peer_frame(NetUserId peer_id, const TbBool* has_received_frame)
{
    if (my_player_number == get_host_player_id()) {
        return has_received_frame[peer_id];
    }
    for (NetUserId id = 0; id < netstate.max_players; id += 1) {
        if (id == netstate.my_id || !IsUserActive(id)) {
            continue;
        }
        if (!has_received_frame[id]) {
            return false;
        }
    }
    return true;
}

void initialize_packet_history(void)
{
    memset(&received_packet_history, 0, sizeof(received_packet_history));
    memset(player_packet_history, 0, sizeof(player_packet_history));
}

const struct Packet* get_received_turn_packets(GameTurn turn)
{
    static struct Packet packets_for_turn[PACKETS_COUNT];
    TbBool found = false;
    memset(packets_for_turn, 0, sizeof(packets_for_turn));

    for (int i = 0; i < PACKET_HISTORY_SIZE * PACKETS_COUNT; i += 1) {
        const struct ReceivedPacketEntry* entry = &received_packet_history.entries[i];
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
        const struct ReceivedPacketEntry* entry = &received_packet_history.entries[i];
        if (entry->valid && entry->turn == turn && entry->player == player) {
            return true;
        }
    }
    return false;
}

static TbBool have_all_turn_packets(PlayerNumber local_packet_player)
{
    GameTurn expected_turn = get_gameturn() - game.input_lag_turns;
    for (PlayerNumber player = 0; player < NET_PLAYERS_COUNT; player += 1) {
        if (player != local_packet_player && network_player_active(player) && !has_received_player_packet(expected_turn, player)) {
            return false;
        }
    }
    return true;
}

static void send_packet_history(void)
{
    static TbClockMSec last_packet_history_send = -PROACTIVE_HISTORY_SEND_MS;

    TbClockMSec current_time = LbTimerClock();
    TbClockMSec send_interval = PROACTIVE_HISTORY_SEND_MS;
    NetUserId target_peer = SERVER_ID;
    if (netstate.my_id == SERVER_ID && netstate.max_players > 1) {
        send_interval = PROACTIVE_HISTORY_SEND_MS / (netstate.max_players - 1);
        target_peer = 1 + ((current_time / send_interval) % (netstate.max_players - 1));
    }
    if (current_time - last_packet_history_send < send_interval || !can_send_to_peer(target_peer)) {
        return;
    }

    last_packet_history_send = current_time;
    for (PlayerNumber player = 0; player < netstate.max_players; player += 1) {
        if (player == target_peer ||
            (netstate.my_id != SERVER_ID && player != netstate.my_id) ||
            (player != netstate.my_id && !network_player_active(player))) {
            continue;
        }
        char packet_history_buffer[sizeof(struct RedundantPacketBundle)];
        size_t packet_history_size = build_history_bundle(player, (struct RedundantPacketBundle*)packet_history_buffer, PACKET_HISTORY_SIZE, NULL);
        if (packet_history_size == 0) {
            continue;
        }
        uLong data_crc = crc32(0L, Z_NULL, 0);
        data_crc = crc32(data_crc, (const Bytef*)packet_history_buffer, packet_history_size);
        uLongf compressed_size = compressBound(packet_history_size);
        size_t message_buffer_size = sizeof(unsigned char) + sizeof(struct PacketHistoryHeader) + compressed_size;
        char* message_buffer = (char*)malloc(message_buffer_size);
        if (message_buffer == NULL) {
            ERRORLOG("Failed to allocate gameplay packet history message buffer");
            continue;
        }
        char* compressed_buffer = message_buffer + sizeof(unsigned char) + sizeof(struct PacketHistoryHeader);
        int compress_result = compress((Bytef*)compressed_buffer, &compressed_size, (const Bytef*)packet_history_buffer, packet_history_size);
        if (compress_result != Z_OK) {
            ERRORLOG("Gameplay packet history compression failed for player %d: zlib error %d", (int)player, compress_result);
            free(message_buffer);
            continue;
        }
        struct PacketHistoryHeader header = {(PlayerNumber)player, (unsigned int)compressed_size, (unsigned int)packet_history_size, (unsigned int)data_crc};
        message_buffer[0] = NETMSG_GAMEPLAY_PACKET_HISTORY;
        memcpy(message_buffer + sizeof(unsigned char), &header, sizeof(header));
        MULTIPLAYER_LOG("Sending compressed gameplay packet history for player=%d to peer=%d (%lu -> %lu bytes)",
            (int)player, (int)target_peer, (unsigned long)packet_history_size, (unsigned long)compressed_size);
        netstate.sp->sendmsg_single(target_peer, message_buffer, sizeof(unsigned char) + sizeof(struct PacketHistoryHeader) + compressed_size);
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
    char *write_pos = begin_net_message(NETMSG_LOGIN);
    strcpy(write_pos, netstate.password);
    write_pos += strlen(netstate.password) + 1;
    strcpy(write_pos, player_name);
    write_pos += strlen(player_name) + 1;
    memcpy(write_pos, &net_current_version, sizeof(net_current_version));
    write_pos += sizeof(net_current_version);
    send_message_buffer(SERVER_ID, write_pos);
    TbClockMSec wait_start_time = LbTimerClock();
    while (true) {
        TbClockMSec elapsed = LbTimerClock() - wait_start_time;
        if (elapsed >= TIMEOUT_JOIN_LOBBY) {
            NETMSG("ExchangeLogin: timed out waiting for login response (%dms)", (int)TIMEOUT_JOIN_LOBBY);
            break;
        }
        unsigned wait_ms = (unsigned)(TIMEOUT_JOIN_LOBBY - elapsed);
        if (!netstate.sp->msgready(SERVER_ID, wait_ms)) {
            NETMSG("ExchangeLogin: msgready returned false");
            break;
        }
        if (process_network_message(SERVER_ID, &net_screen_packet, sizeof(struct ScreenPacket)) == Lb_FAIL) {
            NETMSG("ExchangeLogin: process_network_message failed");
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
        process_network_message(SERVER_ID, &net_screen_packet, sizeof(struct ScreenPacket));
    }
    if (netstate.my_id == INVALID_USER_ID) {
        NETMSG("ExchangeLogin: login unsuccessful, still INVALID_USER_ID");
        return Lb_FAIL;
    }
    return Lb_OK;
}

TbError LbNetwork_ExchangeGameplay(void *send_buf, void *server_buf, size_t client_frame_size)
{
    if (exchange_frame_message(send_buf, server_buf, client_frame_size, NETMSG_GAMEPLAY_UNSEQUENCED) != Lb_OK) {
        return Lb_FAIL;
    }

    for (NetUserId peer_id = 0; peer_id < netstate.max_players; peer_id += 1) {
        if (can_send_to_peer(peer_id)) {
            process_peer_msgs(peer_id, server_buf, client_frame_size);
        }
    }
    send_packet_history();
    if (game.skip_initial_input_turns <= 0) {
        struct PlayerInfo* my_player = get_my_player();
        if (player_exists(my_player)) {
            PlayerNumber local_packet_player = my_player->packet_num;
            if (!have_all_turn_packets(local_packet_player)) {
                GameTurn expected_turn = get_gameturn() - game.input_lag_turns;
                TbClockMSec wait_start_time = LbTimerClock();
                MULTIPLAYER_LOG("LbNetwork_ExchangeGameplay: Missing packets for turn=%lu, collecting...", (unsigned long)expected_turn);
                send_packet_history();

                while (!have_all_turn_packets(local_packet_player)) {
                    netstate.sp->update(OnNewUser);
                    if (netstate.my_id != SERVER_ID && netstate.users[SERVER_ID].progress == USER_UNUSED) {
                        MULTIPLAYER_LOG("LbNetwork_ExchangeGameplay: Host disconnected while waiting for turn=%lu", (unsigned long)expected_turn);
                        netstate.seq_nbr += 1;
                        return Lb_OK;
                    }
                    for (NetUserId peer_id = 0; peer_id < netstate.max_players; peer_id += 1) {
                        if (!can_send_to_peer(peer_id)) {
                            continue;
                        }
                        if (netstate.my_id == SERVER_ID && has_received_player_packet(expected_turn, (PlayerNumber)peer_id)) {
                            continue;
                        }
                        process_peer_msgs(peer_id, server_buf, client_frame_size);
                        if (netstate.my_id != SERVER_ID && netstate.users[SERVER_ID].progress == USER_UNUSED) {
                            MULTIPLAYER_LOG("LbNetwork_ExchangeGameplay: Host disconnected while collecting turn=%lu", (unsigned long)expected_turn);
                            netstate.seq_nbr += 1;
                            return Lb_OK;
                        }
                        if (have_all_turn_packets(local_packet_player)) {
                            int32_t elapsed = LbTimerClock() - wait_start_time;
                            MULTIPLAYER_LOG("LbNetwork_ExchangeGameplay: Completed wait for turn=%lu after %dms", (unsigned long)expected_turn, elapsed);
                            break;
                        }
                    }
                    if (have_all_turn_packets(local_packet_player)) {
                        break;
                    }
                    if ((LbTimerClock() - wait_start_time) >= TIMEOUT_GAMEPLAY_MISSING_PACKET) {
                        MULTIPLAYER_LOG("LbNetwork_ExchangeGameplay: Missing packets remained for turn=%lu after collection", (unsigned long)expected_turn);
                        break;
                    }
                    send_packet_history();
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
    if (exchange_frame_message(send_buf, server_buf, client_frame_size, msg_type) != Lb_OK) {
        return Lb_FAIL;
    }

    TbBool should_stop_waiting = false;
    if (msg_type == NETMSG_FRONTEND) {
        should_stop_waiting = host_started_level(server_buf, client_frame_size);
    }
    TbBool has_received_frame[MAX_N_USERS] = {false};
    for (NetUserId peer_id = 0; peer_id < netstate.max_players; peer_id += 1) {
        if (!can_send_to_peer(peer_id)) {
            continue;
        }

        TbClockMSec wait_start_time = LbTimerClock();
        while (netstate.users[peer_id].progress != USER_UNUSED && !should_stop_waiting) {
            TbClockMSec elapsed = LbTimerClock() - wait_start_time;
            if (elapsed >= TIMEOUT_LOBBY_EXCHANGE) {
                break;
            }

            unsigned wait_ms = min(TIMEOUT_LOBBY_EXCHANGE - elapsed, (TbClockMSec)16);
            if (netstate.sp->msgready(peer_id, wait_ms)) {
                process_wait_msgs(peer_id, server_buf, client_frame_size, msg_type, has_received_frame);
                if (msg_type == NETMSG_FRONTEND) {
                    should_stop_waiting = host_started_level(server_buf, client_frame_size);
                }
            }

            if (have_peer_frame(peer_id, has_received_frame)) {
                break;
            }

            if (msg_type == NETMSG_FRONTEND) {
                network_yield_draw_frontend();
            } else {
                network_yield_draw_gameplay();
            }
        }
        if (should_stop_waiting) {
            break;
        }
    }
    netstate.seq_nbr += 1;
    return Lb_OK;
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
