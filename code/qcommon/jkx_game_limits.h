/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

#pragma once

// The numbers that are different in the two games, in one place.
//
// One engine built twice - jkx_jk2 is this code with -DJK2_MODE - and every
// difference between the games that is not data is a preprocessor branch.
// There are two hundred of them and tools/ci/check_jk2mode.py ratchets the
// count, but the count is not the point: a difference behind a conditional
// three hundred lines away from the next one cannot be read as a difference at
// all. It reads as a definition that happens to have an #ifdef near it.
//
// This is the shape that fixes that, and shared/win32/product.h is the
// precedent: one conditional, both games' values side by side, so the question
// "what is different about Outcast" has an answer you can point at.
//
// Macros expand where they are used rather than where they are written, so the
// configstring layout below can be stated here even though CS_MODELS and
// MAX_MODELS are defined after this is included. That is what makes one table
// possible instead of four.

#ifdef JK2_MODE

// Jedi Outcast.
#define JKX_MAX_SOUNDS			256
#define JKX_MAX_WORLD_FX		4
#define JKX_MAX_CONFIGSTRINGS	1024

// Outcast has no skybox origin and no sub-BSP models, so its configstrings sit
// two blocks lower than Academy's.
#define CS_SOUNDS				( CS_MODELS + MAX_MODELS )
#define CS_EFFECTS				( CS_LIGHT_STYLES + ( MAX_LIGHT_STYLES * 3 ) )

#else

// Jedi Academy.
#define JKX_MAX_SOUNDS			380
#define JKX_MAX_WORLD_FX		66		// was 16, then 4
#define JKX_MAX_CONFIGSTRINGS	1300	// 1024 until terrain needed more

#define CS_SKYBOXORG			( CS_MODELS + MAX_MODELS )
#define CS_SOUNDS				( CS_SKYBOXORG + 1 )
// CS_TERRAINS was between CS_LIGHT_STYLES and this, one slot wide, and nothing
// wrote it or read it: the only reader was a loop in cg_main.cpp over an empty
// range. Removing it moved every configstring after it down by one, which is a
// savegame format change and was meant to be - this engine's saves are its own,
// the retail format was given up deliberately, and a reserved slot for a
// feature cut before the game shipped is not worth carrying to keep a number
// the same.
#define CS_BSP_MODELS			( CS_LIGHT_STYLES + ( MAX_LIGHT_STYLES * 3 ) )
#define CS_EFFECTS				( CS_BSP_MODELS + MAX_SUB_BSP )

#endif // JK2_MODE
