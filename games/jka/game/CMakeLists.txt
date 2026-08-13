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
set(JKAGameDefines ${SharedDefines} "JKX_GAME_MODULE")
# ${JKADir} sits ahead of ${CodeDir} here, the same way ${JK2Dir} does in the
# other game's list. It is what makes "game/g_shared.h" and "cgame/cg_local.h"
# resolve into this tree: they used to resolve through ${CodeDir} because this
# gamecode was inside code/, and shared/qcommon/safe/files.cpp still writes the
# first of those - it is compiled into both game libraries and has to reach
# whichever gamecode it is being built into.
set(JKAGameIncludeDirectories
	${SharedDir}
	"${JKADir}"
	${CodeDir}
	"${JKADir}/game"
	"${GSLIncludeDirectory}"
	"${CMAKE_BINARY_DIR}/shared"
	)

if(WIN32)
	set(JKAGameLibraries "winmm")
endif(WIN32)

set(JKAGameGameFiles
	"${JKADir}/game/AI_Animal.cpp"
	"${JKADir}/game/AI_AssassinDroid.cpp"
	"${JKADir}/game/AI_Atst.cpp"
	"${JKADir}/game/AI_BobaFett.cpp"
	"${JKADir}/game/AI_Civilian.cpp"
	"${JKADir}/game/AI_Default.cpp"
	"${JKADir}/game/AI_Droid.cpp"
	"${JKADir}/game/AI_GalakMech.cpp"
	"${JKADir}/game/AI_Grenadier.cpp"
	"${JKADir}/game/AI_HazardTrooper.cpp"
	"${JKADir}/game/AI_Howler.cpp"
	"${JKADir}/game/AI_ImperialProbe.cpp"
	"${JKADir}/game/AI_Interrogator.cpp"
	"${JKADir}/game/AI_Jedi.cpp"
	"${JKADir}/game/AI_Mark1.cpp"
	"${JKADir}/game/AI_Mark2.cpp"
	"${JKADir}/game/AI_MineMonster.cpp"
	"${JKADir}/game/AI_Rancor.cpp"
	"${JKADir}/game/AI_Remote.cpp"
	"${JKADir}/game/AI_RocketTrooper.cpp"
	"${JKADir}/game/AI_SaberDroid.cpp"
	"${JKADir}/game/AI_SandCreature.cpp"
	"${JKADir}/game/AI_Seeker.cpp"
	"${JKADir}/game/AI_Sentry.cpp"
	"${JKADir}/game/AI_Sniper.cpp"
	"${JKADir}/game/AI_Stormtrooper.cpp"
	"${JKADir}/game/AI_Tusken.cpp"
	"${JKADir}/game/AI_Utils.cpp"
	"${JKADir}/game/AI_Wampa.cpp"
	"${JKADir}/game/g_active.cpp"
	"${JKADir}/game/g_breakable.cpp"
	"${JKADir}/game/g_camera.cpp"
	"${JKADir}/game/g_client.cpp"
	"${JKADir}/game/g_cmds.cpp"
	"${JKADir}/game/g_combat.cpp"
	"${JKADir}/game/g_emplaced.cpp"
	"${JKADir}/game/g_functions.cpp"
	"${JKADir}/game/g_fx.cpp"
	"${JKADir}/game/g_inventory.cpp"
	"${JKADir}/game/g_itemLoad.cpp"
	"${JKADir}/game/g_items.cpp"
	"${JKADir}/game/g_main.cpp"
	"${JKADir}/game/g_mem.cpp"
	"${JKADir}/game/g_misc.cpp"
	"${JKADir}/game/g_misc_model.cpp"
	"${JKADir}/game/g_missile.cpp"
	"${JKADir}/game/g_mover.cpp"
	"${JKADir}/game/g_nav.cpp"
	"${JKADir}/game/g_navigator.cpp"
	"${JKADir}/game/g_navnew.cpp"
	"${JKADir}/game/g_object.cpp"
	"${JKADir}/game/g_objectives.cpp"
	"${JKADir}/game/g_rail.cpp"
	"${JKADir}/game/g_ref.cpp"
	"${JKADir}/game/g_roff.cpp"
	"${JKADir}/game/g_savegame.cpp"
	"${JKADir}/game/g_session.cpp"
	"${JKADir}/game/g_spawn.cpp"
	"${JKADir}/game/g_svcmds.cpp"
	"${JKADir}/game/g_target.cpp"
	"${JKADir}/game/G_Timer.cpp"
	"${JKADir}/game/g_trigger.cpp"
	"${JKADir}/game/g_turret.cpp"
	"${JKADir}/game/g_usable.cpp"
	"${JKADir}/game/g_utils.cpp"
	"${JKADir}/game/g_weapon.cpp"
	"${JKADir}/game/g_weaponLoad.cpp"
	"${CodeDir}/qcommon/GenericParser2.cpp"
	"${JKADir}/game/NPC.cpp"
	"${JKADir}/game/NPC_behavior.cpp"
	"${JKADir}/game/NPC_combat.cpp"
	"${JKADir}/game/NPC_goal.cpp"
	"${JKADir}/game/NPC_misc.cpp"
	"${JKADir}/game/NPC_move.cpp"
	"${JKADir}/game/NPC_reactions.cpp"
	"${JKADir}/game/NPC_senses.cpp"
	"${JKADir}/game/NPC_sounds.cpp"
	"${JKADir}/game/NPC_spawn.cpp"
	"${JKADir}/game/NPC_stats.cpp"
	"${JKADir}/game/NPC_utils.cpp"
	"${JKADir}/game/Q3_Interface.h"
	"${JKADir}/game/wp_atst.cpp"
	"${JKADir}/game/wp_blaster_pistol.cpp"
	"${JKADir}/game/wp_blaster_rifle.cpp"
	"${JKADir}/game/wp_bot_laser.cpp"
	"${JKADir}/game/wp_bowcaster.cpp"
	"${JKADir}/game/wp_concussion.cpp"
	"${JKADir}/game/wp_demp2.cpp"
	"${JKADir}/game/wp_det_pack.cpp"
	"${JKADir}/game/wp_disruptor.cpp"
	"${JKADir}/game/wp_emplaced_gun.cpp"
	"${JKADir}/game/wp_flechette.cpp"
	"${JKADir}/game/wp_melee.cpp"
	"${JKADir}/game/wp_noghri_stick.cpp"
	"${JKADir}/game/wp_repeater.cpp"
	"${JKADir}/game/wp_rocket_launcher.cpp"
	"${JKADir}/game/wp_saber.cpp"
	"${JKADir}/game/wp_saberLoad.cpp"
	"${JKADir}/game/wp_stun_baton.cpp"
	"${JKADir}/game/wp_thermal.cpp"
	"${JKADir}/game/wp_trip_mine.cpp"
	"${JKADir}/game/wp_tusken.cpp"
	"${JKADir}/game/Q3_Interface.cpp"
	"${JKADir}/game/bg_misc.cpp"
	"${JKADir}/game/bg_pangles.cpp"
	"${JKADir}/game/bg_panimate.cpp"
	"${JKADir}/game/bg_pmove.cpp"
	"${JKADir}/game/bg_slidemove.cpp"
	"${JKADir}/game/AnimalNPC.cpp"
	"${JKADir}/game/bg_vehicleLoad.cpp"
	"${JKADir}/game/FighterNPC.cpp"
	"${JKADir}/game/g_vehicles.cpp"
	"${JKADir}/game/SpeederNPC.cpp"
	"${JKADir}/game/WalkerNPC.cpp"
	"${JKADir}/game/ai.h"
	"${JKADir}/game/anims.h"
	"${JKADir}/game/b_local.h"
	"${JKADir}/game/b_public.h"
	"${JKADir}/game/bg_local.h"
	"${JKADir}/game/bg_public.h"
	"${JKADir}/game/bset.h"
	"${JKADir}/game/bstate.h"
	"${JKADir}/game/common_headers.h"
	"${JKADir}/game/dmstates.h"
	"${JKADir}/game/events.h"
	"${JKADir}/game/fields.h"
	"${JKADir}/game/g_functions.h"
	"${JKADir}/game/g_items.h"
	"${JKADir}/game/g_local.h"
	"${JKADir}/game/g_nav.h"
	"${JKADir}/game/g_navigator.h"
	"${JKADir}/game/g_roff.h"
	"${JKADir}/game/g_shared.h"
	"${CodeDir}/qcommon/GenericParser2.h"
	"${JKADir}/game/hitlocs.h"
	"${JKADir}/game/npc_headers.h"
	"${JKADir}/game/objectives.h"
	"${JKADir}/game/say.h"
	"${JKADir}/game/teams.h"
	"${JKADir}/game/weapons.h"
	"${JKADir}/game/w_local.h"
	"${JKADir}/game/wp_saber.h"
	"${JKADir}/game/g_vehicles.h"
	)
source_group("game" FILES ${JKAGameGameFiles})
set(JKAGameFiles ${JKAGameFiles} ${JKAGameGameFiles})

set(JKAGameCGameFiles
	"${JKADir}/cgame/cg_camera.cpp"
	"${JKADir}/cgame/cg_consolecmds.cpp"
	"${JKADir}/cgame/cg_credits.cpp"
	"${JKADir}/cgame/cg_draw.cpp"
	"${JKADir}/cgame/cg_drawtools.cpp"
	"${JKADir}/cgame/cg_effects.cpp"
	"${JKADir}/cgame/cg_ents.cpp"
	"${JKADir}/cgame/cg_event.cpp"
	"${JKADir}/cgame/cg_headers.cpp"
	"${JKADir}/cgame/cg_info.cpp"
	"${JKADir}/cgame/cg_lights.cpp"
	"${JKADir}/cgame/cg_localents.cpp"
	"${JKADir}/cgame/cg_main.cpp"
	"${JKADir}/cgame/cg_marks.cpp"
	"${JKADir}/cgame/cg_players.cpp"
	"${JKADir}/cgame/cg_playerstate.cpp"
	"${JKADir}/cgame/cg_predict.cpp"
	"${JKADir}/cgame/cg_scoreboard.cpp"
	"${JKADir}/cgame/cg_servercmds.cpp"
	"${JKADir}/cgame/cg_snapshot.cpp"
	"${JKADir}/cgame/cg_syscalls.cpp"
	"${JKADir}/cgame/cg_text.cpp"
	"${JKADir}/cgame/cg_view.cpp"
	"${JKADir}/cgame/cg_weapons.cpp"
	"${JKADir}/cgame/FX_ATSTMain.cpp"
	"${JKADir}/cgame/FX_Blaster.cpp"
	"${JKADir}/cgame/FX_Bowcaster.cpp"
	"${JKADir}/cgame/FX_BryarPistol.cpp"
	"${JKADir}/cgame/FX_Concussion.cpp"
	"${JKADir}/cgame/FX_DEMP2.cpp"
	"${JKADir}/cgame/FX_Disruptor.cpp"
	"${JKADir}/cgame/FX_Emplaced.cpp"
	"${JKADir}/cgame/FX_Flechette.cpp"
	"${JKADir}/cgame/FX_HeavyRepeater.cpp"
	"${JKADir}/cgame/FX_NoghriShot.cpp"
	"${JKADir}/cgame/FX_RocketLauncher.cpp"
	"${JKADir}/cgame/FX_TuskenShot.cpp"
	"${JKADir}/cgame/FxPrimitives.cpp"
	"${JKADir}/cgame/FxScheduler.cpp"
	"${JKADir}/cgame/FxSystem.cpp"
	"${JKADir}/cgame/FxTemplate.cpp"
	"${JKADir}/cgame/FxUtil.cpp"
	"${JKADir}/cgame/cg_camera.h"
	"${JKADir}/cgame/animtable.cpp"
	"${JKADir}/cgame/cg_headers.h"
	"${JKADir}/cgame/cg_local.h"
	"${JKADir}/cgame/cg_media.h"
	"${JKADir}/cgame/common_headers.h"
	"${JKADir}/cgame/FxPrimitives.h"
	"${JKADir}/cgame/FxScheduler.h"
	"${JKADir}/cgame/FxSystem.h"
	"${JKADir}/cgame/FxUtil.h"
	"${JKADir}/cgame/strip_objectives.h"
	)
source_group("cgame" FILES ${JKAGameCGameFiles})
set(JKAGameFiles ${JKAGameFiles} ${JKAGameCGameFiles})

set(JKAGameUIFiles
	)
source_group("ui" FILES ${JKAGameUIFiles})
set(JKAGameFiles ${JKAGameFiles} ${JKAGameUIFiles})

set(JKAGameCommonFiles
	"${CodeDir}/qcommon/tri_coll_test.cpp"
	"${CodeDir}/qcommon/sstring.h"
	"${CodeDir}/qcommon/tags.h"
	"${CodeDir}/qcommon/timing.h"
	"${CodeDir}/qcommon/q_shared.cpp"
	"${CodeDir}/qcommon/q_shared.h"
	"${CodeDir}/qcommon/ojk_i_saved_game.h"
	"${CodeDir}/qcommon/ojk_saved_game_class_archivers.h"
	"${CodeDir}/qcommon/ojk_saved_game_helper.h"
	"${CodeDir}/qcommon/ojk_saved_game_helper_fwd.h"
	"${CodeDir}/qcommon/ojk_scope_guard.h"
	${SharedCommonFiles}
	)
source_group("common" FILES ${JKAGameCommonFiles})
set(JKAGameFiles ${JKAGameFiles} ${JKAGameCommonFiles})

set(JKAGameCommonSafeFiles
	${SharedCommonSafeFiles}
	"${SharedDir}/qcommon/safe/files.h"
	"${SharedDir}/qcommon/safe/files.cpp"
	"${CodeDir}/qcommon/safe/memory.h"
	)
source_group("common/safe" FILES ${JKAGameCommonSafeFiles})
set(JKAGameFiles ${JKAGameFiles} ${JKAGameCommonSafeFiles})

set(JKAGameIcarusFiles
	"${JKADir}/icarus/BlockStream.cpp"
	"${JKADir}/icarus/IcarusImplementation.cpp"
	"${JKADir}/icarus/Sequence.cpp"
	"${JKADir}/icarus/Sequencer.cpp"
	"${JKADir}/icarus/TaskManager.cpp"
	"${JKADir}/icarus/blockstream.h"
	"${JKADir}/icarus/IcarusImplementation.h"
	"${JKADir}/icarus/IcarusInterface.h"
	"${JKADir}/icarus/sequence.h"
	"${JKADir}/icarus/sequencer.h"
	"${JKADir}/icarus/StdAfx.h"
	"${JKADir}/icarus/taskmanager.h"
	)
source_group("icarus" FILES ${JKAGameIcarusFiles})
set(JKAGameFiles ${JKAGameFiles} ${JKAGameIcarusFiles})

set(JKAGameRatlFiles
	"${CodeDir}/Ratl/ratl.cpp"
	"${CodeDir}/Ratl/array_vs.h"
	"${CodeDir}/Ratl/bits_vs.h"
	"${CodeDir}/Ratl/grid_vs.h"
	"${CodeDir}/Ratl/handle_pool_vs.h"
	"${CodeDir}/Ratl/hash_pool_vs.h"
	"${CodeDir}/Ratl/heap_vs.h"
	"${CodeDir}/Ratl/list_vs.h"
	"${CodeDir}/Ratl/map_vs.h"
	"${CodeDir}/Ratl/pool_vs.h"
	"${CodeDir}/Ratl/queue_vs.h"
	"${CodeDir}/Ratl/ratl_common.h"
	"${CodeDir}/Ratl/scheduler_vs.h"
	"${CodeDir}/Ratl/stack_vs.h"
	"${CodeDir}/Ratl/string_vs.h"
	"${CodeDir}/Ratl/vector_vs.h"
	)
source_group("ratl" FILES ${JKAGameRatlFiles})
set(JKAGameFiles ${JKAGameFiles} ${JKAGameRatlFiles})

set(JKAGameRavlFiles
	"${CodeDir}/Ravl/CBounds.cpp"
	"${CodeDir}/Ravl/CVec.cpp"
	"${CodeDir}/Ravl/CBounds.h"
	"${CodeDir}/Ravl/CMatrix.h"
	"${CodeDir}/Ravl/CVec.h"
	)
source_group("ravl" FILES ${JKAGameRavlFiles})
set(JKAGameFiles ${JKAGameFiles} ${JKAGameRavlFiles})

set(JKAGameRuflFiles
	"${CodeDir}/Rufl/hfile.cpp"
	"${CodeDir}/Rufl/hstring.cpp"
	"${CodeDir}/Rufl/hfile.h"
	"${CodeDir}/Rufl/hstring.h"
	)
source_group("rufl" FILES ${JKAGameRuflFiles})
set(JKAGameFiles ${JKAGameFiles} ${JKAGameRuflFiles})

set(JKAGameRaglFiles
	"${CodeDir}/Ragl/graph_region.h"
	"${CodeDir}/Ragl/graph_triangulate.h"
	"${CodeDir}/Ragl/graph_vs.h"
	"${CodeDir}/Ragl/kdtree_vs.h"
	"${CodeDir}/Ragl/ragl_common.h"
	)
source_group("ragl" FILES ${JKAGameRaglFiles})
set(JKAGameFiles ${JKAGameFiles} ${JKAGameRaglFiles})

set(JKAGameG2Files
	"${CodeDir}/ghoul2/G2.h"
	"${CodeDir}/ghoul2/ghoul2_gore.h"
	)
source_group("ghoul2" FILES ${JKAGameG2Files})
set(JKAGameFiles ${JKAGameFiles} ${JKAGameG2Files})

set(JKAGameRendererFiles
	"${CodeDir}/rd-common/mdx_format.h"
	"${CodeDir}/rd-common/tr_types.h"
	)
source_group("renderer" FILES ${JKAGameRendererFiles})
set(JKAGameFiles ${JKAGameFiles} ${JKAGameRendererFiles})

set(JKAGameClientFiles
	"${CodeDir}/client/vmachine.h"
	)
source_group("client" FILES ${JKAGameClientFiles})
set(JKAGameFiles ${JKAGameFiles} ${JKAGameClientFiles})

if(WIN32)
	set(JKAGameWin32Files
		"${SharedDir}/win32/game.rc")
	source_group("win32" FILES ${JKAGameWin32Files})
	set(JKAGameFiles ${JKAGameFiles} ${JKAGameWin32Files})
endif(WIN32)

add_library(${JKAGame} SHARED ${JKAGameFiles})
# cgame and icarus are compiled into this library, so the whole of the legacy
# gamecode is covered here. See jkx_legacy_string_literals in the root
# CMakeLists for why, and for what it would take to drop this line.
jkx_legacy_string_literals(${JKAGame})
jkx_no_undefined_symbols(${JKAGame})

# Hide symbols not explicitly marked public.
set_property(TARGET ${JKAGame} APPEND PROPERTY COMPILE_OPTIONS ${OPENJK_VISIBILITY_FLAGS})

if(NOT MSVC)
	# remove "lib" prefix for .so/.dylib files
	set_target_properties(${JKAGame} PROPERTIES PREFIX "")
endif()

if(WIN32)
	install(TARGETS ${JKAGame}
		RUNTIME
		DESTINATION "${JKAInstallDir}"
		COMPONENT ${JKAClientComponent})
else(WIN32)
	if(MakeApplicationBundles AND BuildJKAEngine)
		install(TARGETS ${JKAGame}
			LIBRARY
			DESTINATION "${JKAInstallDir}/${EngineJKA}.app/Contents/MacOS"
			COMPONENT ${JKAClientComponent})
	else()
		install(TARGETS ${JKAGame}
			LIBRARY
			DESTINATION "${JKAInstallDir}"
			COMPONENT ${JKAClientComponent})
	endif()
endif()

set_target_properties(${JKAGame} PROPERTIES COMPILE_DEFINITIONS "${JKAGameDefines}")
set_target_properties(${JKAGame} PROPERTIES INCLUDE_DIRECTORIES "${JKAGameIncludeDirectories}")
set_target_properties(${JKAGame} PROPERTIES PROJECT_LABEL "Jedi Academy Game Library")
# no libraries used
if(JKAGameLibraries)
	target_link_libraries(${JKAGame} ${JKAGameLibraries})
endif(JKAGameLibraries)
