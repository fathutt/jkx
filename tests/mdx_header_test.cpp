/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// Model headers, fed malformed models.
//
// Three formats arrive in pk3s - MD3 for props and weapons, MDXM for a Ghoul2
// mesh, MDXA for its skeleton - and every loader walked their offsets without
// once comparing one against the length of the file. See mdx_check.h; the worst
// of it is R_LoadMDXM taking ofsEnd out of the file and allocating and copying
// that many bytes out of a buffer that holds however many the file had.
//
// Same shape as bsp_header_test: build valid headers, break them every way the
// checker is supposed to notice, then mutate tens of thousands of times from a
// fixed sequence. And, as there, every header the checker ACCEPTS is re-checked
// here from the bytes, by arithmetic written separately from the checker's own -
// a checker is worth exactly as much as the claim that what it accepted is
// true, and that claim must not be verified with the thing being tested.

#include "../code/rd-common/mdx_check.h"

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

const int	MD3_IDENT  = ( '3' << 24 ) + ( 'P' << 16 ) + ( 'D' << 8 ) + 'I';
const int	MDXM_IDENT = ( '2' << 24 ) + ( 'L' << 16 ) + ( 'G' << 8 ) + 'M';
const int	MDXA_IDENT = ( '2' << 24 ) + ( 'L' << 16 ) + ( 'G' << 8 ) + 'A';

const size_t	MD3_HEADER = 108;
const size_t	MDXM_HEADER = 164;
const size_t	MDXA_HEADER = 100;

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


// A skeleton with two bones and three frames, laid out the way the format says.
std::vector<unsigned char> makeMDXA()
{
	const int	bones = 2, frames = 3;
	const size_t	ofsSkel = MDXA_HEADER;
	const size_t	skelBytes = (size_t)bones * 4 + (size_t)bones * 172;
	const size_t	ofsFrames = ofsSkel + skelBytes;
	const size_t	frameBytes = (size_t)frames * bones * 3;
	const size_t	ofsPool = ofsFrames + frameBytes;
	const size_t	end = ofsPool + 14 * 4;

	std::vector<unsigned char> v( end, 0 );

	putLong( v, 0, MDXA_IDENT );
	putLong( v, 4, 6 );
	putLong( v, 76, frames );
	putLong( v, 80, (int)ofsFrames );
	putLong( v, 84, bones );
	putLong( v, 88, (int)ofsPool );
	putLong( v, 92, (int)ofsSkel );
	putLong( v, 96, (int)end );
	return v;
}

std::vector<unsigned char> makeMDXM()
{
	const int	bones = 2, lods = 1, surfaces = 2;
	const size_t	ofsHier = MDXM_HEADER;
	const size_t	ofsLODs = ofsHier + (size_t)surfaces * 4 + 256;
	const size_t	end = ofsLODs + (size_t)lods * 4 + 256;

	std::vector<unsigned char> v( end, 0 );

	putLong( v, 0, MDXM_IDENT );
	putLong( v, 4, 6 );
	putLong( v, 140, bones );
	putLong( v, 144, lods );
	putLong( v, 148, (int)ofsLODs );
	putLong( v, 152, surfaces );
	putLong( v, 156, (int)ofsHier );
	putLong( v, 160, (int)end );
	return v;
}

std::vector<unsigned char> makeMD3()
{
	const int	frames = 2, tags = 1, surfaces = 1;
	const size_t	ofsFrames = MD3_HEADER;
	const size_t	ofsTags = ofsFrames + (size_t)frames * 56;
	const size_t	ofsSurfaces = ofsTags + (size_t)tags * frames * 112;
	const size_t	end = ofsSurfaces + 108;

	std::vector<unsigned char> v( end, 0 );

	putLong( v, 0, MD3_IDENT );
	putLong( v, 4, 15 );
	putLong( v, 76, frames );
	putLong( v, 80, tags );
	putLong( v, 84, surfaces );
	putLong( v, 92, (int)ofsFrames );
	putLong( v, 96, (int)ofsTags );
	putLong( v, 100, (int)ofsSurfaces );
	putLong( v, 104, (int)end );
	return v;
}


struct Format {
	const char					*name;
	std::vector<unsigned char>	(*make)();
	size_t						header;
	size_t						endAt;		// where ofsEnd lives
	size_t						firstOfs;	// an offset field to point outside
};

const Format formats[3] = {
	{ "MDXA", makeMDXA, MDXA_HEADER, 96, 80 },
	{ "MDXM", makeMDXM, MDXM_HEADER, 160, 148 },
	{ "MD3", makeMD3, MD3_HEADER, 104, 92 },
};


void testValid()
{
	for ( int f = 0; f < 3; f++ ) {
		std::vector<unsigned char>	v = formats[f].make();
		const char					*bad = MDX_CheckHeader( v.data(), v.size() );
		char						label[96];

		snprintf( label, sizeof( label ), "a well formed %s is accepted", formats[f].name );
		check( bad == NULL, label );
		if ( bad ) {
			printf( "  %s\n", bad );
		}
	}

	// A file this does not recognise is not this function's business: the
	// caller has its own message for an unknown identifier, and answering with
	// a complaint here would turn every non-model into a model error.
	{
		std::vector<unsigned char> v( 64, 0 );

		putLong( v, 0, 0x41414141 );
		check( MDX_CheckHeader( v.data(), v.size() ) == NULL,
			"a file that is not a model at all is left alone" );
	}
}


void testRefusals()
{
	// Under four bytes. R_RegisterMD3 reads the identifier out of the buffer
	// before anything has said the buffer has four bytes in it.
	for ( size_t len = 0; len < 4; len++ ) {
		std::vector<unsigned char> v( len, 0 );

		check( MDX_CheckHeader( v.data(), v.size() ) != NULL,
			"a file too short to hold an identifier is refused" );
	}

	for ( int f = 0; f < 3; f++ ) {
		char	label[128];

		// Shorter than its own header.
		{
			std::vector<unsigned char> v = formats[f].make();

			v.resize( formats[f].header - 1 );
			snprintf( label, sizeof( label ), "a %s shorter than its header is refused",
				formats[f].name );
			check( MDX_CheckHeader( v.data(), v.size() ) != NULL, label );
		}

		// The declared size larger than the file. This is the allocate-and-copy.
		{
			std::vector<unsigned char> v = formats[f].make();

			putLong( v, formats[f].endAt, 0x7fffff00 );
			snprintf( label, sizeof( label ), "a %s declaring two gigabytes is refused",
				formats[f].name );
			check( MDX_CheckHeader( v.data(), v.size() ) != NULL, label );
		}

		// The declared size smaller than the header it just read.
		{
			std::vector<unsigned char> v = formats[f].make();

			putLong( v, formats[f].endAt, 4 );
			snprintf( label, sizeof( label ), "a %s declaring less than its own header is refused",
				formats[f].name );
			check( MDX_CheckHeader( v.data(), v.size() ) != NULL, label );
		}

		// An array offset pointing past the end.
		{
			std::vector<unsigned char> v = formats[f].make();

			putLong( v, formats[f].firstOfs, (int)v.size() );
			snprintf( label, sizeof( label ), "a %s array outside the file is refused",
				formats[f].name );
			check( MDX_CheckHeader( v.data(), v.size() ) != NULL, label );
		}

		// A negative offset.
		{
			std::vector<unsigned char> v = formats[f].make();

			putLong( v, formats[f].firstOfs, -64 );
			snprintf( label, sizeof( label ), "a %s array at a negative offset is refused",
				formats[f].name );
			check( MDX_CheckHeader( v.data(), v.size() ) != NULL, label );
		}

		// The wrong version.
		{
			std::vector<unsigned char> v = formats[f].make();

			putLong( v, 4, 99 );
			snprintf( label, sizeof( label ), "a %s of the wrong version is refused",
				formats[f].name );
			check( MDX_CheckHeader( v.data(), v.size() ) != NULL, label );
		}
	}

	// The count that multiplies. A skeleton claiming a great many bones and
	// frames must not be allowed to overflow the product that sizes its frame
	// array - which is the arithmetic the loader then does for real.
	{
		std::vector<unsigned char> v = makeMDXA();

		putLong( v, 76, 0x40000000 );	// frames
		putLong( v, 84, 0x40000000 );	// bones
		check( MDX_CheckHeader( v.data(), v.size() ) != NULL,
			"a skeleton whose bones times frames overflows is refused" );
	}
}


unsigned int nextRandom( unsigned int *state )
{
	*state = *state * 1664525u + 1013904223u;
	return ( *state >> 8 );
}


// Written from the bytes, deliberately not sharing a line with the checker.
bool acceptedHeaderIsTrue( const std::vector<unsigned char> &v, int which )
{
	const long long	size = (long long)v.size();
	long long		end;

	if ( which == 0 ) {		// MDXA
		end = getLong( v, 96 );
		const long long frames = getLong( v, 76 );
		const long long bones = getLong( v, 84 );

		if ( end < 0 || end > size ) return false;
		if ( frames < 0 || bones < 0 ) return false;
		if ( (long long)getLong( v, 80 ) + frames * bones * 3 > end ) return false;
		if ( (long long)getLong( v, 92 ) + bones * 4 > end ) return false;
		if ( getLong( v, 80 ) < 0 || getLong( v, 92 ) < 0 || getLong( v, 88 ) < 0 ) return false;
		return true;
	}
	if ( which == 1 ) {		// MDXM
		end = getLong( v, 160 );
		const long long lods = getLong( v, 144 );
		const long long surfaces = getLong( v, 152 );

		if ( end < 0 || end > size ) return false;
		if ( lods < 0 || surfaces < 0 ) return false;
		if ( getLong( v, 148 ) < 0 || getLong( v, 156 ) < 0 ) return false;
		if ( (long long)getLong( v, 148 ) + lods * 4 > end ) return false;
		if ( (long long)getLong( v, 156 ) + surfaces * 4 > end ) return false;
		return true;
	}

	end = getLong( v, 104 );					// MD3
	const long long frames = getLong( v, 76 );
	const long long tags = getLong( v, 80 );

	if ( end < 0 || end > size ) return false;
	if ( frames < 1 || tags < 0 ) return false;
	if ( getLong( v, 92 ) < 0 || getLong( v, 96 ) < 0 ) return false;
	if ( (long long)getLong( v, 92 ) + frames * 56 > end ) return false;
	if ( (long long)getLong( v, 96 ) + tags * frames * 112 > end ) return false;
	return true;
}


void testMutations( int rounds )
{
	int	accepted = 0, refused = 0;

	for ( int round = 0; round < rounds; round++ ) {
		unsigned int				state = (unsigned int)round * 2654435761u + 11u;
		const int					which = (int)( nextRandom( &state ) % 3 );
		std::vector<unsigned char>	v = formats[which].make();
		const int					edits = 1 + (int)( nextRandom( &state ) % 4 );

		for ( int e = 0; e < edits; e++ ) {
			if ( nextRandom( &state ) % 2 ) {
				// A whole field, at a plausible value. Random bytes in an
				// offset make it enormous and always refused, which exercises
				// one branch over and over.
				const size_t at = ( nextRandom( &state ) % ( v.size() / 4 ) ) * 4;

				putLong( v, at, (int)( nextRandom( &state ) % ( v.size() * 2 ) ) );
			} else {
				v[ nextRandom( &state ) % v.size() ] = (unsigned char)nextRandom( &state );
			}
		}

		if ( nextRandom( &state ) % 4 == 0 ) {
			v.resize( 1 + nextRandom( &state ) % v.size() );
		}

		// A mutation may have changed the identifier into another format's, or
		// into nothing. Ask the bytes which one it is now.
		int now = -1;
		if ( v.size() >= 4 ) {
			const int ident = getLong( v, 0 );

			if ( ident == MDXA_IDENT ) now = 0;
			else if ( ident == MDXM_IDENT ) now = 1;
			else if ( ident == MD3_IDENT ) now = 2;
		}

		const char *bad = MDX_CheckHeader( v.data(), v.size() );

		if ( bad ) {
			refused++;
			continue;
		}
		if ( now < 0 ) {
			// Not a model any more, and left alone on purpose.
			continue;
		}

		accepted++;

		if ( !acceptedHeaderIsTrue( v, now ) ) {
			g_failures++;
			printf( "FAIL: round %i accepted a %s header whose arrays are not inside it\n",
				round, formats[now].name );
		}
	}

	printf( "  %i mutated header(s): %i refused, %i accepted as models\n",
		rounds, refused, accepted );

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

	printf( "mdx headers: %i check(s), %i failure(s)\n", g_checks, g_failures );
	return g_failures ? 1 : 0;
}
