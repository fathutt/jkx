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
// What is deliberately *not* here is the animation enum, the animation_t
// struct, or the per-game limits. The two games disagree about all three:
// Academy's animation_t packs into eight bytes and carries a glaIndex, Outcast's
// is five ints with an initialLerp, and the enums share no ordering at all. None
// of that is shared, because the engine never hands an animation_t to a game or
// takes one from it - it parses animation.cfg into its own struct and calls
// G2API_SetBoneAnim with the frame numbers.
//
// So the whole of the interface is a table of names and a count. The table is
// defined once per game in games/<game>/cgame/animtable.cpp, which is linked
// into that game's engine and its game library both.

#ifndef ANIM_NAMES_H
#define ANIM_NAMES_H

// Name to id and back. Terminated by an entry with a NULL name, so
// GetIDForString can walk it without being told how long it is.
extern stringID_table_t animTable[];

// How many entries the table has, not counting the terminator. This exists so
// that nothing outside a game needs that game's MAX_ANIMATIONS at compile time.
int Anim_Count( void );

#endif	// ANIM_NAMES_H
