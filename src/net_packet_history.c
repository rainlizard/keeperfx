/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file net_packet_history.c
 *     Gameplay packet history storage, bundling, and recovery.
 * @par Purpose:
 *     Stores received gameplay packets, bundles packet history, and resends it.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     30 Apr 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "net_packet_history.h"

#include "bflib_datetm.h"
#include "bflib_network_internal.h"
#include "game_legacy.h"
#include "net_game.h"
#include "packets.h"
#include "player_data.h"
#include <stdlib.h>
#include <zlib.h>
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
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

/******************************************************************************/

static TbBool valid_history_player(PlayerNumber player)
{
    return player >= 0 && player < MAX_N_USERS;
}

static size_t packet_list_size(unsigned char valid_count)
{
    return sizeof(unsigned char) + valid_count * sizeof(struct Packet);
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

static size_t write_packet_history(PlayerNumber player, char* out_buffer)
{
    if (!valid_history_player(player)) {
        return 0;
    }
    struct PacketHistory* history = &packet_history[player];
    if (history->valid_count == 0) {
        return 0;
    }
    struct RedundantPacketBundle* packet_bundle = (struct RedundantPacketBundle*)out_buffer;
    packet_bundle->valid_count = copy_packet_history(packet_bundle->packets, history, history->valid_count, NULL);
    if (packet_bundle->valid_count == 0) {
        return 0;
    }
    return packet_list_size(packet_bundle->valid_count);
}

static TbBool store_packet_bundle(const char* packet_bundle_buffer, size_t packet_bundle_buffer_size, PlayerNumber source_player)
{
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
    return true;
}

static TbBool can_send_history(NetUserId peer_id)
{
    return (peer_id != netstate.my_id) &&
        (netstate.users[peer_id].progress != USER_UNUSED) &&
        (my_player_number == get_host_player_id() || peer_id == SERVER_ID);
}

static void send_packet_history(NetUserId destination, PlayerNumber player)
{
    char packet_history_buffer[sizeof(struct RedundantPacketBundle)];
    size_t packet_history_size = write_packet_history(player, packet_history_buffer);
    if (packet_history_size == 0) {
        return;
    }

    uLong data_crc = crc32(0L, Z_NULL, 0);
    data_crc = crc32(data_crc, (const Bytef*)packet_history_buffer, packet_history_size);

    uLongf compressed_size = compressBound(packet_history_size);
    char* compressed_buffer = (char*)malloc(compressed_size);
    if (compressed_buffer == NULL) {
        ERRORLOG("Failed to allocate gameplay packet history compression buffer");
        return;
    }

    int compress_result = compress((Bytef*)compressed_buffer, &compressed_size, (const Bytef*)packet_history_buffer, packet_history_size);
    if (compress_result != Z_OK) {
        ERRORLOG("Gameplay packet history compression failed for player %d: zlib error %d", (int)player, compress_result);
        free(compressed_buffer);
        return;
    }

    struct GameplayPacketHeader header;
    header.player = player;
    header.compressed_length = compressed_size;
    header.original_length = packet_history_size;
    header.data_checksum = (unsigned int)data_crc;

    size_t message_size = sizeof(unsigned char) + sizeof(struct GameplayPacketHeader) + compressed_size;
    char* message_buffer = (char*)malloc(message_size);
    if (message_buffer == NULL) {
        ERRORLOG("Failed to allocate gameplay packet history message buffer");
        free(compressed_buffer);
        return;
    }

    message_buffer[0] = NETMSG_GAMEPLAY_PACKET_HISTORY;
    memcpy(message_buffer + sizeof(unsigned char), &header, sizeof(header));
    memcpy(message_buffer + sizeof(unsigned char) + sizeof(header), compressed_buffer, compressed_size);
    MULTIPLAYER_LOG("Sending compressed gameplay packet history for player=%d to peer=%d (%lu -> %lu bytes)",
        (int)player, (int)destination, (unsigned long)packet_history_size, (unsigned long)compressed_size);
    netstate.sp->sendmsg_single(destination, message_buffer, message_size);

    free(message_buffer);
    free(compressed_buffer);
}

static TbBool valid_history_header(NetUserId source, TbBool from_server, const struct GameplayPacketHeader* header, size_t msg_size)
{
    if (header->player < 0 || header->player >= netstate.max_players) {
        WARNLOG("Gameplay packet history from peer %i had invalid player %d", source, (int)header->player);
        return false;
    }
    if (!from_server && source != header->player) {
        WARNLOG("Peer %i tried to send gameplay packet history for peer %i", source, (int)header->player);
        return false;
    }

    size_t expected_size = sizeof(unsigned char) + sizeof(struct GameplayPacketHeader) + header->compressed_length;
    if (msg_size != expected_size) {
        WARNLOG("Gameplay packet history from peer %i had size mismatch (%u != %u + %u)",
            source, (unsigned)msg_size, (unsigned)(sizeof(unsigned char) + sizeof(struct GameplayPacketHeader)), header->compressed_length);
        return false;
    }
    if (header->original_length > sizeof(struct RedundantPacketBundle) || header->original_length < sizeof(unsigned char)) {
        WARNLOG("Gameplay packet history from peer %i had invalid original length %u", source, header->original_length);
        return false;
    }
    return true;
}

static TbBool unpack_packet_history(NetUserId source, const struct GameplayPacketHeader* header, const char* compressed_buffer, char* packet_history_buffer)
{
    uLongf packet_history_size = header->original_length;
    int uncompress_result = uncompress((Bytef*)packet_history_buffer, &packet_history_size, (const Bytef*)compressed_buffer, header->compressed_length);
    if (uncompress_result != Z_OK || packet_history_size != header->original_length) {
        WARNLOG("Gameplay packet history decompression from peer %i failed: zlib error %d", source, uncompress_result);
        return false;
    }

    uLong verify_crc = crc32(0L, Z_NULL, 0);
    verify_crc = crc32(verify_crc, (const Bytef*)packet_history_buffer, packet_history_size);
    if ((unsigned int)verify_crc != header->data_checksum) {
        WARNLOG("Gameplay packet history checksum mismatch from peer %i", source);
        return false;
    }
    return true;
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

const struct Packet* get_received_player_packet(GameTurn turn, PlayerNumber player)
{
    for (int i = 0; i < PACKET_HISTORY_SIZE * PACKETS_COUNT; i += 1) {
        const struct ReceivedPacketEntry* entry = &received_history.entries[i];
        if (entry->valid && entry->turn == turn && entry->player == player) {
            return &entry->packet;
        }
    }
    return NULL;
}

TbBool have_received_player_packet(GameTurn turn, PlayerNumber player)
{
    return get_received_player_packet(turn, player) != NULL;
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

size_t build_packet_bundle(PlayerNumber player, const struct Packet* current_packet, char* out_buffer)
{
    if (!valid_history_player(player)) {
        return 0;
    }
    struct PacketHistory* history = &packet_history[player];
    struct RedundantPacketBundle* packet_bundle = (struct RedundantPacketBundle*)out_buffer;
    packet_bundle->packets[0] = *current_packet;
    packet_bundle->valid_count = 1 + copy_packet_history(&packet_bundle->packets[1], history, REDUNDANT_PACKET_BUNDLE - 1, current_packet);
    store_packet_history(player, current_packet);
    return packet_list_size(packet_bundle->valid_count);
}

TbBool read_packet_bundle(const char* packet_bundle_buffer, size_t packet_bundle_buffer_size, PlayerNumber source_player, void* out_packet, size_t packet_size)
{
    if (packet_size != sizeof(struct Packet)) {
        WARNLOG("Gameplay packet bundle size mismatch (%u != %u)", (unsigned)packet_size, (unsigned)sizeof(struct Packet));
        return false;
    }
    if (!store_packet_bundle(packet_bundle_buffer, packet_bundle_buffer_size, source_player)) {
        return false;
    }

    const struct RedundantPacketBundle* packet_bundle = (const struct RedundantPacketBundle*)packet_bundle_buffer;
    memcpy(out_packet, &packet_bundle->packets[0], packet_size);
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
    if (now - last_packet_history_send < send_interval || !can_send_history(destination)) {
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

TbBool receive_packet_history(NetUserId source, TbBool from_server, const char* buffer, size_t msg_size, void* server_buf, size_t frame_size)
{
    if (msg_size < sizeof(unsigned char) + sizeof(struct GameplayPacketHeader)) {
        WARNLOG("Gameplay packet history from peer %i was too small (%u bytes)", source, (unsigned)msg_size);
        return false;
    }

    struct GameplayPacketHeader header;
    memcpy(&header, buffer, sizeof(header));
    if (!valid_history_header(source, from_server, &header, msg_size)) {
        return false;
    }

    char packet_history_buffer[sizeof(struct RedundantPacketBundle)];
    const char* compressed_buffer = buffer + sizeof(struct GameplayPacketHeader);
    if (!unpack_packet_history(source, &header, compressed_buffer, packet_history_buffer)) {
        return false;
    }

    char* peer_buf = ((char*)server_buf) + header.player * frame_size;
    if (!read_packet_bundle(packet_history_buffer, header.original_length, header.player, peer_buf, frame_size)) {
        WARNLOG("Gameplay packet history from peer %i could not be stored for player %d", source, (int)header.player);
        return false;
    }
    return true;
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
