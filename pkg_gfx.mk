#******************************************************************************
#  Free implementation of Bullfrog's Dungeon Keeper strategy game.
#******************************************************************************
#   @file pkg_gfx.mk
#      A script used by GNU Make to recompile the project.
#  @par Purpose:
#      Defines make rules for tools needed to build KeeperFX.
#      Most tools can either by compiled from source or downloaded.
#  @par Comment:
#      None.
#  @author   Tomasz Lis
#  @date     25 Jan 2009 - 02 Jul 2011
#  @par  Copying and copyrights:
#      This program is free software; you can redistribute it and/or modify
#      it under the terms of the GNU General Public License as published by
#      the Free Software Foundation; either version 2 of the License, or
#      (at your option) any later version.
#
#******************************************************************************

LANDVIEWRAWS = \
$(foreach num,00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21,pkg/campgns/keeporig_lnd/rgmap$(num).raw pkg/campgns/keeporig_lnd/viframe$(num).dat) \
$(foreach num,00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21,pkg/campgns/ancntkpr_lnd/rgmap$(num).raw pkg/campgns/ancntkpr_lnd/viframe$(num).dat) \
$(foreach num,00 01 02 03 04 05 06 07 08 09,pkg/campgns/burdnimp_lnd/rgmap$(num).raw pkg/campgns/burdnimp_lnd/viframe$(num).dat) \
$(foreach num,00 01 02 03 04 05 06 07,pkg/campgns/dzjr06lv_lnd/rgmap$(num).raw pkg/campgns/dzjr06lv_lnd/viframe$(num).dat) \
$(foreach num,00 01 02 03 04 05 06 07 08 09 10,pkg/campgns/jdkmaps8_lnd/rgmap$(num).raw pkg/campgns/jdkmaps8_lnd/viframe$(num).dat) \
$(foreach num,00 01 02 03 04 05 06 07 08 09 10 11 12,pkg/campgns/lqizgood_lnd/rgmap$(num).raw pkg/campgns/lqizgood_lnd/viframe$(num).dat) \
$(foreach num,00 01 02 03 04 05 06 07 08,pkg/campgns/postanck_lnd/rgmap$(num).raw pkg/campgns/postanck_lnd/viframe$(num).dat) \
$(foreach num,00 01 02 03 04 05 06 07,pkg/campgns/pstunded_lnd/rgmap$(num).raw pkg/campgns/pstunded_lnd/viframe$(num).dat) \
$(foreach num,00 01 02 03 04 05 06 07,pkg/campgns/twinkprs_lnd/rgmap$(num).raw pkg/campgns/twinkprs_lnd/viframe$(num).dat) \
$(foreach num,00 01 02 03 04 05 06 07,pkg/campgns/undedkpr_lnd/rgmap$(num).raw pkg/campgns/undedkpr_lnd/viframe$(num).dat)

LANDVIEWDATTABS = \
pkg/ldata/lndflag_ens.dat \
pkg/ldata/netflag_ens.dat \
pkg/ldata/lndflag_pin.dat \
pkg/ldata/netflag_pin.dat \
pkg/ldata/maphand.dat \
pkg/ldata/netfont.dat

TOTRUREGFX = \
pkg/ldata/torture.raw \
pkg/ldata/door01.dat \
pkg/ldata/door02.dat \
pkg/ldata/door03.dat \
pkg/ldata/door04.dat \
pkg/ldata/door05.dat \
pkg/ldata/door06.dat \
pkg/ldata/door07.dat \
pkg/ldata/door08.dat \
pkg/ldata/door09.dat \
pkg/ldata/fronttor.dat

FRONTENDGFX = \
pkg/data/legal32.raw \
pkg/data/legal64.raw \
pkg/data/legal-720p-wide.raw \
pkg/data/legal-1080p-wide.raw \
pkg/data/startfx32.raw \
pkg/data/startfx64.raw \
pkg/data/loading32.raw \
pkg/data/loading64.raw \
pkg/data/nocd.raw \
pkg/ldata/front.raw \
pkg/ldata/frontft1.dat \
pkg/ldata/frontft2.dat \
pkg/ldata/frontft3.dat \
pkg/ldata/frontft4.dat \
pkg/ldata/frontbit.dat

ENGINEGFX = \
pkg/data/creature.jty \
pkg/data/frac00.raw \
pkg/data/frac01.raw \
pkg/data/frac02.raw \
pkg/data/frac03.raw \
pkg/data/frac04.raw \
pkg/data/frac05.raw \
pkg/data/frac06.raw \
pkg/data/frac07.raw \
pkg/data/frac08.raw \
pkg/data/gui2-64.dat \
pkg/data/gui2-32.dat \
pkg/data/gui1-64.dat \
pkg/data/gui1-32.dat \
pkg/data/pointer64.dat \
pkg/data/font1-64.dat \
pkg/data/font1-32.dat \
pkg/data/font2-32.dat \
pkg/data/font2-64.dat \
pkg/data/tmapa000.dat \
pkg/data/tmapa001.dat \
pkg/data/tmapa002.dat \
pkg/data/tmapa003.dat \
pkg/data/tmapa004.dat \
pkg/data/tmapa005.dat \
pkg/data/tmapa006.dat \
pkg/data/tmapa007.dat \
pkg/data/tmapa008.dat \
pkg/data/tmapa009.dat \
pkg/data/tmapa010.dat \
pkg/data/tmapa011.dat \
pkg/data/tmapa012.dat \
pkg/data/tmapa013.dat \
pkg/data/tmapb000.dat \
pkg/data/tmapb001.dat \
pkg/data/tmapb002.dat \
pkg/data/tmapb003.dat \
pkg/data/tmapb004.dat \
pkg/data/tmapb005.dat \
pkg/data/tmapb006.dat \
pkg/data/tmapb007.dat \
pkg/data/tmapb008.dat \
pkg/data/tmapb009.dat \
pkg/data/tmapb010.dat \
pkg/data/tmapb011.dat \
pkg/data/tmapb012.dat \
pkg/data/tmapb013.dat \
pkg/data/swipe01.dat \
pkg/data/swipe02.dat \
pkg/data/swipe03.dat \
pkg/data/swipe04.dat \
pkg/data/swipe05.dat \
pkg/data/swipe06.dat \
pkg/data/swipe07.dat \
pkg/data/gmap64.raw \
pkg/data/gmap32.raw \
pkg/data/gmapbug.dat

GUIDATTABS = $(LANDVIEWDATTABS) $(TOTRUREDATTABS) $(ENGINEDATTABS)

.PHONY: pkg-gfx pkg-landviews pkg-menugfx pkg-enginegfx

pkg-gfx: pkg-landviews pkg-menugfx pkg-enginegfx

pkg-landviews: $(LANDVIEWRAWS) $(LANDVIEWDATTABS)

pkg-menugfx: $(TOTRUREGFX) $(FRONTENDGFX)

pkg-enginegfx: $(ENGINEGFX)

pkg-landviewtabs: $(LANDVIEWDATTABS)

# Creation of land view image files for campaigns
define define_campaign_landview_rule
pkg/campgns/$(1)_lnd/rgmap%.pal: gfx/landviews/$(1)_lnd/rgmap%.png gfx/landviews/$(1)_lnd/viframe.png tools/png2bestpal/res/color_tbl_landview.txt $$(PNGTOBSPAL)
	-$$(ECHO) 'Building land view palette: $$@'
	@$$(MKDIR) $$(@D)
	$$(PNGTOBSPAL) -o "$$@" -m "$$(word 3,$$^)" "$$(word 1,$$^)" "$$(word 2,$$^)"
	-$$(ECHO) 'Finished building: $$@'
	-$$(ECHO) ' '

pkg/campgns/$(1)_lnd/rgmap%.raw: gfx/landviews/$(1)_lnd/rgmap%.png pkg/campgns/$(1)_lnd/rgmap%.pal $$(PNGTORAW) $$(RNC)
	-$$(ECHO) 'Building land view image: $$@'
	$$(PNGTORAW) -o "$$@" -p "$$(word 2,$$^)" -f raw -l 100 "$$<"
	-$$(RNC) "$$@"
	-$$(ECHO) 'Finished building: $$@'
	-$$(ECHO) ' '

pkg/campgns/$(1)_lnd/viframe%.dat: gfx/landviews/$(1)_lnd/viframe.png pkg/campgns/$(1)_lnd/rgmap%.pal $$(PNGTORAW) $$(RNC)
	-$$(ECHO) 'Building land view frame: $$@'
	$$(PNGTORAW) -o "$$@" -p "$$(word 2,$$^)" -f hspr -l 50 "$$<"
	-$$(RNC) "$$@"
	-$$(ECHO) 'Finished building: $$@'
	-$$(ECHO) ' '

# mark palette files precious to make sure they're not auto-removed after dependencies are built
.PRECIOUS: pkg/campgns/$(1)_lnd/rgmap%.pal
endef

$(foreach campaign,$(sort $(CAMPAIGNS)),$(eval $(call define_campaign_landview_rule,$(campaign))))

pkg/ldata/torture.pal:  gfx/menufx/torturescr/tortr_background.png gfx/menufx/torturescr/tortr_doora_open11.png gfx/menufx/torturescr/tortr_doorb_open11.png gfx/menufx/torturescr/tortr_doorc_open11.png gfx/menufx/torturescr/tortr_doord_open11.png gfx/menufx/torturescr/tortr_doore_open11.png gfx/menufx/torturescr/tortr_doorf_open11.png gfx/menufx/torturescr/tortr_doorg_open11.png gfx/menufx/torturescr/tortr_doorh_open11.png gfx/menufx/torturescr/tortr_doori_open11.png gfx/menufx/torturescr/cursor_horny.png tools/png2bestpal/res/color_tbl_basic.txt $(PNGTOBSPAL)
pkg/data/legal32.pal:   gfx/menufx/loading/legal-32.png tools/png2bestpal/res/color_tbl_basic.txt $(PNGTOBSPAL)
pkg/data/legal64.pal:   gfx/menufx/loading/legal-64.png tools/png2bestpal/res/color_tbl_basic.txt $(PNGTOBSPAL)
pkg/data/legal-720p-wide.pal: gfx/menufx/loading/legal-720p-wide.png tools/png2bestpal/res/color_tbl_basic.txt $(PNGTOBSPAL)
pkg/data/legal-1080p-wide.pal: gfx/menufx/loading/legal-1080p-wide.png tools/png2bestpal/res/color_tbl_basic.txt $(PNGTOBSPAL)
pkg/data/startfx32.pal: gfx/menufx/loading/startupfx-32.png tools/png2bestpal/res/color_tbl_basic.txt $(PNGTOBSPAL)
pkg/data/startfx64.pal: gfx/menufx/loading/startupfx-64.png tools/png2bestpal/res/color_tbl_basic.txt $(PNGTOBSPAL)
pkg/data/loading32.pal: gfx/menufx/loading/loading-32.png tools/png2bestpal/res/color_tbl_basic.txt $(PNGTOBSPAL)
pkg/data/loading64.pal: gfx/menufx/loading/loading-64.png tools/png2bestpal/res/color_tbl_basic.txt $(PNGTOBSPAL)
pkg/data/nocd.pal:      gfx/menufx/loading/nocd-32.png tools/png2bestpal/res/color_tbl_basic.txt $(PNGTOBSPAL)

pkg/ldata/torture.pal pkg/data/legal32.pal pkg/data/legal64.pal pkg/data/legal-720p-wide.pal pkg/data/legal-1080p-wide.pal pkg/data/startfx32.pal pkg/data/startfx64.pal pkg/data/loading32.pal pkg/data/loading64.pal pkg/data/nocd.pal:
	-$(ECHO) 'Building palette: $@'
	@$(MKDIR) $(@D)
	$(PNGTOBSPAL) -o "$@" -m "$(filter %.txt,$^)" $(filter %.png,$^)
	-$(ECHO) 'Finished building: $@'
	-$(ECHO) ' '

pkg/ldata/front.pal: gfx/palettes/front.pal
pkg/data/palette.dat: gfx/palettes/engine.pal

pkg/ldata/front.pal pkg/data/palette.dat:
	-$(ECHO) 'Building palette: $@'
	@$(MKDIR) $(@D)
	# Simplified, for now
	$(CP) "$<" "$@"
	-$(ECHO) 'Finished building: $@'
	-$(ECHO) ' '

# mark palette files precious to make sure they're not auto-removed after dependencies are built
.PRECIOUS: pkg/ldata/torture.pal pkg/ldata/front.pal pkg/data/palette.dat

pkg/ldata/lndflag_ens.dat: gfx/landviewdattabs/landview_ensign/filelist_lndflag.txt pkg/campgns/keeporig_lnd/rgmap00.pal $(PNGTORAW)
pkg/ldata/netflag_ens.dat: gfx/landviewdattabs/landview_ensign/filelist_netflag.txt pkg/campgns/keeporig_lnd/rgmap00.pal $(PNGTORAW)
pkg/ldata/lndflag_pin.dat: gfx/landviewdattabs/landview_pinpnt/filelist_lndflag.txt pkg/campgns/keeporig_lnd/rgmap00.pal $(PNGTORAW)
pkg/ldata/netflag_pin.dat: gfx/landviewdattabs/landview_pinpnt/filelist_netflag.txt pkg/campgns/keeporig_lnd/rgmap00.pal $(PNGTORAW)
pkg/ldata/maphand.dat:     gfx/landviewdattabs/landview_hand/filelist_maphand.txt pkg/campgns/keeporig_lnd/rgmap00.pal $(PNGTORAW)
pkg/ldata/netfont.dat:     gfx/menufx/font_net/filelist_netfont.txt pkg/campgns/keeporig_lnd/rgmap00.pal $(PNGTORAW)

pkg/ldata/fronttor.dat: gfx/menufx/torturescr/filelist_fronttor.txt pkg/ldata/torture.pal $(PNGTORAW)
pkg/ldata/door01.dat:   gfx/menufx/torturescr/filelist_tortr_doora.txt pkg/ldata/torture.pal $(PNGTORAW)
pkg/ldata/door02.dat:   gfx/menufx/torturescr/filelist_tortr_doorb.txt pkg/ldata/torture.pal $(PNGTORAW)
pkg/ldata/door03.dat:   gfx/menufx/torturescr/filelist_tortr_doorc.txt pkg/ldata/torture.pal $(PNGTORAW)
pkg/ldata/door04.dat:   gfx/menufx/torturescr/filelist_tortr_doord.txt pkg/ldata/torture.pal $(PNGTORAW)
pkg/ldata/door05.dat:   gfx/menufx/torturescr/filelist_tortr_doore.txt pkg/ldata/torture.pal $(PNGTORAW)
pkg/ldata/door06.dat:   gfx/menufx/torturescr/filelist_tortr_doorf.txt pkg/ldata/torture.pal $(PNGTORAW)
pkg/ldata/door07.dat:   gfx/menufx/torturescr/filelist_tortr_doorg.txt pkg/ldata/torture.pal $(PNGTORAW)
pkg/ldata/door08.dat:   gfx/menufx/torturescr/filelist_tortr_doorh.txt pkg/ldata/torture.pal $(PNGTORAW)
pkg/ldata/door09.dat:   gfx/menufx/torturescr/filelist_tortr_doori.txt pkg/ldata/torture.pal $(PNGTORAW)
pkg/ldata/torture.raw:  gfx/menufx/torturescr/tortr_background.png pkg/ldata/torture.pal $(PNGTORAW)

pkg/ldata/front.raw:    gfx/menufx/frontend-64/front_background-64.png pkg/ldata/front.pal $(PNGTORAW)
pkg/ldata/frontbit.dat: gfx/menufx/frontend-64/filelist_frontbit.txt pkg/ldata/front.pal $(PNGTORAW)
pkg/ldata/frontft1.dat: gfx/menufx/font_front_hdr_red-64/filelist_frontft1.txt pkg/ldata/front.pal $(PNGTORAW)
pkg/ldata/frontft2.dat: gfx/menufx/font_front_std_red-64/filelist_frontft2.txt pkg/ldata/front.pal $(PNGTORAW)
pkg/ldata/frontft3.dat: gfx/menufx/font_front_std_ylw-64/filelist_frontft3.txt pkg/ldata/front.pal $(PNGTORAW)
pkg/ldata/frontft4.dat: gfx/menufx/font_front_std_dkr-64/filelist_frontft4.txt pkg/ldata/front.pal $(PNGTORAW)

pkg/data/frac00.raw: gfx/enginefx/textures-32/frac00.png gfx/enginefx/textures-32/fract_bw.pal $(PNGTORAW)
pkg/data/frac01.raw: gfx/enginefx/textures-32/frac01.png gfx/enginefx/textures-32/fract_bw.pal $(PNGTORAW)
pkg/data/frac02.raw: gfx/enginefx/textures-32/frac02.png gfx/enginefx/textures-32/fract_bw.pal $(PNGTORAW)
pkg/data/frac03.raw: gfx/enginefx/textures-32/frac03.png gfx/enginefx/textures-32/fract_bw.pal $(PNGTORAW)
pkg/data/frac04.raw: gfx/enginefx/textures-32/frac04.png gfx/enginefx/textures-32/fract_bw.pal $(PNGTORAW)
pkg/data/frac05.raw: gfx/enginefx/textures-32/frac05.png gfx/enginefx/textures-32/fract_bw.pal $(PNGTORAW)
pkg/data/frac06.raw: gfx/enginefx/textures-32/frac06.png gfx/enginefx/textures-32/fract_bw.pal $(PNGTORAW)
pkg/data/frac07.raw: gfx/enginefx/textures-32/frac07.png gfx/enginefx/textures-32/fract_bw.pal $(PNGTORAW)
pkg/data/frac08.raw: gfx/enginefx/textures-32/frac08.png gfx/enginefx/textures-32/fract_bw.pal $(PNGTORAW)

pkg/data/tmapa000.dat: gfx/enginefx/textures-32/filelist_tmapa000.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapa001.dat: gfx/enginefx/textures-32/filelist_tmapa001.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapa002.dat: gfx/enginefx/textures-32/filelist_tmapa002.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapa003.dat: gfx/enginefx/textures-32/filelist_tmapa003.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapa004.dat: gfx/enginefx/textures-32/filelist_tmapa004.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapa005.dat: gfx/enginefx/textures-32/filelist_tmapa005.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapa006.dat: gfx/enginefx/textures-32/filelist_tmapa006.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapa007.dat: gfx/enginefx/textures-32/filelist_tmapa007.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapa008.dat: gfx/enginefx/textures-32/filelist_tmapa008.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapa009.dat: gfx/enginefx/textures-32/filelist_tmapa009.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapa010.dat: gfx/enginefx/textures-32/filelist_tmapa010.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapa011.dat: gfx/enginefx/textures-32/filelist_tmapa011.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapa012.dat: gfx/enginefx/textures-32/filelist_tmapa012.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapa013.dat: gfx/enginefx/textures-32/filelist_tmapa013.txt pkg/data/palette.dat $(PNGTORAW)

pkg/data/tmapb000.dat: gfx/enginefx/textures-32/filelist_tmapb000.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapb001.dat: gfx/enginefx/textures-32/filelist_tmapb001.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapb002.dat: gfx/enginefx/textures-32/filelist_tmapb002.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapb003.dat: gfx/enginefx/textures-32/filelist_tmapb003.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapb004.dat: gfx/enginefx/textures-32/filelist_tmapb004.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapb005.dat: gfx/enginefx/textures-32/filelist_tmapb005.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapb006.dat: gfx/enginefx/textures-32/filelist_tmapb006.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapb007.dat: gfx/enginefx/textures-32/filelist_tmapb007.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapb008.dat: gfx/enginefx/textures-32/filelist_tmapb008.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapb009.dat: gfx/enginefx/textures-32/filelist_tmapb009.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapb010.dat: gfx/enginefx/textures-32/filelist_tmapb010.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapb011.dat: gfx/enginefx/textures-32/filelist_tmapb011.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapb012.dat: gfx/enginefx/textures-32/filelist_tmapb012.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/tmapb013.dat: gfx/enginefx/textures-32/filelist_tmapb013.txt pkg/data/palette.dat $(PNGTORAW)

pkg/data/gmap32.raw:    gfx/enginefx/guimap/gmap-32.png pkg/data/palette.dat $(PNGTORAW)
pkg/data/gmap64.raw:    gfx/enginefx/guimap/gmap-64.png pkg/data/palette.dat $(PNGTORAW)
pkg/data/gmapbug.dat:   gfx/enginefx/parchmentbug/filelist-gbug.txt pkg/data/palette.dat $(PNGTORAW)

pkg/data/legal32.raw:   gfx/menufx/loading/legal-32.png pkg/data/legal32.pal $(PNGTORAW)
pkg/data/legal64.raw:   gfx/menufx/loading/legal-64.png pkg/data/legal64.pal $(PNGTORAW)
pkg/data/legal-720p-wide.raw:    gfx/menufx/loading/legal-720p-wide.png pkg/data/legal-720p-wide.pal $(PNGTORAW)
pkg/data/legal-1080p-wide.raw:   gfx/menufx/loading/legal-1080p-wide.png pkg/data/legal-1080p-wide.pal $(PNGTORAW)
pkg/data/startfx32.raw: gfx/menufx/loading/startupfx-32.png pkg/data/startfx32.pal $(PNGTORAW)
pkg/data/startfx64.raw: gfx/menufx/loading/startupfx-64.png pkg/data/startfx64.pal $(PNGTORAW)
pkg/data/loading32.raw: gfx/menufx/loading/loading-32.png pkg/data/loading32.pal $(PNGTORAW)
pkg/data/loading64.raw: gfx/menufx/loading/loading-64.png pkg/data/loading64.pal $(PNGTORAW)
pkg/data/nocd.raw:      gfx/menufx/loading/nocd-32.png pkg/data/nocd.pal $(PNGTORAW)
pkg/data/gui2-64.dat:   gfx/menufx/gui2-64/filelist_gui2.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/gui2-32.dat:   gfx/menufx/gui2-32/filelist_gui2.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/gui1-64.dat:   gfx/menufx/gui1-64/filelist_gui1.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/gui1-32.dat:   gfx/menufx/gui1-32/filelist_gui1.txt pkg/data/palette.dat $(PNGTORAW)

pkg/data/pointer64.dat: gfx/enginefx/pointer-64/filelist_pointer.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/font1-64.dat:  gfx/enginefx/font_simp-64/filelist_font1.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/font1-32.dat:  gfx/enginefx/font_simp-32/filelist_font1.txt pkg/data/palette.dat $(PNGTORAW)

pkg/data/font2-32.dat:  gfx/menufx/font2-32/filelist_font2.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/font2-64.dat:  gfx/menufx/font2-64/filelist_font2.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/creature.jty:  gfx/enginefx/sprites-32/animlist.txt pkg/data/palette.dat $(PNGTORAW)

pkg/data/swipe01.dat: gfx/enginefx/swipes-32/filelist_bhandrl.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/swipe02.dat: gfx/enginefx/swipes-32/filelist_swordrl.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/swipe03.dat: gfx/enginefx/swipes-32/filelist_scythlr.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/swipe04.dat: gfx/enginefx/swipes-32/filelist_sticklr.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/swipe05.dat: gfx/enginefx/swipes-32/filelist_stickrl.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/swipe06.dat: gfx/enginefx/swipes-32/filelist_clawsrl.txt pkg/data/palette.dat $(PNGTORAW)
pkg/data/swipe07.dat: gfx/enginefx/swipes-32/filelist_teeth.txt pkg/data/palette.dat $(PNGTORAW)

pkg/data/frac%.raw:
	-$(ECHO) 'Building RAW texture: $@'
	$(PNGTORAW) -o "$@" -p "$(word 2,$^)" -r 255 -f raw -l 100 "$<"
	-$(ECHO) 'Finished building: $@'
	-$(ECHO) ' '

pkg/data/tmap%.dat:
	-$(ECHO) 'Building RAW texture: $@'
	$(PNGTORAW) -b -o "$@" -p "$(word 2,$^)" -f raw -l 0 "$<"
	-$(ECHO) 'Finished building: $@'
	-$(ECHO) ' '

pkg/ldata/%.raw pkg/data/%.raw:
	-$(ECHO) 'Building RAW image: $@'
	$(PNGTORAW) -o "$@" -p "$(word 2,$^)" -f raw -l 100 "$<"
	-$(ECHO) 'Finished building: $@'
	-$(ECHO) ' '

pkg/ldata/%.dat pkg/data/%.dat:
	-$(ECHO) 'Building tabulated sprites: $@'
	$(MKDIR) "$(@D)"
	$(PNGTORAW) -b -o "$@" -p "$(word 2,$^)" -f sspr -l 0 "$<"
	-$(ECHO) 'Finished building: $@'
	-$(ECHO) ' '

pkg/creatrs/%.jty pkg/data/%.jty:
	-$(ECHO) 'Building jonty sprites: $@'
	@$(MKDIR) "$(@D)"
	$(PNGTORAW) -m -o "$@" -p "$(word 2,$^)" -f jspr -l 0 "$<"
	-$(ECHO) 'Finished building: $@'
	-$(ECHO) ' '

gfx/%:: | gfx/LICENSE ;

gfx/LICENSE:
	git clone --depth=1 https://github.com/dkfans/FXGraphics.git gfx

# The package is extracted only if targets does not exits; the "|" causes file dates to be ignored
# Note that ignoring timestamp means it is possible to have outadated files after a new
# package release, if no targets were modified with the update.
$(foreach campaign,$(sort $(CAMPAIGNS)), gfx/$(campaign)_lnd/%.png) \
gfx/menufx/loading/%.png gfx/palettes/%.pal \
gfx/menufx/font2-32/%.txt gfx/menufx/font2-64/%.txt gfx/menufx/font_net/%.txt \
gfx/menufx/font_front_std_dkr-64/%.txt gfx/menufx/font_front_std_red-64/%.txt gfx/menufx/font_front_std_ylw-64/%.txt \
gfx/menufx/font_front_hdr_dkr-64/%.txt gfx/menufx/font_front_hdr_red-64/%.txt gfx/menufx/font_front_hdr_ylw-64/%.txt \
gfx/menufx/frontend-64/%.txt gfx/enginefx/pointer-64/%.txt \
gfx/menufx/gui1-32/%.txt gfx/menufx/gui1-64/%.txt gfx/menufx/gui1-128/%.txt gfx/menufx/gui1-256/%.txt \
gfx/menufx/gui2-32/%.txt gfx/menufx/gui2-64/%.txt gfx/menufx/gui2-128/%.txt gfx/menufx/gui2-256/%.txt \
gfx/landviewdattabs/landview_ensign/%.txt gfx/landviewdattabs/landview_hand/%.txt gfx/landviewdattabs/landview_pinpnt/%.txt \
gfx/enginefx/font_simp-32/%.txt gfx/enginefx/font_simp-64/%.txt \
gfx/enginefx/sprites-32/%.txt gfx/enginefx/sprites-64/%.txt gfx/enginefx/sprites-128/%.txt \
gfx/enginefx/swipes-32/%.txt gfx/enginefx/swipes-64/%.txt gfx/enginefx/swipes-128/%.txt \
gfx/enginefx/textures-32/%.png gfx/enginefx/textures-64/%.png gfx/enginefx/textures-128/%.png \
gfx/enginefx/textures-32/%.txt gfx/enginefx/textures-64/%.txt gfx/enginefx/textures-128/%.txt \
gfx/menufx/torturescr/%.png gfx/menufx/torturescr/%.txt gfx/landviewdattabs/guimap/%.txt gfx/enginefx/parchmentbug/%.txt gfx/creatrportrait/%.txt: gfx

#******************************************************************************
