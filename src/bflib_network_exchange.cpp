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
#include "net_received_packets.h"
#include "net_redundant_packets.h"
#include "net_input_lag.h"
#include "game_legacy.h"
#include "packets.h"
#include "keeperfx.hpp"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" void network_yield_draw_gameplay();
extern "C" void network_yield_draw_frontend();
extern "C" long double last_draw_completed_time;
extern "C" void LbNetwork_TimesyncBarrier(void);
extern "C" TbBool keeper_screen_redraw(void);
extern "C" TbResult LbScreenSwap(void);
long double get_time_tick_ns();
#endif

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

#define NETWORK_FPS 60

#pragma pack(1)
struct GameplaySnapshot {
    GameTurn turn;
    unsigned char active_players_mask;
    struct Packet packets[NET_PLAYERS_COUNT];
};
#pragma pack()

static GameTurn last_gameplay_snapshot_turn;
static GameTurn next_host_snapshot_turn;
static TbBool host_snapshot_turn_initialized;
static TbError ProcessMessage(NetUserId source, void* server_buf, size_t frame_size);

char* InitMessageBuffer(enum NetMessageType msg_type) {
    char* ptr = netstate.msg_buffer;
    *ptr = msg_type;
    return ptr + 1;
}

void SendMessage(NetUserId dest, const char* end_ptr) {
    netstate.sp->sendmsg_single(dest, netstate.msg_buffer, end_ptr - netstate.msg_buffer);
}

static TbBool should_exchange_with_user(NetUserId id)
{
    if (id == netstate.my_id) {
        return false;
    }
    if (netstate.users[id].progress == USER_UNUSED) {
        return false;
    }
    if (my_player_number != get_host_player_id() && id != SERVER_ID) {
        return false;
    }
    return true;
}

static TbError drain_pending_messages(void* server_buf, size_t frame_size)
{
    for (NetUserId id = 0; id < (NetUserId)netstate.max_players; id += 1) {
        if (!should_exchange_with_user(id)) {
            continue;
        }
        while (netstate.sp->msgready(id, 0)) {
            if (ProcessMessage(id, server_buf, frame_size) == Lb_FAIL) {
                return Lb_FAIL;
            }
        }
    }
    return Lb_OK;
}

static unsigned char get_gameplay_active_players_mask(void)
{
    unsigned char mask = 0;
    for (NetUserId id = 0; id < (NetUserId)netstate.max_players && id < NET_PLAYERS_COUNT; id += 1) {
        if (id == netstate.my_id || IsUserActive(id)) {
            mask |= (1u << id);
        }
    }
    return mask;
}

static void send_gameplay_upload_to_host(const struct Packet* send_packet, int seq_nbr)
{
    char *ptr = InitMessageBuffer(NETMSG_GAMEPLAY_UPLOAD);
    *ptr = netstate.my_id;
    ptr += 1;
    *(int *)ptr = seq_nbr;
    ptr += 4;
    ptr += bundle_packets((PlayerNumber)netstate.my_id, send_packet, ptr);
    SendMessage(SERVER_ID, ptr);
    store_sent_packet((PlayerNumber)netstate.my_id, send_packet);
}

static TbBool build_gameplay_snapshot_for_turn(GameTurn turn, struct GameplaySnapshot* snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->turn = turn;
    snapshot->active_players_mask = get_gameplay_active_players_mask();

    const struct Packet* received_packets = get_received_packets_for_turn(turn);
    for (NetUserId id = 0; id < (NetUserId)netstate.max_players && id < NET_PLAYERS_COUNT; id += 1) {
        if ((snapshot->active_players_mask & (1u << id)) == 0) {
            continue;
        }
        const struct Packet* packet = NULL;
        if (id == SERVER_ID) {
            packet = get_local_input_lag_packet_for_turn(turn);
        } else {
            if (received_packets == NULL) {
                return false;
            }
            packet = &received_packets[id];
        }
        if (packet == NULL || is_packet_empty(packet) || packet->turn != turn) {
            return false;
        }
        snapshot->packets[id] = *packet;
    }
    return true;
}

static void send_gameplay_snapshot_to_clients(const struct GameplaySnapshot* snapshot, int seq_nbr)
{
    for (NetUserId id = 0; id < (NetUserId)netstate.max_players && id < NET_PLAYERS_COUNT; id += 1) {
        if ((snapshot->active_players_mask & (1u << id)) == 0) {
            continue;
        }
    }

    char *ptr = InitMessageBuffer(NETMSG_GAMEPLAY_SNAPSHOT);
    *ptr = netstate.my_id;
    ptr += 1;
    *(int *)ptr = seq_nbr;
    ptr += 4;
    memcpy(ptr, snapshot, sizeof(*snapshot));
    ptr += sizeof(*snapshot);

    for (NetUserId id = 0; id < (NetUserId)netstate.max_players; id += 1) {
        if (id == netstate.my_id || !IsUserActive(id)) {
            continue;
        }
        SendMessage(id, ptr);
    }
}

static TbError broadcast_ready_gameplay_snapshots(void)
{
    struct GameplaySnapshot snapshot;

    if (!host_snapshot_turn_initialized) {
        next_host_snapshot_turn = game.play_gameturn;
        host_snapshot_turn_initialized = true;
    } else if (game.play_gameturn < next_host_snapshot_turn) {
        next_host_snapshot_turn = game.play_gameturn;
    }

    while (next_host_snapshot_turn <= game.play_gameturn) {
        if (!build_gameplay_snapshot_for_turn(next_host_snapshot_turn, &snapshot)) {
            break;
        }
        send_gameplay_snapshot_to_clients(&snapshot, netstate.seq_nbr);
        last_gameplay_snapshot_turn = snapshot.turn;
        next_host_snapshot_turn += 1;
    }
    return Lb_OK;
}

static void process_gameplay_upload_message(NetUserId source, const char* ptr, void* server_buf, size_t frame_size)
{
    char* peer_buf = ((char*)server_buf) + source * frame_size;
    const struct BundledPacket* bundled = (const struct BundledPacket*)ptr;
    memcpy(peer_buf, &bundled->packets[0], frame_size);
    unbundle_packets(ptr, (PlayerNumber)source);
}

static void process_gameplay_snapshot_message(NetUserId source, const char* ptr, void* server_buf)
{
    if (source != SERVER_ID) {
        WARNLOG("Ignoring gameplay snapshot from non-host user %d", source);
        return;
    }

    const struct GameplaySnapshot* snapshot = (const struct GameplaySnapshot*)ptr;
    memcpy(server_buf, snapshot->packets, sizeof(snapshot->packets));

    PlayerNumber my_packet_num = (PlayerNumber)netstate.my_id;
    struct PlayerInfo* player = get_my_player();
    if (player_exists(player)) {
        my_packet_num = player->packet_num;
    }

    for (int i = 0; i < NET_PLAYERS_COUNT && i < (int)netstate.max_players; i += 1) {
        if ((snapshot->active_players_mask & (1u << i)) == 0 || i == my_packet_num) {
            continue;
        }
        if (get_received_packet_for_player(snapshot->turn, (PlayerNumber)i) == NULL) {
            store_received_packet(snapshot->turn, (PlayerNumber)i, &snapshot->packets[i]);
        }
    }
    last_gameplay_snapshot_turn = snapshot->turn;
}

static TbError relay_gameplay_turn(void *send_buf, void *server_buf, size_t client_frame_size)
{
    memcpy(((char*)server_buf) + netstate.my_id * client_frame_size, send_buf, client_frame_size);

    if (netstate.my_id == SERVER_ID) {
        if (drain_pending_messages(server_buf, client_frame_size) == Lb_FAIL) {
            return Lb_FAIL;
        }
        if (broadcast_ready_gameplay_snapshots() == Lb_FAIL) {
            return Lb_FAIL;
        }
    } else {
        send_gameplay_upload_to_host((const struct Packet*)send_buf, netstate.seq_nbr);
        if (drain_pending_messages(server_buf, client_frame_size) == Lb_FAIL) {
            return Lb_FAIL;
        }
    }

    netstate.seq_nbr += 1;
    return Lb_OK;
}

void SendFrameToPeers(NetUserId source_id, const void * send_buf, size_t buf_size, int seq_nbr, enum NetMessageType msg_type) {
    char * ptr = InitMessageBuffer(msg_type);
    *ptr = source_id;
    ptr += 1;
    *(int *) ptr = seq_nbr;
    ptr += 4;
    if (msg_type == NETMSG_GAMEPLAY) {
        size_t bundled_size = bundle_packets((PlayerNumber)source_id, (const struct Packet*)send_buf, ptr);
        ptr += bundled_size;
    } else {
        memcpy(ptr, send_buf, buf_size);
        ptr += buf_size;
    }
    NetUserId id;
    for (id = 0; id < netstate.max_players; id += 1) {
        if (id == source_id) { continue; }
        if (!IsUserActive(id)) { continue; }
        if (msg_type == NETMSG_GAMEPLAY) {
            netstate.sp->sendmsg_single_unsequenced(id, netstate.msg_buffer, ptr - netstate.msg_buffer);
            netstate.sp->sendmsg_single(id, netstate.msg_buffer, ptr - netstate.msg_buffer);
        } else {
            SendMessage(id, ptr);
        }
    }
    if (msg_type == NETMSG_GAMEPLAY) {
        store_sent_packet((PlayerNumber)source_id, (const struct Packet*)send_buf);
    }
}

static TbError ProcessMessage(NetUserId source, void* server_buf, size_t frame_size) {
    if (netstate.sp->readmsg(source, netstate.msg_buffer, sizeof(netstate.msg_buffer)) <= 0) {
        ERRORLOG("Problem reading message from %u", source);
        return Lb_FAIL;
    }
    char *ptr = netstate.msg_buffer;
    enum NetMessageType type = (enum NetMessageType)*ptr;
    TbBool from_server = (source == SERVER_ID);
    ptr += 1;
    if (type == NETMSG_LOGIN) {
        if (from_server) {
            netstate.my_id = (NetUserId)*ptr;
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
        NETMSG("User %s successfully logged in", netstate.users[source].name);
        netstate.users[source].progress = USER_LOGGEDIN;
        play_non_3d_sample(76);
        char * msg_ptr = InitMessageBuffer(NETMSG_LOGIN);
        *msg_ptr = source;
        msg_ptr += 1;
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
        if (!from_server) {
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
    if (type == NETMSG_GAMEPLAY_UPLOAD) {
        NetUserId peer_id = (NetUserId)*ptr;
        if (peer_id < 0 || peer_id >= netstate.max_players) {
            ERRORLOG("Critical error: Out of range gameplay upload peer ID %i received", peer_id);
            abort();
        }
        ptr += 1;
        netstate.users[peer_id].ack = *(int *)ptr;
        ptr += 4;
        if (peer_id != source) {
            WARNLOG("Ignoring gameplay upload with mismatched source %d != %d", source, peer_id);
            return Lb_OK;
        }
        process_gameplay_upload_message(peer_id, ptr, server_buf, frame_size);
        return Lb_OK;
    }
    if (type == NETMSG_GAMEPLAY_SNAPSHOT) {
        NetUserId peer_id = (NetUserId)*ptr;
        if (peer_id < 0 || peer_id >= netstate.max_players) {
            ERRORLOG("Critical error: Out of range gameplay snapshot peer ID %i received", peer_id);
            abort();
        }
        ptr += 1;
        netstate.users[peer_id].ack = *(int *)ptr;
        ptr += 4;
        process_gameplay_snapshot_message(source, ptr, server_buf);
        return Lb_OK;
    }
    if (type == NETMSG_FRONTEND || type == NETMSG_SMALLDATA || type == NETMSG_GAMEPLAY) {
        NetUserId peer_id = (NetUserId)*ptr;
        if (peer_id < 0 || peer_id >= netstate.max_players) {
            ERRORLOG("Critical error: Out of range peer ID %i received, could be used for buffer overflow attack", peer_id);
            abort();
        }
        char* peer_buf = ((char*)server_buf) + peer_id * frame_size;
        ptr += 1;
        netstate.users[peer_id].ack = *(int *)ptr;
        ptr += 4;
        if (type == NETMSG_GAMEPLAY) {
            struct BundledPacket* bundled = (struct BundledPacket*)ptr;
            memcpy(peer_buf, &bundled->packets[0], frame_size);
            unbundle_packets(ptr, (PlayerNumber)peer_id);
        } else {
            memcpy(peer_buf, ptr, frame_size);
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
        return Lb_OK;
    }
    return Lb_OK;
}

TbError LbNetwork_ExchangeLogin(char *plyr_name) {
    NETMSG("Logging in as %s", plyr_name);
    if (1 + strlen(netstate.password) + 1 + strlen(plyr_name) + 1 >= sizeof(netstate.msg_buffer)) {
        ERRORLOG("Login credentials too long");
        return Lb_FAIL;
    }
    char * ptr = InitMessageBuffer(NETMSG_LOGIN);
    strcpy(ptr, netstate.password);
    ptr += strlen(netstate.password) + 1;
    strcpy(ptr, plyr_name);
    ptr += strlen(plyr_name) + 1;
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

void LbNetwork_WaitForMissingPackets(void* server_buf, size_t client_frame_size) {
    if (game.skip_initial_input_turns > 0) {
        return;
    }
    if (drain_pending_messages(server_buf, client_frame_size) == Lb_FAIL) {
        return;
    }
    if (netstate.my_id == SERVER_ID) {
        broadcast_ready_gameplay_snapshots();
    }
    GameTurn historical_turn = game.play_gameturn - game.input_lag_turns;
    const struct Packet* received_packets = get_received_packets_for_turn(historical_turn);
    if (received_packets == NULL) {
        MULTIPLAYER_LOG("LbNetwork_WaitForMissingPackets: Missing packets for turn=%lu, waiting...", (unsigned long)historical_turn);
        TbClockMSec start = LbTimerClock();
        while (true) {
            int elapsed = LbTimerClock() - start;
            if (elapsed >= TIMEOUT_GAMEPLAY_MISSING_PACKET) {
                MULTIPLAYER_LOG("LbNetwork_WaitForMissingPackets: Timeout waiting for turn=%lu packets", (unsigned long)historical_turn);
                break;
            }

            NetUserId id;
            for (id = 0; id < netstate.max_players; id += 1) {
                if (!should_exchange_with_user(id)) { continue; }

                int wait_time = TIMEOUT_GAMEPLAY_MISSING_PACKET - elapsed;
                if (netstate.sp->msgready(id, wait_time)) {
                    while (netstate.sp->msgready(id, 0)) {
                        ProcessMessage(id, server_buf, client_frame_size);
                    }
                    if (netstate.my_id == SERVER_ID) {
                        broadcast_ready_gameplay_snapshots();
                    }
                }
            }

            received_packets = get_received_packets_for_turn(historical_turn);
            if (received_packets != NULL) {
                MULTIPLAYER_LOG("LbNetwork_WaitForMissingPackets: Successfully received packets for turn=%lu after %dms", (unsigned long)historical_turn, elapsed);
                break;
            }

            network_yield_draw_gameplay();
        }
    }
}

TbError LbNetwork_Exchange(enum NetMessageType msg_type, void *send_buf, void *server_buf, size_t client_frame_size) {
    if (netstate.my_id < 0 || netstate.my_id >= netstate.max_players) {
        ERRORLOG("Invalid my_id %i in LbNetwork_Exchange (disconnected?)", netstate.my_id);
        return Lb_FAIL;
    }
    netstate.sp->update(OnNewUser);
    if (msg_type == NETMSG_GAMEPLAY_UPLOAD) {
        return relay_gameplay_turn(send_buf, server_buf, client_frame_size);
    }
    memcpy(((char*)server_buf) + netstate.my_id * client_frame_size, send_buf, client_frame_size);
    SendFrameToPeers(netstate.my_id, send_buf, client_frame_size, netstate.seq_nbr, msg_type);

    long double draw_interval_nanoseconds = 1000000000.0 / NETWORK_FPS;
    if (msg_type == NETMSG_FRONTEND) {
        draw_interval_nanoseconds = 0;
    }
    int timeout_max = TIMEOUT_LOBBY_EXCHANGE;
    if (msg_type == NETMSG_GAMEPLAY) {
        timeout_max = (1000 / game_num_fps);
    }

    NetUserId id;
    for (id = 0; id < netstate.max_players; id += 1) {
        if (!should_exchange_with_user(id)) { continue; }

        TbClockMSec start = LbTimerClock();
        while (true) {
            int elapsed = LbTimerClock() - start;
            if (elapsed >= timeout_max) {
                break;
            }
            if (netstate.users[id].progress == USER_UNUSED) {
                break;
            }

            long long time_since_draw_nanoseconds = get_time_tick_ns() - last_draw_completed_time;
            int remaining_time_until_draw = (int)((draw_interval_nanoseconds - time_since_draw_nanoseconds) / 1000000.0);
            if (remaining_time_until_draw < 0) {remaining_time_until_draw = 0;}
            int wait = min(timeout_max - elapsed, remaining_time_until_draw);

            if (netstate.sp->msgready(id, wait)) {
                ProcessMessage(id, server_buf, client_frame_size);
                if (msg_type != NETMSG_GAMEPLAY) {
                    break;
                }
                TbBool received_gameplay_msg = (netstate.msg_buffer[0] == NETMSG_GAMEPLAY);
                while (netstate.sp->msgready(id, 0)) {
                    ProcessMessage(id, server_buf, client_frame_size);
                    if (netstate.msg_buffer[0] == NETMSG_GAMEPLAY) {
                        received_gameplay_msg = true;
                    }
                }
                if (received_gameplay_msg) {
                    break;
                }
            }

            if (LbTimerClock() - start < timeout_max) {
                if (msg_type == NETMSG_FRONTEND) {
                    network_yield_draw_frontend();
                } else {
                    network_yield_draw_gameplay();
                }
            }
        }
    }
    netstate.seq_nbr += 1;
    return Lb_OK;
}

void LbNetwork_SendChatMessageImmediate(int player_id, const char *message) {
    char* ptr = InitMessageBuffer(NETMSG_CHATMESSAGE);
    *ptr = player_id;
    ptr += 1;
    strcpy(ptr, message);
    for (NetUserId id = 0; id < netstate.max_players; id += 1) {
        if (id != netstate.my_id && IsUserActive(id)) {
            netstate.sp->sendmsg_single(id, netstate.msg_buffer, 3 + strlen(message));
        }
    }
}

void LbNetwork_BroadcastUnpauseTimesync(void) {
    MULTIPLAYER_LOG("LbNetwork_BroadcastUnpauseTimesync");
    InitMessageBuffer(NETMSG_UNPAUSE);
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
