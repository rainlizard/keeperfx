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

#define PACKET_HISTORY_SIZE 20
#define WAITING_HISTORY_MIN_DELAY_MS 50
#define WAITING_HISTORY_COOLDOWN_MS 500
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
static TbClockMSec last_packet_history_send;

static TbBool is_history_player_valid(PlayerNumber player)
{
    return player >= 0 && player < MAX_N_USERS;
}

static size_t packet_bundle_size(unsigned char valid_count)
{
    return sizeof(unsigned char) + valid_count * sizeof(struct Packet);
}

static void store_packet_history(PlayerNumber player, const struct Packet *packet)
{
    if (!is_history_player_valid(player)) {
        return;
    }
    struct PlayerPacketHistory *history = &player_packet_history[player];
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

static size_t build_history_bundle(PlayerNumber player, struct RedundantPacketBundle *packet_bundle, unsigned char packet_count, const struct Packet *first_packet)
{
    const struct PlayerPacketHistory *history;
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

static TbBool has_received_player_packet(GameTurn turn, PlayerNumber player)
{
    for (int i = 0; i < PACKET_HISTORY_SIZE * PACKETS_COUNT; i += 1) {
        const struct ReceivedPacketEntry *entry = &received_packet_history.entries[i];
        if (entry->valid && entry->turn == turn && entry->player == player) {
            return true;
        }
    }
    return false;
}

static TbBool unpack_history_bundle(const char *buffer, size_t buffer_size, PlayerNumber player, void *out_packet, size_t packet_size)
{
    if (packet_size != sizeof(struct Packet)) {
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
        store_packet_history(player, packet);
        if (!has_received_player_packet(packet->turn, player)) {
            if (is_packet_empty(packet)) {
                MULTIPLAYER_LOG("unpack_history_bundle: Skipping empty packet for player %d turn %lu", player, (unsigned long)packet->turn);
                continue;
            }
            int entry_index = received_packet_history.next_index;
            struct ReceivedPacketEntry *entry = &received_packet_history.entries[entry_index];
            entry->turn = packet->turn;
            entry->player = player;
            entry->valid = true;
            memcpy(&entry->packet, packet, sizeof(struct Packet));
            received_packet_history.next_index = (entry_index + 1) % (PACKET_HISTORY_SIZE * PACKETS_COUNT);
        }
    }
    memcpy(out_packet, &packet_bundle->packets[0], packet_size);
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

TbBool gameplay_read_history(NetUserId source, const char *buffer, size_t buffer_size, void *server_buf, size_t frame_size)
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

    char *player_frame = ((char *)server_buf) + header.player * frame_size;
    if (!unpack_history_bundle(packet_history_buffer, header.original_length, header.player, player_frame, frame_size)) {
        WARNLOG("Gameplay packet history from peer %i could not be stored for player %d", source, (int)header.player);
        return false;
    }
    return true;
}

void initialize_packet_history(void)
{
    memset(&received_packet_history, 0, sizeof(received_packet_history));
    memset(player_packet_history, 0, sizeof(player_packet_history));
    last_packet_history_send = 0;
}

const struct Packet *get_received_turn_packets(GameTurn turn)
{
    static struct Packet packets_for_turn[PACKETS_COUNT];
    TbBool found = false;
    memset(packets_for_turn, 0, sizeof(packets_for_turn));

    for (int i = 0; i < PACKET_HISTORY_SIZE * PACKETS_COUNT; i += 1) {
        const struct ReceivedPacketEntry *entry = &received_packet_history.entries[i];
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

static TbBool history_send_due(TbClockMSec wait_start_time)
{
    TbClockMSec current_time = LbTimerClock();
    if (current_time - wait_start_time < WAITING_HISTORY_MIN_DELAY_MS) {
        return false;
    }
    if (last_packet_history_send != 0 && current_time - last_packet_history_send < WAITING_HISTORY_COOLDOWN_MS) {
        return false;
    }
    last_packet_history_send = current_time;
    return true;
}

static void send_packet_history(void)
{
    NetUserId first_target = SERVER_ID;
    NetUserId target_limit = SERVER_ID + 1;
    if (netstate.my_id == SERVER_ID) {
        first_target = 1;
        target_limit = netstate.max_players;
    }

    for (NetUserId target_peer = first_target; target_peer < target_limit; target_peer += 1) {
        if (!can_send_to_peer(target_peer)) {
            continue;
        }

        for (PlayerNumber player = 0; player < netstate.max_players; player += 1) {
            if (player == target_peer ||
                (netstate.my_id != SERVER_ID && player != netstate.my_id) ||
                (player != netstate.my_id && !network_player_active(player))) {
                continue;
            }
            char packet_history_buffer[sizeof(struct RedundantPacketBundle)];
            size_t packet_history_size = build_history_bundle(player, (struct RedundantPacketBundle *)packet_history_buffer, PACKET_HISTORY_SIZE, NULL);
            if (packet_history_size == 0) {
                continue;
            }
            uLong data_crc = crc32(0L, Z_NULL, 0);
            data_crc = crc32(data_crc, (const Bytef *)packet_history_buffer, packet_history_size);
            uLongf compressed_size = compressBound(packet_history_size);
            size_t max_message_size = sizeof(unsigned char) + sizeof(struct PacketHistoryHeader) + compressed_size;
            char *message_buffer = (char *)malloc(max_message_size);
            if (message_buffer == NULL) {
                ERRORLOG("Failed to allocate gameplay packet history message buffer");
                continue;
            }
            char *compressed_buffer = message_buffer + sizeof(unsigned char) + sizeof(struct PacketHistoryHeader);
            int compress_result = compress((Bytef *)compressed_buffer, &compressed_size, (const Bytef *)packet_history_buffer, packet_history_size);
            if (compress_result != Z_OK) {
                ERRORLOG("Gameplay packet history compression failed for player %d: zlib error %d", (int)player, compress_result);
                free(message_buffer);
                continue;
            }
            struct PacketHistoryHeader header = { (PlayerNumber)player, (unsigned int)compressed_size, (unsigned int)packet_history_size, (unsigned int)data_crc };
            size_t message_size = sizeof(unsigned char) + sizeof(struct PacketHistoryHeader) + compressed_size;
            message_buffer[0] = NETMSG_GAMEPLAY_PACKET_HISTORY;
            memcpy(message_buffer + sizeof(unsigned char), &header, sizeof(header));
            MULTIPLAYER_LOG("Sending reliable compressed gameplay packet history for player=%d to peer=%d (%lu -> %lu bytes)",
                (int)player, (int)target_peer, (unsigned long)packet_history_size, (unsigned long)compressed_size);
            netstate.sp->sendmsg_single(target_peer, message_buffer, message_size);
            free(message_buffer);
        }
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
                MULTIPLAYER_LOG("LbNetwork_ExchangeGameplay: Missing packets for turn=%lu, collecting...", (unsigned long)expected_turn);

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
                        /* Keep draining every peer while stalled so future packets do not back up behind one missing turn. */
                        process_peer_msgs(peer_id, server_buf, frame_size);
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
                    if (history_send_due(wait_start_time)) {
                        send_packet_history();
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
