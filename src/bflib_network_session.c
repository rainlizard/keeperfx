/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file bflib_network_session.c
 *     Session-level network data exchange for Dungeon Keeper multiplayer.
 * @par Purpose:
 *     Login, relay and synchronized frame exchange routines.
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
#include "bflib_sound.h"
#include "bflib_video.h"
#include "engine_redraw.h"
#include "globals.h"
#include "player_data.h"
#include "net_game.h"
#include "net_resync.h"
#include "front_landview.h"
#include "game_legacy.h"
#include "packets.h"
#include "post_inc.h"

extern void network_yield_draw_gameplay(void);
extern void network_yield_draw_frontend(void);

/******************************************************************************/

#define SEND_DUPLICATE_PACKETS 3

static TbBool read_msg_text(char **read_pos, const char **text, size_t max_len)
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

static TbBool reject_new_user(NetUserId *)
{
    return false;
}

static TbBool host_started_level(const void *server_buf, size_t frame_size)
{
    if (frame_size != sizeof(struct ScreenPacket)) {
        return false;
    }
    const struct ScreenPacket *host_packet = &((const struct ScreenPacket *)server_buf)[get_host_player_id()];
    return ((host_packet->networkstatus_flags & NetStat_PlayerConnected) != 0)
        && (screen_packet_action(host_packet) == NetAct_HostStartLevel)
        && (host_packet->action_par1 > 0);
}

TbBool can_send_to_peer(NetUserId peer_id)
{
    return (peer_id != netstate.my_id) &&
        (netstate.users[peer_id].progress != USER_UNUSED) &&
        (my_player_number == get_host_player_id() || peer_id == SERVER_ID);
}

static void send_network_message(NetUserId destination, const char *buffer, size_t msg_size, TbBool unsequenced)
{
    if (!unsequenced) {
        netstate.sp->sendmsg_single(destination, buffer, msg_size);
        return;
    }

    for (int i = 0; i < SEND_DUPLICATE_PACKETS; i += 1) {
        netstate.sp->sendmsg_single_unsequenced(destination, buffer, msg_size);
    }
}

static void send_to_active_peers(NetUserId first_skip_id, NetUserId second_skip_id, const char *buffer, size_t msg_size, TbBool unsequenced)
{
    for (NetUserId id = 0; id < netstate.max_players; id += 1) {
        if (id == first_skip_id || id == second_skip_id || !IsUserActive(id)) {
            continue;
        }
        send_network_message(id, buffer, msg_size, unsequenced);
    }
}

TbError exchange_frame_message(void *send_buf, void *server_buf, size_t frame_size, enum NetMessageType msg_type)
{
    if (netstate.my_id < 0 || netstate.my_id >= netstate.max_players) {
        ERRORLOG("Invalid my_id %i in network exchange (disconnected?)", netstate.my_id);
        return Lb_FAIL;
    }
    netstate.sp->update(OnNewUser);
    memcpy(((char *)server_buf) + netstate.my_id * frame_size, send_buf, frame_size);
    char *write_pos = begin_net_message(msg_type);
    *write_pos = netstate.my_id;
    write_pos += 1;
    *(int *)write_pos = netstate.seq_nbr;
    write_pos += 4;
    if (msg_type == NETMSG_GAMEPLAY_UNSEQUENCED) {
        size_t payload_size = gameplay_build_payload((PlayerNumber)netstate.my_id, (const struct Packet *)send_buf, write_pos);
        if (payload_size > 0) {
            write_pos += payload_size;
        }
    } else {
        memcpy(write_pos, send_buf, frame_size);
        write_pos += frame_size;
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

TbError process_network_message(NetUserId source, void *server_buf, size_t frame_size)
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
        const char *password;
        if (!read_msg_text(&read_pos, &password, sizeof(netstate.password))) {
            NETDBG(6, "Connected peer sent invalid password");
            netstate.sp->drop_user(source);
            return Lb_OK;
        }
        if (netstate.password[0] != 0 && strncmp(password, netstate.password, sizeof(netstate.password)) != 0) {
            NETMSG("Peer chose wrong password");
            return Lb_OK;
        }
        const char *name;
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
        const char *name;
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
        char *player_frame = ((char *)server_buf) + peer_id * frame_size;
        read_pos += 1;
        netstate.users[peer_id].ack = *(int *)read_pos;
        read_pos += 4;
        if (message_type == NETMSG_GAMEPLAY_UNSEQUENCED) {
            size_t payload_size = message_size - (read_pos - netstate.msg_buffer);
            if (!gameplay_unpack_payload(read_pos, payload_size, (PlayerNumber)peer_id, player_frame, frame_size)) {
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
        MULTIPLAYER_LOG("ProcessMessage NETMSG_UNPAUSE: applying unpause");
        unpausing_in_progress = 1;
        keeper_screen_redraw();
        LbScreenSwap();
        if (my_player_number == get_host_player_id()) {
            LbNetwork_BroadcastUnpause();
        }
        process_pause_packet(0, 0);
        unpausing_in_progress = 0;
        return Lb_OK;
    }
    if (message_type == NETMSG_CHATMESSAGE) {
        int player_id = (int)*read_pos;
        read_pos += 1;
        const char *message;
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
        size_t payload_size = message_size - (read_pos - netstate.msg_buffer);
        gameplay_read_history(source, read_pos, payload_size, server_buf, frame_size);
        return Lb_OK;
    }
    return Lb_OK;
}

void process_peer_msgs(NetUserId peer_id, void *server_buf, size_t frame_size)
{
    while (netstate.sp->msgready(peer_id, 0)) {
        process_network_message(peer_id, server_buf, frame_size);
    }
}

static void process_wait_msgs(NetUserId peer_id, void *server_buf, size_t frame_size, enum NetMessageType msg_type, TbBool *has_received_frame)
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

static void process_all_wait_msgs(void *server_buf, size_t frame_size, enum NetMessageType msg_type, TbBool *has_received_frame)
{
    for (NetUserId peer_id = 0; peer_id < netstate.max_players; peer_id += 1) {
        if (!can_send_to_peer(peer_id)) {
            continue;
        }
        process_wait_msgs(peer_id, server_buf, frame_size, msg_type, has_received_frame);
    }
}

static TbBool have_peer_frame(NetUserId peer_id, const TbBool *has_received_frame)
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
        if (netstate.sp->msgready(SERVER_ID, 0)) {
            if (process_network_message(SERVER_ID, &net_screen_packet, sizeof(struct ScreenPacket)) == Lb_FAIL) {
                NETMSG("ExchangeLogin: process_network_message failed");
                break;
            }
            if (netstate.msg_buffer[0] == NETMSG_LOGIN) {
                break;
            }
            continue;
        }
        netstate.sp->update(reject_new_user);
        if (netstate.sp->msgready(SERVER_ID, 0)) {
            continue;
        }
        SDL_Delay(1);
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

TbError LbNetwork_ExchangeWithWait(enum NetMessageType msg_type, void *send_buf, void *server_buf, size_t frame_size)
{
    if (exchange_frame_message(send_buf, server_buf, frame_size, msg_type) != Lb_OK) {
        return Lb_FAIL;
    }

    TbBool should_stop_waiting = false;
    if (msg_type == NETMSG_FRONTEND) {
        should_stop_waiting = host_started_level(server_buf, frame_size);
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

            process_all_wait_msgs(server_buf, frame_size, msg_type, has_received_frame);
            if (msg_type == NETMSG_FRONTEND) {
                should_stop_waiting = host_started_level(server_buf, frame_size);
            }
            if (have_peer_frame(peer_id, has_received_frame) || should_stop_waiting) {
                break;
            }

            netstate.sp->update(OnNewUser);
            process_all_wait_msgs(server_buf, frame_size, msg_type, has_received_frame);
            if (msg_type == NETMSG_FRONTEND) {
                should_stop_waiting = host_started_level(server_buf, frame_size);
            }

            if (have_peer_frame(peer_id, has_received_frame)) {
                break;
            }

            if (msg_type == NETMSG_FRONTEND) {
                network_yield_draw_frontend();
            } else {
                network_yield_draw_gameplay();
            }
            SDL_Delay(1);
        }
        if (should_stop_waiting) {
            break;
        }
    }
    netstate.seq_nbr += 1;
    return Lb_OK;
}
