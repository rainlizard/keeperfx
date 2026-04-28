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
#include "net_packet_history.h"
#include "game_legacy.h"
#include "packets.h"
#include "keeperfx.hpp"
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

char* InitMessageBuffer(enum NetMessageType msg_type)
{
    char* ptr = netstate.msg_buffer;
    *ptr = msg_type;
    return ptr + 1;
}

// Clients send directly only to the host; the host relays frames to everyone else.
static TbBool can_send_directly_to_peer(NetUserId peer_id)
{
    return (peer_id != netstate.my_id) &&
        (netstate.users[peer_id].progress != USER_UNUSED) &&
        (my_player_number == get_host_player_id() || peer_id == SERVER_ID);
}

static TbBool host_has_left(void)
{
    return (netstate.my_id != SERVER_ID) && (netstate.users[SERVER_ID].progress == USER_UNUSED);
}

static TbBool frontend_start_received(void *server_buf, size_t client_frame_size)
{
    if (client_frame_size != sizeof(struct ScreenPacket)) {
        return false;
    }
    const struct ScreenPacket* host_packet = &((const struct ScreenPacket*)server_buf)[get_host_player_id()];
    return ((host_packet->networkstatus_flags & NetStat_PlayerConnected) != 0)
        && (screen_packet_action(host_packet) == NetAct_HostStartLevel)
        && (host_packet->action_par1 > 0);
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

static char* prepare_frame_message_header(enum NetMessageType msg_type, NetUserId source_id, int seq_nbr)
{
    char* ptr = InitMessageBuffer(msg_type);
    *ptr = source_id;
    ptr += 1;
    *(int*)ptr = seq_nbr;
    ptr += 4;
    return ptr;
}

void SendFrameToPeers(NetUserId source_id, const void* send_buf, size_t buf_size, int seq_nbr, enum NetMessageType msg_type)
{
    char* ptr = prepare_frame_message_header(msg_type, source_id, seq_nbr);
    if (msg_type == NETMSG_GAMEPLAY_UNSEQUENCED) {
        ptr += build_packet_bundle((PlayerNumber)source_id, (const struct Packet*)send_buf, ptr);
    } else {
        memcpy(ptr, send_buf, buf_size);
        ptr += buf_size;
    }

    size_t msg_size = ptr - netstate.msg_buffer;
    TbBool unsequenced = (msg_type == NETMSG_GAMEPLAY_UNSEQUENCED);
    for (NetUserId id = 0; id < netstate.max_players; id += 1) {
        if (id == source_id) { continue; }
        if (!can_send_directly_to_peer(id)) { continue; }
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
        size_t max_read = sizeof(netstate.msg_buffer) - (ptr - netstate.msg_buffer);
        size_t password_len = strnlen(ptr, max_read);
        if (password_len >= max_read || password_len > sizeof(netstate.password)) {
            NETDBG(6, "Connected peer sent invalid password");
            netstate.sp->drop_user(source);
            return Lb_OK;
        }
        if (netstate.password[0] != 0 && strncmp(ptr, netstate.password, sizeof(netstate.password)) != 0) {
            NETMSG("Peer chose wrong password");
            return Lb_OK;
        }
        ptr += password_len + 1;
        max_read = sizeof(netstate.msg_buffer) - (ptr - netstate.msg_buffer);
        size_t name_len = strnlen(ptr, max_read);
        if (name_len == 0 || name_len >= max_read || name_len >= sizeof(netstate.users[source].name)) {
            NETDBG(6, "Connected peer sent invalid name");
            netstate.sp->drop_user(source);
            return Lb_OK;
        }
        snprintf(netstate.users[source].name, sizeof(netstate.users[source].name), "%s", ptr);
        if (!isalnum(netstate.users[source].name[0])) {
            NETDBG(6, "Connected peer had bad name starting with %c", netstate.users[source].name[0]);
            netstate.sp->drop_user(source);
            return Lb_OK;
        }
        ptr += name_len + 1;
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
        if (strnlen(ptr, sizeof(netstate.msg_buffer) - (ptr - netstate.msg_buffer)) >= sizeof(netstate.msg_buffer) - (ptr - netstate.msg_buffer)) {
            ERRORLOG("Critical error: Unterminated name in USERUPDATE");
            abort();
        }
        snprintf(netstate.users[id].name, sizeof(netstate.users[id].name), "%s", ptr);
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
            for (NetUserId id = 0; id < netstate.max_players; id += 1) {
                if (id == netstate.my_id || id == peer_id || !IsUserActive(id)) { continue; }
                send_network_message(id, netstate.msg_buffer, msg_size, unsequenced);
            }
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
        size_t max_read = sizeof(netstate.msg_buffer) - (ptr - netstate.msg_buffer);
        size_t msg_len = strnlen(ptr, max_read);
        if (msg_len >= max_read) {
            ERRORLOG("Chat message too long or not null-terminated");
            return Lb_OK;
        }
        process_chat_message_end(player_id, ptr);
        if (netstate.my_id == SERVER_ID && source != SERVER_ID) {
            for (NetUserId id = 0; id < netstate.max_players; id += 1) {
                if (id == netstate.my_id || id == source || !IsUserActive(id)) { continue; }
                netstate.sp->sendmsg_single(id, netstate.msg_buffer, msg_size);
            }
        }
        return Lb_OK;
    }
    if (type == NETMSG_GAMEPLAY_PACKET_HISTORY) {
        receive_packet_history(source, source == SERVER_ID, ptr, msg_size, server_buf, frame_size);
        return Lb_OK;
    }
    return Lb_OK;
}

static void collect_messages_from_peer(NetUserId peer_id, void *server_buf, size_t frame_size)
{
    while (netstate.sp->msgready(peer_id, 0)) {
        ProcessMessage(peer_id, server_buf, frame_size);
    }
}

static void resend_missing_gameplay_packets_until_received(void *server_buf, size_t frame_size)
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
        if (host_has_left()) {
            MULTIPLAYER_LOG("LbNetwork_ExchangeGameplay: Host disconnected while waiting for turn=%lu", (unsigned long)historical_turn);
            return;
        }
        for (NetUserId peer_id = 0; peer_id < netstate.max_players; peer_id += 1) {
            if (!can_send_directly_to_peer(peer_id)) {
                continue;
            }
            if (netstate.my_id == SERVER_ID && have_received_player_packet(historical_turn, (PlayerNumber)peer_id)) {
                continue;
            }
            collect_messages_from_peer(peer_id, server_buf, frame_size);
            if (host_has_left()) {
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
        if (can_send_directly_to_peer(peer_id)) {
            collect_messages_from_peer(peer_id, server_buf, client_frame_size);
        }
    }
    sync_packet_history();
    resend_missing_gameplay_packets_until_received(server_buf, client_frame_size);
    netstate.seq_nbr += 1;
    return Lb_OK;
}

TbError LbNetwork_ExchangeWithWait(enum NetMessageType msg_type, void *send_buf, void *server_buf, size_t client_frame_size)
{
    if (send_exchange_data(send_buf, server_buf, client_frame_size, msg_type) != Lb_OK) {
        return Lb_FAIL;
    }

    TbBool stop_waiting = (msg_type == NETMSG_FRONTEND) && frontend_start_received(server_buf, client_frame_size);
    TbBool received_frames[MAX_N_USERS] = {false};
    for (NetUserId peer_id = 0; peer_id < netstate.max_players; peer_id += 1) {
        if (!can_send_directly_to_peer(peer_id)) {
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
                stop_waiting = (msg_type == NETMSG_FRONTEND) && frontend_start_received(server_buf, client_frame_size);
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
    if (netstate.my_id != SERVER_ID) {
        if (IsUserActive(SERVER_ID)) {
            SendMessage(SERVER_ID, ptr);
        }
        return;
    }
    for (NetUserId id = 0; id < netstate.max_players; id += 1) {
        if (id != netstate.my_id && IsUserActive(id)) {
            SendMessage(id, ptr);
        }
    }
}

void LbNetwork_BroadcastUnpauseTimesync(void)
{
    MULTIPLAYER_LOG("LbNetwork_BroadcastUnpauseTimesync");
    InitMessageBuffer(NETMSG_UNPAUSE);
    if (netstate.my_id != SERVER_ID) {
        if (IsUserActive(SERVER_ID)) {
            netstate.sp->sendmsg_single(SERVER_ID, netstate.msg_buffer, 1);
        }
        return;
    }
    for (NetUserId id = 0; id < netstate.max_players; id += 1) {
        if (id != netstate.my_id && IsUserActive(id)) {
            netstate.sp->sendmsg_single(id, netstate.msg_buffer, 1);
        }
    }
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
