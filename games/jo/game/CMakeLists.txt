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

set(JOGameDefines ${SharedDefines} "JKX_GAME_OUTCAST" "JKX_GAME_MODULE")

# ${CodeDir} is on this list so that this tree can name what it includes from the
# engine instead of counting directory levels back to it. Thirty-seven includes
# here reached across as "../../code/qcommon/q_shared.h" or, worse,
# "../code/qcommon/..." - which resolved only because games/jo itself is an
# include directory, so the path went games/jo/../code. Two spellings of the same
# thing, both of them depending on how deeply this tree happens to sit, and they
# would have broken differently the moment it moved.
set(JOGameIncludeDirectories
	"${JODir}"
	"${JODir}/game"
	"${CodeDir}"
	"${SharedDir}"
	"${GSLIncludeDirectory}"
	"${CMAKE_BINARY_DIR}/shared"
	)
if(WIN32)
	set(JOGameLibraries "winmm")
endif(WIN32)

set(JOGameGameFiles
	"${JODir}/game/AI_Atst.cpp"
	"${JODir}/game/AI_Default.cpp"
	"${JODir}/game/AI_Droid.cpp"
	"${JODir}/game/AI_GalakMech.cpp"
	"${JODir}/game/AI_Grenadier.cpp"
	"${JODir}/game/AI_Howler.cpp"
	"${JODir}/game/AI_ImperialProbe.cpp"
	"${JODir}/game/AI_Interrogator.cpp"
	"${JODir}/game/AI_Jedi.cpp"
	"${JODir}/game/AI_Mark1.cpp"
	"${JODir}/game/AI_Mark2.cpp"
	"${JODir}/game/AI_MineMonster.cpp"
	"${JODir}/game/AI_Remote.cpp"
	"${JODir}/game/AI_Seeker.cpp"
	"${JODir}/game/AI_Sentry.cpp"
	"${JODir}/game/AI_Sniper.cpp"
	"${JODir}/game/AI_Stormtrooper.cpp"
	"${JODir}/game/AI_Utils.cpp"
	"${JODir}/game/G_Timer.cpp"
	"${JODir}/game/NPC.cpp"
	"${JODir}/game/NPC_behavior.cpp"
	"${JODir}/game/NPC_combat.cpp"
	"${JODir}/game/NPC_goal.cpp"
	"${JODir}/game/NPC_misc.cpp"
	"${JODir}/game/NPC_move.cpp"
	"${JODir}/game/NPC_reactions.cpp"
	"${JODir}/game/NPC_senses.cpp"
	"${JODir}/game/NPC_sounds.cpp"
	"${JODir}/game/NPC_spawn.cpp"
	"${JODir}/game/NPC_stats.cpp"
	"${JODir}/game/NPC_utils.cpp"
	"${JODir}/game/Q3_Interface.cpp"
	"${JODir}/game/Q3_Registers.cpp"
	"${JODir}/game/bg_misc.cpp"
	"${JODir}/game/bg_pangles.cpp"
	"${JODir}/game/bg_panimate.cpp"
	"${JODir}/game/bg_pmove.cpp"
	"${JODir}/game/bg_slidemove.cpp"
	"${JODir}/game/g_ICARUS.cpp"
	"${JODir}/game/g_active.cpp"
	"${JODir}/game/g_breakable.cpp"
	"${JODir}/game/g_camera.cpp"
	"${JODir}/game/g_client.cpp"
	"${JODir}/game/g_cmds.cpp"
	"${JODir}/game/g_combat.cpp"
	"${JODir}/game/g_functions.cpp"
	"${JODir}/game/g_fx.cpp"
	"${JODir}/game/g_inventory.cpp"
	"${JODir}/game/g_itemLoad.cpp"
	"${JODir}/game/g_items.cpp"
	"${JODir}/game/g_main.cpp"
	"${JODir}/game/g_mem.cpp"
	"${JODir}/game/g_misc.cpp"
	"${JODir}/game/g_misc_model.cpp"
	"${JODir}/game/g_missile.cpp"
	"${JODir}/game/g_mover.cpp"
	"${JODir}/game/g_nav.cpp"
	"${JODir}/game/g_navnew.cpp"
	"${JODir}/game/g_navigator.cpp"
	"${JODir}/game/g_object.cpp"
	"${JODir}/game/g_objectives.cpp"
	"${JODir}/game/g_ref.cpp"
	"${JODir}/game/g_roff.cpp"
	"${JODir}/game/g_savegame.cpp"
	"${JODir}/game/g_session.cpp"
	"${JODir}/game/g_spawn.cpp"
	"${JODir}/game/g_svcmds.cpp"
	"${JODir}/game/g_target.cpp"
	"${JODir}/game/g_trigger.cpp"
	"${JODir}/game/g_turret.cpp"
	"${JODir}/game/g_usable.cpp"
	"${JODir}/game/g_utils.cpp"
	"${JODir}/game/g_weapon.cpp"
	"${JODir}/game/g_weaponLoad.cpp"
	"${JODir}/game/genericparser2.cpp"
	"${JODir}/game/wp_atst.cpp"
	"${JODir}/game/wp_blaster_rifle.cpp"
	"${JODir}/game/wp_bot_laser.cpp"
	"${JODir}/game/wp_bowcaster.cpp"
	"${JODir}/game/wp_bryar_pistol.cpp"
	"${JODir}/game/wp_demp2.cpp"
	"${JODir}/game/wp_det_pack.cpp"
	"${JODir}/game/wp_disruptor.cpp"
	"${JODir}/game/wp_emplaced_gun.cpp"
	"${JODir}/game/wp_flechette.cpp"
	"${JODir}/game/wp_melee.cpp"
	"${JODir}/game/wp_repeater.cpp"
	"${JODir}/game/wp_rocket_launcher.cpp"
	"${JODir}/game/wp_saber.cpp"
	"${JODir}/game/wp_stun_baton.cpp"
	"${JODir}/game/wp_thermal.cpp"
	"${JODir}/game/wp_trip_mine.cpp"
	"${JODir}/game/Q3_Interface.h"
	"${JODir}/game/Q3_Registers.h"
	"${JODir}/game/ai.h"
	"${JODir}/game/anims.h"
	"${JODir}/game/b_local.h"
	"${JODir}/game/b_public.h"
	"${JODir}/game/bg_local.h"
	"${JODir}/game/bg_public.h"
	"${JODir}/game/bset.h"
	"${JODir}/game/bstate.h"
	"${JODir}/game/dmstates.h"
	"${JODir}/game/events.h"
	"${JODir}/game/fields.h"
	"${JODir}/game/g_functions.h"
	"${JODir}/game/g_headers.h"
	"${JODir}/game/g_icarus.h"
	"${JODir}/game/g_items.h"
	"${JODir}/game/g_local.h"
	"${JODir}/game/g_nav.h"
	"${JODir}/game/g_navigator.h"
	"${JODir}/game/g_roff.h"
	"${JODir}/game/g_shared.h"
	"${JODir}/game/genericparser2.h"
	"${JODir}/game/npc_headers.h"
	"${JODir}/game/objectives.h"
	"${JODir}/game/say.h"
	"${JODir}/game/teams.h"
	"${JODir}/game/weapons.h"
	"${JODir}/game/w_local.h"
	"${JODir}/game/wp_saber.h"
	)
source_group("game" FILES ${JOGameGameFiles})
set(JOGameFiles ${JOGameFiles} ${JOGameGameFiles})

set(JOGameCGameFiles
	"${JODir}/cgame/FX_ATSTMain.cpp"
	"${JODir}/cgame/FX_Blaster.cpp"
	"${JODir}/cgame/FX_Bowcaster.cpp"
	"${JODir}/cgame/FX_BryarPistol.cpp"
	"${JODir}/cgame/FX_DEMP2.cpp"
	"${JODir}/cgame/FX_Disruptor.cpp"
	"${JODir}/cgame/FX_Emplaced.cpp"
	"${JODir}/cgame/FX_Flechette.cpp"
	"${JODir}/cgame/FX_HeavyRepeater.cpp"
	"${JODir}/cgame/FX_RocketLauncher.cpp"
	"${JODir}/cgame/FxPrimitives.cpp"
	"${JODir}/cgame/FxPrimitives.h"
	"${JODir}/cgame/FxScheduler.cpp"
	"${JODir}/cgame/FxScheduler.h"
	"${JODir}/cgame/FxSystem.cpp"
	"${JODir}/cgame/FxSystem.h"
	"${JODir}/cgame/FxTemplate.cpp"
	"${JODir}/cgame/FxUtil.cpp"
	"${JODir}/cgame/FxUtil.h"
	"${JODir}/cgame/animtable.cpp"
	"${JODir}/cgame/cg_camera.cpp"
	"${JODir}/cgame/cg_camera.h"
	"${JODir}/cgame/cg_consolecmds.cpp"
	"${JODir}/cgame/cg_credits.cpp"
	"${JODir}/cgame/cg_draw.cpp"
	"${JODir}/cgame/cg_drawtools.cpp"
	"${JODir}/cgame/cg_effects.cpp"
	"${JODir}/cgame/cg_ents.cpp"
	"${JODir}/cgame/cg_event.cpp"
	"${JODir}/cgame/cg_info.cpp"
	"${JODir}/cgame/cg_lights.cpp"
	"${JODir}/cgame/cg_local.h"
	"${JODir}/cgame/cg_localents.cpp"
	"${JODir}/cgame/cg_main.cpp"
	"${JODir}/cgame/cg_marks.cpp"
	"${JODir}/cgame/cg_media.h"
	"${JODir}/cgame/cg_players.cpp"
	"${JODir}/cgame/cg_playerstate.cpp"
	"${JODir}/cgame/cg_predict.cpp"
	"${JODir}/cgame/cg_scoreboard.cpp"
	"${JODir}/cgame/cg_servercmds.cpp"
	"${JODir}/cgame/cg_snapshot.cpp"
	"${JODir}/cgame/cg_syscalls.cpp"
	"${JODir}/cgame/cg_text.cpp"
	"${JODir}/cgame/cg_view.cpp"
	"${JODir}/cgame/cg_weapons.cpp"
	"${JODir}/cgame/strip_objectives.h"
	)
source_group("cgame" FILES ${JOGameCGameFiles})
set(JOGameFiles ${JOGameFiles} ${JOGameCGameFiles})

set(JOGameUIFiles
	)
source_group("ui" FILES ${JOGameUIFiles})
set(JOGameFiles ${JOGameFiles} ${JOGameUIFiles})

set(JOGameCommonFiles
	#We only have JKA qcommon files now.
	"${CodeDir}/qcommon/tri_coll_test.cpp"
	"${CodeDir}/qcommon/q_shared.cpp"
	"${CodeDir}/qcommon/q_shared.h"
	"${CodeDir}/qcommon/strippublic.h"
	"${CodeDir}/qcommon/jkx_i_saved_game.h"
	"${CodeDir}/qcommon/jkx_saved_game_class_archivers.h"
	"${CodeDir}/qcommon/jkx_saved_game_helper.h"
	"${CodeDir}/qcommon/jkx_saved_game_helper_fwd.h"
	"${CodeDir}/qcommon/jkx_scope_guard.h"
	${SharedCommonFiles}
	)
source_group("common" FILES ${JOGameCommonFiles})
set(JOGameFiles ${JOGameFiles} ${JOGameCommonFiles})

set(JOGameCommonSafeFiles
	${SharedCommonSafeFiles}
	"${SharedDir}/qcommon/safe/files.h"
	"${SharedDir}/qcommon/safe/files.cpp"
	)
source_group("common/safe" FILES ${JOGameCommonSafeFiles})
set(JOGameFiles ${JOGameFiles} ${JOGameCommonSafeFiles})

set(JOGameIcarusFiles
	"${JODir}/icarus/BlockStream.cpp"
	"${JODir}/icarus/Sequence.cpp"
	"${JODir}/icarus/Sequencer.cpp"
	"${JODir}/icarus/TaskManager.cpp"
	"${JODir}/icarus/Instance.cpp"
	"${JODir}/icarus/blockstream.h"
	"${JODir}/icarus/icarus.h"
	"${JODir}/icarus/interface.h"
	"${JODir}/icarus/interpreter.h"
	"${JODir}/icarus/sequence.h"
	"${JODir}/icarus/sequencer.h"
	"${JODir}/icarus/taskmanager.h"
	"${JODir}/icarus/instance.h"
	"${JODir}/icarus/tokenizer.h"
	)
source_group("icarus" FILES ${JOGameIcarusFiles})
set(JOGameFiles ${JOGameFiles} ${JOGameIcarusFiles})

set(JOGameRuflFiles
	"${CodeDir}/Rufl/hstring.cpp"
	"${CodeDir}/Rufl/hstring.h"
	)
source_group("rufl" FILES ${JOGameRuflFiles})
set(JOGameFiles ${JOGameFiles} ${JOGameRuflFiles})

if(WIN32)
	set(JOGameWin32Files
		"${SharedDir}/win32/game.rc")
	source_group("win32" FILES ${JOGameWin32Files})
	set(JOGameFiles ${JOGameFiles} ${JOGameWin32Files})
endif(WIN32)

add_library(${JOGame} SHARED ${JOGameFiles})
# cgame and icarus are compiled into this library, so the whole of the legacy
# gamecode is covered here. See jkx_legacy_string_literals in the root
# CMakeLists for why, and for what it would take to drop this line.
jkx_legacy_string_literals(${JOGame})
jkx_no_undefined_symbols(${JOGame})

if(NOT MSVC)
	# remove "lib" prefix for .so/.dylib files
	set_target_properties(${JOGame} PROPERTIES PREFIX "")
endif()

if(WIN32)
	install(TARGETS ${JOGame} RUNTIME DESTINATION "${JOInstallDir}")
else(WIN32)
	if(MakeApplicationBundles AND BuildEngine)
		install(TARGETS ${JOGame} LIBRARY DESTINATION "${JOInstallDir}/${Engine}.app/Contents/MacOS")
	else(MakeApplicationBundles AND BuildEngine)
		install(TARGETS ${JOGame} LIBRARY DESTINATION "${JOInstallDir}")
	endif(MakeApplicationBundles AND BuildEngine)
endif(WIN32)

set_target_properties(${JOGame} PROPERTIES COMPILE_DEFINITIONS "${JOGameDefines}")

# Hide symbols not explicitly marked public.
set_property(TARGET ${JOGame} APPEND PROPERTY COMPILE_OPTIONS ${OPENJK_VISIBILITY_FLAGS})

set_target_properties(${JOGame} PROPERTIES INCLUDE_DIRECTORIES "${JOGameIncludeDirectories}")
set_target_properties(${JOGame} PROPERTIES PROJECT_LABEL "Jedi Outcast Game Library")
# no libraries used
if(JOGameLibraries)
	target_link_libraries(${JOGame} ${JOGameLibraries})
endif(JOGameLibraries)
