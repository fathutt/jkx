/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
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

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// Filename:	persindex.h
//
// accessed from both server and game modules
//
// This is statindex.h's sibling and exists for the same reason: an array in
// playerState_t is sized in q_shared.h and indexed by an enumeration the server
// also has to name. q_shared.h has carried the warning about it for twenty
// years - "be careful about altering this because although it's used to define
// an array size, the entry indexes come from the typedef'd enum persEnum_t in
// bg_public.h, and there's no compile-tie between the 2 -slc".
//
// The server needed exactly one of these values, PERS_SCORE, and the way it got
// it was to include all of bg_public.h - 765 lines of weapon events, animation
// frames and entity flags, none of which the server has any business knowing.
// The note left in server.h when that was done reads "this won't do.. I need to
// include bg_public.h in the exe elsewhere. I'm including it here instead so we
// can have our PERS_SCORE value. And have it be the proper enum value." Two
// other engine files hit the same wall and gave up: both say "bg_public.h won't
// cooperate in here" and hard-code the number they wanted.
//
// The enumeration is byte for byte identical in the two games, which is what
// makes one copy of it correct rather than convenient. It lives beside q_math.h
// rather than in the gamecode because the engine is the lower layer and may not
// include upwards; q_shared.h pulls it in beside MAX_PERSISTANT, which is the
// array it indexes, and asserts there that the two still agree. Both games get
// it the same way, with no relative paths.

#ifndef PERSINDEX_H
#define PERSINDEX_H


// player_state->persistant[] indexes
// !!! PERS_SCORE MUST NOT CHANGE, SERVER AND GAME BOTH REFERENCE IT !!!
typedef enum {
	PERS_SCORE,
	PERS_HITS,						// total points damage inflicted so damage beeps can sound on change
	PERS_TEAM,
	PERS_SPAWN_COUNT,				// incremented every respawn
//	PERS_REWARD_COUNT,				// incremented for each reward sound
	PERS_ATTACKER,					// clientnum of last damage inflicter
	PERS_KILLED,					// count of the number of times you died

	PERS_ACCURACY_SHOTS,			// scoreboard - number of player shots
	PERS_ACCURACY_HITS,				// scoreboard - number of player shots that hit an enemy
	PERS_ENEMIES_KILLED,			// scoreboard - number of enemies player killed
	PERS_TEAMMATES_KILLED			// scoreboard - number of teammates killed
} persEnum_t;


#endif	// #ifndef PERSINDEX_H


/////////////////////// eof /////////////////////
