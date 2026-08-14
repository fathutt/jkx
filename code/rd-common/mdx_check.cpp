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

#include "mdx_check.h"

#include <stdio.h>


// Spelled out rather than included, so that this file depends on nothing and
// can be compiled into a test on its own. mdx_check_sizes.h asserts every one
// of them against the real definitions.
#define MDX_MD3_IDENT		( ( '3' << 24 ) + ( 'P' << 16 ) + ( 'D' << 8 ) + 'I' )
#define MDX_MD3_VERSION		15
#define MDX_MDXM_IDENT		( ( '2' << 24 ) + ( 'L' << 16 ) + ( 'G' << 8 ) + 'M' )
#define MDX_MDXM_VERSION	6
#define MDX_MDXA_IDENT		( ( '2' << 24 ) + ( 'L' << 16 ) + ( 'G' << 8 ) + 'A' )
#define MDX_MDXA_VERSION	6

#define MDX_MAX_QPATH		64

// sizeof each header, from mdx_format.h and qfiles.h.
#define MDX_MD3_HEADER		( 8 + MDX_MAX_QPATH + 4 * 9 )
#define MDX_MDXM_HEADER		( 8 + MDX_MAX_QPATH * 2 + 4 * 7 )
#define MDX_MDXA_HEADER		( 8 + MDX_MAX_QPATH + 4 + 4 * 6 )

// One entry of each top-level array, so that a count can be turned into bytes.
#define MDX_MD3_FRAME		56		// md3Frame_t
#define MDX_MDXA_SKELOFS	4		// one int per bone in mdxaSkelOffsets_t
#define MDX_MDXA_COMPBONE	14		// seven shorts
#define MDX_MDXM_LODOFS		4		// one int per LOD in mdxmLODInfo_t
#define MDX_MDXM_HIEROFS	4		// one int per surface in mdxmHierarchyOffsets_t


static char	mdxMessage[192];


static int MDX_ReadLong( const unsigned char *at )
{
	const unsigned int u = (unsigned int)at[0]
		| ( (unsigned int)at[1] << 8 )
		| ( (unsigned int)at[2] << 16 )
		| ( (unsigned int)at[3] << 24 );

	return (int)u;
}


// An offset and a byte count, both from the file, against the end of the file.
// Computed unsigned so that two large values cannot wrap past each other into
// something that looks small.
static int MDX_Fits( int ofs, long long bytes, size_t limit )
{
	if ( ofs < 0 || bytes < 0 ) {
		return 0;
	}
	if ( (long long)ofs + bytes > (long long)limit ) {
		return 0;
	}
	return 1;
}


static const char *MDX_Complain( const char *what, const char *file )
{
	snprintf( mdxMessage, sizeof( mdxMessage ), "the %s of this %s is outside the file", what, file );
	return mdxMessage;
}


static const char *MDX_CheckMDXA( const unsigned char *data, size_t len )
{
	int	version, numFrames, ofsFrames, numBones, ofsCompBonePool, ofsSkel, ofsEnd;

	if ( len < MDX_MDXA_HEADER ) {
		return "shorter than an MDXA header";
	}

	version = MDX_ReadLong( data + 4 );
	if ( version != MDX_MDXA_VERSION ) {
		return "wrong MDXA version";
	}

	// 8 + name[64] + fScale, then the six.
	numFrames		= MDX_ReadLong( data + 76 );
	ofsFrames		= MDX_ReadLong( data + 80 );
	numBones		= MDX_ReadLong( data + 84 );
	ofsCompBonePool	= MDX_ReadLong( data + 88 );
	ofsSkel			= MDX_ReadLong( data + 92 );
	ofsEnd			= MDX_ReadLong( data + 96 );

	if ( !MDX_Fits( ofsEnd, 0, len ) || ofsEnd < MDX_MDXA_HEADER ) {
		return MDX_Complain( "declared size", "skeleton" );
	}
	if ( numBones < 0 || numFrames < 0 ) {
		return "a skeleton with a negative number of bones or frames";
	}
	if ( numBones > 0xffff || numFrames > 0xffffff ) {
		// Not a limit anyone can hit honestly: the retail humanoid has 53 bones
		// and 21376 frames. This is here so that the multiplication below
		// cannot be made to overflow by a file.
		return "a skeleton with an impossible number of bones or frames";
	}

	// The frame array: three bytes of pool index per bone per frame.
	if ( !MDX_Fits( ofsFrames, (long long)numFrames * numBones * 3, (size_t)ofsEnd ) ) {
		return MDX_Complain( "frame array", "skeleton" );
	}
	// The offset table that precedes the bones, one int each.
	if ( !MDX_Fits( ofsSkel, (long long)numBones * MDX_MDXA_SKELOFS, (size_t)ofsEnd ) ) {
		return MDX_Complain( "bone table", "skeleton" );
	}
	// The compressed bone pool has no count in the header - it runs to the end
	// - so all that can be said is where it starts.
	if ( !MDX_Fits( ofsCompBonePool, MDX_MDXA_COMPBONE, (size_t)ofsEnd ) ) {
		return MDX_Complain( "bone pool", "skeleton" );
	}

	return NULL;
}


static const char *MDX_CheckMDXM( const unsigned char *data, size_t len )
{
	int	version, numBones, numLODs, ofsLODs, numSurfaces, ofsSurfHierarchy, ofsEnd;

	if ( len < MDX_MDXM_HEADER ) {
		return "shorter than an MDXM header";
	}

	version = MDX_ReadLong( data + 4 );
	if ( version != MDX_MDXM_VERSION ) {
		return "wrong MDXM version";
	}

	// 8 + name[64] + animName[64] + animIndex, then the rest.
	numBones			= MDX_ReadLong( data + 140 );
	numLODs				= MDX_ReadLong( data + 144 );
	ofsLODs				= MDX_ReadLong( data + 148 );
	numSurfaces			= MDX_ReadLong( data + 152 );
	ofsSurfHierarchy	= MDX_ReadLong( data + 156 );
	ofsEnd				= MDX_ReadLong( data + 160 );

	// This is the one that matters most. R_LoadMDXM allocates and copies
	// ofsEnd bytes out of a buffer that is len long, so a wrong number here is
	// not a bad read of the model, it is a bad read of everything after it.
	if ( !MDX_Fits( ofsEnd, 0, len ) || ofsEnd < MDX_MDXM_HEADER ) {
		return MDX_Complain( "declared size", "mesh" );
	}
	if ( numBones < 0 || numLODs < 0 || numSurfaces < 0 ) {
		return "a mesh with a negative number of bones, LODs or surfaces";
	}
	if ( numLODs > 0xffff || numSurfaces > 0xffff ) {
		return "a mesh with an impossible number of LODs or surfaces";
	}

	if ( !MDX_Fits( ofsLODs, (long long)numLODs * MDX_MDXM_LODOFS, (size_t)ofsEnd ) ) {
		return MDX_Complain( "LOD table", "mesh" );
	}
	if ( !MDX_Fits( ofsSurfHierarchy, (long long)numSurfaces * MDX_MDXM_HIEROFS, (size_t)ofsEnd ) ) {
		return MDX_Complain( "surface table", "mesh" );
	}

	return NULL;
}


static const char *MDX_CheckMD3( const unsigned char *data, size_t len )
{
	int	version, numFrames, numTags, numSurfaces, ofsFrames, ofsTags, ofsSurfaces, ofsEnd;

	if ( len < MDX_MD3_HEADER ) {
		return "shorter than an MD3 header";
	}

	version = MDX_ReadLong( data + 4 );
	if ( version != MDX_MD3_VERSION ) {
		return "wrong MD3 version";
	}

	// 8 + name[64] + flags, then the rest.
	numFrames	= MDX_ReadLong( data + 76 );
	numTags		= MDX_ReadLong( data + 80 );
	numSurfaces	= MDX_ReadLong( data + 84 );
	// data + 88 is numSkins, which nothing reads.
	ofsFrames	= MDX_ReadLong( data + 92 );
	ofsTags		= MDX_ReadLong( data + 96 );
	ofsSurfaces	= MDX_ReadLong( data + 100 );
	ofsEnd		= MDX_ReadLong( data + 104 );

	if ( !MDX_Fits( ofsEnd, 0, len ) || ofsEnd < MDX_MD3_HEADER ) {
		return MDX_Complain( "declared size", "model" );
	}
	if ( numFrames < 0 || numTags < 0 || numSurfaces < 0 ) {
		return "a model with a negative number of frames, tags or surfaces";
	}
	if ( numFrames > 0xffff || numTags > 0xffff || numSurfaces > 0xffff ) {
		return "a model with an impossible number of frames, tags or surfaces";
	}
	if ( numFrames < 1 ) {
		return "a model with no frames";
	}

	if ( !MDX_Fits( ofsFrames, (long long)numFrames * MDX_MD3_FRAME, (size_t)ofsEnd ) ) {
		return MDX_Complain( "frame array", "model" );
	}
	// Tags are numTags per frame, which is the part that is easy to forget.
	if ( !MDX_Fits( ofsTags, (long long)numTags * numFrames * ( MDX_MAX_QPATH + 12 * 4 ), (size_t)ofsEnd ) ) {
		return MDX_Complain( "tag array", "model" );
	}
	if ( numSurfaces > 0 && !MDX_Fits( ofsSurfaces, 1, (size_t)ofsEnd ) ) {
		return MDX_Complain( "surface list", "model" );
	}

	return NULL;
}


const char *MDX_CheckHeader( const unsigned char *data, size_t len )
{
	int	ident;

	if ( !data ) {
		return "no data";
	}

	// R_RegisterMD3 reads the identifier out of the buffer before anything has
	// established that the buffer has four bytes in it. A file of one byte is
	// a three byte over-read on the first thing done with it.
	if ( len < 4 ) {
		return "too short to hold an identifier";
	}

	ident = MDX_ReadLong( data );

	if ( ident == MDX_MDXA_IDENT ) {
		return MDX_CheckMDXA( data, len );
	}
	if ( ident == MDX_MDXM_IDENT ) {
		return MDX_CheckMDXM( data, len );
	}
	if ( ident == MDX_MD3_IDENT ) {
		return MDX_CheckMD3( data, len );
	}

	// Not a model this knows. Deciding what that means is the caller's job.
	return NULL;
}
