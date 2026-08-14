/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// The savegame's run-length coding, round-tripped and then attacked.
//
// A .sav is a file players send each other, and every chunk of one is stored
// through this. What the decoder used to do with a malformed chunk is in the
// comment at the top of jkx_rle.h: three overflows and a hang, none of which
// needed anything cleverer than a wrong byte.
//
// Two halves here, and the first is what makes the second safe to believe:
//
//   1. Round trip. Encode and decode a few thousand buffers - runs, literals,
//      alternating pairs, the boundary lengths around a 127-byte run - and
//      require the bytes back exactly. Without this, every check below could be
//      satisfied by a decoder that refuses everything.
//   2. Mutation. Take a valid encoding, break it, and require that the decoder
//      either reproduces the original or answers false, having touched nothing
//      outside the output buffer. Under asan, with buffers sized exactly, that
//      is the whole claim.

#include "../code/qcommon/jkx_rle.h"

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

unsigned int nextRandom( unsigned int *state )
{
	*state = *state * 1664525u + 1013904223u;
	return ( *state >> 8 );
}


bool roundTrip( const std::vector<unsigned char> &plain )
{
	std::vector<unsigned char>	encoded( RLE_MaxEncodedSize( plain.size() ) );
	const size_t				n = RLE_Encode( plain.data(), plain.size(),
									encoded.data(), encoded.size() );

	encoded.resize( n );

	// Exactly the right size, from the heap: a byte either side is a fault.
	std::vector<unsigned char>	back( plain.size() );

	if ( plain.empty() ) {
		return n == 0;
	}
	if ( !RLE_Decode( encoded.data(), encoded.size(), back.data(), back.size() ) ) {
		return false;
	}
	return back == plain;
}


void testRoundTrips()
{
	// The shapes the coder has branches for.
	check( roundTrip( std::vector<unsigned char>( 300, 0x5a ) ),
		"one long run round trips" );
	check( roundTrip( std::vector<unsigned char>( 1, 0x01 ) ),
		"a single byte round trips" );
	check( roundTrip( std::vector<unsigned char>() ),
		"nothing round trips to nothing" );

	// A run of exactly 127, and one either side of it.
	for ( size_t len = 126; len <= 128; len++ ) {
		check( roundTrip( std::vector<unsigned char>( len, 0x7f ) ),
			"a run at the length limit round trips" );
	}

	// Alternating bytes, which is the literal path, and the two-back rule in
	// the encoder that decides where a literal stretch ends.
	{
		std::vector<unsigned char> v;

		for ( int i = 0; i < 400; i++ ) {
			v.push_back( (unsigned char)( i & 1 ) );
		}
		check( roundTrip( v ), "alternating bytes round trip" );
	}

	// Random data of every length up to a few hundred, which is where the
	// encoder's stretch-ending rule meets the end of the buffer.
	for ( int len = 0; len < 400; len++ ) {
		unsigned int				state = (unsigned int)len * 2654435761u + 3u;
		std::vector<unsigned char>	v;

		for ( int i = 0; i < len; i++ ) {
			// Biased towards repeats, so that both paths are taken.
			v.push_back( (unsigned char)( nextRandom( &state ) % 5 ) );
		}
		if ( !roundTrip( v ) ) {
			g_failures++;
			printf( "FAIL: a %i byte buffer did not round trip\n", len );
			break;
		}
	}
	g_checks++;
}


void testNamedDefects()
{
	std::vector<unsigned char>	out( 64, 0 );

	// A zero count. The old loop wrote nothing, consumed nothing but the count,
	// and never changed how much was left - an infinite loop reading further
	// out of bounds every time round. One byte.
	{
		const unsigned char stream[] = { 0x00, 0x41 };

		check( !RLE_Decode( stream, sizeof( stream ), out.data(), out.size() ),
			"a zero count is refused rather than spun on" );
	}

	// A run that does not fit. One byte of room and a count of 127 wrote 126
	// bytes past the end of the output.
	{
		const unsigned char	stream[] = { 127, 0x41 };
		std::vector<unsigned char>	one( 1, 0 );

		check( !RLE_Decode( stream, sizeof( stream ), one.data(), one.size() ),
			"a run longer than the space left is refused" );
	}

	// A stream that ends early. The old loop only ever asked whether the OUTPUT
	// had room, so it kept reading the input long after it had run out.
	{
		const unsigned char stream[] = { 10 };	// a run count with no byte after it

		check( !RLE_Decode( stream, sizeof( stream ), out.data(), out.size() ),
			"a run count with nothing after it is refused" );
	}
	{
		const unsigned char stream[] = { 0xf6, 0x01, 0x02 };	// -10 literals, only two given

		check( !RLE_Decode( stream, sizeof( stream ), out.data(), out.size() ),
			"a literal run longer than the input is refused" );
	}

	// The count of -128, which cannot be negated in a signed char. The old code
	// negated it and passed the result to a copy as a length.
	{
		std::vector<unsigned char>	stream;
		std::vector<unsigned char>	big( 128, 0 );

		stream.push_back( 0x80 );
		for ( int i = 0; i < 128; i++ ) {
			stream.push_back( (unsigned char)i );
		}
		check( RLE_Decode( stream.data(), stream.size(), big.data(), big.size() ),
			"a literal run of 128 decodes" );
		check( big[0] == 0 && big[127] == 127,
			"and decodes to the bytes it carried" );
	}

	// An empty input for a non-empty output.
	{
		check( !RLE_Decode( NULL, 0, out.data(), out.size() ),
			"no input for a non-empty output is refused" );
	}
}


void testMutations( int rounds )
{
	int	produced = 0, refused = 0;

	for ( int round = 0; round < rounds; round++ ) {
		unsigned int				state = (unsigned int)round * 2246822519u + 5u;
		const size_t				len = 1 + nextRandom( &state ) % 200;
		std::vector<unsigned char>	plain;

		for ( size_t i = 0; i < len; i++ ) {
			plain.push_back( (unsigned char)( nextRandom( &state ) % 6 ) );
		}

		std::vector<unsigned char>	encoded( RLE_MaxEncodedSize( plain.size() ) );
		const size_t				n = RLE_Encode( plain.data(), plain.size(),
										encoded.data(), encoded.size() );

		encoded.resize( n );

		const int edits = 1 + (int)( nextRandom( &state ) % 4 );

		for ( int e = 0; e < edits; e++ ) {
			const unsigned int what = nextRandom( &state ) % 4;

			if ( what == 0 && encoded.size() > 1 ) {
				encoded.resize( 1 + nextRandom( &state ) % encoded.size() );
			} else if ( what == 1 ) {
				encoded.push_back( (unsigned char)nextRandom( &state ) );
			} else if ( !encoded.empty() ) {
				encoded[ nextRandom( &state ) % encoded.size() ] =
					(unsigned char)nextRandom( &state );
			}
		}

		// The output buffer is exactly the size the chunk claims, from the
		// heap. That is the whole test: any write outside it is a fault, and
		// the decoder has no way to know how big it "should" be beyond what it
		// is told.
		std::vector<unsigned char>	back( plain.size() );

		if ( RLE_Decode( encoded.data(), encoded.size(), back.data(), back.size() ) ) {
			produced++;
		} else {
			refused++;
		}
	}

	printf( "  %i mutated stream(s): %i refused, %i decoded to full length\n",
		rounds, refused, produced );

	// If every mutation were refused this would prove nothing about the
	// bounds, because a decoder that returns on the first byte cannot overflow.
	check( produced > rounds / 50,
		"a fair number of mutated streams still decode" );
}

}	// namespace


int main( int argc, char **argv )
{
	const int rounds = ( argc > 1 ) ? atoi( argv[1] ) : 40000;

	testRoundTrips();
	testNamedDefects();
	testMutations( rounds );

	printf( "savegame rle: %i check(s), %i failure(s)\n", g_checks, g_failures );
	return g_failures ? 1 : 0;
}
