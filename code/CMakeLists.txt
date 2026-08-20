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

# The two gamecode projects are added from the top-level script now.
# add_subdirectory cannot reach outside its own directory without being told
# where to put the build tree, and games/ is not under code/ any more.

#    Add Modular Renderer project
add_subdirectory("${CodeDir}/rd-vulkan")

#    Add Engine Project
if(BuildEngine)
	# Libraries
	if(WIN32)
		set(EngineLibraries "winmm")
	endif(WIN32)
	# Defines
	# Defined when the engine executable is being compiled, as opposed to a game
	# library. It was called _JK2EXE, and the comment sitting beside it here said
	# "it's called JK2EXE but it really just means Singleplayer Exe" - a name that
	# managed to be wrong twice over. It is set for both engines, Academy and
	# Outcast alike, so the JK2 in it named the wrong game half the time; and the
	# thing it distinguishes is not single-player from multiplayer, which this
	# tree does not have, but the engine from the game module.
	#
	# It reads in shared/sys/sys_main.cpp, in sdl_icon.h and in ghoul2_shared.h -
	# places where a reader has every reason to take "JK2" at face value.
	set(EngineDefines ${SharedDefines} "JKX_ENGINE")

	set(EngineIncludeDirectories
		${EngineIncludeDirectories}
		${SharedDir}
		${CMAKE_BINARY_DIR}/shared
		${CodeDir}
		"${ThirdPartyDir}"
		"${GSLIncludeDirectory}")

	#    Dependencies

	if (NOT WIN32)
		# OpenGL
		find_package(OpenGL REQUIRED)
		set(EngineIncludeDirectories ${EngineIncludeDirectories} ${OPENGL_INCLUDE_DIR})
		set(EngineLibraries ${EngineLibraries} ${OPENGL_LIBRARIES})
	endif()

	if(UseInternalSDL2)
		if(CMAKE_SIZEOF_VOID_P EQUAL 4)
			set(EngineLibraries
				${EngineLibraries}
				${ThirdPartyDir}/SDL2/lib/x86/SDL2main.lib
				${ThirdPartyDir}/SDL2/lib/x86/SDL2.lib
				)
		else()
			set(EngineLibraries
				${EngineLibraries}
				${ThirdPartyDir}/SDL2/lib/x64/SDL2main.lib
				${ThirdPartyDir}/SDL2/lib/x64/SDL2.lib
				)
		endif()

		set(EngineIncludeDirectories
			${EngineIncludeDirectories}
			${ThirdPartyDir}/SDL2/include
			)
	else()
		find_package(SDL2 REQUIRED CONFIG)
		if (TARGET SDL2::SDL2)
			set(SDL2_INCLUDE_DIRS "")
			set(SDL2_LIBRARIES SDL2::SDL2)
			# A WIN32 executable enters at WinMain, and SDL provides it in a
			# separate library. The bundled path linked SDL2main.lib by hand;
			# the packaged one exposes it as a target, and without it the link
			# fails on an unresolved WinMain rather than on anything obvious.
			if (WIN32 AND TARGET SDL2::SDL2main)
				set(SDL2_LIBRARIES SDL2::SDL2main ${SDL2_LIBRARIES})
			endif()

			# The Windows package needs SDL2.dll beside the executable. The
			# bundled SDK used to be committed and installed with everything
			# else; a packaged SDL2 is a DLL somewhere in the toolchain's
			# prefix, and nothing copies it unless this does. Without it the
			# package builds, uploads, and fails to start on the user's machine
			# - the worst possible place to find out.
			if (WIN32 AND NOT CMAKE_VERSION VERSION_LESS 3.21)
				install(IMPORTED_RUNTIME_ARTIFACTS SDL2::SDL2
					RUNTIME DESTINATION ${JKAInstallDir} COMPONENT ${JKAClientComponent})
				install(IMPORTED_RUNTIME_ARTIFACTS SDL2::SDL2
					RUNTIME DESTINATION ${JK2InstallDir} COMPONENT ${JK2ClientComponent})
			endif()
		else()
			# Some SDL2Config.cmake versions hand back a path with whitespace
			# around it. STRIP takes exactly one string, so it cannot run on the
			# target list above.
			string(STRIP "${SDL2_LIBRARIES}" SDL2_LIBRARIES)
		endif()
		set(EngineIncludeDirectories ${EngineIncludeDirectories} ${SDL2_INCLUDE_DIRS})
		set(EngineLibraries ${EngineLibraries} ${SDL2_LIBRARIES})
	endif()

	#    Source Files

	# Client files
	set(EngineClientFiles
		"${CodeDir}/client/keys.h"
		"${CodeDir}/client/keycodes.h"

		"${CodeDir}/client/cl_cgame.cpp"
		"${CodeDir}/client/cl_cin.cpp"
		"${CodeDir}/client/cl_console.cpp"
		"${CodeDir}/client/cl_input.cpp"
		"${CodeDir}/client/cl_keys.cpp"
		"${CodeDir}/client/cl_main.cpp"
		"${CodeDir}/client/cl_parse.cpp"
		"${CodeDir}/client/cl_scrn.cpp"
		"${CodeDir}/client/cl_ui.cpp"
		"${CodeDir}/client/client.h"
		"${CodeDir}/client/client_ui.h"

		"${CodeDir}/client/snd_ambient.cpp"
		"${CodeDir}/client/snd_codec.cpp"
		"${ThirdPartyDir}/stb/stb_vorbis.c"
		"${CodeDir}/client/snd_dma.cpp"
		"${CodeDir}/client/snd_mem.cpp"
		"${CodeDir}/client/snd_mix.cpp"
		"${CodeDir}/client/snd_music.cpp"
		"${CodeDir}/client/snd_ambient.h"
		"${CodeDir}/client/snd_codec.h"
		"${CodeDir}/client/snd_local.h"
		"${CodeDir}/client/snd_music.h"
		"${CodeDir}/client/snd_public.h"

		"${CodeDir}/client/vmachine.cpp"
		"${CodeDir}/client/vmachine.h"
		)
	source_group("client" FILES ${EngineClientFiles})
	set(EngineFiles ${EngineFiles} ${EngineClientFiles})


	# qcommon files
	set(EngineCommonFiles
		"${CodeDir}/qcommon/cm_load.cpp"
		"${CodeDir}/qcommon/cm_bsp_check.cpp"
		"${CodeDir}/qcommon/cm_bsp_check.h"
		"${CodeDir}/qcommon/cm_patch.cpp"
		"${CodeDir}/qcommon/cm_polylib.cpp"
		"${CodeDir}/qcommon/cm_test.cpp"
		"${CodeDir}/qcommon/cm_trace.cpp"
		"${CodeDir}/qcommon/cm_local.h"
		"${CodeDir}/qcommon/cm_patch.h"
		"${CodeDir}/qcommon/cm_polylib.h"
		"${CodeDir}/qcommon/cm_public.h"

		"${CodeDir}/qcommon/cmd.cpp"
		"${CodeDir}/qcommon/common.cpp"
		"${CodeDir}/qcommon/cvar.cpp"

		"${CodeDir}/qcommon/files.cpp"

		"${CodeDir}/qcommon/md4.cpp"
		"${CodeDir}/qcommon/msg.cpp"
		"${CodeDir}/qcommon/net_chan.cpp"

		"${CodeDir}/qcommon/q_shared.cpp"
		"${CodeDir}/qcommon/jkx_game_limits.h"
		"${CodeDir}/qcommon/jkx_vec3.h"
		"${CodeDir}/qcommon/q_shared.h"

		"${CodeDir}/qcommon/sstring.h"
		"${CodeDir}/qcommon/stringed_ingame.cpp"
		"${CodeDir}/qcommon/stringed_interface.cpp"
		"${CodeDir}/qcommon/stringed_ingame.h"
		"${CodeDir}/qcommon/stringed_interface.h"

		"${CodeDir}/qcommon/strip.cpp"
		"${CodeDir}/qcommon/persistence.cpp"
		"${CodeDir}/qcommon/tags.h"
		"${CodeDir}/qcommon/z_memman_pc.cpp"
		"${CodeDir}/qcommon/qfiles.h"
		"${CodeDir}/qcommon/qcommon.h"
		"${CodeDir}/qcommon/game_version.h"
		"${CodeDir}/qcommon/timing.h"
		"${CodeDir}/qcommon/MiniHeap.h"

		"${CodeDir}/qcommon/jkx_i_saved_game.h"
		"${CodeDir}/qcommon/jkx_saved_game.h"
		"${CodeDir}/qcommon/jkx_saved_game.cpp"
		"${CodeDir}/qcommon/jkx_rle.h"
		"${CodeDir}/qcommon/jkx_rle.cpp"
		"${CodeDir}/qcommon/jkx_saved_game_helper.h"
		"${CodeDir}/qcommon/jkx_saved_game_helper_fwd.h"
		"${CodeDir}/qcommon/jkx_scope_guard.h"

		${SharedCommonFiles}
		)
	source_group("common" FILES ${EngineCommonFiles})
	set(EngineFiles ${EngineFiles} ${EngineCommonFiles})

	set(EngineCommonSafeFiles
		"${SharedDir}/qcommon/safe/files.cpp"
		"${SharedDir}/qcommon/safe/files.h"
		${SharedCommonSafeFiles}
		"${CodeDir}/qcommon/safe/memory.h"
		)
	source_group("common/safe" FILES ${EngineCommonSafeFiles})
	set(EngineFiles ${EngineFiles} ${EngineCommonSafeFiles})


	# Server files
	set(EngineServerFiles
		"${CodeDir}/server/exe_headers.cpp"
		"${CodeDir}/server/sv_ccmds.cpp"
		"${CodeDir}/server/sv_client.cpp"
		"${CodeDir}/server/sv_game.cpp"
		"${CodeDir}/server/sv_init.cpp"
		"${CodeDir}/server/sv_main.cpp"
		"${CodeDir}/server/sv_savegame.cpp"
		"${CodeDir}/server/sv_snapshot.cpp"
		"${CodeDir}/server/sv_world.cpp"
		"${CodeDir}/server/exe_headers.h"
		"${CodeDir}/server/server.h"
		)
	source_group("server" FILES ${EngineServerFiles})
	set(EngineFiles ${EngineFiles} ${EngineServerFiles})


	# The gamecode headers the engine actually includes.
	#
	# This list used to name eighteen of them, which was a picture of the tree in
	# about 2013: anims.h, b_public.h, bg_public.h, bset.h, bstate.h,
	# common_headers.h, g_functions.h, g_items.h, g_local.h, g_nav.h, g_shared.h,
	# statindex.h, teams.h, weapons.h, channels.h, surfaceflags.h. None of them
	# affected the build - a header in a source list is grouping for the IDE and
	# nothing else - so nothing ever noticed when the includes went away or the
	# files moved. What is left is what the engine includes today, and the four
	# headers are the whole of the engine-to-game interface.
	set(EngineGameFiles
		"${CodeDir}/qcommon/GenericParser2.cpp"
		"${CodeDir}/qcommon/pc_parse.cpp"
		"${CodeDir}/qcommon/GenericParser2.h"
		"${CodeDir}/api/g_public.h"
		"${CodeDir}/api/cg_public.h"
		"${CodeDir}/api/ui_public.h"
		"${CodeDir}/ui/ui_shared.h"
		)
	source_group("game" FILES ${EngineGameFiles})
	set(EngineFiles ${EngineFiles} ${EngineGameFiles})



	set(EngineRendererFiles
		"${CodeDir}/rd-common/mdx_format.h"
		"${CodeDir}/rd-common/tr_public.h"
		"${CodeDir}/rd-common/tr_types.h"
		)
	source_group("renderer" FILES ${EngineRendererFiles})
	set(EngineFiles ${EngineFiles} ${EngineRendererFiles})

	# UI files
	set(EngineUIFiles
		"${CodeDir}/ui/anim_names.cpp"
		"${CodeDir}/ui/ui_atoms.cpp"
		"${CodeDir}/ui/ui_connect.cpp"
		"${CodeDir}/ui/ui_main.cpp"
		"${CodeDir}/ui/ui_saber.cpp"
		"${CodeDir}/ui/ui_shared.cpp"
		"${CodeDir}/ui/ui_syscalls.cpp"
		"${CodeDir}/ui/menudef.h"
		"${CodeDir}/ui/ui_local.h"
		"${CodeDir}/ui/ui_shared.h"
		)
	source_group("ui" FILES ${EngineUIFiles})
	set(EngineFiles ${EngineFiles} ${EngineUIFiles})


	# Ghoul2 files
	set(EngineGhoul2Files
		"${CodeDir}/ghoul2/G2.h"
		"${CodeDir}/ghoul2/g2_local.h"
		"${CodeDir}/ghoul2/ghoul2_gore.h"
		)

	# Phase 2.3: the skeleton, bolt, bone and surface halves of Ghoul2 live in
	# the engine rather than inside the renderer. They are animation and
	# collision logic; the only reason they were ever in a renderer is that
	# Ghoul2 arrived as one piece. G2_misc.cpp is still over there, held by gore,
	# which genuinely is drawing.
	#
	# Attached to the target below rather than to EngineFiles, because they ask
	# the model registry four questions and the answers come from the renderer
	# linked in beside them. The JK2 engine still loads its renderer at runtime
	# and gets its Ghoul2 from there, exactly as it did.
	set(EngineGhoul2Sources
		"${CodeDir}/ghoul2/G2_API.cpp"
		"${CodeDir}/ghoul2/G2_bolts.cpp"
		"${CodeDir}/ghoul2/G2_bones.cpp"
		"${CodeDir}/ghoul2/G2_surfaces.cpp"
		)
	source_group("ghoul2" FILES ${EngineGhoul2Files})
	set(EngineFiles ${EngineFiles} ${EngineGhoul2Files})


	# The animation name table. Which game's is decided per engine, inside
	# add_engine_project below, because it is the one source file in here that
	# differs between the two.

	set(EngineSysFiles
		"${SharedDir}/sys/sys_main.cpp"
		"${SharedDir}/sys/sys_event.cpp"
		"${SharedDir}/sys/sys_local.h"
		"${SharedDir}/sys/sys_public.h"
		"${SharedDir}/sys/sys_loadlib.h"
		"${SharedDir}/sys/con_local.h"
		"${SharedDir}/sys/con_log.cpp"
		"${SharedDir}/sdl/sdl_qgl.h"
		"${SharedDir}/sdl/sdl_sound.cpp"
		"${SharedDir}/sdl/sdl_input.cpp"
		"${SharedDir}/sdl/sdl_window.cpp"
		"${SharedDir}/sdl/sdl_icon.h"
		)

	if(WIN32)
		set(EngineSysFiles
			${EngineSysFiles}
			"${SharedDir}/sys/sys_win32.cpp"
			"${SharedDir}/sys/con_passive.cpp"
			)
	else()
		set(EngineSysFiles
			${EngineSysFiles}
			"${SharedDir}/sys/sys_unix.cpp"
			"${SharedDir}/sys/con_tty.cpp"
			)
	endif()

	source_group("sys" FILES ${EngineSysFiles})
	set(EngineFiles ${EngineFiles} ${EngineSysFiles})

    # Transparently use our bundled minizip.
    list(APPEND EngineIncludeDirectories ${MINIZIP_INCLUDE_DIRS})
    list(APPEND EngineLibraries          ${MINIZIP_LIBRARIES})

    # Transparently use either bundled or system zlib.
    list(APPEND EngineIncludeDirectories ${ZLIB_INCLUDE_DIR})
    list(APPEND EngineLibraries          ${ZLIB_LIBRARIES})

	# One macro, invoked once. It used to be invoked twice - the same sources
	# with -DJK2_MODE for the second - and that define is gone: which game this
	# is, is com_game, read at startup. GameDirName is where the macOS bundle
	# resources live and is code/ for historical reasons.
	#
	# Both animation tables are compiled in. The engine resolves animation names
	# out of .menu files before any game module is loaded, so it cannot be handed
	# the right table; code/ui/anim_names.cpp picks between them at runtime.
	function(add_engine_project ProjectName Label GameDirName InstallDir Component)
		set(EngineCGameFiles
			"${JKADir}/cgame/animtable.cpp"
			"${JK2Dir}/cgame/animtable.cpp"
			)
		source_group("cgame" FILES ${EngineCGameFiles})
		set(EngineFiles ${EngineFiles} ${EngineCGameFiles})

		if(WIN32)
			set(EngineWin32Files
				"${SharedDir}/win32/resource.h"
				"${SharedDir}/win32/product.h"
				"${SharedDir}/win32/winquake.rc"
				"${SharedDir}/sys/win_manifest.manifest"
				)
			source_group("win32" FILES ${EngineWin32Files})
			set(EngineFiles ${EngineFiles} ${EngineWin32Files})
			
			add_executable(${ProjectName} WIN32 ${EngineFiles})
			if(UseInternalSDL2)
				if(CMAKE_SIZEOF_VOID_P EQUAL 4)
					set(EngineExtraInstallFiles
						${ThirdPartyDir}/SDL2/bin/x86/SDL2.dll
						)
				else()
					set(EngineExtraInstallFiles
						${ThirdPartyDir}/SDL2/bin/x64/SDL2.dll
						)
				endif()
			endif()
		else(WIN32)
			if(MakeApplicationBundles)
				set_source_files_properties(${GameDirName}/macosx/OpenJK.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
				set(EngineFiles ${EngineFiles} ${GameDirName}/macosx/OpenJK.icns)
				set_source_files_properties(${GameDirName}/macosx/English.lproj/InfoPlist.strings PROPERTIES MACOSX_PACKAGE_LOCATION Resources/English.lproj)
				set(EngineFiles ${EngineFiles} ${GameDirName}/macosx/English.lproj/InfoPlist.strings)
				
				add_executable(${ProjectName} MACOSX_BUNDLE ${EngineFiles})
				set_target_properties(${ProjectName} PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${GameDirName}/macosx/OpenJKInfo.plist")
			else(MakeApplicationBundles)
				add_executable(${ProjectName} ${EngineFiles})
			endif(MakeApplicationBundles)
		endif(WIN32)

		if(MakeApplicationBundles)
			install(CODE "
				include(BundleUtilities)
				set(BU_CHMOD_BUNDLE_ITEMS ON)
				fixup_bundle(\"${CMAKE_BINARY_DIR}/${ProjectName}.app\" \"\" \"\")
				"
				COMPONENT Runtime)
			install(TARGETS ${ProjectName}
				BUNDLE
				DESTINATION ${InstallDir}
				COMPONENT ${Component})
		else(MakeApplicationBundles)
			install(TARGETS ${ProjectName}
				RUNTIME
				DESTINATION ${InstallDir}
				COMPONENT ${Component})
			if(EngineExtraInstallFiles)
				install(FILES ${EngineExtraInstallFiles}
					DESTINATION ${InstallDir}
					COMPONENT ${Component})
			endif()
		endif(MakeApplicationBundles)

		set_target_properties(${ProjectName} PROPERTIES COMPILE_DEFINITIONS "${EngineDefines}")

		# Hide symbols not explicitly marked public.
		set_property(TARGET ${ProjectName} APPEND PROPERTY COMPILE_OPTIONS ${OPENJK_VISIBILITY_FLAGS})

		set_target_properties(${ProjectName} PROPERTIES INCLUDE_DIRECTORIES "${EngineIncludeDirectories}")
		set_target_properties(${ProjectName} PROPERTIES PROJECT_LABEL ${Label})
		if(EngineLibraries)
			target_link_libraries(${ProjectName} ${EngineLibraries})
		endif(EngineLibraries)
	endfunction(add_engine_project)
	if(BuildEngine)
		add_engine_project(${Engine} "SP Client" ${CodeDir} ${JKAInstallDir} ${JKAClientComponent})

		# The renderer is part of the engine, not a module beside it. GetRefAPI
		# is then an ordinary function call and the client says so at compile
		# time rather than looking for a file at runtime.
		if(BuildRdVulkan)
			target_link_libraries(${Engine} ${RDVulkanRenderer})
			target_sources(${Engine} PRIVATE ${EngineGhoul2Sources})
			source_group("ghoul2" FILES ${EngineGhoul2Sources})
		endif()
	endif(BuildEngine)

endif(BuildEngine)

# ---------------------------------------------------------------------------
# The launcher.
#
# One command that works out which game a directory holds and starts the
# matching engine. It is a separate binary rather than a mode of the engine
# because the engine is built twice - once per game - and the thing that has to
# choose between them cannot be one of them.
#
# It links minizip and nothing else from the engine: the question it answers is
# "which game's assets are these", and the answer is inside the archive.
# ---------------------------------------------------------------------------
if(BuildEngine)
	set(LauncherName "jkx_launcher")
	set(LauncherFiles
		"${CodeDir}/launcher/jkx_launcher.h"
		"${CodeDir}/launcher/jkx_install_scan.h"
		"${CodeDir}/launcher/jkx_install_scan.cpp"
		"${CodeDir}/launcher/jkx_install_find.h"
		"${CodeDir}/launcher/jkx_install_find.cpp"
		"${CodeDir}/launcher/jkx_launcher_args.h"
		"${CodeDir}/launcher/jkx_launcher_args.cpp"
		"${CodeDir}/launcher/jkx_launcher.cpp"
		"${CodeDir}/launcher/jkx_launcher_win32.cpp"
		)

	add_executable(${LauncherName} ${LauncherFiles})

	# A window, not a console window.
	#
	# The subsystem is what decides whether Windows opens a black rectangle
	# behind the launcher, and a launcher that flashes a console before showing
	# its window looks broken. main() stays as the entry point - the alternative
	# is a WinMain that exists only to call it, and then the same program has
	# two shapes depending on the platform.
	#
	# It still prints: when it is started from a console it attaches to that
	# one, and everything below has something to write to either way.
	if(WIN32)
		target_link_libraries(${LauncherName} shell32 ole32 advapi32)
		if(MSVC)
			set_target_properties(${LauncherName} PROPERTIES
				LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
		else()
			set_target_properties(${LauncherName} PROPERTIES LINK_FLAGS "-mwindows")
		endif()
	endif()
	# zlib as well as minizip: unzip.h includes zlib.h, and the engine picks
	# these up through EngineIncludeDirectories, which the launcher is not part
	# of. The MinGW cross-build is where that showed - it has no system zlib
	# header to fall back on, which is the useful kind of build to have.
	target_include_directories(${LauncherName} PRIVATE
		${MINIZIP_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIR})
	# The engine carries the architecture in its name - jkx.x86_64 - so
	# the launcher has to know what this build calls it. Same string the engine
	# gets, from the same variable, rather than a second copy of the rule.
	target_compile_definitions(${LauncherName} PRIVATE "ARCH_STRING=\"${Architecture}\"")
	target_link_libraries(${LauncherName} ${MINIZIP_LIBRARIES} ${ZLIB_LIBRARIES})
	set_target_properties(${LauncherName} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
	install(TARGETS ${LauncherName}
		DESTINATION ${JKAInstallDir}
		COMPONENT ${JKAClientComponent})
endif()
