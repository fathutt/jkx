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

// The strings the Windows resource compiler puts in the binaries.
//
// There used to be two copies of each .rc - one under code/win32 for Jedi
// Academy and one under codeJK2/win32 for Jedi Outcast - differing in exactly
// these values. Resource scripts are compiled only on Windows, so nothing on
// Linux ever reads them, and that is twice how they went stale: the rename off
// OpenJK left "JASP" and "jasp.exe" in both, and moving the gamecode one
// directory changed how many "../" it takes to reach qcommon and neither copy
// noticed until the cross build failed.
//
// Then there was one copy behind #ifdef JK2_MODE. Now there is no conditional
// at all, because there is one engine: the same jkx.exe runs both games and
// picks which one at startup. A resource string is baked by rc.exe and cannot
// be chosen at runtime, so the engine's description names the engine rather
// than either game - Com_WindowTitle() is what the player actually sees.
//
// The gamecode strings stay a pair because the gamecode stays a pair: two
// modules, loaded by name, and each .rc is compiled with its own module's
// defines.

#define JKX_ENGINE_DESCRIPTION   "JKX: Jedi Knight"
#define JKX_ENGINE_INTERNAL      "jkx"
#define JKX_ENGINE_FILENAME      "jkx.exe"
#define JKX_ENGINE_ICON          "icons/jkx.ico"

#ifdef JKX_GAME_OUTCAST

	#define JKX_GAME_DESCRIPTION     "JKX: Jedi Outcast gamecode"
	#define JKX_GAME_INTERNAL        "jogame"
	#define JKX_GAME_FILENAME        "jogamex86_64.dll"

#else

	#define JKX_GAME_DESCRIPTION     "JKX: Jedi Academy gamecode"
	#define JKX_GAME_INTERNAL        "jagame"
	#define JKX_GAME_FILENAME        "jagamex86_64.dll"

#endif
