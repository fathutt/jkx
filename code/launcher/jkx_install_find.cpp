/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

#include "jkx_install_find.h"

#include <stdio.h>
#include <string.h>

/*
===============================================================================

Small helpers

===============================================================================
*/

static void AppendPath( char *out, int outSize, const char *dir, const char *rest )
{
	int	n = 0;

	while ( dir[n] != '\0' && n + 2 < outSize ) {
		out[n] = dir[n];
		n++;
	}
	if ( n > 0 && out[n - 1] != '/' && out[n - 1] != '\\' && n + 1 < outSize ) {
		// Backslash, because every caller of this file is on Windows and a path
		// that mixes separators is a path a person reading an error message has
		// to think about.
		out[n++] = '\\';
	}
	for ( int i = 0; rest[i] != '\0' && n + 1 < outSize; i++ ) {
		out[n++] = rest[i];
	}
	out[n] = '\0';
}


static int SameFolder( const char *a, const char *b )
{
	int	i = 0;

	// Case-insensitively, and treating the two separators as one character:
	// Steam writes forward slashes into its own files on some installs and
	// backslashes on others, and the same folder under two spellings would be
	// offered to the user twice.
	for ( ;; ) {
		char	ca = a[i];
		char	cb = b[i];

		if ( ca >= 'A' && ca <= 'Z' ) { ca = (char)( ca - 'A' + 'a' ); }
		if ( cb >= 'A' && cb <= 'Z' ) { cb = (char)( cb - 'A' + 'a' ); }
		if ( ca == '/' ) { ca = '\\'; }
		if ( cb == '/' ) { cb = '\\'; }

		if ( ca != cb ) {
			return 0;
		}
		if ( ca == '\0' ) {
			return 1;
		}
		i++;
	}
}


// One place that adds to the answer, so "already there" is checked once and the
// caller never has to remember to.
static int AddCandidate( char *out, int outStride, int max, int count, const char *path )
{
	if ( path[0] == '\0' || count >= max ) {
		return count;
	}

	for ( int i = 0; i < count; i++ ) {
		if ( SameFolder( out + (size_t)i * outStride, path ) ) {
			return count;
		}
	}

	snprintf( out + (size_t)count * outStride, (size_t)outStride, "%s", path );
	return count + 1;
}


// A retail install is a folder with base/ in it, and both games put that folder
// inside one called GameData - but a GOG install can be either shape depending
// on how old it is, and somebody who copied the game by hand can have any shape
// at all. Offering both costs one stat call.
static int AddCandidateAndGameData( char *out, int outStride, int max, int count,
									const char *path )
{
	char	withData[JKX_FIND_MAX_PATH];

	count = AddCandidate( out, outStride, max, count, path );

	AppendPath( withData, sizeof( withData ), path, "GameData" );
	return AddCandidate( out, outStride, max, count, withData );
}


/*
===============================================================================

Steam

===============================================================================
*/

// libraryfolders.vdf, in both shapes Steam has used.
//
// The current one is a block per library with a "path" key inside it; the older
// one is a numbered key whose value IS the path. Rather than write a parser for
// a format with no specification, this reads the quoted strings and looks at
// each ADJACENT PAIR of them, taking the second when the first is "path", or
// when the first is a number and the second looks like a path.
//
// Adjacent pairs rather than key-and-value pairs, and that distinction is the
// whole of it. Splitting the tokens into fixed pairs assumes every key has a
// value, and in this format they do not: "libraryfolders" is followed by a
// brace, and so is every numbered block in the current shape. One valueless key
// puts the alignment out for the rest of the file - which is exactly what
// happened, and the current format only worked because it has an even number of
// tokens before the first path by coincidence. A sliding window has no
// alignment to lose.
int InstallFind_ParseLibraryFolders( const char *text, char *out, int outStride, int max )
{
	int			count = 0;
	const char	*p = text;
	char		previous[JKX_FIND_MAX_PATH];
	int			havePrevious = 0;

	if ( text == NULL ) {
		return 0;
	}

	previous[0] = '\0';

	while ( *p != '\0' && count < max ) {
		char	token[JKX_FIND_MAX_PATH];
		int		n = 0;

		while ( *p != '\0' && *p != '"' ) {
			p++;
		}
		if ( *p == '\0' ) {
			break;
		}
		p++;

		// Unescaped as it is read: the file holds C-style escapes, so every
		// separator in it is a doubled backslash and a path taken literally
		// would not open.
		while ( *p != '\0' && *p != '"' && n + 1 < (int)sizeof( token ) ) {
			if ( *p == '\\' && p[1] != '\0' ) {
				p++;
			}
			token[n++] = *p++;
		}
		token[n] = '\0';

		// An unterminated string is the end of a truncated file, not a value.
		if ( *p != '"' ) {
			break;
		}
		p++;

		if ( havePrevious ) {
			int	previousIsNumber = ( previous[0] != '\0' );

			for ( int i = 0; previous[i] != '\0'; i++ ) {
				if ( previous[i] < '0' || previous[i] > '9' ) {
					previousIsNumber = 0;
					break;
				}
			}

			const int looksLikePath =
				( strchr( token, ':' ) != NULL ) || ( token[0] == '/' );

			if ( strcmp( previous, "path" ) == 0
				 || ( previousIsNumber && looksLikePath ) ) {
				count = AddCandidate( out, outStride, max, count, token );
			}
		}

		snprintf( previous, sizeof( previous ), "%s", token );
		havePrevious = 1;
	}

	return count;
}


// Every directory under a library's steamapps/common, offered as it is and with
// GameData under it.
//
// Not by app id, deliberately. 6020 and 6030 are the ids today; they are not
// written down anywhere this project controls, they differ between the bundled
// and standalone releases, and an id that is wrong fails silently by finding
// nothing. A directory listing cannot go out of date, and the thing that
// decides what a directory holds is looking inside the archives anyway.
static int AddSteamLibrary( const installSearch_t *search, const char *library,
							char *out, int outStride, int max, int count )
{
	char	common[JKX_FIND_MAX_PATH];
	char	names[32][128];
	int		found;

	AppendPath( common, sizeof( common ), library, "steamapps" );
	AppendPath( common, sizeof( common ), common, "common" );

	found = search->listDirs( search->user, common,
							  &names[0][0], (int)sizeof( names[0] ),
							  (int)( sizeof( names ) / sizeof( names[0] ) ) );

	for ( int i = 0; i < found && count < max; i++ ) {
		char	path[JKX_FIND_MAX_PATH];

		AppendPath( path, sizeof( path ), common, names[i] );
		count = AddCandidateAndGameData( out, outStride, max, count, path );
	}

	return count;
}


static int AddSteam( const installSearch_t *search, char *out, int outStride, int max, int count )
{
	char	steam[JKX_FIND_MAX_PATH];
	char	vdf[JKX_FIND_MAX_PATH];
	char	text[16384];
	char	libraries[16][JKX_FIND_MAX_PATH];
	int		libraryCount = 0;

	// The per-user key first: it is the one Steam updates when the client moves,
	// and on a machine with two accounts it is the one that belongs to whoever
	// is running this.
	if ( !search->readRegistry( search->user, JKX_REG_CURRENT_USER,
								"Software\\Valve\\Steam", "SteamPath",
								steam, sizeof( steam ) )
		 && !search->readRegistry( search->user, JKX_REG_LOCAL_MACHINE,
								   "SOFTWARE\\WOW6432Node\\Valve\\Steam", "InstallPath",
								   steam, sizeof( steam ) )
		 && !search->readRegistry( search->user, JKX_REG_LOCAL_MACHINE,
								   "SOFTWARE\\Valve\\Steam", "InstallPath",
								   steam, sizeof( steam ) ) ) {
		return count;
	}

	snprintf( libraries[libraryCount++], JKX_FIND_MAX_PATH, "%s", steam );

	AppendPath( vdf, sizeof( vdf ), steam, "steamapps" );
	AppendPath( vdf, sizeof( vdf ), vdf, "libraryfolders.vdf" );

	if ( search->readText( search->user, vdf, text, (int)sizeof( text ) ) > 0 ) {
		char	extra[16][JKX_FIND_MAX_PATH];
		const int n = InstallFind_ParseLibraryFolders( text, &extra[0][0],
													  JKX_FIND_MAX_PATH,
													  (int)( sizeof( extra ) / sizeof( extra[0] ) ) );

		for ( int i = 0; i < n && libraryCount < (int)( sizeof( libraries ) / sizeof( libraries[0] ) ); i++ ) {
			int	already = 0;

			for ( int j = 0; j < libraryCount; j++ ) {
				if ( SameFolder( libraries[j], extra[i] ) ) {
					already = 1;
					break;
				}
			}
			if ( !already ) {
				// Bounded in the format as well as by the size argument: the two
				// arrays are the same width, and saying so is what stops the
				// compiler warning about a truncation that cannot happen.
				snprintf( libraries[libraryCount++], JKX_FIND_MAX_PATH, "%.*s",
						  JKX_FIND_MAX_PATH - 1, extra[i] );
			}
		}
	}

	for ( int i = 0; i < libraryCount && count < max; i++ ) {
		count = AddSteamLibrary( search, libraries[i], out, outStride, max, count );
	}

	return count;
}


/*
===============================================================================

GOG, and the places an installer puts things when it is not either of them

===============================================================================
*/

// Every game GOG knows about, by its path rather than by its id - the same
// reasoning as for Steam, and here it matters more: GOG ids differ between the
// standalone releases and the bundles, and there are several of each.
static int AddGog( const installSearch_t *search, char *out, int outStride, int max, int count )
{
	static const char *roots[] = {
		"SOFTWARE\\WOW6432Node\\GOG.com\\Games",
		"SOFTWARE\\GOG.com\\Games"
	};

	for ( size_t r = 0; r < sizeof( roots ) / sizeof( roots[0] ); r++ ) {
		char	ids[64][64];
		int		found;

		found = search->listSubKeys( search->user, JKX_REG_LOCAL_MACHINE, roots[r],
									 &ids[0][0], (int)sizeof( ids[0] ),
									 (int)( sizeof( ids ) / sizeof( ids[0] ) ) );

		for ( int i = 0; i < found && count < max; i++ ) {
			char	key[JKX_FIND_MAX_PATH];
			char	path[JKX_FIND_MAX_PATH];

			AppendPath( key, sizeof( key ), roots[r], ids[i] );
			// The separator AppendPath chose is a backslash, which is also what
			// a registry key wants, so this needs no conversion.

			if ( search->readRegistry( search->user, JKX_REG_LOCAL_MACHINE, key,
									   "path", path, sizeof( path ) ) ) {
				count = AddCandidateAndGameData( out, outStride, max, count, path );
			}
		}
	}

	return count;
}


// Where the retail discs and the pre-Steam installers put things. Cheap to try
// and it is where a machine that has had these games since 2003 still has them.
static int AddFixedPlaces( const installSearch_t *search, char *out, int outStride, int max, int count )
{
	static const char *suffixes[] = {
		"LucasArts\\Star Wars Jedi Knight Jedi Academy\\GameData",
		"LucasArts\\Star Wars JK II Jedi Outcast\\GameData",
		"LucasArts\\Star Wars Jedi Knight II Jedi Outcast\\GameData",
		"Steam\\steamapps\\common\\Jedi Academy\\GameData",
		"Steam\\steamapps\\common\\Star Wars Jedi Knight II - Jedi Outcast\\GameData"
	};
	static const char *programFiles[] = {
		"C:\\Program Files (x86)",
		"C:\\Program Files",
		"C:\\GOG Games",
		"D:\\Games"
	};

	(void)search;

	for ( size_t d = 0; d < sizeof( programFiles ) / sizeof( programFiles[0] ); d++ ) {
		for ( size_t s = 0; s < sizeof( suffixes ) / sizeof( suffixes[0] ); s++ ) {
			char	path[JKX_FIND_MAX_PATH];

			AppendPath( path, sizeof( path ), programFiles[d], suffixes[s] );
			count = AddCandidate( out, outStride, max, count, path );
			if ( count >= max ) {
				return count;
			}
		}
	}

	return count;
}


int InstallFind_Candidates( const installSearch_t *search, char *out, int outStride, int max )
{
	int	count = 0;

	if ( search == NULL || out == NULL || max <= 0 ) {
		return 0;
	}
	if ( search->readRegistry == NULL || search->listSubKeys == NULL
		 || search->listDirs == NULL || search->readText == NULL ) {
		return 0;
	}

	// Steam first because it is where these games are on most machines that
	// have them at all, and the order here is the order the user is offered.
	count = AddSteam( search, out, outStride, max, count );
	count = AddGog( search, out, outStride, max, count );
	count = AddFixedPlaces( search, out, outStride, max, count );

	return count;
}
