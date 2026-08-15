/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// Which game is in this directory.
//
// The launcher's whole job starts here, and every wrong answer it can give
// costs the player something different: offering a folder that is not a game
// wastes their time, offering the wrong game starts an engine that will fail
// on a string package with a message about CON_TEXT, and refusing a real
// install sends them looking for a file that is already there.
//
// So each of those is a case below, driven through a fake filesystem rather
// than a real one. A test that lays out a pretend Steam library on disk can
// only check the shapes somebody thought to create; a table can be asked about
// the shapes nobody would create on purpose, which is where players actually
// live - half-deleted installs, merged folders, mod directories pointed at by
// mistake.

#include "../code/launcher/jkx_install_scan.h"

#include <cstdio>
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


// A directory that exists only as a list of what is in it.
struct FakeTree
{
	std::vector<std::string>							files;
	std::vector<std::pair<std::string, std::string> >	inArchives;

	void addFile( const char *path ) { files.push_back( path ); }
	void addToArchive( const char *archive, const char *name )
	{
		files.push_back( archive );
		inArchives.push_back( std::make_pair( std::string( archive ), std::string( name ) ) );
	}
};

int fakeExists( void *user, const char *path )
{
	const FakeTree	*t = (const FakeTree *)user;

	for ( size_t i = 0; i < t->files.size(); i++ ) {
		if ( t->files[i] == path ) {
			return 1;
		}
	}
	return 0;
}

int fakeArchiveHas( void *user, const char *archive, const char *name )
{
	const FakeTree	*t = (const FakeTree *)user;

	for ( size_t i = 0; i < t->inArchives.size(); i++ ) {
		if ( t->inArchives[i].first == archive && t->inArchives[i].second == name ) {
			return 1;
		}
	}
	return 0;
}

installResult_t identify( FakeTree &t )
{
	installProbe_t	probe;

	probe.fileExists = fakeExists;
	probe.archiveHas = fakeArchiveHas;
	probe.user = &t;
	return InstallScan_Identify( &probe );
}


void testTheTwoGames()
{
	{
		FakeTree	t;

		t.addFile( "base" );
		t.addToArchive( "base/assets0.pk3", "ui/jahud.txt" );
		t.addFile( "base/assets1.pk3" );
		t.addFile( "base/assets2.pk3" );
		t.addFile( "base/assets3.pk3" );

		const installResult_t r = identify( t );

		check( r.kind == INSTALL_ACADEMY, "an Academy install is Academy" );
	}

	{
		FakeTree	t;

		t.addFile( "base" );
		t.addToArchive( "base/assets0.pk3", "ui/jk2hud.txt" );
		t.addFile( "base/assets1.pk3" );
		t.addFile( "base/assets2.pk3" );

		const installResult_t r = identify( t );

		check( r.kind == INSTALL_OUTCAST, "an Outcast install is Outcast" );
	}

	// Which archive carries the layout is a packing decision, not a promise,
	// so the answer must not depend on it being the first one.
	{
		FakeTree	t;

		t.addFile( "base" );
		t.addFile( "base/assets0.pk3" );
		t.addToArchive( "base/assets2.pk3", "ui/jk2hud.txt" );

		const installResult_t r = identify( t );

		check( r.kind == INSTALL_OUTCAST,
			"the layout is found wherever it was packed" );
	}
}


void testTheWaysItIsNotAGame()
{
	{
		FakeTree			t;
		const installResult_t	r = identify( t );

		check( r.kind == INSTALL_NONE, "an empty directory is not a game" );
		check( strstr( r.reason, "base" ) != NULL,
			"and the reason names what was missing" );
	}

	// The one a player actually does: points the launcher at a mod folder, or
	// at an install whose archives have not downloaded yet.
	{
		FakeTree	t;

		t.addFile( "base" );
		t.addFile( "base/somemod.pk3" );

		const installResult_t r = identify( t );

		check( r.kind == INSTALL_NONE, "base with no assets is not a game" );
		check( strstr( r.reason, "mod folder" ) != NULL,
			"and the reason says what it probably is instead" );
	}

	// Archives, but of something else entirely.
	{
		FakeTree	t;

		t.addFile( "base" );
		t.addToArchive( "base/assets0.pk3", "ui/somethingelse.txt" );

		const installResult_t r = identify( t );

		check( r.kind == INSTALL_NONE,
			"assets without either head-up display are not one of these games" );
	}

	// Both games merged into one folder. Refusing is the right answer: an
	// engine started here would find both games' data on its search path.
	{
		FakeTree	t;

		t.addFile( "base" );
		t.addToArchive( "base/assets0.pk3", "ui/jahud.txt" );
		t.addToArchive( "base/assets1.pk3", "ui/jk2hud.txt" );

		const installResult_t r = identify( t );

		check( r.kind == INSTALL_NONE, "two games in one folder is refused" );
		check( strstr( r.reason, "both games" ) != NULL,
			"and the reason says why" );
	}
}


void testItNeverAnswersWithoutASentence()
{
	FakeTree	trees[4];

	trees[1].addFile( "base" );
	trees[2].addFile( "base" );
	trees[2].addFile( "base/assets0.pk3" );
	trees[3].addFile( "base" );
	trees[3].addToArchive( "base/assets0.pk3", "ui/jahud.txt" );

	for ( int i = 0; i < 4; i++ ) {
		const installResult_t r = identify( trees[i] );

		if ( !r.reason || !r.reason[0] ) {
			check( false, "every answer comes with a reason" );
			return;
		}
	}
	check( true, "every answer comes with a reason" );

	// And a probe that cannot answer anything is not a crash.
	{
		installProbe_t			probe = { NULL, NULL, NULL };
		const installResult_t	r = InstallScan_Identify( &probe );

		check( r.kind == INSTALL_NONE && r.reason && r.reason[0],
			"a probe with no callbacks is refused rather than followed" );
		check( InstallScan_Identify( NULL ).kind == INSTALL_NONE,
			"and so is no probe at all" );
	}
}

}	// namespace


int main( void )
{
	testTheTwoGames();
	testTheWaysItIsNotAGame();
	testItNeverAnswersWithoutASentence();

	printf( "install scan: %i check(s), %i failure(s)\n", g_checks, g_failures );
	return g_failures ? 1 : 0;
}
