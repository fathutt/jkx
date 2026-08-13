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

# Defined when a game library is being compiled, as opposed to the engine. It
# was called SP_GAME, which named the wrong thing: SP in this tree means three
# different things, and only one of them is single-player. The other two are
# spawn functions - SP_worldspawn, SP_trigger_push, two hundred of them - and
# the STRIP string package, SP_GetStringText and the .sp files on disk. This
# one was the odd one out, and the distinction it draws is engine or game
# module, which has nothing to do with either.
#
# Its counterpart is JKX_ENGINE; see the note in code/CMakeLists.txt.
set(SPGameDefines ${SharedDefines} "JKX_GAME_MODULE")
# ${JKASPDir} sits ahead of ${SPDir} here, the same way ${JK2SPDir} does in the
# other game's list. It is what makes "game/g_shared.h" and "cgame/cg_local.h"
# resolve into this tree: they used to resolve through ${SPDir} because this
# gamecode was inside code/, and shared/qcommon/safe/files.cpp still writes the
# first of those - it is compiled into both game libraries and has to reach
# whichever gamecode it is being built into.
set(SPGameIncludeDirectories
	${SharedDir}
	"${JKASPDir}"
	${SPDir}
	"${JKASPDir}/game"
	"${GSLIncludeDirectory}"
	"${CMAKE_BINARY_DIR}/shared"
	)

if(WIN32)
	set(SPGameLibraries "winmm")
endif(WIN32)

set(SPGameGameFiles
	"${JKASPDir}/game/AI_Animal.cpp"
	"${JKASPDir}/game/AI_AssassinDroid.cpp"
	"${JKASPDir}/game/AI_Atst.cpp"
	"${JKASPDir}/game/AI_BobaFett.cpp"
	"${JKASPDir}/game/AI_Civilian.cpp"
	"${JKASPDir}/game/AI_Default.cpp"
	"${JKASPDir}/game/AI_Droid.cpp"
	"${JKASPDir}/game/AI_GalakMech.cpp"
	"${JKASPDir}/game/AI_Grenadier.cpp"
	"${JKASPDir}/game/AI_HazardTrooper.cpp"
	"${JKASPDir}/game/AI_Howler.cpp"
	"${JKASPDir}/game/AI_ImperialProbe.cpp"
	"${JKASPDir}/game/AI_Interrogator.cpp"
	"${JKASPDir}/game/AI_Jedi.cpp"
	"${JKASPDir}/game/AI_Mark1.cpp"
	"${JKASPDir}/game/AI_Mark2.cpp"
	"${JKASPDir}/game/AI_MineMonster.cpp"
	"${JKASPDir}/game/AI_Rancor.cpp"
	"${JKASPDir}/game/AI_Remote.cpp"
	"${JKASPDir}/game/AI_RocketTrooper.cpp"
	"${JKASPDir}/game/AI_SaberDroid.cpp"
	"${JKASPDir}/game/AI_SandCreature.cpp"
	"${JKASPDir}/game/AI_Seeker.cpp"
	"${JKASPDir}/game/AI_Sentry.cpp"
	"${JKASPDir}/game/AI_Sniper.cpp"
	"${JKASPDir}/game/AI_Stormtrooper.cpp"
	"${JKASPDir}/game/AI_Tusken.cpp"
	"${JKASPDir}/game/AI_Utils.cpp"
	"${JKASPDir}/game/AI_Wampa.cpp"
	"${JKASPDir}/game/g_active.cpp"
	"${JKASPDir}/game/g_breakable.cpp"
	"${JKASPDir}/game/g_camera.cpp"
	"${JKASPDir}/game/g_client.cpp"
	"${JKASPDir}/game/g_cmds.cpp"
	"${JKASPDir}/game/g_combat.cpp"
	"${JKASPDir}/game/g_emplaced.cpp"
	"${JKASPDir}/game/g_functions.cpp"
	"${JKASPDir}/game/g_fx.cpp"
	"${JKASPDir}/game/g_inventory.cpp"
	"${JKASPDir}/game/g_itemLoad.cpp"
	"${JKASPDir}/game/g_items.cpp"
	"${JKASPDir}/game/g_main.cpp"
	"${JKASPDir}/game/g_mem.cpp"
	"${JKASPDir}/game/g_misc.cpp"
	"${JKASPDir}/game/g_misc_model.cpp"
	"${JKASPDir}/game/g_missile.cpp"
	"${JKASPDir}/game/g_mover.cpp"
	"${JKASPDir}/game/g_nav.cpp"
	"${JKASPDir}/game/g_navigator.cpp"
	"${JKASPDir}/game/g_navnew.cpp"
	"${JKASPDir}/game/g_object.cpp"
	"${JKASPDir}/game/g_objectives.cpp"
	"${JKASPDir}/game/g_rail.cpp"
	"${JKASPDir}/game/g_ref.cpp"
	"${JKASPDir}/game/g_roff.cpp"
	"${JKASPDir}/game/g_savegame.cpp"
	"${JKASPDir}/game/g_session.cpp"
	"${JKASPDir}/game/g_spawn.cpp"
	"${JKASPDir}/game/g_svcmds.cpp"
	"${JKASPDir}/game/g_target.cpp"
	"${JKASPDir}/game/G_Timer.cpp"
	"${JKASPDir}/game/g_trigger.cpp"
	"${JKASPDir}/game/g_turret.cpp"
	"${JKASPDir}/game/g_usable.cpp"
	"${JKASPDir}/game/g_utils.cpp"
	"${JKASPDir}/game/g_weapon.cpp"
	"${JKASPDir}/game/g_weaponLoad.cpp"
	"${SPDir}/qcommon/GenericParser2.cpp"
	"${JKASPDir}/game/NPC.cpp"
	"${JKASPDir}/game/NPC_behavior.cpp"
	"${JKASPDir}/game/NPC_combat.cpp"
	"${JKASPDir}/game/NPC_goal.cpp"
	"${JKASPDir}/game/NPC_misc.cpp"
	"${JKASPDir}/game/NPC_move.cpp"
	"${JKASPDir}/game/NPC_reactions.cpp"
	"${JKASPDir}/game/NPC_senses.cpp"
	"${JKASPDir}/game/NPC_sounds.cpp"
	"${JKASPDir}/game/NPC_spawn.cpp"
	"${JKASPDir}/game/NPC_stats.cpp"
	"${JKASPDir}/game/NPC_utils.cpp"
	"${JKASPDir}/game/Q3_Interface.h"
	"${JKASPDir}/game/wp_atst.cpp"
	"${JKASPDir}/game/wp_blaster_pistol.cpp"
	"${JKASPDir}/game/wp_blaster_rifle.cpp"
	"${JKASPDir}/game/wp_bot_laser.cpp"
	"${JKASPDir}/game/wp_bowcaster.cpp"
	"${JKASPDir}/game/wp_concussion.cpp"
	"${JKASPDir}/game/wp_demp2.cpp"
	"${JKASPDir}/game/wp_det_pack.cpp"
	"${JKASPDir}/game/wp_disruptor.cpp"
	"${JKASPDir}/game/wp_emplaced_gun.cpp"
	"${JKASPDir}/game/wp_flechette.cpp"
	"${JKASPDir}/game/wp_melee.cpp"
	"${JKASPDir}/game/wp_noghri_stick.cpp"
	"${JKASPDir}/game/wp_repeater.cpp"
	"${JKASPDir}/game/wp_rocket_launcher.cpp"
	"${JKASPDir}/game/wp_saber.cpp"
	"${JKASPDir}/game/wp_saberLoad.cpp"
	"${JKASPDir}/game/wp_stun_baton.cpp"
	"${JKASPDir}/game/wp_thermal.cpp"
	"${JKASPDir}/game/wp_trip_mine.cpp"
	"${JKASPDir}/game/wp_tusken.cpp"
	"${JKASPDir}/game/Q3_Interface.cpp"
	"${JKASPDir}/game/bg_misc.cpp"
	"${JKASPDir}/game/bg_pangles.cpp"
	"${JKASPDir}/game/bg_panimate.cpp"
	"${JKASPDir}/game/bg_pmove.cpp"
	"${JKASPDir}/game/bg_slidemove.cpp"
	"${JKASPDir}/game/AnimalNPC.cpp"
	"${JKASPDir}/game/bg_vehicleLoad.cpp"
	"${JKASPDir}/game/FighterNPC.cpp"
	"${JKASPDir}/game/g_vehicles.cpp"
	"${JKASPDir}/game/SpeederNPC.cpp"
	"${JKASPDir}/game/WalkerNPC.cpp"
	"${JKASPDir}/game/ai.h"
	"${JKASPDir}/game/anims.h"
	"${JKASPDir}/game/b_local.h"
	"${JKASPDir}/game/b_public.h"
	"${JKASPDir}/game/bg_local.h"
	"${JKASPDir}/game/bg_public.h"
	"${JKASPDir}/game/bset.h"
	"${JKASPDir}/game/bstate.h"
	"${JKASPDir}/game/common_headers.h"
	"${JKASPDir}/game/dmstates.h"
	"${JKASPDir}/game/events.h"
	"${JKASPDir}/game/fields.h"
	"${JKASPDir}/game/g_functions.h"
	"${JKASPDir}/game/g_items.h"
	"${JKASPDir}/game/g_local.h"
	"${JKASPDir}/game/g_nav.h"
	"${JKASPDir}/game/g_navigator.h"
	"${JKASPDir}/game/g_roff.h"
	"${JKASPDir}/game/g_shared.h"
	"${SPDir}/qcommon/GenericParser2.h"
	"${JKASPDir}/game/hitlocs.h"
	"${JKASPDir}/game/npc_headers.h"
	"${JKASPDir}/game/objectives.h"
	"${JKASPDir}/game/say.h"
	"${JKASPDir}/game/teams.h"
	"${JKASPDir}/game/weapons.h"
	"${JKASPDir}/game/w_local.h"
	"${JKASPDir}/game/wp_saber.h"
	"${JKASPDir}/game/g_vehicles.h"
	)
source_group("game" FILES ${SPGameGameFiles})
set(SPGameFiles ${SPGameFiles} ${SPGameGameFiles})

set(SPGameCGameFiles
	"${JKASPDir}/cgame/cg_camera.cpp"
	"${JKASPDir}/cgame/cg_consolecmds.cpp"
	"${JKASPDir}/cgame/cg_credits.cpp"
	"${JKASPDir}/cgame/cg_draw.cpp"
	"${JKASPDir}/cgame/cg_drawtools.cpp"
	"${JKASPDir}/cgame/cg_effects.cpp"
	"${JKASPDir}/cgame/cg_ents.cpp"
	"${JKASPDir}/cgame/cg_event.cpp"
	"${JKASPDir}/cgame/cg_headers.cpp"
	"${JKASPDir}/cgame/cg_info.cpp"
	"${JKASPDir}/cgame/cg_lights.cpp"
	"${JKASPDir}/cgame/cg_localents.cpp"
	"${JKASPDir}/cgame/cg_main.cpp"
	"${JKASPDir}/cgame/cg_marks.cpp"
	"${JKASPDir}/cgame/cg_players.cpp"
	"${JKASPDir}/cgame/cg_playerstate.cpp"
	"${JKASPDir}/cgame/cg_predict.cpp"
	"${JKASPDir}/cgame/cg_scoreboard.cpp"
	"${JKASPDir}/cgame/cg_servercmds.cpp"
	"${JKASPDir}/cgame/cg_snapshot.cpp"
	"${JKASPDir}/cgame/cg_syscalls.cpp"
	"${JKASPDir}/cgame/cg_text.cpp"
	"${JKASPDir}/cgame/cg_view.cpp"
	"${JKASPDir}/cgame/cg_weapons.cpp"
	"${JKASPDir}/cgame/FX_ATSTMain.cpp"
	"${JKASPDir}/cgame/FX_Blaster.cpp"
	"${JKASPDir}/cgame/FX_Bowcaster.cpp"
	"${JKASPDir}/cgame/FX_BryarPistol.cpp"
	"${JKASPDir}/cgame/FX_Concussion.cpp"
	"${JKASPDir}/cgame/FX_DEMP2.cpp"
	"${JKASPDir}/cgame/FX_Disruptor.cpp"
	"${JKASPDir}/cgame/FX_Emplaced.cpp"
	"${JKASPDir}/cgame/FX_Flechette.cpp"
	"${JKASPDir}/cgame/FX_HeavyRepeater.cpp"
	"${JKASPDir}/cgame/FX_NoghriShot.cpp"
	"${JKASPDir}/cgame/FX_RocketLauncher.cpp"
	"${JKASPDir}/cgame/FX_TuskenShot.cpp"
	"${JKASPDir}/cgame/FxPrimitives.cpp"
	"${JKASPDir}/cgame/FxScheduler.cpp"
	"${JKASPDir}/cgame/FxSystem.cpp"
	"${JKASPDir}/cgame/FxTemplate.cpp"
	"${JKASPDir}/cgame/FxUtil.cpp"
	"${JKASPDir}/cgame/cg_camera.h"
	"${JKASPDir}/cgame/animtable.h"
	"${JKASPDir}/cgame/cg_headers.h"
	"${JKASPDir}/cgame/cg_local.h"
	"${JKASPDir}/cgame/cg_media.h"
	"${JKASPDir}/cgame/common_headers.h"
	"${JKASPDir}/cgame/FxPrimitives.h"
	"${JKASPDir}/cgame/FxScheduler.h"
	"${JKASPDir}/cgame/FxSystem.h"
	"${JKASPDir}/cgame/FxUtil.h"
	"${JKASPDir}/cgame/strip_objectives.h"
	)
source_group("cgame" FILES ${SPGameCGameFiles})
set(SPGameFiles ${SPGameFiles} ${SPGameCGameFiles})

set(SPGameUIFiles
	"${SPDir}/ui/gameinfo.cpp"
	"${SPDir}/ui/gameinfo.h"
	)
source_group("ui" FILES ${SPGameUIFiles})
set(SPGameFiles ${SPGameFiles} ${SPGameUIFiles})

set(SPGameCommonFiles
	"${SPDir}/qcommon/tri_coll_test.cpp"
	"${SPDir}/qcommon/sstring.h"
	"${SPDir}/qcommon/tags.h"
	"${SPDir}/qcommon/timing.h"
	"${SPDir}/qcommon/q_shared.cpp"
	"${SPDir}/qcommon/q_shared.h"
	"${SPDir}/qcommon/ojk_i_saved_game.h"
	"${SPDir}/qcommon/ojk_saved_game_class_archivers.h"
	"${SPDir}/qcommon/ojk_saved_game_helper.h"
	"${SPDir}/qcommon/ojk_saved_game_helper_fwd.h"
	"${SPDir}/qcommon/ojk_scope_guard.h"
	${SharedCommonFiles}
	)
source_group("common" FILES ${SPGameCommonFiles})
set(SPGameFiles ${SPGameFiles} ${SPGameCommonFiles})

set(SPGameCommonSafeFiles
	${SharedCommonSafeFiles}
	"${SharedDir}/qcommon/safe/files.h"
	"${SharedDir}/qcommon/safe/files.cpp"
	"${SPDir}/qcommon/safe/memory.h"
	)
source_group("common/safe" FILES ${SPGameCommonSafeFiles})
set(SPGameFiles ${SPGameFiles} ${SPGameCommonSafeFiles})

set(SPGameIcarusFiles
	"${SPDir}/icarus/BlockStream.cpp"
	"${SPDir}/icarus/IcarusImplementation.cpp"
	"${SPDir}/icarus/Sequence.cpp"
	"${SPDir}/icarus/Sequencer.cpp"
	"${SPDir}/icarus/TaskManager.cpp"
	"${SPDir}/icarus/blockstream.h"
	"${SPDir}/icarus/IcarusImplementation.h"
	"${SPDir}/icarus/IcarusInterface.h"
	"${SPDir}/icarus/sequence.h"
	"${SPDir}/icarus/sequencer.h"
	"${SPDir}/icarus/StdAfx.h"
	"${SPDir}/icarus/taskmanager.h"
	)
source_group("icarus" FILES ${SPGameIcarusFiles})
set(SPGameFiles ${SPGameFiles} ${SPGameIcarusFiles})

set(SPGameRatlFiles
	"${SPDir}/Ratl/ratl.cpp"
	"${SPDir}/Ratl/array_vs.h"
	"${SPDir}/Ratl/bits_vs.h"
	"${SPDir}/Ratl/grid_vs.h"
	"${SPDir}/Ratl/handle_pool_vs.h"
	"${SPDir}/Ratl/hash_pool_vs.h"
	"${SPDir}/Ratl/heap_vs.h"
	"${SPDir}/Ratl/list_vs.h"
	"${SPDir}/Ratl/map_vs.h"
	"${SPDir}/Ratl/pool_vs.h"
	"${SPDir}/Ratl/queue_vs.h"
	"${SPDir}/Ratl/ratl_common.h"
	"${SPDir}/Ratl/scheduler_vs.h"
	"${SPDir}/Ratl/stack_vs.h"
	"${SPDir}/Ratl/string_vs.h"
	"${SPDir}/Ratl/vector_vs.h"
	)
source_group("ratl" FILES ${SPGameRatlFiles})
set(SPGameFiles ${SPGameFiles} ${SPGameRatlFiles})

set(SPGameRavlFiles
	"${SPDir}/Ravl/CBounds.cpp"
	"${SPDir}/Ravl/CVec.cpp"
	"${SPDir}/Ravl/CBounds.h"
	"${SPDir}/Ravl/CMatrix.h"
	"${SPDir}/Ravl/CVec.h"
	)
source_group("ravl" FILES ${SPGameRavlFiles})
set(SPGameFiles ${SPGameFiles} ${SPGameRavlFiles})

set(SPGameRuflFiles
	"${SPDir}/Rufl/hfile.cpp"
	"${SPDir}/Rufl/hstring.cpp"
	"${SPDir}/Rufl/hfile.h"
	"${SPDir}/Rufl/hstring.h"
	)
source_group("rufl" FILES ${SPGameRuflFiles})
set(SPGameFiles ${SPGameFiles} ${SPGameRuflFiles})

set(SPGameRaglFiles
	"${SPDir}/Ragl/graph_region.h"
	"${SPDir}/Ragl/graph_triangulate.h"
	"${SPDir}/Ragl/graph_vs.h"
	"${SPDir}/Ragl/kdtree_vs.h"
	"${SPDir}/Ragl/ragl_common.h"
	)
source_group("ragl" FILES ${SPGameRaglFiles})
set(SPGameFiles ${SPGameFiles} ${SPGameRaglFiles})

set(SPGameG2Files
	"${SPDir}/ghoul2/G2.h"
	"${SPDir}/ghoul2/ghoul2_gore.h"
	)
source_group("ghoul2" FILES ${SPGameG2Files})
set(SPGameFiles ${SPGameFiles} ${SPGameG2Files})

set(SPGameRendererFiles
	"${SPDir}/rd-common/mdx_format.h"
	"${SPDir}/rd-common/tr_types.h"
	)
source_group("renderer" FILES ${SPGameRendererFiles})
set(SPGameFiles ${SPGameFiles} ${SPGameRendererFiles})

set(SPGameClientFiles
	"${SPDir}/client/vmachine.h"
	)
source_group("client" FILES ${SPGameClientFiles})
set(SPGameFiles ${SPGameFiles} ${SPGameClientFiles})

if(WIN32)
	set(SPGameWin32Files
		"${SPDir}/win32/game.rc")
	source_group("win32" FILES ${SPGameWin32Files})
	set(SPGameFiles ${SPGameFiles} ${SPGameWin32Files})
endif(WIN32)

add_library(${SPGame} SHARED ${SPGameFiles})
# cgame and icarus are compiled into this library, so the whole of the legacy
# gamecode is covered here. See jkx_legacy_string_literals in the root
# CMakeLists for why, and for what it would take to drop this line.
jkx_legacy_string_literals(${SPGame})
jkx_no_undefined_symbols(${SPGame})

# Hide symbols not explicitly marked public.
set_property(TARGET ${SPGame} APPEND PROPERTY COMPILE_OPTIONS ${OPENJK_VISIBILITY_FLAGS})

if(NOT MSVC)
	# remove "lib" prefix for .so/.dylib files
	set_target_properties(${SPGame} PROPERTIES PREFIX "")
endif()

if(WIN32)
	install(TARGETS ${SPGame}
		RUNTIME
		DESTINATION "${JKAInstallDir}"
		COMPONENT ${JKASPClientComponent})
else(WIN32)
	if(MakeApplicationBundles AND BuildSPEngine)
		install(TARGETS ${SPGame}
			LIBRARY
			DESTINATION "${JKAInstallDir}/${SPEngine}.app/Contents/MacOS"
			COMPONENT ${JKASPClientComponent})
	else()
		install(TARGETS ${SPGame}
			LIBRARY
			DESTINATION "${JKAInstallDir}"
			COMPONENT ${JKASPClientComponent})
	endif()
endif()

set_target_properties(${SPGame} PROPERTIES COMPILE_DEFINITIONS "${SPGameDefines}")
set_target_properties(${SPGame} PROPERTIES INCLUDE_DIRECTORIES "${SPGameIncludeDirectories}")
set_target_properties(${SPGame} PROPERTIES PROJECT_LABEL "SP Game Library")
# no libraries used
if(SPGameLibraries)
	target_link_libraries(${SPGame} ${SPGameLibraries})
endif(SPGameLibraries)
