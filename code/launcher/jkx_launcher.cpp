/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// One command that starts the right game.
//
// Today a player has to know that jkx_jka is Academy, that jkx_jk2 is Outcast,
// and that each one needs its OWN installation with its own assets. That is
// not knowledge anyone should need, and getting it wrong produces a message
// about a string package called CON_TEXT - which is a true statement about
// Outcast's data and tells nobody anything. The way it was found was exactly
// that: the Outcast engine started in an Academy directory.
//
// So: point this at a directory, or run it beside one, and it works out which
// game the directory holds and starts the matching engine with the paths
// already set. What it does NOT do yet is find installations by itself -
// Steam's registry key and libraryfolders.vdf, GOG's key - and that is the
// next step rather than a decision. The identification is the part that had to
// be right first, because everything else is built on the answer; it lives in
// jkx_install_scan.cpp and is tested against a fake filesystem.

#include "jkx_install_scan.h"

#include "minizip/unzip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#define JKX_EXEC		_execv
#define JKX_PATH_SEP	'\\'
#define JKX_EXE_SUFFIX	".exe"
#else
#include <unistd.h>
#define JKX_EXEC		execv
#define JKX_PATH_SEP	'/'
#define JKX_EXE_SUFFIX	""
#endif

#define JKX_MAX_PATH	1024


// minizip in this tree is built against the engine's zone allocator, which the
// launcher is not and should not become: it opens one archive, asks it one
// question and closes it. The C library is the right allocator for that, and
// saying so here is cheaper than making the launcher depend on the engine's
// memory system to read a directory listing.
extern "C" void *openjk_minizip_malloc( int size );
extern "C" void openjk_minizip_free( void *p );

extern "C" void *openjk_minizip_malloc( int size )
{
	return malloc( (size_t)size );
}

extern "C" void openjk_minizip_free( void *p )
{
	free( p );
}


typedef struct {
	char	root[JKX_MAX_PATH];
} probeContext_t;


static void JoinPath( char *out, size_t outSize, const char *dir, const char *rest )
{
	size_t	n = 0;

	while ( dir[n] && n + 2 < outSize ) {
		out[n] = dir[n];
		n++;
	}
	if ( n > 0 && out[n - 1] != '/' && out[n - 1] != '\\' && n + 1 < outSize ) {
		out[n++] = JKX_PATH_SEP;
	}
	for ( size_t i = 0; rest[i] && n + 1 < outSize; i++ ) {
		out[n++] = rest[i];
	}
	out[n] = '\0';
}


static int RealFileExists( void *user, const char *relative )
{
	const probeContext_t	*ctx = (const probeContext_t *)user;
	char					path[JKX_MAX_PATH];
	struct stat				st;

	JoinPath( path, sizeof( path ), ctx->root, relative );
	return stat( path, &st ) == 0;
}


static int RealArchiveHas( void *user, const char *archive, const char *name )
{
	const probeContext_t	*ctx = (const probeContext_t *)user;
	char					path[JKX_MAX_PATH];
	unzFile					uf;
	int						found;

	JoinPath( path, sizeof( path ), ctx->root, archive );

	uf = unzOpen( path );
	if ( !uf ) {
		// An archive that cannot be opened is not an archive that says no: it
		// is one this launcher could not read. Answering no is still the right
		// move - it means "do not claim this is that game on my say-so" - and
		// the caller's message says which files were looked at.
		return 0;
	}

	// Case insensitive, which is the second argument: a pk3 built on Windows
	// and read on Linux is the ordinary case here, not the exotic one.
	found = ( unzLocateFile( uf, name, 2 ) == UNZ_OK );
	unzClose( uf );
	return found;
}


// The engines carry the architecture in their names, and a packaged build may
// not: both spellings are tried, in the order that prefers the exact one.
static const char *EngineNames( installKind_t kind, int which )
{
	if ( kind == INSTALL_ACADEMY ) {
		return ( which == 0 ) ? "jkx_jka." ARCH_STRING JKX_EXE_SUFFIX
			: "jkx_jka" JKX_EXE_SUFFIX;
	}
	if ( kind == INSTALL_OUTCAST ) {
		return ( which == 0 ) ? "jkx_jk2." ARCH_STRING JKX_EXE_SUFFIX
			: "jkx_jk2" JKX_EXE_SUFFIX;
	}
	return NULL;
}


static void DirectoryOf( char *out, size_t outSize, const char *path )
{
	size_t	n = strlen( path );

	while ( n > 0 && path[n - 1] != '/' && path[n - 1] != '\\' ) {
		n--;
	}
	if ( n == 0 ) {
		// No separator at all: the program was found on PATH or started from
		// the directory it is in. "." is the honest answer and it is also the
		// right one.
		out[0] = '.';
		out[1] = '\0';
		return;
	}
	if ( n >= outSize ) {
		n = outSize - 1;
	}
	memcpy( out, path, n - 1 );
	out[n - 1] = '\0';
}


// A console this program created for itself closes the instant it returns, so a
// message printed on the way out is a message nobody reads. That is what
// double-clicking it looked like: a window that flashes. GetConsoleProcessList
// returning one says the console has no other owner, which is exactly the case
// where waiting is right and the only one - run from a shell, it must not wait.
static void WaitIfWeOwnTheConsole( void )
{
#ifdef _WIN32
	DWORD	pids[2];

	if ( GetConsoleProcessList( pids, 2 ) == 1 ) {
		printf( "\nPress Enter to close." );
		fflush( stdout );
		(void)getchar();
	}
#endif
}


int main( int argc, char **argv )
{
	probeContext_t	ctx;
	installProbe_t	probe;
	installResult_t	result;
	char			engine[JKX_MAX_PATH];
	char			here[JKX_MAX_PATH];
	const char		*name = NULL;

	if ( argc > 2 ) {
		printf( "usage: %s [game directory]\n", argv[0] );
		printf( "\nWith no argument it looks in the current directory, then beside\n"
			"itself, then one folder up. Give it the folder that has base/ in it -\n"
			"for a Steam or GOG install that is the one called GameData.\n" );
		WaitIfWeOwnTheConsole();
		return 2;
	}

	probe.fileExists = RealFileExists;
	probe.archiveHas = RealArchiveHas;
	probe.user = &ctx;

	DirectoryOf( here, sizeof( here ), argv[0] );

	// Where to look, in the order a person would. Double-clicked from Explorer
	// the current directory is wherever Explorer felt like, which is why its own
	// folder and the folder above it are on the list: an engine sitting next to
	// or inside a game install is the ordinary arrangement.
	{
		const char	*places[4];
		int			count = 0;
		char		up[JKX_MAX_PATH];

		if ( argc > 1 ) {
			places[count++] = argv[1];
		} else {
			DirectoryOf( up, sizeof( up ), here );

			places[count++] = ".";
			places[count++] = here;
			places[count++] = up;
		}

		for ( int i = 0; i < count; i++ ) {
			snprintf( ctx.root, sizeof( ctx.root ), "%s", places[i] );
			result = InstallScan_Identify( &probe );
			name = EngineNames( result.kind, 0 );
			if ( name ) {
				break;
			}
			printf( "%s: %s\n", ctx.root, result.reason );
		}
	}

	if ( !name ) {
		printf( "\nPoint this at the folder that contains base/ - for a Steam or GOG\n"
			"install of either game that is the one called GameData. Finding one by\n"
			"itself, out of Steam's and GOG's own records, is the next step and is\n"
			"not written yet.\n" );
		WaitIfWeOwnTheConsole();
		return 1;
	}

	printf( "%s: %s\n", ctx.root, result.reason );

	// The engine lives beside the launcher, not inside the game directory. A
	// copy in the game directory is what a mod is, and the retail folder is
	// not ours to write into.
	JoinPath( engine, sizeof( engine ), here, name );

	{
		struct stat	st;

		if ( stat( engine, &st ) != 0 ) {
			JoinPath( engine, sizeof( engine ), here,
				EngineNames( result.kind, 1 ) );
		}
	}

	{
		char	*args[6];
		char	basepath[] = "+set";
		char	fsbase[] = "fs_basepath";

		args[0] = engine;
		args[1] = basepath;
		args[2] = fsbase;
		args[3] = ctx.root;
		args[4] = NULL;
		args[5] = NULL;

		printf( "starting %s\n", engine );
		JKX_EXEC( engine, args );
	}

	// Only reached when the engine could not be started at all.
	printf( "could not start %s - is it beside this program?\n", engine );
	WaitIfWeOwnTheConsole();
	return 1;
}
