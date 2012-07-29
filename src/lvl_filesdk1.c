/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lvl_filesdk1.c
 *     Level files reading routines fore standard DK1 levels.
 * @par Purpose:
 *     Allows reading level files in DK1 format.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     10 Mar 2009 - 20 Mar 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "lvl_filesdk1.h"

#include "globals.h"
#include "bflib_basics.h"

#include "bflib_dernc.h"
#include "bflib_memory.h"
#include "bflib_bufrw.h"
#include "bflib_fileio.h"

#include "front_simple.h"
#include "config.h"
#include "config_campaigns.h"
#include "config_terrain.h"
#include "light_data.h"
#include "keeperfx.hpp"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
const char slabclm_fname[] = "slabs.clm";
const char slabdat_fname[] = "slabs.dat";
long level_file_version = 0;
/******************************************************************************/
DLLIMPORT long _DK_convert_old_column_file(unsigned long lv_num);
DLLIMPORT unsigned char _DK_load_map_slab_file(unsigned long lv_num);
DLLIMPORT long _DK_load_column_file(unsigned long lv_num);
DLLIMPORT void _DK_load_slab_file(void);
DLLIMPORT long _DK_load_map_data_file(unsigned long lv_num);
DLLIMPORT void _DK_load_thing_file(unsigned long lv_num);
DLLIMPORT long _DK_load_action_point_file(unsigned long lv_num);
DLLIMPORT void _DK_load_level_file(long lvnum);
DLLIMPORT void _DK_initialise_extra_slab_info(unsigned long lv_num);
/******************************************************************************/

/**
 * Loads map file with given level number and file extension.
 * @return Returns NULL if the file doesn't exist or is smaller than ldsize;
 * on success, returns a buffer which should be freed after use,
 * and sets ldsize into its size.
 */
unsigned char *load_single_map_file_to_buffer(unsigned long lvnum,const char *fext,long *ldsize,unsigned short flags)
{
  unsigned char *buf;
  char *fname;
  long fsize;
  short fgroup;
  fgroup = get_level_fgroup(lvnum);
  fname = prepare_file_fmtpath(fgroup,"map%05lu.%s",lvnum,fext);
  wait_for_cd_to_be_available();
  fsize = LbFileLengthRnc(fname);
  if (fsize < *ldsize)
  {
    if ((flags & LMFF_Optional) == 0)
      WARNMSG("Map file \"map%05lu.%s\" doesn't exist or is too small.",lvnum,fext);
    else
      SYNCMSG("Optional file \"map%05lu.%s\" doesn't exist or is too small.",lvnum,fext);
    return NULL;
  }
  if (fsize > ANY_MAP_FILE_MAX_SIZE)
  {
    if ((flags & LMFF_Optional) == 0)
      WARNMSG("Map file \"map%05lu.%s\" exceeds max size of %d; loading failed.",lvnum,fext,ANY_MAP_FILE_MAX_SIZE);
    else
      SYNCMSG("Optional file \"map%05lu.%s\" exceeds max size of %d; not loading.",lvnum,fext,ANY_MAP_FILE_MAX_SIZE);
    return NULL;
  }
  buf = LbMemoryAlloc(fsize+16);
  if (buf == NULL)
  {
    if ((flags & LMFF_Optional) == 0)
      WARNMSG("Can't allocate %ld bytes to load \"map%05lu.%s\".",fsize,lvnum,fext);
    else
      SYNCMSG("Can't allocate %ld bytes to load \"map%05lu.%s\".",fsize,lvnum,fext);
    return NULL;
  }
  fsize = LbFileLoadAt(fname,buf);
  if (fsize < *ldsize)
  {
    if ((flags & LMFF_Optional) == 0)
      WARNMSG("Reading map file \"map%05lu.%s\" failed.",lvnum,fext);
    else
      SYNCMSG("Reading optional file \"map%05lu.%s\" failed.",lvnum,fext);
    LbMemoryFree(buf);
    return NULL;
  }
  *ldsize = fsize;
  SYNCDBG(7,"Map file \"map%05lu.%s\" loaded.",lvnum,fext);
  return buf;
}

long get_level_number_from_file_name(char *fname)
{
  long lvnum;
  if (strnicmp(fname,"map",3) != 0)
    return SINGLEPLAYER_NOTSTARTED;
  // Get level number
  lvnum = strtol(&fname[3],NULL,10);
  if (lvnum <= 0)
    return SINGLEPLAYER_NOTSTARTED;
  return lvnum;
}

/**
 * Analyzes one line of .LIF file buffer. The buffer must be null-terminated.
 * @return Length of the parsed line.
 */
long level_lif_entry_parse(char *fname, char *buf)
{
  long lvnum;
  char *cbuf;
  long i;
  if (buf[0] == '\0')
    return 0;
  i = 0;
  // Skip spaces and control chars
  while (buf[i] != '\0')
  {
    if (!isspace(buf[i]) && (buf[i] != ',') && (buf[i] != ';') && (buf[i] != ':'))
      break;
    i++;
  }
  // Get level number
  lvnum = strtol(&buf[i],&cbuf,0);
  // If can't read number, return
  if (cbuf == &buf[i])
  {
    WARNMSG("Can't read level number from \"%s\"", fname);
    return 0;
  }
  // Skip spaces and blank chars
  while (cbuf[0] != '\0')
  {
    if (!isspace(cbuf[0]) && (cbuf[0] != ',') && (cbuf[0] != ';') && (cbuf[0] != ':'))
      break;
    cbuf++;
  }
  // Find length of level name; make it null-terminated
  i = 0;
  while (cbuf[i] != '\0')
  {
    if ((cbuf[i] == '\n') || (cbuf[i] == '\r'))
    {
      cbuf[i] = '\0';
      break;
    }
    i++;
  }
  if (i >= LINEMSG_SIZE)
  {
    WARNMSG("Level name from \"%s\" truncated from %d to %d characters", fname,i,LINEMSG_SIZE);
    i = LINEMSG_SIZE-1;
    cbuf[i] = '\0';
  }
  if (cbuf[0] == '\0')
  {
    WARNMSG("Can't read level name from \"%s\"", fname);
    return 0;
  }
  // check if the level isn't added as other type of level
  if (is_campaign_level(lvnum))
    return (cbuf-buf)+i;
  // Store level name
  if (add_freeplay_level_to_campaign(&campaign,lvnum) < 0)
  {
    WARNMSG("Can't add freeplay level from \"%s\" to campaign", fname);
    return 0;
  }
  if (!set_level_info_text_name(lvnum,cbuf,LvOp_IsFree))
  {
    WARNMSG("Can't set name of level from file \"%s\"", fname);
    return 0;
  }
  return (cbuf-buf)+i;
}

/** Analyzes given LIF file buffer.
 *
 * @param fname Name of the LIF file whixh we've loaded.
 * @param buf The buffer; must be null-terminated.
 * @param buflen Length of the buffer.
 * @return
 */
short level_lif_file_parse(char *fname, char *buf, long buflen)
{
  short result;
  long i;
  long pos;
  if (buf == NULL)
    return false;
  result = false;
  pos = 0;
  do
  {
    i = level_lif_entry_parse(fname, &buf[pos]);
    if (i > 0)
    {
      result = true;
      pos += i+1;
      if (pos+1 >= buflen)
        break;
    }
  } while (i > 0);
  return result;
}

/**
 * Searches levels folder for LIF files and adds them to campaign levels list.
 */
TbBool find_and_load_lif_files(void)
{
  struct TbFileFind fileinfo;
  unsigned char *buf;
  char *fname;
  short result;
  int rc;
  long i;
  buf = LbMemoryAlloc(MAX_LIF_SIZE);
  if (buf == NULL)
  {
    ERRORLOG("Can't allocate memory for .LIF files parsing.");
    return false;
  }
  result = false;
  fname = prepare_file_path(FGrp_VarLevels,"*.lif");
  rc = LbFileFindFirst(fname, &fileinfo, 0x21u);
  while (rc != -1)
  {
    fname = prepare_file_path(FGrp_VarLevels,fileinfo.Filename);
    i = LbFileLength(fname);
    if ((i < 0) || (i >= MAX_LIF_SIZE))
    {
      WARNMSG("File \"%s\" too long (Max size %d)", fileinfo.Filename, MAX_LIF_SIZE);

    } else
    if (LbFileLoadAt(fname, buf) != i)
    {
      WARNMSG("Unable to read .LIF file, \"%s\"", fileinfo.Filename);
    } else
    {
      buf[i] = '\0';
      if (level_lif_file_parse(fileinfo.Filename, (char *)buf, i))
        result = true;
    }
    rc = LbFileFindNext(&fileinfo);
  }
  LbFileFindEnd(&fileinfo);
  LbMemoryFree(buf);
  return result;
}

/**
 * Analyzes given LOF file buffer. The buffer must be null-terminated.
 */
TbBool level_lof_file_parse(char *fname, char *buf, long len)
{
    struct LevelInformation *lvinfo;
    long pos;
    char word_buf[32];
    long lvnum;
    int cmd_num;
    int k,n;
    SYNCDBG(8,"Starting for \"%s\"",fname);
    if (buf == NULL)
      return false;
    lvnum = get_level_number_from_file_name(fname);
    if (lvnum < 1)
    {
      WARNLOG("Incorrect .LOF file name \"%s\", skipped.",fname);
      return false;
    }
    lvinfo = get_or_create_level_info(lvnum, LvOp_None);
    if (lvinfo == NULL)
    {
      WARNMSG("Can't get LevelInformation item to store level %ld data from LOF file.",lvnum);
      return 0;
    }
    lvinfo->location = LvLc_Custom;
    pos = 0;
#define COMMAND_TEXT(cmd_num) get_conf_parameter_text(cmpgn_map_commands,cmd_num)
    while (pos<len)
    {
        // Finding command number in this line
        cmd_num = recognize_conf_command(buf,&pos,len,cmpgn_map_commands);
        // Now store the config item in correct place
        if (cmd_num == -3) break; // if next block starts
        n = 0;
        switch (cmd_num)
        {
        case 1: // NAME_TEXT
            if (get_conf_parameter_whole(buf,&pos,len,lvinfo->name,LINEMSG_SIZE) <= 0)
            {
              WARNMSG("Couldn't read \"%s\" parameter in LOF file '%s'.",
                  COMMAND_TEXT(cmd_num),fname);
              break;
            }
            break;
        case 2: // NAME_ID
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              if (k > 0)
              {
                lvinfo->name_id = k;
                n++;
              }
            }
            if (n < 1)
            {
              WARNMSG("Couldn't recognize \"%s\" number in LOF file '%s'.",
                  COMMAND_TEXT(cmd_num),fname);
            }
            break;
        case 3: // ENSIGN_POS
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                if (k > 0)
                {
                  lvinfo->ensign_x = k;
                  n++;
                }
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                if (k > 0)
                {
                  lvinfo->ensign_y = k;
                  n++;
                }
            }
            if (n < 2)
            {
              WARNMSG("Couldn't recognize \"%s\" coordinates in LOF file '%s'.",
                  COMMAND_TEXT(cmd_num),fname);
            }
            break;
        case 4: // ENSIGN_ZOOM
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                if (k > 0)
                {
                  lvinfo->ensign_zoom_x = k;
                  n++;
                }
            }
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
                k = atoi(word_buf);
                if (k > 0)
                {
                  lvinfo->ensign_zoom_y = k;
                  n++;
                }
            }
            if (n < 2)
            {
              WARNMSG("Couldn't recognize \"%s\" coordinates in LOF file '%s'.",
                  COMMAND_TEXT(cmd_num),fname);
            }
            break;
        case 5: // PLAYERS
            if (get_conf_parameter_single(buf,&pos,len,word_buf,sizeof(word_buf)) > 0)
            {
              k = atoi(word_buf);
              if (k > 0)
              {
                lvinfo->players = k;
                n++;
              }
            }
            if (n < 1)
            {
              WARNMSG("Couldn't recognize \"%s\" number in LOF file '%s'.",
                  COMMAND_TEXT(cmd_num),fname);
            }
            break;
        case 6: // OPTIONS
            while ((k = recognize_conf_parameter(buf,&pos,len,cmpgn_map_cmnds_options)) > 0)
            {
              switch (k)
              {
              case LvOp_Tutorial:
                lvinfo->options |= k;
                break;
              }
              n++;
            }
            break;
        case 7: // SPEECH
            if (get_conf_parameter_single(buf,&pos,len,lvinfo->speech_before,DISKPATH_SIZE) > 0)
            {
              n++;
            }
            if (get_conf_parameter_single(buf,&pos,len,lvinfo->speech_after,DISKPATH_SIZE) > 0)
            {
              n++;
            }
            if (n < 2)
            {
              WARNMSG("Couldn't recognize \"%s\" file names in LOF file '%s'.",
                  COMMAND_TEXT(cmd_num),fname);
            }
            break;
        case 8: // LAND_VIEW
            if (get_conf_parameter_single(buf,&pos,len,lvinfo->land_view,DISKPATH_SIZE) > 0)
            {
              n++;
            }
            if (get_conf_parameter_single(buf,&pos,len,lvinfo->land_window,DISKPATH_SIZE) > 0)
            {
              n++;
            }
            if (n < 2)
            {
              WARNMSG("Couldn't recognize \"%s\" file names in LOF file '%s'.",
                  COMMAND_TEXT(cmd_num),fname);
            }
            break;
        case 9: // KIND
            while ((k = recognize_conf_parameter(buf,&pos,len,cmpgn_map_cmnds_kind)) > 0)
            {
              switch (k)
              {
              case LvOp_IsSingle:
                if ((lvinfo->options & LvOp_IsSingle) == 0)
                {
                    if (add_single_level_to_campaign(&campaign,lvinfo->lvnum) >= 0)
                        n++;
                    lvinfo->options |= LvOp_IsSingle;
                } else
                    n++;
                break;
              case LvOp_IsMulti:
                if ((lvinfo->options & LvOp_IsMulti) == 0)
                {
                    if (add_multi_level_to_campaign(&campaign,lvinfo->lvnum) >= 0)
                        n++;
                    lvinfo->options |= LvOp_IsMulti;
                } else
                    n++;
                break;
              case LvOp_IsBonus:
                if ((lvinfo->options & LvOp_IsBonus) == 0)
                {
                    if (add_bonus_level_to_campaign(&campaign,lvinfo->lvnum) >= 0)
                        n++;
                    lvinfo->options |= LvOp_IsBonus;
                } else
                    n++;
                break;
              case LvOp_IsExtra:
                if ((lvinfo->options & LvOp_IsExtra) == 0)
                {
                    if (add_extra_level_to_campaign(&campaign,lvinfo->lvnum) >= 0)
                        n++;
                    lvinfo->options |= LvOp_IsExtra;
                } else
                    n++;
                break;
              case LvOp_IsFree:
                if ((lvinfo->options & LvOp_IsFree) == 0)
                {
                    if (add_freeplay_level_to_campaign(&campaign,lvinfo->lvnum) >= 0)
                        n++;
                    lvinfo->options |= LvOp_IsFree;
                } else
                    n++;
                break;
              }
              if (n < 1)
              {
                  WARNMSG("Level %ld defined in '%s' wasn't added to any list; "
                      "kind is wrong or there's no space.",(long)lvinfo->lvnum,fname);
              }
            }
            break;
        case 10: // AUTHOR
        case 11: // DESCRIPTION
        case 12: // DATE
            // As for now, ignore these
            break;
        case 0: // comment
            break;
        case -1: // end of buffer
            break;
        default:
            WARNMSG("Unrecognized command (%d) in LOF file '%s', starting on byte %d.",cmd_num,fname,pos);
            break;
        }
        skip_conf_to_next_line(buf,&pos,len);
    }
    SYNCDBG(18,"Level %ld ensign (%d,%d) zoom (%d,%d)",(long)lvinfo->lvnum,(int)lvinfo->ensign_x,(int)lvinfo->ensign_y,(int)lvinfo->ensign_zoom_x,(int)lvinfo->ensign_zoom_y);
#undef COMMAND_TEXT
    return true;
}

/**
 * Searches levels folder for LOF files and adds them to campaign levels list.
 */
TbBool find_and_load_lof_files(void)
{
    struct TbFileFind fileinfo;
    unsigned char *buf;
    char *fname;
    short result;
    int rc;
    long i;
    SYNCDBG(16,"Starting");
    buf = LbMemoryAlloc(MAX_LIF_SIZE);
    if (buf == NULL)
    {
      ERRORLOG("Can't allocate memory for .LOF files parsing.");
      return false;
    }
    result = false;
    fname = prepare_file_path(FGrp_VarLevels,"*.lof");
    rc = LbFileFindFirst(fname, &fileinfo, 0x21u);
    while (rc != -1)
    {
        fname = prepare_file_path(FGrp_VarLevels,fileinfo.Filename);
        i = LbFileLength(fname);
        if ((i < 0) || (i >= MAX_LIF_SIZE))
        {
          WARNMSG("File '%s' too long (Max size %d)", fileinfo.Filename, MAX_LIF_SIZE);

        } else
        if (LbFileLoadAt(fname, buf) != i)
        {
          WARNMSG("Unable to read .LOF file, '%s'", fileinfo.Filename);
        } else
        {
          buf[i] = '\0';
          if (level_lof_file_parse(fileinfo.Filename, (char *)buf, i))
            result = true;
        }
        rc = LbFileFindNext(&fileinfo);
    }
    LbFileFindEnd(&fileinfo);
    LbMemoryFree(buf);
    return result;
}

long convert_old_column_file(unsigned long lv_num)
{
  return _DK_convert_old_column_file(lv_num);
}

TbBool load_column_file(unsigned long lv_num)
{
    //return _DK_load_column_file(lv_num);
    struct Column *colmn;
    unsigned long i;
    long k;
    unsigned short n;
    long total;
    unsigned char *buf;
    long fsize;
    if (game.numfield_C & 0x08)
    {
      convert_old_column_file(lv_num);
      set_flag_byte(&game.numfield_C,0x08,false);
    }
    fsize = 8;
    buf = load_single_map_file_to_buffer(lv_num,"clm",&fsize,LMFF_None);
    if (buf == NULL)
      return false;
    clear_columns();
    i = 0;
    total = llong(&buf[i]);
    i += 4;
    // Validate total amount of columns
    if ((total < 0) || (total > (fsize-8)/sizeof(struct Column)))
    {
      total = (fsize-8)/sizeof(struct Column);
      WARNMSG("Bad amount of columns in CLM file; corrected to %ld.",total);
    }
    if (total > COLUMNS_COUNT)
    {
      WARNMSG("Only %d columns supported, CLM file has %ld.",COLUMNS_COUNT,total);
      total = COLUMNS_COUNT;
    }
    // Read and validate second amount
    game.field_14AB3F = llong(&buf[i]);
    if (game.field_14AB3F >= COLUMNS_COUNT)
    {
      game.field_14AB3F = COLUMNS_COUNT-1;
    }
    i += 4;
    // Fill the columns
    for (k=0; k < total; k++)
    {
        colmn = &game.columns_data[k];
        LbMemoryCopy(colmn, &buf[i], sizeof(struct Column));
        //Update top cube in the column
        n = find_column_height(colmn);
        colmn->bitfileds &= 0x0F;
        colmn->bitfileds |= (n<<4) & 0xF0;
        i += sizeof(struct Column);
    }
    LbMemoryFree(buf);
    return true;
}

TbBool load_map_data_file(unsigned long lv_num)
{
  //return _DK_load_map_data_file(lv_num);
  struct Map *map;
  unsigned long x,y;
  unsigned char *buf;
  unsigned long i;
  unsigned long n;
  unsigned short *wptr;
  long fsize;
  clear_map();
  fsize = 2*(map_subtiles_y+1)*(map_subtiles_x+1);
  buf = load_single_map_file_to_buffer(lv_num,"dat",&fsize,LMFF_None);
  if (buf == NULL)
    return false;
  i = 0;
  for (y=0; y < (map_subtiles_y+1); y++)
    for (x=0; x < (map_subtiles_x+1); x++)
    {
      map = get_map_block_at(x,y);
      n = -lword(&buf[i]);
      map->data ^= (map->data ^ n) & 0x7FF;
      i += 2;
    }
  LbMemoryFree(buf);
  // Clear some bits and do some other setup
  for (y=0; y < (map_subtiles_y+1); y++)
    for (x=0; x < (map_subtiles_x+1); x++)
    {
      map = get_map_block_at(x,y);
      wptr = &game.subtile_lightness[get_subtile_number(x,y)];
      *wptr = 32;
      map->data &= 0xFFC007FFu;
      map->data &= 0xF0FFFFFFu;
      map->data &= 0x0FFFFFFFu;
    }
  return true;
}

TbBool load_thing_file(unsigned long lv_num)
{
  struct InitThing itng;
  unsigned long i;
  long k;
  long total;
  unsigned char *buf;
  long fsize;
  SYNCDBG(5,"Starting");
  fsize = 2;
  buf = load_single_map_file_to_buffer(lv_num,"tng",&fsize,LMFF_None);
  if (buf == NULL)
    return false;
  i = 0;
  total = lword(&buf[i]);
  i += 2;
  // Validate total amount of things
  if ((total < 0) || (total > (fsize-2)/sizeof(struct InitThing)))
  {
    total = (fsize-2)/sizeof(struct InitThing);
    WARNMSG("Bad amount of things in TNG file; corrected to %ld.",total);
  }
  if (total > THINGS_COUNT-2)
  {
    WARNMSG("Only %d things supported, TNG file has %ld.",THINGS_COUNT-2,total);
    total = THINGS_COUNT-2;
  }
  // Create things
  for (k=0; k < total; k++)
  {
    LbMemoryCopy(&itng, &buf[i], sizeof(struct InitThing));
    thing_create_thing(&itng);
    i += sizeof(struct InitThing);
  }
  LbMemoryFree(buf);
  return true;
}

TbBool load_action_point_file(unsigned long lv_num)
{
  struct InitActionPoint iapt;
  unsigned long i;
  long k;
  long total;
  unsigned char *buf;
  long fsize;
  SYNCDBG(5,"Starting");
  //return _DK_load_action_point_file(lv_num);
  fsize = 4;
  buf = load_single_map_file_to_buffer(lv_num,"apt",&fsize,LMFF_None);
  if (buf == NULL)
    return false;
  i = 0;
  total = llong(&buf[i]);
  i += 4;
  // Validate total amount of action points
  if ((total < 0) || (total > (fsize-4)/sizeof(struct InitActionPoint)))
  {
    total = (fsize-4)/sizeof(struct InitActionPoint);
    WARNMSG("Bad amount of action points in APT file; corrected to %ld.",total);
  }
  if (total > ACTN_POINTS_COUNT-1)
  {
    WARNMSG("Only %d action points supported, APT file has %ld.",ACTN_POINTS_COUNT-1,total);
    total = ACTN_POINTS_COUNT-1;
  }
  // Create action points
  for (k=0; k < total; k++)
  {
    LbMemoryCopy(&iapt, &buf[i], sizeof(struct InitActionPoint));
    if (actnpoint_create_actnpoint(&iapt) == &game.action_points[0])
    {
      ERRORLOG("Cannot allocate action point %d during APT load",k);
    }
    i += sizeof(struct InitActionPoint);
  }
  LbMemoryFree(buf);
  return true;
}

TbBool load_slabdat_file(struct SlabSet *slbset, long *scount)
{
  long total;
  unsigned char *buf;
  long fsize;
  long i,k,n;
  SYNCDBG(5,"Starting");
  fsize = 2;
  buf = load_data_file_to_buffer(&fsize, FGrp_StdData, "slabs.dat");
  if (buf == NULL)
    return false;
  i = 0;
  total = lword(&buf[i]);
  i += 2;
  // Validate total amount of indices
  if ((total < 0) || (total > (fsize-2)/(9*sizeof(short))))
  {
    total = (fsize-2)/(9*sizeof(short));
    WARNMSG("Bad amount of indices in Slab Set file; corrected to %ld.",total);
  }
  if (total > *scount)
  {
    WARNMSG("Only %d slabs supported, Slab Set file has %ld.",SLABSET_COUNT,total);
    total = *scount;
  }
  for (n=0; n < total; n++)
    for (k=0; k < 9; k++)
    {
      slbset[n].col_idx[k] = lword(&buf[i]);
      i += 2;
    }
  *scount = total;
  LbMemoryFree(buf);
  return true;
}

/**
 * Updates "use" property of given columns set, using given SlabSet entries.
 */
TbBool update_columns_use(struct Column *cols,long ccount,struct SlabSet *sset,long scount)
{
  long i,k;
  long ncol;
  for (i=0; i < ccount; i++)
  {
    cols[i].use = 0;
  }
  for (i=0; i < scount; i++)
    for (k=0; k < 9; k++)
    {
      ncol = -sset[i].col_idx[k];
      if ((ncol >= 0) && (ncol < ccount))
        cols[ncol].use++;
    }
  return true;
}

TbBool load_slabclm_file(struct Column *cols, long *ccount)
{
  long total;
  unsigned char *buf;
  long fsize;
  long i,k;
  SYNCDBG(18,"Starting");
  fsize = 4;
  buf = load_data_file_to_buffer(&fsize, FGrp_StdData, "slabs.clm");
  if (buf == NULL)
    return false;
  i = 0;
  total = llong(&buf[i]);
  i += 4;
  // Validate total amount of columns
  if ((total < 0) || (total > (fsize-4)/sizeof(struct Column)))
  {
    total = (fsize-4)/sizeof(struct Column);
    WARNMSG("Bad amount of columns in Column Set file; corrected to %ld.",total);
  }
  if (total > *ccount)
  {
    WARNMSG("Only %d columns supported, Column Set file has %ld.",*ccount,total);
    total = *ccount;
  }
  for (k=0; k < total; k++)
  {
    LbMemoryCopy(&cols[k],&buf[i],sizeof(struct Column));
    i += sizeof(struct Column);
  }
  *ccount = total;
  LbMemoryFree(buf);
  return true;
}

TbBool columns_add_static_entries(void)
{
    struct Column *colmn;
    struct Column lcolmn;
    short *wptr;
    short c[3];
    long ncol;
    long i,k;

    for (i=0; i < 3; i++)
      c[i] = 0;
    LbMemorySet(&lcolmn,0,sizeof(struct Column));
    wptr = &game.field_14A818[0];
    for (i=0; i < 3; i++)
    {
      memset(&lcolmn, 0, sizeof(struct Column));
      lcolmn.baseblock = c[i];
      for (k=0; k < 6; k++)
      {
        lcolmn.cubes[0] = player_cubes[k];
        make_solidmask(&lcolmn);
        ncol = find_column(&lcolmn);
        if (ncol == 0)
          ncol = create_column(&lcolmn);
        colmn = get_column(ncol);
        colmn->bitfileds |= 0x01;
        *wptr = -(short)ncol;
        wptr++;
      }
    }
    return true;
}

TbBool update_slabset_column_indices(struct Column *cols, long ccount)
{
    struct Column *colmn;
    struct Column lcolmn;
    struct SlabSet *sset;
    long ncol;
    long i,k,n;
    LbMemorySet(&lcolmn,0,sizeof(struct Column));
    for (i=0; i < game.slabset_num; i++)
    {
      sset = &game.slabset[i];
      for (k=0; k < 9; k++)
      {
          n = sset->col_idx[k];
          if (n >= 0)
          {
            lcolmn.baseblock = n;
            ncol = find_column(&lcolmn);
            if (ncol == 0)
            {
              ncol = create_column(&lcolmn);
              colmn = get_column(ncol);
              colmn->bitfileds |= 0x01;
            }
          } else
          {
            if (-n < ccount)
              ncol = find_column(&cols[-n]);
            else
              ncol = 0;
            if (ncol == 0)
            {
              ERRORLOG("E14R432Q#222564-3; I should be able to find a column here");
              continue;
            }
          }
          sset->col_idx[k] = -ncol;
      }
    }
    return true;
}

TbBool create_columns_from_list(struct Column *cols, long ccount)
{
    struct Column *colmn;
    long ncol;
    long i;
    for (i=1; i < ccount; i++)
    {
        if (cols[i].use)
        {
          ncol = find_column(&cols[i]);
          if (ncol == 0)
            ncol = create_column(&cols[i]);
          colmn = get_column(ncol);
          colmn->bitfileds |= 0x01;
        }
    }
    return true;
}

TbBool load_slab_datclm_files(void)
{
    struct Column *cols;
    long cols_tot;
    struct SlabSet *slbset;
    long slbset_tot;
    struct SlabSet *sset;
    long i;
    SYNCDBG(5,"Starting");
    // Load Column Set
    cols_tot = COLUMNS_COUNT;
    cols = (struct Column *)LbMemoryAlloc(cols_tot*sizeof(struct Column));
    if (cols == NULL)
    {
      WARNMSG("Can't allocate memory for %d column sets.",cols_tot);
      return false;
    }
    if (!load_slabclm_file(cols, &cols_tot))
    {
      LbMemoryFree(cols);
      return false;
    }
    // Load Slab Set
    slbset_tot = SLABSET_COUNT;
    slbset = (struct SlabSet *)LbMemoryAlloc(slbset_tot*sizeof(struct SlabSet));
    if (slbset == NULL)
    {
      WARNMSG("Can't allocate memory for %d slab sets.",slbset_tot);
      return false;
    }
    if (!load_slabdat_file(slbset, &slbset_tot))
    {
      LbMemoryFree(cols);
      LbMemoryFree(slbset);
      return false;
    }
    // Update the structure
    for (i=0; i < slbset_tot; i++)
    {
        sset = &game.slabset[i];
        LbMemoryCopy(sset, &slbset[i], sizeof(struct SlabSet));
    }
    game.slabset_num = slbset_tot;
    update_columns_use(cols,cols_tot,slbset,slbset_tot);
    LbMemoryFree(slbset);
    create_columns_from_list(cols,cols_tot);
    update_slabset_column_indices(cols,cols_tot);
    LbMemoryFree(cols);
    return true;
}

TbBool load_slab_tng_file(void)
{
    char *fname;
    SYNCDBG(5,"Starting");
    fname = prepare_file_fmtpath(FGrp_StdData,"slabs.tng");
    wait_for_cd_to_be_available();
    if ( LbFileExists(fname) )
      LbFileLoadAt(fname, &game.slabobjs_num);
    else
      ERRORLOG("Could not load slab object set");
    return true;
}

TbBool load_slab_file(void)
{
    TbBool result;
    result = true;
    if (!load_slab_datclm_files())
        result = false;
    if (!columns_add_static_entries())
        result = false;
    if (!load_slab_tng_file())
        result = false;
    return result;
}

long load_map_wibble_file(unsigned long lv_num)
{
    struct Map *map;
    unsigned long stl_x,stl_y;
    unsigned char *buf;
    unsigned long i,k;
    long fsize;
    fsize = (map_subtiles_y+1)*(map_subtiles_x+1);
    buf = load_single_map_file_to_buffer(lv_num,"wib",&fsize,LMFF_None);
    if (buf == NULL)
      return false;
    i = 0;
    for (stl_y=0; stl_y < (map_subtiles_y+1); stl_y++)
      for (stl_x=0; stl_x < (map_subtiles_x+1); stl_x++)
      {
        map = get_map_block_at(stl_x,stl_y);
        k = buf[i];
        map->data ^= ((map->data ^ (k << 22)) & 0xC00000);
        i++;
      }
    LbMemoryFree(buf);
    return true;
}

short load_map_ownership_file(unsigned long lv_num)
{
    struct SlabMap *slb;
    unsigned long x,y;
    unsigned char *buf;
    unsigned long i;
    long fsize;
    fsize = (map_subtiles_y+1)*(map_subtiles_x+1);
    buf = load_single_map_file_to_buffer(lv_num,"own",&fsize,LMFF_None);
    if (buf == NULL)
      return false;
    i = 0;
    for (y=0; y < (map_subtiles_y+1); y++)
      for (x=0; x < (map_subtiles_x+1); x++)
      {
        slb = get_slabmap_for_subtile(x,y);
        if ((x < map_subtiles_x) && (y < map_subtiles_y))
            slabmap_set_owner(slb,buf[i]);
        else
            slabmap_set_owner(slb,5);
        i++;
      }
    LbMemoryFree(buf);
    return true;
}

short initialise_map_wlb_auto(void)
{
    struct SlabMap *slb;
    struct SlabAttr *slbattr;
    unsigned long x,y;
    unsigned long n,nbridge;
    nbridge = 0;
    for (y=0; y < map_tiles_y; y++)
      for (x=0; x < map_tiles_x; x++)
      {
        slb = get_slabmap_block(x,y);
        if (slb->kind == SlbT_BRIDGE)
        {
          if (slabs_count_near(x,y,1,SlbT_LAVA) > slabs_count_near(x,y,1,SlbT_WATER))
            n = SlbT_LAVA;
          else
            n = SlbT_WATER;
          nbridge++;
        } else
        {
          n = slb->kind;
        }
        slbattr = get_slab_kind_attrs(n);
        n = (slbattr->field_15 << 3);
        slb->field_5 ^= (slb->field_5 ^ n) & 0x18;
      }
    SYNCMSG("Regenerated WLB flags, unsure for %lu bridge blocks.",nbridge);
    return true;
}

short load_map_wlb_file(unsigned long lv_num)
{
    struct SlabMap *slb;
    unsigned long x,y;
    unsigned char *buf;
    unsigned long i;
    unsigned long n;
    unsigned long nfixes;
    long fsize;
    SYNCDBG(7,"Starting");
    nfixes = 0;
    fsize = map_tiles_y*map_tiles_x;
    buf = load_single_map_file_to_buffer(lv_num,"wlb",&fsize,LMFF_Optional);
    if (buf == NULL)
      return false;
    i = 0;
    for (y=0; y < map_tiles_y; y++)
      for (x=0; x < map_tiles_x; x++)
      {
        slb = get_slabmap_block(x,y);
        n = (buf[i] << 3);
        n = slb->field_5 ^ ((slb->field_5 ^ n) & 0x18);
        slb->field_5 = n;
        n &= 0x18;
        if ((n != 16) || (slb->kind != SlbT_WATER))
          if ((n != 8) || (slb->kind != SlbT_LAVA))
            if (((n == 16) || (n == 8)) && (slb->kind != SlbT_BRIDGE))
            {
                nfixes++;
                slb->field_5 &= 0xE7u;
            }
        i++;
      }
    LbMemoryFree(buf);
    if (nfixes > 0)
    {
      ERRORLOG("WLB file is muddled - Fixed values for %lu tiles",nfixes);
    }
    return true;
}

short initialise_extra_slab_info(unsigned long lv_num)
{
    short result;
    initialise_map_rooms();
    result = load_map_wlb_file(lv_num);
    if (!result)
      result = initialise_map_wlb_auto();
    return result;
}

short load_map_slab_file(unsigned long lv_num)
{
    SYNCDBG(7,"Starting");
    //return _DK_load_map_slab_file(lv_num);
    struct SlabMap *slb;
    unsigned long x,y;
    unsigned char *buf;
    unsigned long i;
    unsigned long n;
    long fsize;
    fsize = 2*map_tiles_y*map_tiles_x;
    buf = load_single_map_file_to_buffer(lv_num,"slb",&fsize,LMFF_None);
    if (buf == NULL)
      return false;
    i = 0;
    for (y=0; y < map_tiles_y; y++)
      for (x=0; x < map_tiles_x; x++)
      {
        slb = get_slabmap_block(x,y);
        n = lword(&buf[i]);
        if (n > SLAB_TYPES_COUNT)
        {
          WARNMSG("Slab Type %d exceeds limit of %d",n,SLAB_TYPES_COUNT);
          n = SlbT_ROCK;
        }
        slb->kind = n;
        i += 2;
      }
    LbMemoryFree(buf);
    initialise_map_collides();
    initialise_map_health();
    initialise_extra_slab_info(lv_num);
    return true;
}

short load_map_flag_file(unsigned long lv_num)
{
    SYNCDBG(5,"Starting");
    struct Map *mapblk;
    unsigned long stl_x,stl_y;
    unsigned char *buf;
    unsigned long i;
    long fsize;
    fsize = 2*(map_subtiles_y+1)*(map_subtiles_x+1);
    buf = load_single_map_file_to_buffer(lv_num,"flg",&fsize,LMFF_Optional);
    if (buf == NULL)
        return false;
    i = 0;
    for (stl_y=0; stl_y < (map_subtiles_y+1); stl_y++)
    {
        for (stl_x=0; stl_x < (map_subtiles_x+1); stl_x++)
        {
            mapblk = get_map_block_at(stl_x,stl_y);
            mapblk->flags = buf[i];
            i += 2;
        }
    }
    LbMemoryFree(buf);
    return true;
}

long load_static_light_file(unsigned long lv_num)
{
    unsigned long i;
    long k;
    long total;
    unsigned char *buf;
    struct InitLight ilght;
    long fsize;
    fsize = 4;
    buf = load_single_map_file_to_buffer(lv_num,"lgt",&fsize,LMFF_Optional);
    if (buf == NULL)
        return false;
    light_initialise();
    i = 0;
    total = llong(&buf[i]);
    i += 4;
    // Validate total amount of lights
    if ((total < 0) || (total > (fsize-4)/sizeof(struct InitLight)))
    {
        total = (fsize-4)/sizeof(struct InitLight);
        WARNMSG("Bad amount of static lights in LGT file; corrected to %ld.",total);
    }
    if (total >= LIGHTS_COUNT)
    {
        WARNMSG("Only %d static lights supported, LGT file has %ld.",LIGHTS_COUNT,total);
        total = LIGHTS_COUNT-1;
    } else
    if (total >= LIGHTS_COUNT/2)
    {
        WARNMSG("More than %d%% of light slots used by static lights.",100*total/LIGHTS_COUNT);
    }
    // Create the lights
    for (k=0; k < total; k++)
    {
        LbMemoryCopy(&ilght, &buf[i], sizeof(struct InitLight));
        if (light_create_light(&ilght) == 0)
        {
            WARNLOG("Couldn't allocate static light %d",(int)k);
        }
        i += sizeof(struct InitLight);
    }
    LbMemoryFree(buf);
    return true;
}

short load_and_setup_map_info(unsigned long lv_num)
{
    unsigned char *buf;
    long fsize;
    fsize = 1;
    buf = load_single_map_file_to_buffer(lv_num,"inf",&fsize,LMFF_None);
    if (buf == NULL)
    {
        game.texture_id = 0;
        return false;
    }
    game.texture_id = buf[0];
    LbMemoryFree(buf);
    return true;
}

short load_level_file(LevelNumber lvnum)
{
    char *fname;
    short fgroup;
    short result;
    fgroup = get_level_fgroup(lvnum);
    fname = prepare_file_fmtpath(fgroup,"map%05lu.slb",(unsigned long)lvnum);
    wait_for_cd_to_be_available();
    if (LbFileExists(fname))
    {
        result = true;
        load_map_data_file(lvnum);
        load_map_flag_file(lvnum);
        load_column_file(lvnum);
        init_whole_blocks();
        load_slab_file();
        init_columns();
        load_static_light_file(lvnum);
        if (!load_map_ownership_file(lvnum))
          result = false;
        load_map_wibble_file(lvnum);
        load_and_setup_map_info(lvnum);
        load_texture_map_file(game.texture_id, 2);
        load_action_point_file(lvnum);
        if (!load_map_slab_file(lvnum))
          result = false;
        if (!load_thing_file(lvnum))
          result = false;
        reinitialise_treaure_rooms();
        ceiling_init(0, 1);
    } else
    {
        ERRORLOG("The level \"map%05lu\" doesn't exist; creating empty map.",lvnum);
        init_whole_blocks();
        load_slab_file();
        init_columns();
        game.texture_id = 0;
        load_texture_map_file(game.texture_id, 2);
        init_top_texture_to_cube_table();
        result = false;
    }
    return result;
}

TbBool load_map_file(LevelNumber lvnum)
{
    TbBool result;
    result = load_level_file(lvnum);
    if (result)
        set_loaded_level_number(lvnum);
    else
        set_loaded_level_number(SINGLEPLAYER_NOTSTARTED);
    return result;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
