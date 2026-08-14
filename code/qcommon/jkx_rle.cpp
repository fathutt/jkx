/*
===========================================================================
Copyright (C) 2013 - 2015, OpenJK contributors
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

#include "jkx_rle.h"


// A run is at most this many bytes, encoder and decoder agreeing. The count is
// stored in a signed byte, so 127 is the ceiling either way; the original used
// 127 as the loop bound and never said why.
#define RLE_MAX_RUN		127


bool RLE_Decode( const unsigned char *src, size_t srcLen,
	unsigned char *dst, size_t dstLen )
{
	size_t	at = 0;
	size_t	out = 0;

	if ( !src || !dst ) {
		return false;
	}

	while ( out < dstLen )
	{
		signed char	count;
		size_t		run;

		if ( at >= srcLen ) {
			// The stream ended before the output was full. This is the case the
			// old loop could not see at all: it only ever asked whether the
			// output still had room.
			return false;
		}

		count = (signed char)src[at++];

		if ( count == 0 ) {
			// Neither a run nor a literal, and the old loop treated it as
			// both: nothing was written, nothing was consumed except this
			// byte, and the amount left never changed. One of these anywhere
			// in a savegame span it forever, reading further out of bounds
			// each time round.
			return false;
		}

		if ( count > 0 ) {
			// A run: the count, then the byte to repeat.
			run = (size_t)count;

			if ( at >= srcLen ) {
				return false;
			}
			if ( run > dstLen - out ) {
				// One byte of room and a count of 127 wrote 126 bytes past the
				// end, because the space left was only compared afterwards.
				return false;
			}

			const unsigned char b = src[at++];

			for ( size_t i = 0; i < run; i++ ) {
				dst[out + i] = b;
			}
		} else {
			// A literal run of -count bytes. Written as an unsigned promotion
			// rather than as -count, because count can be -128 and negating
			// that does not fit in a signed char - the old code did exactly
			// that and handed the result to a copy as a length.
			run = (size_t)( 0 - (int)count );

			if ( run > srcLen - at ) {
				return false;
			}
			if ( run > dstLen - out ) {
				return false;
			}

			for ( size_t i = 0; i < run; i++ ) {
				dst[out + i] = src[at + i];
			}
			at += run;
		}

		out += run;
	}

	return true;
}


size_t RLE_MaxEncodedSize( size_t srcLen )
{
	// A literal run costs one byte of count per run of up to 127, so the worst
	// case is srcLen plus one count byte per 127 bytes, and a lone byte at the
	// end costs two. Twice the input is what the original allocated and it is
	// comfortably enough; kept, because the format is unchanged.
	return 2 * srcLen + 2;
}


size_t RLE_Encode( const unsigned char *src, size_t srcLen,
	unsigned char *dst, size_t dstLen )
{
	size_t	at = 0;
	size_t	out = 0;

	if ( !src || !dst || dstLen < RLE_MaxEncodedSize( srcLen ) ) {
		return 0;
	}

	while ( at < srcLen )
	{
		size_t				end = at;
		const unsigned char	b = src[end++];

		// How far the same byte repeats.
		while ( end < srcLen && ( end - at ) < RLE_MAX_RUN && src[end] == b ) {
			end++;
		}

		if ( ( end - at ) == 1 )
		{
			// Not a run: gather a literal stretch, stopping before a pair that
			// would be better as one. This is the original's rule, transcribed
			// - it looks two bytes back, which is why a run of two is left to
			// the literal path and a run of three is not.
			while ( end < srcLen && ( end - at ) < RLE_MAX_RUN
				&& ( src[end] != src[end - 1]
					|| ( end > 1 && src[end] != src[end - 2] ) ) ) {
				end++;
			}

			while ( end > at + 1 && end < srcLen && src[end] == src[end - 1] ) {
				end--;
			}

			dst[out++] = (unsigned char)(signed char)( 0 - (int)( end - at ) );

			for ( size_t i = at; i < end; i++ ) {
				dst[out++] = src[i];
			}
		}
		else
		{
			dst[out++] = (unsigned char)( end - at );
			dst[out++] = b;
		}

		at = end;
	}

	return out;
}
