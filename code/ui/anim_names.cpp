/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// Which animation name table the engine is looking at.
//
// The tables themselves are games/<game>/cgame/animtable.cpp, one per game, and
// this binary links both: the menu code turns animation names from .menu files
// into ids with no game module loaded, so the question "which game" is answered
// at runtime and not by a define.
//
// The count is cached per table rather than globally, because com_game is fixed
// before the first menu is parsed but a cached count keyed on nothing at all is
// the kind of thing that survives a design change badly.

#include "qcommon/q_shared.h"
#include "api/anim_names.h"

stringID_table_t *Anim_Table( void )
{
	return Com_IsOutcast() ? animTableOutcast : animTableAcademy;
}

int Anim_Count( void )
{
	static int academyCount = -1;
	static int outcastCount = -1;

	int *cached = Com_IsOutcast() ? &outcastCount : &academyCount;

	if ( *cached < 0 ) {
		const stringID_table_t *table = Anim_Table();
		int count = 0;

		while ( table[count].name ) {
			count++;
		}

		*cached = count;
	}

	return *cached;
}
