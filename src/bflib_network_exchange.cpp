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

static TbBool have_received_player_packet(GameTurn turn, PlayerNumber player);
static TbBool receive_packet_history(NetUserId source, TbBool from_server, const char* buffer, size_t payload_size, void* server_buf, size_t frame_size);

char* InitMessageBuffer(enum NetMessageType msg_type)
{
    char* ptr = netstate.msg_buffer;
    *ptr = msg_type;
    return ptr + 1;
}

// Clients send directly only to the host; the host relays frames to everyone else.
static TbBool can_send_peer(NetUserId peer_id)
{
    return (peer_id != netstate.my_id) &&
        (netstate.users[peer_id].progress != USER_UNUSED) &&
        (my_player_number == get_host_player_id() || peer_id == SERVER_ID);
}

static TbBool valid_history_player(PlayerNumber player)
{
    return player >= 0 && player < MAX_N_USERS;
}

static size_t packet_list_size(unsigned char valid_count)
{
    return sizeof(unsigned char) + valid_count * sizeof(struct Packet);
}

static TbBool read_message_string(char** ptr, size_t max_len, TbBool allow_empty, const char** text)
{
    size_t max_read = sizeof(netstate.msg_buffer) - (*ptr - netstate.msg_buffer);
    size_t len = strnlen(*ptr, max_read);
    if (len >= max_read || len > max_len || (!allow_empty && len == 0)) {
        return false;
    }
    *text = *ptr;
    *ptr += len + 1;
    return true;
}

static void store_received_packet(GameTurn turn, PlayerNumber player, const struct Packet* packet)
{
    if (is_packet_empty(packet)) {
        MULTIPLAYER_LOG("store_received_packet: Skipping empty packet for player %d turn %lu", player, (unsigned long)turn);
        return;
    }
    int index = received_history.head;
    struct ReceivedPacketEntry* entry = &received_history.entries[index];
    entry->turn = turn;
    entry->player = player;
    entry->valid = true;
    memcpy(&entry->packet, packet, sizeof(struct Packet));
    received_history.head = (index + 1) % (PACKET_HISTORY_SIZE * PACKETS_COUNT);
}

static void store_packet_history(PlayerNumber player, const struct Packet* packet)
{
    if (!valid_history_player(player)) {
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

static unsigned char copy_packet_history(struct Packet* out_packets, const struct PacketHistory* history, unsigned char packet_count, const struct Packet* skipped_packet)
{
    unsigned char copied_count = 0;
    for (unsigned char i = 0; i < history->valid_count && copied_count < packet_count; i += 1) {
        int32_t history_index = (history->write_index + PACKET_HISTORY_SIZE - i - 1) % PACKET_HISTORY_SIZE;
        if (skipped_packet != NULL && history->packets[history_index].turn == skipped_packet->turn) {
            continue;
        }
        out_packets[copied_count] = history->packets[history_index];
        copied_count += 1;
    }
    return copied_count;
}

static size_t make_packet_bundle(PlayerNumber player, struct RedundantPacketBundle* packet_bundle, unsigned char packet_count, const struct Packet* first_packet)
{
    unsigned char valid_count = 0;
    const struct PacketHistory* history;
    if (!valid_history_player(player) || packet_count < 1) {
        return 0;
    }

    history = &packet_history[player];
    if (first_packet == NULL && history->valid_count < 1) {
        return 0;
    }
    if (first_packet != NULL) {
        packet_bundle->packets[0] = *first_packet;
        valid_count = 1;
    }
    valid_count += copy_packet_history(&packet_bundle->packets[valid_count], history, packet_count - valid_count, first_packet);
    packet_bundle->valid_count = valid_count;
    if (first_packet != NULL) {
        store_packet_history(player, first_packet);
    }
    if (valid_count < 1) {
        return 0;
    }
    return packet_list_size(valid_count);
}

static TbBool read_packet_bundle(const char* packet_bundle_buffer, size_t packet_bundle_buffer_size, PlayerNumber source_player, void* out_packet, size_t packet_size)
{
    if (packet_size != sizeof(struct Packet)) {
        WARNLOG("Gameplay packet bundle size mismatch (%u != %u)", (unsigned)packet_size, (unsigned)sizeof(struct Packet));
        return false;
    }
    if (!valid_history_player(source_player) || packet_bundle_buffer_size < sizeof(unsigned char)) {
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
        if (!have_received_player_packet(packet->turn, source_player)) {
            store_received_packet(packet->turn, source_player, packet);
        }
    }
    memcpy(out_packet, &packet_bundle->packets[0], packet_size);
    return true;
}

static void send_packet_history(NetUserId destination, PlayerNumber player)
{
    char packet_history_buffer[sizeof(struct RedundantPacketBundle)];
    struct RedundantPacketBundle* packet_bundle = (struct RedundantPacketBundle*)packet_history_buffer;
    size_t packet_history_size = make_packet_bundle(player, packet_bundle, PACKET_HISTORY_SIZE, NULL);
    if (packet_history_size == 0) {
        return;
    }

    uLong data_crc = crc32(0L, Z_NULL, 0);
    data_crc = crc32(data_crc, (const Bytef*)packet_history_buffer, packet_history_size);

    uLongf compressed_size = compressBound(packet_history_size);
    size_t message_capacity = sizeof(unsigned char) + sizeof(struct GameplayPacketHeader) + compressed_size;
    char* message_buffer = (char*)malloc(message_capacity);
    if (message_buffer == NULL) {
        ERRORLOG("Failed to allocate gameplay packet history message buffer");
        return;
    }

    char* compressed_buffer = message_buffer + sizeof(unsigned char) + sizeof(struct GameplayPacketHeader);
    int compress_result = compress((Bytef*)compressed_buffer, &compressed_size, (const Bytef*)packet_history_buffer, packet_history_size);
    if (compress_result != Z_OK) {
        ERRORLOG("Gameplay packet history compression failed for player %d: zlib error %d", (int)player, compress_result);
        free(message_buffer);
        return;
    }

    struct GameplayPacketHeader header;
    header.player = player;
    header.compressed_length = compressed_size;
    header.original_length = packet_history_size;
    header.data_checksum = (unsigned int)data_crc;

    size_t message_size = sizeof(unsigned char) + sizeof(struct GameplayPacketHeader) + compressed_size;
    message_buffer[0] = NETMSG_GAMEPLAY_PACKET_HISTORY;
    memcpy(message_buffer + sizeof(unsigned char), &header, sizeof(header));
    MULTIPLAYER_LOG("Sending compressed gameplay packet history for player=%d to peer=%d (%lu -> %lu bytes)",
        (int)player, (int)destination, (unsigned long)packet_history_size, (unsigned long)compressed_size);
    netstate.sp->sendmsg_single(destination, message_buffer, message_size);

    free(message_buffer);
}

void SendMessage(NetUserId dest, const char* end_ptr)
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

static void send_or_broadcast(size_t msg_size)
{
    if (netstate.my_id != SERVER_ID) {
        if (IsUserActive(SERVER_ID)) {
            netstate.sp->sendmsg_single(SERVER_ID, netstate.msg_buffer, msg_size);
        }
        return;
    }
    send_to_active_peers(netstate.my_id, INVALID_USER_ID, netstate.msg_buffer, msg_size, false);
}

void SendFrameToPeers(NetUserId source_id, const void* send_buf, size_t buf_size, int seq_nbr, enum NetMessageType msg_type)
{
    char* ptr = InitMessageBuffer(msg_type);
    *ptr = source_id;
    ptr += 1;
    *(int*)ptr = seq_nbr;
    ptr += 4;
    if (msg_type == NETMSG_GAMEPLAY_UNSEQUENCED) {
        struct RedundantPacketBundle* packet_bundle = (struct RedundantPacketBundle*)ptr;
        size_t packet_bundle_size = make_packet_bundle((PlayerNumber)source_id, packet_bundle, REDUNDANT_PACKET_BUNDLE, (const struct Packet*)send_buf);
        if (packet_bundle_size > 0) {
            ptr += packet_bundle_size;
        }
    } else {
        memcpy(ptr, send_buf, buf_size);
        ptr += buf_size;
    }

    size_t msg_size = ptr - netstate.msg_buffer;
    TbBool unsequenced = (msg_type == NETMSG_GAMEPLAY_UNSEQUENCED);
    for (NetUserId id = 0; id < netstate.max_players; id += 1) {
        if (id == source_id) { continue; }
        if (!can_send_peer(id)) { continue; }
        send_network_message(id, netstate.msg_buffer, msg_size, unsequenced);
    }
}

static TbError send_exchange_data(void *send_buf, void *server_buf, size_t client_frame_size, enum NetMessageType msg_type)
{
    if (netstate.my_id < 0 || netstate.my_id >= netstate.max_players) {
        ERRORLOG("Invalid my_id %i in network exchange (disconnected?)", netstate.my_id);
        return Lb_FAIL;
    }
    netstate.sp->update(OnNewUser);
    memcpy(((char*)server_buf) + netstate.my_id * client_frame_size, send_buf, client_frame_size);
    SendFrameToPeers(netstate.my_id, send_buf, client_frame_size, netstate.seq_nbr, msg_type);
    return Lb_OK;
}

TbError ProcessMessage(NetUserId source, void* server_buf, size_t frame_size)
{
    size_t msg_size = netstate.sp->readmsg(source, netstate.msg_buffer, sizeof(netstate.msg_buffer));
    if (msg_size <= 0) {
        ERRORLOG("Problem reading message from %u", source);
        return Lb_FAIL;
    }
    char *ptr = netstate.msg_buffer;
    enum NetMessageType type = (enum NetMessageType)*ptr;
    ptr += 1;
    if (type == NETMSG_LOGIN) {
        if (source == SERVER_ID) {
            netstate.my_id = (NetUserId)*ptr;
            ptr += 1;
            netstate.users[netstate.my_id].version = net_current_version;
            netstate.users[SERVER_ID].version = *(const struct GameVersionPacket *)ptr;
            return Lb_OK;
        }
        if (netstate.users[source].progress != USER_CONNECTED) {
            NETMSG("Peer was not in connected state");
            return Lb_OK;
        }
        const char* password;
        if (!read_message_string(&ptr, sizeof(netstate.password), true, &password)) {
            NETDBG(6, "Connected peer sent invalid password");
            netstate.sp->drop_user(source);
            return Lb_OK;
        }
        if (netstate.password[0] != 0 && strncmp(password, netstate.password, sizeof(netstate.password)) != 0) {
            NETMSG("Peer chose wrong password");
            return Lb_OK;
        }
        const char* name;
        if (!read_message_string(&ptr, sizeof(netstate.users[source].name) - 1, false, &name)) {
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
        netstate.users[source].version = *(const struct GameVersionPacket *)ptr;
        NETMSG("User %s successfully logged in", netstate.users[source].name);
        netstate.users[source].progress = USER_LOGGEDIN;
        play_non_3d_sample(76);
        char * msg_ptr = InitMessageBuffer(NETMSG_LOGIN);
        *msg_ptr = source;
        msg_ptr += 1;
        memcpy(msg_ptr, &netstate.users[SERVER_ID].version, sizeof(netstate.users[SERVER_ID].version));
        msg_ptr += sizeof(netstate.users[SERVER_ID].version);
        SendMessage(source, msg_ptr);
        NetUserId uid;
        for (uid = 0; uid < netstate.max_players; uid += 1) {
            if (netstate.users[uid].progress == USER_UNUSED) {
                continue;
            }
            SendUserUpdate(source, uid);
            if (uid != netstate.my_id && uid != source) {
                SendUserUpdate(uid, source);
            }
        }
        UpdateLocalPlayerInfo(source);
        return Lb_OK;
    }
    if (type == NETMSG_USERUPDATE) {
        if (source != SERVER_ID) {
            WARNLOG("Unexpected USERUPDATE");
            return Lb_OK;
        }
        NetUserId id = (NetUserId)*ptr;
        if (id < 0 || id >= netstate.max_players) {
            ERRORLOG("Critical error: Out of range user ID %i received from server, could be used for buffer overflow attack", id);
            abort();
        }
        ptr += 1;
        netstate.users[id].progress = (enum NetUserProgress)*ptr;
        ptr += 1;
        const char* name;
        if (!read_message_string(&ptr, sizeof(netstate.msg_buffer), true, &name)) {
            ERRORLOG("Critical error: Unterminated name in USERUPDATE");
            abort();
        }
        snprintf(netstate.users[id].name, sizeof(netstate.users[id].name), "%s", name);
        UpdateLocalPlayerInfo(id);
        return Lb_OK;
    }
    if (type == NETMSG_FRONTEND || type == NETMSG_STARTUP_SYNC || type == NETMSG_GAMEPLAY_UNSEQUENCED) {
        NetUserId peer_id = (NetUserId)*ptr;
        if (peer_id < 0 || peer_id >= netstate.max_players) {
            ERRORLOG("Critical error: Out of range peer ID %i received, could be used for buffer overflow attack", peer_id);
            abort();
        }
        if (source != SERVER_ID && source != peer_id) {
            WARNLOG("Peer %i tried to send gameplay frame for peer %i", source, peer_id);
            return Lb_OK;
        }
        char* peer_buf = ((char*)server_buf) + peer_id * frame_size;
        ptr += 1;
        netstate.users[peer_id].ack = *(int*)ptr;
        ptr += 4;
        if (type == NETMSG_GAMEPLAY_UNSEQUENCED) {
            size_t payload_size = msg_size - (ptr - netstate.msg_buffer);
            if (!read_packet_bundle(ptr, payload_size, (PlayerNumber)peer_id, peer_buf, frame_size)) {
                WARNLOG("Invalid gameplay packet bundle from peer %i (%u bytes)", peer_id, (unsigned)payload_size);
                return Lb_OK;
            }
        } else {
            memcpy(peer_buf, ptr, frame_size);
        }
        if (netstate.my_id == SERVER_ID) {
            TbBool unsequenced = (type == NETMSG_GAMEPLAY_UNSEQUENCED);
            send_to_active_peers(netstate.my_id, peer_id, netstate.msg_buffer, msg_size, unsequenced);
        }
        return Lb_OK;
    }
    if (type == NETMSG_UNPAUSE) {
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
    if (type == NETMSG_CHATMESSAGE) {
        int player_id = (int)*ptr;
        ptr += 1;
        const char* message;
        if (!read_message_string(&ptr, sizeof(netstate.msg_buffer), true, &message)) {
            ERRORLOG("Chat message too long or not null-terminated");
            return Lb_OK;
        }
        process_chat_message_end(player_id, message);
        if (netstate.my_id == SERVER_ID && source != SERVER_ID) {
            send_to_active_peers(netstate.my_id, source, netstate.msg_buffer, msg_size, false);
        }
        return Lb_OK;
    }
    if (type == NETMSG_GAMEPLAY_PACKET_HISTORY) {
        size_t payload_size = msg_size - (ptr - netstate.msg_buffer);
        receive_packet_history(source, source == SERVER_ID, ptr, payload_size, server_buf, frame_size);
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

static TbBool have_received_player_packet(GameTurn turn, PlayerNumber player)
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
    GameTurn historical_turn = get_gameturn() - game.input_lag_turns;
    for (PlayerNumber player = 0; player < NET_PLAYERS_COUNT; player += 1) {
        if (player != local_packet_num && network_player_active(player) && !have_received_player_packet(historical_turn, player)) {
            return false;
        }
    }
    return true;
}

void sync_packet_history(void)
{
    static TbClockMSec last_packet_history_send = -PROACTIVE_HISTORY_SEND_MS;

    TbClockMSec now = LbTimerClock();
    TbClockMSec send_interval = PROACTIVE_HISTORY_SEND_MS;
    NetUserId destination = SERVER_ID;
    if (netstate.my_id == SERVER_ID && netstate.max_players > 1) {
        send_interval = PROACTIVE_HISTORY_SEND_MS / (netstate.max_players - 1);
        destination = 1 + ((now / send_interval) % (netstate.max_players - 1));
    }
    if (now - last_packet_history_send < send_interval || !can_send_peer(destination)) {
        return;
    }

    last_packet_history_send = now;
    for (PlayerNumber player = 0; player < netstate.max_players; player += 1) {
        if (player == destination ||
            (netstate.my_id != SERVER_ID && player != netstate.my_id) ||
            (player != netstate.my_id && !network_player_active(player))) {
            continue;
        }
        send_packet_history(destination, player);
    }
}

static TbBool host_gone(void)
{
    return netstate.my_id != SERVER_ID && netstate.users[SERVER_ID].progress == USER_UNUSED;
}

static TbBool host_started_level(const void* server_buf, size_t frame_size)
{
    const struct ScreenPacket* host_packet;
    if (frame_size != sizeof(struct ScreenPacket)) {
        return false;
    }

    host_packet = &((const struct ScreenPacket*)server_buf)[get_host_player_id()];
    if ((host_packet->networkstatus_flags & NetStat_PlayerConnected) == 0) {
        return false;
    }
    if (screen_packet_action(host_packet) != NetAct_HostStartLevel) {
        return false;
    }
    return host_packet->action_par1 > 0;
}

static TbBool receive_packet_history(NetUserId source, TbBool from_server, const char* buffer, size_t payload_size, void* server_buf, size_t frame_size)
{
    if (payload_size < sizeof(struct GameplayPacketHeader)) {
        WARNLOG("Gameplay packet history from peer %i was too small (%u bytes)", source, (unsigned)payload_size);
        return false;
    }

    struct GameplayPacketHeader header;
    memcpy(&header, buffer, sizeof(header));
    if (header.player < 0 || header.player >= netstate.max_players) {
        WARNLOG("Gameplay packet history from peer %i had invalid player %d", source, (int)header.player);
        return false;
    }
    if (!from_server && source != header.player) {
        WARNLOG("Peer %i tried to send gameplay packet history for peer %i", source, (int)header.player);
        return false;
    }
    size_t expected_size = sizeof(struct GameplayPacketHeader) + header.compressed_length;
    if (payload_size != expected_size) {
        WARNLOG("Gameplay packet history from peer %i had size mismatch (%u != %u + %u)",
            source, (unsigned)payload_size, (unsigned)sizeof(struct GameplayPacketHeader), header.compressed_length);
        return false;
    }
    if (header.original_length > sizeof(struct RedundantPacketBundle) || header.original_length < sizeof(unsigned char)) {
        WARNLOG("Gameplay packet history from peer %i had invalid original length %u", source, header.original_length);
        return false;
    }

    char packet_history_buffer[sizeof(struct RedundantPacketBundle)];
    const char* compressed_buffer = buffer + sizeof(struct GameplayPacketHeader);
    uLongf packet_history_size = header.original_length;
    int uncompress_result = uncompress((Bytef*)packet_history_buffer, &packet_history_size, (const Bytef*)compressed_buffer, header.compressed_length);
    if (uncompress_result != Z_OK || packet_history_size != header.original_length) {
        WARNLOG("Gameplay packet history decompression from peer %i failed: zlib error %d", source, uncompress_result);
        return false;
    }
    uLong verify_crc = crc32(0L, Z_NULL, 0);
    verify_crc = crc32(verify_crc, (const Bytef*)packet_history_buffer, packet_history_size);
    if ((unsigned int)verify_crc != header.data_checksum) {
        WARNLOG("Gameplay packet history checksum mismatch from peer %i", source);
        return false;
    }

    char* peer_buf = ((char*)server_buf) + header.player * frame_size;
    if (!read_packet_bundle(packet_history_buffer, header.original_length, header.player, peer_buf, frame_size)) {
        WARNLOG("Gameplay packet history from peer %i could not be stored for player %d", source, (int)header.player);
        return false;
    }
    return true;
}

static void collect_messages_from_peer(NetUserId peer_id, void *server_buf, size_t frame_size)
{
    while (netstate.sp->msgready(peer_id, 0)) {
        ProcessMessage(peer_id, server_buf, frame_size);
    }
}

static void wait_missing_packets(void *server_buf, size_t frame_size)
{
    if (game.skip_initial_input_turns > 0) {
        return;
    }

    struct PlayerInfo* my_player = get_my_player();
    if (!player_exists(my_player)) {
        return;
    }

    PlayerNumber local_packet_num = my_player->packet_num;
    if (have_received_all_packets(local_packet_num)) {
        return;
    }

    GameTurn historical_turn = get_gameturn() - game.input_lag_turns;
    TbClockMSec start = LbTimerClock();
    MULTIPLAYER_LOG("LbNetwork_ExchangeGameplay: Missing packets for turn=%lu, collecting...", (unsigned long)historical_turn);
    sync_packet_history();

    while (!have_received_all_packets(local_packet_num)) {
        netstate.sp->update(OnNewUser);
        if (host_gone()) {
            MULTIPLAYER_LOG("LbNetwork_ExchangeGameplay: Host disconnected while waiting for turn=%lu", (unsigned long)historical_turn);
            return;
        }
        for (NetUserId peer_id = 0; peer_id < netstate.max_players; peer_id += 1) {
            if (!can_send_peer(peer_id)) {
                continue;
            }
            if (netstate.my_id == SERVER_ID && have_received_player_packet(historical_turn, (PlayerNumber)peer_id)) {
                continue;
            }
            collect_messages_from_peer(peer_id, server_buf, frame_size);
            if (host_gone()) {
                MULTIPLAYER_LOG("LbNetwork_ExchangeGameplay: Host disconnected while collecting turn=%lu", (unsigned long)historical_turn);
                return;
            }
            if (have_received_all_packets(local_packet_num)) {
                int32_t elapsed = LbTimerClock() - start;
                MULTIPLAYER_LOG("LbNetwork_ExchangeGameplay: Completed wait for turn=%lu after %dms", (unsigned long)historical_turn, elapsed);
                return;
            }
        }
        if ((LbTimerClock() - start) >= TIMEOUT_GAMEPLAY_MISSING_PACKET) {
            MULTIPLAYER_LOG("LbNetwork_ExchangeGameplay: Missing packets remained for turn=%lu after collection", (unsigned long)historical_turn);
            return;
        }
        sync_packet_history();
        network_yield_waiting_gameplay_packets();
        if (quit_game || exit_keeper) {
            return;
        }
    }
}

TbError LbNetwork_ExchangeLogin(char *plyr_name)
{
    NETMSG("Logging in as %s", plyr_name);
    if (1 + strlen(netstate.password) + 1 + strlen(plyr_name) + 1 + sizeof(net_current_version) >= sizeof(netstate.msg_buffer)) {
        ERRORLOG("Login credentials too long");
        return Lb_FAIL;
    }
    char * ptr = InitMessageBuffer(NETMSG_LOGIN);
    strcpy(ptr, netstate.password);
    ptr += strlen(netstate.password) + 1;
    strcpy(ptr, plyr_name);
    ptr += strlen(plyr_name) + 1;
    memcpy(ptr, &net_current_version, sizeof(net_current_version));
    ptr += sizeof(net_current_version);
    SendMessage(SERVER_ID, ptr);
    TbClockMSec start = LbTimerClock();
    while (true) {
        TbClockMSec elapsed = LbTimerClock() - start;
        if (elapsed >= TIMEOUT_JOIN_LOBBY) {
            NETMSG("ExchangeLogin: timed out waiting for login response (%dms)", (int)TIMEOUT_JOIN_LOBBY);
            break;
        }
        unsigned wait_ms = (unsigned)(TIMEOUT_JOIN_LOBBY - elapsed);
        if (!netstate.sp->msgready(SERVER_ID, wait_ms)) {
            NETMSG("ExchangeLogin: msgready returned false");
            break;
        }
        if (ProcessMessage(SERVER_ID, &net_screen_packet, sizeof(struct ScreenPacket)) == Lb_FAIL) {
            NETMSG("ExchangeLogin: ProcessMessage failed");
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
        ProcessMessage(SERVER_ID, &net_screen_packet, sizeof(struct ScreenPacket));
    }
    if (netstate.my_id == INVALID_USER_ID) {
        NETMSG("ExchangeLogin: login unsuccessful, still INVALID_USER_ID");
        return Lb_FAIL;
    }
    return Lb_OK;
}

TbError LbNetwork_ExchangeGameplay(void *send_buf, void *server_buf, size_t client_frame_size)
{
    if (send_exchange_data(send_buf, server_buf, client_frame_size, NETMSG_GAMEPLAY_UNSEQUENCED) != Lb_OK) {
        return Lb_FAIL;
    }

    for (NetUserId peer_id = 0; peer_id < netstate.max_players; peer_id += 1) {
        if (can_send_peer(peer_id)) {
            collect_messages_from_peer(peer_id, server_buf, client_frame_size);
        }
    }
    sync_packet_history();
    wait_missing_packets(server_buf, client_frame_size);
    netstate.seq_nbr += 1;
    return Lb_OK;
}

TbError LbNetwork_ExchangeWithWait(enum NetMessageType msg_type, void *send_buf, void *server_buf, size_t client_frame_size)
{
    if (send_exchange_data(send_buf, server_buf, client_frame_size, msg_type) != Lb_OK) {
        return Lb_FAIL;
    }

    TbBool stop_waiting = false;
    if (msg_type == NETMSG_FRONTEND) {
        stop_waiting = host_started_level(server_buf, client_frame_size);
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
                    ProcessMessage(peer_id, server_buf, client_frame_size);
                    if ((enum NetMessageType)netstate.msg_buffer[0] == msg_type) {
                        NetUserId frame_peer_id = (unsigned char)netstate.msg_buffer[1];
                        if (frame_peer_id < netstate.max_players) {
                            received_frames[frame_peer_id] = true;
                        }
                    }
                }
                if (msg_type == NETMSG_FRONTEND) {
                    stop_waiting = host_started_level(server_buf, client_frame_size);
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
    char* ptr = InitMessageBuffer(NETMSG_CHATMESSAGE);
    *ptr = player_id;
    ptr += 1;
    strcpy(ptr, message);
    ptr += strlen(message) + 1;
    send_or_broadcast(ptr - netstate.msg_buffer);
}

void LbNetwork_BroadcastUnpauseTimesync(void)
{
    MULTIPLAYER_LOG("LbNetwork_BroadcastUnpauseTimesync");
    InitMessageBuffer(NETMSG_UNPAUSE);
    send_or_broadcast(1);
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
