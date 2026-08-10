/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// Engine services the renderer names differently from the engine.
//
// This was a compatibility layer over two refimports. There is one now, so what
// is left is a handful of renames and two shape changes, each with what it
// costs written above it. It goes away with the refimport table itself in 2.2.

#pragma once


// Multiplayer's collision trace is CM_BoxTrace(results, start, end, mins, maxs,
// model, brushmask, capsule). Single-player's is SV_Trace, which takes the same
// box and adds what Ghoul2 needs: which entity to ignore, the Ghoul2 collision
// type and a level of detail. Note the argument order differs - end and mins
// swap places - which is exactly the kind of thing that compiles either way.
#define R_EngineBoxTrace( results, start, end, mins, maxs, brushmask ) \
	SV_Trace( ( results ), ( start ), ( mins ), ( maxs ), ( end ), ENTITYNUM_NONE, \
		( brushmask ), G2_NOCOLLIDE, 0 )

// The cached map image. This was two layers of wrapping - a getter in the
// refimport table around a getter in the client around the variable - because
// a module cannot see another module's globals. It can now.
#define R_CachedMapImage()				( gpvCachedMapDiskImage )
#define R_SetUsingCachedMap( onOff )	( gbUsingCachedMapDataRightNow = ( onOff ) )

// Releasing the cached map image is NOT a rename, and the first attempt at one
// here was a memory-corrupting bug: single-player's accessor returns the
// pointer's value, not its address, so writing through it writes through the
// image. There is no setter because the renderer is not meant to set it.
//
// The single-player renderer had the same code with the same free commented
// out, and the reason: the engine keeps the disk image for a respawn, and only
// holds one at all when the machine has memory to spare. So under single-player
// this is a no-op and the engine owns the lifetime, which is where it belongs.
#define R_ReleaseCachedMapImage()		( (void)0 )

// Named for the platform on one side and not on the other.
#define R_LowPhysicalMemory()			( Sys_LowPhysicalMemory() )

// Multiplayer has a second print entry that goes to the OS console rather than
// the game console. Single-player has one console; sending both to it loses the
// distinction and nothing else, and the renderer only uses OPrintf for the same
// diagnostics it sends to Printf anyway.
// Single-player's console print takes a level; multiplayer's OS-console print
// does not. Everything the renderer sends here it also sends to Printf.
#define R_OPrintf( ... )				CL_RefPrintf( PRINT_ALL, __VA_ARGS__ )

// Multiplayer carries a one-line description per console command. Unlike the
// cvar descriptions, which were 136 real strings and worth taking into the
// engine, every one of these is the empty string - so this drops it rather
// than widening another interface to carry nothing.
#define R_AddCommand( name, func, desc )	Cmd_AddCommand( ( name ), ( func ) )

// Single-player's cinematics can carry a separate audio file. The renderer
// only ever plays shader videos, which have none.
#define R_PlayCinematic( name, x, y, w, h, bits ) \
	CIN_PlayCinematic( ( name ), ( x ), ( y ), ( w ), ( h ), ( bits ), NULL )

