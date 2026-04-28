/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file net_packet_history.h
 *     Header file for net_packet_history.c.
 * @par Purpose:
 *     Gameplay packet history storage, bundling, and recovery.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   KeeperFX Team
 * @date     30 Apr 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_NET_PACKET_HISTORY_H
#define DK_NET_PACKET_HISTORY_H

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_network.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
#define PACKET_HISTORY_SIZE 32

struct Packet;

void initialize_packet_history(void);
const struct Packet* get_received_turn_packets(GameTurn turn);
const struct Packet* get_received_player_packet(GameTurn turn, PlayerNumber player);
TbBool have_received_player_packet(GameTurn turn, PlayerNumber player);
TbBool have_received_all_packets(PlayerNumber local_packet_num);
size_t build_packet_bundle(PlayerNumber player, const struct Packet* current_packet, char* out_buffer);
TbBool read_packet_bundle(const char* packet_bundle_buffer, size_t packet_bundle_buffer_size, PlayerNumber source_player, void* out_packet, size_t packet_size);
void sync_packet_history(void);
TbBool receive_packet_history(NetUserId source, TbBool from_server, const char* buffer, size_t msg_size, void* server_buf, size_t frame_size);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
