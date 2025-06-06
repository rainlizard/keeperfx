/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_crtrmodel.c
 *     Specific creature model configuration loading functions.
 * @par Purpose:
 *     Support of configuration files for specific creatures.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     25 May 2009 - 26 Jul 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "config_crtrmodel.h"
#include "globals.h"
#include "game_merge.h"
#include "game_legacy.h"

#include "bflib_basics.h"
#include "bflib_math.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h"

#include "config.h"
#include "thing_doors.h"
#include "thing_list.h"
#include "thing_stats.h"
#include "config_campaigns.h"
#include "config_creature.h"
#include "config_magic.h"
#include "config_terrain.h"
#include "config_lenses.h"
#include "creature_control.h"
#include "creature_graphics.h"
#include "creature_states.h"
#include "creature_states_mood.h"
#include "player_data.h"
#include "console_cmd.h"
#include "custom_sprites.h"
#include "lvl_script_lib.h"
#include "keeperfx.hpp"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

const struct NamedCommand creatmodel_attributes_commands[] = {
  {"NAME",                1},
  {"HEALTH",              2},
  {"HEALREQUIREMENT",     3},
  {"HEALTHRESHOLD",       4},
  {"STRENGTH",            5},
  {"ARMOUR",              6},
  {"DEXTERITY",           7},
  {"FEARWOUNDED",         8},
  {"FEARSTRONGER",        9},
  {"DEFENCE",            10},
  {"LUCK",               11},
  {"RECOVERY",           12},
  {"HUNGERRATE",         13},
  {"HUNGERFILL",         14},
  {"LAIRSIZE",           15},
  {"HURTBYLAVA",         16},
  {"BASESPEED",          17},
  {"GOLDHOLD",           18},
  {"SIZE",               19},
  {"ATTACKPREFERENCE",   20},
  {"PAY",                21},
  {"HEROVSKEEPERCOST",   22}, // Removed.
  {"SLAPSTOKILL",        23},
  {"CREATURELOYALTY",    24},
  {"LOYALTYLEVEL",       25},
  {"DAMAGETOBOULDER",    26},
  {"THINGSIZE",          27},
  {"PROPERTIES",         28},
  {"NAMETEXTID",         29},
  {"FEARSOMEFACTOR",     30},
  {"TOKINGRECOVERY",     31},
  {"CORPSEVANISHEFFECT", 32},
  {"FOOTSTEPPITCH",      33},
  {"LAIROBJECT",         34},
  {"PRISONKIND",         35},
  {"TORTUREKIND",        36},
  {"SPELLIMMUNITY",      37},
  {"HOSTILETOWARDS",     38},
  {NULL,                  0},
  };

const struct NamedCommand creatmodel_properties_commands[] = {
  {"BLEEDS",             1},
  {"UNAFFECTED_BY_WIND", 2}, // Deprecated, but retained in NamedCommand for backward compatibility.
  {"IMMUNE_TO_GAS",      3}, // Deprecated, but retained in NamedCommand for backward compatibility.
  {"HUMANOID_SKELETON",  4},
  {"PISS_ON_DEAD",       5},
  {"FLYING",             7},
  {"SEE_INVISIBLE",      8},
  {"PASS_LOCKED_DOORS",  9},
  {"SPECIAL_DIGGER",    10},
  {"ARACHNID",          11},
  {"DIPTERA",           12},
  {"LORD",              13},
  {"SPECTATOR",         14},
  {"EVIL",              15},
  {"NEVER_CHICKENS",    16}, // Deprecated, but retained in NamedCommand for backward compatibility.
  {"IMMUNE_TO_BOULDER", 17},
  {"NO_CORPSE_ROTTING", 18},
  {"NO_ENMHEART_ATTCK", 19},
  {"TREMBLING_FAT",     20},
  {"FEMALE",            21},
  {"INSECT",            22},
  {"ONE_OF_KIND",       23},
  {"NO_IMPRISONMENT",   24},
  {"IMMUNE_TO_DISEASE", 25}, // Deprecated, but retained in NamedCommand for backward compatibility.
  {"ILLUMINATED",       26},
  {"ALLURING_SCVNGR",   27},
  {"NO_RESURRECT",      28},
  {"NO_TRANSFER",       29},
  {"TREMBLING",         30},
  {"FAT",               31},
  {"NO_STEAL_HERO",     32},
  {"PREFER_STEAL",      33},
  {"EVENTFUL_DEATH",    34},
  {"DIGGING_CREATURE",  35},
  {NULL,                 0},
  };

const struct NamedCommand creatmodel_attraction_commands[] = {
  {"ENTRANCEROOM",       1},
  {"ROOMSLABSREQUIRED",  2},
  {"BASEENTRANCESCORE",  3},
  {"SCAVENGEREQUIREMENT",4},
  {"TORTURETIME",        5},
  {NULL,                 0},
  };

const struct NamedCommand creatmodel_annoyance_commands[] = {
  {"EATFOOD",              1},
  {"WILLNOTDOJOB",         2},
  {"INHAND",               3},
  {"NOLAIR",               4},
  {"NOHATCHERY",           5},
  {"WOKENUP",              6},
  {"STANDINGONDEADENEMY",  7},
  {"SULKING",              8},
  {"NOSALARY",             9},
  {"SLAPPED",             10},
  {"STANDINGONDEADFRIEND",11},
  {"INTORTURE",           12},
  {"INTEMPLE",            13},
  {"SLEEPING",            14},
  {"GOTWAGE",             15},
  {"WINBATTLE",           16},
  {"UNTRAINED",           17},
  {"OTHERSLEAVING",       18},
  {"JOBSTRESS",           19},
  {"QUEUE",               20},
  {"LAIRENEMY",           21},
  {"ANNOYLEVEL",          22},
  {"ANGERJOBS",           23},
  {"GOINGPOSTAL",         24},
  {NULL,                   0},
  };

const struct NamedCommand creatmodel_senses_commands[] = {
  {"HEARING",              1},
  {"EYEHEIGHT",            2},
  {"FIELDOFVIEW",          3},
  {"EYEEFFECT",            4},
  {"MAXANGLECHANGE",       5},
  {NULL,                   0},
  };

const struct NamedCommand creatmodel_appearance_commands[] = {
  {"WALKINGANIMSPEED",     1},
  {"VISUALRANGE",          2},
  {"POSSESSSWIPEINDEX",    3},
  {"NATURALDEATHKIND",     4},
  {"SHOTORIGIN",           5},
  {"CORPSEVANISHEFFECT",   6},
  {"FOOTSTEPPITCH",        7},
  {"PICKUPOFFSET",         8},
  {"STATUSOFFSET",         9},
  {"TRANSPARENCYFLAGS",   10},
  {"FIXEDANIMSPEED",      11},
  {NULL,                   0},
  };

const struct NamedCommand creature_deathkind_desc[] = {
    {"NORMAL",          Death_Normal},
    {"FLESHEXPLODE",    Death_FleshExplode},
    {"GASFLESHEXPLODE", Death_GasFleshExplode},
    {"SMOKEEXPLODE",    Death_SmokeExplode},
    {"ICEEXPLODE",      Death_IceExplode},
    {NULL,              0},
    };


const struct NamedCommand creatmodel_experience_commands[] = {
  {"POWERS",               1},
  {"POWERSLEVELREQUIRED",  2},
  {"LEVELSTRAINVALUES",    3},
  {"GROWUP",               4},
  {"SLEEPEXPERIENCE",      5},
  {"EXPERIENCEFORHITTING", 6},
  {"REBIRTH",              7},
  {NULL,                   0},
  };

const struct NamedCommand creatmodel_jobs_commands[] = {
  {"PRIMARYJOBS",          1},
  {"SECONDARYJOBS",        2},
  {"NOTDOJOBS",            3},
  {"STRESSFULJOBS",        4},
  {"TRAININGVALUE",        5},
  {"TRAININGCOST",         6},
  {"SCAVENGEVALUE",        7},
  {"SCAVENGERCOST",        8},
  {"RESEARCHVALUE",        9},
  {"MANUFACTUREVALUE",    10},
  {"PARTNERTRAINING",     11},
  {NULL,                   0},
  };
  
const struct NamedCommand creatmodel_sounds_commands[] = {
  {"HURT",                 CrSnd_Hurt},
  {"HIT",                  CrSnd_Hit},
  {"HAPPY",                CrSnd_Happy},
  {"SAD",                  CrSnd_Sad},
  {"HANG",                 CrSnd_Hang},
  {"DROP",                 CrSnd_Drop},
  {"TORTURE",              CrSnd_Torture},
  {"SLAP",                 CrSnd_Slap},
  {"DIE",                  CrSnd_Die},
  {"FOOT",                 CrSnd_Foot},
  {"FIGHT",                CrSnd_Fight},
  {"PISS",                 CrSnd_Piss},
  {NULL,                   0},
  };

/******************************************************************************/

void strtolower(char * str) {
    for (; *str; ++str) {
        *str = tolower(*str);
    }
}

TbBool parse_creaturemodel_attributes_blocks(long crtr_model,char *buf,long len,const char *config_textname,unsigned short flags)
{
  // Block name and parameter word store variables
  struct CreatureModelConfig* crconf = creature_stats_get(crtr_model);
  // Find the block
  const char * block_name = "attributes";
  long pos = 0;
  int k = find_conf_block(buf, &pos, len, block_name);
  if (k < 0)
  {
      if ((flags & CnfLd_AcceptPartial) == 0)
          WARNMSG("Block [%s] not found in %s file.", block_name, config_textname);
      return false;
  }
#define COMMAND_TEXT(cmd_num) get_conf_parameter_text(creatmodel_attributes_commands,cmd_num)
  while (pos<len)
  {
      // Finding command number in this line
      int cmd_num = recognize_conf_command(buf, &pos, len, creatmodel_attributes_commands);
      // Now store the config item in correct place
      if (cmd_num == ccr_endOfBlock) break; // if next block starts
      int n = 0;
      char word_buf[COMMAND_WORD_LEN];
      switch (cmd_num)
      {
      case 1: // NAME
          // Name is ignored - it was defined in creature.cfg
          break;
      case 2: // HEALTH
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->health = k;
            n++;
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 3: // HEALREQUIREMENT
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->heal_requirement = k;
            n++;
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 4: // HEALTHRESHOLD
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->heal_threshold = k;
            n++;
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 5: // STRENGTH
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->strength = k;
            n++;
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 6: // ARMOUR
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->armour = k;
            n++;
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 7: // DEXTERITY
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->dexterity = k;
            n++;
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 8: // FEARWOUNDED
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->fear_wounded = k;
            n++;
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 9: // FEARSTRONGER
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->fear_stronger = k;
            n++;
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 10: // DEFENCE
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->defense = k;
            n++;
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 11: // LUCK
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->luck = k;
            n++;
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 12: // RECOVERY
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->sleep_recovery = k;
            n++;
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 13: // HUNGERRATE
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->hunger_rate = k;
            n++;
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 14: // HUNGERFILL
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->hunger_fill = k;
            n++;
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 15: // LAIRSIZE
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->lair_size = k;
            n++;
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 16: // HURTBYLAVA
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->hurt_by_lava = k;
            n++;
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 17: // BASESPEED
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->base_speed = k;
            n++;
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 18: // GOLDHOLD
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->gold_hold = k;
            n++;
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 19: // SIZE
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->size_xy = k;
            n++;
          }
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->size_z = k;
            n++;
          }
          if (n < 2)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 20: // ATTACKPREFERENCE
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = get_id(attackpref_desc, word_buf);
            if (k > 0)
            {
              crconf->attack_preference = k;
              n++;
            }
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 21: // PAY
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->pay = k;
            n++;
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 22: // HEROVSKEEPERCOST
          break;
      case 23: // SLAPSTOKILL
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->slaps_to_kill = k;
            n++;
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 24: // CREATURELOYALTY
          // Unused
          break;
      case 25: // LOYALTYLEVEL
          // Unused
          break;
      case 26: // DAMAGETOBOULDER
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->damage_to_boulder = k;
            n++;
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 27: // THINGSIZE
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->thing_size_xy = k;
            n++;
          }
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->thing_size_z = k;
            n++;
          }
          if (n < 2)
          {
            CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s %s file.",
                COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
          }
          break;
      case 28: // PROPERTIES
          crconf->bleeds = false;
          crconf->humanoid_creature = false;
          crconf->piss_on_dead = false;
          crconf->flying = false;
          crconf->can_see_invisible = false;
          crconf->can_go_locked_doors = false;
          crconf->model_flags = 0;
          while (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = get_id(creatmodel_properties_commands, word_buf);
            switch (k)
            {
            case 1: // BLEEDS
              crconf->bleeds = true;
              n++;
              break;
            case 2: // UNAFFECTED_BY_WIND
              set_flag(crconf->immunity_flags, CSAfF_Wind);
              n++;
              break;
            case 3: // IMMUNE_TO_GAS
              set_flag(crconf->immunity_flags, CSAfF_PoisonCloud);
              n++;
              break;
            case 4: // HUMANOID_SKELETON
              crconf->humanoid_creature = true;
              n++;
              break;
            case 5: // PISS_ON_DEAD
              crconf->piss_on_dead = true;
              n++;
              break;
            case 7: // FLYING
              crconf->flying = true;
              n++;
              break;
            case 8: // SEE_INVISIBLE
              crconf->can_see_invisible = true;
              n++;
              break;
            case 9: // PASS_LOCKED_DOORS
              crconf->can_go_locked_doors = true;
              n++;
              break;
            case 10: // SPECIAL_DIGGER
              crconf->model_flags |= CMF_IsSpecDigger;
              n++;
              break;
            case 11: // ARACHNID
              crconf->model_flags |= CMF_IsArachnid;
              n++;
              break;
            case 12: // DIPTERA
              crconf->model_flags |= CMF_IsDiptera;
              n++;
              break;
            case 13: // LORD
              crconf->model_flags |= CMF_IsLordOfLand;
              n++;
              break;
            case 14: // SPECTATOR
              crconf->model_flags |= CMF_IsSpectator;
              n++;
              break;
            case 15: // EVIL
              crconf->model_flags |= CMF_IsEvil;
              n++;
              break;
            case 16: // NEVER_CHICKENS
              set_flag(crconf->immunity_flags, CSAfF_Chicken);
              n++;
              break;
            case 17: // IMMUNE_TO_BOULDER
                crconf->model_flags |= CMF_ImmuneToBoulder;
                n++;
                break;
            case 18: // NO_CORPSE_ROTTING
                crconf->model_flags |= CMF_NoCorpseRotting;
                n++;
                break;
            case 19: // NO_ENMHEART_ATTCK
                crconf->model_flags |= CMF_NoEnmHeartAttack;
                n++;
                break;
            case 20: // TREMBLING_FAT
                crconf->model_flags |= CMF_Trembling;
                crconf->model_flags |= CMF_Fat;
                n++;
                break;
            case 21: // FEMALE
                crconf->model_flags |= CMF_Female;
                n++;
                break;
            case 22: // INSECT
                crconf->model_flags |= CMF_Insect;
                n++;
                break;
            case 23: // ONE_OF_KIND
                crconf->model_flags |= CMF_OneOfKind;
                n++;
                break;
            case 24: // NO_IMPRISONMENT
                crconf->model_flags |= CMF_NoImprisonment;
                n++;
                break;
            case 25: // IMMUNE_TO_DISEASE
                set_flag(crconf->immunity_flags, CSAfF_Disease);
                n++;
                break;
            case 26: // ILLUMINATED
                crconf->illuminated = true;
                n++;
                break;
            case 27: // ALLURING_SCVNGR
                crconf->entrance_force = true;
                n++;
                break;
            case 28: // NO_RESURRECT
                crconf->model_flags |= CMF_NoResurrect;
                n++;
                break;
            case 29: // NO_TRANSFER
                crconf->model_flags |= CMF_NoTransfer;
                n++;
                break;
            case 30: // TREMBLING
                crconf->model_flags |= CMF_Trembling;
                n++;
                break;
            case 31: // FAT
                crconf->model_flags |= CMF_Fat;
                n++;
                break;
            case 32: // NO_STEAL_HERO
                crconf->model_flags |= CMF_NoStealHero;
                n++;
                break;
            case 33: // PREFER_STEAL
                crconf->model_flags |= CMF_PreferSteal;
                n++;
                break;
            case 34: // EVENTFUL_DEATH
                crconf->model_flags |= CMF_EventfulDeath;
                n++;
                break;
            case 35: // DIGGING_CREATURE
                crconf->model_flags |= CMF_IsDiggingCreature;
                n++;
                break;
            default:
              CONFWRNLOG("Incorrect value of \"%s\" parameter \"%s\" in [%s] block of %s %s file.",
                  COMMAND_TEXT(cmd_num),word_buf, block_name, creature_code_name(crtr_model), config_textname);
              break;
            }
          }
          break;
      case 29: // NAMETEXTID
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            if (k > 0)
            {
              crconf->namestr_idx = k;
              n++;
            }
          }
          if (n < 1)
          {
            CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                COMMAND_TEXT(cmd_num), block_name, config_textname);
          }
          break;
      case 30: // FEARSOMEFACTOR
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->fearsome_factor = k;
            n++;
          }
          if (n < 1)
          {
            CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                COMMAND_TEXT(cmd_num), block_name, config_textname);
          }
          break;
      case 31: // TOKINGRECOVERY
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->toking_recovery = k;
            n++;
          }
          if (n < 1)
          {
            CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                COMMAND_TEXT(cmd_num), block_name, config_textname);
          }
          break;
      case 34: // LAIROBJECT
          if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
          {
              k = get_id(object_desc, word_buf);
              if (k > 0)
              {
                  crconf->lair_object = k;
                  n++;
              }
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
          }
          break;
      case 35: // PRISONKIND
          if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
          {
              k = get_id(creature_desc, word_buf);
              if (k > 0)
              {
                  crconf->prison_kind = k;
                  n++;
              }
              else if (strcasecmp(word_buf,"NULL") == 0)
              {
                  n++;
              }
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
          }
          break;
      case 36: // TORTUREKIND
          if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
          {
              k = get_id(creature_desc, word_buf);
              if (k > 0)
              {
                  crconf->torture_kind = k;
                  n++;
              }
              else if (strcasecmp(word_buf,"NULL") == 0)
              {
                  n++;
              }
          }
          if (n < 1)
          {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
          }
          break;
        case 37: // SPELLIMMUNITY
        {
            // Backward compatibility check.
            TbBool unaffected_by_wind = flag_is_set(crconf->immunity_flags, CSAfF_Wind);
            TbBool immune_to_gas = flag_is_set(crconf->immunity_flags, CSAfF_PoisonCloud);
            TbBool never_chickens = flag_is_set(crconf->immunity_flags, CSAfF_Chicken);
            TbBool immune_to_disease = flag_is_set(crconf->immunity_flags, CSAfF_Disease);
            crconf->immunity_flags = 0; // Clear flags, this is necessary for partial config if modder wants to remove all flags.
            // Backward compatibility fix.
            if (unaffected_by_wind) { set_flag(crconf->immunity_flags, CSAfF_Wind); }
            if (immune_to_gas) { set_flag(crconf->immunity_flags, CSAfF_PoisonCloud); }
            if (never_chickens) { set_flag(crconf->immunity_flags, CSAfF_Chicken); }
            if (immune_to_disease) { set_flag(crconf->immunity_flags, CSAfF_Disease); }
            while (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                if (parameter_is_number(word_buf))
                {
                    k = atoi(word_buf);
                    crconf->immunity_flags = k;
                    n++;
                }
                else
                {
                    k = get_id(spell_effect_flags, word_buf);
                    if (k > 0)
                    {
                        set_flag(crconf->immunity_flags, k);
                        n++;
                    }
                }
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s %s file.",
                    COMMAND_TEXT(cmd_num), block_name, creature_code_name(crtr_model), config_textname);
            }
            break;
        }
        case 38: // HOSTILETOWARDS
            for (int i = 0; i < CREATURE_TYPES_MAX; i++)
            {
                if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
                {
                    k = get_id(creature_desc, word_buf);
                    if (k >= 0)
                    {
                        crconf->hostile_towards[i] = k;
                        n++;
                    }
                    else if (0 == strcmp(word_buf, "ANY_CREATURE"))
                    {
                        crconf->hostile_towards[i] = CREATURE_ANY;
                        n++;
                    }
                    else
                    {
                        crconf->hostile_towards[i] = 0;
                        if (strcasecmp(word_buf, "NULL") == 0)
                        {
                            n++;
                        }
                    }
                }
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
      case ccr_comment:
          break;
      case ccr_endOfFile:
          break;
      default:
          CONFWRNLOG("Unrecognized command (%d) in [%s] block of %s file.",
              cmd_num, block_name, config_textname);
          break;
      }
      skip_conf_to_next_line(buf,&pos,len);
  }
#undef COMMAND_TEXT
  // If the creature is a special breed, then update an attribute in CreatureConfig struct
  if ((crconf->model_flags & (CMF_IsSpecDigger|CMF_IsDiggingCreature)) != 0)
  {
      if ((crconf->model_flags & CMF_IsEvil) != 0) {
          game.conf.crtr_conf.special_digger_evil = crtr_model;
      } else {
          game.conf.crtr_conf.special_digger_good = crtr_model;
      }
  }
  if ((crconf->model_flags & CMF_IsSpectator) != 0)
  {
      game.conf.crtr_conf.spectator_breed = crtr_model;
  }
  // Set creature start states based on the flags
  if ((crconf->model_flags & (CMF_IsSpecDigger|CMF_IsDiggingCreature)) != 0)
  {
      crconf->evil_start_state = CrSt_ImpDoingNothing;
      crconf->good_start_state = CrSt_TunnellerDoingNothing;
  } else
  {
      crconf->evil_start_state = CrSt_CreatureDoingNothing;
      crconf->good_start_state = CrSt_GoodDoingNothing;
  }
  return true;
}

TbBool parse_creaturemodel_attraction_blocks(long crtr_model,char *buf,long len,const char *config_textname,unsigned short flags)
{
  int n;
  struct CreatureModelConfig* crconf = creature_stats_get(crtr_model);
  // Find the block
  const char * block_name = "attraction";
  long pos = 0;
  int k = find_conf_block(buf, &pos, len, block_name);
  if (k < 0)
  {
      if ((flags & CnfLd_AcceptPartial) == 0)
          WARNMSG("Block [%s] not found in %s file.", block_name, config_textname);
      return false;
  }
#define COMMAND_TEXT(cmd_num) get_conf_parameter_text(creatmodel_attraction_commands,cmd_num)
  while (pos<len)
  {
      // Finding command number in this line
      int cmd_num = recognize_conf_command(buf, &pos, len, creatmodel_attraction_commands);
      // Now store the config item in correct place
      if (cmd_num == ccr_endOfBlock) break; // if next block starts
      n = 0;
      char word_buf[COMMAND_WORD_LEN];
      switch (cmd_num)
      {
      case 1: // ENTRANCEROOM
          for (k=0; k < ENTRANCE_ROOMS_COUNT; k++)
            crconf->entrance_rooms[k] = 0;
          while (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = get_id(room_desc, word_buf);
            if ((k >= 0) && (n < ENTRANCE_ROOMS_COUNT))
            {
              crconf->entrance_rooms[n] = k;
              n++;
            } else
            {
              CONFWRNLOG("Too many params, or incorrect value of \"%s\" parameter \"%s\", in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), word_buf, block_name, config_textname);
            }
          }
          break;
      case 2: // ROOMSLABSREQUIRED
          for (k=0; k < ENTRANCE_ROOMS_COUNT; k++)
            crconf->entrance_slabs_req[k] = 0;
          while (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            if (n < ENTRANCE_ROOMS_COUNT)
            {
              crconf->entrance_slabs_req[n] = k;
              n++;
            } else
            {
              CONFWRNLOG("Too many parameters of \"%s\" in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
          }
          break;
      case 3: // BASEENTRANCESCORE
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->entrance_score = k;
            n++;
          }
          if (n < 1)
          {
            CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                COMMAND_TEXT(cmd_num), block_name, config_textname);
          }
          break;
      case 4: // SCAVENGEREQUIREMENT
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->scavenge_require = k;
            n++;
          }
          if (n < 1)
          {
            CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                COMMAND_TEXT(cmd_num), block_name, config_textname);
          }
          break;
      case 5: // TORTURETIME
          if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
          {
            k = atoi(word_buf);
            crconf->torture_break_time = k;
            n++;
          }
          if (n < 1)
          {
            CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                COMMAND_TEXT(cmd_num), block_name, config_textname);
          }
          break;
      case ccr_comment:
          break;
      case ccr_endOfFile:
          break;
      default:
          CONFWRNLOG("Unrecognized command (%d) in [%s] block of %s file.",
              cmd_num, block_name, config_textname);
          break;
      }
      skip_conf_to_next_line(buf,&pos,len);
  }
#undef COMMAND_TEXT
  return true;
}

TbBool parse_creaturemodel_annoyance_blocks(long crtr_model,char *buf,long len,const char *config_textname,unsigned short flags)
{
    struct CreatureModelConfig* crconf = creature_stats_get(crtr_model);
    // Find the block
    const char * block_name = "annoyance";
    long pos = 0;
    int k = find_conf_block(buf, &pos, len, block_name);
    if (k < 0)
    {
        if ((flags & CnfLd_AcceptPartial) == 0)
            WARNMSG("Block [%s] not found in %s file.", block_name, config_textname);
        return false;
    }
#define COMMAND_TEXT(cmd_num) get_conf_parameter_text(creatmodel_annoyance_commands,cmd_num)
    while (pos<len)
    {
        // Finding command number in this line
        int cmd_num = recognize_conf_command(buf, &pos, len, creatmodel_annoyance_commands);
        // Now store the config item in correct place
        if (cmd_num == ccr_endOfBlock) break; // if next block starts
        int n = 0;
        char word_buf[COMMAND_WORD_LEN];
        switch (cmd_num)
        {
        case 1: // EATFOOD
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_eat_food = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case 2: // WILLNOTDOJOB
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_will_not_do_job = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case 3: // INHAND
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_in_hand = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case 4: // NOLAIR
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_no_lair = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case 5: // NOHATCHERY
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_no_hatchery = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case 6: // WOKENUP
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_woken_up = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case 7: // STANDINGONDEADENEMY
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_on_dead_enemy = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case 8: // SULKING
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_sulking = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case 9: // NOSALARY
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_no_salary = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case 10: // SLAPPED
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_slapped = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case 11: // STANDINGONDEADFRIEND
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_on_dead_friend = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case 12: // INTORTURE
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_in_torture = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case 13: // INTEMPLE
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_in_temple = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case 14: // SLEEPING
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_sleeping = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case 15: // GOTWAGE
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_got_wage = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case 16: // WINBATTLE
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_win_battle = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case 17: // UNTRAINED
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_untrained_time = k;
              n++;
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_untrained = k;
              n++;
            }
            if (n < 2)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case 18: // OTHERSLEAVING
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_others_leaving = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case 19: // JOBSTRESS
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_job_stress = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case 20: // QUEUE
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_queue = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case 21: // LAIRENEMY
            for (int i = 0; i < LAIR_ENEMY_MAX; i++)
            {
                if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
                {
                    k = get_id(creature_desc, word_buf);
                    if (k >= 0)
                    {
                        crconf->lair_enemy[i] = k;
                        n++;
                    }
                    else if (0 == strcmp(word_buf, "ANY_CREATURE"))
                    {
                        crconf->lair_enemy[i] = CREATURE_ANY;
                        n++;
                    }
                    else
                    {
                        crconf->lair_enemy[i] = 0;
                        if (strcasecmp(word_buf, "NULL") == 0)
                            n++;
                    }
                }
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case 22: // ANNOYLEVEL
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_level = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case 23: // ANGERJOBS
            crconf->jobs_anger = 0;
            while (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = get_id(angerjob_desc, word_buf);
              if (k > 0)
              {
                crconf->jobs_anger |= k;
                n++;
              } else
              {
                  CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                      COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
              }
            }
            break;
        case 24: // GOINGPOSTAL
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->annoy_going_postal = k;
              n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            }
            break;
        case ccr_comment:
            break;
        case ccr_endOfFile:
            break;
        default:
            CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file of creature %s.",
                COMMAND_TEXT(cmd_num), block_name, config_textname, creature_code_name(crtr_model));
            break;
        }
        skip_conf_to_next_line(buf,&pos,len);
    }
#undef COMMAND_TEXT
    return true;
}

TbBool parse_creaturemodel_senses_blocks(long crtr_model,char *buf,long len,const char *config_textname,unsigned short flags)
{
    struct CreatureModelConfig* crconf = creature_stats_get(crtr_model);
    // Find the block
    const char * block_name = "senses";
    long pos = 0;
    int k = find_conf_block(buf, &pos, len, block_name);
    if (k < 0)
    {
        if ((flags & CnfLd_AcceptPartial) == 0)
            WARNMSG("Block [%s] not found in %s file.", block_name, config_textname);
        return false;
    }
#define COMMAND_TEXT(cmd_num) get_conf_parameter_text(creatmodel_senses_commands,cmd_num)
    while (pos<len)
    {
        // Finding command number in this line
        int cmd_num = recognize_conf_command(buf, &pos, len, creatmodel_senses_commands);
        // Now store the config item in correct place
        if (cmd_num == ccr_endOfBlock) break; // if next block starts
        int n = 0;
        char word_buf[COMMAND_WORD_LEN];
        switch (cmd_num)
        {
        case 1: // HEARING
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->hearing = k;
              n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 2: // EYEHEIGHT
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->base_eye_height = k;
              n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 3: // FIELDOFVIEW
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->field_of_view = k;
              n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 4: // EYEEFFECT
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = get_id(lenses_desc, word_buf);
              if (k >= 0)
              {
                  crconf->eye_effect = k;
                  n++;
              }
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 5: // MAXANGLECHANGE
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              if (k > 0)
              {
                  crconf->max_turning_speed = (k * LbFPMath_PI) / 180;
                  n++;
              }
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case ccr_comment:
            break;
        case ccr_endOfFile:
            break;
        default:
            CONFWRNLOG("Unrecognized command (%d) in [%s] block of %s file.",
                cmd_num, block_name, config_textname);
            break;
        }
        skip_conf_to_next_line(buf,&pos,len);
    }
#undef COMMAND_TEXT
    return true;
}

TbBool parse_creaturemodel_appearance_blocks(long crtr_model,char *buf,long len,const char *config_textname,unsigned short flags)
{
    // Block name and parameter word store variables
    struct CreatureModelConfig* crconf = creature_stats_get(crtr_model);
    // Find the block
    const char * block_name = "appearance";
    long pos = 0;
    int k = find_conf_block(buf, &pos, len, block_name);
    if (k < 0)
    {
        if ((flags & CnfLd_AcceptPartial) == 0)
            WARNMSG("Block [%s] not found in %s file.", block_name, config_textname);
        return false;
    }
#define COMMAND_TEXT(cmd_num) get_conf_parameter_text(creatmodel_appearance_commands,cmd_num)
    while (pos<len)
    {
        // Finding command number in this line
        int cmd_num = recognize_conf_command(buf, &pos, len, creatmodel_appearance_commands);
        // Now store the config item in correct place
        if (cmd_num == ccr_endOfBlock) break; // if next block starts
        int n = 0;
        char word_buf[COMMAND_WORD_LEN];
        switch (cmd_num)
        {
        case 1: // WALKINGANIMSPEED
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->walking_anim_speed = k;
              n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 2: // VISUALRANGE
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->visual_range = k;
              n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 3: // SWIPEINDEX
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                if (k >= 0)
                {
                    crconf->swipe_idx = k;
                    n++;
                }
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 4: // NATURALDEATHKIND
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = get_id(creature_deathkind_desc, word_buf);
                if (k > 0)
                {
                    crconf->natural_death_kind = k;
                }
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 5: // SHOTORIGIN
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                crconf->shot_shift_x = k;
                n++;
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                crconf->shot_shift_y = k;
                n++;
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                crconf->shot_shift_z = k;
                n++;
            }
            if (n < 3)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 6: // CORPSEVANISHEFFECT
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                crconf->corpse_vanish_effect = k;
                n++;
            }
            break;
        case 7: // FOOTSTEPPITCH
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                crconf->footstep_pitch = k;
                n++;
            }
            break;
        case 8: // PICKUPOFFSET
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                crconf->creature_picked_up_offset.delta_x = k;
                n++;
            }
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                crconf->creature_picked_up_offset.delta_y = k;
                n++;
            }
            if (n < 2)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 9: // STATUSOFFSET
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                crconf->status_offset = k;
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 10: // TRANSPARENCYFLAGS
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                if (k > 0)
                {
                    crconf->transparency_flags = k<<4; // Bitshift to get the transparancy bit in the render flag
                }
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 11: // FIXEDANIMSPEED
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                if (k >= 0)
                {
                    crconf->fixed_anim_speed = k;
                }
                n++;
            }
            if (n < 1)
            {
                CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case ccr_comment:
            break;
        case ccr_endOfFile:
            break;
        default:
            CONFWRNLOG("Unrecognized command (%d) in [%s] block of %s file.",
                cmd_num, block_name, config_textname);
            break;
        }
        skip_conf_to_next_line(buf,&pos,len);
    }
#undef COMMAND_TEXT
    return true;
}

TbBool parse_creaturemodel_experience_blocks(long crtr_model,char *buf,long len,const char *config_textname,unsigned short flags)
{
    int n;
    // Block name and parameter word store variables
    struct CreatureModelConfig* crconf = creature_stats_get(crtr_model);
    // Find the block
    const char * block_name = "experience";
    long pos = 0;
    int k = find_conf_block(buf, &pos, len, block_name);
    if (k < 0)
    {
        if ((flags & CnfLd_AcceptPartial) == 0)
            WARNMSG("Block [%s] not found in %s file.", block_name, config_textname);
        return false;
    }
#define COMMAND_TEXT(cmd_num) get_conf_parameter_text(creatmodel_experience_commands,cmd_num)
    while (pos<len)
    {
        // Finding command number in this line
        int cmd_num = recognize_conf_command(buf, &pos, len, creatmodel_experience_commands);
        // Now store the config item in correct place
        if (cmd_num == ccr_endOfBlock) break; // if next block starts
        n = 0;
        char word_buf[COMMAND_WORD_LEN];
        switch (cmd_num)
        {
        case 1: // POWERS
            while (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = get_id(instance_desc, word_buf);
              if ((k >= 0) && (n < LEARNED_INSTANCES_COUNT))
              {
                crconf->learned_instance_id[n] = k;
                n++;
              } else
              {
                CONFWRNLOG("Too many params, or incorrect value of \"%s\" parameter \"%s\", in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num),word_buf, block_name, config_textname);
              }
            }
            break;
        case 2: // POWERSLEVELREQUIRED
            while (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              if ((k >= 0) && (n < LEARNED_INSTANCES_COUNT))
              {
                crconf->learned_instance_level[n] = k;
                n++;
              } else
              {
                CONFWRNLOG("Too many params, or incorrect value of \"%s\" parameter \"%s\", in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num),word_buf, block_name, config_textname);
              }
            }
            break;
        case 3: // LEVELSTRAINVALUES
            while (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              if ((k >= 0) && (n < CREATURE_MAX_LEVEL-1))
              {
                crconf->to_level[n] = k;
                n++;
              } else
              {
                CONFWRNLOG("Too many params, or incorrect value of \"%s\" parameter \"%s\", in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num),word_buf, block_name, config_textname);
              }
            }
            break;
        case 4: // GROWUP
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->to_level[CREATURE_MAX_LEVEL-1] = k;
              n++;
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = parse_creature_name(word_buf);
              if (k >= 0)
              {
                crconf->grow_up = k;
                n++;
              } else
              {
                crconf->grow_up = 0;
                if (strcasecmp(word_buf,"NULL") == 0)
                  n++;
              }
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->grow_up_level = k;
              n++;
            }
            if (n < 3)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 5: // SLEEPEXPERIENCE
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = get_id(slab_desc, word_buf);
              if (k >= 0)
              {
                crconf->sleep_exp_slab = k;
                n++;
              } else
              {
                crconf->sleep_exp_slab = 0;
              }
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->sleep_experience = k;
              n++;
            }
            if (n < 2)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameters in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 6: // EXPERIENCEFORHITTING
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->exp_for_hitting = k;
              n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 7: // REBIRTH
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->rebirth = k;
              n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case ccr_comment:
            break;
        case ccr_endOfFile:
            break;
        default:
            CONFWRNLOG("Unrecognized command (%d) in [%s] block of %s file.",
                cmd_num, block_name, config_textname);
            break;
        }
        skip_conf_to_next_line(buf,&pos,len);
    }
#undef COMMAND_TEXT
    return true;
}

TbBool parse_creaturemodel_jobs_blocks(long crtr_model,char *buf,long len,const char *config_textname,unsigned short flags)
{
    // Block name and parameter word store variables
    struct CreatureModelConfig* crconf = creature_stats_get(crtr_model);
    // Find the block
    const char * block_name = "jobs";
    long pos = 0;
    int k = find_conf_block(buf, &pos, len, block_name);
    if (k < 0)
    {
        if ((flags & CnfLd_AcceptPartial) == 0)
            WARNMSG("Block [%s] not found in %s file.", block_name, config_textname);
        return false;
    }
#define COMMAND_TEXT(cmd_num) get_conf_parameter_text(creatmodel_jobs_commands,cmd_num)
    while (pos<len)
    {
        // Finding command number in this line
        int cmd_num = recognize_conf_command(buf, &pos, len, creatmodel_jobs_commands);
        // Now store the config item in correct place
        if (cmd_num == ccr_endOfBlock) break; // if next block starts
        int n = 0;
        char word_buf[COMMAND_WORD_LEN];
        switch (cmd_num)
        {
        case 1: // PRIMARYJOBS
            crconf->job_primary = 0;
            while (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = get_id(creaturejob_desc, word_buf);
              if (k > 0)
              {
                crconf->job_primary |= k;
                n++;
              } else
              {
                CONFWRNLOG("Incorrect value of \"%s\" parameter \"%s\", in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num),word_buf, block_name, config_textname);
              }
            }
            break;
        case 2: // SECONDARYJOBS
            crconf->job_secondary = 0;
            while (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = get_id(creaturejob_desc, word_buf);
              if (k > 0)
              {
                crconf->job_secondary |= k;
                n++;
              } else
              {
                CONFWRNLOG("Incorrect value of \"%s\" parameter \"%s\", in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num),word_buf, block_name, config_textname);
              }
            }
            break;
        case 3: // NOTDOJOBS
            crconf->jobs_not_do = 0;
            while (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = get_id(creaturejob_desc, word_buf);
              if (k > 0)
              {
                crconf->jobs_not_do |= k;
                n++;
              } else
              {
                CONFWRNLOG("Incorrect value of \"%s\" parameter \"%s\", in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num),word_buf, block_name, config_textname);
              }
            }
            break;
        case 4: // STRESSFULJOBS
            crconf->job_stress = 0;
            while (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = get_id(creaturejob_desc, word_buf);
              if (k > 0)
              {
                crconf->job_stress |= k;
                n++;
              } else
              {
                CONFWRNLOG("Incorrect value of \"%s\" parameter \"%s\", in [%s] block of %s file.",
                    COMMAND_TEXT(cmd_num),word_buf, block_name, config_textname);
              }
            }
            break;
        case 5: // TRAININGVALUE
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->training_value = k;
              n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 6: // TRAININGCOST
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->training_cost = k;
              n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 7: // SCAVENGEVALUE
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->scavenge_value = k;
              n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 8: // SCAVENGERCOST
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->scavenger_cost = k;
              n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 9: // RESEARCHVALUE
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->research_value = k;
              n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 10: // MANUFACTUREVALUE
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->manufacture_value = k;
              n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case 11: // PARTNERTRAINING
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              crconf->partner_training = k;
              n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case ccr_comment:
            break;
        case ccr_endOfFile:
            break;
        default:
            CONFWRNLOG("Unrecognized command (%d) in [%s] block of %s file.",
                cmd_num, block_name, config_textname);
            break;
        }
        skip_conf_to_next_line(buf,&pos,len);
    }
#undef COMMAND_TEXT
    return true;
}

TbBool parse_creaturemodel_sprites_blocks(long crtr_model,char *buf,long len,const char *config_textname,unsigned short flags)
{
  int n;
  // Find the block
  const char * block_name = "sprites";
  long pos = 0;
  int k = find_conf_block(buf, &pos, len, block_name);
  if (k < 0)
  {
      if ((flags & CnfLd_AcceptPartial) == 0)
          WARNMSG("Block [%s] not found in %s file.", block_name, config_textname);
      return false;
  }
#define COMMAND_TEXT(cmd_num) get_conf_parameter_text(creature_graphics_desc,cmd_num)
  while (pos<len)
  {
      // Finding command number in this line
      int cmd_num = recognize_conf_command(buf, &pos, len, creature_graphics_desc);
      // Now store the config item in correct place
      if (cmd_num == ccr_endOfBlock) break; // if next block starts
      n = 0;
      if ((cmd_num == (CGI_HandSymbol + 1)) || (cmd_num == (CGI_QuerySymbol + 1)))
      {
          char word_buf[COMMAND_WORD_LEN];
          if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
          {
              n = get_icon_id(word_buf);
              if (n >= 0)
              {
                  set_creature_model_graphics(crtr_model, cmd_num-1, n);
              }
              else
              {
                  set_creature_model_graphics(crtr_model, cmd_num-1, bad_icon_id);
                  CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                             COMMAND_TEXT(cmd_num), block_name, config_textname);
              }
          }
      }
      else if ((cmd_num > 0) && (cmd_num <= CREATURE_GRAPHICS_INSTANCES))
      {
          char word_buf[COMMAND_WORD_LEN];
          if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
          {
            k = get_anim_id_(word_buf);
            set_creature_model_graphics(crtr_model, cmd_num-1, k);
            n++;
          }
          if (n < 1)
          {
            CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                COMMAND_TEXT(cmd_num), block_name, config_textname);
          }
      } else
      switch (cmd_num)
      {
      case ccr_comment:
          break;
      case ccr_endOfFile:
          break;
      default:
          CONFWRNLOG("Unrecognized command (%d) in [%s] block of %s file.",
              cmd_num, block_name, config_textname);
          break;
      }
      skip_conf_to_next_line(buf,&pos,len);
  }
#undef COMMAND_TEXT
  return true;
}

TbBool parse_creaturemodel_sounds_blocks(long crtr_model,char *buf,long len,const char *config_textname,unsigned short flags)
{
    // Find the block
    const char * block_name = "sounds";
    long pos = 0;
    int k = find_conf_block(buf, &pos, len, block_name);
    if (k < 0)
    {
        if ((flags & CnfLd_AcceptPartial) == 0)
            WARNMSG("Block [%s] not found in %s file.", block_name, config_textname);
        return false;
    }
#define COMMAND_TEXT(cmd_num) get_conf_parameter_text(creatmodel_sounds_commands,cmd_num)
    while (pos<len)
    {
        // Finding command number in this line
        int cmd_num = recognize_conf_command(buf, &pos, len, creatmodel_sounds_commands);
        // Now store the config item in correct place
        if (cmd_num == ccr_endOfBlock) break; // if next block starts
        int n = 0;
        char word_buf[COMMAND_WORD_LEN];
        switch (cmd_num)
        {
        case CrSnd_Hurt:
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                game.conf.crtr_conf.creature_sounds[crtr_model].hurt.index = k;
                n++;
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                game.conf.crtr_conf.creature_sounds[crtr_model].hurt.count = k;
                n++;
            }            
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case CrSnd_Hit:
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                game.conf.crtr_conf.creature_sounds[crtr_model].hit.index = k;
                n++;
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                game.conf.crtr_conf.creature_sounds[crtr_model].hit.count = k;
                n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case CrSnd_Happy:
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                game.conf.crtr_conf.creature_sounds[crtr_model].happy.index = k;
                n++;
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                game.conf.crtr_conf.creature_sounds[crtr_model].happy.count = k;
                n++;
            }            
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case CrSnd_Sad:
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                game.conf.crtr_conf.creature_sounds[crtr_model].sad.index = k;
                n++;
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                game.conf.crtr_conf.creature_sounds[crtr_model].sad.count = k;
                n++;
            }            
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case CrSnd_Hang:
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              game.conf.crtr_conf.creature_sounds[crtr_model].hang.index = k;
              n++;
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              game.conf.crtr_conf.creature_sounds[crtr_model].hang.count = k;
              n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case CrSnd_Drop:
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              game.conf.crtr_conf.creature_sounds[crtr_model].drop.index = k;
              n++;
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              game.conf.crtr_conf.creature_sounds[crtr_model].drop.count = k;
              n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case CrSnd_Torture:
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              game.conf.crtr_conf.creature_sounds[crtr_model].torture.index = k;
              n++;
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              game.conf.crtr_conf.creature_sounds[crtr_model].torture.count = k;
              n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case CrSnd_Slap:
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              game.conf.crtr_conf.creature_sounds[crtr_model].slap.index = k;
              n++;
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              game.conf.crtr_conf.creature_sounds[crtr_model].slap.count = k;
              n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case CrSnd_Die:
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              game.conf.crtr_conf.creature_sounds[crtr_model].die.index = k;
              n++;
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              game.conf.crtr_conf.creature_sounds[crtr_model].die.count = k;
              n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case CrSnd_Foot:
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              game.conf.crtr_conf.creature_sounds[crtr_model].foot.index = k;
              n++;
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              game.conf.crtr_conf.creature_sounds[crtr_model].foot.count = k;
              n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case CrSnd_Fight:
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              game.conf.crtr_conf.creature_sounds[crtr_model].fight.index = k;
              n++;
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              game.conf.crtr_conf.creature_sounds[crtr_model].fight.count = k;
              n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case CrSnd_Piss:
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              game.conf.crtr_conf.creature_sounds[crtr_model].piss.index = k;
              n++;
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              game.conf.crtr_conf.creature_sounds[crtr_model].piss.count = k;
              n++;
            }
            if (n < 1)
            {
              CONFWRNLOG("Incorrect value of \"%s\" parameter in [%s] block of %s file.",
                  COMMAND_TEXT(cmd_num), block_name, config_textname);
            }
            break;
        case ccr_comment:
            break;
        case ccr_endOfFile:
            break;
        default:
            CONFWRNLOG("Unrecognized command (%d) in [%s] block of %s file.",
                cmd_num, block_name, config_textname);
            break;
        }
        skip_conf_to_next_line(buf,&pos,len);
    }
#undef COMMAND_TEXT
    return true;
}

static TbBool load_creaturemodel_config_file(long crtr_model,const char *textname,const char *fname,unsigned short flags)
{
    SYNCDBG(0,"%s model %ld from file \"%s\".",((flags & CnfLd_ListOnly) == 0)?"Reading":"Parsing",crtr_model,fname);
    long len = LbFileLengthRnc(fname);
    if (len < MIN_CONFIG_FILE_SIZE)
    {
        return false;
    }
    char* buf = (char*)calloc(len + 256, 1);
    if (buf == NULL)
        return false;
    // Loading file data
    len = LbFileLoadAt(fname, buf);
    TbBool result = (len > 0);
    // Parse blocks of the config file
    if (result)
    {
        parse_creaturemodel_attributes_blocks(crtr_model, buf, len, fname, flags);
        parse_creaturemodel_attraction_blocks(crtr_model, buf, len, fname, flags);
        parse_creaturemodel_annoyance_blocks(crtr_model, buf, len, fname, flags);
        parse_creaturemodel_senses_blocks(crtr_model, buf, len, fname, flags);
        parse_creaturemodel_appearance_blocks(crtr_model, buf, len, fname, flags);
        parse_creaturemodel_experience_blocks(crtr_model, buf, len, fname, flags);
        parse_creaturemodel_jobs_blocks(crtr_model, buf, len, fname, flags);
        parse_creaturemodel_sprites_blocks(crtr_model, buf, len, fname, flags);
        parse_creaturemodel_sounds_blocks(crtr_model, buf, len, fname, flags);
    }
    // Freeing and exiting
    free(buf);
    return result;
}

TbBool load_creaturemodel_config(ThingModel crmodel, unsigned short flags)
{
    static const char config_global_textname[] = "global creature model config";
    static const char config_campgn_textname[] = "campaign creature model config";
    static const char config_level_textname[] = "level creature model config";
    char conf_fnstr[COMMAND_WORD_LEN];
    snprintf(conf_fnstr, COMMAND_WORD_LEN, "%s", get_conf_parameter_text(creature_desc,crmodel));
    strtolower(conf_fnstr);
    if (strlen(conf_fnstr) == 0)
    {
        WARNMSG("Cannot get config file name for creature %d.",crmodel);
        return false;
    }
    char* fname = prepare_file_fmtpath(FGrp_CrtrData, "%s.cfg", conf_fnstr);
    TbBool result = load_creaturemodel_config_file(crmodel, config_global_textname, fname, flags);
    if (result)
    {
        set_flag(flags, (CnfLd_AcceptPartial | CnfLd_IgnoreErrors));
    }
    fname = prepare_file_fmtpath(FGrp_CmpgCrtrs,"%s.cfg",conf_fnstr);
    if (strlen(fname) > 0)
    {
        result |= load_creaturemodel_config_file(crmodel,config_campgn_textname,fname,flags);
        if (result)
        {
            set_flag(flags, (CnfLd_AcceptPartial | CnfLd_IgnoreErrors));
        }
    }
    fname = prepare_file_fmtpath(FGrp_CmpgLvls, "map%05lu.%s.cfg", get_selected_level_number(), conf_fnstr);
    if (strlen(fname) > 0)
    {
        result |= load_creaturemodel_config_file(crmodel,config_level_textname,fname,flags);
    }
    if (!result)
    {
        ERRORLOG("Unable to load a complete '%s' creature model config file.", creature_code_name(crmodel));
    }
    return result;
}

TbBool swap_creaturemodel_config(ThingModel nwcrmodel, ThingModel crmodel, unsigned short flags)
{
    static const char config_global_textname[] = "global creature model config";
    static const char config_campgn_textname[] = "campaing creature model config";
    static const char config_level_textname[] = "level creature model config";
    char conf_fnstr[COMMAND_WORD_LEN];
    snprintf(conf_fnstr, COMMAND_WORD_LEN, "%s", get_conf_parameter_text(creature_desc, nwcrmodel));
    strtolower(conf_fnstr);
    if (strlen(conf_fnstr) == 0)
    {
        WARNMSG("Cannot get config file name for creature %d.", crmodel);
        return false;
    }
    char* fname = prepare_file_fmtpath(FGrp_CrtrData, "%s.cfg", conf_fnstr);
    TbBool result = load_creaturemodel_config_file(crmodel, config_global_textname, fname, flags);
    fname = prepare_file_fmtpath(FGrp_CmpgCrtrs, "%s.cfg", conf_fnstr);
    if (strlen(fname) > 0)
    {
        load_creaturemodel_config_file(crmodel, config_campgn_textname, fname, flags | CnfLd_AcceptPartial | CnfLd_IgnoreErrors);
    }
    fname = prepare_file_fmtpath(FGrp_CmpgLvls, "map%05lu.%s.cfg", get_selected_level_number(), conf_fnstr);
    if (strlen(fname) > 0)
    {
        load_creaturemodel_config_file(crmodel, config_level_textname,fname,flags|CnfLd_AcceptPartial|CnfLd_IgnoreErrors);
    }
    //Freeing and exiting
    return result;
}

static void do_creature_swap(ThingModel ncrt_id, ThingModel crtr_id)
{
    swap_creaturemodel_config(ncrt_id, crtr_id, 0);
    SCRPTLOG("Swapped creature %s out for creature %s", creature_code_name(crtr_id), creature_code_name(ncrt_id));
}

TbBool swap_creature(ThingModel ncrt_id, ThingModel crtr_id)
{
    if ((crtr_id < 0) || (crtr_id >= game.conf.crtr_conf.model_count))
    {
        ERRORLOG("Creature index %d is invalid", crtr_id);
        return false;
    }
    if ((ncrt_id < 0) || (ncrt_id >= game.conf.crtr_conf.model_count))
    {
        ERRORLOG("Creature index %d is invalid", ncrt_id);
        return false;
    }
    struct CreatureModelConfig* crconf = creature_stats_get(crtr_id);
    ThingModel oldlair = crconf->lair_object;
    do_creature_swap(ncrt_id, crtr_id);
    struct CreatureModelConfig* ncrconf = creature_stats_get(crtr_id);
    ThingModel newlair = ncrconf->lair_object;
    for (PlayerNumber plyr_idx = 0; plyr_idx < PLAYERS_COUNT; plyr_idx++)
    {
        do_to_players_all_creatures_of_model(plyr_idx, crtr_id, update_relative_creature_health);
        update_speed_of_player_creatures_of_model(plyr_idx, crtr_id);
        if (oldlair != newlair)
        {
            do_to_players_all_creatures_of_model(plyr_idx, crtr_id, remove_creature_lair);
        }
        do_to_players_all_creatures_of_model(plyr_idx, crtr_id, process_job_stress_and_going_postal);
    }
    
    recalculate_all_creature_digger_lists();
    update_creatr_model_activities_list(1);

    return true;
}

/**
 * Zeroes all the maintenance costs for all creatures.
 */
TbBool make_all_creatures_free(void)
{
    for (long i = 0; i < game.conf.crtr_conf.model_count; i++)
    {
        struct CreatureModelConfig* crconf = creature_stats_get(i);
        crconf->training_cost = 0;
        crconf->scavenger_cost = 0;
        crconf->pay = 0;
    }
    return true;
}

/**
 * Changes max health of creatures, and updates all creatures to max.
 */
TbBool change_max_health_of_creature_kind(ThingModel crmodel, HitPoints new_max)
{
    struct CreatureModelConfig* crconf = creature_stats_get(crmodel);
    if (creature_stats_invalid(crconf)) {
        ERRORLOG("Invalid creature model %d",(int)crmodel);
        return false;
    }
    SYNCDBG(3,"Changing all %s health from %d to %d.",creature_code_name(crmodel),(int)crconf->health,(int)new_max);
    crconf->health = saturate_set_signed(new_max, 16);
    int n = do_to_all_things_of_class_and_model(TCls_Creature, crmodel, update_creature_health_to_max);
    return (n > 0);
}

TbBool heal_completely_all_players_creatures(PlayerNumber plyr_idx, ThingModel crmodel)
{
    SYNCDBG(3,"Healing all player %d creatures of model %s",(int)plyr_idx,creature_code_name(crmodel));
    int n = do_to_players_all_creatures_of_model(plyr_idx, crmodel, update_creature_health_to_max);
    return (n > 0);
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
