#============================================================================
# Copyright (C) 2013 - 2018, OpenJK contributors
#
# This file is part of the OpenJK source code.
#
# OpenJK is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
#============================================================================

# Make sure the user is not executing this script directly
if(NOT InOpenJK)
	message(FATAL_ERROR "Use the top-level cmake script!")
endif(NOT InOpenJK)

set(JK2GameDefines ${SharedDefines} "JK2_MODE" "JKX_GAME_MODULE")

# ${CodeDir} is on this list so that this tree can name what it includes from the
# engine instead of counting directory levels back to it. Thirty-seven includes
# here reached across as "../../code/qcommon/q_shared.h" or, worse,
# "../code/qcommon/..." - which resolved only because games/jk2 itself is an
# include directory, so the path went games/jk2/../code. Two spellings of the same
# thing, both of them depending on how deeply this tree happens to sit, and they
# would have broken differently the moment it moved.
set(JK2GameIncludeDirectories
	"${JK2Dir}"
	"${JK2Dir}/game"
	"${CodeDir}"
	"${SharedDir}"
	"${GSLIncludeDirectory}"
	"${CMAKE_BINARY_DIR}/shared"
	)
if(WIN32)
	set(JK2GameLibraries "winmm")
endif(WIN32)

set(JK2GameGameFiles
	"${JK2Dir}/game/AI_Atst.cpp"
	"${JK2Dir}/game/AI_Default.cpp"
	"${JK2Dir}/game/AI_Droid.cpp"
	"${JK2Dir}/game/AI_GalakMech.cpp"
	"${JK2Dir}/game/AI_Grenadier.cpp"
	"${JK2Dir}/game/AI_Howler.cpp"
	"${JK2Dir}/game/AI_ImperialProbe.cpp"
	"${JK2Dir}/game/AI_Interrogator.cpp"
	"${JK2Dir}/game/AI_Jedi.cpp"
	"${JK2Dir}/game/AI_Mark1.cpp"
	"${JK2Dir}/game/AI_Mark2.cpp"
	"${JK2Dir}/game/AI_MineMonster.cpp"
	"${JK2Dir}/game/AI_Remote.cpp"
	"${JK2Dir}/game/AI_Seeker.cpp"
	"${JK2Dir}/game/AI_Sentry.cpp"
	"${JK2Dir}/game/AI_Sniper.cpp"
	"${JK2Dir}/game/AI_Stormtrooper.cpp"
	"${JK2Dir}/game/AI_Utils.cpp"
	"${JK2Dir}/game/G_Timer.cpp"
	"${JK2Dir}/game/NPC.cpp"
	"${JK2Dir}/game/NPC_behavior.cpp"
	"${JK2Dir}/game/NPC_combat.cpp"
	"${JK2Dir}/game/NPC_goal.cpp"
	"${JK2Dir}/game/NPC_misc.cpp"
	"${JK2Dir}/game/NPC_move.cpp"
	"${JK2Dir}/game/NPC_reactions.cpp"
	"${JK2Dir}/game/NPC_senses.cpp"
	"${JK2Dir}/game/NPC_sounds.cpp"
	"${JK2Dir}/game/NPC_spawn.cpp"
	"${JK2Dir}/game/NPC_stats.cpp"
	"${JK2Dir}/game/NPC_utils.cpp"
	"${JK2Dir}/game/Q3_Interface.cpp"
	"${JK2Dir}/game/Q3_Registers.cpp"
	"${JK2Dir}/game/bg_misc.cpp"
	"${JK2Dir}/game/bg_pangles.cpp"
	"${JK2Dir}/game/bg_panimate.cpp"
	"${JK2Dir}/game/bg_pmove.cpp"
	"${JK2Dir}/game/bg_slidemove.cpp"
	"${JK2Dir}/game/g_ICARUS.cpp"
	"${JK2Dir}/game/g_active.cpp"
	"${JK2Dir}/game/g_breakable.cpp"
	"${JK2Dir}/game/g_camera.cpp"
	"${JK2Dir}/game/g_client.cpp"
	"${JK2Dir}/game/g_cmds.cpp"
	"${JK2Dir}/game/g_combat.cpp"
	"${JK2Dir}/game/g_functions.cpp"
	"${JK2Dir}/game/g_fx.cpp"
	"${JK2Dir}/game/g_inventory.cpp"
	"${JK2Dir}/game/g_itemLoad.cpp"
	"${JK2Dir}/game/g_items.cpp"
	"${JK2Dir}/game/g_main.cpp"
	"${JK2Dir}/game/g_mem.cpp"
	"${JK2Dir}/game/g_misc.cpp"
	"${JK2Dir}/game/g_misc_model.cpp"
	"${JK2Dir}/game/g_missile.cpp"
	"${JK2Dir}/game/g_mover.cpp"
	"${JK2Dir}/game/g_nav.cpp"
	"${JK2Dir}/game/g_navnew.cpp"
	"${JK2Dir}/game/g_navigator.cpp"
	"${JK2Dir}/game/g_object.cpp"
	"${JK2Dir}/game/g_objectives.cpp"
	"${JK2Dir}/game/g_ref.cpp"
	"${JK2Dir}/game/g_roff.cpp"
	"${JK2Dir}/game/g_savegame.cpp"
	"${JK2Dir}/game/g_session.cpp"
	"${JK2Dir}/game/g_spawn.cpp"
	"${JK2Dir}/game/g_svcmds.cpp"
	"${JK2Dir}/game/g_target.cpp"
	"${JK2Dir}/game/g_trigger.cpp"
	"${JK2Dir}/game/g_turret.cpp"
	"${JK2Dir}/game/g_usable.cpp"
	"${JK2Dir}/game/g_utils.cpp"
	"${JK2Dir}/game/g_weapon.cpp"
	"${JK2Dir}/game/g_weaponLoad.cpp"
	"${JK2Dir}/game/genericparser2.cpp"
	"${JK2Dir}/game/wp_atst.cpp"
	"${JK2Dir}/game/wp_blaster_rifle.cpp"
	"${JK2Dir}/game/wp_bot_laser.cpp"
	"${JK2Dir}/game/wp_bowcaster.cpp"
	"${JK2Dir}/game/wp_bryar_pistol.cpp"
	"${JK2Dir}/game/wp_demp2.cpp"
	"${JK2Dir}/game/wp_det_pack.cpp"
	"${JK2Dir}/game/wp_disruptor.cpp"
	"${JK2Dir}/game/wp_emplaced_gun.cpp"
	"${JK2Dir}/game/wp_flechette.cpp"
	"${JK2Dir}/game/wp_melee.cpp"
	"${JK2Dir}/game/wp_repeater.cpp"
	"${JK2Dir}/game/wp_rocket_launcher.cpp"
	"${JK2Dir}/game/wp_saber.cpp"
	"${JK2Dir}/game/wp_stun_baton.cpp"
	"${JK2Dir}/game/wp_thermal.cpp"
	"${JK2Dir}/game/wp_trip_mine.cpp"
	"${JK2Dir}/game/Q3_Interface.h"
	"${JK2Dir}/game/Q3_Registers.h"
	"${JK2Dir}/game/ai.h"
	"${JK2Dir}/game/anims.h"
	"${JK2Dir}/game/b_local.h"
	"${JK2Dir}/game/b_public.h"
	"${JK2Dir}/game/bg_local.h"
	"${JK2Dir}/game/bg_public.h"
	"${JK2Dir}/game/bset.h"
	"${JK2Dir}/game/bstate.h"
	"${JK2Dir}/game/dmstates.h"
	"${JK2Dir}/game/events.h"
	"${JK2Dir}/game/fields.h"
	"${JK2Dir}/game/g_functions.h"
	"${JK2Dir}/game/g_headers.h"
	"${JK2Dir}/game/g_icarus.h"
	"${JK2Dir}/game/g_items.h"
	"${JK2Dir}/game/g_local.h"
	"${JK2Dir}/game/g_nav.h"
	"${JK2Dir}/game/g_navigator.h"
	"${JK2Dir}/game/g_roff.h"
	"${JK2Dir}/game/g_shared.h"
	"${JK2Dir}/game/genericparser2.h"
	"${JK2Dir}/game/npc_headers.h"
	"${JK2Dir}/game/objectives.h"
	"${JK2Dir}/game/say.h"
	"${JK2Dir}/game/teams.h"
	"${JK2Dir}/game/weapons.h"
	"${JK2Dir}/game/w_local.h"
	"${JK2Dir}/game/wp_saber.h"
	)
source_group("game" FILES ${JK2GameGameFiles})
set(JK2GameFiles ${JK2GameFiles} ${JK2GameGameFiles})

set(JK2GameCGameFiles
	"${JK2Dir}/cgame/FX_ATSTMain.cpp"
	"${JK2Dir}/cgame/FX_Blaster.cpp"
	"${JK2Dir}/cgame/FX_Bowcaster.cpp"
	"${JK2Dir}/cgame/FX_BryarPistol.cpp"
	"${JK2Dir}/cgame/FX_DEMP2.cpp"
	"${JK2Dir}/cgame/FX_Disruptor.cpp"
	"${JK2Dir}/cgame/FX_Emplaced.cpp"
	"${JK2Dir}/cgame/FX_Flechette.cpp"
	"${JK2Dir}/cgame/FX_HeavyRepeater.cpp"
	"${JK2Dir}/cgame/FX_RocketLauncher.cpp"
	"${JK2Dir}/cgame/FxPrimitives.cpp"
	"${JK2Dir}/cgame/FxPrimitives.h"
	"${JK2Dir}/cgame/FxScheduler.cpp"
	"${JK2Dir}/cgame/FxScheduler.h"
	"${JK2Dir}/cgame/FxSystem.cpp"
	"${JK2Dir}/cgame/FxSystem.h"
	"${JK2Dir}/cgame/FxTemplate.cpp"
	"${JK2Dir}/cgame/FxUtil.cpp"
	"${JK2Dir}/cgame/FxUtil.h"
	"${JK2Dir}/cgame/animtable.h"
	"${JK2Dir}/cgame/cg_camera.cpp"
	"${JK2Dir}/cgame/cg_camera.h"
	"${JK2Dir}/cgame/cg_consolecmds.cpp"
	"${JK2Dir}/cgame/cg_credits.cpp"
	"${JK2Dir}/cgame/cg_draw.cpp"
	"${JK2Dir}/cgame/cg_drawtools.cpp"
	"${JK2Dir}/cgame/cg_effects.cpp"
	"${JK2Dir}/cgame/cg_ents.cpp"
	"${JK2Dir}/cgame/cg_event.cpp"
	"${JK2Dir}/cgame/cg_info.cpp"
	"${JK2Dir}/cgame/cg_lights.cpp"
	"${JK2Dir}/cgame/cg_local.h"
	"${JK2Dir}/cgame/cg_localents.cpp"
	"${JK2Dir}/cgame/cg_main.cpp"
	"${JK2Dir}/cgame/cg_marks.cpp"
	"${JK2Dir}/cgame/cg_media.h"
	"${JK2Dir}/cgame/cg_players.cpp"
	"${JK2Dir}/cgame/cg_playerstate.cpp"
	"${JK2Dir}/cgame/cg_predict.cpp"
	"${JK2Dir}/cgame/cg_scoreboard.cpp"
	"${JK2Dir}/cgame/cg_servercmds.cpp"
	"${JK2Dir}/cgame/cg_snapshot.cpp"
	"${JK2Dir}/cgame/cg_syscalls.cpp"
	"${JK2Dir}/cgame/cg_text.cpp"
	"${JK2Dir}/cgame/cg_view.cpp"
	"${JK2Dir}/cgame/cg_weapons.cpp"
	"${JK2Dir}/cgame/strip_objectives.h"
	)
source_group("cgame" FILES ${JK2GameCGameFiles})
set(JK2GameFiles ${JK2GameFiles} ${JK2GameCGameFiles})

set(JK2GameUIFiles
	"${CodeDir}/ui/gameinfo.cpp"
	"${CodeDir}/ui/gameinfo.h"
	)
source_group("ui" FILES ${JK2GameUIFiles})
set(JK2GameFiles ${JK2GameFiles} ${JK2GameUIFiles})

set(JK2GameCommonFiles
	#We only have JKA qcommon files now.
	"${CodeDir}/qcommon/tri_coll_test.cpp"
	"${CodeDir}/qcommon/q_shared.cpp"
	"${CodeDir}/qcommon/q_shared.h"
	"${CodeDir}/qcommon/strippublic.h"
	"${CodeDir}/qcommon/ojk_i_saved_game.h"
	"${CodeDir}/qcommon/ojk_saved_game_class_archivers.h"
	"${CodeDir}/qcommon/ojk_saved_game_helper.h"
	"${CodeDir}/qcommon/ojk_saved_game_helper_fwd.h"
	"${CodeDir}/qcommon/ojk_scope_guard.h"
	${SharedCommonFiles}
	)
source_group("common" FILES ${JK2GameCommonFiles})
set(JK2GameFiles ${JK2GameFiles} ${JK2GameCommonFiles})

set(JK2GameCommonSafeFiles
	${SharedCommonSafeFiles}
	"${SharedDir}/qcommon/safe/files.h"
	"${SharedDir}/qcommon/safe/files.cpp"
	)
source_group("common/safe" FILES ${JK2GameCommonSafeFiles})
set(JK2GameFiles ${JK2GameFiles} ${JK2GameCommonSafeFiles})

set(JK2GameIcarusFiles
	"${JK2Dir}/icarus/BlockStream.cpp"
	"${JK2Dir}/icarus/Sequence.cpp"
	"${JK2Dir}/icarus/Sequencer.cpp"
	"${JK2Dir}/icarus/TaskManager.cpp"
	"${JK2Dir}/icarus/Instance.cpp"
	"${JK2Dir}/icarus/blockstream.h"
	"${JK2Dir}/icarus/icarus.h"
	"${JK2Dir}/icarus/interface.h"
	"${JK2Dir}/icarus/interpreter.h"
	"${JK2Dir}/icarus/sequence.h"
	"${JK2Dir}/icarus/sequencer.h"
	"${JK2Dir}/icarus/taskmanager.h"
	"${JK2Dir}/icarus/instance.h"
	"${JK2Dir}/icarus/tokenizer.h"
	)
source_group("icarus" FILES ${JK2GameIcarusFiles})
set(JK2GameFiles ${JK2GameFiles} ${JK2GameIcarusFiles})

set(JK2GameRuflFiles
	"${CodeDir}/Rufl/hstring.cpp"
	"${CodeDir}/Rufl/hstring.h"
	)
source_group("rufl" FILES ${JK2GameRuflFiles})
set(JK2GameFiles ${JK2GameFiles} ${JK2GameRuflFiles})

if(WIN32)
	set(JK2GameWin32Files
		"${JK2Dir}/win32/game.rc")
	source_group("win32" FILES ${JK2GameWin32Files})
	set(JK2GameFiles ${JK2GameFiles} ${JK2GameWin32Files})
endif(WIN32)

add_library(${JK2Game} SHARED ${JK2GameFiles})
# cgame and icarus are compiled into this library, so the whole of the legacy
# gamecode is covered here. See jkx_legacy_string_literals in the root
# CMakeLists for why, and for what it would take to drop this line.
jkx_legacy_string_literals(${JK2Game})
jkx_no_undefined_symbols(${JK2Game})

if(NOT MSVC)
	# remove "lib" prefix for .so/.dylib files
	set_target_properties(${JK2Game} PROPERTIES PREFIX "")
endif()

if(WIN32)
	install(TARGETS ${JK2Game} RUNTIME DESTINATION "${JK2InstallDir}")
else(WIN32)
	if(MakeApplicationBundles AND BuildJK2Engine)
		install(TARGETS ${JK2Game} LIBRARY DESTINATION "${JK2InstallDir}/${EngineJK2}.app/Contents/MacOS")
	else(MakeApplicationBundles AND BuildJK2Engine)
		install(TARGETS ${JK2Game} LIBRARY DESTINATION "${JK2InstallDir}")
	endif(MakeApplicationBundles AND BuildJK2Engine)
endif(WIN32)

set_target_properties(${JK2Game} PROPERTIES COMPILE_DEFINITIONS "${JK2GameDefines}")

# Hide symbols not explicitly marked public.
set_property(TARGET ${JK2Game} APPEND PROPERTY COMPILE_OPTIONS ${OPENJK_VISIBILITY_FLAGS})

set_target_properties(${JK2Game} PROPERTIES INCLUDE_DIRECTORIES "${JK2GameIncludeDirectories}")
set_target_properties(${JK2Game} PROPERTIES PROJECT_LABEL "JK2 SP Game Library")
# no libraries used
if(JK2GameLibraries)
	target_link_libraries(${JK2Game} ${JK2GameLibraries})
endif(JK2GameLibraries)
