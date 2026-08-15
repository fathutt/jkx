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

// The second pass needs the sizes of the records the first pass only counted.
#define MDX_MDXM_LOD		4		// mdxmLOD_t: one offset to the next LOD
#define MDX_MDXM_SURF		( 4 * 10 )	// mdxmSurface_t, ten ints
#define MDX_MDXM_VERT		32		// mdxmVertex_t, kept at 32 for cache lines
#define MDX_MDXM_TEXCOORD	8		// mdxmVertexTexCoord_t, two floats
#define MDX_MDXM_TRI		12		// mdxmTriangle_t, three indices
// mdxmSurfHierarchy_t up to childIndexes: name, flags, shader, shaderIndex,
// parentIndex, numChildren.
#define MDX_MDXM_HIER		( MDX_MAX_QPATH + 4 + MDX_MAX_QPATH + 4 + 4 + 4 )
// mdxaSkel_t up to children: name, flags, parent, two 3x4 matrices, numChildren.
#define MDX_MDXA_SKEL		( MDX_MAX_QPATH + 4 + 4 + 48 + 48 + 4 )

// MD3's records, all measured rather than counted by eye.
#define MDX_MD3_SURF		108		// md3Surface_t
#define MDX_MD3_SHADER		68		// md3Shader_t: a name and an index
#define MDX_MD3_TRI			12		// md3Triangle_t
#define MDX_MD3_ST			8		// md3St_t, two floats
#define MDX_MD3_XYZ			8		// md3XyzNormal_t, four shorts
#define MDX_MD3_TAG			112		// md3Tag_t

// The loader's own ceilings, from qfiles.h. R_LoadMDXM refuses a surface past
// them, so a checker that allowed more would be answering a different question
// than the code it protects.
#define MDX_MAX_VERTEXES	1000
#define MDX_MAX_INDEXES		( 6 * MDX_MAX_VERTEXES )


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


/*
=================================================================

The second pass.

The first pass asks whether the top-level arrays are inside the file. That is
not the same question as whether the loader can walk them, because both formats
nest: an MDXM is a list of LODs, each of which is a list of surfaces, each of
which carries three more offsets of its own; an MDXA is a table of bone
offsets and a frame array whose entries are indices into a pool. Every one of
those is a number out of the file used as a position, and none was compared
against anything.

Kept separate from the first pass on purpose. Until the top-level arrays are
known to be inside the file there is nothing safe to walk here, so this runs
second and assumes what the first established - in particular that ofsEnd is
inside the buffer, which is why everything below bounds against ofsEnd rather
than against len.

=================================================================
*/


static const char *MDX_ComplainAt( const char *what, int which )
{
	snprintf( mdxMessage, sizeof( mdxMessage ),
		"the %s of %i is outside the file", what, which );
	return mdxMessage;
}


// One surface of one LOD. 'at' is where the surface starts, 'room' is what is
// left of the file from there; the surface's own offsets are relative to it.
static const char *MDX_CheckMDXMSurface( const unsigned char *data, int at,
	int room, int numSurfaces, int numBones, int *surfaceBytes )
{
	int	thisSurfaceIndex, numVerts, ofsVerts, numTriangles, ofsTriangles;
	int	numBoneReferences, ofsBoneReferences, ofsEnd, i;

	if ( room < MDX_MDXM_SURF ) {
		return "a surface that does not have room for its own header";
	}

	thisSurfaceIndex	= MDX_ReadLong( data + at + 4 );
	numVerts			= MDX_ReadLong( data + at + 12 );
	ofsVerts			= MDX_ReadLong( data + at + 16 );
	numTriangles		= MDX_ReadLong( data + at + 20 );
	ofsTriangles		= MDX_ReadLong( data + at + 24 );
	numBoneReferences	= MDX_ReadLong( data + at + 28 );
	ofsBoneReferences	= MDX_ReadLong( data + at + 32 );
	ofsEnd				= MDX_ReadLong( data + at + 36 );

	if ( thisSurfaceIndex < 0 || thisSurfaceIndex >= numSurfaces ) {
		return "a surface that says it is not one of this model's surfaces";
	}

	// The loader's ceilings, checked here rather than there because there they
	// are checked after the offsets above have already been used.
	if ( numVerts < 0 || numVerts > MDX_MAX_VERTEXES ) {
		return "a surface with more vertices than the renderer can hold";
	}
	if ( numTriangles < 0 || numTriangles > MDX_MAX_INDEXES / 3 ) {
		return "a surface with more triangles than the renderer can hold";
	}
	if ( numBoneReferences < 0 || numBoneReferences > numBones ) {
		return "a surface referring to more bones than the skeleton has";
	}

	if ( ofsEnd < MDX_MDXM_SURF || ofsEnd > room ) {
		return "a surface that ends outside the file";
	}

	// Everything a surface points at is inside the surface, so ofsEnd is the
	// limit rather than the file. The texture coordinates are a second array
	// immediately after the vertices and have no offset of their own - the
	// loader finds them at &verts[numVerts], so their room has to be counted
	// here or a file can hide numVerts * 8 bytes past the end of the block.
	if ( !MDX_Fits( ofsVerts, (long long)numVerts * ( MDX_MDXM_VERT + MDX_MDXM_TEXCOORD ),
			(size_t)ofsEnd ) ) {
		return "a surface whose vertices run past its end";
	}
	if ( !MDX_Fits( ofsTriangles, (long long)numTriangles * MDX_MDXM_TRI, (size_t)ofsEnd ) ) {
		return "a surface whose triangles run past its end";
	}
	if ( !MDX_Fits( ofsBoneReferences, (long long)numBoneReferences * 4, (size_t)ofsEnd ) ) {
		return "a surface whose bone references run past its end";
	}

	// The bone references are indices into the skeleton, and G2_TransformGhoulBones
	// uses them to index the transformed bone array without looking.
	for ( i = 0; i < numBoneReferences; i++ ) {
		const int bone = MDX_ReadLong( data + at + ofsBoneReferences + i * 4 );

		if ( bone < 0 || bone >= numBones ) {
			return "a surface referring to a bone the skeleton does not have";
		}
	}

	*surfaceBytes = ofsEnd;
	return NULL;
}


static const char *MDX_CheckMDXMDeep( const unsigned char *data )
{
	const int	numBones			= MDX_ReadLong( data + 140 );
	const int	numLODs				= MDX_ReadLong( data + 144 );
	const int	ofsLODs				= MDX_ReadLong( data + 148 );
	const int	numSurfaces			= MDX_ReadLong( data + 152 );
	const int	ofsSurfHierarchy	= MDX_ReadLong( data + 156 );
	const int	ofsEnd				= MDX_ReadLong( data + 160 );
	int			i, l, at;

	// The surface hierarchy. Entries are variable sized and walked by adding
	// their own numChildren, so a single wrong count moves every entry after it.
	at = ofsSurfHierarchy;
	for ( i = 0; i < numSurfaces; i++ ) {
		int	numChildren, parentIndex, j;

		if ( !MDX_Fits( at, MDX_MDXM_HIER, (size_t)ofsEnd ) ) {
			return MDX_ComplainAt( "surface hierarchy entry", i );
		}

		parentIndex	= MDX_ReadLong( data + at + MDX_MAX_QPATH + 4 + MDX_MAX_QPATH + 4 );
		numChildren	= MDX_ReadLong( data + at + MDX_MAX_QPATH + 4 + MDX_MAX_QPATH + 8 );

		if ( numChildren < 0 || numChildren > numSurfaces ) {
			return "a surface with an impossible number of children";
		}
		if ( parentIndex < -1 || parentIndex >= numSurfaces ) {
			return "a surface whose parent is not one of this model's surfaces";
		}
		if ( !MDX_Fits( at, (long long)MDX_MDXM_HIER + (long long)numChildren * 4,
				(size_t)ofsEnd ) ) {
			return MDX_ComplainAt( "children of surface", i );
		}
		for ( j = 0; j < numChildren; j++ ) {
			const int child = MDX_ReadLong( data + at + MDX_MDXM_HIER + j * 4 );

			if ( child < 0 || child >= numSurfaces ) {
				return "a surface whose child is not one of this model's surfaces";
			}
		}

		at += MDX_MDXM_HIER + numChildren * 4;
	}

	// The LODs. Each carries the offset to the next, so the walk is the file's
	// arithmetic rather than ours, and a zero would loop for ever.
	at = ofsLODs;
	for ( l = 0; l < numLODs; l++ ) {
		int	lodEnd, surfAt;

		if ( !MDX_Fits( at, MDX_MDXM_LOD, (size_t)ofsEnd ) ) {
			return MDX_ComplainAt( "LOD", l );
		}

		lodEnd = MDX_ReadLong( data + at );

		// The surfaces of a LOD start after its header and after the table of
		// per-surface offsets, which is what the loader does.
		surfAt = at + MDX_MDXM_LOD + numSurfaces * MDX_MDXM_LODOFS;

		if ( lodEnd <= MDX_MDXM_LOD || !MDX_Fits( at, lodEnd, (size_t)ofsEnd ) ) {
			return MDX_ComplainAt( "end of LOD", l );
		}
		if ( surfAt > at + lodEnd ) {
			return MDX_ComplainAt( "surface table of LOD", l );
		}

		for ( i = 0; i < numSurfaces; i++ ) {
			int			bytes = 0;
			const char	*bad = MDX_CheckMDXMSurface( data, surfAt,
							at + lodEnd - surfAt, numSurfaces, numBones, &bytes );

			if ( bad ) {
				return bad;
			}
			surfAt += bytes;
		}

		at += lodEnd;
	}

	return NULL;
}


static const char *MDX_CheckMDXADeep( const unsigned char *data )
{
	const int	numFrames		= MDX_ReadLong( data + 76 );
	const int	ofsFrames		= MDX_ReadLong( data + 80 );
	const int	numBones		= MDX_ReadLong( data + 84 );
	const int	ofsCompBonePool	= MDX_ReadLong( data + 88 );
	const int	ofsSkel			= MDX_ReadLong( data + 92 );
	const int	ofsEnd			= MDX_ReadLong( data + 96 );
	long long	poolBytes;
	long long	poolEntries;
	long long	i;
	int			b;

	// The bone table: one offset per bone, relative to ofsSkel, each pointing
	// at a variable-sized mdxaSkel_t.
	for ( b = 0; b < numBones; b++ ) {
		int	at, numChildren, parent, j;

		at = MDX_ReadLong( data + ofsSkel + b * MDX_MDXA_SKELOFS );
		if ( at < 0 ) {
			return MDX_ComplainAt( "bone", b );
		}
		at += ofsSkel;

		if ( !MDX_Fits( at, MDX_MDXA_SKEL, (size_t)ofsEnd ) ) {
			return MDX_ComplainAt( "bone", b );
		}

		parent		= MDX_ReadLong( data + at + MDX_MAX_QPATH + 4 );
		numChildren	= MDX_ReadLong( data + at + MDX_MDXA_SKEL - 4 );

		if ( parent < -1 || parent >= numBones ) {
			return "a bone whose parent is not one of this skeleton's bones";
		}
		if ( numChildren < 0 || numChildren > numBones ) {
			return "a bone with an impossible number of children";
		}
		if ( !MDX_Fits( at, (long long)MDX_MDXA_SKEL + (long long)numChildren * 4,
				(size_t)ofsEnd ) ) {
			return MDX_ComplainAt( "children of bone", b );
		}
		for ( j = 0; j < numChildren; j++ ) {
			const int child = MDX_ReadLong( data + at + MDX_MDXA_SKEL + j * 4 );

			if ( child < 0 || child >= numBones ) {
				return "a bone whose child is not one of this skeleton's bones";
			}
		}
	}

	// The frame array is three-byte indices into the compressed bone pool, and
	// the pool has no count of its own - it runs from ofsCompBonePool to the
	// end of the file. G2_TimingModel reads pool[index] every frame for every
	// bone without looking at either end, so an index one too large is a read
	// past the model on every frame of an animation that plays.
	poolBytes = (long long)ofsEnd - ofsCompBonePool;
	if ( poolBytes < 0 ) {
		return MDX_Complain( "bone pool", "skeleton" );
	}
	poolEntries = poolBytes / MDX_MDXA_COMPBONE;

	for ( i = 0; i < (long long)numFrames * numBones; i++ ) {
		const unsigned char	*at = data + ofsFrames + i * 3;
		const long long		index = (long long)at[0]
			| ( (long long)at[1] << 8 )
			| ( (long long)at[2] << 16 );

		if ( index >= poolEntries ) {
			snprintf( mdxMessage, sizeof( mdxMessage ),
				"a frame asking for compressed bone %lld of %lld",
				index, poolEntries );
			return mdxMessage;
		}
	}

	return NULL;
}


static const char *MDX_CheckMD3Deep( const unsigned char *data )
{
	const int	numFrames	= MDX_ReadLong( data + 76 );
	const int	numSurfaces	= MDX_ReadLong( data + 84 );
	const int	ofsSurfaces	= MDX_ReadLong( data + 100 );
	const int	ofsEnd		= MDX_ReadLong( data + 104 );
	int			at, i;

	at = ofsSurfaces;
	for ( i = 0; i < numSurfaces; i++ ) {
		int	surfFrames, numShaders, numVerts, numTriangles;
		int	ofsTriangles, ofsShaders, ofsSt, ofsXyzNormals, surfEnd, j;

		if ( !MDX_Fits( at, MDX_MD3_SURF, (size_t)ofsEnd ) ) {
			return MDX_ComplainAt( "surface", i );
		}

		surfFrames		= MDX_ReadLong( data + at + 72 );
		numShaders		= MDX_ReadLong( data + at + 76 );
		numVerts		= MDX_ReadLong( data + at + 80 );
		numTriangles	= MDX_ReadLong( data + at + 84 );
		ofsTriangles	= MDX_ReadLong( data + at + 88 );
		ofsShaders		= MDX_ReadLong( data + at + 92 );
		ofsSt			= MDX_ReadLong( data + at + 96 );
		ofsXyzNormals	= MDX_ReadLong( data + at + 100 );
		surfEnd			= MDX_ReadLong( data + at + 104 );

		// The loader's ceilings, and it uses >= rather than >, so this does
		// too: a checker that accepted the boundary would be handing the
		// loader a file it is about to refuse, which is a different answer
		// from the one this gives.
		if ( numVerts < 0 || numVerts >= MDX_MAX_VERTEXES ) {
			return "a surface with more vertices than the renderer can hold";
		}
		// The multiplication is widened deliberately: numTriangles is a number
		// out of the file, and (int)838860803 * 3 overflows - which is signed
		// overflow, undefined, and was caught here by ubsan on the first run of
		// the mutation loop rather than by reading the line.
		if ( numTriangles < 0 || (long long)numTriangles * 3 >= MDX_MAX_INDEXES ) {
			return "a surface with more triangles than the renderer can hold";
		}
		if ( numShaders < 0 || numShaders > 0xffff ) {
			return "a surface with an impossible number of shaders";
		}

		// Every surface carries its own frame count and the vertex array is
		// sized by it, but the renderer indexes that array with the MODEL's
		// frame number. A surface claiming fewer frames than the model is
		// therefore a read past the array on every frame past its end - and
		// the format's own comment says the two "should" agree, which is the
		// kind of should that nothing checks.
		if ( surfFrames != numFrames ) {
			return "a surface with a different number of frames from its model";
		}

		if ( surfEnd < MDX_MD3_SURF || !MDX_Fits( at, surfEnd, (size_t)ofsEnd ) ) {
			return MDX_ComplainAt( "end of surface", i );
		}

		// Everything a surface points at is inside the surface, so its own end
		// is the limit rather than the file's.
		if ( !MDX_Fits( ofsShaders, (long long)numShaders * MDX_MD3_SHADER, (size_t)surfEnd ) ) {
			return "a surface whose shaders run past its end";
		}
		if ( !MDX_Fits( ofsTriangles, (long long)numTriangles * MDX_MD3_TRI, (size_t)surfEnd ) ) {
			return "a surface whose triangles run past its end";
		}
		if ( !MDX_Fits( ofsSt, (long long)numVerts * MDX_MD3_ST, (size_t)surfEnd ) ) {
			return "a surface whose texture coordinates run past its end";
		}
		if ( !MDX_Fits( ofsXyzNormals,
				(long long)numVerts * numFrames * MDX_MD3_XYZ, (size_t)surfEnd ) ) {
			return "a surface whose vertices run past its end";
		}

		// The indices are used to index the vertex array, and the renderer
		// does not look at them either.
		for ( j = 0; j < numTriangles * 3; j++ ) {
			const int index = MDX_ReadLong( data + at + ofsTriangles + j * 4 );

			if ( index < 0 || index >= numVerts ) {
				return "a triangle pointing at a vertex the surface does not have";
			}
		}

		at += surfEnd;
	}

	return NULL;
}


const char *MDX_CheckModel( const unsigned char *data, size_t len )
{
	const char	*bad = MDX_CheckHeader( data, len );
	int			ident;

	if ( bad ) {
		return bad;
	}
	if ( !data || len < 4 ) {
		return NULL;
	}

	ident = MDX_ReadLong( data );

	if ( ident == MDX_MDXM_IDENT ) {
		return MDX_CheckMDXMDeep( data );
	}
	if ( ident == MDX_MDXA_IDENT ) {
		return MDX_CheckMDXADeep( data );
	}

	if ( ident == MDX_MD3_IDENT ) {
		return MDX_CheckMD3Deep( data );
	}

	return NULL;
}
