/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// The renderer's memory API, over two engines that do not agree on one.
//
// This is NOT tr_sp_compat.h. That header may only rename things; this one
// encodes a difference in behaviour, which is why it is separate and why every
// mapping below says what it is giving up.
//
//   multiplayer   Hunk_Alloc(size, ha_pref), Hunk_AllocateTempMemory,
//                 Hunk_FreeTempMemory, Z_Malloc(size, tag, zero, align).
//                 A hunk is a stack: allocations are freed wholesale at a mark,
//                 and ha_pref picks which end of it to take from.
//   single-player No hunk at all. One tagged heap: ri.Malloc(size, tag, zero,
//                 align) and ri.Z_Free, with tags standing in for lifetime.
//
// The mapping used here - hunk allocations become TAG_HUNKALLOC on the tagged
// heap - is not invented: it is exactly what the single-player renderer does in
// rd-vanilla/tr_subs.cpp, and the engine frees that tag at the same moments it
// used to drop the hunk (see Z_TagFree(TAG_HUNKALLOC) in qcommon/common.cpp).
// So lifetimes match; what is genuinely lost is ha_pref, which has no meaning
// against a heap. rd-vanilla drops it too, and its R_Hunk_Alloc does not even
// take the argument.
//
// Only the bodies differ between the two engines, and only they are guarded -
// by JKX_SP_FIELDS, because they reach into refimport_t, which is the one thing
// the shape check cannot compile.

#pragma once

// Declared for both engines, because these are the renderer's own functions.
// The single-player engine already declares Z_Free, Z_MemSize and a
// three-argument Z_Malloc *macro* of its own, so the renderer must not reuse
// those names - which is exactly why rd-vanilla prefixes its wrappers with R_.
//
// The allocation preference is taken as int rather than ha_pref: multiplayer
// has that enum, single-player does not, and int accepts both spellings of the
// constant without this header having to invent a type on one side.
#ifdef JKX_SP_FIELDS
enum { h_high, h_low, h_dontcare };
#endif

void	*R_Hunk_Alloc( int size, int preference );
void	*R_Hunk_AllocateTempMemory( int size );
void	 R_Hunk_FreeTempMemory( void *buf );
int		 R_Hunk_MemoryRemaining( void );

// Defaults, because multiplayer's declaration has them and a good third of the
// call sites rely on it.
void	*R_Z_Malloc( int iSize, memtag_t eTag, qboolean bZeroit = qfalse, int iAlign = 4 );
int		 R_Z_Free( void *ptr );
int		 R_Z_MemSize( memtag_t eTag );
