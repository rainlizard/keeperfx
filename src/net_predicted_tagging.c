#include "pre_inc.h"
#include "net_predicted_tagging.h"

#include "game_legacy.h"
#include "map_data.h"
#include "map_blocks.h"
#include "net_input_lag.h"
#include "packets.h"
#include "player_data.h"
#include "player_utils.h"
#include "roomspace.h"
#include "slab_data.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static unsigned char local_predicted_dig_overlay[MAX_TILES_X * MAX_TILES_Y];

struct RoomspaceHighlightState {
    struct RoomSpace render_roomspace;
    TbBool one_click_lock_cursor;
    TbBool ignore_next_PCtr_RBtnRelease;
    TbBool ignore_next_PCtr_LBtnRelease;
    char swap_to_untag_mode;
};

struct RoomSpace create_box_roomspace_from_drag(struct RoomSpace roomspace, MapSlabCoord start_x, MapSlabCoord start_y, MapSlabCoord end_x, MapSlabCoord end_y);
struct RoomSpace check_roomspace_for_diggable_slabs(struct RoomSpace roomspace, PlayerNumber plyr_idx);

static void set_local_predicted_dig_overlay_slab(MapSlabCoord slb_x, MapSlabCoord slb_y, TbBool untag_mode)
{
    struct Map *mapblk;

    if ((slb_x < 0) || (slb_x >= game.map_tiles_x))
    {
        return;
    }
    if ((slb_y < 0) || (slb_y >= game.map_tiles_y))
    {
        return;
    }
    if (untag_mode)
    {
        local_predicted_dig_overlay[get_slab_number(slb_x, slb_y)] = LocalPredictedDig_Clear;
        return;
    }
    mapblk = get_map_block_at(slab_subtile_center(slb_x), slab_subtile_center(slb_y));
    if ((mapblk->flags & SlbAtFlg_Valuable) != 0)
    {
        local_predicted_dig_overlay[get_slab_number(slb_x, slb_y)] = LocalPredictedDig_Gold;
        return;
    }
    local_predicted_dig_overlay[get_slab_number(slb_x, slb_y)] = LocalPredictedDig_Land;
}

static void build_dungeon_highlight_user_roomspace_from_state(struct RoomSpace *roomspace, struct RoomspaceHighlightState *state,
    PlayerNumber plyr_idx, const struct PlayerInfo *player, const struct Packet *pckt, MapSubtlCoord stl_x, MapSubtlCoord stl_y)
{
    MapSlabCoord slb_x;
    MapSlabCoord slb_y;
    struct RoomSpace current_roomspace;
    TbBool highlight_mode;
    TbBool untag_mode;
    TbBool one_click_mode_exclusive;
    MapSlabCoord drag_start_x;
    MapSlabCoord drag_start_y;

    slb_x = subtile_slab(stl_x);
    slb_y = subtile_slab(stl_y);
    highlight_mode = false;
    untag_mode = false;
    one_click_mode_exclusive = false;
    drag_start_x = slb_x;
    drag_start_y = slb_y;
    if (state->ignore_next_PCtr_LBtnRelease)
    {
        state->render_roomspace.drag_mode = false;
        state->one_click_lock_cursor = false;
        current_roomspace = create_box_roomspace(state->render_roomspace, player->roomspace_width, player->roomspace_height, slb_x, slb_y);
        current_roomspace.highlight_mode = false;
        current_roomspace.untag_mode = false;
        current_roomspace.one_click_mode_exclusive = false;
        current_roomspace = check_roomspace_for_diggable_slabs(current_roomspace, plyr_idx);
        state->render_roomspace = current_roomspace;
        *roomspace = current_roomspace;
        return;
    }
    if (!state->render_roomspace.drag_mode)
    {
        state->render_roomspace.drag_start_x = slb_x;
        state->render_roomspace.drag_start_y = slb_y;
    }
    if ((pckt->control_flags & PCtr_LBtnHeld) == PCtr_LBtnHeld)
    {
        state->one_click_lock_cursor = true;
        untag_mode = state->render_roomspace.untag_mode;
    }
    else
    {
        if (find_from_task_list(plyr_idx, get_subtile_number(stl_slab_center_subtile(stl_x), stl_slab_center_subtile(stl_y))) != -1)
        {
            untag_mode = true;
        }
    }
    if ((state->swap_to_untag_mode == -1) && ((pckt->control_flags & PCtr_RBtnHeld) == PCtr_RBtnHeld) && (player->roomspace_highlight_mode == 2) && (!subtile_is_diggable_for_player(plyr_idx, stl_x, stl_y, false)) && ((pckt->control_flags & PCtr_LBtnAnyAction) == 0))
    {
        state->swap_to_untag_mode = 0;
    }
    if (state->swap_to_untag_mode == 0)
    {
        if (!subtile_is_diggable_for_player(plyr_idx, stl_x, stl_y, false))
        {
            state->swap_to_untag_mode = 1;
        }
    }
    if (player->roomspace_highlight_mode == 1)
    {
        if (((pckt->control_flags & PCtr_LBtnHeld) != 0) || ((pckt->control_flags & PCtr_LBtnRelease) != 0))
        {
            state->one_click_lock_cursor = true;
            untag_mode = state->render_roomspace.untag_mode;
            one_click_mode_exclusive = true;
            drag_start_x = state->render_roomspace.drag_start_x;
            drag_start_y = state->render_roomspace.drag_start_y;
        }
        if (((pckt->control_flags & PCtr_RBtnHeld) != 0) && ((pckt->control_flags & PCtr_LBtnClick) != 0))
        {
            state->ignore_next_PCtr_RBtnRelease = true;
        }
        if (((pckt->control_flags & PCtr_LBtnHeld) != 0) && ((pckt->control_flags & PCtr_RBtnClick) != 0))
        {
            state->ignore_next_PCtr_LBtnRelease = true;
            state->ignore_next_PCtr_RBtnRelease = true;
            drag_start_x = slb_x;
            drag_start_y = slb_y;
        }
        highlight_mode = true;
        current_roomspace = create_box_roomspace_from_drag(state->render_roomspace, drag_start_x, drag_start_y, slb_x, slb_y);
        detect_roomspace_direction(&current_roomspace);
    }
    else if (player->roomspace_highlight_mode == 2)
    {
        if ((pckt->control_flags & PCtr_HeldAnyButton) != 0)
        {
            state->one_click_lock_cursor = true;
            one_click_mode_exclusive = true;
        }
        highlight_mode = true;
        current_roomspace = create_box_roomspace(state->render_roomspace, player->user_defined_roomspace_width, player->user_defined_roomspace_width, slb_x, slb_y);
    }
    else
    {
        current_roomspace = create_box_roomspace(state->render_roomspace, 1, 1, slb_x, slb_y);
    }
    current_roomspace.highlight_mode = highlight_mode;
    current_roomspace.untag_mode = untag_mode;
    current_roomspace.one_click_mode_exclusive = one_click_mode_exclusive;
    current_roomspace = check_roomspace_for_diggable_slabs(current_roomspace, plyr_idx);
    if (state->swap_to_untag_mode == 1)
    {
        if (current_roomspace.slab_count == 0)
        {
            struct RoomSpace untag_roomspace;

            untag_roomspace = current_roomspace;
            untag_roomspace.untag_mode = true;
            untag_roomspace = check_roomspace_for_diggable_slabs(untag_roomspace, plyr_idx);
            if ((untag_roomspace.slab_count > 0) && ((pckt->control_flags & PCtr_LBtnAnyAction) == 0))
            {
                current_roomspace = untag_roomspace;
                state->swap_to_untag_mode = 2;
            }
        }
        else
        {
            state->swap_to_untag_mode = -1;
        }
    }
    if (current_roomspace.slab_count > 0)
    {
        current_roomspace.tag_for_dig = true;
    }
    if ((state->one_click_lock_cursor) && ((pckt->control_flags & PCtr_LBtnHeld) != 0) && (!current_roomspace.drag_mode))
    {
        current_roomspace.is_roomspace_a_box = true;
    }
    if (state->swap_to_untag_mode == 2)
    {
        state->swap_to_untag_mode = -1;
    }
    state->render_roomspace = current_roomspace;
    *roomspace = current_roomspace;
}

void rebuild_local_predicted_dig_preview(void)
{
    struct PlayerInfo *player;
    struct RoomspaceHighlightState state;
    TbBool have_previous_packet;
    MapSubtlCoord previous_packet_stl_x;
    MapSubtlCoord previous_packet_stl_y;
    GameTurn current_turn;
    GameTurn first_pending_turn;

    memset(local_predicted_dig_overlay, 0, sizeof(local_predicted_dig_overlay));
    player = get_player(my_player_number);
    if (player_invalid(player))
    {
        return;
    }
    if (!is_my_player(player))
    {
        return;
    }
    if (player->view_type != PVT_DungeonTop)
    {
        return;
    }
    if (player->work_state != PSt_CtrlDungeon)
    {
        return;
    }

    state.render_roomspace = player->render_roomspace;
    state.one_click_lock_cursor = player->one_click_lock_cursor;
    state.ignore_next_PCtr_RBtnRelease = player->ignore_next_PCtr_RBtnRelease;
    state.ignore_next_PCtr_LBtnRelease = player->ignore_next_PCtr_LBtnRelease;
    state.swap_to_untag_mode = player->swap_to_untag_mode;
    have_previous_packet = false;
    current_turn = get_gameturn();
    first_pending_turn = 0;
    if ((game.input_lag_turns > 0) && (current_turn >= (GameTurn)game.input_lag_turns))
    {
        first_pending_turn = current_turn - game.input_lag_turns;
    }

    for (GameTurn turn = first_pending_turn; turn <= current_turn; turn++)
    {
        const struct Packet *queued_pckt;
        unsigned short primary_cursor_state;
        struct RoomSpace predicted_roomspace;
        MapSubtlCoord stl_x;
        MapSubtlCoord stl_y;
        MapSubtlCoord previous_stl_x;
        MapSubtlCoord previous_stl_y;

        queued_pckt = get_local_input_lag_packet_for_turn(turn);
        if (queued_pckt == NULL)
        {
            continue;
        }
        if ((queued_pckt->control_flags & PCtr_MapCoordsValid) == 0)
        {
            continue;
        }
        if ((queued_pckt->control_flags & (PCtr_LBtnClick | PCtr_LBtnHeld)) == 0)
        {
            continue;
        }
        primary_cursor_state = (unsigned short)(queued_pckt->additional_packet_values & PCAdV_ContextMask) >> 1;
        if ((primary_cursor_state != CSt_PickAxe) && (primary_cursor_state != CSt_PowerHand))
        {
            continue;
        }

        stl_x = coord_subtile(queued_pckt->pos_x);
        stl_y = coord_subtile(queued_pckt->pos_y);
        previous_stl_x = stl_x;
        previous_stl_y = stl_y;
        if (have_previous_packet)
        {
            previous_stl_x = previous_packet_stl_x;
            previous_stl_y = previous_packet_stl_y;
        }

        build_dungeon_highlight_user_roomspace_from_state(&predicted_roomspace, &state, my_player_number, player, queued_pckt, stl_x, stl_y);
        if (predicted_roomspace.slab_count <= 0)
        {
            previous_packet_stl_x = stl_x;
            previous_packet_stl_y = stl_y;
            have_previous_packet = true;
            continue;
        }

        if (player->roomspace_highlight_mode == 0)
        {
            for (int32_t y = 0; y < predicted_roomspace.height; y++)
            {
                int32_t current_y;

                current_y = predicted_roomspace.top + y;
                for (int32_t x = 0; x < predicted_roomspace.width; x++)
                {
                    int32_t current_x;
                    int32_t draw_path_x;
                    int32_t draw_path_y;

                    current_x = predicted_roomspace.left + x;
                    draw_path_x = previous_stl_x / STL_PER_SLB;
                    draw_path_y = previous_stl_y / STL_PER_SLB;
                    while (true)
                    {
                        set_local_predicted_dig_overlay_slab(draw_path_x, draw_path_y, predicted_roomspace.untag_mode);
                        if ((draw_path_x == current_x) && (draw_path_y == current_y))
                        {
                            break;
                        }
                        if (abs(draw_path_x - current_x) > abs(draw_path_y - current_y))
                        {
                            if (draw_path_x < current_x)
                            {
                                draw_path_x += 1;
                            }
                            else
                            {
                                draw_path_x -= 1;
                            }
                        }
                        else
                        {
                            if (draw_path_y < current_y)
                            {
                                draw_path_y += 1;
                            }
                            else
                            {
                                draw_path_y -= 1;
                            }
                        }
                    }
                }
            }
        }
        else
        {
            int32_t current_x;
            int32_t current_y;

            switch (predicted_roomspace.drag_direction)
            {
                case top_left_to_bottom_right:
                {
                    current_y = predicted_roomspace.top;
                    current_x = predicted_roomspace.left;
                    break;
                }
                case bottom_right_to_top_left:
                {
                    current_y = predicted_roomspace.bottom;
                    current_x = predicted_roomspace.right;
                    break;
                }
                case top_right_to_bottom_left:
                {
                    current_y = predicted_roomspace.top;
                    current_x = predicted_roomspace.right;
                    break;
                }
                case bottom_left_to_top_right:
                {
                    current_y = predicted_roomspace.bottom;
                    current_x = predicted_roomspace.left;
                    break;
                }
                default:
                {
                    current_x = predicted_roomspace.left;
                    current_y = predicted_roomspace.top;
                    break;
                }
            }

            while (true)
            {
                set_local_predicted_dig_overlay_slab(current_x, current_y, predicted_roomspace.untag_mode);
                switch (predicted_roomspace.drag_direction)
                {
                    case top_left_to_bottom_right:
                    {
                        current_x++;
                        if (current_x > predicted_roomspace.right)
                        {
                            current_x = predicted_roomspace.left;
                            current_y++;
                        }
                        if (current_y > predicted_roomspace.bottom)
                        {
                            break;
                        }
                        continue;
                    }
                    case bottom_right_to_top_left:
                    {
                        current_x--;
                        if (current_x < predicted_roomspace.left)
                        {
                            current_x = predicted_roomspace.right;
                            current_y--;
                        }
                        if (current_y < predicted_roomspace.top)
                        {
                            break;
                        }
                        continue;
                    }
                    case top_right_to_bottom_left:
                    {
                        current_x--;
                        if (current_x < predicted_roomspace.left)
                        {
                            current_x = predicted_roomspace.right;
                            current_y++;
                        }
                        if (current_y > predicted_roomspace.bottom)
                        {
                            break;
                        }
                        continue;
                    }
                    case bottom_left_to_top_right:
                    {
                        current_x++;
                        if (current_x > predicted_roomspace.right)
                        {
                            current_x = predicted_roomspace.left;
                            current_y--;
                        }
                        if (current_y < predicted_roomspace.top)
                        {
                            break;
                        }
                        continue;
                    }
                    default:
                    {
                        break;
                    }
                }
                break;
            }
        }

        previous_packet_stl_x = stl_x;
        previous_packet_stl_y = stl_y;
        have_previous_packet = true;
    }
}

void clear_local_predicted_dig_preview(void)
{
    memset(local_predicted_dig_overlay, 0, sizeof(local_predicted_dig_overlay));
}

unsigned char get_local_predicted_dig_state(MapSubtlCoord stl_x, MapSubtlCoord stl_y)
{
    MapSlabCoord slb_x;
    MapSlabCoord slb_y;

    slb_x = subtile_slab(stl_x);
    slb_y = subtile_slab(stl_y);
    if ((slb_x < 0) || (slb_x >= game.map_tiles_x))
    {
        return LocalPredictedDig_None;
    }
    if ((slb_y < 0) || (slb_y >= game.map_tiles_y))
    {
        return LocalPredictedDig_None;
    }
    return local_predicted_dig_overlay[get_slab_number(slb_x, slb_y)];
}

#ifdef __cplusplus
}
#endif
