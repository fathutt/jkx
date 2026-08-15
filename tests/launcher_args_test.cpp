/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// Splitting the launcher's command line, against lines nobody would type.
//
// The split itself is four lines, so the test is not really about the code - it
// is about the cases. A launcher is the one program in this project a person
// reaches by dragging a folder onto it, which means its argument list is
// whatever a file manager, a shortcut, or a shell script decided to hand over,
// and that includes things a person would never type: an empty word from an
// unset variable in quotes, a path that begins with a dash because someone
// named a folder that way, arguments and no directory at all.

#include "../code/launcher/jkx_launcher_args.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void Check( bool condition, const char *what )
{
	if ( !condition ) {
		printf( "  FAIL: %s\n", what );
		failures++;
	}
}

// argv as main gets it: argv[0] is the program.
//
// The storage outlives the call on purpose. launcherArgs_t.pass points INTO
// argv, the way main's does, and the first version of this helper built argv on
// its own stack and returned - which the sanitizer caught immediately as a
// stack-use-after-return. That is the test being wrong rather than the code,
// and it is worth leaving a note about: a function that hands back a pointer
// into its arguments is fine, and a caller that lets the arguments die is not.
static char *g_argv[16];

static launcherArgs_t Split( const char *const *words, int count )
{
	for ( int i = 0; i < count; i++ ) {
		g_argv[i] = (char *)words[i];
	}

	return Launcher_SplitArgs( count, g_argv );
}

static void TestNothing()
{
	const char *const words[] = { "jkx_launcher" };
	const launcherArgs_t a = Split( words, 1 );

	Check( a.directory == NULL, "no argument means no directory" );
	Check( a.passCount == 0, "and nothing to pass on" );
	Check( a.extra == 0, "and no complaint" );
}

static void TestDirectoryOnly()
{
	const char *const words[] = { "jkx_launcher", "D:\\Games\\GameData" };
	const launcherArgs_t a = Split( words, 2 );

	Check( a.directory != NULL && !strcmp( a.directory, "D:\\Games\\GameData" ),
		   "one bare word is the directory" );
	Check( a.passCount == 0, "and there is nothing for the engine" );
}

static void TestArgumentsOnly()
{
	// No directory at all, which is the case that matters: the launcher still
	// has to go looking for the games, and the window still has to appear.
	const char *const words[] = { "jkx_launcher", "+set", "logfile", "0" };
	const launcherArgs_t a = Split( words, 4 );

	Check( a.directory == NULL, "arguments alone leave the directory unset" );
	Check( a.passCount == 3, "and all three words go to the engine" );
	Check( a.pass != NULL && !strcmp( a.pass[0], "+set" ), "starting at the marked one" );
	Check( a.pass != NULL && !strcmp( a.pass[2], "0" ), "and including the value" );
}

static void TestBoth()
{
	const char *const words[] = { "jkx_launcher", "/games/GameData",
								  "+set", "logfile", "0", "+devmap", "t1_sour" };
	const launcherArgs_t a = Split( words, 7 );

	Check( a.directory != NULL && !strcmp( a.directory, "/games/GameData" ),
		   "the bare word before the first mark is the directory" );
	Check( a.passCount == 5, "and everything from the mark onward is the engine's" );
	Check( a.pass != NULL && !strcmp( a.pass[0], "+set" ), "first" );
	Check( a.pass != NULL && !strcmp( a.pass[4], "t1_sour" ), "last" );
}

static void TestUnmarkedWordsAfterAMarkAreNotDirectories()
{
	// This is the whole reason the split is at the first mark rather than word
	// by word. "logfile" and "0" are bare words and neither is a directory.
	const char *const words[] = { "jkx_launcher", "+set", "logfile", "0" };
	const launcherArgs_t a = Split( words, 4 );

	Check( a.extra == 0, "words belonging to +set are not extra directories" );
	Check( a.directory == NULL, "and none of them is the directory" );
}

static void TestSecondDirectoryIsAnError()
{
	const char *const words[] = { "jkx_launcher", "/one", "/two" };
	const launcherArgs_t a = Split( words, 3 );

	Check( a.directory != NULL && !strcmp( a.directory, "/one" ),
		   "the first bare word is still the directory" );
	Check( a.extra == 1, "and the second is counted so the caller can complain" );
}

static void TestDashIsForTheEngineToo()
{
	// Not hypothetical. The engine takes "-nolog" and friends, and a person who
	// has used any Quake engine will type a dash without thinking about it.
	const char *const words[] = { "jkx_launcher", "/games", "-nolog" };
	const launcherArgs_t a = Split( words, 3 );

	Check( a.passCount == 1 && !strcmp( a.pass[0], "-nolog" ),
		   "a dash marks the engine's half as well as a plus" );
}

static void TestEmptyWordsAreSkipped()
{
	// "$DIR" with DIR unset, from a launcher script. It is not a directory and
	// it must not become one, or the search is told to look in nowhere and
	// finds nothing.
	const char *const words[] = { "jkx_launcher", "", "/games/GameData", "" };
	const launcherArgs_t a = Split( words, 4 );

	Check( a.directory != NULL && !strcmp( a.directory, "/games/GameData" ),
		   "an empty word is not the directory" );
	Check( a.extra == 0, "nor is it a second one" );
	Check( a.passCount == 0, "nor is it for the engine" );
}

static void TestNullsAreSurvivable()
{
	char			*argv[3];
	launcherArgs_t	a;

	argv[0] = (char *)"jkx_launcher";
	argv[1] = NULL;
	argv[2] = (char *)"/games";

	a = Launcher_SplitArgs( 3, argv );
	Check( a.directory != NULL && !strcmp( a.directory, "/games" ),
		   "a null in the middle is stepped over" );

	a = Launcher_SplitArgs( 0, NULL );
	Check( a.directory == NULL && a.passCount == 0, "no argv at all is survivable" );
}

int main( void )
{
	printf( "launcher_args_test\n" );

	TestNothing();
	TestDirectoryOnly();
	TestArgumentsOnly();
	TestBoth();
	TestUnmarkedWordsAfterAMarkAreNotDirectories();
	TestSecondDirectoryIsAnError();
	TestDashIsForTheEngineToo();
	TestEmptyWordsAreSkipped();
	TestNullsAreSurvivable();

	if ( failures ) {
		printf( "%d check(s) failed\n", failures );
		return 1;
	}

	printf( "OK\n" );
	return 0;
}
