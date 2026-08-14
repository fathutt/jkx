/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// The BSP lump table, fed malformed maps.
//
// A .bsp arrives in a pk3 and is read before anything is drawn, so a map is the
// earliest thing a stranger's bytes reach. Every loader in cm_load.cpp and
// tr_bsp.cpp does the same thing with a lump - add its offset to the base
// pointer and read filelen/sizeof(element) elements - and nothing compared
// either number against the length of the file. See cm_bsp_check.h.
//
// The header is 152 bytes, so a synthetic one is a complete seed: this builds
// them rather than reading a map, and the checker never looks past the header.
//
// The output of the checker is a message or nothing, so there is no buffer to
// overflow here and the sanitizers have less to say than they did about the
// Targa reader. What is being checked is the decision - that every way of
// pointing a lump outside the file is refused, and that a map which is fine is
// still accepted, because a checker that refuses everything passes every safety
// test and breaks every map.

#include "../code/qcommon/cm_bsp_check.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
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


const int	NUM_LUMPS = 18;
const int	IDENT = ( 'P' << 24 ) + ( 'S' << 16 ) + ( 'B' << 8 ) + 'R';

void putLong( std::vector<unsigned char> &v, size_t at, int value )
{
	const unsigned int u = (unsigned int)value;

	v[at + 0] = (unsigned char)( u & 0xff );
	v[at + 1] = (unsigned char)( ( u >> 8 ) & 0xff );
	v[at + 2] = (unsigned char)( ( u >> 16 ) & 0xff );
	v[at + 3] = (unsigned char)( ( u >> 24 ) & 0xff );
}

int getLong( const std::vector<unsigned char> &v, size_t at )
{
	return (int)( (unsigned int)v[at]
		| ( (unsigned int)v[at + 1] << 8 )
		| ( (unsigned int)v[at + 2] << 16 )
		| ( (unsigned int)v[at + 3] << 24 ) );
}

void setLump( std::vector<unsigned char> &v, int lump, int ofs, int len )
{
	putLong( v, 8 + (size_t)lump * 8 + 0, ofs );
	putLong( v, 8 + (size_t)lump * 8 + 4, len );
}


// A map whose lumps all sit inside it: one element in each typed lump, laid end
// to end after the header.
std::vector<unsigned char> makeValid()
{
	static const int element[NUM_LUMPS] = {
		0, BSP_ELEM_SHADERS, BSP_ELEM_PLANES, BSP_ELEM_NODES, BSP_ELEM_LEAFS,
		BSP_ELEM_LEAFSURFACES, BSP_ELEM_LEAFBRUSHES, BSP_ELEM_MODELS,
		BSP_ELEM_BRUSHES, BSP_ELEM_BRUSHSIDES, BSP_ELEM_DRAWVERTS, 0,
		BSP_ELEM_FOGS, BSP_ELEM_SURFACES, 0, 0, 0, 0
	};

	std::vector<unsigned char>	v( BSP_HEADER_BYTES, 0 );
	int							at = BSP_HEADER_BYTES;

	putLong( v, 0, IDENT );
	putLong( v, 4, 1 );

	for ( int i = 0; i < NUM_LUMPS; i++ ) {
		const int size = element[i] ? element[i] : 16;

		setLump( v, i, at, size );
		at += size;
	}
	v.resize( (size_t)at, 0 );
	return v;
}


void testValid()
{
	std::vector<unsigned char> v = makeValid();

	check( BSP_CheckHeader( v.data(), v.size() ) == NULL,
		"a map whose lumps are all inside it is accepted" );

	// Empty lumps are ordinary: a map with no fogs has a fog lump of length
	// zero, and its offset is whatever the compiler left there.
	for ( int i = 0; i < NUM_LUMPS; i++ ) {
		std::vector<unsigned char> e = makeValid();

		setLump( e, i, (int)e.size(), 0 );
		check( BSP_CheckHeader( e.data(), e.size() ) == NULL,
			"an empty lump at the end of the file is accepted" );
	}
}


void testRefusals()
{
	// The identifier, which BSP_IDENT declared and nothing had ever compared.
	{
		std::vector<unsigned char> v = makeValid();

		putLong( v, 0, IDENT + 1 );
		check( BSP_CheckHeader( v.data(), v.size() ) != NULL,
			"a file that is not a BSP is refused by its identifier" );
	}

	{
		std::vector<unsigned char> v = makeValid();

		putLong( v, 4, 2 );
		check( BSP_CheckHeader( v.data(), v.size() ) != NULL,
			"the wrong version is refused" );
	}

	// Shorter than a header, one byte at a time. The 151-byte map in the
	// fixtures is this case, and it used to reach CMod_LoadShaders.
	for ( size_t len = 0; len < BSP_HEADER_BYTES; len++ ) {
		std::vector<unsigned char> v = makeValid();

		v.resize( len );
		check( BSP_CheckHeader( v.data(), v.size() ) != NULL,
			"a file shorter than a header is refused" );
	}

	// Every lump, pointed outside the file four ways.
	for ( int i = 0; i < NUM_LUMPS; i++ ) {
		std::vector<unsigned char>	v = makeValid();
		const int					ofs = getLong( v, 8 + (size_t)i * 8 );
		const int					len = getLong( v, 8 + (size_t)i * 8 + 4 );

		setLump( v, i, -1, len );
		check( BSP_CheckHeader( v.data(), v.size() ) != NULL,
			"a negative lump offset is refused" );

		setLump( v, i, ofs, -1 );
		check( BSP_CheckHeader( v.data(), v.size() ) != NULL,
			"a negative lump length is refused" );

		setLump( v, i, 0x7ffffff0, 16 );
		check( BSP_CheckHeader( v.data(), v.size() ) != NULL,
			"a lump two gigabytes away is refused" );

		// Begins inside the file and ends outside it, which is the one that
		// looks most like a real map.
		setLump( v, i, ofs, (int)v.size() );
		check( BSP_CheckHeader( v.data(), v.size() ) != NULL,
			"a lump that runs off the end is refused" );

		// And the pair that wraps: offset plus length overflows back to a small
		// number, so a signed comparison would call the end of it negative.
		setLump( v, i, 0x40000000, 0x40000000 );
		check( BSP_CheckHeader( v.data(), v.size() ) != NULL,
			"a lump whose offset plus length wraps is refused" );
	}

	// A typed lump that is not a whole number of elements. This is the
	// twenty-year-old "funny lump size", which some loaders checked and some
	// did not - the ones that did not read a partial element off the end.
	{
		std::vector<unsigned char> v = makeValid();

		setLump( v, 2 /* planes */, BSP_HEADER_BYTES, BSP_ELEM_PLANES - 1 );
		const char *bad = BSP_CheckHeader( v.data(), v.size() );
		check( bad != NULL, "a partial element is refused" );
		check( bad && strstr( bad, "planes" ),
			"and the message names the lump, which funny-lump-size never did" );
	}
}


unsigned int nextRandom( unsigned int *state )
{
	*state = *state * 1664525u + 1013904223u;
	return ( *state >> 8 );
}


void testMutations( int rounds )
{
	const std::vector<unsigned char>	seed = makeValid();
	int									accepted = 0, refused = 0;

	for ( int round = 0; round < rounds; round++ ) {
		unsigned int				state = (unsigned int)round * 2654435761u + 7u;
		std::vector<unsigned char>	v( seed );
		const int					edits = 1 + (int)( nextRandom( &state ) % 4 );

		for ( int e = 0; e < edits; e++ ) {
			// Whole fields as often as single bytes: a random byte in a lump
			// offset usually makes it enormous and always refused, which tests
			// one branch over and over. Setting a field to a plausible value
			// reaches the interesting comparisons.
			if ( nextRandom( &state ) % 2 ) {
				const size_t at = ( nextRandom( &state ) % ( v.size() / 4 ) ) * 4;

				putLong( v, at, (int)nextRandom( &state ) % (int)( v.size() * 2 ) );
			} else {
				v[ nextRandom( &state ) % v.size() ] = (unsigned char)nextRandom( &state );
			}
		}

		// Truncation, sometimes: the commonest real corruption.
		if ( nextRandom( &state ) % 4 == 0 ) {
			v.resize( 1 + nextRandom( &state ) % v.size() );
		}

		if ( BSP_CheckHeader( v.data(), v.size() ) ) {
			refused++;
			continue;
		}

		accepted++;

		// Anything accepted has to be true: every lump inside the file. This is
		// the check on the checker, and it is deliberately written from the
		// header rather than reusing anything the checker computed.
		for ( int i = 0; i < NUM_LUMPS; i++ ) {
			const long long ofs = getLong( v, 8 + (size_t)i * 8 );
			const long long len = getLong( v, 8 + (size_t)i * 8 + 4 );

			if ( ofs < 0 || len < 0 || ofs + len > (long long)v.size() ) {
				g_failures++;
				printf( "FAIL: round %i accepted lump %i at %lld+%lld in %zu bytes\n",
					round, i, ofs, len, v.size() );
				break;
			}
		}
	}

	printf( "  %i mutated header(s): %i refused, %i accepted\n",
		rounds, refused, accepted );

	// A checker that refuses everything passes every safety test and breaks
	// every map.
	check( accepted > rounds / 100,
		"a fair number of mutated headers are still accepted" );
}

}	// namespace


int main( int argc, char **argv )
{
	const int rounds = ( argc > 1 ) ? atoi( argv[1] ) : 40000;

	testValid();
	testRefusals();
	testMutations( rounds );

	printf( "bsp header: %i check(s), %i failure(s)\n", g_checks, g_failures );
	return g_failures ? 1 : 0;
}
