/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// One program that starts the right game.
//
// A player should not have to know that jkx_jka is Academy, that jkx_jk2 is
// Outcast, and that each one needs its own installation with its own assets.
// Getting it wrong does not even say so: starting the Outcast engine in an
// Academy directory produces a message about a string package called CON_TEXT,
// which is a true statement about Outcast's data and tells nobody anything.
// That is how this was found.
//
// So this looks for the installations - Steam's registry key and its library
// list, GOG's, and the places installers used before either existed - works out
// which game each directory holds by looking inside its assets, and offers what
// it found. On Windows that offer is a window with a button per game; elsewhere
// it is a list on the console.
//
// The three parts are separate on purpose and only the first is interesting:
//   jkx_install_scan.cpp  - what game is this directory, tested against a
//                           filesystem that does not exist
//   jkx_install_find.cpp  - where might a game be, tested against a machine
//                           that does not exist
//   jkx_launcher_win32.cpp- the window and the registry, which can only be
//                           looked at on Windows
// This file is what joins them, and it is deliberately the thin one.

#include "jkx_launcher.h"
#include "jkx_install_find.h"
#include "jkx_launcher_args.h"

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


/*
===============================================================================

Paths

===============================================================================
*/

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


// Two paths naming the same folder. Case-insensitively on Windows, where they
// do, and exactly everywhere else, where they do not - comparing exactly on
// Windows would remember the same install twice under two spellings, which is
// how a "choose between two games" prompt turns into a prompt offering the same
// game twice.
static int PathsAreTheSame( const char *a, const char *b )
{
#ifdef _WIN32
	return _stricmp( a, b ) == 0;
#else
	return strcmp( a, b ) == 0;
#endif
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


/*
===============================================================================

Looking inside a directory

===============================================================================
*/

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


int Launcher_Identify( const char *path, launcherFound_t *out )
{
	probeContext_t	ctx;
	installProbe_t	probe;
	installResult_t	result;

	snprintf( ctx.root, sizeof( ctx.root ), "%s", path );

	probe.fileExists	= RealFileExists;
	probe.archiveHas	= RealArchiveHas;
	probe.user			= &ctx;

	result = InstallScan_Identify( &probe );

	snprintf( out->root, sizeof( out->root ), "%s", path );
	snprintf( out->reason, sizeof( out->reason ), "%s", result.reason );
	out->kind = result.kind;

	return ( result.kind == INSTALL_ACADEMY || result.kind == INSTALL_OUTCAST );
}


const char *Launcher_GameName( installKind_t kind )
{
	if ( kind == INSTALL_ACADEMY ) {
		return "Jedi Knight: Jedi Academy";
	}
	if ( kind == INSTALL_OUTCAST ) {
		return "Jedi Knight II: Jedi Outcast";
	}
	return "unknown";
}


/*
===============================================================================

Directories this has started a game from before

Discovering an installation is the right answer and is done below; this is what
covers the case discovery cannot, which is a copy of the game somewhere nobody
would think to look. Pointed at it once, it is remembered.

===============================================================================
*/

#define JKX_MAX_REMEMBERED	8

typedef struct {
	char	path[JKX_MAX_REMEMBERED][JKX_MAX_PATH];
	int		count;
} rememberedList_t;


static void RememberedFile( char *out, size_t outSize, const char *here )
{
	JoinPath( out, outSize, here, "jkx_launcher.txt" );
}


static void LoadRemembered( rememberedList_t *list, const char *here )
{
	char	file[JKX_MAX_PATH];
	char	line[JKX_MAX_PATH];
	FILE	*f;

	list->count = 0;

	RememberedFile( file, sizeof( file ), here );
	f = fopen( file, "r" );
	if ( !f ) {
		return;
	}

	while ( list->count < JKX_MAX_REMEMBERED && fgets( line, sizeof( line ), f ) ) {
		size_t	n = strlen( line );

		while ( n > 0 && ( line[n - 1] == '\n' || line[n - 1] == '\r' ) ) {
			line[--n] = '\0';
		}
		if ( n == 0 ) {
			continue;
		}
		snprintf( list->path[list->count], JKX_MAX_PATH, "%s", line );
		list->count++;
	}

	fclose( f );
}


// The one just used goes first, so the list is in the order a person last cared
// about. Writing it is best effort: a launcher unpacked somewhere unwritable
// still works, it just searches again next time, and saying so on every run
// would be noise about something nobody asked for.
void Launcher_Remember( const char *here, const char *path )
{
	rememberedList_t	list;
	char				file[JKX_MAX_PATH];
	FILE				*f;

	LoadRemembered( &list, here );

	RememberedFile( file, sizeof( file ), here );
	f = fopen( file, "w" );
	if ( !f ) {
		return;
	}

	fprintf( f, "%s\n", path );
	for ( int i = 0; i < list.count; i++ ) {
		if ( PathsAreTheSame( list.path[i], path ) ) {
			continue;
		}
		fprintf( f, "%s\n", list.path[i] );
	}

	fclose( f );
}


/*
===============================================================================

The search

===============================================================================
*/

static int AlreadyFound( const launcherFound_t *found, int count, const char *path )
{
	for ( int i = 0; i < count; i++ ) {
		if ( PathsAreTheSame( found[i].root, path ) ) {
			return 1;
		}
	}
	return 0;
}


int Launcher_Search( const char *here, const char *explicitPath,
					 launcherFound_t *out, int max )
{
	rememberedList_t	remembered;
	launcherFound_t		one;
	int					count = 0;

	if ( explicitPath != NULL ) {
		if ( Launcher_Identify( explicitPath, &one ) ) {
			out[count++] = one;
		} else {
			// Not silently: a path given by hand that turns out not to be a
			// game is the user's question answered, and the answer is the
			// reason string.
			printf( "%s: %s\n", one.root, one.reason );
		}
		return count;
	}

	// Remembered first. It is the only source that knows about an installation
	// nothing else can see, and it is the one the user chose last time.
	LoadRemembered( &remembered, here );
	for ( int i = 0; i < remembered.count && count < max; i++ ) {
		if ( AlreadyFound( out, count, remembered.path[i] ) ) {
			continue;
		}
		if ( Launcher_Identify( remembered.path[i], &one ) ) {
			out[count++] = one;
		}
		// A remembered path that no longer holds a game is not news: the game
		// moved or went away, and the list drops it without a word.
	}

	// Then whatever the machine's own records say. This is the part that means
	// a first run has nothing to do.
	{
		static char	candidates[JKX_FIND_MAX_CANDIDATES][JKX_FIND_MAX_PATH];
		const int	n = Launcher_PlatformCandidates( &candidates[0][0],
													 JKX_FIND_MAX_PATH,
													 JKX_FIND_MAX_CANDIDATES );

		for ( int i = 0; i < n && count < max; i++ ) {
			if ( AlreadyFound( out, count, candidates[i] ) ) {
				continue;
			}
			if ( Launcher_Identify( candidates[i], &one ) ) {
				out[count++] = one;
			}
		}
	}

	// And last, where this program is standing. An engine unpacked inside a
	// game directory is the arrangement people have been using for twenty
	// years, and it costs two stat calls to support.
	{
		char		up[JKX_MAX_PATH];
		const char	*places[3];

		DirectoryOf( up, sizeof( up ), here );
		places[0] = ".";
		places[1] = here;
		places[2] = up;

		for ( int i = 0; i < 3 && count < max; i++ ) {
			if ( AlreadyFound( out, count, places[i] ) ) {
				continue;
			}
			if ( Launcher_Identify( places[i], &one ) ) {
				out[count++] = one;
			}
		}
	}

	return count;
}


static void ReportProblem( const char *text );

/*
===============================================================================

Starting it

===============================================================================
*/

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


void Launcher_Start( const char *here, const launcherFound_t *game,
					 char **pass, int passCount )
{
	char		engine[JKX_MAX_PATH];
	const char	*name = EngineNames( game->kind, 0 );

	if ( !name ) {
		return;
	}

	// The engine lives beside the launcher, not inside the game directory. A
	// copy in the game directory is what a mod is, and the retail folder is not
	// ours to write into.
	JoinPath( engine, sizeof( engine ), here, name );

	{
		struct stat	st;

		if ( stat( engine, &st ) != 0 ) {
			JoinPath( engine, sizeof( engine ), here, EngineNames( game->kind, 1 ) );
		}
	}

	Launcher_Remember( here, game->root );

	{
		// Two paths, not one, and which is which matters.
		//
		// The retail folder goes in fs_cdpath and OUR folder in fs_basepath.
		// The engine searches cdpath, then basepath, then homepath, and each
		// one it adds takes priority over the last - so the retail assets are
		// found and everything this package ships overrides them. Putting the
		// retail folder in fs_basepath, which is what this did first, drops our
		// own directory out of the search entirely: shaders.pak lives there,
		// the renderer loads it at startup, and the game stops on the first
		// pipeline it tries to build.
		//
		// Nothing is written into the retail folder either way. fs_homepath is
		// where saves and configs go and the engine picks that itself.
		char	root[JKX_MAX_PATH];
		char	own[JKX_MAX_PATH];
		char	*args[8 + JKX_MAX_PASS];
		char	set1[] = "+set";
		char	set2[] = "+set";
		char	fsbase[] = "fs_basepath";
		char	fscd[] = "fs_cdpath";
		int		n = 0;

		snprintf( root, sizeof( root ), "%s", game->root );
		snprintf( own, sizeof( own ), "%s", here );

		args[n++] = engine;
		args[n++] = set1;
		args[n++] = fsbase;
		args[n++] = own;
		args[n++] = set2;
		args[n++] = fscd;
		args[n++] = root;

		// Everything the person put after the directory, passed straight
		// through.
		//
		// Without this the launcher was a door with no handle: the engine takes
		// its whole configuration on the command line, so "+set logfile 0" or
		// "+devmap t1_sour" had to be run by hand, and running it by hand meant
		// working out fs_cdpath by hand, which is the one thing this program
		// exists to know. That is how the first check of the log folder was
		// done, and it should not have been.
		//
		// These go AFTER the two paths, so a person can override either. The
		// engine takes the last setting of a cvar on the command line, and
		// somebody typing fs_cdpath means it.
		for ( int i = 0; i < passCount; i++ ) {
			args[n++] = pass[i];
		}

		args[n] = NULL;

		printf( "starting %s\n", engine );
		fflush( stdout );
		JKX_EXEC( engine, args );
	}

	{
		char	complaint[JKX_MAX_PATH + 128];

		snprintf( complaint, sizeof( complaint ),
			"Could not start %s.\n\nThe engine has to be in the same folder as "
			"this program.", engine );
		ReportProblem( complaint );
	}
}


/*
===============================================================================

Without a window

===============================================================================
*/

#ifndef _WIN32
int Launcher_PlatformCandidates( char *out, int outStride, int max )
{
	// Steam and GOG both keep their records in the Windows registry, and this
	// launcher exists for the case where somebody double-clicks something.
	// Rather than half-find things here, this says nothing and the console path
	// below asks for a directory - which is what a person on this platform was
	// going to do anyway.
	(void)out;
	(void)outStride;
	(void)max;
	return 0;
}

int Launcher_ChooseInWindow( launcherFound_t *found, int *count, int max )
{
	(void)found;
	(void)count;
	(void)max;
	return JKX_UI_UNAVAILABLE;
}
#endif


// This is a windowed program on Windows - it has to be, or every start of it
// flashes a black rectangle before the window appears - and a windowed program
// gets no standard output at all. Started FROM a console there is one to borrow,
// and borrowing it is what makes the same binary usable from a shell.
static void AttachToAnyConsole( void )
{
#ifdef _WIN32
	if ( GetConsoleWindow() != NULL ) {
		return;
	}
	if ( !AttachConsole( ATTACH_PARENT_PROCESS ) ) {
		return;
	}

	// The handles have to be reopened onto it: attaching gives the process a
	// console, not a stdout.
	FILE	*ignored;

	freopen_s( &ignored, "CONOUT$", "w", stdout );
	freopen_s( &ignored, "CONOUT$", "w", stderr );
	freopen_s( &ignored, "CONIN$", "r", stdin );
#endif
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

	if ( GetConsoleWindow() == NULL ) {
		return;
	}
	if ( GetConsoleProcessList( pids, 2 ) == 1 ) {
		printf( "\nPress Enter to close." );
		fflush( stdout );
		(void)getchar();
	}
#endif
}


// Something went wrong and there may be nowhere to say so. A windowed program
// started from Explorer has no console at all, and a message printed into
// nothing is the same as no message - which is the failure mode this whole
// program exists to avoid.
static void ReportProblem( const char *text )
{
	printf( "%s\n", text );
	fflush( stdout );

#ifdef _WIN32
	if ( GetConsoleWindow() == NULL ) {
		MessageBoxA( NULL, text, "JKX", MB_OK | MB_ICONWARNING );
	}
#endif
}


static int ChooseOnConsole( const launcherFound_t *found, int count )
{
	char	answer[16];

	if ( count == 1 ) {
		return 0;
	}

	printf( "\nMore than one game found:\n\n" );
	for ( int i = 0; i < count; i++ ) {
		printf( "  %d) %s\n     %s\n", i + 1, Launcher_GameName( found[i].kind ),
				found[i].root );
	}
	printf( "\nWhich one? [1-%d, Enter for 1] ", count );
	fflush( stdout );

	if ( fgets( answer, sizeof( answer ), stdin ) ) {
		const int	picked = atoi( answer );

		if ( picked >= 1 && picked <= count ) {
			return picked - 1;
		}
	}
	return 0;
}


int main( int argc, char **argv )
{
	launcherFound_t	found[JKX_MAX_FOUND];
	char			here[JKX_MAX_PATH];
	int				count;
	int				chosen;

	// What is a directory and what is for the engine. See jkx_launcher_args.h;
	// the split is a unit of its own so that a test can hand it argument lists
	// nobody would type by accident.
	const launcherArgs_t	split = Launcher_SplitArgs( argc, argv );
	const char				*directory = split.directory;
	char					**pass = split.pass;
	int						passCount = split.passCount;

	if ( split.extra ) {
		printf( "usage: %s [game directory] [+set name value ...]\n", argv[0] );
		printf( "\nWith no argument it looks for the games itself - Steam's records,\n"
			"GOG's, the folders installers used before either, and anywhere it has\n"
			"started a game from before. Give it a directory to use that one: the\n"
			"folder with base/ in it, which for a Steam or GOG install is the one\n"
			"called GameData.\n"
			"\nAnything beginning with + or - is handed to the engine as it stands,\n"
			"after the paths this program works out, so it can override them.\n" );
		WaitIfWeOwnTheConsole();
		return 2;
	}

	if ( passCount > JKX_MAX_PASS ) {
		printf( "only the first %d argument(s) are passed on; the rest are dropped\n",
				JKX_MAX_PASS );
		passCount = JKX_MAX_PASS;
	}

	AttachToAnyConsole();

	DirectoryOf( here, sizeof( here ), argv[0] );

	count = Launcher_Search( here, directory, found, JKX_MAX_FOUND );

	// The window, when there is one and the user did not already say which
	// directory they meant. It can add to the list - there is a button on it
	// for picking a folder - so it takes count by pointer.
	if ( directory == NULL ) {
		chosen = Launcher_ChooseInWindow( found, &count, JKX_MAX_FOUND );

		if ( chosen == JKX_UI_CLOSED ) {
			return 0;
		}
		if ( chosen != JKX_UI_UNAVAILABLE ) {
			Launcher_Start( here, &found[chosen], pass, passCount );
			WaitIfWeOwnTheConsole();
			return 1;
		}
	}

	if ( count == 0 ) {
		ReportProblem( "No installation of Jedi Academy or Jedi Outcast was found.\n\n"
			"Give this the folder that has base in it - for a Steam or GOG install "
			"that is the one called GameData - by dragging it onto this program or "
			"naming it on the command line. It is remembered afterwards." );
		WaitIfWeOwnTheConsole();
		return 1;
	}

	chosen = ChooseOnConsole( found, count );

	printf( "%s: %s\n", found[chosen].root, found[chosen].reason );
	Launcher_Start( here, &found[chosen], pass, passCount );

	WaitIfWeOwnTheConsole();
	return 1;
}
