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

#include "cm_bsp_check.h"

#include <stdio.h>


// Spelled out rather than included, so that this file depends on nothing and
// can be compiled into a test. The engine asserts its own definitions against
// these; see cm_load.cpp.
#define BSP_CHECK_IDENT		( ( 'P' << 24 ) + ( 'S' << 16 ) + ( 'B' << 8 ) + 'R' )
#define BSP_CHECK_VERSION	1
#define BSP_CHECK_LUMPS		18


// The lumps, in the order the header stores them, with the size of one element
// and a name for the message. Two of these were guessed wrong at first - a
// drawVert is 80 bytes, not 44, and a dsurface is 148, not 108 - which is why
// cm_load.cpp asserts every one of them against the real structure. A size
// table that drifts from the structs does not fail, it just stops checking. Zero means the lump is bytes rather than
// elements: entity text, the visibility bit vectors, lightmap pixels and the
// draw indexes, which are a bare int array the loaders size themselves.
typedef struct {
	const char	*name;
	int			elementSize;
} bspLumpKind_t;

static const bspLumpKind_t bspLumps[BSP_CHECK_LUMPS] = {
	{ "entities",		0	},
	{ "shaders",		BSP_ELEM_SHADERS	},	// dshader_t:    char[64] + 2 ints
	{ "planes",			BSP_ELEM_PLANES	},	// dplane_t:     4 floats
	{ "nodes",			BSP_ELEM_NODES	},	// dnode_t:      2 ints + 2 ints + 3 + 3 ints
	{ "leafs",			BSP_ELEM_LEAFS	},	// dleaf_t:      12 ints
	{ "leafsurfaces",	BSP_ELEM_LEAFSURFACES	},
	{ "leafbrushes",	BSP_ELEM_LEAFBRUSHES	},
	{ "models",			BSP_ELEM_MODELS	},	// dmodel_t:     6 floats + 4 ints
	{ "brushes",		BSP_ELEM_BRUSHES	},	// dbrush_t:     3 ints
	{ "brushsides",		BSP_ELEM_BRUSHSIDES	},	// dbrushside_t: 3 ints
	{ "drawverts",		BSP_ELEM_DRAWVERTS	},	// drawVert_t
	{ "drawindexes",	0	},
	{ "fogs",			BSP_ELEM_FOGS	},	// dfog_t:       char[64] + 2 ints
	{ "surfaces",		BSP_ELEM_SURFACES	},	// dsurface_t
	{ "lightmaps",		0	},
	{ "lightgrid",		0	},
	{ "visibility",		0	},
	{ "lightarray",		0	}
};


int BSP_LumpElementSize( int lump )
{
	if ( lump < 0 || lump >= BSP_CHECK_LUMPS ) {
		return 0;
	}
	return bspLumps[lump].elementSize;
}


static int BSP_ReadLong( const unsigned char *at )
{
	// Read rather than cast, and little-endian by construction. The engine
	// byte-swaps the header in place after copying it; this runs before that,
	// on the file, and must not touch it.
	const unsigned int u = (unsigned int)at[0]
		| ( (unsigned int)at[1] << 8 )
		| ( (unsigned int)at[2] << 16 )
		| ( (unsigned int)at[3] << 24 );

	return (int)u;
}


const char *BSP_CheckHeader( const unsigned char *data, size_t len )
{
	static char	message[160];
	int			i;

	if ( !data ) {
		return "no data";
	}

	if ( len < BSP_HEADER_BYTES ) {
		return "shorter than a BSP header";
	}

	if ( BSP_ReadLong( data + 0 ) != BSP_CHECK_IDENT ) {
		return "not a BSP: wrong identifier";
	}

	if ( BSP_ReadLong( data + 4 ) != BSP_CHECK_VERSION ) {
		return "wrong BSP version";
	}

	for ( i = 0; i < BSP_CHECK_LUMPS; i++ ) {
		const int		ofs = BSP_ReadLong( data + 8 + i * 8 + 0 );
		const int		length = BSP_ReadLong( data + 8 + i * 8 + 4 );
		const int		element = bspLumps[i].elementSize;
		unsigned int	end;

		if ( ofs < 0 || length < 0 ) {
			// Both are signed in the format, and a negative offset added to the
			// base pointer reads backwards from the map.
			snprintf( message, sizeof( message ),
				"the %s lump has a negative offset or length", bspLumps[i].name );
			return message;
		}

		// In unsigned, so that two values near the top of the range cannot wrap
		// past each other into something that looks small.
		end = (unsigned int)ofs + (unsigned int)length;

		if ( end < (unsigned int)ofs || (size_t)end > len ) {
			snprintf( message, sizeof( message ),
				"the %s lump ends past the end of the file", bspLumps[i].name );
			return message;
		}

		if ( element && ( length % element ) != 0 ) {
			// "funny lump size", which is what the loaders that bother to check
			// this have always called it, without saying which lump or by how
			// much.
			snprintf( message, sizeof( message ),
				"the %s lump is %i bytes, not a whole number of %i-byte entries",
				bspLumps[i].name, length, element );
			return message;
		}
	}

	return NULL;
}
