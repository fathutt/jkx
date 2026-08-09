/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// Engine services the two refimports carry under different names.
//
// The pattern is the same one tr_mem.h uses for memory: the capability exists
// on both sides, so the renderer calls one name and this header points it at
// whichever the surrounding engine actually offers. Where a service genuinely
// does not exist on one side - the Vulkan window entries, which single-player
// has none of because its renderer is OpenGL - there is nothing to adapt and
// nothing here; those have to be added to the interface and implemented.
//
// Everything below is a rename or a shape change with the same meaning. If a
// mapping ever loses something, it says so in a comment, the way the cached
// map image one does.
//
// This goes away with the refimport table itself in 2.2. Guarded on
// JKX_SP_FIELDS because every line reaches into refimport_t.

#pragma once

#ifdef JKX_SP_FIELDS

// Multiplayer's collision trace is CM_BoxTrace(results, start, end, mins, maxs,
// model, brushmask, capsule). Single-player's is SV_Trace, which takes the same
// box and adds what Ghoul2 needs: which entity to ignore, the Ghoul2 collision
// type and a level of detail. Note the argument order differs - end and mins
// swap places - which is exactly the kind of thing that compiles either way.
#define R_EngineBoxTrace( results, start, end, mins, maxs, brushmask ) \
	ri.SV_Trace( ( results ), ( start ), ( mins ), ( maxs ), ( end ), ENTITYNUM_NONE, \
		( brushmask ), G2_NOCOLLIDE, 0 )

// The cached map image. Multiplayer wraps it in getter and setter calls;
// single-player hands out a pointer to the variable itself and to the flag
// beside it. Same storage, one less layer.
#define R_CachedMapImage()				( ri.gpvCachedMapDiskImage() )
#define R_SetUsingCachedMap( onOff )	( *ri.gbUsingCachedMapDataRightNow() = ( onOff ) )

// Releasing the cached map image is NOT a rename, and the first attempt at one
// here was a memory-corrupting bug: single-player's accessor returns the
// pointer's value, not its address, so writing through it writes through the
// image. There is no setter because the renderer is not meant to set it.
//
// rd-vanilla/tr_bsp.cpp:1451 has the same code and the same free commented out,
// with the reason: the engine keeps the disk image for a respawn, and only
// holds one at all when the machine has memory to spare. So under single-player
// this is a no-op and the engine owns the lifetime, which is where it belongs.
#define R_ReleaseCachedMapImage()		( (void)0 )

// Named for the platform on one side and not on the other.
#define R_LowPhysicalMemory()			( ri.LowPhysicalMemory() )

// Multiplayer has a second print entry that goes to the OS console rather than
// the game console. Single-player has one console; sending both to it loses the
// distinction and nothing else, and the renderer only uses OPrintf for the same
// diagnostics it sends to Printf anyway.
#define R_OPrintf						ri.Printf

#else

#define R_EngineBoxTrace( results, start, end, mins, maxs, brushmask ) \
	ri.CM_BoxTrace( ( results ), ( start ), ( end ), ( mins ), ( maxs ), 0, ( brushmask ), 0 )

#define R_CachedMapImage()				( ri.CM_GetCachedMapDiskImage() )
#define R_SetUsingCachedMap( onOff )	( ri.CM_SetUsingCache( onOff ) )
#define R_ReleaseCachedMapImage() \
	do { R_Z_Free( ri.CM_GetCachedMapDiskImage() ); ri.CM_SetCachedMapDiskImage( NULL ); } while ( 0 )
#define R_LowPhysicalMemory()			( ri.Sys_LowPhysicalMemory() )
#define R_OPrintf						ri.OPrintf

#endif // JKX_SP_FIELDS
