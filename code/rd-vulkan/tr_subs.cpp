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
#include "tr_local.h"

void QDECL Com_Printf( const char *msg, ... )
{
	va_list         argptr;
	char            text[1024];

	va_start(argptr, msg);
	Q_vsnprintf(text, sizeof(text), msg, argptr);
	va_end(argptr);

	ri.Printf(PRINT_ALL, "%s", text);
}

void QDECL Com_OPrintf( const char *msg, ... )
{
	va_list         argptr;
	char            text[1024];

	va_start(argptr, msg);
	Q_vsnprintf(text, sizeof(text), msg, argptr);
	va_end(argptr);

	ri.OPrintf("%s", text);
}

void QDECL Com_Error( int level, const char *error, ... )
{
	va_list         argptr;
	char            text[1024];

	va_start(argptr, error);
	Q_vsnprintf(text, sizeof(text), error, argptr);
	va_end(argptr);

	ri.Error(level, "%s", text);
}

// HUNK
#ifdef JKX_SP_FIELDS

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

#else

void *R_Hunk_AllocateTempMemory( int size ) {
	return ri.Hunk_AllocateTempMemory( size );
}

void R_Hunk_FreeTempMemory( void *buf ) {
	ri.Hunk_FreeTempMemory( buf );
}

void *R_Hunk_Alloc( int size, int preference ) {
	// Cast because our signature takes int: the enum exists on this side only.
	return ri.Hunk_Alloc( size, (ha_pref)preference );
}

int R_Hunk_MemoryRemaining( void ) {
	return ri.Hunk_MemoryRemaining();
}

#endif // JKX_SP_FIELDS

#ifndef JKX_SP_FIELDS
// The harness build links the fork's own qcommon sources - q_shared.cpp,
// matcomp.cpp, G2_gore.cpp - and those call the unprefixed names, expecting the
// renderer module to define them. Single-player's engine defines them itself,
// so there these must not exist at all. --no-undefined found this the moment
// the wrappers were renamed, which is the second time that flag has paid for
// itself this week.
void *Hunk_AllocateTempMemory( int size )                { return R_Hunk_AllocateTempMemory( size ); }
void  Hunk_FreeTempMemory( void *buf )                   { R_Hunk_FreeTempMemory( buf ); }
void *Hunk_Alloc( int size, ha_pref preference )         { return R_Hunk_Alloc( size, (int)preference ); }
int   Hunk_MemoryRemaining( void )                       { return R_Hunk_MemoryRemaining(); }
void *Z_Malloc( int iSize, memtag_t eTag, qboolean bZeroit, int iAlign ) {
	return R_Z_Malloc( iSize, eTag, bZeroit, iAlign );
}
void  Z_Free( void *ptr )                                { R_Z_Free( ptr ); }
int   Z_MemSize( memtag_t eTag )                         { return R_Z_MemSize( eTag ); }
#endif // !JKX_SP_FIELDS

// ZONE
void *R_Z_Malloc( int iSize, memtag_t eTag, qboolean bZeroit, int iAlign ) {
#ifdef JKX_SP_FIELDS
	// Same call, different name on this side of the fence.
	return ri.Malloc( iSize, eTag, bZeroit, iAlign );
#else
	return ri.Z_Malloc( iSize, eTag, bZeroit, iAlign );
#endif
}

int R_Z_Free( void *ptr ) {
	// Single-player's Z_Free reports how many bytes it freed; multiplayer's
	// returns nothing. Ours returns the number where there is one.
#ifdef JKX_SP_FIELDS
	return ri.Z_Free( ptr );
#else
	ri.Z_Free( ptr );
	return 0;
#endif
}

int R_Z_MemSize( memtag_t eTag ) {
	return ri.Z_MemSize( eTag );
}

void Z_MorphMallocTag( void *pvBuffer, memtag_t eDesiredTag ) {
	ri.Z_MorphMallocTag( pvBuffer, eDesiredTag );
}
