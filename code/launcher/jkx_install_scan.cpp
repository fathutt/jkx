/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

#include "jkx_install_scan.h"


// The archives a retail install has in base/. Both games number them from
// zero, which is exactly why the count cannot be used to tell them apart -
// Academy ships four and Outcast three, and a player who deleted one, or a
// Steam install mid-download, breaks any rule built on that. They are listed
// here only so that the HUD question can be asked of each in turn: which
// archive carries the layout is a packing decision and not a promise.
static const char *installArchives[] = {
	"base/assets0.pk3",
	"base/assets1.pk3",
	"base/assets2.pk3",
	"base/assets3.pk3",
	"base/assets5.pk3"
};

#define INSTALL_ARCHIVE_COUNT	( sizeof( installArchives ) / sizeof( installArchives[0] ) )

// What each game's assets carry and the other's do not. The engine refuses to
// reach a map without its own one of these, so a directory that has neither is
// not an installation this launcher can offer.
#define INSTALL_ACADEMY_HUD		"ui/jahud.txt"
#define INSTALL_OUTCAST_HUD		"ui/jk2hud.txt"


static int InstallScan_AnyArchiveHas( const installProbe_t *probe, const char *name )
{
	size_t	i;

	for ( i = 0; i < INSTALL_ARCHIVE_COUNT; i++ ) {
		if ( !probe->fileExists( probe->user, installArchives[i] ) ) {
			continue;
		}
		if ( probe->archiveHas( probe->user, installArchives[i], name ) ) {
			return 1;
		}
	}
	return 0;
}


static int InstallScan_AnyArchive( const installProbe_t *probe )
{
	size_t	i;

	for ( i = 0; i < INSTALL_ARCHIVE_COUNT; i++ ) {
		if ( probe->fileExists( probe->user, installArchives[i] ) ) {
			return 1;
		}
	}
	return 0;
}


installResult_t InstallScan_Identify( const installProbe_t *probe )
{
	installResult_t	out;
	int				academy, outcast;

	out.kind = INSTALL_NONE;
	out.reason = "nothing was looked at";

	if ( !probe || !probe->fileExists || !probe->archiveHas ) {
		out.reason = "no way to look at this directory";
		return out;
	}

	// The cheapest question first, and the one whose answer is most often no:
	// a directory somebody pointed at by accident.
	if ( !probe->fileExists( probe->user, "base" ) ) {
		out.reason = "there is no base folder here, so this is not a game directory";
		return out;
	}

	if ( !InstallScan_AnyArchive( probe ) ) {
		out.reason = "base is here but has no assets pk3 in it - this looks like "
			"a mod folder or an unfinished download rather than an installation";
		return out;
	}

	academy = InstallScan_AnyArchiveHas( probe, INSTALL_ACADEMY_HUD );
	outcast = InstallScan_AnyArchiveHas( probe, INSTALL_OUTCAST_HUD );

	if ( academy && outcast ) {
		// Someone has merged the two, or dropped one game's assets into the
		// other's folder. Refusing is right: whichever engine is started here
		// will find both games' data on its search path and load whichever
		// comes first, which is a much more confusing failure than this
		// sentence.
		out.kind = INSTALL_NONE;
		out.reason = "both games' assets are in this one folder, so there is no "
			"way to tell which game it is meant to be";
		return out;
	}

	if ( academy ) {
		out.kind = INSTALL_ACADEMY;
		out.reason = "the assets carry Jedi Academy's head-up display";
		return out;
	}

	if ( outcast ) {
		out.kind = INSTALL_OUTCAST;
		out.reason = "the assets carry Jedi Outcast's head-up display";
		return out;
	}

	// Archives are here and neither layout is in them. That is a real
	// installation of something else - a total conversion, or a game this
	// launcher does not know - and saying so is more use than guessing.
	out.reason = "there are assets here, but neither game's head-up display is "
		"in them, so this is not Jedi Outcast or Jedi Academy";
	return out;
}
