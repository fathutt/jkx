/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// What the launcher's two halves say to each other.
//
// One half decides what is installed and starts it, and knows nothing about
// windows. The other half is a window, exists only on Windows, and knows
// nothing about pk3 files. Everything they share is here.

#ifndef JKX_LAUNCHER_H
#define JKX_LAUNCHER_H

#include "jkx_install_scan.h"

#define JKX_MAX_PATH	1024
#define JKX_MAX_FOUND	12

typedef struct {
	char			root[JKX_MAX_PATH];
	installKind_t	kind;
	char			reason[192];
} launcherFound_t;

#ifdef __cplusplus
extern "C" {
#endif

// Everything installed that this can find and identify, best first. When
// explicitPath is not NULL it is the only place looked at.
int			Launcher_Search( const char *here, const char *explicitPath,
							 launcherFound_t *out, int max );

// What one directory holds, for a path the user has just picked by hand.
int			Launcher_Identify( const char *path, launcherFound_t *out );

const char *Launcher_GameName( installKind_t kind );

// Puts this directory at the top of the list the launcher tries next time.
void		Launcher_Remember( const char *here, const char *path );

// Replaces this process with the engine. Only returns on failure.
void		Launcher_Start( const char *here, const launcherFound_t *game );

// ---------------------------------------------------------------------------
// Supplied per platform.

// Directories the system's own records say a game might be in. These have NOT
// been looked at; Launcher_Search identifies each one. Returns how many were
// written. Zero everywhere except Windows, where the registry is.
int			Launcher_PlatformCandidates( char *out, int outStride, int max );

// A window with a button per game. Returns the index the user chose, -1 if the
// window was closed without choosing, and -2 where there is no window to show -
// which is every platform except Windows, and Windows when a path was given on
// the command line.
//
// If the user picks a folder by hand, it is identified, appended to found, and
// its index returned; count is updated through the pointer.
#define JKX_UI_CLOSED		(-1)
#define JKX_UI_UNAVAILABLE	(-2)

int			Launcher_ChooseInWindow( launcherFound_t *found, int *count, int max );

#ifdef __cplusplus
}
#endif

#endif	// JKX_LAUNCHER_H
