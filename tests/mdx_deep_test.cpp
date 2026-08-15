/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// The second pass over a model file: every LOD, every surface, every bone.
//
// mdx_header_test.cpp covers the first pass - the header and the top-level
// arrays. This one covers what is inside them, and the difference matters
// because both formats nest. An MDXM is a list of LODs, each a list of
// surfaces, each carrying three more offsets and three more counts of its own;
// an MDXA is a table of bone offsets plus a frame array of indices into a pool
// with no length. Every one of those is a number out of a file used as a
// position, and the loaders walk all of them without a single comparison.
//
// Two halves, and the first is what makes the second worth anything:
//
//   1. Build models that are valid and require them to be accepted. Without
//      this, a checker that refused everything would pass every safety check
//      below - and refusing every model is not a small bug, it is a game that
//      draws no characters.
//   2. Mutate those models and require that what comes back is either an
//      acceptance or a message, with nothing read outside the buffer. Under
//      asan, with the buffer sized exactly, that is the whole claim.
//
// What the deliberate breakages showed, and it is not what the earlier passes
// showed, so it is worth stating rather than implying. Three guards were
// removed one at a time and forty thousand mutants run against each: the
// hierarchy entry bound, the children bound, and the surface's own end. None
// of the three produced a read outside the buffer.
//
// That is not the checker being untested, it is the shape of the thing. Every
// read here is bounded by ofsEnd, and the first pass has already established
// that ofsEnd is inside the buffer - so the deep pass cannot walk off the end
// whatever the file says. What these guards protect is the LOADER, which has
// no such wall: it is handed a pointer and walks the same offsets with nothing
// to compare them against. A checker that let a bad surface through would not
// crash here, it would crash there, and no test in this repository can reach
// that. So the guards are held to a weaker standard honestly stated: each is
// shown refusing a model that is wrong in exactly one named way, below.
//
// The builders are here rather than shared with the bench's make_test_glm.py
// on purpose: this test has to be able to produce a model that is wrong in one
// stated way, which a generator built to produce correct output cannot do.

#include "../code/rd-common/mdx_check.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{

int g_failures = 0;
int g_checks = 0;

void check( bool condition, const char *what )
{
	g_checks++;
	if ( !condition ) {
		g_failures++;
		printf( "FAIL: %s\n", what );
	}
}

unsigned int nextRandom( unsigned int *state )
{
	*state = *state * 1664525u + 1013904223u;
	return ( *state >> 8 );
}


const int MAX_QPATH = 64;

const int MDXM_IDENT = ( '2' << 24 ) + ( 'L' << 16 ) + ( 'G' << 8 ) + 'M';
const int MDXA_IDENT = ( '2' << 24 ) + ( 'L' << 16 ) + ( 'G' << 8 ) + 'A';
const int MDXM_VERSION = 6;
const int MDXA_VERSION = 6;


void putLong( std::vector<unsigned char> &out, int v )
{
	const unsigned int u = (unsigned int)v;

	out.push_back( (unsigned char)( u & 0xff ) );
	out.push_back( (unsigned char)( ( u >> 8 ) & 0xff ) );
	out.push_back( (unsigned char)( ( u >> 16 ) & 0xff ) );
	out.push_back( (unsigned char)( ( u >> 24 ) & 0xff ) );
}

void setLong( std::vector<unsigned char> &out, size_t at, int v )
{
	const unsigned int u = (unsigned int)v;

	out[at + 0] = (unsigned char)( u & 0xff );
	out[at + 1] = (unsigned char)( ( u >> 8 ) & 0xff );
	out[at + 2] = (unsigned char)( ( u >> 16 ) & 0xff );
	out[at + 3] = (unsigned char)( ( u >> 24 ) & 0xff );
}

void putName( std::vector<unsigned char> &out, const char *name )
{
	for ( int i = 0; i < MAX_QPATH; i++ ) {
		out.push_back( (unsigned char)( name[i] ? name[i] : 0 ) );
		if ( !name[i] ) {
			for ( int j = i + 1; j < MAX_QPATH; j++ ) {
				out.push_back( 0 );
			}
			return;
		}
	}
}

void pad( std::vector<unsigned char> &out, size_t bytes )
{
	for ( size_t i = 0; i < bytes; i++ ) {
		out.push_back( 0 );
	}
}


// A mesh: numSurfaces surfaces, numLODs levels, and every surface carrying a
// few vertices and triangles. Small enough to read in a hex dump and shaped
// exactly like the real thing - which is the point, because the checker's
// arithmetic is the loader's arithmetic.
std::vector<unsigned char> buildMDXM( int numSurfaces, int numLODs, int numBones,
	int numVerts, int numTris )
{
	std::vector<unsigned char>	m;

	putLong( m, MDXM_IDENT );
	putLong( m, MDXM_VERSION );
	putName( m, "models/players/test.glm" );
	putName( m, "models/players/test" );
	putLong( m, 0 );			// animIndex
	putLong( m, numBones );
	putLong( m, numLODs );
	const size_t ofsLODsAt = m.size();
	putLong( m, 0 );			// ofsLODs, filled in below
	putLong( m, numSurfaces );
	const size_t ofsHierAt = m.size();
	putLong( m, 0 );			// ofsSurfHierarchy
	const size_t ofsEndAt = m.size();
	putLong( m, 0 );			// ofsEnd

	// The offsets table the loader expects immediately after the header, one
	// int per surface, relative to the table itself.
	const size_t hierTableAt = m.size();
	for ( int i = 0; i < numSurfaces; i++ ) {
		putLong( m, 0 );
	}

	// The hierarchy entries. One child each except the last, so that the
	// variable-sized walk is actually exercised: a fixed-size one would pass
	// with the wrong stride.
	//
	// ofsSurfHierarchy points at the FIRST ENTRY, not at the table of offsets
	// above it - the table sits at sizeof(mdxmHeader_t) and its entries are
	// relative to itself. Written the other way round first, and the round trip
	// caught it on the first run: the checker read the table as an entry and
	// reported an impossible number of children, which is exactly what it was.
	setLong( m, ofsHierAt, (int)m.size() );
	for ( int i = 0; i < numSurfaces; i++ ) {
		const int	numChildren = ( i + 1 < numSurfaces ) ? 1 : 0;

		setLong( m, hierTableAt + i * 4, (int)( m.size() - hierTableAt ) );
		putName( m, "surface" );
		putLong( m, 0 );					// flags
		putName( m, "models/test/skin" );
		putLong( m, 0 );					// shaderIndex
		putLong( m, ( i == 0 ) ? -1 : 0 );	// parentIndex
		putLong( m, numChildren );
		for ( int c = 0; c < numChildren; c++ ) {
			putLong( m, i + 1 );
		}
	}

	setLong( m, ofsLODsAt, (int)m.size() );
	for ( int l = 0; l < numLODs; l++ ) {
		const size_t	lodAt = m.size();

		putLong( m, 0 );					// ofsEnd of this LOD, below
		const size_t	surfTableAt = m.size();
		for ( int i = 0; i < numSurfaces; i++ ) {
			putLong( m, 0 );
		}

		for ( int i = 0; i < numSurfaces; i++ ) {
			const size_t	surfAt = m.size();

			setLong( m, surfTableAt + i * 4, (int)( surfAt - lodAt ) );

			putLong( m, 0 );				// ident
			putLong( m, i );				// thisSurfaceIndex
			putLong( m, -(int)( surfAt ) );	// ofsHeader, back to the model
			putLong( m, numVerts );
			const size_t ofsVertsAt = m.size();
			putLong( m, 0 );
			putLong( m, numTris );
			const size_t ofsTrisAt = m.size();
			putLong( m, 0 );
			putLong( m, numBones );			// numBoneReferences
			const size_t ofsBonesAt = m.size();
			putLong( m, 0 );
			const size_t ofsSurfEndAt = m.size();
			putLong( m, 0 );

			setLong( m, ofsTrisAt, (int)( m.size() - surfAt ) );
			for ( int t = 0; t < numTris; t++ ) {
				putLong( m, 0 );
				putLong( m, ( numVerts > 1 ) ? 1 : 0 );
				putLong( m, ( numVerts > 2 ) ? 2 : 0 );
			}

			setLong( m, ofsVertsAt, (int)( m.size() - surfAt ) );
			pad( m, (size_t)numVerts * 32 );		// mdxmVertex_t
			pad( m, (size_t)numVerts * 8 );			// texture coordinates

			setLong( m, ofsBonesAt, (int)( m.size() - surfAt ) );
			for ( int b = 0; b < numBones; b++ ) {
				putLong( m, b );
			}

			setLong( m, ofsSurfEndAt, (int)( m.size() - surfAt ) );
		}

		setLong( m, lodAt, (int)( m.size() - lodAt ) );
	}

	setLong( m, ofsEndAt, (int)m.size() );
	return m;
}


// A skeleton: numBones bones with a base pose each, numFrames frames of
// indices, and a pool big enough for the indices that are written.
std::vector<unsigned char> buildMDXA( int numBones, int numFrames, int poolEntries )
{
	std::vector<unsigned char>	m;

	putLong( m, MDXA_IDENT );
	putLong( m, MDXA_VERSION );
	putName( m, "models/players/test" );
	putLong( m, 0 );				// fScale, a float of zero
	putLong( m, numFrames );
	const size_t ofsFramesAt = m.size();
	putLong( m, 0 );
	putLong( m, numBones );
	const size_t ofsPoolAt = m.size();
	putLong( m, 0 );
	const size_t ofsSkelAt = m.size();
	putLong( m, 0 );
	const size_t ofsEndAt = m.size();
	putLong( m, 0 );

	// The bone table, then the bones.
	setLong( m, ofsSkelAt, (int)m.size() );
	const size_t skelTableAt = m.size();
	for ( int b = 0; b < numBones; b++ ) {
		putLong( m, 0 );
	}
	for ( int b = 0; b < numBones; b++ ) {
		const int	numChildren = ( b + 1 < numBones ) ? 1 : 0;

		setLong( m, skelTableAt + b * 4, (int)( m.size() - skelTableAt ) );
		putName( m, "bone" );
		putLong( m, 0 );					// flags
		putLong( m, ( b == 0 ) ? -1 : 0 );	// parent
		pad( m, 48 );						// BasePoseMat
		pad( m, 48 );						// BasePoseMatInv
		putLong( m, numChildren );
		for ( int c = 0; c < numChildren; c++ ) {
			putLong( m, b + 1 );
		}
	}

	// The frames: three bytes of pool index per bone per frame, spread over
	// the pool so that the check has something to compare.
	setLong( m, ofsFramesAt, (int)m.size() );
	for ( int f = 0; f < numFrames; f++ ) {
		for ( int b = 0; b < numBones; b++ ) {
			const int index = ( poolEntries > 0 ) ? ( ( f + b ) % poolEntries ) : 0;

			m.push_back( (unsigned char)( index & 0xff ) );
			m.push_back( (unsigned char)( ( index >> 8 ) & 0xff ) );
			m.push_back( (unsigned char)( ( index >> 16 ) & 0xff ) );
		}
	}

	// The pool runs to the end of the file, which is what gives it its length.
	setLong( m, ofsPoolAt, (int)m.size() );
	pad( m, (size_t)poolEntries * 14 );

	setLong( m, ofsEndAt, (int)m.size() );
	return m;
}


bool accepted( const std::vector<unsigned char> &m, const char **why )
{
	// Exactly the size of the model, from the heap: a byte either side is a
	// fault, and the checker has nothing to go on but the length it is told.
	std::vector<unsigned char>	exact( m );
	const char					*bad = MDX_CheckModel( exact.data(), exact.size() );

	if ( why ) {
		*why = bad;
	}
	return bad == NULL;
}


void testValidModelsAreAccepted()
{
	const char	*why = NULL;

	check( accepted( buildMDXM( 1, 1, 1, 3, 1 ), &why ),
		"the smallest mesh is accepted" );
	if ( why ) {
		printf( "  the smallest mesh was refused: %s\n", why );
	}

	check( accepted( buildMDXM( 4, 3, 8, 12, 6 ), &why ),
		"a mesh with several surfaces and LODs is accepted" );
	if ( why ) {
		printf( "  the larger mesh was refused: %s\n", why );
	}

	check( accepted( buildMDXA( 1, 1, 1 ), &why ),
		"the smallest skeleton is accepted" );
	if ( why ) {
		printf( "  the smallest skeleton was refused: %s\n", why );
	}

	check( accepted( buildMDXA( 53, 40, 200 ), &why ),
		"a skeleton the shape of the retail humanoid is accepted" );
	if ( why ) {
		printf( "  the humanoid-shaped skeleton was refused: %s\n", why );
	}
}


// The defects this pass exists for, each stated as one wrong number in an
// otherwise valid model.
void testNamedDefects()
{
	// A surface whose vertices run past the end of its own block. The loader
	// reads numVerts * 32 bytes from there and then numVerts * 8 more for the
	// texture coordinates, which have no offset of their own.
	{
		std::vector<unsigned char>	m = buildMDXM( 1, 1, 1, 3, 1 );
		// numVerts of the first surface: header is 168, then the hierarchy,
		// then the LOD. Rather than compute it, find the surface by its
		// ofsHeader field, which is the only negative number in the file.
		size_t	surfAt = 0;

		for ( size_t i = 168; i + 4 <= m.size(); i += 4 ) {
			const int v = (int)( (unsigned int)m[i] | ( (unsigned int)m[i + 1] << 8 )
				| ( (unsigned int)m[i + 2] << 16 ) | ( (unsigned int)m[i + 3] << 24 ) );

			if ( v < 0 && i >= 8 ) {
				surfAt = i - 8;
				break;
			}
		}
		check( surfAt != 0, "the test can find the surface it means to break" );
		if ( surfAt ) {
			setLong( m, surfAt + 12, 900 );		// numVerts, room for three
			check( !accepted( m, NULL ),
				"a surface whose vertices run past its end is refused" );
		}
	}

	// More vertices than the renderer's own ceiling. R_LoadMDXM checks this
	// too - after it has used the offsets.
	{
		std::vector<unsigned char>	m = buildMDXM( 1, 1, 1, 3, 1 );
		size_t						surfAt = 0;

		for ( size_t i = 168; i + 4 <= m.size(); i += 4 ) {
			const int v = (int)( (unsigned int)m[i] | ( (unsigned int)m[i + 1] << 8 )
				| ( (unsigned int)m[i + 2] << 16 ) | ( (unsigned int)m[i + 3] << 24 ) );

			if ( v < 0 && i >= 8 ) {
				surfAt = i - 8;
				break;
			}
		}
		if ( surfAt ) {
			setLong( m, surfAt + 12, 100000 );
			check( !accepted( m, NULL ),
				"a surface with more vertices than the renderer can hold is refused" );
		}
	}

	// A frame index past the end of the compressed bone pool. Nothing in the
	// engine looks at this, and it is read once per bone per frame for as long
	// as the animation plays.
	{
		std::vector<unsigned char>	m = buildMDXA( 2, 2, 4 );
		const int					ofsFrames = (int)( (unsigned int)m[80]
			| ( (unsigned int)m[81] << 8 ) | ( (unsigned int)m[82] << 16 )
			| ( (unsigned int)m[83] << 24 ) );

		check( accepted( m, NULL ), "the skeleton is valid before it is broken" );
		m[ofsFrames + 0] = 0xff;
		m[ofsFrames + 1] = 0xff;
		m[ofsFrames + 2] = 0x00;
		check( !accepted( m, NULL ),
			"a frame index past the end of the bone pool is refused" );
	}

	// A bone whose parent is not a bone of this skeleton.
	{
		std::vector<unsigned char>	m = buildMDXA( 4, 2, 8 );
		const int					ofsSkel = (int)( (unsigned int)m[92]
			| ( (unsigned int)m[93] << 8 ) | ( (unsigned int)m[94] << 16 )
			| ( (unsigned int)m[95] << 24 ) );
		const int					first = (int)( (unsigned int)m[ofsSkel]
			| ( (unsigned int)m[ofsSkel + 1] << 8 )
			| ( (unsigned int)m[ofsSkel + 2] << 16 )
			| ( (unsigned int)m[ofsSkel + 3] << 24 ) );

		setLong( m, (size_t)ofsSkel + first + MAX_QPATH + 4, 9999 );
		check( !accepted( m, NULL ),
			"a bone whose parent is not one of this skeleton's is refused" );
	}

	// A LOD that says it ends where it began. The walk adds this to get to the
	// next LOD, so zero is a loop that never advances.
	{
		std::vector<unsigned char>	m = buildMDXM( 1, 2, 1, 3, 1 );
		const int					ofsLODs = (int)( (unsigned int)m[148]
			| ( (unsigned int)m[149] << 8 ) | ( (unsigned int)m[150] << 16 )
			| ( (unsigned int)m[151] << 24 ) );

		setLong( m, (size_t)ofsLODs, 0 );
		check( !accepted( m, NULL ), "a LOD of no length is refused" );
	}
}


void testMutations( int rounds )
{
	int	acceptedCount = 0, refused = 0;

	for ( int round = 0; round < rounds; round++ ) {
		unsigned int				state = (unsigned int)round * 2654435761u + 11u;
		std::vector<unsigned char>	m;

		// Alternate between the two formats, and vary the shape so that the
		// mutations land on different fields from round to round.
		if ( ( round & 1 ) == 0 ) {
			m = buildMDXM( 1 + (int)( nextRandom( &state ) % 4 ),
				1 + (int)( nextRandom( &state ) % 3 ),
				1 + (int)( nextRandom( &state ) % 8 ),
				(int)( nextRandom( &state ) % 20 ),
				(int)( nextRandom( &state ) % 10 ) );
		} else {
			m = buildMDXA( 1 + (int)( nextRandom( &state ) % 20 ),
				1 + (int)( nextRandom( &state ) % 30 ),
				1 + (int)( nextRandom( &state ) % 50 ) );
		}

		const int edits = 1 + (int)( nextRandom( &state ) % 3 );

		for ( int e = 0; e < edits; e++ ) {
			const unsigned int what = nextRandom( &state ) % 4;

			if ( what == 0 && m.size() > 8 ) {
				// Truncate: the length the checker is told shrinks, and every
				// offset in the file now points past it.
				m.resize( 8 + nextRandom( &state ) % ( m.size() - 8 ) );
			} else if ( what == 1 && m.size() > 4 ) {
				// A whole field, which is how a real corruption looks: a
				// plausible number in the wrong place.
				const size_t at = ( nextRandom( &state ) % ( m.size() / 4 ) ) * 4;

				setLong( m, at, (int)nextRandom( &state ) );
			} else if ( !m.empty() ) {
				m[ nextRandom( &state ) % m.size() ] = (unsigned char)nextRandom( &state );
			}
		}

		if ( accepted( m, NULL ) ) {
			acceptedCount++;
		} else {
			refused++;
		}
	}

	printf( "  %i mutated model(s): %i refused, %i still accepted\n",
		rounds, refused, acceptedCount );

	// The number that makes the rest mean something. A checker that answers
	// "no" to everything cannot read out of bounds and would pass every check
	// above; this says a fair share of the mutants got all the way through the
	// walk, which is where an out-of-bounds read would happen.
	check( acceptedCount > rounds / 50,
		"a fair number of mutated models are still walked to the end" );
}

}	// namespace


int main( int argc, char **argv )
{
	const int rounds = ( argc > 1 ) ? atoi( argv[1] ) : 40000;

	testValidModelsAreAccepted();
	testNamedDefects();
	testMutations( rounds );

	printf( "mdx second pass: %i check(s), %i failure(s)\n", g_checks, g_failures );
	return g_failures ? 1 : 0;
}
