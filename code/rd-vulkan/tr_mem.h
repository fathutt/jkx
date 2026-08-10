/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// The renderer's memory API.
//
// This was a compatibility layer over two engines that named memory
// differently. There is one engine now, so what is left is one real collision:
// the engine declares Z_Free, Z_MemSize and a three-argument Z_Malloc *macro*,
// so the renderer cannot reuse those names even in the same binary. These
// wrappers exist for that reason and no other.

#pragma once

// The R_ prefix is not decoration: the engine already declares Z_Free, Z_MemSize
// and a three-argument Z_Malloc *macro*, so the renderer must not reuse those
// names. The old
// single-player renderer prefixed its wrappers for the same reason.
//
// The allocation preference is inherited from the hunk this used to be and does
// nothing against a tagged heap; the single-player renderer does not pass it
// either. It stays only so the call sites do not all have to change twice.
enum { h_high, h_low, h_dontcare };

void	*R_Hunk_Alloc( int size, int preference );
void	*R_Hunk_AllocateTempMemory( int size );
void	 R_Hunk_FreeTempMemory( void *buf );
int		 R_Hunk_MemoryRemaining( void );

// Defaults, because multiplayer's declaration has them and a good third of the
// call sites rely on it.
void	*R_Z_Malloc( int iSize, memtag_t eTag, qboolean bZeroit = qfalse, int iAlign = 4 );
int		 R_Z_Free( void *ptr );
int		 R_Z_MemSize( memtag_t eTag );
