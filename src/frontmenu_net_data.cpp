/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file frontmenu_net_data.cpp
 *     GUI menus for network support.
 * @par Purpose:
 *     Structures to show and maintain network screens.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     05 Jan 2009 - 15 Jun 2014
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "frontmenu_net.h"
#include "globals.h"
#include "bflib_basics.h"

#include "bflib_netsp.hpp"
#include "bflib_guibtns.h"
#include "bflib_video.h"
#include "bflib_vidraw.h"
#include "bflib_sprite.h"
#include "bflib_sprfnt.h"

#include "config_strings.h"
#include "front_network.h"
#include "gui_frontbtns.h"
#include "gui_draw.h"
#include "frontend.h"
#include "front_landview.h"
#include "net_game.h"
#include "sprites.h"
#include "custom_sprites.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct GuiButtonInit frontend_net_service_buttons[] = {
  { LbBtnT_NormalBtn,  BID_MENU_TITLE, 0, 0, NULL,               NULL,        NULL,               0, 999,  30, 999,  30,371, 46, frontend_draw_large_menu_button,   0, GUIStr_Empty, 0,      {10},            0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,        NULL,               0,  82, 124,  82, 124,220, 26, frontnet_draw_scroll_box_tab,      0, GUIStr_Empty, 0,      {12},            0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,        NULL,               0,  82, 150,  82, 150,450,180, frontnet_draw_scroll_box,          0, GUIStr_Empty, 0,      {26},            0, NULL },
  { LbBtnT_HoldableBtn,BID_DEFAULT, 0, 0, frontnet_service_up,NULL,frontend_over_button,       0, 532, 149, 532, 149, 26, 14, frontnet_draw_slider_button,       0, GUIStr_Empty, 0,      {17},            0, frontnet_service_up_maintain },
  { LbBtnT_HoldableBtn,BID_DEFAULT, 0, 0, frontnet_service_down,NULL,frontend_over_button,     0, 532, 317, 532, 317, 26, 14, frontnet_draw_slider_button,       0, GUIStr_Empty, 0,      {18},            0, frontnet_service_down_maintain },
  { LbBtnT_HoldableBtn,BID_DEFAULT, 0, 0, NULL,               NULL,        NULL,               0, 536, 163, 536, 163, 20,154, frontnet_draw_services_scroll_tab, 0, GUIStr_Empty, 0,      {40},            0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,        NULL,               0, 102, 125, 102, 125,220, 26, frontend_draw_text,                0, GUIStr_Empty, 0,      {33},            0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_service_select,NULL,frontend_over_button,   0,  95, 158,  95, 158,424, 26, frontnet_draw_service_button,      0, GUIStr_Empty, 0,      {45},            0, frontnet_service_maintain },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_service_select,NULL,frontend_over_button,   0,  95, 184,  95, 184,424, 26, frontnet_draw_service_button,      0, GUIStr_Empty, 0,      {46},            0, frontnet_service_maintain },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_service_select,NULL,frontend_over_button,   0,  95, 210,  95, 210,424, 26, frontnet_draw_service_button,      0, GUIStr_Empty, 0,      {47},            0, frontnet_service_maintain },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_service_select,NULL,frontend_over_button,   0,  95, 236,  95, 236,424, 26, frontnet_draw_service_button,      0, GUIStr_Empty, 0,      {48},            0, frontnet_service_maintain },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_service_select,NULL,frontend_over_button,   0,  95, 262,  95, 262,424, 26, frontnet_draw_service_button,      0, GUIStr_Empty, 0,      {49},            0, frontnet_service_maintain },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_service_select,NULL,frontend_over_button,   0,  95, 288,  95, 288,424, 26, frontnet_draw_service_button,      0, GUIStr_Empty, 0,      {50},            0, frontnet_service_maintain },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontend_change_state,NULL,frontend_over_button,     1, 999, 404, 999, 404,371, 46, frontend_draw_large_menu_button,   0, GUIStr_Empty, 0,       {6},            0, NULL },
  {-1,  BID_DEFAULT, 0, 0, NULL,               NULL,        NULL,               0,   0,   0,   0,   0,  0,  0, NULL,                              0,           0,  0,       {0},            0, NULL },
};

struct GuiButtonInit frontend_net_session_buttons[] = {
  { LbBtnT_NormalBtn,  BID_MENU_TITLE, 0, 0, NULL,               NULL,        NULL,               0, 999,  12, 999,  12,371, 46, frontend_draw_large_menu_button,   0, GUIStr_Empty, 0,      {12},            0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,        NULL,               0,  82,  61,  82,  61,165, 29, frontnet_draw_text_bar,            0, GUIStr_Empty, 0,      {27},            0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,        NULL,               0,  95,  63,  91,  63,165, 25, frontend_draw_text,                0, GUIStr_Empty, 0,      {19},            0, NULL },
  { 5, -1,-1, 0, frontnet_session_set_player_name,NULL,frontend_over_button,19,200,63,95,63,432, 25, frontend_draw_enter_text,          0, GUIStr_Empty, 0,{.str = tmp_net_player_name}, 20, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_session_add,NULL,      frontend_over_button,0, 321,  93, 321,  93,247, 46, frontend_draw_small_menu_button,   0, GUIStr_Empty, 0,     {110},            0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,        NULL,               0,  82, 112,  82, 112,220, 26, frontnet_draw_scroll_box_tab,      0, GUIStr_Empty, 0,      {28},            0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,        NULL,               0,  82, 138,  82, 138,450,180, frontnet_draw_scroll_box,          0, GUIStr_Empty, 0,      {25},            0, NULL },
  { LbBtnT_HoldableBtn,BID_DEFAULT, 0, 0, frontnet_session_up,NULL,       frontend_over_button,0, 532, 137, 532, 137, 26, 14, frontnet_draw_slider_button,       0, GUIStr_Empty, 0,      {17},            0, frontnet_session_up_maintain },
  { LbBtnT_HoldableBtn,BID_DEFAULT, 0, 0, frontnet_session_down,NULL,     frontend_over_button,0, 532, 217, 532, 217, 26, 14, frontnet_draw_slider_button,       0, GUIStr_Empty, 0,      {18},            0, frontnet_session_down_maintain },
  { LbBtnT_HoldableBtn,BID_DEFAULT, 0, 0, NULL,               NULL,        NULL,               0, 536, 151, 536, 151, 20, 66, frontnet_draw_sessions_scroll_tab, 0, GUIStr_Empty, 0,      {40},            0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,        NULL,               0, 102, 113, 102, 113,220, 26, frontend_draw_text,                0, GUIStr_Empty, 0,      {29},            0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,        NULL,               0,  82, 230,  82, 230,450, 28, frontnet_draw_session_selected,    0, GUIStr_Empty, 0,      {35},            0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_session_select,NULL,   frontend_over_button,0,  95, 141,  95, 141,424, 26, frontnet_draw_session_button,      0, GUIStr_Empty, 0,      {45},            0, frontnet_session_maintain },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_session_select,NULL,   frontend_over_button,0,  95, 167,  95, 167,424, 26, frontnet_draw_session_button,      0, GUIStr_Empty, 0,      {46},            0, frontnet_session_maintain },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_session_select,NULL,   frontend_over_button,0,  95, 193,  95, 193,424, 26, frontnet_draw_session_button,      0, GUIStr_Empty, 0,      {47},            0, frontnet_session_maintain },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,        NULL,               0,  82, 261,  82, 261,220, 26, frontnet_draw_scroll_box_tab,      0, GUIStr_Empty, 0,      {28},            0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,        NULL,               0,  82, 287,  82, 287,450, 74, frontnet_draw_scroll_box,          0, GUIStr_Empty, 0,      {24},            0, NULL },
  { LbBtnT_HoldableBtn,BID_DEFAULT, 0, 0, frontnet_players_up,NULL,       frontend_over_button,0, 532, 286, 532, 286, 26, 14, frontnet_draw_slider_button,       0, GUIStr_Empty, 0,      {36},            0, frontnet_players_up_maintain },
  { LbBtnT_HoldableBtn,BID_DEFAULT, 0, 0, frontnet_players_down,NULL,     frontend_over_button,0, 532, 344, 532, 344, 26, 14, frontnet_draw_slider_button,       0, GUIStr_Empty, 0,      {37},            0, frontnet_players_down_maintain },
  { LbBtnT_HoldableBtn,BID_DEFAULT, 0, 0, NULL,               NULL,        NULL,               0, 536, 300, 536, 300, 20, 44, frontnet_draw_players_scroll_tab,  0, GUIStr_Empty, 0,      {40},            0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,        NULL,               0,  95, 262,  95, 262,220, 22, frontend_draw_text,                0, GUIStr_Empty, 0,      {31},            0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,        NULL,               0,  95, 291,  82, 291,450, 52, frontnet_draw_net_session_players, 0, GUIStr_Empty, 0,      {21},            0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_session_join,NULL,     frontend_over_button,0,  72, 360,  72, 360,247, 46, frontend_draw_small_menu_button,   0, GUIStr_Empty, 0,      {13},            0, frontnet_join_game_maintain },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_session_create,NULL,   frontend_over_button,0, 321, 360, 321, 360,247, 46, frontend_draw_small_menu_button,   0, GUIStr_Empty, 0,      {14},            0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_return_to_main_menu,NULL,frontend_over_button,0,999,404, 999, 404,371, 46, frontend_draw_large_menu_button,   0, GUIStr_Empty, 0,       {6},            0, NULL },
  {-1,  BID_DEFAULT, 0, 0, NULL,               NULL,        NULL,               0,   0,   0,   0,   0,  0,  0, NULL,                              0,           0,  0,       {0},            0, NULL },
};

struct GuiButtonInit frontend_net_start_buttons[] = {
  { LbBtnT_NormalBtn,  BID_MENU_TITLE, 0, 0, NULL,               NULL,   NULL,                    0, 999,  30, 999,  30, 371, 46, frontend_draw_large_menu_button,   0, GUIStr_Empty, 0,  {12},    0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,   NULL,                    0,  82,  78,  82,  78, 220, 26, frontnet_draw_scroll_box_tab,      0, GUIStr_Empty, 0,  {28},    0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,   NULL,                    0, 421,  81, 421,  81, 100, 27, frontnet_draw_alliance_box_tab,    0, GUIStr_Empty, 0,  {28},    0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,   NULL,                    0,  82, 104,  82, 104, 450, 70, frontnet_draw_scroll_box,          0, GUIStr_Empty, 0,  {90},    0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,   NULL,                    0, 102,  79, 102,  79, 220, 26, frontend_draw_text,                0, GUIStr_Empty, 0,  {31},    0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,   NULL,                    0,  95, 105,  82, 105, 432,104, frontnet_draw_net_start_players,   0, GUIStr_Empty, 0,  {21},    0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_select_alliance,NULL,frontend_over_button,  0, 431, 107, 431, 116, 432, 88, frontnet_draw_alliance_grid,       0, GUIStr_Empty, 0,  {74},    0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_select_alliance,NULL,frontend_over_button,  0, 431, 108, 431, 108,  22, 26, frontnet_draw_alliance_button,     0, GUIStr_Empty, 0,  {74},    0, frontnet_maintain_alliance },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_select_alliance,NULL,frontend_over_button,  1, 453, 108, 453, 108,  22, 26, frontnet_draw_alliance_button,     0, GUIStr_Empty, 0,  {74},    0, frontnet_maintain_alliance },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_select_alliance,NULL,frontend_over_button,  2, 475, 108, 475, 108,  22, 26, frontnet_draw_alliance_button,     0, GUIStr_Empty, 0,  {74},    0, frontnet_maintain_alliance },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_select_alliance,NULL,frontend_over_button,  3, 497, 108, 497, 108,  22, 26, frontnet_draw_alliance_button,     0, GUIStr_Empty, 0,  {74},    0, frontnet_maintain_alliance },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_select_alliance,NULL,frontend_over_button,  0, 431, 134, 431, 134,  22, 26, frontnet_draw_alliance_button,     0, GUIStr_Empty, 0,  {75},    0, frontnet_maintain_alliance },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_select_alliance,NULL,frontend_over_button,  1, 453, 134, 453, 134,  22, 26, frontnet_draw_alliance_button,     0, GUIStr_Empty, 0,  {75},    0, frontnet_maintain_alliance },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_select_alliance,NULL,frontend_over_button,  2, 475, 134, 475, 134,  22, 26, frontnet_draw_alliance_button,     0, GUIStr_Empty, 0,  {75},    0, frontnet_maintain_alliance },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_select_alliance,NULL,frontend_over_button,  3, 497, 134, 497, 134,  22, 26, frontnet_draw_alliance_button,     0, GUIStr_Empty, 0,  {75},    0, frontnet_maintain_alliance },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_select_alliance,NULL,frontend_over_button,  0, 431, 160, 431, 160,  22, 26, frontnet_draw_alliance_button,     0, GUIStr_Empty, 0,  {76},    0, frontnet_maintain_alliance },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_select_alliance,NULL,frontend_over_button,  1, 453, 160, 453, 160,  22, 26, frontnet_draw_alliance_button,     0, GUIStr_Empty, 0,  {76},    0, frontnet_maintain_alliance },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_select_alliance,NULL,frontend_over_button,  2, 475, 160, 475, 160,  22, 26, frontnet_draw_alliance_button,     0, GUIStr_Empty, 0,  {76},    0, frontnet_maintain_alliance },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_select_alliance,NULL,frontend_over_button,  3, 497, 160, 497, 160,  22, 26, frontnet_draw_alliance_button,     0, GUIStr_Empty, 0,  {76},    0, frontnet_maintain_alliance },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_select_alliance,NULL,frontend_over_button,  0, 431, 186, 431, 183,  22, 26, frontnet_draw_alliance_button,     0, GUIStr_Empty, 0,  {77},    0, frontnet_maintain_alliance },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_select_alliance,NULL,frontend_over_button,  1, 453, 186, 453, 186,  22, 26, frontnet_draw_alliance_button,     0, GUIStr_Empty, 0,  {77},    0, frontnet_maintain_alliance },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_select_alliance,NULL,frontend_over_button,  2, 475, 186, 475, 186,  22, 26, frontnet_draw_alliance_button,     0, GUIStr_Empty, 0,  {77},    0, frontnet_maintain_alliance },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_select_alliance,NULL,frontend_over_button,  3, 497, 186, 497, 186,  22, 26, frontnet_draw_alliance_button,     0, GUIStr_Empty, 0,  {77},    0, frontnet_maintain_alliance },
  { LbBtnT_HoldableBtn,BID_DEFAULT, 0, 0, NULL,               NULL,   NULL,                    0, 284, 217, 284, 217, 240, 26, frontnet_draw_bottom_scroll_box_tab,0,GUIStr_Empty, 0,  {28},    0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontend_toggle_computer_players,NULL,frontend_over_button,0,297,214,297,214,220,26, frontend_draw_computer_players,    0, GUIStr_Empty, 0, {103},    0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,   NULL,                    0,  82, 246,  82, 246, 220, 26, frontnet_draw_scroll_box_tab,      0, GUIStr_Empty, 0,  {28},    0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,   NULL,                    0,  82, 272,  82, 272, 450,111, frontnet_draw_scroll_box,          0, GUIStr_Empty, 0,  {91},    0, NULL },
  { LbBtnT_HoldableBtn,BID_DEFAULT, 0, 0, frontnet_messages_up,NULL,  frontend_over_button,    0, 532, 271, 532, 271,  26, 14, frontnet_draw_slider_button,       0, GUIStr_Empty, 0,  {38},    0, frontnet_messages_up_maintain },
  { LbBtnT_HoldableBtn,BID_DEFAULT, 0, 0, frontnet_messages_down,NULL,frontend_over_button,    0, 532, 373, 532, 373,  26, 14, frontnet_draw_slider_button,       0, GUIStr_Empty, 0,  {39},    0, frontnet_messages_down_maintain },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,   NULL,                    0, 102, 247, 102, 247, 220, 26, frontend_draw_text,                0, GUIStr_Empty, 0,  {34},    0, NULL },
  { LbBtnT_HoldableBtn,BID_DEFAULT, 0, 0, NULL,               NULL,   NULL,                    0, 536, 285, 536, 285,  20, 88, frontnet_draw_messages_scroll_tab, 0, GUIStr_Empty, 0,  {40},    0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,   NULL,                    0,  82, 386,  82, 386, 459, 28, frontnet_draw_current_message,     0, GUIStr_Empty, 0,  {43},    0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,   NULL,                    0,  89, 273,  89, 273, 438,104, frontnet_draw_messages,            0, GUIStr_Empty, 0,  {44},    0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, set_packet_start,   NULL,   frontend_over_button,    0,  49, 412,  49, 412, 247, 46, frontend_draw_small_menu_button,   0, GUIStr_Empty, 0,  {15},    0, frontnet_start_game_maintain },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_return_to_session_menu,NULL,frontend_over_button,1, 345,412,345,412,247,46, frontend_draw_small_menu_button,   0, GUIStr_Empty, 0,  {16},    0, NULL },
  {-1,  BID_DEFAULT, 0, 0, NULL,               NULL,   NULL,                    0,   0,   0,   0,   0,   0,  0, NULL,                              0,           0,  0,   {0},    0, NULL },
};

struct GuiButtonInit frontend_add_session_buttons[] = {//TODO GUI prepare add session screen
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, NULL,               NULL,        NULL,               0, 999, 0, 999, 0,450,180, frontnet_draw_scroll_box,               0, GUIStr_Empty, 0,      {26},            0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_add_session_done,NULL,  frontend_over_button,0,  72, 48,  72, 48,247, 46, frontend_draw_small_menu_button,     0, GUIStr_Empty, 0,      {13},            0, NULL },
  { LbBtnT_NormalBtn,  BID_DEFAULT, 0, 0, frontnet_add_session_back,NULL,  frontend_over_button,0, 321, 48, 321, 48,247, 46, frontend_draw_small_menu_button,     0, GUIStr_Empty, 0,      {16},            0, NULL },
  {-1,  BID_DEFAULT, 0, 0, NULL,               NULL,        NULL,               0,   0,   0,   0,   0,   0,  0, NULL,                              0, GUIStr_Empty, 0,       {0},            0, NULL },
};

struct GuiMenu frontend_net_service_menu =
 { GMnu_FENET_SERVICE, 0, 1, frontend_net_service_buttons, POS_SCRCTR, POS_SCRCTR,  640, 480, NULL, 0, NULL,    NULL,                    0, 0, 0,};
struct GuiMenu frontend_net_session_menu =
 { GMnu_FENET_SESSION, 0, 1, frontend_net_session_buttons, POS_SCRCTR, POS_SCRCTR,  640, 480, NULL, 0, NULL,    NULL,                    0, 0, 0,};
struct GuiMenu frontend_net_start_menu =
 { GMnu_FENET_START,   0, 1, frontend_net_start_buttons,   POS_SCRCTR, POS_SCRCTR,  640, 480, NULL, 0, NULL,    NULL,                    0, 0, 0,};
struct GuiMenu frontend_add_session_box =
 { GMnu_FEADD_SESSION, 0, 1, frontend_add_session_buttons, POS_SCRCTR, POS_SCRCTR,  450,  92, NULL, 0, NULL,    NULL,                    0, 1, 0,};
/******************************************************************************/
#ifdef __cplusplus
}
#endif
/******************************************************************************/
void frontnet_draw_session_selected(struct GuiButton *gbtn)
{
    const struct TbSprite *spr;
    long pos_x;
    long pos_y;
    int i;
    pos_x = gbtn->scr_pos_x;
    pos_y = gbtn->scr_pos_y;
    int fs_units_per_px;
    fs_units_per_px = simple_frontend_sprite_height_units_per_px(gbtn, GFS_largearea_xts_tx1_c, 100);
    spr = get_frontend_sprite(GFS_largearea_xts_cor_l);
    for (i=0; i < 6; i++)
    {
        LbSpriteDrawResized(pos_x, pos_y, fs_units_per_px, spr);
        pos_x += spr->SWidth * fs_units_per_px / 16;
        spr++;
    }
    if (net_session_index_active >= 0)
    {
        const char *text;
        text = net_session[net_session_index_active]->text;
        i = frontend_button_caption_font(gbtn, 0);
        if (text != NULL)
        {
            lbDisplay.DrawFlags = 0;
            LbTextSetFont(frontend_font[i]);
            // Set drawing window and draw the text
            int tx_units_per_px;
            tx_units_per_px = (gbtn->height*13/14) * 16 / LbTextLineHeight();
            int h;
            h = LbTextLineHeight()*tx_units_per_px/16;
            LbTextSetWindow(gbtn->scr_pos_x + 13*fs_units_per_px/16, gbtn->scr_pos_y, gbtn->width - 26*fs_units_per_px/16, h);
            LbTextDrawResized(0, 0, tx_units_per_px, text);
        }
    }
}

void frontnet_session_select(struct GuiButton *gbtn)
{
    long i;
    i = gbtn->content.lval + net_session_scroll_offset - 45;
    if (net_number_of_sessions > i)
    {
        net_session_index_active = i;
        net_session_index_active_id = net_session[i]->id;
    }
}

void frontnet_draw_session_button(struct GuiButton *gbtn)
{
    long sessionIndex;
    long febtn_idx;
    long height;

    febtn_idx = gbtn->content.lval;
    sessionIndex = net_session_scroll_offset + febtn_idx - 45;
    if ((sessionIndex < 0) || (sessionIndex >= net_number_of_sessions))
        return;
    int font_idx;
    font_idx = frontend_button_caption_font(gbtn,frontend_mouse_over_button);
    LbTextSetFont(frontend_font[font_idx]);
    lbDisplay.DrawFlags = 0;
    int tx_units_per_px;
    tx_units_per_px = gbtn->height * 16 / LbTextLineHeight();
    height = LbTextLineHeight() * tx_units_per_px / 16;
    LbTextSetWindow(gbtn->scr_pos_x, gbtn->scr_pos_y, gbtn->width, height);
    LbTextDrawResized(0, 0, tx_units_per_px, net_session[sessionIndex]->text);
}

void frontnet_session_create(struct GuiButton *gbtn)
{
    // Create a new session using the player name as the session name.
    // Append a number to the session name if it already exists.
    long idx = 0;
    for (int i = 0; i < net_number_of_sessions; i++)
    {
        const auto nsname = net_session[i];
        if (nsname == nullptr) continue;
        const char * backslash = strchr(nsname->text, '\'');
        if (backslash) {
            if (strlen(net_player_name) == backslash - nsname->text) {
                if (strncmp(nsname->text, net_player_name, backslash - nsname->text) == 0) {
                    idx++;
                }
            }
        } else if (strcmp(nsname->text, net_player_name) == 0) {
            idx++;
        }
    }
    char text[sizeof(net_session[0]->text) + 16];
    if (idx > 0) {
        snprintf(text, sizeof(text), "%s (%ld)", net_player_name, idx + 1);
    } else {
        snprintf(text, sizeof(text), "%s", net_player_name);
    }
    unsigned long plyr_num;
    if (LbNetwork_Create(text, net_player_name, &plyr_num, nullptr))
    {
        process_network_error(-801);
        return;
    }
    frontend_set_player_number(plyr_num);
    fe_computer_players = 0;
    frontend_set_state(FeSt_NET_START);
}
/******************************************************************************/
