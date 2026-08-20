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
set(JAGameDefines ${SharedDefines} "JKX_GAME_MODULE")
# ${JADir} sits ahead of ${CodeDir} here, the same way ${JODir} does in the
# other game's list. It is what makes "game/g_shared.h" and "cgame/cg_local.h"
# resolve into this tree: they used to resolve through ${CodeDir} because this
# gamecode was inside code/, and shared/qcommon/safe/files.cpp still writes the
# first of those - it is compiled into both game libraries and has to reach
# whichever gamecode it is being built into.
set(JAGameIncludeDirectories
	${SharedDir}
	"${JADir}"
	${CodeDir}
	"${JADir}/game"
	"${GSLIncludeDirectory}"
	"${CMAKE_BINARY_DIR}/shared"
	)

if(WIN32)
	set(JAGameLibraries "winmm")
endif(WIN32)

set(JAGameGameFiles
	"${JADir}/game/AI_Animal.cpp"
	"${JADir}/game/AI_AssassinDroid.cpp"
	"${JADir}/game/AI_Atst.cpp"
	"${JADir}/game/AI_BobaFett.cpp"
	"${JADir}/game/AI_Civilian.cpp"
	"${JADir}/game/AI_Default.cpp"
	"${JADir}/game/AI_Droid.cpp"
	"${JADir}/game/AI_GalakMech.cpp"
	"${JADir}/game/AI_Grenadier.cpp"
	"${JADir}/game/AI_HazardTrooper.cpp"
	"${JADir}/game/AI_Howler.cpp"
	"${JADir}/game/AI_ImperialProbe.cpp"
	"${JADir}/game/AI_Interrogator.cpp"
	"${JADir}/game/AI_Jedi.cpp"
	"${JADir}/game/AI_Mark1.cpp"
	"${JADir}/game/AI_Mark2.cpp"
	"${JADir}/game/AI_MineMonster.cpp"
	"${JADir}/game/AI_Rancor.cpp"
	"${JADir}/game/AI_Remote.cpp"
	"${JADir}/game/AI_RocketTrooper.cpp"
	"${JADir}/game/AI_SaberDroid.cpp"
	"${JADir}/game/AI_SandCreature.cpp"
	"${JADir}/game/AI_Seeker.cpp"
	"${JADir}/game/AI_Sentry.cpp"
	"${JADir}/game/AI_Sniper.cpp"
	"${JADir}/game/AI_Stormtrooper.cpp"
	"${JADir}/game/AI_Tusken.cpp"
	"${JADir}/game/AI_Utils.cpp"
	"${JADir}/game/AI_Wampa.cpp"
	"${JADir}/game/g_active.cpp"
	"${JADir}/game/g_breakable.cpp"
	"${JADir}/game/g_camera.cpp"
	"${JADir}/game/g_client.cpp"
	"${JADir}/game/g_cmds.cpp"
	"${JADir}/game/g_combat.cpp"
	"${JADir}/game/g_emplaced.cpp"
	"${JADir}/game/g_functions.cpp"
	"${JADir}/game/g_fx.cpp"
	"${JADir}/game/g_inventory.cpp"
	"${JADir}/game/g_itemLoad.cpp"
	"${JADir}/game/g_items.cpp"
	"${JADir}/game/g_main.cpp"
	"${JADir}/game/g_mem.cpp"
	"${JADir}/game/g_misc.cpp"
	"${JADir}/game/g_misc_model.cpp"
	"${JADir}/game/g_missile.cpp"
	"${JADir}/game/g_mover.cpp"
	"${JADir}/game/g_nav.cpp"
	"${JADir}/game/g_navigator.cpp"
	"${JADir}/game/g_navnew.cpp"
	"${JADir}/game/g_object.cpp"
	"${JADir}/game/g_objectives.cpp"
	"${JADir}/game/g_rail.cpp"
	"${JADir}/game/g_ref.cpp"
	"${JADir}/game/g_roff.cpp"
	"${JADir}/game/g_savegame.cpp"
	"${JADir}/game/g_session.cpp"
	"${JADir}/game/g_spawn.cpp"
	"${JADir}/game/g_svcmds.cpp"
	"${JADir}/game/g_target.cpp"
	"${JADir}/game/G_Timer.cpp"
	"${JADir}/game/g_trigger.cpp"
	"${JADir}/game/g_turret.cpp"
	"${JADir}/game/g_usable.cpp"
	"${JADir}/game/g_utils.cpp"
	"${JADir}/game/g_weapon.cpp"
	"${JADir}/game/g_weaponLoad.cpp"
	"${CodeDir}/qcommon/GenericParser2.cpp"
	"${JADir}/game/NPC.cpp"
	"${JADir}/game/NPC_behavior.cpp"
	"${JADir}/game/NPC_combat.cpp"
	"${JADir}/game/NPC_goal.cpp"
	"${JADir}/game/NPC_misc.cpp"
	"${JADir}/game/NPC_move.cpp"
	"${JADir}/game/NPC_reactions.cpp"
	"${JADir}/game/NPC_senses.cpp"
	"${JADir}/game/NPC_sounds.cpp"
	"${JADir}/game/NPC_spawn.cpp"
	"${JADir}/game/NPC_stats.cpp"
	"${JADir}/game/NPC_utils.cpp"
	"${JADir}/game/Q3_Interface.h"
	"${JADir}/game/wp_atst.cpp"
	"${JADir}/game/wp_blaster_pistol.cpp"
	"${JADir}/game/wp_blaster_rifle.cpp"
	"${JADir}/game/wp_bot_laser.cpp"
	"${JADir}/game/wp_bowcaster.cpp"
	"${JADir}/game/wp_concussion.cpp"
	"${JADir}/game/wp_demp2.cpp"
	"${JADir}/game/wp_det_pack.cpp"
	"${JADir}/game/wp_disruptor.cpp"
	"${JADir}/game/wp_emplaced_gun.cpp"
	"${JADir}/game/wp_flechette.cpp"
	"${JADir}/game/wp_melee.cpp"
	"${JADir}/game/wp_noghri_stick.cpp"
	"${JADir}/game/wp_repeater.cpp"
	"${JADir}/game/wp_rocket_launcher.cpp"
	"${JADir}/game/wp_saber.cpp"
	"${JADir}/game/wp_saberLoad.cpp"
	"${JADir}/game/wp_stun_baton.cpp"
	"${JADir}/game/wp_thermal.cpp"
	"${JADir}/game/wp_trip_mine.cpp"
	"${JADir}/game/wp_tusken.cpp"
	"${JADir}/game/Q3_Interface.cpp"
	"${JADir}/game/bg_misc.cpp"
	"${JADir}/game/bg_pangles.cpp"
	"${JADir}/game/bg_panimate.cpp"
	"${JADir}/game/bg_pmove.cpp"
	"${JADir}/game/bg_slidemove.cpp"
	"${JADir}/game/AnimalNPC.cpp"
	"${JADir}/game/bg_vehicleLoad.cpp"
	"${JADir}/game/FighterNPC.cpp"
	"${JADir}/game/g_vehicles.cpp"
	"${JADir}/game/SpeederNPC.cpp"
	"${JADir}/game/WalkerNPC.cpp"
	"${JADir}/game/ai.h"
	"${JADir}/game/anims.h"
	"${JADir}/game/b_local.h"
	"${JADir}/game/b_public.h"
	"${JADir}/game/bg_local.h"
	"${JADir}/game/bg_public.h"
	"${JADir}/game/bset.h"
	"${JADir}/game/bstate.h"
	"${JADir}/game/common_headers.h"
	"${JADir}/game/dmstates.h"
	"${JADir}/game/events.h"
	"${JADir}/game/fields.h"
	"${JADir}/game/g_functions.h"
	"${JADir}/game/g_items.h"
	"${JADir}/game/g_local.h"
	"${JADir}/game/g_nav.h"
	"${JADir}/game/g_navigator.h"
	"${JADir}/game/g_roff.h"
	"${JADir}/game/g_shared.h"
	"${CodeDir}/qcommon/GenericParser2.h"
	"${JADir}/game/hitlocs.h"
	"${JADir}/game/npc_headers.h"
	"${JADir}/game/objectives.h"
	"${JADir}/game/say.h"
	"${JADir}/game/teams.h"
	"${JADir}/game/weapons.h"
	"${JADir}/game/w_local.h"
	"${JADir}/game/wp_saber.h"
	"${JADir}/game/g_vehicles.h"
	)
source_group("game" FILES ${JAGameGameFiles})
set(JAGameFiles ${JAGameFiles} ${JAGameGameFiles})

set(JAGameCGameFiles
	"${JADir}/cgame/cg_camera.cpp"
	"${JADir}/cgame/cg_consolecmds.cpp"
	"${JADir}/cgame/cg_credits.cpp"
	"${JADir}/cgame/cg_draw.cpp"
	"${JADir}/cgame/cg_drawtools.cpp"
	"${JADir}/cgame/cg_effects.cpp"
	"${JADir}/cgame/cg_ents.cpp"
	"${JADir}/cgame/cg_event.cpp"
	"${JADir}/cgame/cg_headers.cpp"
	"${JADir}/cgame/cg_info.cpp"
	"${JADir}/cgame/cg_lights.cpp"
	"${JADir}/cgame/cg_localents.cpp"
	"${JADir}/cgame/cg_main.cpp"
	"${JADir}/cgame/cg_marks.cpp"
	"${JADir}/cgame/cg_players.cpp"
	"${JADir}/cgame/cg_playerstate.cpp"
	"${JADir}/cgame/cg_predict.cpp"
	"${JADir}/cgame/cg_scoreboard.cpp"
	"${JADir}/cgame/cg_servercmds.cpp"
	"${JADir}/cgame/cg_snapshot.cpp"
	"${JADir}/cgame/cg_syscalls.cpp"
	"${JADir}/cgame/cg_text.cpp"
	"${JADir}/cgame/cg_view.cpp"
	"${JADir}/cgame/cg_weapons.cpp"
	"${JADir}/cgame/FX_ATSTMain.cpp"
	"${JADir}/cgame/FX_Blaster.cpp"
	"${JADir}/cgame/FX_Bowcaster.cpp"
	"${JADir}/cgame/FX_BryarPistol.cpp"
	"${JADir}/cgame/FX_Concussion.cpp"
	"${JADir}/cgame/FX_DEMP2.cpp"
	"${JADir}/cgame/FX_Disruptor.cpp"
	"${JADir}/cgame/FX_Emplaced.cpp"
	"${JADir}/cgame/FX_Flechette.cpp"
	"${JADir}/cgame/FX_HeavyRepeater.cpp"
	"${JADir}/cgame/FX_NoghriShot.cpp"
	"${JADir}/cgame/FX_RocketLauncher.cpp"
	"${JADir}/cgame/FX_TuskenShot.cpp"
	"${JADir}/cgame/FxPrimitives.cpp"
	"${JADir}/cgame/FxScheduler.cpp"
	"${JADir}/cgame/FxSystem.cpp"
	"${JADir}/cgame/FxTemplate.cpp"
	"${JADir}/cgame/FxUtil.cpp"
	"${JADir}/cgame/cg_camera.h"
	"${JADir}/cgame/animtable.cpp"
	"${JADir}/cgame/cg_headers.h"
	"${JADir}/cgame/cg_local.h"
	"${JADir}/cgame/cg_media.h"
	"${JADir}/cgame/common_headers.h"
	"${JADir}/cgame/FxPrimitives.h"
	"${JADir}/cgame/FxScheduler.h"
	"${JADir}/cgame/FxSystem.h"
	"${JADir}/cgame/FxUtil.h"
	"${JADir}/cgame/strip_objectives.h"
	)
source_group("cgame" FILES ${JAGameCGameFiles})
set(JAGameFiles ${JAGameFiles} ${JAGameCGameFiles})

set(JAGameUIFiles
	)
source_group("ui" FILES ${JAGameUIFiles})
set(JAGameFiles ${JAGameFiles} ${JAGameUIFiles})

set(JAGameCommonFiles
	"${CodeDir}/qcommon/tri_coll_test.cpp"
	"${CodeDir}/qcommon/sstring.h"
	"${CodeDir}/qcommon/tags.h"
	"${CodeDir}/qcommon/timing.h"
	"${CodeDir}/qcommon/q_shared.cpp"
	"${CodeDir}/qcommon/q_shared.h"
	"${CodeDir}/qcommon/jkx_i_saved_game.h"
	"${CodeDir}/qcommon/jkx_saved_game_class_archivers.h"
	"${CodeDir}/qcommon/jkx_saved_game_helper.h"
	"${CodeDir}/qcommon/jkx_saved_game_helper_fwd.h"
	"${CodeDir}/qcommon/jkx_scope_guard.h"
	${SharedCommonFiles}
	)
source_group("common" FILES ${JAGameCommonFiles})
set(JAGameFiles ${JAGameFiles} ${JAGameCommonFiles})

set(JAGameCommonSafeFiles
	${SharedCommonSafeFiles}
	"${SharedDir}/qcommon/safe/files.h"
	"${SharedDir}/qcommon/safe/files.cpp"
	"${CodeDir}/qcommon/safe/memory.h"
	)
source_group("common/safe" FILES ${JAGameCommonSafeFiles})
set(JAGameFiles ${JAGameFiles} ${JAGameCommonSafeFiles})

set(JAGameIcarusFiles
	"${JADir}/icarus/BlockStream.cpp"
	"${JADir}/icarus/IcarusImplementation.cpp"
	"${JADir}/icarus/Sequence.cpp"
	"${JADir}/icarus/Sequencer.cpp"
	"${JADir}/icarus/TaskManager.cpp"
	"${JADir}/icarus/blockstream.h"
	"${JADir}/icarus/IcarusImplementation.h"
	"${JADir}/icarus/IcarusInterface.h"
	"${JADir}/icarus/sequence.h"
	"${JADir}/icarus/sequencer.h"
	"${JADir}/icarus/StdAfx.h"
	"${JADir}/icarus/taskmanager.h"
	)
source_group("icarus" FILES ${JAGameIcarusFiles})
set(JAGameFiles ${JAGameFiles} ${JAGameIcarusFiles})

set(JAGameRatlFiles
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
source_group("ratl" FILES ${JAGameRatlFiles})
set(JAGameFiles ${JAGameFiles} ${JAGameRatlFiles})

set(JAGameRavlFiles
	"${CodeDir}/Ravl/CBounds.cpp"
	"${CodeDir}/Ravl/CVec.cpp"
	"${CodeDir}/Ravl/CBounds.h"
	"${CodeDir}/Ravl/CMatrix.h"
	"${CodeDir}/Ravl/CVec.h"
	)
source_group("ravl" FILES ${JAGameRavlFiles})
set(JAGameFiles ${JAGameFiles} ${JAGameRavlFiles})

set(JAGameRuflFiles
	"${CodeDir}/Rufl/hfile.cpp"
	"${CodeDir}/Rufl/hstring.cpp"
	"${CodeDir}/Rufl/hfile.h"
	"${CodeDir}/Rufl/hstring.h"
	)
source_group("rufl" FILES ${JAGameRuflFiles})
set(JAGameFiles ${JAGameFiles} ${JAGameRuflFiles})

set(JAGameRaglFiles
	"${CodeDir}/Ragl/graph_region.h"
	"${CodeDir}/Ragl/graph_triangulate.h"
	"${CodeDir}/Ragl/graph_vs.h"
	"${CodeDir}/Ragl/kdtree_vs.h"
	"${CodeDir}/Ragl/ragl_common.h"
	)
source_group("ragl" FILES ${JAGameRaglFiles})
set(JAGameFiles ${JAGameFiles} ${JAGameRaglFiles})

set(JAGameG2Files
	"${CodeDir}/ghoul2/G2.h"
	"${CodeDir}/ghoul2/ghoul2_gore.h"
	)
source_group("ghoul2" FILES ${JAGameG2Files})
set(JAGameFiles ${JAGameFiles} ${JAGameG2Files})

set(JAGameRendererFiles
	"${CodeDir}/rd-common/mdx_format.h"
	"${CodeDir}/rd-common/tr_types.h"
	)
source_group("renderer" FILES ${JAGameRendererFiles})
set(JAGameFiles ${JAGameFiles} ${JAGameRendererFiles})

set(JAGameClientFiles
	"${CodeDir}/client/vmachine.h"
	)
source_group("client" FILES ${JAGameClientFiles})
set(JAGameFiles ${JAGameFiles} ${JAGameClientFiles})

if(WIN32)
	set(JAGameWin32Files
		"${SharedDir}/win32/game.rc")
	source_group("win32" FILES ${JAGameWin32Files})
	set(JAGameFiles ${JAGameFiles} ${JAGameWin32Files})
endif(WIN32)

add_library(${JAGame} SHARED ${JAGameFiles})
# cgame and icarus are compiled into this library, so the whole of the legacy
# gamecode is covered here. See jkx_legacy_string_literals in the root
# CMakeLists for why, and for what it would take to drop this line.
jkx_legacy_string_literals(${JAGame})
jkx_no_undefined_symbols(${JAGame})

# Hide symbols not explicitly marked public.
set_property(TARGET ${JAGame} APPEND PROPERTY COMPILE_OPTIONS ${OPENJK_VISIBILITY_FLAGS})

if(NOT MSVC)
	# remove "lib" prefix for .so/.dylib files
	set_target_properties(${JAGame} PROPERTIES PREFIX "")
endif()

if(WIN32)
	install(TARGETS ${JAGame}
		RUNTIME
		DESTINATION "${JAInstallDir}"
		COMPONENT ${JAClientComponent})
else(WIN32)
	if(MakeApplicationBundles AND BuildEngine)
		install(TARGETS ${JAGame}
			LIBRARY
			DESTINATION "${JAInstallDir}/${Engine}.app/Contents/MacOS"
			COMPONENT ${JAClientComponent})
	else()
		install(TARGETS ${JAGame}
			LIBRARY
			DESTINATION "${JAInstallDir}"
			COMPONENT ${JAClientComponent})
	endif()
endif()

set_target_properties(${JAGame} PROPERTIES COMPILE_DEFINITIONS "${JAGameDefines}")
set_target_properties(${JAGame} PROPERTIES INCLUDE_DIRECTORIES "${JAGameIncludeDirectories}")
set_target_properties(${JAGame} PROPERTIES PROJECT_LABEL "Jedi Academy Game Library")
# no libraries used
if(JAGameLibraries)
	target_link_libraries(${JAGame} ${JAGameLibraries})
endif(JAGameLibraries)
