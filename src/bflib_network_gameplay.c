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
#include <zlib.h>
#include "post_inc.h"

extern void network_yield_waiting_gameplay_packets(void);
extern int32_t sync_mp_client_ns;

/******************************************************************************/

#define PACKET_HISTORY_SIZE 32
#define SEND_HISTORY_COOLDOWN_MS 500

struct PacketHistory {
    struct Packet entries[PACKET_HISTORY_SIZE];
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
};
#pragma pack()

static struct PacketHistory packet_history[MAX_N_USERS];
static TbClockMSec last_packet_history_send;
static TbClockMSec last_host_resend;
static NetUserId next_history_peer;

void store_packet_history(PlayerNumber player, const struct Packet *packet)
{
    if (player < 0 || player >= MAX_N_USERS || is_packet_empty(packet)) {
        return;
    }

    struct Packet *entry = &packet_history[player].entries[packet->turn % PACKET_HISTORY_SIZE];
    if (!is_packet_empty(entry) && (GameTurnDelta)(entry->turn - packet->turn) > 0) {
        return;
    }
    *entry = *packet;
}

const struct Packet *get_history_packet(PlayerNumber player, GameTurn turn)
{
    if (player < 0 || player >= MAX_N_USERS) {
        return NULL;
    }

    const struct Packet *packet = &packet_history[player].entries[turn % PACKET_HISTORY_SIZE];
    if (is_packet_empty(packet) || packet->turn != turn) {
        return NULL;
    }
    return packet;
}

TbBool read_packet_history(NetUserId source, const char *buffer, size_t buffer_size)
{
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
    if (source != SERVER_ID && source != header.player) {
        WARNLOG("Peer %i tried to send gameplay packet history for peer %i", source, (int)header.player);
        return false;
    }
    if (buffer_size != sizeof(struct PacketHistoryHeader) + header.compressed_length) {
        WARNLOG("Gameplay packet history from peer %i had size mismatch (%u != %u + %u)",
            source, (unsigned)buffer_size, (unsigned)sizeof(struct PacketHistoryHeader), header.compressed_length);
        return false;
    }
    if (header.original_length > sizeof(struct RedundantPacketBundle) || header.original_length < sizeof(unsigned char)) {
        WARNLOG("Gameplay packet history from peer %i had invalid original length %u", source, header.original_length);
        return false;
    }

    char packet_history_buffer[sizeof(struct RedundantPacketBundle)];
    uLongf packet_history_size = header.original_length;
    int uncompress_result = uncompress((Bytef *)packet_history_buffer, &packet_history_size,
        (const Bytef *)(buffer + sizeof(struct PacketHistoryHeader)), header.compressed_length);
    if (uncompress_result != Z_OK || packet_history_size != header.original_length) {
        WARNLOG("Gameplay packet history decompression from peer %i failed: zlib error %d", source, uncompress_result);
        return false;
    }

    const struct RedundantPacketBundle *packet_bundle = (const struct RedundantPacketBundle *)packet_history_buffer;
    if (packet_bundle->valid_count < 1 || packet_bundle->valid_count > PACKET_HISTORY_SIZE) {
        WARNLOG("Gameplay packet history from peer %i had invalid packet count %u", source, (unsigned)packet_bundle->valid_count);
        return false;
    }
    if (header.original_length < sizeof(unsigned char) + packet_bundle->valid_count * sizeof(struct Packet)) {
        WARNLOG("Gameplay packet history from peer %i was truncated for player %d", source, (int)header.player);
        return false;
    }
    for (unsigned char i = 0; i < packet_bundle->valid_count; i += 1) {
        const struct Packet *packet = &packet_bundle->packets[i];
        if (is_packet_empty(packet)) {
            MULTIPLAYER_LOG("read_packet_history: Skipping empty packet for player %d turn %lu", header.player, (unsigned long)packet->turn);
            continue;
        }
        store_packet_history(header.player, packet);
    }
    return true;
}

void initialize_packet_history(void)
{
    memset(packet_history, 0, sizeof(packet_history));
    last_packet_history_send = last_host_resend = 0;
    next_history_peer = SERVER_ID;
}

static TbBool have_all_turn_packets(PlayerNumber local_packet_player)
{
    GameTurn expected_turn = get_gameturn() - game.input_lag_turns;
    for (PlayerNumber player = 0; player < NET_PLAYERS_COUNT; player += 1) {
        const struct Packet *packet = get_history_packet(player, expected_turn);
        if (player != local_packet_player && network_player_active(player) && packet == NULL) {
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
        return true;
    }
    if (current_time - *last_send_time < interval) {
        return false;
    }
    *last_send_time = current_time;
    return true;
}

static TbBool host_lost(GameTurn turn, const char *state)
{
    if (netstate.my_id == SERVER_ID || netstate.users[SERVER_ID].progress != USER_UNUSED) {
        return false;
    }
    MULTIPLAYER_LOG("LbNetwork_ExchangePackets: Host disconnected while %s turn=%lu", state, (unsigned long)turn);
    netstate.seq_nbr += 1;
    return true;
}

static void send_history_to(NetUserId peer_id, PlayerNumber player)
{
    if (!can_send_to_peer(peer_id)) {
        return;
    }
    if (player < 0 || player >= MAX_N_USERS) {
        return;
    }

    struct RedundantPacketBundle packet_bundle;
    packet_bundle.valid_count = 0;

    const struct PacketHistory *history = &packet_history[player];
    GameTurn latest_turn = 0;
    TbBool have_turn = false;
    for (int i = 0; i < PACKET_HISTORY_SIZE; i += 1) {
        const struct Packet *packet = &history->entries[i];
        if (is_packet_empty(packet)) {
            continue;
        }
        if (!have_turn || (GameTurnDelta)(packet->turn - latest_turn) > 0) {
            latest_turn = packet->turn;
            have_turn = true;
        }
    }
    if (!have_turn) {
        return;
    }
    for (GameTurnDelta offset = 0; offset < PACKET_HISTORY_SIZE; offset += 1) {
        if ((GameTurn)offset > latest_turn) {
            break;
        }
        GameTurn turn = latest_turn - offset;
        const struct Packet *packet = &history->entries[turn % PACKET_HISTORY_SIZE];
        if (is_packet_empty(packet) || packet->turn != turn) {
            continue;
        }
        packet_bundle.packets[packet_bundle.valid_count] = *packet;
        packet_bundle.valid_count += 1;
    }
    size_t packet_history_size = sizeof(unsigned char) + packet_bundle.valid_count * sizeof(struct Packet);

    char *write_pos = begin_net_message(NETMSG_GAMEPLAY_PACKET_HISTORY);
    struct PacketHistoryHeader *header = (struct PacketHistoryHeader *)write_pos;
    write_pos += sizeof(struct PacketHistoryHeader);
    uLongf compressed_size = sizeof(netstate.msg_buffer) - (write_pos - netstate.msg_buffer);
    int compress_result = compress((Bytef *)write_pos, &compressed_size, (const Bytef *)&packet_bundle, packet_history_size);
    if (compress_result != Z_OK) {
        ERRORLOG("Gameplay packet history compression failed for player %d: zlib error %d", (int)player, compress_result);
        return;
    }
    header->player = player;
    header->compressed_length = (unsigned int)compressed_size;
    header->original_length = (unsigned int)packet_history_size;
    MULTIPLAYER_LOG("Sending reliable compressed gameplay packet history for player=%d to peer=%d (%lu -> %lu bytes)",
        (int)player, (int)peer_id, (unsigned long)packet_history_size, (unsigned long)compressed_size);
    netstate.sp->sendmsg_single(peer_id, netstate.msg_buffer, (write_pos - netstate.msg_buffer) + compressed_size);
}

static void send_wait_history(void)
{
    if (netstate.my_id != SERVER_ID) {
        if (send_due(&last_packet_history_send, SEND_HISTORY_COOLDOWN_MS)) {
            PlayerNumber player = (PlayerNumber)netstate.my_id;
            if (player >= 0 && player < MAX_N_USERS) {
                send_history_to(SERVER_ID, player);
            }
        }
        return;
    }

    int active_peers = 0;
    for (NetUserId peer_id = 0; peer_id < netstate.max_players; peer_id += 1) {
        active_peers += can_send_to_peer(peer_id);
    }
    if (active_peers <= 0) {
        return;
    }

    TbClockMSec stagger_interval = SEND_HISTORY_COOLDOWN_MS / active_peers;
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
        for (PlayerNumber player = 0; player < netstate.max_players; player += 1) {
            if (player == peer_id || !network_player_active(player)) {
                continue;
            }
            send_history_to(peer_id, player);
        }
        return;
    }
}

TbError LbNetwork_ExchangePackets(void *send_buf, void *server_buf, size_t frame_size)
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
                MULTIPLAYER_LOG("LbNetwork_ExchangePackets: Missing packets for turn=%lu, collecting...", (unsigned long)expected_turn);

                while (!turn_complete) {
                    send_wait_history();
                    netstate.sp->update(OnNewUser);
                    if (host_lost(expected_turn, "waiting for")) {
                        return Lb_OK;
                    }
                    turn_complete = have_all_turn_packets(local_packet_player);
                    for (NetUserId peer_id = 0; peer_id < netstate.max_players && !turn_complete; peer_id += 1) {
                        const struct Packet *packet = get_history_packet(peer_id, expected_turn);
                        if (!can_send_to_peer(peer_id)) {
                            continue;
                        }
                        if (netstate.my_id == SERVER_ID && packet != NULL) {
                            continue;
                        }
                        process_peer_msgs(peer_id, server_buf, frame_size);
                        if (host_lost(expected_turn, "collecting")) {
                            return Lb_OK;
                        }
                        turn_complete = have_all_turn_packets(local_packet_player);
                    }
                    if (turn_complete) {
                        break;
                    }
                    if (netstate.my_id == SERVER_ID && send_due(&last_host_resend, SEND_HISTORY_COOLDOWN_MS)) {
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
                if (netstate.my_id != SERVER_ID) {
                    sync_mp_client_ns = 1000000;
                }
                MULTIPLAYER_LOG("LbNetwork_ExchangePackets: Completed wait for turn=%lu after %dms", (unsigned long)expected_turn, (int32_t)(LbTimerClock() - wait_start_time));
            } else if (netstate.my_id != SERVER_ID) {
                sync_mp_client_ns = -1000000;
            }
        }
    }
    netstate.seq_nbr += 1;
    return Lb_OK;
}
