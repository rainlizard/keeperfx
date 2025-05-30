/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file creature_states_pray.c
 *     Creature state machine functions related to temple.
 * @par Purpose:
 *     Defines elements of states[] array, containing valid creature states.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     23 Sep 2009 - 05 Jan 2011
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "creature_states_pray.h"
#include "globals.h"

#include "bflib_math.h"
#include "creature_states.h"
#include "creature_instances.h"
#include "creature_states_rsrch.h"
#include "creature_states_mood.h"
#include "thing_list.h"
#include "creature_control.h"
#include "config_creature.h"
#include "config_rules.h"
#include "config_terrain.h"
#include "creature_graphics.h"
#include "thing_stats.h"
#include "thing_objects.h"
#include "thing_effects.h"
#include "thing_navigate.h"
#include "thing_physics.h"
#include "room_data.h"
#include "room_jobs.h"
#include "room_workshop.h"
#include "power_hand.h"
#include "gui_soundmsgs.h"
#include "game_legacy.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#ifdef __cplusplus
}
#endif
/******************************************************************************/
TbBool creature_is_doing_temple_pray_activity(const struct Thing *thing)
{
    long i = get_creature_state_besides_interruptions(thing);
    if ((i == CrSt_AtTemple) || (i == CrSt_PrayingInTemple))
        return true;
    return false;
}

CrStateRet process_temple_visuals(struct Thing *creatng, struct Room *room)
{
    struct CreatureControl* cctrl = creature_control_get_from_thing(creatng);
    if (cctrl->instance_id != CrInst_NULL)
        return CrStRet_Unchanged;
    long turns_in_temple = cctrl->turns_at_job;
    if (turns_in_temple <= 120)
    {
        // Walk for 120 turns
        if (creature_setup_adjacent_move_for_job_within_room(creatng, room, Job_TEMPLE_PRAY)) {
            creatng->continue_state = get_continue_state_for_job(Job_TEMPLE_PRAY);
        }
    } else
    if (turns_in_temple < 120 + 50)
    {
        // Then celebrate for 50 turns
        set_creature_instance(creatng, CrInst_CELEBRATE_SHORT, 0, 0);
    } else
    {
        // Then start from the beginning
        cctrl->turns_at_job = 0;
    }
    return CrStRet_Modified;
}

// This is state-process function of a creature
short at_temple(struct Thing *thing)
{
    struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
    cctrl->target_room_id = 0;
    struct Room* room = get_room_thing_is_on(thing);
    if (!room_initially_valid_as_type_for_thing(room, get_room_role_for_job(Job_TEMPLE_PRAY), thing))
    {
        WARNLOG("Room %s owned by player %d is invalid for %s index %d",room_code_name(room->kind),(int)room->owner,thing_model_name(thing),(int)thing->index);
        set_start_state(thing);
        return 0;
    }
    struct Dungeon* dungeon = get_dungeon(thing->owner);
    if (!add_creature_to_work_room(thing, room, Job_TEMPLE_PRAY))
    {
        output_room_message(room->owner, room->kind, OMsg_RoomTooSmall);
        remove_creature_from_work_room(thing);
        set_start_state(thing);
        return 0;
    }
    internal_set_thing_state(thing, get_continue_state_for_job(Job_TEMPLE_PRAY));
    dungeon->creatures_praying[thing->model]++;
    cctrl->turns_at_job = 0;
    return 1;
}

// This is state-process function of a creature
CrStateRet praying_in_temple(struct Thing *thing)
{
    TRACE_THING(thing);
    struct Room* room = get_room_thing_is_on(thing);
    if (creature_job_in_room_no_longer_possible(room, Job_TEMPLE_PRAY, thing))
    {
        remove_creature_from_work_room(thing);
        set_start_state(thing);
        return CrStRet_ResetFail;
    }
    switch (process_temple_function(thing))
    {
    case CrCkRet_Deleted:
        return CrStRet_Deleted;
    case CrCkRet_Available:
        process_temple_visuals(thing, room);
        return CrStRet_Modified;
    default:
        return CrStRet_ResetOk;
    }
}

long process_temple_cure(struct Thing *creatng)
{
    TRACE_THING(creatng);
    if (creature_under_spell_effect(creatng, CSAfF_Disease))
    {
        clean_spell_effect(creatng, CSAfF_Disease);
    }
    if (creature_under_spell_effect(creatng, CSAfF_Chicken))
    {
        clean_spell_effect(creatng, CSAfF_Chicken);
    }
    // TODO: Make it configurable by room type.
    struct CreatureControl *cctrl = creature_control_get_from_thing(creatng);
    cctrl->temple_cure_gameturn = game.play_gameturn;
    return 1;
}

// This is state-movecheck function of a creature
CrCheckRet process_temple_function(struct Thing *thing)
{
    struct Room* room = get_room_thing_is_on(thing);
    if (!room_still_valid_as_type_for_thing(room, get_room_role_for_job(Job_TEMPLE_PRAY), thing))
    {
        remove_creature_from_work_room(thing);
        set_start_state(thing);
        return CrCkRet_Continue;
    }
    { // Modify anger
        struct CreatureModelConfig* crconf = creature_stats_get_from_thing(thing);
        long anger_change = process_work_speed_on_work_value(thing, crconf->annoy_in_temple);
        anger_apply_anger_to_creature(thing, anger_change, AngR_Other, 1);
    }
    // Terminate spells
    process_temple_cure(thing);
    {
        struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
        cctrl->turns_at_job++;
    }
    return CrCkRet_Available;
}

short state_cleanup_in_temple(struct Thing *creatng)
{
    struct Dungeon* dungeon = get_dungeon(creatng->owner);
    if ( dungeon->creatures_praying[creatng->model] > 0 )
    {
       dungeon->creatures_praying[creatng->model]--;
    }
    else
    {
        ERRORLOG("No creature in temple to cleanup");
    }
    state_cleanup_in_room(creatng);
    return 1;
}

TbBool summon_creature(long model, struct Coord3d *pos, long owner, CrtrExpLevel exp_level)
{
    SYNCDBG(4,"Creating model %ld for player %ld",model,owner);
    if (!creature_count_below_map_limit(0))
    {
        SYNCLOG("Summon creature %s failed to due to map creature limit", creature_code_name(model));
        return false;
    }
    struct Thing* thing = create_creature(pos, model, owner);
    if (thing_is_invalid(thing))
    {
        ERRORLOG("Could not create creature");
        return false;
    }
    init_creature_level(thing, exp_level);
    internal_set_thing_state(thing, CrSt_CreatureBeingSummoned);
    thing->movement_flags |= TMvF_BeingSacrificed;
    struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
    cctrl->sacrifice.word_9C = 48;
    return true;
}

TbBool add_anger_to_all_creatures_of_player(PlayerNumber plyr_idx, short percentage)
{
    SYNCDBG(8, "Starting");
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    unsigned long k = 0;
    int i = dungeon->creatr_list_start;
    while (i != 0)
    {
        struct Thing* thing = thing_get(i);
        TRACE_THING(thing);
        struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
        if (thing_is_invalid(thing) || creature_control_invalid(cctrl))
        {
            ERRORLOG("Jump to invalid creature detected");
            break;
        }
        i = cctrl->players_next_creature_idx;
        // Thing list loop body
        anger_give_creatures_annoyance_percentage(thing, percentage, AngR_Other);
        // Thing list loop body ends
        k++;
        if (k > CREATURES_COUNT)
        {
            ERRORLOG("Infinite loop detected when sweeping creatures list");
            break;
        }
    }
    SYNCDBG(19, "Finished");
    return true;
}

TbBool make_all_players_creatures_angry(long plyr_idx)
{
    SYNCDBG(8,"Starting");
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    unsigned long k = 0;
    int i = dungeon->creatr_list_start;
    while (i != 0)
    {
        struct Thing* thing = thing_get(i);
        TRACE_THING(thing);
        struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
        if (thing_is_invalid(thing) || creature_control_invalid(cctrl))
        {
            ERRORLOG("Jump to invalid creature detected");
            break;
        }
        i = cctrl->players_next_creature_idx;
        // Thing list loop body
        anger_make_creature_angry(thing, AngR_Other);
        // Thing list loop body ends
        k++;
        if (k > CREATURES_COUNT)
        {
            ERRORLOG("Infinite loop detected when sweeping creatures list");
            break;
        }
    }
    SYNCDBG(19,"Finished");
    return true;
}

TbBool anger_make_creature_happy(struct Thing* creatng)
{
    struct CreatureModelConfig* crconf = creature_stats_get_from_thing(creatng);
    if ((crconf->annoy_level <= 0))
        return false;
    if (!anger_free_for_anger_decrease(creatng))
    {
        return false;
    }
    creature_be_happy(creatng);
    anger_set_creature_anger_all_types(creatng, 0);
    return true;
}

TbBool make_all_players_creatures_happy(long plyr_idx)
{
    SYNCDBG(8, "Starting");
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    unsigned long k = 0;
    int i = dungeon->creatr_list_start;
    while (i != 0)
    {
        struct Thing* thing = thing_get(i);
        TRACE_THING(thing);
        struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
        if (thing_is_invalid(thing) || creature_control_invalid(cctrl))
        {
            ERRORLOG("Jump to invalid creature detected");
            break;
        }
        i = cctrl->players_next_creature_idx;
        // Thing list loop body
        anger_make_creature_happy(thing);
        // Thing list loop body ends
        k++;
        if (k > CREATURES_COUNT)
        {
            ERRORLOG("Infinite loop detected when sweeping creatures list");
            break;
        }
    }
    SYNCDBG(19, "Finished");
    return true;
}

long force_complete_current_manufacturing(long plyr_idx)
{
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    if (dungeon_invalid(dungeon))
    {
        ERRORLOG("Player %d cannot manufacture.",(int)plyr_idx);
        return 0;
    }
    if (dungeon->manufacture_class == TCls_Empty)
    {
        WARNLOG("No manufacture in progress for player %d",(int)plyr_idx);
        return 0;
    }
    int manufct_required = manufacture_points_required(dungeon->manufacture_class, dungeon->manufacture_kind);
    if (manufct_required <= 0)
    {
        WARNLOG("No points required to finish manufacture of class %d",(int)dungeon->manufacture_class);
        return 0;
    }
    long i = manufct_required << 8;
    if (i <= dungeon->manufacture_progress)
        i = dungeon->manufacture_progress;
    dungeon->manufacture_progress = i;
    return 0;
}

void apply_spell_effect_to_players_creatures(PlayerNumber plyr_idx, ThingModel crmodel, long spl_idx, CrtrExpLevel overchrg)
{
    SYNCDBG(8,"Starting");
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    unsigned long k = 0;

    TbBool need_spec_digger = (crmodel > 0) && creature_kind_is_for_dungeon_diggers_list(plyr_idx, crmodel);
    struct Thing* thing = INVALID_THING;
    int i;
    if ((!need_spec_digger) || (crmodel == CREATURE_ANY) || (crmodel == CREATURE_NOT_A_DIGGER))
    {
        i = dungeon->creatr_list_start;
    }
    else
    {
        i = dungeon->digger_list_start;
    }

    while (i != 0)
    {
        thing = thing_get(i);
        TRACE_THING(thing);
        struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
        if (thing_is_invalid(thing) || creature_control_invalid(cctrl))
        {
            ERRORLOG("Jump to invalid creature detected");
            break;
        }
        i = cctrl->players_next_creature_idx;
        // Thing list loop body
        if (creature_matches_model(thing,crmodel))
        {      
            apply_spell_effect_to_thing(thing, spl_idx, overchrg, plyr_idx);
        }
        // Thing list loop body ends
        k++;
        if (k > CREATURES_COUNT)
        {
            ERRORLOG("Infinite loop detected when sweeping creatures list");
            break;
        }
    }
    SYNCDBG(19,"Finished");
}

TbBool kill_creature_if_under_chicken_spell(struct Thing *thing)
{
    if (creature_under_spell_effect(thing, CSAfF_Chicken) && !thing_is_picked_up(thing))
    {
        thing->health = -1;
        return true;
    }
    SYNCDBG(19, "Skipped %s index %d", thing_model_name(thing), (int)thing->index);
    return false;
}

void kill_all_players_chickens(PlayerNumber plyr_idx)
{
    SYNCDBG(18,"Starting");
    const struct StructureList* slist = get_list_for_thing_class(TCls_Object);
    unsigned long k = 0;
    int i = slist->index;
    while (i != 0)
    {
        struct Thing* thing = thing_get(i);
        if (thing_is_invalid(thing))
        {
            ERRORLOG("Jump to invalid thing detected");
            break;
        }
        i = thing->next_of_class;
        // Per-thing code
        if (thing_exists(thing) && thing_is_mature_food(thing) && (thing->owner == plyr_idx)
          && !thing_is_picked_up(thing)) {
            thing->food.some_chicken_was_sacrificed = true;
        }
        // Per-thing code ends
        k++;
        if (k > slist->count)
        {
            ERRORLOG("Infinite loop detected when sweeping things list");
            break;
        }
    }
    // Force leave or kill normal creatures and special diggers
    do_to_players_all_creatures_of_model(plyr_idx, CREATURE_ANY, kill_creature_if_under_chicken_spell);
}

// This is state-process function of a creature
short creature_being_summoned(struct Thing *thing)
{
    struct CreatureControl *cctrl;
    short orig_w;
    short orig_h;
    short unsc_w;
    short unsc_h;
    cctrl = creature_control_get_from_thing(thing);
    if (creature_control_invalid(cctrl)) {
        return 0;
    }
    if (cctrl->sacrifice.word_9A <= 0)
    {
        get_keepsprite_unscaled_dimensions(thing->anim_sprite, thing->move_angle_xy, thing->current_frame, &orig_w, &orig_h, &unsc_w, &unsc_h);
        create_effect(&thing->mappos, TngEff_Explosion4, thing->owner);
        thing->movement_flags |= TMvF_BeingSacrificed;
        cctrl->sacrifice.word_9A = 1;
        cctrl->sacrifice.word_9C = 48;//orig_h;
        return 0;
    }
    cctrl->sacrifice.word_9A++;
    if (cctrl->sacrifice.word_9A > cctrl->sacrifice.word_9C)
    {
        thing->movement_flags &= ~TMvF_BeingSacrificed;
        set_start_state(thing);
        return 0;
    }
    // Rotate the creature as it appears from temple
    creature_turn_to_face_angle(thing, thing->move_angle_xy + LbFPMath_PI/4);
    return 0;
}

short cleanup_sacrifice(struct Thing *creatng)
{
    // If the creature has flight ability, return it to flying state
    restore_creature_flight_flag(creatng);
    creatng->movement_flags &= ~TMvF_BeingSacrificed;
    return 1;
}

TbBool tally_sacrificed_imps(PlayerNumber plyr_idx, short count)
{
    struct Dungeon* dungeon;
    dungeon = get_dungeon(plyr_idx);
    if (dungeon_invalid(dungeon)) {
        ERRORDBG(11, "Can't change imp price, player %d has no dungeon.", (int)plyr_idx);
        return false;
    }
    dungeon->cheaper_diggers += count;
    return true;
}

long create_sacrifice_unique_award(struct Coord3d *pos, PlayerNumber plyr_idx, long sacfunc, CrtrExpLevel exp_level)
{
  switch (sacfunc)
  {
  case UnqF_MkAllAngry:
      make_all_players_creatures_angry(plyr_idx);
      return SacR_Punished;
  case UnqF_MkAllVerAngry:
      add_anger_to_all_creatures_of_player(plyr_idx,201);
      return SacR_Punished;
  case UnqF_ComplResrch:
      force_complete_current_research(plyr_idx);
      return SacR_Awarded;
  case UnqF_ComplManufc:
      force_complete_current_manufacturing(plyr_idx);
      return SacR_Awarded;
  case UnqF_KillChickns:
      kill_all_players_chickens(plyr_idx);
      return SacR_Punished;
  case UnqF_CheaperImp:
      tally_sacrificed_imps(plyr_idx,1);
      return SacR_Pleased;
  case UnqF_CostlierImp:
      tally_sacrificed_imps(plyr_idx, -1);
      return SacR_AngryWarn;
  case UnqF_MkAllHappy:
      make_all_players_creatures_happy(plyr_idx);
      return SacR_Awarded;
  default:
      ERRORLOG("Unsupported unique sacrifice award!");
      return SacR_AngryWarn;
  }
}

long creature_sacrifice_average_exp_level(struct Dungeon *dungeon, struct SacrificeRecipe *sac)
{
    long num = 0;
    long exp = 0;
    for (long i = 0; i < MAX_SACRIFICE_VICTIMS; i++)
    {
        long model = sac->victims[i];
        // Do not count the same model twice
        if (i > 0)
        {
            if (model == sac->victims[i - 1])
                break;
        }
        num += dungeon->creature_sacrifice[model];
        exp += dungeon->creature_sacrifice_exp[model];
  }
  if (num < 1) num = 1;
  exp = (exp/num);
  if (exp < 0) return 0;
  return exp;
}

void creature_sacrifice_reset(struct Dungeon *dungeon, struct SacrificeRecipe *sac)
{
  // Some models may be set more than once; dut we don't really care...
  for (long i = 0; i < MAX_SACRIFICE_VICTIMS; i++)
  {
      long model = sac->victims[i];
      dungeon->creature_sacrifice[model] = 0;
      dungeon->creature_sacrifice_exp[model] = 0;
  }
}

static long sacrifice_victim_model_count(struct SacrificeRecipe *sac, ThingModel model)
{
    long k = 0;
    for (long i = 0; i < MAX_SACRIFICE_VICTIMS; i++)
    {
        if (sac->victims[i] == model)
            k++;
  }
  return k;
}

TbBool sacrifice_victim_conditions_met(struct Dungeon *dungeon, struct SacrificeRecipe *sac)
{
    // Some models may be checked more than once; dut we don't really care...
    for (long i = 0; i < MAX_SACRIFICE_VICTIMS; i++)
    {
        ThingModel model = sac->victims[i];
        if (model < 1)
            continue;
        long required = sacrifice_victim_model_count(sac, model);
        SYNCDBG(6, "Model %d (%s) exists %d times", (int)model, creature_code_name(model), (int)required);
        if (dungeon->creature_sacrifice[model] < required)
            return false;
  }
  return true;
}

long process_sacrifice_award(struct Coord3d *pos, ThingModel model, PlayerNumber plyr_idx)
{
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    if (dungeon_invalid(dungeon))
    {
        ERRORLOG("Player %d cannot sacrifice creatures.", (int)plyr_idx);
        return 0;
  }
  long ret = SacR_DontCare;
  struct SacrificeRecipe* sac = &game.conf.rules.sacrifices.sacrifice_recipes[0];
  do {
    // Check if the just sacrificed creature is in the sacrifice
    if (sacrifice_victim_model_count(sac,model) > 0)
    {
      // Set the return value in case of partial sacrifice recipe
      if (ret != SacR_Pleased)
      {
        switch (sac->action)
        {
        case SacA_MkGoodHero:
        case SacA_NegSpellAll:
        case SacA_NegUniqFunc:
          ret = SacR_AngryWarn;
          break;
        default:
          ret = SacR_Pleased;
          break;
        }
      }
      SYNCDBG(8,"Creature %d used in sacrifice %d",(int)model,(int)(sac-&game.conf.rules.sacrifices.sacrifice_recipes[0]));
      // Check if the complete sacrifice condition is met
      if (sacrifice_victim_conditions_met(dungeon, sac))
      {
        SYNCDBG(6,"Sacrifice recipe %d condition met, action %d for player %d",(int)(sac-&game.conf.rules.sacrifices.sacrifice_recipes[0]),(int)sac->action,(int)plyr_idx);
        CrtrExpLevel exp_level = creature_sacrifice_average_exp_level(dungeon, sac);
        switch (sac->action)
        {
        case SacA_MkCreature:
            if (exp_level >= CREATURE_MAX_LEVEL) exp_level = CREATURE_MAX_LEVEL-1;
            if ( summon_creature(sac->param, pos, plyr_idx, exp_level) )
            {
                dungeon->lvstats.creatures_from_sacrifice++;
                dungeon->creature_awarded[sac->param]++;
            }
            ret = SacR_Awarded;
            break;
        case SacA_MkGoodHero:
            if (exp_level >= CREATURE_MAX_LEVEL) exp_level = CREATURE_MAX_LEVEL-1;
            if ( summon_creature(sac->param, pos, 4, exp_level) )
              dungeon->lvstats.creatures_from_sacrifice++;
            ret = SacR_Punished;
            break;
        case SacA_NegSpellAll:
            if (exp_level > SPELL_MAX_LEVEL) exp_level = SPELL_MAX_LEVEL;
            apply_spell_effect_to_players_creatures(plyr_idx, CREATURE_NOT_A_DIGGER, sac->param, exp_level);
            ret = SacR_Punished;
            break;
        case SacA_PosSpellAll:
            if (exp_level > SPELL_MAX_LEVEL) exp_level = SPELL_MAX_LEVEL;
            apply_spell_effect_to_players_creatures(plyr_idx, CREATURE_NOT_A_DIGGER, sac->param, exp_level);
            ret = SacR_Awarded;
            break;
        case SacA_NegUniqFunc:
        case SacA_PosUniqFunc:
            ret = create_sacrifice_unique_award(pos, plyr_idx, sac->param, exp_level);
            break;
        case SacA_CustomReward:
            if (sac->param > 0) // Zero means do nothing
            {
                dungeon->script_flags[sac->param - 1]++;
            }
            ret = SacR_Awarded;
            break;
        case SacA_CustomPunish:
            if (sac->param > 0)
            {
                dungeon->script_flags[sac->param - 1]++;
            }
            ret = SacR_Punished;
            break;
        default:
            ERRORLOG("Unsupported sacrifice action %d!",(int)sac->action);
            ret = SacR_Pleased;
            break;
        }
        if ((ret != SacR_Pleased) && (ret != SacR_AngryWarn))
          creature_sacrifice_reset(dungeon, sac);
        return ret;
      }
    }
    sac++;
  } while (sac->action != SacA_None);
  return ret;
}

void process_sacrifice_creature(struct Coord3d *pos, ThingModel model, PlayerNumber owner, TbBool partial)
{
    long award = process_sacrifice_award(pos, model, owner);
    if (is_my_player_number(owner))
    {
      switch (award)
      {
      case SacR_AngryWarn:
          if (partial)
          {
              output_message(SMsg_SacrificeBad, 0);
          }
          break;
      case SacR_DontCare:
          if (partial)
          {
              output_message(SMsg_SacrificeNeutral, 0);
          }
          break;
      case SacR_Pleased:
          if (partial)
          {
              output_message(SMsg_SacrificeGood, 0);
          }
          break;
      case SacR_Awarded:
          output_message(SMsg_SacrificeReward, 0);
          break;
      case SacR_Punished:
          output_message(SMsg_SacrificePunish, 0);
          break;
      default:
          ERRORLOG("Invalid sacrifice return, %d",(int)award);
          break;
      }
    }
}

// This is state-process function of a creature
short creature_being_sacrificed(struct Thing *thing)
{
    SYNCDBG(6,"Starting");

    struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
    cctrl->sacrifice.word_9A--;
    if (cctrl->sacrifice.word_9A > 0)
    {
        // No flying while being sacrificed
        creature_turn_to_face_angle(thing, thing->move_angle_xy + LbFPMath_PI/4);
        thing->movement_flags &= ~TMvF_Flying;
        return 0;
    }
    struct SlabMap* slb = get_slabmap_for_subtile(thing->mappos.x.stl.num, thing->mappos.y.stl.num);
    long owner = slabmap_owner(slb);
    add_creature_to_sacrifice_list(owner, thing->model, cctrl->exp_level);
    struct Coord3d pos;
    pos.x.val = thing->mappos.x.val;
    pos.y.val = thing->mappos.y.val;
    pos.z.val = thing->mappos.z.val;
    long model = thing->model;

    memcpy(&game.triggered_object_location, &pos, sizeof(struct Coord3d));

    kill_creature(thing, INVALID_THING, -1, CrDed_NoEffects|CrDed_NotReallyDying);
    process_sacrifice_creature(&pos, model, owner, true);
    return -1;
}

// This is state-process function of a creature
short creature_sacrifice(struct Thing *thing)
{
    if ((thing->movement_flags & TMvF_Flying) != 0) {
        thing->movement_flags &= ~TMvF_Flying;
    }
    struct Coord3d pos;
    pos.x.val = subtile_coord_center(stl_slab_center_subtile(thing->mappos.x.stl.num));
    pos.y.val = subtile_coord_center(stl_slab_center_subtile(thing->mappos.y.stl.num));
    pos.z.val = thing->mappos.z.val;
    if (!subtile_has_sacrificial_on_top(pos.x.stl.num, pos.y.stl.num)) {
        set_start_state(thing);
        return 1;
    }
    struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
    if (creature_move_to(thing, &pos, cctrl->max_speed, 0, 0) == 0) {
        return 0;
    }
    if (get_thing_height_at(thing, &pos) != pos.z.val) {
        return 0;
    }
    if (thing_touching_floor(thing))
    {
        cctrl->sacrifice.word_9A = 48;
        cctrl->sacrifice.word_9C = 48;
        thing->movement_flags |= TMvF_BeingSacrificed;
        internal_set_thing_state(thing, CrSt_CreatureBeingSacrificed);
        thing->creature.gold_carried = 0;
        struct SlabMap* slb = get_slabmap_thing_is_on(thing);
        create_effect(&thing->mappos, TngEff_TempleSplash, slabmap_owner(slb));
    }
    return 1;
}

TbBool find_random_sacrifice_center(struct Coord3d *pos, const struct Room *room)
{
    // Find a random slab in the room to be used as our starting point
    long i = PLAYER_RANDOM(room->owner, room->slabs_count);
    unsigned long n = room->slabs_list;
    while (i > 0)
    {
        n = get_next_slab_number_in_room(n);
        i--;
    }
    // Now loop starting from that point
    i = room->slabs_count;
    while (i > 0)
    {
        // Loop the slabs list
        if (n <= 0) {
            n = room->slabs_list;
        }
        MapSlabCoord slb_x = slb_num_decode_x(n);
        MapSlabCoord slb_y = slb_num_decode_y(n);
        if  (slab_is_area_inner_fill(slb_x, slb_y))
        {
            // In case we will select a column on that subtile, do 3 tries
            pos->x.val = subtile_coord_center(slab_subtile_center(slb_x));
            pos->y.val = subtile_coord_center(slab_subtile_center(slb_y));
            pos->z.val = subtile_coord(1,0);
            struct Map* mapblk = get_map_block_at(pos->x.stl.num, pos->y.stl.num);
            if (((mapblk->flags & SlbAtFlg_Blocking) == 0) && ((mapblk->flags & SlbAtFlg_IsDoor) == 0)
                && (get_navigation_map_floor_height(pos->x.stl.num, pos->y.stl.num) < 4)
                )
            {
                return true;
            }
        }
        n = get_next_slab_number_in_room(n);
        i--;
    }
    return false;
}

TbBool find_temple_pool(int player_idx, struct Coord3d *pos)
{
    struct Room *best_room = NULL;
    long max_value = 0;
    struct Dungeon *dungeon = get_dungeon(player_idx);
    if (dungeon == INVALID_DUNGEON)
        return false;

    for (RoomKind rkind = 0; rkind < game.conf.slab_conf.room_types_count; rkind++)
    {
        if(room_role_matches(rkind, RoRoF_CrSacrifice))
        {
            int k = 0, i = dungeon->room_kind[rkind];
            while (i != 0)
            {
                struct Room* room = room_get(i);
                if (room_is_invalid(room))
                {
                    ERRORLOG("Jump to invalid room detected");
                    break;
                }
                i = room->next_of_owner;
                // Per-room code
                if (find_random_sacrifice_center(pos, room))
                {
                    if (max_value < room->total_capacity)
                    {
                        max_value = room->total_capacity;
                        best_room = room;
                    }
                }
                // Per-room code ends
                k++;
                if (k > ROOMS_COUNT)
                {
                ERRORLOG("Infinite loop detected when sweeping rooms list");
                break;
                }
            }
        }
    }

    if (room_is_invalid(best_room))
    {
        WARNLOG("No temple to spawn a creature");
        memset(pos, 0, sizeof(*pos));
        return false; // No room
    }

    return true;
}

static int sac_compare_fn(const void *ptr_a, const void *ptr_b)
{
    const char *a = (const char*)ptr_a;
    const char *b = (const char*)ptr_b;
    return *a < *b;
}

void script_set_sacrifice_recipe(const int action, const int param, ThingModel* victims)
{
    qsort(victims, MAX_SACRIFICE_VICTIMS, sizeof(ThingModel), &sac_compare_fn);
    for (int i = 1; i < MAX_SACRIFICE_RECIPES; i++)
    {
        struct SacrificeRecipe* sac = &game.conf.rules.sacrifices.sacrifice_recipes[i];
        if (sac->action == (long)SacA_None)
        {
            break;
        }
        if (memcmp(victims, sac->victims, MAX_SACRIFICE_VICTIMS * sizeof(char)) == 0)
        {
            sac->action = action;
            sac->param = param;
            if (action == (long)SacA_None)
            {
                // Remove empty slot and shift remaining elements
                int index = sac - game.conf.rules.sacrifices.sacrifice_recipes;
                int remaining = MAX_SACRIFICE_RECIPES - index - 1;
                if (remaining > 0)
                {
                    memmove(sac, sac + 1, remaining * sizeof(*sac));
                }
            }
            return;
        }
    }

    if (action == (long)SacA_None) // No rule found to remove
    {
        WARNLOG("Unable to find sacrifice rule to remove");
        return;
    }

    struct SacrificeRecipe* sac = get_unused_sacrifice_recipe_slot();
    if (!sac)  // Properly check for NULL
    {
        ERRORLOG("No free sacrifice rules");
        return;
    }

    memcpy(sac->victims, victims, MAX_SACRIFICE_VICTIMS * sizeof(int));
    sac->action = action;
    sac->param = param;

    struct Coord3d temple_pos;
    for (int player_idx = 0; player_idx < DUNGEONS_COUNT; player_idx++)
    {
        if (find_temple_pool(player_idx, &temple_pos))
        {
            // Process the sacrifice if the pool already matches
            for (int i = 0; i < MAX_SACRIFICE_VICTIMS; i++)
            {
                if (victims[i] == 0)
                    break;
                process_sacrifice_creature(&temple_pos, victims[i], player_idx, false);
            }
        }
    }
}



/******************************************************************************/
