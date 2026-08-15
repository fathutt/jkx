/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

#include "jkx_launcher_args.h"

#include <stddef.h>

launcherArgs_t Launcher_SplitArgs( int argc, char **argv )
{
	launcherArgs_t	out;

	out.directory = NULL;
	out.pass = NULL;
	out.passCount = 0;
	out.extra = 0;

	if ( argc < 1 || argv == NULL ) {
		return out;
	}

	for ( int i = 1; i < argc; i++ ) {
		const char	*word = argv[i];

		if ( word == NULL ) {
			continue;
		}

		// An empty word is neither. It cannot be a directory - there is no
		// directory called nothing - and handing it to the engine would put an
		// empty argument in the middle of a command line for no reason. A shell
		// produces one from an unset variable in quotes, which is a thing that
		// happens in a launcher script rather than at a prompt.
		if ( word[0] == '\0' ) {
			continue;
		}

		if ( word[0] == '+' || word[0] == '-' ) {
			out.pass = &argv[i];
			out.passCount = argc - i;
			return out;
		}

		if ( out.directory != NULL ) {
			out.extra++;
			continue;
		}

		out.directory = word;
	}

	return out;
}
