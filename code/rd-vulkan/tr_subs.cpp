/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
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

// tr_subs.cpp - common function replacements for modular renderer
//
// Everything here exists because the renderer used to be a module: a second
// binary with its own copy of names the engine already has. Inside one binary
// most of it is a duplicate definition and the linker says so, which is how the
// list below got shorter.
#include "tr_local.h"

#if !defined(JKX_MONOLITH_RENDERER)
void QDECL Com_Printf( const char *msg, ... )
{
	va_list         argptr;
	char            text[1024];

	va_start(argptr, msg);
	Q_vsnprintf(text, sizeof(text), msg, argptr);
	va_end(argptr);

	ri.Printf(PRINT_ALL, "%s", text);
}
#endif

void QDECL Com_OPrintf( const char *msg, ... )
{
	va_list         argptr;
	char            text[1024];

	va_start(argptr, msg);
	Q_vsnprintf(text, sizeof(text), msg, argptr);
	va_end(argptr);

	R_OPrintf("%s", text);
}

#if !defined(JKX_MONOLITH_RENDERER)
void QDECL Com_Error( int level, const char *error, ... )
{
	va_list         argptr;
	char            text[1024];

	va_start(argptr, error);
	Q_vsnprintf(text, sizeof(text), error, argptr);
	va_end(argptr);

	ri.Error(level, "%s", text);
}
#endif

// HUNK

// See tr_mem.h for why these map the way they do. Short version: single-player
// has no hunk, and TAG_HUNKALLOC on its tagged heap is freed at the same points
// the hunk used to be dropped, so lifetimes are preserved and only the
// allocation preference is lost - which the single-player renderer discards too.

void *R_Hunk_AllocateTempMemory( int size ) {
	return ri.Malloc( size, TAG_TEMP_WORKSPACE, qfalse, 4 );
}

void R_Hunk_FreeTempMemory( void *buf ) {
	ri.Z_Free( buf );
}

void *R_Hunk_Alloc( int size, int preference ) {
	(void)preference;
	return ri.Malloc( size, TAG_HUNKALLOC, qtrue, 4 );
}

int R_Hunk_MemoryRemaining( void ) {
	// The hunk could answer this because it was a stack with a top. A heap
	// cannot, and the only caller uses it for a diagnostic print, so say
	// "plenty" rather than pretend to a number.
	return 0x7fffffff;
}



// ZONE
void *R_Z_Malloc( int iSize, memtag_t eTag, qboolean bZeroit, int iAlign ) {
	// Same call, different name on this side of the fence.
	return ri.Malloc( iSize, eTag, bZeroit, iAlign );
}

int R_Z_Free( void *ptr ) {
	// Single-player's Z_Free reports how many bytes it freed; multiplayer's
	// returns nothing. Ours returns the number where there is one.
	return ri.Z_Free( ptr );
}

int R_Z_MemSize( memtag_t eTag ) {
	return ri.Z_MemSize( eTag );
}

#if !defined(JKX_MONOLITH_RENDERER)
void Z_MorphMallocTag( void *pvBuffer, memtag_t eDesiredTag ) {
	ri.Z_MorphMallocTag( pvBuffer, eDesiredTag );
}
#endif


// rd-common allocates through these. rd-vanilla defines them the same way; the
// multiplayer build gets them from its own rd-common instead.
void *R_Malloc( int iSize, memtag_t eTag, qboolean bZeroit ) {
	return ri.Malloc( iSize, eTag, bZeroit, 4 );
}

void R_Free( void *ptr ) {
	ri.Z_Free( ptr );
}

// The font loader reads this to decide whether to touch every glyph while
// building a script. rd-common declares it and expects the renderer module to
// define and register it, exactly as rd-vanilla does - see R_Register. Inside
// the engine it is the engine's, which has had one all along.
#if !defined(JKX_MONOLITH_RENDERER)
cvar_t	*com_buildScript;
#endif

