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

// What on the launcher's command line is a game directory, and what belongs to
// the engine.
//
// The launcher used to take one thing and drop everything else, which made it a
// door with no handle: the engine takes its whole configuration on the command
// line, so "+set logfile 0" or "+devmap t1_sour" meant running the engine by
// hand, and running it by hand meant working out fs_cdpath by hand - the one
// thing this program exists to know. The first check of the log folder was done
// that way, and it should not have been.
//
// The split is at the first word beginning with '+' or '-', not word by word,
// because that is the shape of an engine command line: "+set name value" is
// three words and only the first carries a mark. Everything from that word
// onward is the engine's, verbatim.
//
// It lives in its own file with no includes and no platform in it so that a
// test can hand it argument lists that no one would type by accident.

typedef struct {
	const char	*directory;		// the game directory, or NULL if none was given
	char		**pass;			// the first argument meant for the engine
	int			passCount;		// how many, from pass onward
	int			extra;			// a second directory was given: this many too many
} launcherArgs_t;

// Split argv. argc and argv are as main received them, so argv[0] is the
// program and is skipped.
//
// A second bare word is not a directory and not for the engine, so it is an
// error the caller has to report: extra says how many bare words came after the
// first, and directory holds the first one regardless.
launcherArgs_t	Launcher_SplitArgs( int argc, char **argv );
