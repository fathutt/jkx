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

// The numbers that used to be different in the two games, in one place.
//
// There is one engine now, so these cannot be a preprocessor branch: the
// configstring layout and the array bounds below are the shape of memory that
// crosses the engine/gamecode boundary, and a shape that depends on a define
// is the thing a single binary cannot have.
//
// So the wider of each pair wins. Both gamecode modules are ours, both are
// rebuilt against this header, and a limit that is too large costs a few
// kilobytes of configstring table while a limit that is too small truncates.
// Outcast never sets a skybox origin and never loads a sub-BSP model, so the
// two slots Academy needs for those simply stay empty when Outcast is running -
// an index that is never written is cheaper than an index that moves.
//
// Macros expand where they are used rather than where they are written, so the
// configstring layout below can be stated here even though CS_MODELS and
// MAX_MODELS are defined after this is included. That is what makes one table
// possible instead of four.

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
