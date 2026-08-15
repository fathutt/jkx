/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// Finding the games, against a machine that does not exist.
//
// Everything this code learns about the world arrives through four callbacks,
// so the whole search can be run against a registry and a filesystem written
// here - including the ones nobody has: a Steam install with three libraries on
// three drives, a libraryfolders.vdf in the format Steam used four years ago, a
// GOG key with six games in it. On the machine this was written on there is no
// registry at all.
//
// What is NOT checked here is whether a candidate really is a game: that is
// jkx_install_scan.cpp and tests/install_scan_test.cpp. This half is allowed to
// be generous and the test says so by asserting that it offers MORE than the
// right answer.

#include "../code/launcher/jkx_install_find.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int s_failures = 0;

#define CHECK( cond, ... ) \
	do { \
		if ( !( cond ) ) { \
			printf( "  FAIL " ); printf( __VA_ARGS__ ); printf( "\n" ); \
			s_failures++; \
		} \
	} while ( 0 )

/*
===============================================================================

A machine, made up

===============================================================================
*/

struct FakeMachine {
	// key -> value -> data
	std::vector<std::pair<std::string, std::pair<std::string, std::string>>> registry;
	// key -> subkey names
	std::vector<std::pair<std::string, std::vector<std::string>>> subKeys;
	// directory -> entry names
	std::vector<std::pair<std::string, std::vector<std::string>>> dirs;
	// path -> contents
	std::vector<std::pair<std::string, std::string>> files;
};

static std::string Lower( const std::string &s )
{
	std::string out = s;
	for ( size_t i = 0; i < out.size(); i++ ) {
		if ( out[i] >= 'A' && out[i] <= 'Z' ) {
			out[i] = (char)( out[i] - 'A' + 'a' );
		}
		if ( out[i] == '/' ) {
			out[i] = '\\';
		}
	}
	return out;
}

static int FakeReadRegistry( void *user, installRegRoot_t root, const char *key,
							 const char *value, char *out, int outSize )
{
	const FakeMachine *m = (const FakeMachine *)user;
	const std::string full = std::string( root == JKX_REG_LOCAL_MACHINE ? "HKLM\\" : "HKCU\\" ) + key;

	for ( size_t i = 0; i < m->registry.size(); i++ ) {
		if ( Lower( m->registry[i].first ) == Lower( full )
			 && Lower( m->registry[i].second.first ) == Lower( value ) ) {
			snprintf( out, (size_t)outSize, "%s", m->registry[i].second.second.c_str() );
			return 1;
		}
	}
	return 0;
}

static int FakeListSubKeys( void *user, installRegRoot_t root, const char *key,
							char *out, int outStride, int max )
{
	const FakeMachine *m = (const FakeMachine *)user;
	const std::string full = std::string( root == JKX_REG_LOCAL_MACHINE ? "HKLM\\" : "HKCU\\" ) + key;
	int count = 0;

	for ( size_t i = 0; i < m->subKeys.size(); i++ ) {
		if ( Lower( m->subKeys[i].first ) != Lower( full ) ) {
			continue;
		}
		for ( size_t j = 0; j < m->subKeys[i].second.size() && count < max; j++ ) {
			snprintf( out + (size_t)count * outStride, (size_t)outStride, "%s",
					  m->subKeys[i].second[j].c_str() );
			count++;
		}
	}
	return count;
}

static int FakeListDirs( void *user, const char *path, char *out, int outStride, int max )
{
	const FakeMachine *m = (const FakeMachine *)user;
	int count = 0;

	for ( size_t i = 0; i < m->dirs.size(); i++ ) {
		if ( Lower( m->dirs[i].first ) != Lower( path ) ) {
			continue;
		}
		for ( size_t j = 0; j < m->dirs[i].second.size() && count < max; j++ ) {
			snprintf( out + (size_t)count * outStride, (size_t)outStride, "%s",
					  m->dirs[i].second[j].c_str() );
			count++;
		}
	}
	return count;
}

static int FakeReadText( void *user, const char *path, char *out, int outSize )
{
	const FakeMachine *m = (const FakeMachine *)user;

	for ( size_t i = 0; i < m->files.size(); i++ ) {
		if ( Lower( m->files[i].first ) != Lower( path ) ) {
			continue;
		}
		if ( (int)m->files[i].second.size() + 1 > outSize ) {
			return 0;
		}
		memcpy( out, m->files[i].second.data(), m->files[i].second.size() );
		out[m->files[i].second.size()] = '\0';
		return (int)m->files[i].second.size();
	}
	return 0;
}

static installSearch_t SearchOver( FakeMachine *m )
{
	installSearch_t s;

	s.readRegistry	= FakeReadRegistry;
	s.listSubKeys	= FakeListSubKeys;
	s.listDirs		= FakeListDirs;
	s.readText		= FakeReadText;
	s.user			= m;
	return s;
}

typedef char candidateList_t[JKX_FIND_MAX_CANDIDATES][JKX_FIND_MAX_PATH];

static int Offers( candidateList_t list, int count, const char *wanted )
{
	for ( int i = 0; i < count; i++ ) {
		if ( Lower( list[i] ) == Lower( wanted ) ) {
			return 1;
		}
	}
	return 0;
}

/*
===============================================================================

The file format

===============================================================================
*/

static void TestLibraryFolders( void )
{
	// The shape Steam writes today. Note the doubled backslashes: they are what
	// is in the file, and a path taken from it literally does not open.
	{
		const char *vdf =
			"\"libraryfolders\"\n"
			"{\n"
			"\t\"0\"\n"
			"\t{\n"
			"\t\t\"path\"\t\t\"C:\\\\Program Files (x86)\\\\Steam\"\n"
			"\t\t\"label\"\t\t\"\"\n"
			"\t\t\"contentid\"\t\t\"123\"\n"
			"\t\t\"apps\"\n"
			"\t\t{\n"
			"\t\t\t\"6020\"\t\t\"1234567\"\n"
			"\t\t}\n"
			"\t}\n"
			"\t\"1\"\n"
			"\t{\n"
			"\t\t\"path\"\t\t\"D:\\\\Games\\\\Steam\"\n"
			"\t}\n"
			"}\n";

		candidateList_t got;
		const int n = InstallFind_ParseLibraryFolders( vdf, &got[0][0], JKX_FIND_MAX_PATH,
													  JKX_FIND_MAX_CANDIDATES );

		CHECK( n == 2, "current vdf gave %d libraries, expected 2", n );
		CHECK( Offers( got, n, "C:\\Program Files (x86)\\Steam" ),
			   "the first library is missing - escapes not undone?" );
		CHECK( Offers( got, n, "D:\\Games\\Steam" ), "the second library is missing" );

		// "contentid" has a numeric value and "apps" holds numeric keys with
		// numeric values. Neither is a path and neither may be offered as one.
		CHECK( !Offers( got, n, "123" ), "a content id was offered as a library" );
		CHECK( !Offers( got, n, "1234567" ), "an app size was offered as a library" );
	}

	// The shape Steam used before that: the numbered key IS the path.
	{
		const char *vdf =
			"\"LibraryFolders\"\n"
			"{\n"
			"\t\"TimeNextStatsReport\"\t\"1500000000\"\n"
			"\t\"ContentStatsID\"\t\"-1234567890\"\n"
			"\t\"1\"\t\t\"E:\\\\SteamLibrary\"\n"
			"}\n";

		candidateList_t got;
		const int n = InstallFind_ParseLibraryFolders( vdf, &got[0][0], JKX_FIND_MAX_PATH,
													  JKX_FIND_MAX_CANDIDATES );

		CHECK( n == 1, "old vdf gave %d libraries, expected 1", n );
		CHECK( Offers( got, n, "E:\\SteamLibrary" ), "the old-format library is missing" );
	}

	// Things that are not a file.
	{
		candidateList_t got;

		CHECK( InstallFind_ParseLibraryFolders( "", &got[0][0], JKX_FIND_MAX_PATH,
												JKX_FIND_MAX_CANDIDATES ) == 0,
			   "an empty file produced libraries" );
		CHECK( InstallFind_ParseLibraryFolders( NULL, &got[0][0], JKX_FIND_MAX_PATH,
												JKX_FIND_MAX_CANDIDATES ) == 0,
			   "a null file produced libraries" );
		CHECK( InstallFind_ParseLibraryFolders( "\"path\"", &got[0][0], JKX_FIND_MAX_PATH,
												JKX_FIND_MAX_CANDIDATES ) == 0,
			   "a key with no value produced a library" );
		// An unterminated quote at the end of the file must not walk off it.
		// Under the sanitiser this is the check that matters most here.
		CHECK( InstallFind_ParseLibraryFolders( "\"path\" \"C:\\\\x", &got[0][0],
												JKX_FIND_MAX_PATH, JKX_FIND_MAX_CANDIDATES ) <= 1,
			   "an unterminated string produced more than one library" );
	}
}

/*
===============================================================================

The whole search

===============================================================================
*/

static void TestSteam( void )
{
	FakeMachine m;

	m.registry.push_back( { "HKCU\\Software\\Valve\\Steam",
							{ "SteamPath", "C:\\Program Files (x86)\\Steam" } } );

	m.files.push_back( { "C:\\Program Files (x86)\\Steam\\steamapps\\libraryfolders.vdf",
						 "\"libraryfolders\"\n{\n"
						 "\t\"0\"\n\t{\n\t\t\"path\"\t\"C:\\\\Program Files (x86)\\\\Steam\"\n\t}\n"
						 "\t\"1\"\n\t{\n\t\t\"path\"\t\"D:\\\\SteamLibrary\"\n\t}\n}\n" } );

	m.dirs.push_back( { "C:\\Program Files (x86)\\Steam\\steamapps\\common",
						{ "Half-Life", "Jedi Academy" } } );
	m.dirs.push_back( { "D:\\SteamLibrary\\steamapps\\common",
						{ "Star Wars Jedi Knight II - Jedi Outcast" } } );

	installSearch_t search = SearchOver( &m );
	candidateList_t got;
	const int n = InstallFind_Candidates( &search, &got[0][0], JKX_FIND_MAX_PATH,
										  JKX_FIND_MAX_CANDIDATES );

	CHECK( Offers( got, n, "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Jedi Academy\\GameData" ),
		   "Academy in the default library was not offered" );
	CHECK( Offers( got, n, "D:\\SteamLibrary\\steamapps\\common\\Star Wars Jedi Knight II - Jedi Outcast\\GameData" ),
		   "Outcast on the second drive was not offered - libraryfolders.vdf not read?" );

	// Generous on purpose: this half does not know what a game looks like, and
	// a candidate that is not one costs a failed archive open. Half-Life being
	// on the list is the design working, not a defect.
	CHECK( Offers( got, n, "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Half-Life" ),
		   "the search decided by itself which folders were worth offering" );

	// Both shapes, because a GOG install can be either and a hand-copied one
	// can be anything.
	CHECK( Offers( got, n, "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Jedi Academy" ),
		   "the folder itself was not offered, only GameData under it" );
}

static void TestGog( void )
{
	FakeMachine m;

	m.subKeys.push_back( { "HKLM\\SOFTWARE\\WOW6432Node\\GOG.com\\Games",
						   { "1421404433", "2018847457" } } );
	m.registry.push_back( { "HKLM\\SOFTWARE\\WOW6432Node\\GOG.com\\Games\\1421404433",
							{ "path", "C:\\GOG Games\\Jedi Knight Jedi Academy" } } );
	m.registry.push_back( { "HKLM\\SOFTWARE\\WOW6432Node\\GOG.com\\Games\\2018847457",
							{ "path", "C:\\GOG Games\\Jedi Knight II" } } );

	installSearch_t search = SearchOver( &m );
	candidateList_t got;
	const int n = InstallFind_Candidates( &search, &got[0][0], JKX_FIND_MAX_PATH,
										  JKX_FIND_MAX_CANDIDATES );

	CHECK( Offers( got, n, "C:\\GOG Games\\Jedi Knight Jedi Academy\\GameData" ),
		   "the GOG Academy install was not offered" );
	CHECK( Offers( got, n, "C:\\GOG Games\\Jedi Knight II" ),
		   "the GOG Outcast install was not offered without the GameData suffix" );
}

static void TestNothingInstalled( void )
{
	FakeMachine m;
	installSearch_t search = SearchOver( &m );
	candidateList_t got;
	const int n = InstallFind_Candidates( &search, &got[0][0], JKX_FIND_MAX_PATH,
										  JKX_FIND_MAX_CANDIDATES );

	// Not zero: the fixed places are always worth a look, and a machine with no
	// Steam and no GOG is a machine where the game was installed from a disc.
	// What matters is that nothing crashed and nothing invented a Steam library.
	CHECK( n > 0, "with no Steam and no GOG, nothing at all was offered" );
	for ( int i = 0; i < n; i++ ) {
		CHECK( strstr( got[i], "steamapps\\common\\Jedi" ) != NULL
			   || strstr( got[i], "LucasArts" ) != NULL
			   || strstr( got[i], "Steam\\steamapps" ) != NULL,
			   "with no registry at all, %s was offered from somewhere unexpected", got[i] );
	}
}

static void TestDuplicatesAndLimits( void )
{
	FakeMachine m;

	// The same library named twice, once with forward slashes. Steam has
	// written both.
	m.registry.push_back( { "HKCU\\Software\\Valve\\Steam",
							{ "SteamPath", "C:/Program Files (x86)/Steam" } } );
	m.files.push_back( { "C:/Program Files (x86)/Steam\\steamapps\\libraryfolders.vdf",
						 "\"libraryfolders\"\n{\n"
						 "\t\"0\"\n\t{\n\t\t\"path\"\t\"C:\\\\Program Files (x86)\\\\Steam\"\n\t}\n}\n" } );
	m.dirs.push_back( { "C:/Program Files (x86)/Steam\\steamapps\\common", { "Jedi Academy" } } );
	m.dirs.push_back( { "C:\\Program Files (x86)\\Steam\\steamapps\\common", { "Jedi Academy" } } );

	installSearch_t search = SearchOver( &m );
	candidateList_t got;
	const int n = InstallFind_Candidates( &search, &got[0][0], JKX_FIND_MAX_PATH,
										  JKX_FIND_MAX_CANDIDATES );

	// The same folder arrives three times over: from the Steam key with forward
	// slashes, from libraryfolders.vdf with backslashes, and from the list of
	// fixed places with backslashes again. It may appear once.
	//
	// The fixed places also hold other paths ending in "Jedi Academy\GameData"
	// under other program-files roots, and those are different folders - so
	// this counts exact matches rather than anything ending in the same name,
	// which is what an earlier version of this check got wrong.
	int academy = 0;
	for ( int i = 0; i < n; i++ ) {
		if ( Lower( got[i] ) ==
			 Lower( "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Jedi Academy\\GameData" ) ) {
			academy++;
		}
	}
	CHECK( academy == 1, "the same install was offered %d times - the user would be "
		   "asked to choose between one game and itself", academy );

	// The limit is respected even when there is more to say than room to say
	// it, which is the case that overruns a buffer if it is not.
	{
		char small[3][JKX_FIND_MAX_PATH];
		const int few = InstallFind_Candidates( &search, &small[0][0], JKX_FIND_MAX_PATH, 3 );

		CHECK( few <= 3, "asked for at most 3 candidates and got %d", few );
	}
}

int main( void )
{
	TestLibraryFolders();
	TestSteam();
	TestGog();
	TestNothingInstalled();
	TestDuplicatesAndLimits();

	if ( s_failures ) {
		printf( "%d check(s) failed\n", s_failures );
		return 1;
	}

	printf( "OK: install discovery - Steam libraries in both vdf formats, GOG, "
			"fixed places, duplicates, limits\n" );
	return 0;
}
