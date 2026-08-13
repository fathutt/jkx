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

# The Jedi Academy game project is added from the top-level script now, beside
# the Jedi Outcast one. add_subdirectory cannot reach outside its own directory
# without being told where to put the build tree, and games/jka is not under
# code/ any more.

#    Add Modular Renderer project
add_subdirectory("${CodeDir}/rd-vulkan")

#    Add Engine Project
if(BuildJKAEngine OR BuildJK2Engine)
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

		"${CodeDir}/qcommon/ojk_i_saved_game.h"
		"${CodeDir}/qcommon/ojk_saved_game.h"
		"${CodeDir}/qcommon/ojk_saved_game.cpp"
		"${CodeDir}/qcommon/ojk_saved_game_helper.h"
		"${CodeDir}/qcommon/ojk_saved_game_helper_fwd.h"
		"${CodeDir}/qcommon/ojk_scope_guard.h"

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
		"${CodeDir}/ui/ui_atoms.cpp"
		"${CodeDir}/ui/ui_connect.cpp"
		"${CodeDir}/ui/ui_main.cpp"
		"${CodeDir}/ui/ui_saber.cpp"
		"${CodeDir}/ui/ui_shared.cpp"
		"${CodeDir}/ui/ui_syscalls.cpp"
		"${CodeDir}/ui/gameinfo.h"
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


	# Server files
	set(EngineCGameFiles
		"${JKADir}/cgame/animtable.h"
		)
	source_group("cgame" FILES ${EngineCGameFiles})
	set(EngineFiles ${EngineFiles} ${EngineCGameFiles})

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

	# One macro, invoked twice: once for Jedi Academy and once for Jedi Outcast.
	# The two engines are the same sources with a different define. The Windows
	# resources are now the same file too: shared/win32/product.h reads JK2_MODE
	# and supplies the five strings that used to be a second copy of each .rc.
	function(add_engine_project ProjectName Label GameDirName InstallDir Component)
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
	if(BuildJKAEngine)
		add_engine_project(${EngineJKA} "SP Client" ${CodeDir} ${JKAInstallDir} ${JKAClientComponent})

		# The renderer is part of the engine, not a module beside it. GetRefAPI
		# is then an ordinary function call and the client says so at compile
		# time rather than looking for a file at runtime.
		if(BuildRdVulkan)
			target_link_libraries(${EngineJKA} ${RDVulkanRenderer})
			target_sources(${EngineJKA} PRIVATE ${EngineGhoul2Sources})
			source_group("ghoul2" FILES ${EngineGhoul2Sources})
		endif()
	endif(BuildJKAEngine)
	if(BuildJK2Engine)
		set(EngineDefines ${EngineDefines} "JK2_MODE")
		add_engine_project(${EngineJK2} "JK2 SP Client" ${JK2Dir} ${JK2InstallDir} ${JK2ClientComponent})

		# The same renderer, linked the same way. It used to be the one place
		# the two engines diverged: JK2 loaded a renderer module while JKA had
		# one inside it, which was a stopgap and is now not even possible - the
		# loader is gone. The renderer does not know which game it is drawing.
		if(BuildRdVulkan)
			target_link_libraries(${EngineJK2} ${RDVulkanRenderer})
			target_sources(${EngineJK2} PRIVATE ${EngineGhoul2Sources})
		endif()
	endif(BuildJK2Engine)

endif(BuildJKAEngine OR BuildJK2Engine)
