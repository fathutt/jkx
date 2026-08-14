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

#pragma once

// Reading a Targa, with the file's length in hand.
//
// This is a separate pair of files from tr_image_tga.cpp, and dependency-free
// on purpose: it takes a pointer and a length and answers pixels, so it can be
// compiled into a test and handed a few hundred thousand malformed files. The
// loader it came out of could not be tested at all - it read through
// FS_ReadFile, allocated through the zone and reported through Com_Error, so
// there was no way to ask it what it did with a bad one except to run the game.
//
// What it did with a bad one was read past the end of the file, and the .tga
// path is reachable from any pk3 a player installs.
//
//   - The 18-byte header was cast over the buffer and its fields BYTE-SWAPPED
//     IN PLACE before anything checked that the file had 18 bytes in it.
//   - The pixel loops read width * height * planes/8 bytes from the file and
//     never looked at how many there were. A twenty-byte file declaring
//     4096x4096x32 read sixty-four megabytes past the end.
//   - The output was allocated as width * height * 4 in an int, and both are
//     16-bit fields: 65535 * 65535 * 4 overflows to a small number, after
//     which the loops write the full amount into it.
//   - The run-length path never bounds-checked either side, and its rows wrap
//     backwards, so a run that overshoots walks off the front of the buffer
//     rather than the end of it.
//
// Everything here is bounds-checked against len, and every product is computed
// in size_t. The functions allocate nothing: the caller reads the header,
// allocates outBytes, and decodes into it.

#include <stddef.h>

// The header is 18 bytes and nothing shorter is a Targa.
#define TGA_HEADER_BYTES	18

typedef struct {
	int		width, height;
	int		planes;			// bits per pixel in the file: 8, 24 or 32
	int		imageType;		// 2 = truecolour, 3 = greyscale, 10 = RLE truecolour
	int		scanOrder;		// the direction bits, masked: 0x00, 0x10, 0x20 or 0x30
	size_t	pixelOffset;	// first byte of pixel data, past the header and the id field
	size_t	outBytes;		// width * height * 4, what TGA_Decode writes
} tgaImage_t;

// Read and check the header. Answers NULL when the file can be decoded, or a
// message saying why not. Nothing is written to data.
const char *TGA_ReadHeader( const unsigned char *data, size_t len, tgaImage_t *out );

// Decode into out, which must be info->outBytes long, from the same data and
// len the header was read from. Answers NULL, or a message.
const char *TGA_Decode( const unsigned char *data, size_t len,
	const tgaImage_t *info, unsigned char *out );
