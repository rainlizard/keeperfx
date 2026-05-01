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

// Bundle size counts the current packet plus older history packets.
#define REDUNDANT_PACKET_BUNDLE 2
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

static TbError ignore_frame(void)
{
    netstate.msg_buffer[0] = 0x7F;
    return Lb_OK;
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
        if (id == first_skip_id || id == second_skip_id || !can_send_to_peer(id)) {
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
        const struct Packet *current_packet = (const struct Packet *)send_buf;
        unsigned char *packet_count = (unsigned char *)write_pos;
        *packet_count = 1;
        write_pos += 1;
        memcpy(write_pos, current_packet, sizeof(struct Packet));
        write_pos += sizeof(struct Packet);
        for (GameTurnDelta offset = 1; *packet_count < REDUNDANT_PACKET_BUNDLE; offset += 1) {
            if ((GameTurn)offset > current_packet->turn) {
                break;
            }
            const struct Packet *history_packet = get_history_packet((PlayerNumber)netstate.my_id, current_packet->turn - offset);
            if (history_packet == NULL) {
                continue;
            }
            memcpy(write_pos, history_packet, sizeof(struct Packet));
            write_pos += sizeof(struct Packet);
            *packet_count += 1;
        }
    } else {
        memcpy(write_pos, send_buf, frame_size);
        write_pos += frame_size;
    }
    size_t message_size = write_pos - netstate.msg_buffer;
    TbBool unsequenced = (msg_type == NETMSG_GAMEPLAY_UNSEQUENCED);
    send_to_active_peers(INVALID_USER_ID, INVALID_USER_ID, netstate.msg_buffer, message_size, unsequenced);
    return Lb_OK;
}

static TbError process_network_message(NetUserId source, void *server_buf, size_t frame_size, enum NetMessageType expected_frame_type)
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
    if (message_type == NETMSG_FRONTEND
     || message_type == NETMSG_STARTUP_SYNC
     || message_type == NETMSG_GAMEPLAY_UNSEQUENCED) {
        if (message_type != expected_frame_type) {
            return Lb_OK;
        }
        size_t header_size = read_pos - netstate.msg_buffer;
        if (message_size < header_size + 5) {
            WARNLOG("Frame message type %d from %i is too short", (int)message_type, (int)source);
            return ignore_frame();
        }
        NetUserId peer_id = (NetUserId)*read_pos;
        if (peer_id < 0 || peer_id >= netstate.max_players) {
            ERRORLOG("Critical error: Out of range peer ID %i received, could be used for buffer overflow attack", peer_id);
            abort();
        }
        if (source != SERVER_ID && source != peer_id) {
            WARNLOG("Peer %i tried to send gameplay frame for peer %i", source, peer_id);
            return ignore_frame();
        }
        char *player_frame = ((char *)server_buf) + peer_id * frame_size;
        read_pos += 1;
        netstate.users[peer_id].ack = *(int *)read_pos;
        read_pos += 4;
        size_t payload_size = message_size - (read_pos - netstate.msg_buffer);
        if (message_type == NETMSG_GAMEPLAY_UNSEQUENCED) {
            if (frame_size != sizeof(struct Packet)) {
                WARNLOG("Gameplay frame size mismatch (%u != %u)", (unsigned)frame_size, (unsigned)sizeof(struct Packet));
                return ignore_frame();
            }
            if (payload_size < sizeof(unsigned char)) {
                WARNLOG("Invalid gameplay packet bundle from peer %i (%u bytes)", peer_id, (unsigned)payload_size);
                return ignore_frame();
            }
            unsigned char packet_count = *(const unsigned char *)read_pos;
            if (packet_count < 1 || packet_count > REDUNDANT_PACKET_BUNDLE
             || payload_size != sizeof(unsigned char) + packet_count * sizeof(struct Packet)) {
                WARNLOG("Invalid gameplay packet bundle from peer %i (%u bytes)", peer_id, (unsigned)payload_size);
                return ignore_frame();
            }
            read_pos += 1;
            const struct Packet *packets = (const struct Packet *)read_pos;
            for (unsigned char i = 0; i < packet_count; i += 1) {
                if (is_packet_empty(&packets[i])) {
                    MULTIPLAYER_LOG("process_network_message: Skipping empty packet for player %d turn %lu", peer_id, (unsigned long)packets[i].turn);
                    continue;
                }
                store_packet_history((PlayerNumber)peer_id, &packets[i]);
            }
            *(struct Packet *)player_frame = packets[0];
        } else {
            if (payload_size != frame_size) {
                WARNLOG("Ignoring frame message type %d from peer %i with %u bytes, expected %u bytes",
                    (int)message_type, peer_id, (unsigned)payload_size, (unsigned)frame_size);
                return ignore_frame();
            }
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
        read_packet_history(source, read_pos, payload_size);
        return Lb_OK;
    }
    return Lb_OK;
}

void process_peer_msgs(NetUserId peer_id, void *server_buf, size_t frame_size)
{
    while (netstate.sp->msgready(peer_id, 0)) {
        process_network_message(peer_id, server_buf, frame_size, NETMSG_GAMEPLAY_UNSEQUENCED);
    }
}

static void drain_wait_msgs(void *server_buf, size_t frame_size, enum NetMessageType msg_type, TbBool *has_received_frame)
{
    for (NetUserId peer_id = 0; peer_id < netstate.max_players; peer_id += 1) {
        if (!can_send_to_peer(peer_id)) {
            continue;
        }
        while (netstate.sp->msgready(peer_id, 0)) {
            process_network_message(peer_id, server_buf, frame_size, msg_type);
            if ((enum NetMessageType)netstate.msg_buffer[0] != msg_type) {
                continue;
            }
            NetUserId frame_peer_id = (unsigned char)netstate.msg_buffer[1];
            if (frame_peer_id < netstate.max_players) {
                has_received_frame[frame_peer_id] = true;
            }
        }
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
    while (LbTimerClock() - wait_start_time < TIMEOUT_JOIN_LOBBY) {
        if (netstate.sp->msgready(SERVER_ID, 0)) {
            if (process_network_message(SERVER_ID, &net_screen_packet, sizeof(struct ScreenPacket), NETMSG_FRONTEND) == Lb_FAIL
             || netstate.msg_buffer[0] == NETMSG_LOGIN) {
                break;
            }
            continue;
        }
        netstate.sp->update(reject_new_user);
        if (!netstate.sp->msgready(SERVER_ID, 0)) {
            SDL_Delay(1);
        }
    }
    if (netstate.msg_buffer[0] != NETMSG_LOGIN) {
        return Lb_FAIL;
    }
    if (netstate.sp->msgready(SERVER_ID, TIMEOUT_JOIN_LOBBY)) {
        process_network_message(SERVER_ID, &net_screen_packet, sizeof(struct ScreenPacket), NETMSG_FRONTEND);
    }
    if (netstate.my_id == INVALID_USER_ID) {
        return Lb_FAIL;
    }
    return Lb_OK;
}

TbError LbNetwork_ExchangeWithWait(enum NetMessageType msg_type, void *send_buf, void *server_buf, size_t frame_size)
{
    if (exchange_frame_message(send_buf, server_buf, frame_size, msg_type) != Lb_OK) {
        return Lb_FAIL;
    }

    TbBool is_frontend_msg = (msg_type == NETMSG_FRONTEND);
    TbBool should_stop_waiting = is_frontend_msg && host_started_level(server_buf, frame_size);
    TbBool has_received_frame[MAX_N_USERS] = {false};
    for (NetUserId peer_id = 0; peer_id < netstate.max_players; peer_id += 1) {
        if (!can_send_to_peer(peer_id)) {
            continue;
        }

        TbClockMSec wait_start_time = LbTimerClock();
        while (netstate.users[peer_id].progress != USER_UNUSED && !should_stop_waiting) {
            if (LbTimerClock() - wait_start_time >= TIMEOUT_LOBBY_EXCHANGE) {
                break;
            }

            TbBool peer_has_frame = false;
            for (int pass = 0; pass < 2; pass += 1) {
                if (pass > 0) {
                    netstate.sp->update(OnNewUser);
                }
                drain_wait_msgs(server_buf, frame_size, msg_type, has_received_frame);
                if (is_frontend_msg) {
                    should_stop_waiting = should_stop_waiting || host_started_level(server_buf, frame_size);
                }
                if (my_player_number == get_host_player_id()) {
                    peer_has_frame = has_received_frame[peer_id];
                } else {
                    peer_has_frame = true;
                    for (NetUserId id = 0; id < netstate.max_players; id += 1) {
                        if (id == netstate.my_id || !IsUserActive(id)) {
                            continue;
                        }
                        if (!has_received_frame[id]) {
                            peer_has_frame = false;
                            break;
                        }
                    }
                }
                if (should_stop_waiting || peer_has_frame) {
                    break;
                }
            }
            if (should_stop_waiting || peer_has_frame) {
                break;
            }
            if (is_frontend_msg) {
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
