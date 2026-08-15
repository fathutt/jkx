/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// Where the games might be, without asking anybody.
//
// This produces CANDIDATE directories and nothing more. Deciding whether a
// directory holds Jedi Academy, Jedi Outcast or neither is jkx_install_scan.h's
// job, and it does it by looking inside the assets - so this half is allowed to
// be generous. A candidate that turns out to be a Half-Life install costs one
// failed archive open.
//
// Everything the outside world can answer arrives through installSearch_t: the
// registry, the contents of a directory, the text of a file. The Windows API
// does not appear below this line, which is what lets the whole search run
// against a machine that does not exist - see tests/install_find_test.cpp.

#ifndef JKX_INSTALL_FIND_H
#define JKX_INSTALL_FIND_H

#ifdef __cplusplus
extern "C" {
#endif

#define JKX_FIND_MAX_PATH		1024
#define JKX_FIND_MAX_CANDIDATES	64

typedef enum {
	JKX_REG_LOCAL_MACHINE = 0,
	JKX_REG_CURRENT_USER
} installRegRoot_t;

typedef struct {
	// A string value under a key. Returns 1 and fills out on success, 0 if the
	// key or value is not there.
	int		(*readRegistry)( void *user, installRegRoot_t root, const char *key,
							 const char *value, char *out, int outSize );

	// The names - not paths - of the subkeys of a key, and of the
	// subdirectories of a directory. Both return how many were written, and
	// write at most max, each at outStride bytes from the last.
	int		(*listSubKeys)( void *user, installRegRoot_t root, const char *key,
							char *out, int outStride, int max );
	int		(*listDirs)( void *user, const char *path,
						 char *out, int outStride, int max );

	// The whole of a small text file. Returns the length, or 0 if it is not
	// there or does not fit.
	int		(*readText)( void *user, const char *path, char *out, int outSize );

	void	*user;
} installSearch_t;

// Fills out with up to max directories worth looking at, most likely first,
// with duplicates removed. Returns how many were written.
//
// Every entry is a directory that MIGHT be a game installation. None of them
// has been opened.
int InstallFind_Candidates( const installSearch_t *search,
							char *out, int outStride, int max );

// Exposed for the test, because parsing a file format somebody else defined is
// the part most likely to be wrong and the part with the most interesting
// inputs. Writes library root directories - the folders that contain
// steamapps - and returns how many.
int InstallFind_ParseLibraryFolders( const char *text,
									 char *out, int outStride, int max );

#ifdef __cplusplus
}
#endif

#endif	// JKX_INSTALL_FIND_H
