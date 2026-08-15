/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

#pragma once

// Which game is installed in this directory, and how sure are we.
//
// The launcher has to answer one question - "is this Jedi Outcast or Jedi
// Academy" - about a directory a player points it at, and the obvious answers
// are all wrong:
//
//   - the directory name is no help. Both games install into GameData;
//   - "base/ exists" is no help either. So does every mod folder, and so does
//     an empty directory somebody made by hand;
//   - the retail executable name (jasp.exe, jk2sp.exe) is a good HINT and not
//     an answer: a Steam install under Proton has them, a GOG one has them,
//     and a copy of just the assets has none. It is also exactly the file a
//     player deletes when they think they have finished with the old engine.
//
// The answer that does not depend on any of that is inside the archive. Each
// game's assets carry its own head-up display layout - ui/jahud.txt for
// Academy, ui/jk2hud.txt for Outcast - and the engine already refuses to start
// without one, so a copy that has neither is not an installation this launcher
// can offer whatever else is in it.
//
// This is deliberately dependency-free: it asks questions through two
// callbacks rather than touching the filesystem or minizip itself, so a test
// can answer them from a table and drive every branch without laying out a
// pretend Steam library on disk. The launcher wires the real ones in.

#include <stddef.h>

typedef enum {
	INSTALL_NONE,		// not an installation of either game
	INSTALL_ACADEMY,
	INSTALL_OUTCAST
} installKind_t;

typedef struct {
	// Does this path exist, relative to the directory being scanned?
	// e.g. "base/assets0.pk3"
	int ( *fileExists )( void *user, const char *relativePath );

	// Does this archive, relative to the directory, contain this name?
	// Answers 0 when the archive cannot be opened at all.
	int ( *archiveHas )( void *user, const char *archiveRelativePath,
			const char *nameInArchive );

	void	*user;
} installProbe_t;

typedef struct {
	installKind_t	kind;

	// Why, in a sentence a player can act on. Always set: when kind is
	// INSTALL_NONE this says what was looked for and not found, and that is
	// the whole point of the launcher existing.
	const char		*reason;
} installResult_t;

// Nothing is written and nothing is opened except through the probe.
installResult_t InstallScan_Identify( const installProbe_t *probe );
