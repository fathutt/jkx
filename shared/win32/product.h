/*
===========================================================================
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// The five strings that differ between the two products, in one place.
//
// There used to be two copies of each .rc - one under code/win32 for Jedi
// Academy and one under codeJK2/win32 for Jedi Outcast - differing in exactly
// these values. Resource scripts are compiled only on Windows, so nothing on
// Linux ever reads them, and that is twice how they went stale: the rename off
// OpenJK left "JASP" and "jasp.exe" in both, and moving the gamecode one
// directory changed how many "../" it takes to reach qcommon and neither copy
// noticed until the cross build failed.
//
// One copy that reads JK2_MODE - the define that already selects everything
// else about which game is being built - cannot go stale in only one of them.

#ifdef JK2_MODE

	#define JKX_ENGINE_DESCRIPTION   "JKX: Jedi Outcast"
	#define JKX_ENGINE_INTERNAL      "jkx_jk2"
	#define JKX_ENGINE_FILENAME      "jkx_jk2.exe"
	#define JKX_ENGINE_ICON          "icons/jkx_jk2.ico"

	#define JKX_GAME_DESCRIPTION     "JKX: Jedi Outcast gamecode"
	#define JKX_GAME_INTERNAL        "jk2game"
	#define JKX_GAME_FILENAME        "jk2gamex86_64.dll"

#else

	#define JKX_ENGINE_DESCRIPTION   "JKX: Jedi Academy"
	#define JKX_ENGINE_INTERNAL      "jkx_jka"
	#define JKX_ENGINE_FILENAME      "jkx_jka.exe"
	#define JKX_ENGINE_ICON          "icons/jkx_jka.ico"

	#define JKX_GAME_DESCRIPTION     "JKX: Jedi Academy gamecode"
	#define JKX_GAME_INTERNAL        "jkagame"
	#define JKX_GAME_FILENAME        "jkagamex86_64.dll"

#endif
