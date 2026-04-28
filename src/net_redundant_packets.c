/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file redundant_packets.c
 *     Redundant packet bundling for network game packet loss prevention.
 * @par Purpose:
 *     Bundles current packet with recent packet history to prevent packet loss.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     11 Nov 2025
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "net_redundant_packets.h"
#include "packets.h"
#include "net_received_packets.h"
#include "bflib_network.h"
#include "game_legacy.h"
#include <string.h>
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
struct PacketHistory {
    struct Packet packets[REDUNDANT_PACKET_MAX_COUNT];
    unsigned char write_index;
    unsigned char valid_count;
};

static struct PacketHistory packet_history[MAX_N_USERS];

/******************************************************************************/

static TbBool valid_packet_history_player(PlayerNumber player)
{
    return player >= 0 && player < MAX_N_USERS;
}

static size_t bundled_packet_size(unsigned char valid_count)
{
    return sizeof(unsigned char) + valid_count * sizeof(struct Packet);
}

void initialize_redundant_packets(void)
{
    memset(packet_history, 0, sizeof(packet_history));
}

static void store_packet_in_history(PlayerNumber player, const struct Packet* packet)
{
    if (!valid_packet_history_player(player)) {
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
    history->write_index = (history->write_index + 1) % REDUNDANT_PACKET_MAX_COUNT;
    if (history->valid_count < REDUNDANT_PACKET_MAX_COUNT) {
        history->valid_count += 1;
    }
}

static unsigned char copy_packet_history_to_bundle(struct BundledPacket* bundled, const struct PacketHistory* history, unsigned char packet_index, unsigned char packet_count, const struct Packet* skipped_packet)
{
    unsigned char copied_count = 0;
    for (unsigned char i = 0; i < history->valid_count && copied_count < packet_count; i += 1) {
        int32_t history_index = (history->write_index + REDUNDANT_PACKET_MAX_COUNT - i - 1) % REDUNDANT_PACKET_MAX_COUNT;
        if (skipped_packet != NULL && history->packets[history_index].turn == skipped_packet->turn) {
            continue;
        }
        bundled->packets[packet_index + copied_count] = history->packets[history_index];
        copied_count += 1;
    }
    return copied_count;
}

size_t bundle_current_packet_with_redundant_history(PlayerNumber player, const struct Packet* current_packet, char* out_buffer)
{
    if (!valid_packet_history_player(player)) {
        return 0;
    }
    struct PacketHistory* history = &packet_history[player];
    struct BundledPacket* bundled = (struct BundledPacket*)out_buffer;
    int32_t bundle_count = game.input_lag_turns + 1;
    if (bundle_count < 1) {
        bundle_count = 1;
    }
    if (bundle_count > REDUNDANT_PACKET_MAX_COUNT) {
        bundle_count = REDUNDANT_PACKET_MAX_COUNT;
    }

    bundled->packets[0] = *current_packet;
    bundled->valid_count = 1 + copy_packet_history_to_bundle(bundled, history, 1, bundle_count - 1, current_packet);
    store_packet_in_history(player, current_packet);
    return bundled_packet_size(bundled->valid_count);
}

size_t bundle_redundant_packet_history(PlayerNumber player, char* out_buffer)
{
    if (!valid_packet_history_player(player)) {
        return 0;
    }
    struct PacketHistory* history = &packet_history[player];
    if (history->valid_count == 0) {
        return 0;
    }
    struct BundledPacket* bundled = (struct BundledPacket*)out_buffer;
    bundled->valid_count = copy_packet_history_to_bundle(bundled, history, 0, history->valid_count, NULL);
    return bundled_packet_size(bundled->valid_count);
}

TbBool store_redundant_packets_from_message(const char* bundled_buffer, size_t bundled_buffer_size, PlayerNumber source_player)
{
    if (!valid_packet_history_player(source_player) || bundled_buffer_size < sizeof(unsigned char)) {
        return false;
    }
    const struct BundledPacket* bundled = (const struct BundledPacket*)bundled_buffer;
    if (bundled->valid_count < 1 || bundled->valid_count > REDUNDANT_PACKET_MAX_COUNT) {
        return false;
    }
    if (bundled_buffer_size < bundled_packet_size(bundled->valid_count)) {
        return false;
    }
    for (unsigned char i = 0; i < bundled->valid_count; i += 1) {
        const struct Packet* packet = &bundled->packets[i];
        store_packet_in_history(source_player, packet);
        if (!have_received_packet_from_player(packet->turn, source_player)) {
            store_received_packet(packet->turn, source_player, packet);
        }
    }
    return true;
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
