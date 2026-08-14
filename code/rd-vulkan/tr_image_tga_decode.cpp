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

#include "tr_image_tga_decode.h"

#include <limits.h>


// The picture is 32-bit RGBA whatever the file was, so four bytes a pixel.
#define TGA_OUT_BPP		4

// How many pixels one byte of a run-length image can produce, at most.
//
// The densest packet is a 24-bit run: one header byte plus three colour bytes
// gives up to 128 pixels, so 32 pixels per byte. This is what stops a
// twenty-byte file from asking for a two gigabyte allocation - an uncompressed
// image is bounded by having to carry its own pixels, and a compressed one is
// not.
#define TGA_MAX_PIXELS_PER_BYTE		32


static unsigned int TGA_ReadShort( const unsigned char *at )
{
	// Read rather than cast. The original swapped these fields in place inside
	// the file buffer, which is a write to data this code does not own and
	// which happened before anything had checked the buffer was long enough to
	// hold them.
	return (unsigned int)at[0] | ( (unsigned int)at[1] << 8 );
}


const char *TGA_ReadHeader( const unsigned char *data, size_t len, tgaImage_t *out )
{
	unsigned int	colourMapLength, firstColourMapEntry;
	unsigned int	colourMapEntrySize, idFieldLength;
	size_t			pixels, needed;

	if ( !data || !out ) {
		return "no data";
	}

	if ( len < TGA_HEADER_BYTES ) {
		return "shorter than a Targa header";
	}

	idFieldLength			= data[0];
	// data[1] is the colour map type.
	out->imageType			= data[2];
	firstColourMapEntry		= TGA_ReadShort( data + 3 );
	colourMapLength			= TGA_ReadShort( data + 5 );
	colourMapEntrySize		= data[7];
	// data[8..11] are the origin, which is ignored here as it always was.
	out->width				= (int)TGA_ReadShort( data + 12 );
	out->height				= (int)TGA_ReadShort( data + 14 );
	out->planes				= data[16];
	out->scanOrder			= data[17] & 0x30;

	if ( data[1] != 0 || colourMapLength != 0 || firstColourMapEntry != 0
		|| colourMapEntrySize != 0 ) {
		// The original accepted a colour map length of 256 and an entry size of
		// 24 here and then never read a palette, so a paletted file was
		// accepted and decoded as though its indices were colours.
		return "colourmaps are not supported";
	}

	if ( out->imageType != 2 && out->imageType != 3 && out->imageType != 10 ) {
		return "only type 2 (RGB), 3 (greyscale) and 10 (RLE RGB) are supported";
	}

	if ( out->imageType == 3 ) {
		if ( out->planes != 8 ) {
			return "a greyscale image must be 8 bits per pixel";
		}
	} else if ( out->imageType == 10 ) {
		if ( out->planes != 24 && out->planes != 32 ) {
			return "an RLE image must be 24 or 32 bits per pixel";
		}
		if ( out->scanOrder != 0x00 ) {
			return "an RLE image must be bottom to top";
		}
	} else {
		if ( out->planes != 24 && out->planes != 32 ) {
			return "a truecolour image must be 24 or 32 bits per pixel";
		}
	}

	if ( out->width <= 0 || out->height <= 0 ) {
		return "no pixels";
	}

	out->pixelOffset = (size_t)TGA_HEADER_BYTES + idFieldLength;
	if ( out->pixelOffset > len ) {
		return "the id field runs past the end of the file";
	}

	// Every product from here is size_t. Both dimensions are 16-bit fields, so
	// the largest picture a header can ask for is 65535 x 65535, whose byte
	// count overflows a 32-bit int four times over - which is what the
	// allocation used to be computed in.
	pixels = (size_t)out->width * (size_t)out->height;
	out->outBytes = pixels * TGA_OUT_BPP;

	if ( out->outBytes / TGA_OUT_BPP != pixels || out->outBytes > (size_t)INT_MAX ) {
		return "too large to decode";
	}

	if ( out->imageType == 10 ) {
		// A compressed image does not have to carry its pixels, so its size has
		// to be bounded some other way. See TGA_MAX_PIXELS_PER_BYTE.
		const size_t available = len - out->pixelOffset;

		if ( pixels / TGA_MAX_PIXELS_PER_BYTE > available ) {
			return "more pixels than the compressed data can hold";
		}
	} else {
		needed = pixels * (size_t)( out->planes / 8 );

		if ( out->pixelOffset + needed > len ) {
			return "the pixel data runs past the end of the file";
		}
	}

	return NULL;
}


// Where the decoder is writing and reading, with the ends known.
typedef struct {
	const unsigned char	*in;
	size_t				inLeft;
	unsigned char		*outBase;
	size_t				outBytes;
} tgaCursor_t;


static int TGA_Take( tgaCursor_t *cur, unsigned char *dest, size_t count )
{
	size_t i;

	if ( cur->inLeft < count ) {
		return 0;
	}
	for ( i = 0; i < count; i++ ) {
		dest[i] = cur->in[i];
	}
	cur->in += count;
	cur->inLeft -= count;
	return 1;
}


// One pixel out, at an offset checked against the end of the picture. The
// offset is computed by the caller from a row and a column, and both of those
// walk backwards in some scan orders, so this is the only place that can tell
// whether the arithmetic left the buffer.
static int TGA_Put( tgaCursor_t *cur, size_t at,
	unsigned char r, unsigned char g, unsigned char b, unsigned char a )
{
	if ( at > cur->outBytes - TGA_OUT_BPP ) {
		return 0;
	}
	cur->outBase[at + 0] = r;
	cur->outBase[at + 1] = g;
	cur->outBase[at + 2] = b;
	cur->outBase[at + 3] = a;
	return 1;
}


static const char *TGA_DecodePlain( tgaCursor_t *cur, const tgaImage_t *info )
{
	const int	bytesPerPixel = info->planes / 8;
	int			xStart, xStep, yStart, yStep;
	int			y, yCount;

	switch ( info->scanOrder )
	{
		default:
		case 0x00:	xStart = 0;					xStep =  1;
					yStart = info->height - 1;	yStep = -1;	break;
		case 0x10:	xStart = info->width - 1;	xStep = -1;
					yStart = info->height - 1;	yStep = -1;	break;
		case 0x20:	xStart = 0;					xStep =  1;
					yStart = 0;					yStep =  1;	break;
		case 0x30:	xStart = info->width - 1;	xStep = -1;
					yStart = 0;					yStep =  1;	break;
	}

	for ( y = yStart, yCount = 0; yCount < info->height; y += yStep, yCount++ )
	{
		int	x, xCount;

		for ( x = xStart, xCount = 0; xCount < info->width; x += xStep, xCount++ )
		{
			unsigned char	texel[4];
			const size_t	at = ( (size_t)y * (size_t)info->width + (size_t)x ) * TGA_OUT_BPP;

			if ( !TGA_Take( cur, texel, (size_t)bytesPerPixel ) ) {
				return "the pixel data ends early";
			}

			// Targa stores blue, green, red.
			if ( bytesPerPixel == 1 ) {
				if ( !TGA_Put( cur, at, texel[0], texel[0], texel[0], 255 ) ) {
					return "a pixel landed outside the picture";
				}
			} else {
				const unsigned char alpha = ( bytesPerPixel == 4 ) ? texel[3] : 255;

				if ( !TGA_Put( cur, at, texel[2], texel[1], texel[0], alpha ) ) {
					return "a pixel landed outside the picture";
				}
			}
		}
	}

	return NULL;
}


static const char *TGA_DecodeRLE( tgaCursor_t *cur, const tgaImage_t *info )
{
	const int	bytesPerPixel = info->planes / 8;
	int			y = info->height - 1;
	int			x = 0;

	// One row at a time from the bottom, which is the only order the header
	// check allows for a compressed image. A run may cross a row boundary, so
	// the position is carried rather than reset per row.
	while ( y >= 0 )
	{
		unsigned char	packetHeader;
		unsigned char	texel[4];
		int				runLength, isRun, i;

		if ( !TGA_Take( cur, &packetHeader, 1 ) ) {
			return "the compressed data ends early";
		}

		isRun = ( packetHeader & 0x80 ) != 0;
		runLength = 1 + ( packetHeader & 0x7f );

		if ( isRun && !TGA_Take( cur, texel, (size_t)bytesPerPixel ) ) {
			return "the compressed data ends early";
		}

		for ( i = 0; i < runLength; i++ )
		{
			const size_t	at = ( (size_t)y * (size_t)info->width + (size_t)x ) * TGA_OUT_BPP;
			unsigned char	alpha;

			if ( !isRun && !TGA_Take( cur, texel, (size_t)bytesPerPixel ) ) {
				return "the compressed data ends early";
			}

			alpha = ( bytesPerPixel == 4 ) ? texel[3] : 255;

			if ( !TGA_Put( cur, at, texel[2], texel[1], texel[0], alpha ) ) {
				return "a pixel landed outside the picture";
			}

			if ( ++x == info->width ) {
				x = 0;
				y--;
				if ( y < 0 ) {
					// A run that spans past the top row is where the original
					// walked off the front of the buffer: rows go backwards, so
					// overshooting subtracts rather than adds. The rest of the
					// run is dropped and the picture is complete.
					return NULL;
				}
			}
		}
	}

	return NULL;
}


const char *TGA_Decode( const unsigned char *data, size_t len,
	const tgaImage_t *info, unsigned char *out )
{
	tgaCursor_t	cur;
	size_t		i;

	if ( !data || !info || !out ) {
		return "no data";
	}
	if ( info->pixelOffset > len ) {
		return "the id field runs past the end of the file";
	}

	cur.in = data + info->pixelOffset;
	cur.inLeft = len - info->pixelOffset;
	cur.outBase = out;
	cur.outBytes = info->outBytes;

	// A file that ends early leaves the rest of the picture untouched rather
	// than uninitialised, because the caller draws whatever comes back.
	for ( i = 0; i < info->outBytes; i++ ) {
		out[i] = 0;
	}

	if ( info->imageType == 10 ) {
		return TGA_DecodeRLE( &cur, info );
	}

	return TGA_DecodePlain( &cur, info );
}
