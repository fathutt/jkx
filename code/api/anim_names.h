/*
===========================================================================
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
===========================================================================
*/

// The animation vocabulary: the names, and nothing else.
//
// This is a contract because the assets are written against it. A .menu file
// names an animation as a string, models/players/<skeleton>/animation.cfg names
// them again, and the engine's menu code has to resolve both without a game
// module loaded - menus exist before any map does.
//
// What is deliberately *not* here is the animation enum, the animation_t struct
// or the per-game limits - the two games disagree about all three, and none of
// it is shared, because the engine never hands an animation_t to a game: it
// parses animation.cfg into its own struct and calls G2API_SetBoneAnim.
//
// So the interface is a table of names and a count. Each table is defined once
// in games/<game>/cgame/animtable.cpp.

#ifndef ANIM_NAMES_H
#define ANIM_NAMES_H

// Name to id and back, terminated by an entry with a NULL name. Two of them,
// and the engine links both: it resolves animation names out of .menu files
// before any game module is loaded, so it cannot wait to be told which game
// this is. Each gamecode module links only its own and names it directly.
extern stringID_table_t animTableAcademy[];
extern stringID_table_t animTableOutcast[];

// The running game's table, and how many entries it has - so that nothing
// outside a game needs that game's MAX_ANIMATIONS at compile time.
stringID_table_t *Anim_Table( void );
int Anim_Count( void );

#endif	// ANIM_NAMES_H
