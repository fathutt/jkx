#============================================================================
# Copyright (C) 2015, OpenJK contributors
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

# One directory, holding both games.
#
# These were two - JediAcademy and JediOutcast - which is right when the two
# engines are two products that never meet. Here they are two executables and a
# launcher that chooses between them, and the launcher looks for the engine
# BESIDE ITSELF, because a copy of the engine inside a retail folder is what a
# mod is. Split across two trees it can only ever find one of them.
#
# The names do not collide: jkx_jka and jkx_jk2, jkagamex86_64 and
# jk2gamex86_64. What the two games do not share is the retail assets, and those
# are not in this package - the engine is pointed at them by fs_basepath, which
# is exactly what the launcher sets.
set(JKAInstallDir "JKX")
set(JK2InstallDir "JKX")

# Install components.
#
# Two, because two things are built. There used to be five: three of them named
# the multiplayer client, server and shared core, and no target has installed
# into any of them since the multiplayer tree left this repository - CPACK
# listed them anyway, so a package built here offered the user three empty
# components. The same list left JK2ClientComponent out, so the Outcast engine
# was built, installed by its own rule, and then not packaged. Both halves of
# that were invisible: nobody runs cpack.
set(JKAClientComponent "JKASPClient")
set(JK2ClientComponent "JK2SPClient")

# Component display names
include(CPackComponent)

set(CPACK_COMPONENT_JKASPCLIENT_DISPLAY_NAME "Core")
set(CPACK_COMPONENT_JKASPCLIENT_DESCRIPTION "Files required to play the Jedi Academy single player game.")
set(CPACK_COMPONENT_JK2SPCLIENT_DISPLAY_NAME "Core")
set(CPACK_COMPONENT_JK2SPCLIENT_DESCRIPTION "Files required to play the Jedi Outcast single player game.")
set(CPACK_COMPONENTS_ALL
	${JKAClientComponent}
	${JK2ClientComponent})

set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)

# Component groups
set(CPACK_COMPONENT_JKASPCLIENT_GROUP "JKASP")
set(CPACK_COMPONENT_JK2SPCLIENT_GROUP "JK2SP")

cpack_add_component_group(JKASP
	DISPLAY_NAME "Jedi Academy Single Player"
	DESCRIPTION "Jedi Academy single player game")
cpack_add_component_group(JK2SP
	DISPLAY_NAME "Jedi Outcast Single Player"
	DESCRIPTION "Jedi Outcast single player game")

if(WIN32)
	set(CPACK_NSIS_DISPLAY_NAME "JKX")
	set(CPACK_NSIS_PACKAGE_NAME "JKX")
	set(CPACK_NSIS_MUI_ICON "${SharedDir}/icons/icon.ico")
	set(CPACK_NSIS_MUI_UNIICON "${SharedDir}/icons/icon.ico")

	set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
	include(InstallRequiredSystemLibraries)

	# The multiplayer tree is not built here and BuildMPEngine does not exist,
	# so its block went with it.

	if(BuildEngine)
		string(REPLACE "/" "\\\\" ICON "${CodeDir}/win32/starwars.ico")
		set(CPACK_NSIS_CREATE_ICONS_EXTRA
			"${CPACK_NSIS_CREATE_ICONS_EXTRA}
			CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Jedi Academy SP.lnk' \\\\
				'$INSTDIR\\\\${EngineJKA}.exe' \\\\
				'' \\\\
				'${ICON}'")

		set(CPACK_NSIS_DELETE_ICONS_EXTRA
			"${CPACK_NSIS_DELETE_ICONS_EXTRA}
			Delete '$SMPROGRAMS\\\\$MUI_TEMP\\\\Jedi Academy SP.lnk'")

		# OpenAL32.dll and EaxMan.dll used to be installed from the multiplayer
		# directory, where they were committed binaries. They went out with the
		# rest of the committed binaries, and MPDir went out with multiplayer -
		# so this was installing "//OpenAL32.dll" and failing the package. It is
		# no loss: the package unpacks over a retail install, which ships both.

		install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
				DESTINATION ${JKAInstallDir}
				COMPONENT ${JKAClientComponent})
	endif()

	# Don't run this for now until we have JK2 SP working
	if(BuildJK2Engine)
		string(REPLACE "/" "\\\\" ICON "${JK2Dir}/win32/starwars.ico")
		set(CPACK_NSIS_CREATE_ICONS_EXTRA
			"${CPACK_NSIS_CREATE_ICONS_EXTRA}
			CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Jedi Outcast SP.lnk' \\\\
				'$INSTDIR\\\\${EngineJK2}.exe' \\\\
				'' \\\\
				'${ICON}'")

		set(CPACK_NSIS_DELETE_ICONS_EXTRA
			"${CPACK_NSIS_DELETE_ICONS_EXTRA}
			Delete '$SMPROGRAMS\\\\$MUI_TEMP\\\\Jedi Outcast SP.lnk'")

		# See the note in the Jedi Academy block above.

		install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
				DESTINATION ${JK2InstallDir}
				COMPONENT ${JK2ClientComponent})
	endif()
endif()

# CPack for installer creation
set(CPACK_PACKAGE_VERSION_MAJOR "1")
set(CPACK_PACKAGE_VERSION_MINOR "0")
set(CPACK_PACKAGE_VERSION_PATCH "0")
set(CPACK_PACKAGE_FILE_NAME "JKX-${CMAKE_SYSTEM_NAME}-${Architecture}")

set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Jedi Knight II and Jedi Academy, modernised")
set(CPACK_PACKAGE_VENDOR "JKX contributors")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "JKX")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.txt")
set(CPACK_PACKAGE_DIRECTORY ${PACKAGE_DIR})
set(CPACK_BINARY_ZIP ON) # always create at least a zip file
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY 0) # prevent additional directory in zip

include(CPack)
