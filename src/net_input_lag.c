#include "pre_inc.h"
#include "net_input_lag.h"

#include "globals.h"
#include "packets.h"
#include "player_data.h"
#include "net_game.h"
#include "game_legacy.h"
#include "bflib_network.h"
#include "bflib_network_internal.h"
#include "bflib_enet.h"
#include "bflib_math.h"
#include "bflib_datetm.h"
#include "frontend.h"
#include "front_landview.h"
#include "front_network.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static struct Packet local_input_lag_packets[MAXIMUM_INPUT_LAG_TURNS + 1];

enum InputLagMode {
    INPUT_LAG_MODE_ONE_VS_ONE = 0,
    INPUT_LAG_MODE_RELAY,
};

void store_local_packet_in_input_lag_queue(PlayerNumber my_packet_num) {
    if (game.input_lag_turns + 1 <= 0) {
        return;
    }
    int slot = game.play_gameturn % (game.input_lag_turns + 1);
    local_input_lag_packets[slot] = game.packets[my_packet_num];
    const char* player_name;
    if (my_packet_num == 0) {player_name = "Host";} else {player_name = "Client";}
    MULTIPLAYER_LOG("store_local_packet_in_input_lag_queue: STORING local packet[%s] turn=%lu checksum=%08lx into queue slot %d", player_name, (unsigned long)game.packets[my_packet_num].turn, (unsigned long)game.packets[my_packet_num].checksum, slot);
}

struct Packet* get_local_input_lag_packet_for_turn(GameTurn target_turn) {
    for (int i = 0; i < game.input_lag_turns + 1; i++) {
        struct Packet* packet = &local_input_lag_packets[i];
        if (!is_packet_empty(packet) && packet->turn == target_turn) {
            MULTIPLAYER_LOG("get_local_input_lag_packet_for_turn: found packet for turn=%lu in slot %d", (unsigned long)target_turn, i);
            return packet;
        }
    }
    MULTIPLAYER_LOG("get_local_input_lag_packet_for_turn: no packet found for turn=%lu", (unsigned long)target_turn);
    return NULL;
}

TbBool input_lag_skips_initial_processing(void) {
    if ((game.operation_flags & GOF_Paused) != 0 && game.game_kind != GKind_LocalGame) {return true;}

    if (game.skip_initial_input_turns > 0) {
        game.skip_initial_input_turns--;
        MULTIPLAYER_LOG("process_packets: Input lag skip turns remaining: %d, skipping packet processing", game.skip_initial_input_turns);
        return true;
    }
    return false;
}

const int heartZoomTime = 35; //30 isn't enough, it causes palette issues if it desyncs during the heart zoom
unsigned short calculate_skip_input(void) {
    if (get_gameturn() <= heartZoomTime) {
        return game.input_lag_turns + heartZoomTime;
    }
    return game.input_lag_turns + (game_num_fps * 0.25);
}

void clear_input_lag_queue(void) {
    memset(local_input_lag_packets, 0, sizeof(local_input_lag_packets));
}

void LbNetwork_UpdateInputLagIfHost(void) {
    static TbClockMSec last_update_ms = 0;
    static TbClockMSec player_count_change_time = 0;
    static int total_ping = 0;
    static int sample_count = 0;
    static int previous_remote_player_count = -1;
    if ((game.system_flags & GSF_NetworkActive) == 0) { return; }
    if (frontend_menu_state == FeSt_START_MPLEVEL) { return; }
    if (my_player_number != get_host_player_id()) { return; }
    if (!netstate.sp) { return; }
    TbClockMSec now = LbTimerClock();
    netstate.sp->update(OnNewUser);
    int remote_player_count = 0;
    NetUserId id;
    for (id = 0; id < netstate.max_players; id += 1) {
        if (id == netstate.my_id) { continue; }
        if (netstate.users[id].progress == USER_LOGGEDIN) {
            remote_player_count += 1;
        }
    }
    if (remote_player_count == 0) {
        player_count_change_time = 0;
        sample_count = 0;
        total_ping = 0;
        previous_remote_player_count = 0;
        return;
    }
    enum InputLagMode mode = INPUT_LAG_MODE_RELAY;
    if (remote_player_count == 1) {
        mode = INPUT_LAG_MODE_ONE_VS_ONE;
    }
    if (previous_remote_player_count != remote_player_count) {
        player_count_change_time = now;
        sample_count = 0;
        total_ping = 0;
        previous_remote_player_count = remote_player_count;
    }
    if (player_count_change_time == 0) {
        player_count_change_time = now;
    }
    if (now - player_count_change_time < WAIT_FOR_STABLE_PLAYER) {
        sample_count = 0;
        total_ping = 0;
        return;
    }
    if (last_update_ms != 0 && now - last_update_ms < AVERAGE_PING_UPDATE_RATE) { return; }
    last_update_ms = now;
    int max_ping = 0;
    for (id = 0; id < netstate.max_players; id += 1) {
        if (id == netstate.my_id) { continue; }
        if (!(netstate.users[id].progress == USER_LOGGEDIN)) { continue; }
        unsigned long ping = GetPing(id);
        if (ping <= 0) {
            MULTIPLAYER_LOG("Player %d (%s) has no RTT data yet", id, netstate.users[id].name);
            continue;
        }
        MULTIPLAYER_LOG("Player %d (%s) Ping: %lums", id, netstate.users[id].name, ping);
        if ((int)ping > max_ping) {
            max_ping = (int)ping;
        }
    }
    if (max_ping == 0) {
        return;
    }
    total_ping += max_ping;
    sample_count++;
    int average_ping = total_ping / sample_count;
    float turn_time_ms = 1000.0f / game_num_fps;
    float effective_ping_ms = (float)average_ping;
    float half_turn_time = (turn_time_ms * 0.5f);
    // 1v1 needs only one-way transit time
    if (mode == INPUT_LAG_MODE_ONE_VS_ONE) {
        effective_ping_ms = average_ping * 0.5f;
    }
    int input_lag = max(1, CEILING((effective_ping_ms - half_turn_time) / turn_time_ms));
    
    if (average_ping < 25) {
        input_lag = 0;
    }

    game.input_lag_turns = min(input_lag, MAXIMUM_INPUT_LAG_TURNS);
}

#ifdef __cplusplus
}
#endif
