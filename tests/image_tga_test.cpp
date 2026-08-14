/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// The Targa reader, fed malformed files on purpose.
//
// A .tga comes out of a pk3, and a pk3 is something a player downloads and
// drops into base/. That makes this loader one of the few places in the engine
// where a stranger chooses the bytes, and until it was separated from the file
// system it could not be handed a bad one at all: the parsing sat between
// FS_ReadFile and Com_Error, so the only way to ask what it did with a
// truncated file was to run the game with one.
//
// What it did is in the comment at the top of tr_image_tga_decode.h. This is
// the check that it has stopped.
//
// Three kinds of case here, and the third is the one that matters:
//
//   1. Round trips. A file this builds decodes back to the pixels it was built
//      from - because a reader that refuses everything also passes every
//      safety check, and that is not what is wanted.
//   2. Named defects. The header that overflows the allocation, the twenty-byte
//      file that declares four megapixels, the run that spans past the top row.
//      Each of these is a way the old reader left the buffer.
//   3. Mutation. Every seed file, byte by byte and in combination, tens of
//      thousands of times, from a fixed sequence so a failure is reproducible.
//      The input and output buffers are exactly the size they should be and
//      come from the heap, so a read or a write one byte outside either is a
//      fault rather than a value nobody notices. Run under the address
//      sanitizer, that is the whole test.
//
// No renderer, no file system, no zone.

#include "../code/rd-vulkan/tr_image_tga_decode.h"

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


// Little-endian, the way the format stores it.
void put16( std::vector<unsigned char> &v, size_t at, unsigned int value )
{
	v[at + 0] = (unsigned char)( value & 0xff );
	v[at + 1] = (unsigned char)( ( value >> 8 ) & 0xff );
}


std::vector<unsigned char> header( int type, int w, int h, int planes, int scanOrder )
{
	std::vector<unsigned char> v( TGA_HEADER_BYTES, 0 );

	v[2] = (unsigned char)type;
	put16( v, 12, (unsigned int)w );
	put16( v, 14, (unsigned int)h );
	v[16] = (unsigned char)planes;
	v[17] = (unsigned char)scanOrder;
	return v;
}


// An uncompressed truecolour file whose pixels are a recognisable ramp.
std::vector<unsigned char> makePlain( int w, int h, int planes, int scanOrder )
{
	std::vector<unsigned char> v = header( 2, w, h, planes, scanOrder );

	for ( int i = 0; i < w * h; i++ ) {
		v.push_back( (unsigned char)( i * 3 + 0 ) );	// blue
		v.push_back( (unsigned char)( i * 3 + 1 ) );	// green
		v.push_back( (unsigned char)( i * 3 + 2 ) );	// red
		if ( planes == 32 ) {
			v.push_back( (unsigned char)( i * 7 ) );
		}
	}
	return v;
}


// A run-length file: one run packet per row, filling it with a single colour.
std::vector<unsigned char> makeRLE( int w, int h )
{
	std::vector<unsigned char> v = header( 10, w, h, 24, 0x00 );

	for ( int row = 0; row < h; row++ ) {
		int left = w;
		while ( left > 0 ) {
			const int run = ( left > 128 ) ? 128 : left;

			v.push_back( (unsigned char)( 0x80 | ( run - 1 ) ) );
			v.push_back( (unsigned char)( row * 4 + 0 ) );	// blue
			v.push_back( (unsigned char)( row * 4 + 1 ) );	// green
			v.push_back( (unsigned char)( row * 4 + 2 ) );	// red
			left -= run;
		}
	}
	return v;
}


// Decode into a buffer of exactly the right size, from the heap. Both of those
// matter: a buffer with slack hides an overflow, and a buffer on the stack
// hides it from the sanitizer.
const char *decodeExact( const std::vector<unsigned char> &file, const tgaImage_t &info,
	std::vector<unsigned char> *pixels )
{
	// Copy the file too, so that its allocation ends exactly where the file
	// does and a read past the end is a fault.
	std::vector<unsigned char> input( file );

	pixels->assign( info.outBytes, 0 );
	return TGA_Decode( input.data(), input.size(), &info, pixels->data() );
}


void testRoundTrips()
{
	const int	orders[4] = { 0x00, 0x10, 0x20, 0x30 };

	for ( int oi = 0; oi < 4; oi++ ) {
		for ( int planes = 24; planes <= 32; planes += 8 ) {
			std::vector<unsigned char>	file = makePlain( 4, 3, planes, orders[oi] );
			std::vector<unsigned char>	pixels;
			tgaImage_t					info;
			char						label[128];

			snprintf( label, sizeof( label ), "a %i-bit image in scan order 0x%02x decodes",
				planes, orders[oi] );

			const char *bad = TGA_ReadHeader( file.data(), file.size(), &info );
			check( bad == NULL, label );
			if ( bad ) {
				printf( "  %s\n", bad );
				continue;
			}

			check( info.width == 4 && info.height == 3, "the size comes back" );
			check( info.outBytes == 4 * 3 * 4, "the picture is four bytes a pixel" );

			bad = decodeExact( file, info, &pixels );
			check( bad == NULL, "and decoding it answers no complaint" );

			// The first pixel of the file is the corner the scan order starts
			// at. Whichever corner that is, it holds file pixel 0 - which is
			// blue 0, green 1, red 2 - and the reader puts red first.
			size_t corner = 0;
			if ( orders[oi] == 0x00 || orders[oi] == 0x10 ) {
				corner = (size_t)( info.height - 1 ) * info.width * 4;
			}
			if ( orders[oi] == 0x10 || orders[oi] == 0x30 ) {
				corner += (size_t)( info.width - 1 ) * 4;
			}
			check( pixels[corner + 0] == 2 && pixels[corner + 1] == 1
				&& pixels[corner + 2] == 0, "the first pixel lands in the right corner" );
			check( pixels[corner + 3] == ( planes == 32 ? 0 : 255 ),
				"and carries its alpha, or opaque when the file has none" );
		}
	}

	// Greyscale.
	{
		std::vector<unsigned char>	file = header( 3, 2, 2, 8, 0x20 );
		std::vector<unsigned char>	pixels;
		tgaImage_t					info;

		file.push_back( 10 ); file.push_back( 20 );
		file.push_back( 30 ); file.push_back( 40 );

		check( TGA_ReadHeader( file.data(), file.size(), &info ) == NULL,
			"a greyscale image is read" );
		check( decodeExact( file, info, &pixels ) == NULL, "and decoded" );
		check( pixels[0] == 10 && pixels[1] == 10 && pixels[2] == 10 && pixels[3] == 255,
			"grey goes to all three channels, opaque" );
	}

	// Run-length.
	{
		std::vector<unsigned char>	file = makeRLE( 300, 2 );
		std::vector<unsigned char>	pixels;
		tgaImage_t					info;

		check( TGA_ReadHeader( file.data(), file.size(), &info ) == NULL,
			"a run-length image is read" );
		check( decodeExact( file, info, &pixels ) == NULL, "and decoded" );

		// Row 0 of the file is the bottom row of the picture, and 300 pixels
		// wide it takes three packets, so this also covers a row built from
		// more than one.
		const size_t bottom = (size_t)1 * 300 * 4;
		check( pixels[bottom + 0] == 2 && pixels[bottom + 1] == 1 && pixels[bottom + 2] == 0,
			"the first run fills the bottom row" );
		check( pixels[0] == 6 && pixels[1] == 5 && pixels[2] == 4,
			"and the second run the row above it" );
	}
}


void testNamedDefects()
{
	tgaImage_t	info;

	// Shorter than a header. The original byte-swapped three fields inside the
	// buffer before this was known.
	for ( size_t len = 0; len < TGA_HEADER_BYTES; len++ ) {
		std::vector<unsigned char>	file = makePlain( 2, 2, 24, 0x00 );

		file.resize( len );
		check( TGA_ReadHeader( file.data(), file.size(), &info ) != NULL,
			"a file shorter than a header is refused" );
	}

	// The allocation that overflowed: 65535 x 65535 x 4 is sixteen gigabytes,
	// which used to be computed in an int and wrapped to a small number, after
	// which the loops wrote the full amount into it.
	{
		std::vector<unsigned char> file = header( 2, 65535, 65535, 32, 0x00 );

		file.resize( file.size() + 64, 0 );
		check( TGA_ReadHeader( file.data(), file.size(), &info ) != NULL,
			"a picture too large to describe is refused" );
	}

	// A small file that declares a large uncompressed picture. This is the
	// sixty-four megabyte over-read.
	{
		std::vector<unsigned char> file = header( 2, 4096, 4096, 32, 0x00 );

		file.resize( file.size() + 2, 0 );
		check( TGA_ReadHeader( file.data(), file.size(), &info ) != NULL,
			"an uncompressed picture larger than its file is refused" );
	}

	// The same for a compressed one, which cannot be checked by size alone -
	// see TGA_MAX_PIXELS_PER_BYTE.
	{
		std::vector<unsigned char> file = header( 10, 8192, 8192, 32, 0x00 );

		file.resize( file.size() + 16, 0 );
		check( TGA_ReadHeader( file.data(), file.size(), &info ) != NULL,
			"a compressed picture beyond any compression ratio is refused" );
	}

	// A run that spans past the top row. Rows go upwards, so overshooting
	// subtracts: the original walked off the FRONT of the picture, which is the
	// half of a buffer overflow that tools notice least.
	{
		std::vector<unsigned char>	file = header( 10, 4, 2, 24, 0x00 );
		std::vector<unsigned char>	pixels;

		// One run of 128 pixels into a picture with eight.
		file.push_back( 0x80 | 127 );
		file.push_back( 1 ); file.push_back( 2 ); file.push_back( 3 );

		check( TGA_ReadHeader( file.data(), file.size(), &info ) == NULL,
			"a run longer than the picture is a readable header" );
		check( decodeExact( file, info, &pixels ) == NULL,
			"and decodes to a full picture rather than a fault" );
		check( pixels[0] == 3 && pixels[4] == 3, "the whole picture is the run's colour" );
	}

	// An id field that claims the whole file.
	{
		std::vector<unsigned char> file = makePlain( 2, 2, 24, 0x00 );

		file[0] = 255;
		check( TGA_ReadHeader( file.data(), file.size(), &info ) != NULL,
			"an id field past the end of the file is refused" );
	}

	// A picture with no pixels. The original allocated nothing and then indexed
	// from row height-1, which is -1.
	{
		std::vector<unsigned char> file = header( 2, 0, 4, 24, 0x00 );

		file.resize( file.size() + 16, 0 );
		check( TGA_ReadHeader( file.data(), file.size(), &info ) != NULL,
			"a picture with no width is refused" );
	}
}


// A fixed sequence, so that a failure is reproducible from the seed printed
// beside it. Not a good generator; a predictable one, which is what is wanted.
unsigned int nextRandom( unsigned int *state )
{
	*state = *state * 1664525u + 1013904223u;
	return ( *state >> 8 );
}


void testMutations( int rounds )
{
	std::vector<std::vector<unsigned char> >	seeds;

	seeds.push_back( makePlain( 8, 5, 24, 0x00 ) );
	seeds.push_back( makePlain( 8, 5, 32, 0x20 ) );
	seeds.push_back( makePlain( 3, 3, 32, 0x30 ) );
	seeds.push_back( makeRLE( 40, 4 ) );
	seeds.push_back( header( 3, 4, 4, 8, 0x00 ) );
	seeds.back().resize( seeds.back().size() + 16, 0x5a );

	int	accepted = 0, refused = 0, complained = 0;

	for ( int round = 0; round < rounds; round++ ) {
		unsigned int				state = (unsigned int)round * 2654435761u + 1u;
		std::vector<unsigned char>	file = seeds[ nextRandom( &state ) % seeds.size() ];
		const int					edits = 1 + (int)( nextRandom( &state ) % 6 );

		for ( int e = 0; e < edits; e++ ) {
			const unsigned int what = nextRandom( &state ) % 8;

			if ( what == 0 && file.size() > 1 ) {
				// Truncate. The commonest real corruption by far: an archive or
				// a download that lost its tail.
				file.resize( 1 + nextRandom( &state ) % file.size() );
			} else if ( what == 1 ) {
				file.push_back( (unsigned char)nextRandom( &state ) );
			} else if ( !file.empty() ) {
				file[ nextRandom( &state ) % file.size() ] = (unsigned char)nextRandom( &state );
			}
		}

		tgaImage_t	info;
		const char	*bad = TGA_ReadHeader( file.data(), file.size(), &info );

		if ( bad ) {
			refused++;
			continue;
		}

		// A header that passed has to describe a picture that fits in memory
		// twice over, because the test allocates it.
		if ( info.outBytes > 64u * 1024u * 1024u ) {
			g_failures++;
			printf( "FAIL: round %i accepted a header asking for %zu bytes\n",
				round, info.outBytes );
			continue;
		}

		std::vector<unsigned char>	pixels;
		const char					*why = decodeExact( file, info, &pixels );

		if ( why ) {
			complained++;

			// This particular complaint is not a complaint about the file, it
			// is the decoder catching its own arithmetic. TGA_Put's bound is
			// unreachable while the header checks hold - the loops walk exactly
			// width by height and the run-length path stops at the top row - so
			// seeing it here means one of those checks stopped holding. Kept
			// because it is the last thing between a wrong loop and a write
			// into the heap, and asserted here because a guard that can never
			// fire is indistinguishable from one that has been removed.
			if ( strstr( why, "outside the picture" ) ) {
				g_failures++;
				printf( "FAIL: round %i decoded a pixel outside the picture, so a "
					"header check is not holding\n", round );
			}
		} else {
			accepted++;
		}
	}

	printf( "  %i mutated file(s): %i refused by the header, %i decoded with a "
		"complaint, %i decoded clean\n", rounds, refused, complained, accepted );

	// If every mutation were refused the fuzzing would prove nothing, because a
	// reader that returns early on everything cannot overflow anything.
	check( accepted + complained > rounds / 20,
		"a fair number of mutated files still reach the decoder" );
}

}	// namespace


int main( int argc, char **argv )
{
	const int rounds = ( argc > 1 ) ? atoi( argv[1] ) : 40000;

	testRoundTrips();
	testNamedDefects();
	testMutations( rounds );

	printf( "tga reader: %i check(s), %i failure(s)\n", g_checks, g_failures );
	return g_failures ? 1 : 0;
}
