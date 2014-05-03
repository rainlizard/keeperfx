/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file map_events.c
 *     Map events support functions.
 * @par Purpose:
 *     Functions to create and maintain events placed on map.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     11 Mar 2010 - 12 May 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "map_events.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_memory.h"
#include "bflib_planar.h"
#include "bflib_sound.h"

#include "thing_objects.h"
#include "thing_doors.h"
#include "thing_traps.h"
#include "config_strings.h"
#include "config_campaigns.h"
#include "config_creature.h"
#include "config_trapdoor.h"
#include "gui_frontmenu.h"
#include "frontend.h"
#include "room_workshop.h"
#include "power_hand.h"
#include "game_legacy.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
DLLIMPORT long _DK_event_create_event_or_update_nearby_existing_event(long map_x, long map_y, unsigned char evkind, unsigned char dngn_id, long target);
DLLIMPORT void _DK_event_initialise_all(void);
DLLIMPORT long _DK_event_move_player_towards_event(struct PlayerInfo *player, long var);
DLLIMPORT struct Event *_DK_event_create_event(long map_x, long map_y, unsigned char evkind, unsigned char dngn_id, long target);
DLLIMPORT void _DK_go_on_then_activate_the_event_box(long plyr_idx, long val);
DLLIMPORT long _DK_event_create_event_or_update_old_event(long a1, long a2, unsigned char combat_kind, unsigned char a4, long a5);
DLLIMPORT void _DK_event_process_events(void);
DLLIMPORT void _DK_update_all_events(void);
DLLIMPORT void _DK_maintain_my_event_list(struct Dungeon *dungeon);
DLLIMPORT void _DK_event_delete_event(long plyr_idx, long num);
DLLIMPORT void _DK_remove_events_thing_is_attached_to(struct Thing *thing);
DLLIMPORT void _DK_event_kill_all_players_events(long plyr_idx);

/******************************************************************************/
TbBool event_is_invalid(const struct Event *event)
{
    return (event <= &game.event[0]) || (event > &game.event[EVENTS_COUNT-1]) || (event == NULL);
}

struct Event *get_event_nearby_of_type_for_player(MapCoord map_x, MapCoord map_y, long max_dist, EventKind evkind, PlayerNumber plyr_idx)
{
    int i;
    for (i=1; i < EVENTS_COUNT; i++)
    {
        struct Event *event;
        event = &game.event[i];
        if (((event->flags & EvF_Exists) != 0) && (event->owner == plyr_idx) && (event->kind == evkind)
         && get_distance_xy(event->mappos_x, event->mappos_y, map_x, map_y) < max_dist) {
            return event;
        }
    }
    return INVALID_EVENT;
}

struct Event *get_event_of_target_and_type_for_player(long target, EventKind evkind, PlayerNumber plyr_idx)
{
    int i;
    for (i=1; i < EVENTS_COUNT; i++)
    {
        struct Event *event;
        event = &game.event[i];
        if (((event->flags & EvF_Exists) != 0) && (event->owner == plyr_idx) && (event->kind == evkind)
         && (event->target == target)) {
            return event;
        }
    }
    return INVALID_EVENT;
}

struct Event *get_event_of_type_for_player(EventKind evkind, PlayerNumber plyr_idx)
{
    int i;
    for (i=1; i < EVENTS_COUNT; i++)
    {
        struct Event *event;
        event = &game.event[i];
        if (((event->flags & EvF_Exists) != 0) && (event->owner == plyr_idx) && (event->kind == evkind)) {
            return event;
        }
    }
    return INVALID_EVENT;
}

/** Creates a map event or updates existing map event of given kind which is within 5 subtiles of the new event location.
 *
 * @param map_x Event position on map, X coord.
 * @param map_y Event position on map, Y coord.
 * @param evkind Event kind to be searched or created.
 * @param dngn_id Owning dungeon index.
 * @param target Event target identification parameter, its meaning depends on event kind.
 * @return Index of the new event, or negative index of updated event. Zero if no action was taken.
 */
EventIndex event_create_event_or_update_nearby_existing_event(MapCoord map_x, MapCoord map_y, EventKind evkind, unsigned char dngn_id, long target)
{
    //return _DK_event_create_event_or_update_nearby_existing_event(map_x, map_y, evkind, dngn_id, msg_id);
    struct Event *event;
    event = get_event_nearby_of_type_for_player(map_x, map_y, subtile_coord(5,0), evkind, dngn_id);
    if (!event_is_invalid(event))
    {
        SYNCDBG(3,"Updating event %d to be kind %d at (%d,%d)",(int)event->index,(int)evkind,(int)coord_subtile(map_x),(int)coord_subtile(map_y));
        event_initialise_event(event, map_x, map_y, evkind, dngn_id, target);
        return -(EventIndex)event->index;
    }
    SYNCDBG(3,"Creating event kind %d at (%d,%d)",(int)evkind,(int)coord_subtile(map_x),(int)coord_subtile(map_y));
    event = event_create_event(map_x, map_y, evkind, dngn_id, target);
    if (event_is_invalid(event)) {
        return 0;
    }
    return (EventIndex)event->index;
}

/** Creates a map event or updates existing map event of given kind which has the same target.
 *
 * @param map_x Event position on map, X coord.
 * @param map_y Event position on map, Y coord.
 * @param evkind Event kind to be searched or created.
 * @param dngn_id Owning dungeon index.
 * @param target Event target identification parameter, its meaning depends on event kind.
 * @return Index of the new event, or negative index of updated event. Zero if no action was taken.
 */
EventIndex event_create_event_or_update_same_target_existing_event(MapCoord map_x, MapCoord map_y, EventKind evkind, unsigned char dngn_id, long target)
{
    struct Event *event;
    event = get_event_of_target_and_type_for_player(target, evkind, dngn_id);
    if (!event_is_invalid(event))
    {
        SYNCDBG(3,"Updating event %d to be kind %d at (%d,%d)",(int)event->index,(int)evkind,(int)coord_subtile(map_x),(int)coord_subtile(map_y));
        event_initialise_event(event, map_x, map_y, evkind, dngn_id, target);
        return -(EventIndex)event->index;
    }
    SYNCDBG(3,"Creating event kind %d at (%d,%d)",(int)evkind,(int)coord_subtile(map_x),(int)coord_subtile(map_y));
    event = event_create_event(map_x, map_y, evkind, dngn_id, target);
    if (event_is_invalid(event)) {
        return 0;
    }
    return (EventIndex)event->index;
}

/** Creates a map event or updates existing map event of given kind if it exists anywhere on map.
 *
 * @param map_x Event position on map, X coord.
 * @param map_y Event position on map, Y coord.
 * @param evkind Event kind to be searched or created.
 * @param dngn_id Owning dungeon index.
 * @param target Event target identification parameter, its meaning depends on event kind.
 * @return Index of the new event, or negative index of updated event. Zero if no action was taken.
 */
EventIndex event_create_event_or_update_old_event(MapCoord map_x, MapCoord map_y, EventKind evkind, unsigned char dngn_id, long target)
{
    //return _DK_event_create_event_or_update_old_event(map_x, map_y, evkind, dngn_id, target);
    struct Event *event;
    // Check if such event already exists
    event = get_event_of_type_for_player(evkind, dngn_id);
    // If we've found a matching event, replace it
    if (!event_is_invalid(event))
    {
        event_initialise_event(event, map_x, map_y, evkind, dngn_id, target);
        event_add_to_event_buttons_list_or_replace_button(event, get_dungeon(dngn_id));
        return -(EventIndex)event->index;
    }
    // If no matching event found, then create new one
    event = event_create_event(map_x, map_y, evkind, dngn_id, target);
    if (event_is_invalid(event)) {
        return 0;
    }
    return (EventIndex)event->index;
}

void event_initialise_all(void)
{
    //_DK_event_initialise_all();
    int i,k;
    for (i=0; i < DUNGEONS_COUNT; i++)
    {
        struct Dungeon *dungeon;
        dungeon = get_dungeon(i);
        for (k=0; k <= EVENT_BUTTONS_COUNT; k++)
        {
            dungeon->event_button_index[k] = 0;
        }
    }
}

long event_move_player_towards_event(struct PlayerInfo *player, long var)
{
  return _DK_event_move_player_towards_event(player,var);
}

struct Event *event_create_event(MapCoord map_x, MapCoord map_y, EventKind evkind, unsigned char dngn_id, long target)
{
    struct Dungeon *dungeon;
    struct Event *event;
    long i,k;
    //return _DK_event_create_event(map_x, map_y, evkind, dngn_id, msg_id);
    if (dngn_id == game.neutral_player_num) {
        return INVALID_EVENT;
    }
    if (evkind >= EVENT_KIND_COUNT) {
        ERRORLOG("Illegal Event kind %d to be created",(int)evkind);
        return INVALID_EVENT;
    }
    dungeon = get_dungeon(dngn_id);
    // TODO FIGHT these are needed because we can't resize "dungeon->field_13B4" and added new events anyway; remove when struct Dungeon can be resized
    switch (evkind)
    {
    case EvKind_QuickInformation:
        i = dungeon->field_13B4[EvKind_Information];
        break;
    case EvKind_FriendlyFight:
        i = dungeon->field_13B4[EvKind_EnemyFight];
        break;
    case EvKind_RoomUnreachable:
        i = dungeon->field_13B4[EvKind_NoMoreLivingSet];
        break;
    default:
        i = dungeon->field_13B4[evkind];
        break;
    }
    if (i != 0)
    {
        k = event_button_info[evkind].turns_between_events;
        if ((k != 0) && (i+k >= game.play_gameturn))
        {
          return INVALID_EVENT;
        }
    }
    event = event_allocate_free_event_structure();
    if (event_is_invalid(event)) {
        return INVALID_EVENT;
    }
    event_initialise_event(event, map_x, map_y, evkind, dngn_id, target);
    event_add_to_event_buttons_list_or_replace_button(event, dungeon);
    return event;
}

struct Event *event_allocate_free_event_structure(void)
{
    struct Event *event;
    long i;
    for (i=1; i < EVENTS_COUNT; i++)
    {
        event = &game.event[i];
        if ((event->flags & EvF_Exists) == 0)
        {
            event->flags |= EvF_Exists;
            event->index = i;
            return event;
        }
    }
    return INVALID_EVENT;
}

void event_initialise_event(struct Event *event, MapCoord map_x, MapCoord map_y, EventKind evkind, unsigned char dngn_id, long target)
{
    event->mappos_x = map_x;
    event->mappos_y = map_y;
    event->kind = evkind;
    event->owner = dngn_id;
    event->lifespan_turns = event_button_info[evkind].lifespan_turns;
    event->target = target;
    event->flags |= EvF_BtnFirstFall;
}

void event_delete_event_structure(long ev_idx)
{
    LbMemorySet(&game.event[ev_idx], 0, sizeof(struct Event));
}

void event_update_last_use(struct Event *event)
{
    struct Dungeon *dungeon;
    dungeon = get_dungeon(event->owner);
    if (dungeon_invalid(dungeon)) {
        ERRORLOG("Player %d dungeon doesn't exist",(int)event->owner);
        return;
    }
    if ((event->kind < 1) || (event->kind >= EVENT_KIND_COUNT)) {
        ERRORLOG("Illegal Event kind %d to be updated",(int)event->kind);
        return;
    }
    // TODO FIGHT these are needed because we can't resize "dungeon->field_13B4" and added new events anyway; remove when struct Dungeon can be resized
    switch (event->kind)
    {
    case EvKind_QuickInformation:
        dungeon->field_13B4[EvKind_Information] = game.play_gameturn;
        break;
    case EvKind_FriendlyFight:
        dungeon->field_13B4[EvKind_EnemyFight] = game.play_gameturn;
        break;
    case EvKind_RoomUnreachable:
        dungeon->field_13B4[EvKind_NoMoreLivingSet] = game.play_gameturn;
        break;
    default:
        dungeon->field_13B4[event->kind] = game.play_gameturn;
        break;
    }
}

void event_delete_event(long plyr_idx, long ev_idx)
{
    struct Event *event;
    long i,k;
//  _DK_event_delete_event(plridx, num);
    event = &game.event[ev_idx];
    event_update_last_use(event);
    struct Dungeon *dungeon;
    dungeon = get_dungeon(plyr_idx);
    for (i=0; i <= EVENT_BUTTONS_COUNT; i++)
    {
        k = dungeon->event_button_index[i];
        if (k == ev_idx)
        {
            turn_off_event_box_if_necessary(plyr_idx, ev_idx);
            dungeon->event_button_index[i] = 0;
            break;
        }
    }
    event_delete_event_structure(ev_idx);
}

void event_update_on_battle_removal(void)
{
    struct Event *event;
    long i;
    for (i=0; i < EVENTS_COUNT; i++)
    {
        event = &game.event[i];
        if ((event->kind == EvKind_FriendlyFight) || (event->kind == EvKind_EnemyFight))
        {
            // Clear coords - new ones will be set during update_battle_events() call
            event->mappos_x = 0;
            event->mappos_y = 0;
        }
    }
}

/**
 * Returns event button index associated to given event.
 * @param event The event which associated button is to be found.
 * @param dungeon
 * @return Event button index, or -1 if the event doesn't have any button associated.
 */
int event_get_button_index(const struct Dungeon *dungeon, EventIndex evidx)
{
    int i;
    for (i=0; i <= EVENT_BUTTONS_COUNT; i++)
    {
        if (dungeon->event_button_index[i] == evidx) {
            return i;
        }
    }
    return -1;
}

void event_add_to_event_buttons_list_or_replace_button(struct Event *event, struct Dungeon *dungeon)
{
    if (dungeon->owner != event->owner) {
      ERRORLOG("Illegal my_event player allocation");
    }
    EventKind replace_evkind;
    replace_evkind = event_button_info[event->kind].replace_event_kind_button;
    long i,ev_idx;
    if (replace_evkind != EvKind_Nothing)
    {
        for (i=EVENT_BUTTONS_COUNT; i >= 0; i--)
        {
            ev_idx = dungeon->event_button_index[i];
            struct Event *event_prev;
            event_prev = &game.event[ev_idx];
            if ((event_prev->kind == event->kind) || (event_prev->kind == replace_evkind)) {
                SYNCDBG(1,"Replacing button at position %d",(int)i);
                dungeon->event_button_index[i] = event->index;
                break;
            }
        }
    } else {
        i = -1;
    }
    if (i < 0)
    {
        for (i=EVENT_BUTTONS_COUNT; i >= 0; i--)
        {
            ev_idx = dungeon->event_button_index[i];
            if (ev_idx == 0) {
                SYNCDBG(1,"New button at position %d",(int)i);
                dungeon->event_button_index[i] = event->index;
                break;
            }
        }
    }
    if (i < 0)
    {
        kill_oldest_my_event(dungeon);
        dungeon->event_button_index[EVENT_BUTTONS_COUNT] = event->index;
    }
}

void event_add_to_event_buttons_list(struct Event *event, struct Dungeon *dungeon)
{
    if (dungeon->owner != event->owner) {
      ERRORLOG("Illegal my_event player allocation");
    }
    long i,ev_idx;
    for (i=EVENT_BUTTONS_COUNT; i > 0; i--)
    {
        ev_idx = dungeon->event_button_index[i];
        if (ev_idx == 0)
        {
            dungeon->event_button_index[i] = event->index;
            break;
        }
    }
    if (i == 0)
    {
        kill_oldest_my_event(dungeon);
        dungeon->event_button_index[EVENT_BUTTONS_COUNT] = event->index;
    }
}

void event_reset_scroll_window(void)
{
  game.evntbox_scroll_window.start_y = 0;
  game.evntbox_scroll_window.action = 0;
  game.evntbox_scroll_window.text_height = 0;
  game.evntbox_scroll_window.window_height = 0;
}

void go_on_then_activate_the_event_box(PlayerNumber plyr_idx, EventIndex evidx)
{
    struct Dungeon *dungeon;
    struct CreatureData *crdata;
    struct DoorConfigStats *doorst;
    struct TrapConfigStats *trapst;
    struct RoomData *rdata;
    struct Event *event;
    struct Thing *thing;
    char *text;
    int i,k;
    short other_off;
    dungeon = get_players_num_dungeon(plyr_idx);
    event = &game.event[evidx];
    SYNCDBG(6,"Starting for event kind %d",event->kind);
    dungeon->visible_event_idx = evidx;
    if (is_my_player_number(plyr_idx))
    {
        i = event_button_info[event->kind].msgstr_id;
        strcpy(game.evntbox_scroll_window.text, gui_string(i));
    }
    if ((event->kind == EvKind_FriendlyFight) || (event->kind == EvKind_EnemyFight)) {
        dungeon->visible_battles[0] = find_first_battle_of_mine(plyr_idx);
    }
    if (is_my_player_number(plyr_idx))
    {
        other_off = 0;
        switch (event->kind)
        {
        case EvKind_HeartAttacked:
        case EvKind_Breach:
            other_off = 1;
            turn_on_menu(GMnu_TEXT_INFO);
            break;
        case EvKind_EnemyFight:
        case EvKind_FriendlyFight:
            turn_off_menu(GMnu_TEXT_INFO);
            turn_on_menu(GMnu_BATTLE);
            break;
        case EvKind_Objective:
            strcpy(game.evntbox_scroll_window.text, game.evntbox_text_objective);
            for (i=EVENT_BUTTONS_COUNT; i >= 0; i--)
            {
              k = dungeon->event_button_index[i];
              if (game.event[k%EVENTS_COUNT].kind == EvKind_Objective)
              {
                  other_off = 1;
                  turn_on_menu(GMnu_TEXT_INFO);
                  new_objective = 0;
                  break;
              }
            }
            break;
        case EvKind_NewRoomResrch:
            other_off = 1;
            rdata = room_data_get_for_kind(event->target);
            i = rdata->msg1str_idx;
            text = buf_sprintf("%s:\n%s",game.evntbox_scroll_window.text, cmpgn_string(i));
            strncpy(game.evntbox_scroll_window.text,text,MESSAGE_TEXT_LEN-1);
            turn_on_menu(GMnu_TEXT_INFO);
            break;
        case EvKind_NewCreature:
            other_off = 1;
            thing = thing_get(event->target);
            // If thing is invalid - leave the message without it.
            // Otherwise, put creature type in it.
            if (!thing_is_invalid(thing))
            {
              crdata = creature_data_get_from_thing(thing);
              i = crdata->namestr_idx;
              text = buf_sprintf("%s:\n%s", game.evntbox_scroll_window.text, cmpgn_string(i));
              strncpy(game.evntbox_scroll_window.text,text,MESSAGE_TEXT_LEN-1);
            }
            turn_on_menu(GMnu_TEXT_INFO);
            break;
        case EvKind_NewSpellResrch:
            other_off = 1;
            i = get_power_name_strindex(event->target);
            text = buf_sprintf("%s:\n%s", game.evntbox_scroll_window.text, cmpgn_string(i));
            strncpy(game.evntbox_scroll_window.text,text,MESSAGE_TEXT_LEN-1);
            turn_on_menu(GMnu_TEXT_INFO);
            break;
        case EvKind_NewTrap:
            other_off = 1;
            trapst = get_trap_model_stats(event->target);
            i = trapst->name_stridx;
            text = buf_sprintf("%s:\n%s", game.evntbox_scroll_window.text, cmpgn_string(i));
            strncpy(game.evntbox_scroll_window.text,text,MESSAGE_TEXT_LEN-1);
            turn_on_menu(GMnu_TEXT_INFO);
            break;
        case EvKind_NewDoor:
            other_off = 1;
            doorst = get_door_model_stats(event->target);
            i = doorst->name_stridx;
            text = buf_sprintf("%s:\n%s", game.evntbox_scroll_window.text, cmpgn_string(i));
            strncpy(game.evntbox_scroll_window.text,text,MESSAGE_TEXT_LEN-1);
            turn_on_menu(GMnu_TEXT_INFO);
            break;
        case EvKind_CreatrScavenged: // Scavenge detected
            other_off = 1;
            thing = thing_get(event->target);
            // If thing is invalid - leave the message without it.
            // Otherwise, put creature type in it.
            if (!thing_is_invalid(thing))
            {
              crdata = creature_data_get_from_thing(thing);
              i = crdata->namestr_idx;
              text = buf_sprintf("%s:\n%s", game.evntbox_scroll_window.text, cmpgn_string(i));
              strncpy(game.evntbox_scroll_window.text,text,MESSAGE_TEXT_LEN-1);
            }
            turn_on_menu(GMnu_TEXT_INFO);
            break;
        case EvKind_TreasureRoomFull:
        case EvKind_AreaDiscovered:
            other_off = 1;
            turn_on_menu(GMnu_TEXT_INFO);
            break;
        case EvKind_CreaturePayday:
            other_off = 1;
            text = buf_sprintf("%s:\n %d", game.evntbox_scroll_window.text, event->target);
            strncpy(game.evntbox_scroll_window.text,text,MESSAGE_TEXT_LEN-1);
            turn_on_menu(GMnu_TEXT_INFO);
            break;
        case EvKind_SpellPickedUp:
            other_off = 1;
            thing = thing_get(event->target);
            if (thing_is_invalid(thing))
              break;
            i = get_power_name_strindex(book_thing_to_magic(thing));
            text = buf_sprintf("%s:\n%s", game.evntbox_scroll_window.text, cmpgn_string(i));
            strncpy(game.evntbox_scroll_window.text,text,MESSAGE_TEXT_LEN-1);
            turn_on_menu(GMnu_TEXT_INFO);
            break;
        case EvKind_RoomTakenOver:
        case EvKind_RoomUnreachable:
            other_off = 1;
            rdata = room_data_get_for_kind(event->target);
            i = rdata->msg1str_idx;
            text = buf_sprintf("%s:\n%s",game.evntbox_scroll_window.text,cmpgn_string(i));
            strncpy(game.evntbox_scroll_window.text,text,MESSAGE_TEXT_LEN-1);
            turn_on_menu(GMnu_TEXT_INFO);
            break;
        case EvKind_CreatrIsAnnoyed:
            other_off = 1;
            thing = thing_get(event->target);
            // If thing is invalid - leave the message without it.
            // Otherwise, put creature type in it.
            if (!thing_is_invalid(thing))
            {
              crdata = creature_data_get_from_thing(thing);
              i = crdata->namestr_idx;
              text = buf_sprintf("%s:\n%s", game.evntbox_scroll_window.text, cmpgn_string(i));
              strncpy(game.evntbox_scroll_window.text,text,MESSAGE_TEXT_LEN-1);
            }
            turn_on_menu(GMnu_TEXT_INFO);
            break;
        case EvKind_NoMoreLivingSet:
        case EvKind_AlarmTriggered:
        case EvKind_RoomUnderAttack:
        case EvKind_NeedTreasureRoom:
        case EvKind_RoomLost:
        case EvKind_CreatrHungry:
            other_off = 1;
            turn_on_menu(GMnu_TEXT_INFO);
            break;
        case EvKind_Information:
            i = (long)event->target;
            if (i < 0)
            {
              i = -i;
              event->target = i;
            }
            strncpy(game.evntbox_text_buffer, cmpgn_string(i), MESSAGE_TEXT_LEN-1);
            strncpy(game.evntbox_scroll_window.text, game.evntbox_text_buffer, MESSAGE_TEXT_LEN-1);
            other_off = 1;
            turn_on_menu(GMnu_TEXT_INFO);
            break;
        case EvKind_TrapCrateFound:
            other_off = 1;
            thing = thing_get(event->target);
            if (thing_is_invalid(thing))
              break;
            trapst = get_trap_model_stats(crate_thing_to_workshop_item_model(thing));
            i = trapst->name_stridx;
            text = buf_sprintf("%s:\n%s", game.evntbox_scroll_window.text, cmpgn_string(i));
            strncpy(game.evntbox_scroll_window.text,text,MESSAGE_TEXT_LEN-1);
            turn_on_menu(GMnu_TEXT_INFO);
            break;
        case EvKind_DoorCrateFound:
            other_off = 1;
            thing = thing_get(event->target);
            if (thing_is_invalid(thing))
              break;
            doorst = get_door_model_stats(crate_thing_to_workshop_item_model(thing));
            i = doorst->name_stridx;
            text = buf_sprintf("%s:\n%s", game.evntbox_scroll_window.text, cmpgn_string(i));
            strncpy(game.evntbox_scroll_window.text,text,MESSAGE_TEXT_LEN-1);
            turn_on_menu(GMnu_TEXT_INFO);
            break;
        case EvKind_DnSpecialFound:
            other_off = 1;
            thing = thing_get(event->target);
            if (thing_is_invalid(thing))
              break;
            i = specials_text[box_thing_to_special(thing)];
            text = buf_sprintf("%s:\n%s", game.evntbox_scroll_window.text, cmpgn_string(i));
            strncpy(game.evntbox_scroll_window.text,text,MESSAGE_TEXT_LEN-1);
            turn_on_menu(GMnu_TEXT_INFO);
            break;
        case EvKind_QuickInformation:
            i = (long)event->target;
            if (i < 0)
            {
              i = -i;
              event->target = i;
            }
            strncpy(game.evntbox_text_buffer, gameadd.quick_messages[i%QUICK_MESSAGES_COUNT], MESSAGE_TEXT_LEN-1);
            strncpy(game.evntbox_scroll_window.text, game.evntbox_text_buffer, MESSAGE_TEXT_LEN-1);
            other_off = 1;
            turn_on_menu(GMnu_TEXT_INFO);
            break;
        default:
            ERRORLOG("Undefined event kind: %d", (int)event->kind);
            break;
        }
        event_reset_scroll_window();
        if (other_off)
        {
            turn_off_menu(GMnu_BATTLE);
            turn_off_menu(GMnu_DUNGEON_SPECIAL);
            turn_off_menu(GMnu_RESURRECT_CREATURE);
            turn_off_menu(GMnu_TRANSFER_CREATURE);
        }
    }
    SYNCDBG(8,"Finished");
}

void maintain_my_event_list(struct Dungeon *dungeon)
{
    //_DK_maintain_my_event_list(dungeon); return;
    int i;
    for (i=1; i <= EVENT_BUTTONS_COUNT; i++)
    {
        unsigned char curr_ev_idx;
        curr_ev_idx = dungeon->event_button_index[i];
        if (curr_ev_idx != 0)
        {
            if (dungeon->event_button_index[i-1] == 0)
            {
                dungeon->event_button_index[i-1] = curr_ev_idx;
                dungeon->event_button_index[i] = 0;
                struct Event *event;
                event = &game.event[curr_ev_idx];
                if (((event->flags & EvF_BtnFirstFall) != 0) || event->falling_button)
                {
                    if ((i == 1) || ((i >= 2) && dungeon->event_button_index[i-2] != 0))
                    {
                        if (is_my_player_number(dungeon->owner)) {
                            play_non_3d_sample(175);
                        }
                        unsigned char prev_ev_idx;
                        prev_ev_idx = dungeon->event_button_index[i-1];
                        event = &game.event[prev_ev_idx];
                        event->flags &= ~EvF_BtnFirstFall;
                        event->falling_button = 0;
                    }
                }
            }
        }
    }
}

void kill_oldest_my_event(struct Dungeon *dungeon)
{
    struct Event *event;
    long old_idx;
    long old_birth;
    long i,k;
    old_idx = -1;
    old_birth = 2147483647;
    for (i=EVENT_BUTTONS_COUNT; i > 0; i--)
    {
        k = dungeon->event_button_index[i];
        event = &game.event[k];
        if ((event->lifespan_turns >= 0) && (event->lifespan_turns < old_birth))
        {
          old_idx = k;
          old_birth = event->lifespan_turns;
        }
    }
    if (old_idx >= 0)
      event_delete_event(dungeon->owner, old_idx);
    maintain_my_event_list(dungeon);
}

void maintain_all_players_event_lists(void)
{
    struct PlayerInfo *player;
    struct Dungeon *dungeon;
    long i;
    for (i=0; i < PLAYERS_COUNT; i++)
    {
        player = get_player(i);
        if (player_exists(player))
        {
            dungeon = get_players_dungeon(player);
            maintain_my_event_list(dungeon);
        }
    }
}

struct Thing *event_is_attached_to_thing(EventIndex ev_idx)
{
    struct Event *event;
    long i;
    event = &game.event[ev_idx];
    switch (event->kind)
    {
    case EvKind_Objective:
    case EvKind_NewCreature:
    case EvKind_CreatrScavenged:
    case EvKind_SpellPickedUp:
    case EvKind_CreatrIsAnnoyed:
    case EvKind_NoMoreLivingSet:
    case EvKind_TrapCrateFound:
    case EvKind_DoorCrateFound:
    case EvKind_DnSpecialFound:
        i = event->target;
        break;
    default:
        i = 0;
        break;
    }
    return thing_get(i);
}

void event_process_events(void)
{
    //_DK_event_process_events();
    struct Event *event;
    long i;
    for (i=0; i < EVENTS_COUNT; i++)
    {
        event = &game.event[i];
        if ((event->flags & EvF_Exists) == 0) {
            continue;
        }
        if (event->lifespan_turns > 0) {
            event->lifespan_turns--;
        }
        if (event->lifespan_turns <= 0)
        {
            int ev_owner;
            ev_owner = event->owner;
            int subev_idx;
            subev_idx = event->index;
            struct Dungeon *dungeon;
            dungeon = get_dungeon(ev_owner);
            if (dungeon->visible_event_idx != subev_idx)
            {
                struct Event *subevent;
                subevent = &game.event[subev_idx];
                event_update_last_use(subevent);
                int i;
                for (i=0; i <= EVENT_BUTTONS_COUNT; i++)
                {
                    if (dungeon->event_button_index[i] == subev_idx) {
                        turn_off_event_box_if_necessary(ev_owner, dungeon->event_button_index[i]);
                        dungeon->event_button_index[i] = 0;
                        break;
                    }
                }
                event_delete_event_structure(subev_idx);
            }
        }
    }
}

void update_all_events(void)
{
    struct Thing *thing;
    struct Event *event;
    long i;
    //_DK_update_all_events(); return;
    for (i=EVENT_BUTTONS_COUNT; i > 0; i--)
    {
        thing = event_is_attached_to_thing(i);
        if (!thing_is_invalid(thing))
        {
            event = &game.event[i];
            if ((thing->class_id == TCls_Creature) && thing_is_picked_up(thing))
            {
                event->mappos_x = 0;
                event->mappos_y = 0;
            } else
            {
                event->mappos_x = thing->mappos.x.val;
                event->mappos_y = thing->mappos.y.val;
            }
        }
    }
    maintain_all_players_event_lists();
}

void event_kill_all_players_events(long plyr_idx)
{
    SYNCDBG(8,"Starting");
    _DK_event_kill_all_players_events(plyr_idx);
}

void remove_events_thing_is_attached_to(struct Thing *thing)
{
    _DK_remove_events_thing_is_attached_to(thing);
}

void clear_events(void)
{
    int i;
    for (i=0; i < EVENTS_COUNT; i++)
    {
      memset(&game.event[i], 0, sizeof(struct Event));
    }
    memset(&game.evntbox_scroll_window, 0, sizeof(struct TextScrollWindow));
    memset(&game.evntbox_text_buffer, 0, MESSAGE_TEXT_LEN);
    memset(&game.evntbox_text_objective, 0, MESSAGE_TEXT_LEN);
    for (i=0; i < 5; i++)
    {
      memset(&game.bookmark[i], 0, sizeof(struct Bookmark));
    }
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
