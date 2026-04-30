/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file bflib_network_exchange_impl.h
 *     Internal network exchange helpers shared between split modules.
 * @par Purpose:
 *     Internal declarations for multiplayer exchange routines.
 * @par Comment:
 *     Shared only between network exchange implementation files.
 * @author   KeeperFX Team
 * @date     30 Apr 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_NET_EXCHANGE_IMPL_H
#define DK_NET_EXCHANGE_IMPL_H

#include "bflib_basics.h"
#include "bflib_network.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Packet;

TbBool can_send_to_peer(NetUserId peer_id);
TbError exchange_frame_message(void *send_buf, void *server_buf, size_t frame_size, enum NetMessageType msg_type);
TbError process_network_message(NetUserId source, void *server_buf, size_t frame_size, enum NetMessageType expected_frame_type);
void process_peer_msgs(NetUserId peer_id, void *server_buf, size_t frame_size);

size_t gameplay_build_payload(PlayerNumber player, const struct Packet *first_packet, void *buffer);
TbBool gameplay_unpack_payload(const char *buffer, size_t buffer_size, PlayerNumber player, void *out_packet, size_t packet_size);
TbBool gameplay_read_history(NetUserId source, const char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif
