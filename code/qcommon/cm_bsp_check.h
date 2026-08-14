/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

#pragma once

// Does this .bsp describe itself consistently?
//
// A BSP header is eight bytes and then eighteen pairs of them: an offset and a
// length per lump. Everything that reads a map does the same thing with those
// pairs - add the offset to the base pointer and read length/sizeof(element)
// elements - and until now nothing compared either number against the length of
// the file.
//
//   - The identifier was never checked at all. BSP_IDENT is defined in
//     qfiles.h and compared nowhere: a file with the right version field and
//     four bytes of anything at the front was loaded as a map.
//   - A lump offset is a signed int taken from the file. 0x7fffffff added to
//     the base pointer is a read two gigabytes away, and the first thing done
//     with it is a loop.
//   - A lump length is likewise unchecked, so a lump can begin inside the file
//     and end far outside it.
//   - Every typed lump is divided by the size of its element, and only some of
//     the loaders check that the division is exact ("funny lump size"). The
//     ones that do not read a partial element off the end.
//
// This is the check that was missing, and it is separated from the engine on
// purpose: it takes a pointer and a length, so a test can hand it a few hundred
// thousand malformed maps. That is the whole reason a map is worth checking -
// a .bsp arrives in a pk3, and a pk3 is something a player downloads.
//
// What this does NOT check is the contents: whether a plane index names a plane
// that exists, whether a leaf's brush range is inside the brush lump. That is
// the second half of the work and it belongs with the loaders, which know what
// the indices mean. This is the half that has to come first, because until the
// lumps are known to be inside the file there is nothing safe to read at all.

#include <stddef.h>

// Big enough for a header: the identifier, the version, and eighteen lumps.
#define BSP_HEADER_BYTES	( 8 + 18 * 8 )

// The size of one element of each typed lump, as numbers rather than as
// sizeof, because this header has to stay free of the engine's structures - a
// test compiles it with nothing else. cm_load.cpp static_asserts every one of
// them against the real struct in qfiles.h, which is what keeps them honest: a
// size table that has drifted from the structures does not fail, it stops
// checking. Two of these were guessed wrong the first time they were written.
#define BSP_ELEM_SHADERS		72
#define BSP_ELEM_PLANES			16
#define BSP_ELEM_NODES			36
#define BSP_ELEM_LEAFS			48
#define BSP_ELEM_LEAFSURFACES	4
#define BSP_ELEM_LEAFBRUSHES	4
#define BSP_ELEM_MODELS			40
#define BSP_ELEM_BRUSHES		12
#define BSP_ELEM_BRUSHSIDES		12
#define BSP_ELEM_DRAWVERTS		80
#define BSP_ELEM_FOGS			72
#define BSP_ELEM_SURFACES		148

// Answers NULL when every lump lies inside a file of len bytes and every typed
// lump divides exactly by its element, or a message saying which one does not.
// The message names the lump, because "funny lump size" has been the whole
// diagnosis for twenty years and it does not say which of the eighteen.
//
// Nothing is written to data.
const char *BSP_CheckHeader( const unsigned char *data, size_t len );

// The element size the checker expects for a lump, or 0 for the lumps that have
// no fixed element - entities, visibility, lightmaps, draw indexes. Exposed so
// that the engine can assert its own structures against it: if a struct in
// qfiles.h changes size, that has to break the build here rather than quietly
// make this check wrong.
int BSP_LumpElementSize( int lump );
