/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file bflib_network_gameplay.c
 *     Gameplay packet exchange for Dungeon Keeper multiplayer.
 * @par Purpose:
 *     Packet redundancy, history and recovery for multiplayer turns.
 * @par Comment:
 *     Split from the former bflib_network_exchange.cpp module.
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
#include "bflib_network_exchange.h"
#include "bflib_network_exchange_impl.h"
#include "bflib_network.h"
#include "bflib_network_internal.h"
#include "bflib_datetm.h"
#include "globals.h"
#include "player_data.h"
#include "net_game.h"
#include "packets.h"
#include "keeperfx.hpp"
#include <stdlib.h>
#include <zlib.h>
#include "post_inc.h"

extern void network_yield_waiting_gameplay_packets(void);

/******************************************************************************/

#define PACKET_HISTORY_SIZE 32
#define PACKET_HISTORY_INTERVAL_MS 500
#define WAITING_HOST_RESEND_COOLDOWN_MS 500
// Redundant packet value represents current packet + additional packets.
#define REDUNDANT_PACKET_BUNDLE 2

struct PacketHistoryEntry {
    GameTurn turn;
    TbBool valid;
    struct Packet packet;
};

struct PacketHistory {
    struct PacketHistoryEntry entries[PACKET_HISTORY_SIZE];
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

static struct PacketHistory packet_history[MAX_N_USERS];
static TbClockMSec last_packet_history_send;
static TbClockMSec last_host_resend;
static NetUserId next_history_peer;

static TbBool is_history_player_valid(PlayerNumber player)
{
    return player >= 0 && player < MAX_N_USERS;
}

static TbBool is_newer_turn(GameTurn turn, GameTurn base_turn)
{
    return (GameTurnDelta)(turn - base_turn) > 0;
}

static size_t packet_bundle_size(unsigned char valid_count)
{
    return sizeof(unsigned char) + valid_count * sizeof(struct Packet);
}

static int32_t find_oldest_history(const struct PacketHistory *history)
{
    int32_t oldest_index = -1;
    for (int32_t i = 0; i < PACKET_HISTORY_SIZE; i += 1) {
        const struct PacketHistoryEntry *entry = &history->entries[i];
        if (!entry->valid) {
            continue;
        }
        if (oldest_index < 0 || is_newer_turn(history->entries[oldest_index].turn, entry->turn)) {
            oldest_index = i;
        }
    }
    return oldest_index;
}

static void store_packet_history(PlayerNumber player, const struct Packet *packet)
{
    if (!is_history_player_valid(player)) {
        return;
    }
    if (is_packet_empty(packet)) {
        return;
    }

    struct PacketHistory *history = &packet_history[player];
    for (int i = 0; i < PACKET_HISTORY_SIZE; i += 1) {
        struct PacketHistoryEntry *entry = &history->entries[i];
        if (entry->valid && entry->turn == packet->turn) {
            memcpy(&entry->packet, packet, sizeof(struct Packet));
            return;
        }
    }

    int32_t entry_index = history->next_index;
    if (history->valid_count >= PACKET_HISTORY_SIZE) {
        entry_index = find_oldest_history(history);
        if (entry_index < 0) {
            return;
        }
        if (!is_newer_turn(packet->turn, history->entries[entry_index].turn)) {
            return;
        }
    }

    struct PacketHistoryEntry *entry = &history->entries[entry_index];
    entry->turn = packet->turn;
    entry->valid = true;
    memcpy(&entry->packet, packet, sizeof(struct Packet));
    history->next_index = (entry_index + 1) % PACKET_HISTORY_SIZE;
    if (history->valid_count < PACKET_HISTORY_SIZE) {
        history->valid_count += 1;
    }
}

static size_t build_history_bundle(PlayerNumber player, struct RedundantPacketBundle *packet_bundle, unsigned char packet_count, const struct Packet *first_packet)
{
    const struct PacketHistory *history;
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
        int32_t history_index = (history->next_index + PACKET_HISTORY_SIZE - i - 1) % PACKET_HISTORY_SIZE;
        const struct PacketHistoryEntry *entry = &history->entries[history_index];
        if (!entry->valid) {
            continue;
        }
        if (first_packet != NULL && entry->turn == first_packet->turn) {
            continue;
        }
        packet_bundle->packets[packet_bundle->valid_count + copied_count] = entry->packet;
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

static TbBool has_received_player_packet(GameTurn turn, PlayerNumber player)
{
    if (!is_history_player_valid(player)) {
        return false;
    }
    const struct PacketHistory *history = &packet_history[player];
    for (int i = 0; i < PACKET_HISTORY_SIZE; i += 1) {
        const struct PacketHistoryEntry *entry = &history->entries[i];
        if (entry->valid && entry->turn == turn) {
            return true;
        }
    }
    return false;
}

static TbBool unpack_history_bundle(const char *buffer, size_t buffer_size, PlayerNumber player, void *out_packet, size_t packet_size)
{
    if (out_packet != NULL && packet_size != sizeof(struct Packet)) {
        WARNLOG("Gameplay packet bundle size mismatch (%u != %u)", (unsigned)packet_size, (unsigned)sizeof(struct Packet));
        return false;
    }
    if (!is_history_player_valid(player) || buffer_size < sizeof(unsigned char)) {
        return false;
    }
    const struct RedundantPacketBundle *packet_bundle = (const struct RedundantPacketBundle *)buffer;
    if (packet_bundle->valid_count < 1 || packet_bundle->valid_count > PACKET_HISTORY_SIZE) {
        return false;
    }
    if (buffer_size < packet_bundle_size(packet_bundle->valid_count)) {
        return false;
    }
    for (unsigned char i = 0; i < packet_bundle->valid_count; i += 1) {
        const struct Packet *packet = &packet_bundle->packets[i];
        if (is_packet_empty(packet)) {
            MULTIPLAYER_LOG("unpack_history_bundle: Skipping empty packet for player %d turn %lu", player, (unsigned long)packet->turn);
            continue;
        }
        store_packet_history(player, packet);
    }
    if (out_packet != NULL) {
        memcpy(out_packet, &packet_bundle->packets[0], packet_size);
    }
    return true;
}

size_t gameplay_build_payload(PlayerNumber player, const struct Packet *first_packet, void *buffer)
{
    return build_history_bundle(player, (struct RedundantPacketBundle *)buffer, REDUNDANT_PACKET_BUNDLE, first_packet);
}

TbBool gameplay_unpack_payload(const char *buffer, size_t buffer_size, PlayerNumber player, void *out_packet, size_t packet_size)
{
    return unpack_history_bundle(buffer, buffer_size, player, out_packet, packet_size);
}

TbBool gameplay_read_history(NetUserId source, const char *buffer, size_t buffer_size)
{
    TbBool from_server = (source == SERVER_ID);
    if (buffer_size < sizeof(struct PacketHistoryHeader)) {
        WARNLOG("Gameplay packet history from peer %i was too small (%u bytes)", source, (unsigned)buffer_size);
        return false;
    }
    struct PacketHistoryHeader header;
    memcpy(&header, buffer, sizeof(header));
    if (header.player < 0 || header.player >= netstate.max_players) {
        WARNLOG("Gameplay packet history from peer %i had invalid player %d", source, (int)header.player);
        return false;
    }
    if (!from_server && source != header.player) {
        WARNLOG("Peer %i tried to send gameplay packet history for peer %i", source, (int)header.player);
        return false;
    }
    size_t expected_size = sizeof(struct PacketHistoryHeader) + header.compressed_length;
    if (buffer_size != expected_size) {
        WARNLOG("Gameplay packet history from peer %i had size mismatch (%u != %u + %u)",
            source, (unsigned)buffer_size, (unsigned)sizeof(struct PacketHistoryHeader), header.compressed_length);
        return false;
    }
    if (header.original_length > sizeof(struct RedundantPacketBundle) || header.original_length < sizeof(unsigned char)) {
        WARNLOG("Gameplay packet history from peer %i had invalid original length %u", source, header.original_length);
        return false;
    }

    char packet_history_buffer[sizeof(struct RedundantPacketBundle)];
    const char *compressed_buffer = buffer + sizeof(struct PacketHistoryHeader);
    uLongf packet_history_size = header.original_length;
    int uncompress_result = uncompress((Bytef *)packet_history_buffer, &packet_history_size, (const Bytef *)compressed_buffer, header.compressed_length);
    if (uncompress_result != Z_OK || packet_history_size != header.original_length) {
        WARNLOG("Gameplay packet history decompression from peer %i failed: zlib error %d", source, uncompress_result);
        return false;
    }
    uLong verify_crc = crc32(0L, Z_NULL, 0);
    verify_crc = crc32(verify_crc, (const Bytef *)packet_history_buffer, packet_history_size);
    if ((unsigned int)verify_crc != header.data_checksum) {
        WARNLOG("Gameplay packet history checksum mismatch from peer %i", source);
        return false;
    }

    if (!unpack_history_bundle(packet_history_buffer, header.original_length, header.player, NULL, sizeof(struct Packet))) {
        WARNLOG("Gameplay packet history from peer %i could not be stored for player %d", source, (int)header.player);
        return false;
    }
    return true;
}

void initialize_packet_history(void)
{
    memset(packet_history, 0, sizeof(packet_history));
    last_packet_history_send = 0;
    last_host_resend = 0;
    next_history_peer = SERVER_ID;
}

const struct Packet *get_received_turn_packets(GameTurn turn)
{
    static struct Packet packets_for_turn[PACKETS_COUNT];
    TbBool found = false;
    memset(packets_for_turn, 0, sizeof(packets_for_turn));

    for (PlayerNumber player = 0; player < NET_PLAYERS_COUNT; player += 1) {
        const struct PacketHistory *history = &packet_history[player];
        for (int i = 0; i < PACKET_HISTORY_SIZE; i += 1) {
            const struct PacketHistoryEntry *entry = &history->entries[i];
            if (!entry->valid || entry->turn != turn) {
                continue;
            }
            packets_for_turn[player] = entry->packet;
            found = true;
        }
    }

    if (!found) {
        return NULL;
    }
    return packets_for_turn;
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

static TbBool send_due(TbClockMSec *last_send_time, TbClockMSec interval)
{
    TbClockMSec current_time = LbTimerClock();
    if (*last_send_time == 0) {
        *last_send_time = current_time;
        return false;
    }
    if (current_time - *last_send_time < interval) {
        return false;
    }
    *last_send_time = current_time;
    return true;
}

static TbBool host_lost(GameTurn turn, const char *state)
{
    if (netstate.my_id == SERVER_ID) {
        return false;
    }
    if (netstate.users[SERVER_ID].progress != USER_UNUSED) {
        return false;
    }
    MULTIPLAYER_LOG("LbNetwork_ExchangeGameplay: Host disconnected while %s turn=%lu", state, (unsigned long)turn);
    netstate.seq_nbr += 1;
    return true;
}

static void send_history_to(NetUserId peer_id, PlayerNumber player, unsigned char packet_count, const struct Packet *first_packet)
{
    if (!can_send_to_peer(peer_id)) {
        return;
    }

    char packet_history_buffer[sizeof(struct RedundantPacketBundle)];
    size_t packet_history_size = build_history_bundle(player, (struct RedundantPacketBundle *)packet_history_buffer, packet_count, first_packet);
    if (packet_history_size == 0) {
        return;
    }
    uLong data_crc = crc32(0L, Z_NULL, 0);
    data_crc = crc32(data_crc, (const Bytef *)packet_history_buffer, packet_history_size);
    size_t header_size = sizeof(unsigned char) + sizeof(struct PacketHistoryHeader);
    uLongf compressed_size = compressBound(packet_history_size);
    size_t max_message_size = header_size + compressed_size;
    char *message_buffer = (char *)malloc(max_message_size);
    if (message_buffer == NULL) {
        ERRORLOG("Failed to allocate gameplay packet history message buffer");
        return;
    }
    char *compressed_buffer = message_buffer + header_size;
    int compress_result = compress((Bytef *)compressed_buffer, &compressed_size, (const Bytef *)packet_history_buffer, packet_history_size);
    if (compress_result != Z_OK) {
        ERRORLOG("Gameplay packet history compression failed for player %d: zlib error %d", (int)player, compress_result);
        free(message_buffer);
        return;
    }
    struct PacketHistoryHeader header = { (PlayerNumber)player, (unsigned int)compressed_size, (unsigned int)packet_history_size, (unsigned int)data_crc };
    size_t message_size = header_size + compressed_size;
    message_buffer[0] = NETMSG_GAMEPLAY_PACKET_HISTORY;
    memcpy(message_buffer + sizeof(unsigned char), &header, sizeof(header));
    MULTIPLAYER_LOG("Sending reliable compressed gameplay packet history for player=%d to peer=%d (%lu -> %lu bytes)",
        (int)player, (int)peer_id, (unsigned long)packet_history_size, (unsigned long)compressed_size);
    netstate.sp->sendmsg_single(peer_id, message_buffer, message_size);
    free(message_buffer);
}

static void send_full_history(NetUserId peer_id)
{
    PlayerNumber player = (PlayerNumber)netstate.my_id;
    if (!is_history_player_valid(player)) {
        return;
    }
    send_history_to(peer_id, player, PACKET_HISTORY_SIZE, NULL);
}

static void send_wait_history(void)
{
    if (netstate.my_id != SERVER_ID) {
        if (send_due(&last_packet_history_send, PACKET_HISTORY_INTERVAL_MS)) {
            send_full_history(SERVER_ID);
        }
        return;
    }

    int active_peers = 0;
    for (NetUserId peer_id = 0; peer_id < netstate.max_players; peer_id += 1) {
        if (can_send_to_peer(peer_id)) {
            active_peers += 1;
        }
    }
    if (active_peers <= 0) {
        return;
    }

    TbClockMSec stagger_interval = PACKET_HISTORY_INTERVAL_MS / active_peers;
    if (stagger_interval < 1) {
        stagger_interval = 1;
    }
    if (!send_due(&last_packet_history_send, stagger_interval)) {
        return;
    }

    for (NetUserId i = 0; i < netstate.max_players; i += 1) {
        NetUserId peer_id = (next_history_peer + i) % netstate.max_players;
        if (!can_send_to_peer(peer_id)) {
            continue;
        }
        next_history_peer = (peer_id + 1) % netstate.max_players;
        send_full_history(peer_id);
        return;
    }
}

TbError LbNetwork_ExchangeGameplay(void *send_buf, void *server_buf, size_t frame_size)
{
    if (exchange_frame_message(send_buf, server_buf, frame_size, NETMSG_GAMEPLAY_UNSEQUENCED) != Lb_OK) {
        return Lb_FAIL;
    }

    for (NetUserId peer_id = 0; peer_id < netstate.max_players; peer_id += 1) {
        if (can_send_to_peer(peer_id)) {
            process_peer_msgs(peer_id, server_buf, frame_size);
        }
    }
    if (game.skip_initial_input_turns <= 0) {
        struct PlayerInfo *my_player = get_my_player();
        if (player_exists(my_player)) {
            PlayerNumber local_packet_player = my_player->packet_num;
            if (!have_all_turn_packets(local_packet_player)) {
                GameTurn expected_turn = get_gameturn() - game.input_lag_turns;
                TbClockMSec wait_start_time = LbTimerClock();
                TbBool turn_complete = false;
                MULTIPLAYER_LOG("LbNetwork_ExchangeGameplay: Missing packets for turn=%lu, collecting...", (unsigned long)expected_turn);

                while (!turn_complete) {
                    send_wait_history();
                    netstate.sp->update(OnNewUser);
                    if (host_lost(expected_turn, "waiting for")) {
                        return Lb_OK;
                    }
                    turn_complete = have_all_turn_packets(local_packet_player);
                    if (turn_complete) {
                        int32_t elapsed = LbTimerClock() - wait_start_time;
                        MULTIPLAYER_LOG("LbNetwork_ExchangeGameplay: Completed wait for turn=%lu after %dms", (unsigned long)expected_turn, elapsed);
                        break;
                    }
                    for (NetUserId peer_id = 0; peer_id < netstate.max_players; peer_id += 1) {
                        if (!can_send_to_peer(peer_id)) {
                            continue;
                        }
                        if (netstate.my_id == SERVER_ID && has_received_player_packet(expected_turn, (PlayerNumber)peer_id)) {
                            continue;
                        }
                        process_peer_msgs(peer_id, server_buf, frame_size);
                        if (host_lost(expected_turn, "collecting")) {
                            return Lb_OK;
                        }
                        turn_complete = have_all_turn_packets(local_packet_player);
                        if (turn_complete) {
                            break;
                        }
                    }
                    if (turn_complete) {
                        int32_t elapsed = LbTimerClock() - wait_start_time;
                        MULTIPLAYER_LOG("LbNetwork_ExchangeGameplay: Completed wait for turn=%lu after %dms", (unsigned long)expected_turn, elapsed);
                        break;
                    }
                    if ((LbTimerClock() - wait_start_time) >= TIMEOUT_GAMEPLAY_MISSING_PACKET) {
                        MULTIPLAYER_LOG("LbNetwork_ExchangeGameplay: Missing packets remained for turn=%lu after collection", (unsigned long)expected_turn);
                        break;
                    }
                    if (netstate.my_id == SERVER_ID && send_due(&last_host_resend, WAITING_HOST_RESEND_COOLDOWN_MS)) {
                        if (exchange_frame_message(send_buf, server_buf, frame_size, NETMSG_GAMEPLAY_UNSEQUENCED) != Lb_OK) {
                            return Lb_FAIL;
                        }
                    }
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
