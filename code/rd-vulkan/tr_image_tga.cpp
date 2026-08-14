/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2005 - 2015, ioquake3 contributors
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

#include "../server/exe_headers.h"

#include "tr_common.h"

#include "tr_image_tga_decode.h"

// Reading the file, and nothing else.
//
// Everything that used to be here - the header struct cast over the buffer, the
// scan-order switch, the two pixel loops - is in tr_image_tga_decode.cpp now,
// which takes a pointer and a length and can therefore be handed a malformed
// file by a test. It could not be, before: the parsing was wrapped around
// FS_ReadFile at one end and Com_Error at the other, so the only way to ask
// what it did with a bad Targa was to run the game with one. See that file for
// what it did.
//
// A malformed image is a warning and no picture, not a dropped level. The
// caller already handles no picture - that is what a missing file gives it -
// and every caller of R_LoadImage checks. Com_Error( ERR_DROP ) here meant one
// bad texture in one pk3 threw the player back to the menu, with the file named
// in a message the level load had already scrolled past.

void LoadTGA ( const char *name, byte **pic, int *width, int *height)
{
	byte		*buffer = NULL;
	tgaImage_t	info;
	const char	*bad;
	long		len;

	*pic = NULL;

	len = FS_ReadFile( (char *)name, (void **)&buffer );
	if ( !buffer ) {
		return;
	}
	if ( len < 0 ) {
		FS_FreeFile( buffer );
		return;
	}

	bad = TGA_ReadHeader( (const unsigned char *)buffer, (size_t)len, &info );
	if ( bad ) {
		CL_RefPrintf( PRINT_ALL, S_COLOR_YELLOW "LoadTGA: %s: %s\n", name, bad );
		FS_FreeFile( buffer );
		return;
	}

	byte *pRGBA = (byte *)R_Malloc( (int)info.outBytes, TAG_TEMP_WORKSPACE, qfalse );

	bad = TGA_Decode( (const unsigned char *)buffer, (size_t)len, &info, (unsigned char *)pRGBA );
	if ( bad ) {
		// The picture is kept. A file that ends early has decoded as much as it
		// carried and the rest is transparent black, which is a better answer
		// than no texture at all for the one case this is likely to be: a
		// download or an archive that lost its tail.
		CL_RefPrintf( PRINT_ALL, S_COLOR_YELLOW "LoadTGA: %s: %s\n", name, bad );
	}

	if ( width ) {
		*width = info.width;
	}
	if ( height ) {
		*height = info.height;
	}
	*pic = pRGBA;

	FS_FreeFile( buffer );
}
